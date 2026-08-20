#include "include/jpg_dec.h"
#include "include/mem.h"
#include "include/vsh_exports.h"

extern int32_t gpp; // games per page
static int32_t jpg_w = 0, jpg_h = 0;

static int32_t create_decoder(jpg_dec_info *dec_ctx);
static int32_t open_decoder(void);
static int32_t open_jpg(jpg_dec_info *dec_ctx, const char *file_path);
static int32_t set_dec_param(jpg_dec_info	*dec_ctx);
static int32_t decode_jpg_stream(jpg_dec_info	*dec_ctx, void *buf);
static void* cb_malloc(uint32_t size, void *cb_malloc_arg);
static int32_t cb_free(void *ptr, void *cb_free_arg);

// Declared here rather than in jpg_dec.h so this file stays self-contained; slaunch.c
// carries its own declaration. (If you'd rather put it in include/jpg_dec.h, drop this
// line and slaunch.c's - two declarations in one translation unit trip -Wredundant-decls.)
void jpg_dec_finalize(void);

/***********************************************************************
* persistent decoder
*
* cellJpgDecCreate() starts an SPU thread and cellJpgDecDestroy() tears it back down.
* That pair costs far more than decoding a cover does, and the stock code ran it around
* every single image - so drawing one page of the grid paid for it ten times over, forty
* in the 10x4 grid, plus once more for every background reload.
*
* Create/Destroy is the session-level pair in this API; Open/Close is the per-file pair.
* So the decoder is now created once, on first use, and released at shutdown.
***********************************************************************/
static jpg_dec_info jpg_dec;
static uint8_t      jpg_dec_ready = 0;

static int32_t open_decoder(void)
{
	if(jpg_dec_ready) return CELL_OK;

	if(create_decoder(&jpg_dec) != CELL_OK) return -1;

	jpg_dec_ready = 1;
	return CELL_OK;
}

// called from stop_VSH_Menu(), alongside font_finalize()
void jpg_dec_finalize(void)
{
	if(!jpg_dec_ready) return;

	cellJpgDecDestroy(jpg_dec.main_h);
	jpg_dec_ready = 0;
}

/***********************************************************************
* create jpg decoder
***********************************************************************/
static int32_t create_decoder(jpg_dec_info *dec_ctx)
{
	CellJpgDecThreadInParam   in;
	CellJpgDecThreadOutParam  out;

	// set params
	dec_ctx->cb_arg_j.mallocCallCounts	=
	dec_ctx->cb_arg_j.freeCallCounts	= 0;

	in.spuThreadEnable		= CELL_JPGDEC_SPU_THREAD_ENABLE; //CELL_JPGDEC_SPU_THREAD_DISABLE;   //
	in.ppuThreadPriority	= 512;
	in.spuThreadPriority	= 200;
	in.cbCtrlMallocFunc		= cb_malloc;
	in.cbCtrlMallocArg		= &dec_ctx->cb_arg_j;
	in.cbCtrlFreeFunc		= cb_free;
	in.cbCtrlFreeArg		= &dec_ctx->cb_arg_j;

	// create jpg decoder
	return cellJpgDecCreate(&dec_ctx->main_h, &in, &out);
}

/***********************************************************************
* open jpg stream
***********************************************************************/
static int32_t open_jpg(jpg_dec_info *dec_ctx, const char *file_path)
{
	CellJpgDecSrc      src;
	CellJpgDecOpnInfo  info;

	// set stream source
	src.srcSelect  = CELL_JPGDEC_FILE;        // source is file
	src.fileName   = file_path;               // path to source file
	src.fileOffset =
	src.fileSize   =
	src.streamSize = 0;
	src.streamPtr  = NULL;

	// spu thread disable
	src.spuThreadEnable = CELL_JPGDEC_SPU_THREAD_ENABLE;

	// open stream
	return cellJpgDecOpen(dec_ctx->main_h, &dec_ctx->sub_h, &src, &info);
}

/***********************************************************************
* set decode parameter
***********************************************************************/
static int32_t set_dec_param(jpg_dec_info	*dec_ctx)
{
	CellJpgDecInfo      info;
	CellJpgDecInParam   in;
	CellJpgDecOutParam  out;

	// read jpg header
	if(cellJpgDecReadHeader(dec_ctx->main_h, dec_ctx->sub_h, &info)!=CELL_OK) return -1;

	jpg_w = info.imageWidth;
	jpg_h = info.imageHeight;
	if(!jpg_w || !jpg_h || (!ISHD(jpg_w) && (jpg_w*jpg_h)>MAX_WH4)) return -1;

	// set decoder parameter
	in.commandPtr			= NULL;
	in.outputMode			= CELL_JPGDEC_TOP_TO_BOTTOM;
	in.outputColorSpace		= CELL_JPG_ARGB;
	in.method				= CELL_JPGDEC_FAST;//CELL_JPGDEC_QUALITY
	in.outputColorAlpha		= 0xFF;
	in.downScale			= 1;

	if(!ISHD(jpg_w) && ((jpg_w*jpg_h*4)>(MAX_WH4) || jpg_w>MAX_W || jpg_h>MAX_H || gpp==40))
	{
		if(gpp==10)
		{
			in.downScale = 2;
			jpg_w/=2; jpg_w+=(jpg_w%4);
			jpg_h/=2;
		}
		else
		if(jpg_w>168 || jpg_h>168)
		{
			in.downScale = 2;
			jpg_w/=2; jpg_w+=(jpg_w%2);
			jpg_h/=2;
		}
	}

	return cellJpgDecSetParameter(dec_ctx->main_h, dec_ctx->sub_h, &in, &out);
}

/***********************************************************************
* decode jpg stream
***********************************************************************/
static int32_t decode_jpg_stream(jpg_dec_info	*dec_ctx, void *buf)
{
	uint8_t *out;
	CellJpgDecDataCtrlParam  param;
	CellJpgDecDataOutInfo    info;

	param.outputBytesPerLine = jpg_w * 4;
	out = (void*)buf;

	// decode jpg...
	return cellJpgDecDecodeData(dec_ctx->main_h, dec_ctx->sub_h, out, &param, &info);
}

/***********************************************************************
* malloc callback
***********************************************************************/
static void *cb_malloc(uint32_t size, void *cb_malloc_arg)
{
	Cb_Arg_J *arg;
	arg = (Cb_Arg_J *)cb_malloc_arg;
	arg->mallocCallCounts++;
	return malloc(size);
}

/***********************************************************************
* free callback
***********************************************************************/
static int32_t cb_free(void *ptr, void *cb_free_arg)
{
	Cb_Arg_J *arg;
	arg = (Cb_Arg_J *)cb_free_arg;
	arg->freeCallCounts++;
	free(ptr);
	return 0;
}


/***********************************************************************
* decode jpg file
* const char *file_path  =  path to jpg file e.g. "/dev_hdd0/test.jpg"
***********************************************************************/
Buffer load_jpg(const char *file_path, void* buf_addr)
{
	Buffer tmp;
	tmp.addr = (uint32_t*)buf_addr;

	tmp.b= // no transparency
	tmp.w=tmp.h=
	tmp.x=tmp.y=
	jpg_w=jpg_h=0;

	if(!file_path || *file_path != '/') return tmp;

	// create the decoder on first use only - see the note above open_decoder()
	if(open_decoder()==CELL_OK)
	{
		// open jpg stream
		if(open_jpg(&jpg_dec, file_path)==CELL_OK)
		{
			// set decode parameter
			if(set_dec_param(&jpg_dec)==CELL_OK)
			{
				// decode jpg stream, into target buffer
				decode_jpg_stream(&jpg_dec, buf_addr);
				tmp.w = jpg_w;
				tmp.h = jpg_h;
			}

			// close jpg stream
			cellJpgDecClose(jpg_dec.main_h, jpg_dec.sub_h);
		}
	}

	return tmp;
}
