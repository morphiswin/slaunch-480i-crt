#include "include/pad.h"
#include "include/network.h"

#include "include/misc.h"
#include "include/mem.h"
#include "include/blitting.h"

SYS_MODULE_INFO(sLaunch, 0, 1, 1);
SYS_MODULE_START(slaunch_start);
SYS_MODULE_STOP(slaunch_stop);

#define THREAD_NAME         "slaunch_thread"
#define STOP_THREAD_NAME    "slaunch_stop_thread"

#define STR_ALLGAMES	"All Games"
#define STR_FAVORITES	"Favorites"
#define STR_UNMOUNT		"Unmount"
#define STR_REFRESH		"Refresh"
#define STR_GAMEDATA	"gameDATA"
#define STR_SYSCALLS	"CFW Syscalls"
#define STR_RESTART		"Restart"
#define STR_SHUTDOWN	"Shutdown"
#define STR_SETUP		"Setup"
#define STR_FILEMNGR	"File Manager"
#define STR_UNLOAD		"Unload webMAN"
#define STR_QUIT		"Quit"

#define APP_VERSION		"1.19h"

// Info bar layout (SD). The strip is INFOBAR_H canvas rows and sits between the two
// rules: the title takes the top half, the counter the bottom half, and the category
// label sits alone on the left so it gets the full height.
//
// print_text() puts the baseline at y + baseLineY. Measured off a capture of this font, a
// capital runs from about y + 0.28*font_h down to the baseline at y + 0.98*font_h, and
// descenders reach roughly y + 1.25*font_h. So to centre the visible text in a band of
// height B starting at BY:  y = BY + B/2 - 0.63*font_h.  The Y values below are that,
// worked out. Change a size and the Y follows the same rule - and note the title cannot
// go much past 38 before its centred position would go negative in a 48-row half.
#define BAR_TITLE_W 38.f
#define BAR_TITLE_H 38.f
#define BAR_TITLE_Y 0         // ink 11..37 of the top half (0..47)

#define BAR_COUNT_W 36.f
#define BAR_COUNT_H 36.f
#define BAR_COUNT_Y 49        // ink 59..84 of the bottom half (48..95)

#define BAR_CAT_W   64.f
#define BAR_CAT_H   72.f        // capped by GLYPH_H in blitting.c
#define BAR_CAT_Y   2         // ink 22..73 of the full strip (0..95)
#define BAR_CAT_X   80

typedef struct {
	uint8_t  gmode;
	uint8_t  dmode;
	uint8_t  gpp;
	uint8_t  fav_mode;
	uint16_t cur_game;
	uint16_t cur_game_;
	uint16_t fav_game;
	uint16_t padd0;
	uint64_t padd1;
} _sconfig;

#define IS_ON_XMB			(vshmain_EB757101() == 0)
#define IS_INGAME			(vshmain_EB757101())
#define TOGGLE(var)			var ^= 1

#define MAX_GAMES 3500
#define WMTMP				"/dev_hdd0/tmp/wmtmp"				// webMAN work/temp folder
#define WM_ICONS_PATH		"/dev_hdd0/tmp/wm_icons"			// webMAN icons folder
#define RB_ICONS_PATH		"/dev_flash/vsh/resource/explore/icon"

#define XMLMANPLS_DIR		"/dev_hdd0/game/XMBMANPLS"
#define XMLMANPLS_IMAGES_DIR XMLMANPLS_DIR "/USRDIR/IMAGES"

#define TYPE_ALL 0
#define TYPE_PSX 1
#define TYPE_PS2 2
#define TYPE_PS3 3
#define TYPE_PSP 4
#define TYPE_VID 5
#define TYPE_ROM 6
#define TYPE_FAV 7
#define TYPE_MAX 8

// The SQUARE cycle, in order. Add or remove entries here and everything follows.
static const uint8_t cat_order[] = {TYPE_PSX, TYPE_PS2, TYPE_PS3};
#define CAT_COUNT (sizeof(cat_order) / sizeof(cat_order[0]))

static uint8_t next_cat(uint8_t m)
{
	for(uint32_t i = 0; i < CAT_COUNT; i++) if(cat_order[i] == m) return cat_order[(i + 1) % CAT_COUNT];
	return cat_order[0];
}

// (a prev_cat() mirror of the above used to live here - nothing calls it now that the
//  category only ever cycles forward, on SQUARE and on the empty-category retry)

// same order, with Favorites tacked on the end (side menu only)
static uint8_t next_sm(uint8_t m)
{
	if(m == TYPE_FAV) return cat_order[0];
	for(uint32_t i = 0; i < CAT_COUNT; i++) if(cat_order[i] == m) return (i + 1 < CAT_COUNT) ? cat_order[i + 1] : TYPE_FAV;
	return cat_order[0];
}

static uint8_t prev_sm(uint8_t m)
{
	if(m == TYPE_FAV) return cat_order[CAT_COUNT - 1];
	for(uint32_t i = 0; i < CAT_COUNT; i++) if(cat_order[i] == m) return (i > 0) ? cat_order[i - 1] : TYPE_FAV;
	return TYPE_FAV;
}


static char game_type[TYPE_MAX][10]=
{
	"\0",
	"PSX",
	"PS2",
	"PS3",
	"PSP",
	"video",
	"ROMS",
	STR_FAVORITES
};

static const char *cat_name(uint8_t m)
{
	if(m >= TYPE_MAX) return "\0";
	return game_type[m];
}

static char wm_icons[7][56] =	{
									RB_ICONS_PATH "/icon_wm_ps3.png",       //020.png  [0]
									RB_ICONS_PATH "/icon_wm_psx.png",       //021.png  [1]
									RB_ICONS_PATH "/icon_wm_ps2.png",       //022.png  [2]
									RB_ICONS_PATH "/icon_wm_ps3.png",       //023.png  [3]
									RB_ICONS_PATH "/icon_wm_psp.png",       //024.png  [4]
									RB_ICONS_PATH "/icon_wm_dvd.png",       //025.png  [5] (vid)
									RB_ICONS_PATH "/icon_wm_retro.png",     //026.png  [6] (roms)
								};

#define SLIST	"slist"
/*
typedef struct // 1MB for 2000+1 titles
{
	uint8_t type;
	char id[10];
	char name[141]; // 128+12+1 for added ' [BXXX12345]'
	char icon[160];
	char path[160];
	char padd[52];
} __attribute__((packed)) _slaunch;
*/

typedef struct // 1MB for 2000+1 titles
{
	uint8_t  type;
	char     id[10];
	uint8_t  path_pos; // start position of path
	uint16_t icon_pos; // start position of icon
	uint16_t padd;
	char     name[508]; // name + path + icon
} __attribute__((packed)) _slaunch;

static _slaunch *slaunch = NULL;

#define GRAY_TEXT   0xff808080
#define LIGHT_TEXT  0xffa0a0a0
#define WHITE_TEXT  0xffc0c0c0
#define BRIGHT_TEXT 0xffe0e0e0

#define RED         0xffc00000ffc00000
#define BLUE        0xff0000ffff0000ff
#define DARK_RED    0xff500000ff500000
#define DARK_BLUE   0xff000050ff000050
#define DARK_GRAY   0xff606060ff606060
#define GRAY        0xff808080ff808080
#define LIGHT_GRAY  0xffa0a0a0ffa0a0a0

// globals
static uint16_t init_delay = 0;
static uint16_t games_count = 0;
static uint16_t cur_game = 0, cur_game_ = 0, fav_game = 0;
static uint8_t do_once = 1;

uint32_t disp_w = 0;
uint32_t disp_h = 0;
uint32_t gpp = 10;

uint8_t web_page = 0;
uint8_t read_pad = 1;

static uint8_t key_repeat = 0, can_skip = 0;

static uint8_t opt_mode = 0;
static uint8_t unload_mode = 1;

static uint64_t tick = 0x80;
static int8_t  delta = 10;

#define SYS_PPU_THREAD_NONE        (sys_ppu_thread_t)NONE

static sys_ppu_thread_t slaunch_tid = SYS_PPU_THREAD_NONE;
static int32_t running = 1;
static uint8_t slaunch_running = 0;	// vsh menu off[0] or on[1]
static uint8_t fav_mode = 0;

static void return_to_xmb(void);
int32_t slaunch_start(uint64_t arg);
int32_t slaunch_stop(void);

static void finalize_module(void);
static void slaunch_stop_thread(uint64_t arg);
static void slaunch_thread(uint64_t arg);

int32_t save_screenshot(const char *path);	// blitting.c - dumps the framebuffer to a BMP

void jpg_dec_finalize(void);	// jpg_dec.c - releases the shared decoder + its SPU thread
void png_dec_finalize(void);	// png_dec.c - same, for the png decoder

static uint8_t draw_side_menu(void);
static void letter_jump(void);
static void draw_selection(uint16_t game_idx);
static void reload_data(uint32_t curpad);

#define HDD0  1
#define NTFS  2

#define DEVS_MAX 4

static char drives[DEVS_MAX][12] = {"dev_hdd0", "ntfs", "dev_usb", "net"};

static uint8_t gmode = TYPE_PSX;   // must be one of cat_order[]
static uint8_t dmode = TYPE_ALL;
static uint8_t cpu_rsx = 0;

static uint32_t frame = 0;

static void load_background(void)
{
	// neither branch below runs when !fav_mode && !gmode, and the not_exists() that
	// follows would then be handed an uninitialised (and possibly unterminated) buffer
	char path[64]; path[0] = 0;

	if(fav_mode)
		sprintf(path, "%s_fav.jpg", "/dev_hdd0/tmp/wm_res/images/slaunch");
	else if(gmode)
	{
		sprintf(path, "%s_%s.jpg", "/dev_hdd0/tmp/wm_res/images/slaunch", cat_name(gmode));

		if(!gmode || not_exists(path)) sprintf(path, "%s.jpg", "/dev_hdd0/plugins/images/slaunch");
	}

	if(not_exists(path))
	{
		if(fav_mode)
			sprintf(path, "/dev_flash/vsh/resource/explore/icon/cinfo-bg-%s","whatsnew.jpg");
		else if(!gmode)
			sprintf(path, "/dev_flash/vsh/resource/explore/icon/cinfo-bg-%s","storemain.jpg");
		else
			sprintf(path, "/dev_flash/vsh/resource/explore/icon/cinfo-bg-%s","storegame.jpg");
	}

	load_img_bitmap(0, path, "\0");
}

/***********************************************************************
* L3+R3 screen capture
*
* Writes /dev_hdd0/slaunch_NN.bmp, picking the first free number so repeated grabs don't
* overwrite each other. Nothing is drawn before the capture, so what lands in the file is
* exactly what was on the TV - side menu, letter panel or plain grid.
***********************************************************************/
static void take_screenshot(void)
{
	char path[40];

	for(uint32_t n = 0; n < 100; n++)
	{
		sprintf(path, "/dev_hdd0/slaunch_%02i.bmp", n);

		if(not_exists(path))
		{
			if(save_screenshot(path) == CELL_FS_SUCCEEDED) play_rco_sound("snd_system_ok");
			else                                           play_rco_sound("snd_cancel");
			return;
		}
	}

	play_rco_sound("snd_cancel");	// 100 already sitting there
}

static void draw_page(uint16_t game_idx, uint8_t key_repeat)
{
	if(game_idx >= games_count) game_idx = 0;

	if(!games_count) return;

	// Row geometry. The old code used left_margin as BOTH the starting margin and the gap
	// between cells, which cannot balance: 5 cells of 320 with a 56 gap need 1824 of the
	// 1920, so the margin has to be the leftover halved (48), not 56 - which is why the
	// grid sat 16px right of centre. Same for the 10x4 grid, 8px out.
	const int cell_w  = (gpp == 10) ? 320 : 160;
	const int col_gap = (gpp == 10) ?  56 :  24;
	const int col_n   = (gpp == 10) ?   5 :  10;
	const int left_margin = (CANVAS_W - (col_n * cell_w) - ((col_n - 1) * col_gap)) / 2;

	uint8_t slot = 0;
	uint16_t i, j, p;
	int px=left_margin, py;	// top-left

	// draw background and menu strip
	flip_frame();
	memcpy((uint8_t *)ctx.menu, (uint8_t *)(ctx.canvas) + INFOBAR_Y * CANVAS_W * 4, CANVAS_W * INFOBAR_H * 4);

	// Colour order reversed from stock. These are 1-row rules widened to 5 on SD and drawn
	// in sequence, so the LAST colour ends up covering three of the four output rows and
	// the first covers one. Stock put LIGHT first, giving one bright row over three dark
	// ones - and since the background brightens left to right, those dark rows stop
	// contrasting against it partway across and the rule appears to thin out. Dark first,
	// light last gives three bright rows, the same way round as the bottom rule below, so
	// it reads at a constant thickness the whole way across.
	set_textbox(DARK_GRAY,  0, INFOBAR_Y - 10, CANVAS_W, 1);
	set_textbox(GRAY,       0, INFOBAR_Y - 9,  CANVAS_W, 1);
	set_textbox(LIGHT_GRAY, 0, INFOBAR_Y - 8,  CANVAS_W, 1);

	set_textbox(DARK_GRAY,  0, INFOBAR_Y + INFOBAR_H + 2, CANVAS_W, 1);
	set_textbox(GRAY,       0, INFOBAR_Y + INFOBAR_H + 3, CANVAS_W, 1);
	set_textbox(LIGHT_GRAY, 0, INFOBAR_Y + INFOBAR_H + 4, CANVAS_W, 1);

	draw_selection(game_idx);

	// draw game icons (5x2) or (10x4)
	j = (game_idx / gpp) * gpp;
	p = (games_count-1) - ((games_count-1) % gpp);
	for(i = j; slot < gpp; i++)
	{
		if(i >= games_count) break;

		// abort drawing if page is changed with L1 or R1 while processing
		if(i & 1)
		{
			MyPadGetData(pad_port, &pdata);  // poll last pad port only

			if(pdata.len > 0)
			{
				curpad = (pdata.button[2] | (pdata.button[3] << 8)); oldpad = read_pad = 0;

				if(++init_delay > 4)
				{
					if((curpad & PAD_L1) && (i > gpp)) break;
					if((curpad & PAD_R1) && (i < p)) break;
					if(curpad & PAD_TRIANGLE)
					{
						uint8_t option = draw_side_menu();
						if(option) break;

						// The menu leaves itself on screen now (see its tail), so repaint
						// exactly the way the main loop does and abandon this half-finished
						// page draw. Without this, closing the menu from here carried on
						// painting icons around the panel that was still up.
						load_background();
						draw_page(game_idx, 0);
						return;
					}
				}
			}
		}

		if(load_img_bitmap(++slot, slaunch[i].name + slaunch[i].icon_pos, wm_icons[slaunch[i].type])<0) break;

		if(gpp == 10)
		{
			py=((i-j)/5)*400+90+((300-ctx.img[slot].h)>>1);
			ctx.img[slot].x=((px+((320-ctx.img[slot].w)>>1))>>1)<<1;
			ctx.img[slot].y=py;
			set_backdrop(slot, 0);
			set_texture(slot, ctx.img[slot].x, ctx.img[slot].y);
			px+=(cell_w+col_gap); if(px>1600) px=left_margin;
		}
		else
		{
			py=((i-j)/10)*200+90+((150-ctx.img[slot].h)>>1);
			ctx.img[slot].x=((px+((160-ctx.img[slot].w)>>1))>>1)<<1;
			ctx.img[slot].y=py;
			set_backdrop(slot, 0);
			set_texture(slot, ctx.img[slot].x, ctx.img[slot].y);
			px+=(cell_w+col_gap); if(px>1800) px=left_margin;
		}

		if(key_repeat) break;
	}
}

static void draw_selection(uint16_t game_idx)
{
	char one_of[32], mode[12];	// mode[8] was one byte short of the longest cat_name()

	if(game_idx < games_count)
	{
		// game name. webMAN builds this as "folder/file", which for a collection laid
		// out one-folder-per-game is just the same title twice - keep the last part.
		char *title = slaunch[game_idx].name;
		for(char *p = slaunch[game_idx].name; *p && (p < (slaunch[game_idx].name + slaunch[game_idx].path_pos)); p++)
			if(*p == '/') title = p + 1;

		if(ISHD(disp_w))	set_font(32.f, 32.f, 1.0f, 1);
		else				set_font(BAR_TITLE_W, BAR_TITLE_H, 3.0f, 1);
		ctx.fg_color = WHITE_TEXT;
		print_text(ctx.menu, CANVAS_W, CENTER_TEXT, ISHD(disp_w) ? 0 : BAR_TITLE_Y, title );
	}

	// game index
	// sits right under the title - the old y=64 left a hole where the path line used to be
	if(ISHD(disp_w))	set_font(24.f, 24.f, 1.0f, 1);
	else				set_font(BAR_COUNT_W, BAR_COUNT_H, 2.0f, 1);
	ctx.fg_color=LIGHT_TEXT;
	sprintf(one_of, "%i / %i", game_idx + 1, games_count);
	print_text(ctx.menu, CANVAS_W, CENTER_TEXT, ISHD(disp_w) ? 44 : BAR_COUNT_Y, one_of );

	// game list mode - sits alone on the left, so it gets the full height of the bar
	if(gmode) sprintf(mode, "%s", cat_name(gmode));

	if(!ISHD(disp_w)) set_font(BAR_CAT_W, BAR_CAT_H, 2.0f, 1);

	uint32_t cat_y = ISHD(disp_w) ? 44 : BAR_CAT_Y;

	if(fav_mode)
		print_text(ctx.menu, CANVAS_W, BAR_CAT_X, cat_y, "Favorites");
	else if(dmode && gmode)
	{
		sprintf(one_of, "/%s/%s", drives[dmode-1], mode);
		print_text(ctx.menu, CANVAS_W, BAR_CAT_X, cat_y, one_of);
	}
	else if(dmode)
		print_text(ctx.menu, CANVAS_W, BAR_CAT_X, cat_y, drives[dmode-1]);
	else if(gmode)
		print_text(ctx.menu, CANVAS_W, BAR_CAT_X, cat_y, mode);

	// temperature
	if(ISHD(disp_w) || (disp_h==720))
	{
		char s_temp[64];
		uint32_t temp_c = 0, temp_f = 0;
		get_temperature(cpu_rsx, &temp_c);
		temp_f = (uint32_t)(1.8f * (float)temp_c + 32.f);
		sprintf(s_temp, "%s :  %i C  /  %i F", cpu_rsx ? "RSX" : "CPU", temp_c, temp_f);
		ctx.fg_color = WHITE_TEXT;
		print_text(ctx.menu, CANVAS_W, CANVAS_W - ((disp_h==720) ? 450 : 300), 64, s_temp);
	}

	// set frame buffer for menu strip
	set_texture_direct(ctx.menu, 0, INFOBAR_Y, CANVAS_W, INFOBAR_H);
	memcpy((uint8_t *)ctx.menu, (uint8_t *)(ctx.canvas)+INFOBAR_Y*CANVAS_W*4, CANVAS_W*INFOBAR_H*4);
}

static uint8_t cur_mode;

static void draw_side_menu_option(uint8_t option)
{
	//memset((uint8_t *)ctx.side, 0x40, SM_M);
	memset32((uint32_t *)ctx.side, 0x40404040, SM_M/4);
	ctx.fg_color=BRIGHT_TEXT;
	set_font(28.f, 24.f, 1.f, 0); print_text(ctx.side, (CANVAS_W-SM_X), SM_TO, SM_Y, "sLaunch MOD " APP_VERSION);

	ctx.fg_color=(option == 1 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 1)*32, SM_Y + 4 * 24, (cur_mode >= TYPE_FAV) ? STR_FAVORITES :
																					cur_mode ? cat_name(cur_mode) : STR_ALLGAMES);
	ctx.fg_color=(option == 2 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 2)*32, SM_Y + 6 * 24, STR_UNMOUNT);
	ctx.fg_color=(option == 3 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 3)*32, SM_Y + 8 * 24, STR_REFRESH);
	ctx.fg_color=(option == 4 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 4)*32, SM_Y + 10 * 24, opt_mode ? STR_SYSCALLS : STR_GAMEDATA);
	ctx.fg_color=(option == 5 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 5)*32, SM_Y + 12 * 24, STR_RESTART);
	ctx.fg_color=(option == 6 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 6)*32, SM_Y + 14 * 24, STR_SHUTDOWN);
	ctx.fg_color=(option == 7 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 7)*32, SM_Y + 16 * 24, unload_mode ? STR_UNLOAD : STR_QUIT);
	ctx.fg_color=(option == 8 ? WHITE_TEXT : GRAY_TEXT);
	print_text(ctx.side, (CANVAS_W - SM_X), SM_TO + (option != 8)*32, SM_Y + 22 * 24, (web_page == 1) ? STR_FILEMNGR :
																			(web_page == 0) ? STR_SETUP :
																			(gpp == 10) ? "Grid 10x4" : "Grid 5x2");

	set_texture_direct(ctx.side, SM_X, 0, (CANVAS_W - SM_X), CANVAS_H);
	set_textbox(GRAY, SM_X + SM_TO, SM_Y + 40,    CANVAS_W - SM_X - SM_TO * 2, 1);
	set_textbox(GRAY, SM_X + SM_TO, SM_Y + 20*24, CANVAS_W - SM_X - SM_TO * 2, 1);
}

// '#' collects anything that doesn't start with a letter (numbers, brackets, etc.)
static const char letters[] = "#ABCDEFGHIJKLMNOPQRSTUVWXYZ";

#define NUM_LETTERS 27
#define LETTER_ROWS 27
#define LETTER_STEP 37

// The letter panel only ever shows one character per row, so it has no use for the side
// menu's width. Keeping it to a narrow strip pinned at the right edge leaves the covers
// underneath visible instead of blanking a fifth of the screen. LJ_W is in canvas pixels;
// 160 of those land on about 60 of a 720-pixel line.
#define LJ_W        160
#define LJ_X        (CANVAS_W - LJ_W)
#define LJ_TO       68        // letter indent - the selected row sits 24px left of this
#define LJ_TITLE_X  29        // "Jump to" centred: (LJ_W - ~102) / 2 at font_w 28

// Vertical layout of the strip, in canvas rows. Sized so the blank above the heading and
// the blank below the last letter come out about equal (~23 canvas rows each) with the
// column filling the height between them, instead of the heading starting a third of the
// way down the panel. LETTER_STEP spreads the 27 rows over what is left.
#define LJ_TITLE_Y  16
#define LJ_SEP_Y    (LJ_TITLE_Y + 44)
#define LJ_FIRST_Y  (LJ_TITLE_Y + 56)
static void draw_letter_menu(uint8_t sel)
{
	// only the strip is used - ctx.side is allocated for the wider side menu, so LJ_W
	// rows of it are plenty. Everything below uses LJ_W as the buffer stride.
	memset32((uint32_t *)ctx.side, 0x40404040, LJ_W * CANVAS_H);

	// CENTER_TEXT centres against the full canvas width, not the buffer's, so it can't be
	// used in here - the title gets an explicit x and a smaller font than the rows, since
	// "Jump to" at the row size would run past the edge of a strip this narrow.
	set_font(28.f, 26.f, 1.f, 0);
	ctx.fg_color = BRIGHT_TEXT;
	print_text(ctx.side, LJ_W, LJ_TITLE_X, LJ_TITLE_Y, "Jump to");

	// same font the side menu uses - a larger one renders glyphs past the 32x32
	// slot render_glyph() copies into without bounds checking
	set_font(28.f, 24.f, 1.f, 0);

	for(uint8_t i = 0; i < NUM_LETTERS; i++)
	{
		char s[2] = {letters[i], 0};
		ctx.fg_color = (i == sel) ? WHITE_TEXT : GRAY_TEXT;
		print_text(ctx.side, LJ_W,
		           LJ_TO - (i == sel) * 24,
		           LJ_FIRST_Y + (i * LETTER_STEP), s);
	}

	set_texture_direct(ctx.side, LJ_X, 0, LJ_W, CANVAS_H);
	set_textbox(GRAY, LJ_X + 16, LJ_SEP_Y, LJ_W - 32, 1);
}

// first character of a title, folded to '#' / 'A'-'Z'
static char title_initial(uint16_t n)
{
	const char *t = slaunch[n].name;

	// skip the folder half of webMAN's "folder/file" name, same as the info bar
	for(const char *p = slaunch[n].name; *p && (p < (slaunch[n].name + slaunch[n].path_pos)); p++)
		if(*p == '/') t = p + 1;

	char ch = *t;
	if((ch >= 'a') && (ch <= 'z')) ch -= 0x20;
	return ((ch >= 'A') && (ch <= 'Z')) ? ch : '#';
}

static void letter_jump(void)
{
	if(!games_count) return;

	uint8_t sel = 0;

	// start on the letter the cursor is already sitting in
	{
		char ch = title_initial(cur_game);
		for(uint8_t i = 0; i < NUM_LETTERS; i++) if(letters[i] == ch) {sel = i; break;}
	}

	play_rco_sound("snd_cursor");

	dump_bg(LJ_X - 6, 0, LJ_W + 6, CANVAS_H);

	set_textbox(0xffe0e0e0ffd0d0d0, LJ_X - 6, 0, 2, CANVAS_H);
	set_textbox(0xffc0c0c0ffb0b0b0, LJ_X - 4, 0, 2, CANVAS_H);
	set_textbox(0xffa0a0a0ff909090, LJ_X - 2, 0, 2, CANVAS_H);

	draw_letter_menu(sel);

	wait_for_button_release(PAD_R3);

	while(slaunch_running)
	{
		pad_read();

		if(curpad)
		{
			if(curpad == oldpad)
			{
				if(++init_delay <= 20) continue;
				sys_timer_usleep(40000); key_repeat = 1;
			}
			else init_delay = key_repeat = 0;

			oldpad = curpad;

			if((curpad & PAD_L3) && (curpad & PAD_R3))	// screen capture
			{
				take_screenshot();
				wait_for_button_release(PAD_L3 | PAD_R3);
				oldpad = 0; continue;
			}

			if(curpad & PAD_UP)		{if(!sel) sel = NUM_LETTERS - 1; else sel--;}
			if(curpad & PAD_DOWN)	{if(++sel >= NUM_LETTERS) sel = 0;}
			if(curpad & PAD_LEFT)	{if(sel < 5) sel = 0; else sel -= 5;}
			if(curpad & PAD_RIGHT)	{sel = ((sel + 5) >= NUM_LETTERS) ? (NUM_LETTERS - 1) : (sel + 5);}

			if((curpad & PAD_R3) || (curpad & PAD_CIRCLE) || (curpad & PAD_TRIANGLE))
				{play_rco_sound("snd_cancel"); break;}

			if(curpad & PAD_CROSS)
			{
				// list is sorted, so the first match is the start of that letter
				uint16_t hit = games_count;
				for(uint16_t n = 0; n < games_count; n++)
					if(title_initial(n) == letters[sel]) {hit = n; break;}

				if(hit < games_count) {play_rco_sound("snd_system_ok"); cur_game = hit;}
				else                   play_rco_sound("snd_cancel");
				break;
			}

			play_rco_sound("snd_cursor");
			draw_letter_menu(sel);
		}
		else init_delay = oldpad = 0;
	}

	init_delay = key_repeat = 0;
}

static uint8_t draw_side_menu(void)
{
	uint8_t option = 1;
	play_rco_sound("snd_cursor");

	dump_bg(SM_X - 6, 0, (CANVAS_W - SM_X) + 6, CANVAS_H);

	set_textbox(0xffe0e0e0ffd0d0d0, SM_X - 6, 0, 2, CANVAS_H);
	set_textbox(0xffc0c0c0ffb0b0b0, SM_X - 4, 0, 2, CANVAS_H);
	set_textbox(0xffa0a0a0ff909090, SM_X - 2, 0, 2, CANVAS_H);


	cur_mode =  fav_mode ? gmode : TYPE_FAV;

	draw_side_menu_option(option);

	wait_for_button_release(PAD_TRIANGLE);

	while(slaunch_running)
	{
		pad_read();

		if(curpad)
		{
			if(curpad == oldpad)	// key-repeat
			{
				if(++init_delay <= 20) continue;
				sys_timer_usleep(40000); key_repeat = 1;
			}
			else
			{
				init_delay = key_repeat = 0;
			}

			oldpad = curpad;

			if((curpad & PAD_L3) && (curpad & PAD_R3))	// screen capture
			{
				take_screenshot();
				wait_for_button_release(PAD_L3 | PAD_R3);
				oldpad = 0; continue;
			}

			if(curpad & (PAD_LEFT | PAD_RIGHT))
			{
				if(option == 1)
				{
					if(curpad & PAD_LEFT ) do {cur_mode = prev_sm(cur_mode);} while(!fav_mode && (cur_mode == gmode));
					if(curpad & PAD_RIGHT) do {cur_mode = next_sm(cur_mode);} while(!fav_mode && (cur_mode == gmode));
				}
				if(option == 4) TOGGLE(opt_mode);	// opt_mode    ? STR_SYSCALLS : STR_GAMEDATA
				if(option == 7) TOGGLE(unload_mode); // unload_mode ? STR_UNLOAD   : STR_QUIT
				if(option == 8)
				{
					if(curpad & PAD_LEFT ) {if(web_page <= 0) web_page = 2; else --web_page;}
					if(curpad & PAD_RIGHT) {if(web_page >= 2) web_page = 0; else ++web_page;}
				}
			}

			if(curpad & PAD_UP)		option--;
			if(curpad & PAD_DOWN)	option++;

			if(option < 1) option = 8;
			if(option > 8) option = 1;

			if(curpad & PAD_TRIANGLE || curpad & PAD_CIRCLE) {option = 0; play_rco_sound("snd_cancel"); break;}

			if(curpad & PAD_CROSS) {play_rco_sound("snd_system_ok"); break;}

			play_rco_sound("snd_cursor");
			draw_side_menu_option(option);
		}
		else
		{
			init_delay=oldpad = 0;
		}
	}
	init_delay = key_repeat = 0;

	// letter_jump() restores nothing and reloads nothing at this point, and that is exactly
	// why the letter panel closes clean: the panel just stays on screen until the caller's
	// flip_frame() rewrites every pixel in one go, so nothing half-done is ever shown.
	// Restoring the saved strip here instead wiped the panel early, left its left-edge
	// divider standing, and then sat on that while load_background() decoded the background
	// - which is the white line, and the second it hangs around for. Both callers below now
	// do load_background() + draw_page(), the same two calls the letter panel already made.

	if(option)
	{
		// toggle grid
		if((option == 8) && (web_page >= 2))
		{
			gpp ^= 34;	// the caller reloads the background and repaints; ctx.canvas
			web_page = curpad = init_delay = 0;	// still holds the strip dump_bg() saved
			return 0;
		}

		if(option == 1)
		{
			if(cur_mode >= TYPE_FAV)
				{oldpad = curpad = PAD_SELECT, dmode = TYPE_ALL, read_pad = 0, init_delay = 20;}
			else
				{fav_mode = 0, gmode = cur_mode, dmode=TYPE_ALL; reload_data(curpad);}
			return option;
		}
		if(option == 2) send_wm_request("/mount_ps3/unmount");
		if(option == 3) send_wm_request("/refresh_ps3");
		if(option == 4)
		{
			if(opt_mode)
			{
				send_wm_request("/popup.ps3?CFW%20Syscalls%20disabled!");
				send_wm_request("/syscall.ps3mapi?scd=1");
			}
			else
				send_wm_request("/extgd.ps3");
		}
		return_to_xmb();
		if(option == 5) send_wm_request("/restart.ps3");
		if(option == 6) send_wm_request("/shutdown.ps3");
		if(option == 7)
		{
			send_wm_request("/popup.ps3?sLaunch%20unloaded!");

			if(unload_mode)
				send_wm_request("/quit.ps3");
			//else
			//	send_wm_request("/unloadprx.ps3?prx=sLaunch");

			running = 0;
		}
		if(option == 8)
		{
			send_wm_request(web_page ? "/browser.ps3/" : "/browser.ps3/setup.ps3");
		}
	}

	return option;
}

static void show_no_content(void)
{
	char no_content[32];
	if(gmode >= TYPE_FAV)
		{sprintf(no_content, "webMAN is not loaded."); gmode = TYPE_PSX;}
	else
		sprintf(no_content, "There are no %s%stitles.", cat_name(gmode), gmode ? " " : "\0");

	ctx.fg_color = WHITE_TEXT;
	set_font(32.f, 32.f, 1.5f, 1); print_text(ctx.canvas, CANVAS_W, 0, 520, no_content);
	flip_frame();

	sys_timer_usleep(1500000);

	if(gmode) load_background();
}

static void show_content(void)
{
	slaunch_running = 1;

	if(games_count)
		draw_page(cur_game, 0);
	else
		show_no_content();
}

static void load_config(void)
{
	int fd;
	_sconfig sconfig;

	if(cellFsOpen(WMTMP "/slaunch.cfg", CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		cellFsRead(fd, (void *)&sconfig, sizeof(_sconfig), 0);
		cellFsClose(fd);

		gmode     = sconfig.gmode;
		{uint8_t ok = 0; for(uint32_t i = 0; i < CAT_COUNT; i++) if(cat_order[i] == gmode) ok = 1;
		 if(!ok) gmode = cat_order[0];}   // clamp old/out-of-range saved values
		dmode     = TYPE_ALL;   // device filter removed - never restore a saved value
		fav_mode  = sconfig.fav_mode;
		fav_game  = sconfig.fav_game;
		cur_game  = sconfig.cur_game;
		cur_game_ = sconfig.cur_game_;
		gpp       = sconfig.gpp;
		if((gpp != 10) && (gpp != 40)) gpp = 10; // only 5x2 (10) and 10x4 (40) are valid
	}
}

static void save_config(void)
{
	_sconfig sconfig;

	sconfig.gmode    = gmode;
	sconfig.dmode    = dmode;
	sconfig.gpp      = gpp;
	sconfig.fav_mode = fav_mode;
	sconfig.fav_game = fav_game;
	sconfig.cur_game = cur_game;
	sconfig.cur_game_= cur_game_;
	sconfig.padd1 =
	sconfig.padd0 = 0;
	int fd;

	if(cellFsOpen(WMTMP "/slaunch.cfg", CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		cellFsWrite(fd, (void *)&sconfig, sizeof(_sconfig), NULL);
		cellFsClose(fd);
	}
}

// case-insensitive substring search. libc.c has strcasestr(), but no header visible to
// this file declares it - calling it would implicitly return int and truncate the pointer.
static char *stristr(const char *hay, const char *needle)
{
	size_t nlen = strlen(needle);
	for(; *hay; hay++)
		if(!strncasecmp(hay, needle, nlen)) return (char *)hay;
	return NULL;
}

static _slaunch sort_tmp;	// scratch for the sort - kept off the 8KB thread stack

// Lift the element out once, shift the winning children up over it, drop it in at the
// end - one 524-byte copy per level instead of the three a swap costs, and none at all
// when nothing needs to move. Element-for-element identical result to the swap version.
static void sift_down(uint16_t root, uint16_t end)
{
	uint32_t child = (uint32_t)root * 2 + 1;
	if(child > end) return;

	if((child < end) && (strcasecmp(slaunch[child].name, slaunch[child + 1].name) < 0)) child++;
	if(strcasecmp(slaunch[root].name, slaunch[child].name) >= 0) return;

	sort_tmp = slaunch[root];

	for(;;)
	{
		slaunch[root] = slaunch[child];
		root = (uint16_t)child;

		child = (uint32_t)root * 2 + 1;
		if(child > end) break;
		if((child < end) && (strcasecmp(slaunch[child].name, slaunch[child + 1].name) < 0)) child++;
		if(strcasecmp(sort_tmp.name, slaunch[child].name) >= 0) break;
	}

	slaunch[root] = sort_tmp;
}

// heapsort - O(n log n) compares, in place, no extra allocation.
// replaces the original O(n^2) selection sort (85x fewer strcasecmp at 3500 titles)
static void sort_games(uint16_t n)
{
	if(n < 2) return;

	for(int32_t start = (n - 2) / 2; start >= 0; start--) sift_down((uint16_t)start, n - 1);

	for(uint16_t end = n - 1; end > 0; end--)
	{
		sort_tmp = slaunch[0]; slaunch[0] = slaunch[end]; slaunch[end] = sort_tmp;
		sift_down(0, end - 1);
	}
}

static void load_data(void)
{
	int fd;

	//if(get_vsh_plugin_slot_by_name("WWWD") >= 7) {games_count = fav_mode = 0, gmode = TYPE_FAV; return;}

	char filename[64];
	if(fav_mode) sprintf(filename, WMTMP "/" SLIST "1.bin"); else sprintf(filename, WMTMP "/" SLIST ".bin");

	uint64_t size = file_len(filename);
	if(fav_mode && (size == 0)) {sprintf(filename, WMTMP "/" SLIST ".bin"); size = file_len(filename); fav_mode = TYPE_ALL;} else
	if(fav_mode) cur_game = fav_game; else cur_game = cur_game_;

	uint32_t full_count = size / sizeof(_slaunch);

	games_count = (full_count >= MAX_GAMES) ? (MAX_GAMES - 1) : (uint16_t)full_count;

	if(cur_game >= games_count) cur_game = 0;

	uint8_t gmode_tries = 0; // guard so the PSX/PS2/PS3 cycle below can't spin forever

reload:
	// Re-derive the count from the file on every pass. The empty-entry drop below lowers
	// games_count, and the retry path re-reads the file - carrying the lowered count over
	// would read only that many entries and silently lose the tail of the library. The
	// size has to be re-read too rather than remembered, because the cache write below
	// legitimately shortens the file when it drops the blank records; trusting the old
	// size would read past EOF and leave stale heap in the tail instead.
	size        = file_len(filename);
	full_count  = size / sizeof(_slaunch);
	games_count = (full_count >= MAX_GAMES) ? (MAX_GAMES - 1) : (uint16_t)full_count;

	reset_heap(); load_background(); slaunch = NULL;

	if(games_count && (cellFsOpen(filename, CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED))
	{
		// load game list in MC memory
		while(games_count)
		{
			slaunch = (_slaunch*)mem_alloc((games_count + 1) * sizeof(_slaunch));
			if(slaunch) break;
			games_count--;
		}

		uint16_t alloc_games = games_count;                       // for the trailing mem_free
		uint8_t  complete    = (games_count == full_count);       // whole file read? safe to cache

		cellFsRead(fd, (void *)slaunch, sizeof(_slaunch) * games_count, NULL);
		cellFsClose(fd);

		//if(!fav_mode)
		{
			uint16_t ngames; char *path;

			// ---- 1. drop empty entries from the full list -------------------------
			// (the original ran this last, and with the copy/count the wrong way round,
			//  so empties survived and an equal number of real titles fell off the end)
			// (ngames != n) skips a 524-byte struct copy onto itself - on a clean list
			// that is every single entry, ~1.8MB of pointless memcpy at 3500 titles
			ngames = 0;
			for(uint16_t n = 0; n < games_count; n++)
			{
				if(*slaunch[n].name && *(slaunch[n].name + slaunch[n].path_pos))
				{
					if(ngames != n) slaunch[ngames] = slaunch[n];
					ngames++;
				}
			}
			games_count = ngames;

			// ---- 2. sort the FULL list once, then cache it back to disk -----------
			// filtering below only compacts, so the filtered view stays sorted and the
			// next load hits the cheap already-sorted check instead of sorting again
			if(games_count > 1)
			{
				uint8_t sorted = 1;

				for(uint16_t n = 0; n < (games_count - 1); n++)
					if(strcasecmp(slaunch[n].name, slaunch[n+1].name) > 0) {sorted = 0; break;}

				if(!sorted)
				{
					sort_games(games_count);

					// only write back a list that is genuinely complete, otherwise a
					// MAX_GAMES cap or a short alloc would truncate the file on disk
					if(complete && (cellFsOpen(filename, CELL_FS_O_CREAT | CELL_FS_O_TRUNC | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED))
					{
						cellFsWrite(fd, (void *)slaunch, sizeof(_slaunch) * games_count, NULL);
						cellFsClose(fd);
					}
				}
			}

			// ---- 3. filter by type ------------------------------------------------
			ngames = games_count;

			if(gmode && !fav_mode)		// filter games by type (favorites are always shown unfiltered)
			{
				ngames = 0;

				for(uint16_t n = 0; n < games_count; n++)
				{
					path = slaunch[n].name + slaunch[n].path_pos;

					// ROMS/PSXISO and ROMS/PS2ISO are scanned under the generic ROM type -
					// split them back out by path so they land in the PSX / PS2 filters too
					if( (slaunch[n].type == gmode) ||
						((gmode == TYPE_PSX) && (slaunch[n].type == TYPE_ROM) && stristr(path, "/PSXISO/")) ||
						((gmode == TYPE_PS2) && (slaunch[n].type == TYPE_ROM) && stristr(path, "/PS2ISO/")) )
					{
						if(ngames != n) slaunch[ngames] = slaunch[n];
						ngames++;
					}
				}

				// nothing matched - try the next category, but only cycle through each of
				// PSX/PS2/PS3 once, then fall through and let show_no_content() report it
				if(ngames == 0) {gmode = next_cat(gmode); if(++gmode_tries < CAT_COUNT) goto reload;}
			}

			// ---- 4. filter by device ----------------------------------------------
			if(dmode)		// filter games by device
			{
				int8_t dlen = strlen(drives[dmode-1]);

				games_count = ngames; ngames = 0;

				for(uint16_t n = 0; n < games_count; n++)
				{
					path = slaunch[n].name + slaunch[n].path_pos;

					if( ((dmode == NTFS) && (strncmp(path + 10, "/dev_hdd0/tmp", 13)   == 0)) ||
						((dmode == HDD0) && (strncmp(path + 11, drives[dmode-1], dlen) == 0) && (path[10+10]!='t')) ||
						((dmode >  NTFS) && (strncmp(path + 11, drives[dmode-1], dlen) == 0))
					  )
					{
						if(ngames != n) slaunch[ngames] = slaunch[n];
						ngames++;
					}
				}

				if(ngames == 0) {if(++dmode > DEVS_MAX) dmode = TYPE_ALL; goto reload;}
			}

			// ---- 5. release the unused tail of the game-list allocation ----------
			if(alloc_games > ngames) mem_free((alloc_games - ngames) * sizeof(_slaunch));

			games_count = ngames;

			if(cur_game >= games_count) cur_game = 0;  // list shrank under the cursor
		}
	}
	else
		games_count = 0;
}

static void reload_data(uint32_t curpad)
{
	play_rco_sound("snd_cursor");

	cur_game = 0, tick = 0x80, delta = 10;

	load_data();
	show_content();
}

//////////////////////////////////////////////////////////////////////
//                       START VSH MENU                             //
//////////////////////////////////////////////////////////////////////

static void start_VSH_Menu(void)
{
	int32_t ret, mem_size;

	uint64_t slist_size = file_len(WMTMP "/" SLIST ".bin"); if(slist_size > (MAX_GAMES * sizeof(_slaunch))) slist_size = MAX_GAMES * sizeof(_slaunch);

	mem_size = ((CANVAS_W * CANVAS_H * 4)   + (CANVAS_W * INFOBAR_H * 4) + (FONT_CACHE_MAX * 32 * 32) + (MAX_WH4) + (SM_M) + (slist_size + sizeof(_slaunch)) + MB(1)) / MB(1);

	// create VSH Menu heap memory from memory container 1("app")
	ret = create_heap(mem_size);

	if(ret) return;

	disp_w = getDisplayWidth();
	disp_h = getDisplayHeight();

	rsx_fifo_pause(1);

	mem_alloc(slist_size + sizeof(_slaunch)); // reserve initial heap for game list

	// initialize VSH Menu graphic
	init_graphic();

	// stop vsh pad
	start_stop_vsh_pad(0);

	// load game list
	load_config();
	load_data();
	show_content();

	if(!games_count && !dmode && !gmode)
	{
		return_to_xmb();

		if(do_once)
		{
			do_once = 0;
			send_wm_request("/refresh_ps3");
			send_wm_request("/browser.ps3/setup.ps3");
		}
	}
}

//////////////////////////////////////////////////////////////////////
//                       STOP VSH MENU                              //
//////////////////////////////////////////////////////////////////////

static void stop_VSH_Menu(void)
{
	// menu off
	slaunch_running = 0;

	// unbind renderer and kill font-instance
	font_finalize();

	// release the shared image decoders held across this session
	jpg_dec_finalize();
	png_dec_finalize();

	// free heap memory
	destroy_heap();

	// save config
	save_config();

	// prevent pass cross button to XMB
	wait_for_button_release(PAD_CROSS);

	// continue rsx rendering
	rsx_fifo_pause(0);

	// restart vsh pad
	start_stop_vsh_pad(1);
}

static void return_to_xmb(void)
{
	dim_bg(0.5f, 0.0f);
	stop_VSH_Menu();
}

static void blink_option(uint64_t color, uint64_t color2, uint32_t msecs)
{
	for(uint8_t u = 0; u < 10; u++)
	{
		set_frame(1 + cur_game % gpp, (u & 1) ? color : color2);
		sys_timer_usleep(msecs);
	}
}

static void add_game(void)
{
	int fd;

	blink_option(BLUE, DARK_BLUE, 25000);

	play_rco_sound("snd_cursor"); if(!games_count) return;

	if(cellFsOpen(WMTMP "/" SLIST "1.bin", CELL_FS_O_RDONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		_slaunch temp; uint64_t bytes_read;

		char *path = slaunch[cur_game].name + slaunch[cur_game].path_pos;

		while(cellFsRead(fd, (void *)&temp, sizeof(_slaunch), &bytes_read) == CELL_FS_SUCCEEDED)
		{
			if(bytes_read < sizeof(_slaunch)) break;

			if(!strcmp(temp.name + temp.path_pos, path)) {cellFsClose(fd); return;}
		}
		cellFsClose(fd);
	}

	play_rco_sound("snd_system_ok");

	if(cellFsOpen(WMTMP "/" SLIST "1.bin", CELL_FS_O_APPEND | CELL_FS_O_CREAT | CELL_FS_O_WRONLY, &fd, NULL, 0) == CELL_FS_SUCCEEDED)
	{
		cellFsWrite(fd, (void *)&slaunch[cur_game], sizeof(_slaunch), NULL);
		cellFsClose(fd);
	}
}

static void remove_game(void)
{
	int fd;

	if(games_count && (cellFsOpen(WMTMP "/" SLIST "1.bin", CELL_FS_O_CREAT | CELL_FS_O_WRONLY | CELL_FS_O_TRUNC, &fd, NULL, 0) == CELL_FS_SUCCEEDED))
	{
		play_rco_sound("snd_cancel");

		uint16_t n;

		for(n = 0; n < games_count; n++)
		{
			if(n!=cur_game) cellFsWrite(fd, (void *)&slaunch[n], sizeof(_slaunch), NULL);
		}
		cellFsClose(fd);

		games_count--;

		blink_option(RED, DARK_RED, 25000);

		for(n = cur_game; n < games_count; n++)
		{
			slaunch[n] = slaunch[n+1];
		}

		memset(&slaunch[games_count], 0, sizeof(_slaunch)); mem_free(sizeof(_slaunch));

		// cur_game-- underflowed to 65535 when the last favorite was removed
		if(cur_game >= games_count) cur_game = games_count ? (games_count - 1) : 0;
		show_content();
	}
	else
		play_rco_sound("snd_cursor");
}

////////////////////////////////////////////////////////////////////////
//                      PLUGIN MAIN PPU THREAD                        //
////////////////////////////////////////////////////////////////////////

static bool gui_allowed(bool popup)
{
	if(slaunch_running) return 0;

	if(xsetting_CC56EB2D()->GetCurrentUserNumber()<0) // user not logged in
	{
		if(popup) {send_wm_request("/popup.ps3?Not%20logged%20in!"); sys_timer_sleep(2);}
		return 0;
	}

	if(
		IS_INGAME ||
		FindLoadedPlugin("videoplayer_plugin") ||
		FindLoadedPlugin("sysconf_plugin") ||
		FindLoadedPlugin("netconf_plugin") ||
		FindLoadedPlugin("software_update_plugin") ||
		FindLoadedPlugin("photoviewer_plugin") ||
		FindLoadedPlugin("audioplayer_plugin") ||
		FindLoadedPlugin("bdp_plugin") ||
		FindLoadedPlugin("download_plugin")
	)
	{
		if(popup) {send_wm_request("/popup.ps3?Not%20in%20XMB!"); sys_timer_sleep(2);}
		return 0;
	}

	return 1;
}

static void slaunch_thread(uint64_t arg)
{
	if(!arg)
	{
		sys_timer_sleep(12);										// wait 12s and not interfere with boot process
		send_wm_request("/popup.ps3?sLaunch%20MOD%20" APP_VERSION);
		unload_mode = 0;
		//play_rco_sound("snd_system_ng");
	}
	else
		unload_mode = 2;

	for(uint8_t n = 0; n < 7; n++)
	{
		if(not_exists(wm_icons[n])) {sprintf(wm_icons[n], WM_ICONS_PATH "%s", wm_icons[n] + 36); // /dev_hdd0/tmp/wm_icons/
		if(not_exists(wm_icons[n]))  sprintf(wm_icons[n], "/dev_flash/vsh/resource/explore/user/0%i.png", n + 20);} // 020.png - 026.png
	}

	uint8_t gpl; uint16_t pg_idx; uint8_t p;

	// is JAP?
	xsettings()->GetEnterButtonAssign(&enter_button);

	while(running)
	{
		if(!slaunch_running)
		{
			if(unload_mode > 2)
			{
				//send_wm_request("/unloadprx.ps3?prx=sLaunch");
				sys_ppu_thread_exit(0);
			}

			if(!IS_ON_XMB) {sys_timer_sleep(5); continue;} sys_timer_usleep(300000);

			pdata.len = 0;
			for(p = 0; p < 2; p++)
				if(cellPadGetData(p, &pdata) == CELL_PAD_OK && pdata.len > 0) break;

			// remote start
			if(arg)
			{
				unload_mode = 5;
				if(!gui_allowed(1)) {running = 0; continue;}

				start_VSH_Menu();
				init_delay = 0;

				// prevent set favorite with start button
				wait_for_button_release(PAD_START);

				continue;
			}

			if(pdata.len)					// if pad data and we are on XMB
			{
				if( ((pdata.button[CELL_PAD_BTN_OFFSET_DIGITAL1] == 0) &&
					 (pdata.button[CELL_PAD_BTN_OFFSET_DIGITAL2] == (CELL_PAD_CTRL_L2 | CELL_PAD_CTRL_R2)))
					||
					((pdata.button[CELL_PAD_BTN_OFFSET_DIGITAL2] == 0) &&
					 (pdata.button[CELL_PAD_BTN_OFFSET_DIGITAL1] == (CELL_PAD_CTRL_START))) )
				{
					if(!gui_allowed(1)) continue;

					start_VSH_Menu();
					init_delay = 0;

					// prevent set favorite with start button
					wait_for_button_release(PAD_START);

					continue;
				}
			}
		}
		else // menu is running
		{
			while(slaunch_running && running)
			{
				if(read_pad) pad_read(); read_pad = 1; // curpad is set in draw_page() when read_pad = 0

				if(curpad)
				{
					if(curpad == oldpad)				// key-repeat
					{
						if(++init_delay <= 20) continue;
						sys_timer_usleep(40000);
					}
					else
						init_delay = 0;

					can_skip = 0;
					oldpad = curpad;

					uint16_t _cur_game = cur_game;
					if(curpad & (PAD_SELECT | PAD_SQUARE | PAD_CIRCLE | PAD_CROSS))
					{
						if(fav_mode) fav_game = cur_game; else cur_game_ = cur_game;
						if(!(curpad & PAD_R3)) init_delay = 1;
					}

					if((curpad & PAD_L3) && (curpad & PAD_R3))	// screen capture
					{
						take_screenshot();
						wait_for_button_release(PAD_L3 | PAD_R3);
						continue;
					}

					if(curpad & PAD_TRIANGLE)		// open side-menu
					{
						uint8_t option = draw_side_menu();
						if(option == 1) continue;
						if(option) break;

						// the same three statements the letter panel uses below
						load_background();
						draw_page(cur_game, 0);
						_cur_game = cur_game;

						wait_for_button_release(PAD_TRIANGLE | PAD_CIRCLE | PAD_CROSS);
					}
					else if(curpad & PAD_CIRCLE)	// back to XMB
					{
						//if(fav_mode) {fav_mode = 0; reload_data(curpad);} else
						{
							play_rco_sound("snd_cancel");
							stop_VSH_Menu(); /*return_to_xmb();*/
						}
						break;
					}
					else if(curpad & PAD_L3 && games_count)	{gpp^=34; draw_page(cur_game, 0); sys_timer_usleep(250000); curpad = init_delay = 0;}

					else if(curpad & PAD_R3 && games_count)	// letter jump
					{
						letter_jump();

						// dump_bg() overwrote ctx.canvas with the saved screen strip, so the
						// background has to be reloaded first. Then repaint the whole page:
						// flip_frame() rewrites every visible pixel, which is the only way to
						// be sure nothing of the panel or its edge dividers survives - putting
						// just the saved strip back left a bright seam at its left edge. This
						// is what already happened when a jump changed page; doing it on
						// cancel and same-page jumps too makes closing the panel consistent.
						load_background();
						draw_page(cur_game, 0);
						_cur_game = cur_game;	// already redrawn - skip the redraw pass below

						wait_for_button_release(PAD_R3 | PAD_CIRCLE | PAD_CROSS);
					}

					else if((curpad & PAD_SQUARE) && !fav_mode) {gmode = next_cat(gmode); reload_data(curpad); wait_for_button_release(PAD_SQUARE); continue;}
					else if((curpad == PAD_START) && games_count)	// favorite game XMB
					{
						if(fav_mode) remove_game(); else add_game();
						wait_for_button_release(PAD_START);
						continue;
					}
					if(curpad & PAD_SELECT)	// favorites menu
					{
						if(init_delay)
						{
							play_rco_sound("snd_cursor");
							TOGGLE(fav_mode); init_delay = 0;
							reload_data(curpad);
							wait_for_button_release(PAD_SELECT);
						}
						else if(curpad & PAD_R3)	{return_to_xmb(); send_wm_request("/popup.ps3"); break;}
						continue;
					}

					if(!games_count) continue;

					gpl = (gpp == 10) ? 5 : 10; // games_count per line

					// LEFT/RIGHT move along the current row and turn the page at its ends,
					// landing on the same row of the next/previous page.
					// UP/DOWN move between rows of the current page and never turn the page.
					uint32_t ps  = (cur_game / gpp) * gpp;      // first item on this page
					uint32_t row = (cur_game - ps) / gpl;       // which row the cursor is on
					uint32_t rs  = ps + (row * gpl);            // first item on this row
					uint32_t re  = rs + gpl - 1;                // last item on this row
					if(re >= games_count) re = games_count - 1;

					if(curpad & PAD_RIGHT)
					{
						if(cur_game < re) cur_game += 1;
						else	// right edge of the row - page forward, same row
						{
							can_skip = 1;
							uint32_t np = ps + gpp; if(np >= games_count) np = 0;
							uint32_t t = np + (row * gpl); if(t >= games_count) t = games_count - 1;
							cur_game = t;
						}
					}
					else if(curpad & PAD_LEFT)
					{
						if(cur_game > rs) cur_game -= 1;
						else	// left edge of the row - page back, same row
						{
							can_skip = 1;
							uint32_t np = ps ? (ps - gpp) : (((games_count - 1) / gpp) * gpp);
							uint32_t t = np + (row * gpl) + (gpl - 1); if(t >= games_count) t = games_count - 1;
							cur_game = t;
						}
					}
					else if(curpad & PAD_UP)	{if(cur_game >= (ps + gpl)) cur_game -= gpl;}
					else if(curpad & PAD_DOWN)	{uint32_t pe = ps + gpp; if(pe > games_count) pe = games_count;
					                             if((cur_game + gpl) < pe) cur_game += gpl;}
					else if(curpad & PAD_R1)	{can_skip = 1; if(cur_game == (games_count - 1)) cur_game = 0; else if((cur_game+gpp)>=games_count) cur_game = games_count - 1; else cur_game += gpp;}
					else if(curpad & PAD_L1)	{can_skip = 1; if(!cur_game) cur_game = games_count - 1; else cur_game -= gpp;}

					// L1 / R1 move one page, L2 / R2 move five - so both scale with the grid:
					// 10 / 50 items in 5x2, 40 / 200 in 10x4. Exact match (as aldo does for
					// START) so a trigger only fires when it is the ONLY thing pressed - the
					// L2+R2 exit combo and stray bits from the PS button no longer trigger it.
					else if(curpad == PAD_R2)
						{can_skip = 1; uint32_t j = gpp * 5; if(cur_game == (games_count - 1)) cur_game = 0; else if((cur_game + j) >= games_count) cur_game = games_count - 1; else cur_game += j;}
					else if(curpad == PAD_L2)
						{can_skip = 1; uint32_t j = gpp * 5; if(!cur_game) cur_game = games_count - 1; else if(cur_game < j) cur_game = 0; else cur_game -= j;}

					else if(curpad & PAD_CROSS && games_count)	// execute action & return to XMB
					{
						bool ps1_ps2 = (slaunch[cur_game].type <= TYPE_PS2);

						char path[512];
						snprintf(path, 512, "%s", slaunch[cur_game].name + slaunch[cur_game].path_pos);

						if(!ps1_ps2) send_wm_request(path);

						play_rco_sound("snd_system_ok");

						blink_option(RED, DARK_RED, 75000);

						if(ps1_ps2) send_wm_request(path);

						return_to_xmb();
						break;
					}

					if((cur_game!=_cur_game) && games_count)		// draw backdrop
					{
						tick = 0xc0, delta = 10;
						play_rco_sound("snd_cursor");
						pg_idx=(1 + _cur_game % gpp);
						if(pg_idx <= games_count) set_backdrop(pg_idx, 1);
						if(cur_game >= games_count) cur_game = 0;
						if((cur_game / gpp) * gpp != (_cur_game / gpp) * gpp)
							draw_page(cur_game, key_repeat & can_skip);
						else
							draw_selection(cur_game);
					}
				}
				else
				{
					init_delay = 0, oldpad = 0, tick+=delta;	// pulsing selection frame
					pg_idx = (1 + cur_game % gpp);
					if(pg_idx <= games_count) set_frame(1 + cur_game % gpp, 0xff000000ff000000|tick<<48|tick<<16); else cur_game = 0;
					if(tick < 0x80 || tick > 0xF0){delta = -delta; tick &= 0xff;}

					// update temperature
					if(++frame > 400) {frame = 0, TOGGLE(cpu_rsx); draw_selection(cur_game);}
				}
			}
		}
	}

	if(slaunch_running)
		stop_VSH_Menu();

	sys_ppu_thread_exit(0);
}

/***********************************************************************
* start thread
***********************************************************************/
int32_t slaunch_start(uint64_t arg)
{
	sys_ppu_thread_create(&slaunch_tid, slaunch_thread, arg, -0x1d8, 0x2000, 1, THREAD_NAME);

	_sys_ppu_thread_exit(0);
	return SYS_PRX_RESIDENT;
}

/***********************************************************************
* stop thread
***********************************************************************/
static void slaunch_stop_thread(uint64_t arg)
{
	if(slaunch_running) stop_VSH_Menu();

	running = 0;

	uint64_t exit_code;

	if(slaunch_tid != SYS_PPU_THREAD_NONE)
			sys_ppu_thread_join(slaunch_tid, &exit_code);

	sys_ppu_thread_exit(0);
}

/***********************************************************************
*
***********************************************************************/
static void finalize_module(void)
{
	uint64_t meminfo[5];

	sys_prx_id_t prx = prx_get_module_id_by_address(finalize_module);

	meminfo[0] = 0x28;
	meminfo[1] = 2;
	meminfo[3] = 0;

	system_call_3(482, prx, 0, (uint64_t)(uint32_t)meminfo);
}

/***********************************************************************
*
***********************************************************************/
int slaunch_stop(void)
{
	sys_ppu_thread_t t;
	uint64_t exit_code;

	int ret = sys_ppu_thread_create(&t, slaunch_stop_thread, 0, 0, 0x2000, 1, STOP_THREAD_NAME);
	if (ret == 0) sys_ppu_thread_join(t, &exit_code);

	finalize_module();

	_sys_ppu_thread_exit(0);
	return SYS_PRX_STOP_OK;
}
