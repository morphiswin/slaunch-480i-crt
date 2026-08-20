# sLaunch GUI / Game Menu — SD / CRT build

sLaunch is an alternative UI for accessing your webMAN games on the XMB.
It's based on the new GUI for sMAN released by DeanK.

This is a modified build of the sLaunch plugin from webMAN MOD, reworked for standard
definition output on a CRT and for large game libraries. The stock plugin draws a
1920x1080 canvas and point-samples it down to the 720x480 framebuffer, throwing away
about 83% of the image — this build area-averages it instead, adds an anti-flicker pass
for 480i, compensates for TV overscan, and fixes a number of bugs in list handling and
navigation along the way. See *Changes in this build* below.

## Usage

Press `L2`+`R2` or `START` button for 3 seconds from XMB to open/close the game menu.

- L/R Sticks = Navigate
- PAD Arrows = Navigate
- `LEFT` / `RIGHT` = Move along the row, turns the page at the ends of it
- `UP` / `DOWN`  = Move between rows, never turns the page
- `L1` / `R1`    = Prev / Next page
- `L2` / `R2`    = Back / Forward 5 pages
- `CROSS`      = Load the game title
- `CIRCLE`     = Exit to XMB
<br>

- `TRIANGLE`   = Side menu
- `SQUARE`     = Filter content by game type (PSX / PS2 / PS3)
- `L3`         = Toggle between 5x2 / 10x4 items per page
- `R3`         = Jump to letter
- `L3`+`R3`    = Save a screenshot to */dev_hdd0/slaunch_NN.bmp*
<br>

- `SELECT`     = Toggle between Favorite / Normal list
- `SELECT`+`R3`  = Exit to XMB and show the webMAN popup
- `START`      = Add / Remove game from Favorite list

### Jump to letter (`R3`)

A narrow strip at the right edge listing `#` and `A`-`Z`. It opens on the initial of the
title you are currently on. The list is kept sorted, so the first match is the start of
that letter.

- `UP` / `DOWN`  = Move one letter
- `LEFT` / `RIGHT` = Move five letters
- `CROSS`      = Jump to the first title starting with that letter
- `CIRCLE` / `TRIANGLE` / `R3` = Close without jumping
- `L3`+`R3`    = Save a screenshot

### Side menu (`TRIANGLE`)

- `UP` / `DOWN`  = Move between options
- `LEFT` / `RIGHT` = Change the value of the highlighted option
- `CROSS`      = Select
- `CIRCLE` / `TRIANGLE` = Close
- `L3`+`R3`    = Save a screenshot

## Features

- Area-averaged rendering and interlaced anti-flicker for SD output
- Overscan compensation, adjustable per axis
- On screen temperature display (CPU/RSX) — HD output only
- Fast menu navigation in grid format (5x2 or 10x4)
- Jump to letter (`R3`)
- Screen capture to BMP (`L3`+`R3`)
- Side menu
  - **Favorites** - Toggle between Favorite / Normal list
  - **Unmount** - Unload mounted game (ISO / folder)
  - **Refresh** - Scan for content / instant list without reload XMB
  - **gameDATA** - Redirects */dev_hdd0/game* to USB device
  - **Disable CFW Syscalls** (press `LEFT`/`RIGHT` over *gameDATA*)
  - **Restart** - Restarts the PS3
  - **Shutdown** - Turn off the PS3
  - **Unload webMAN**
  - **Setup** - Open configuration page
  - **File Manager** (press `LEFT`/`RIGHT` over *Setup*)

# Changes in this build

## Rendering

- **Area-averaged downscale.** The stock blit iterates *source* pixels, so each output
  pixel keeps whichever canvas pixel happened to be written last — pure decimation. This
  one iterates *destination* pixels and averages every source pixel mapping onto each
  (4-9 of them). Alpha images are composited over 18% grey first, as the stock path does.
- **Interlaced anti-flicker.** Vertical 1-2-1 low-pass over the destination rows, applied
  after each blit. `(a + 2b + c) / 4` is exactly `AVG2(b, AVG2(a, c))`, so it costs two
  packed halving adds per pixel — no divides, no channel unpacking.
- **Overscan compensation.** The UI is drawn into an inset region of the framebuffer with
  the border blacked out, since the PS3 has no picture-size control of its own.
  `OVERSCAN_PCT_X` and `OVERSCAN_PCT_Y` in *blitting.c* trim each axis independently;
  both at 0 gives the stock full-frame mapping exactly.
- Thin horizontal rules are widened on SD, since they are drawn straight to the
  framebuffer and bypass the averaging.
- The four integer divides per output pixel are replaced with a multiply and a shift
  against a reciprocal table, and the per-column source mapping is built once per blit
  rather than re-derived for every one of the ~345,000 output pixels.
- Toggles at the top of *blitting.c*: `SD_AREA_AVERAGE`, `SD_ANTIFLICKER`.

## Rendering fixes

- `print_text` cleared `sizeof(Glyph)` bytes at the address of a `Glyph *` — around 60
  bytes written over its own stack frame, on a thread with an 8KB stack.
- Glyph dimensions are clamped to the 0x400 cache slot they are copied into. The
  rasteriser can return an image larger than requested, which is how bigger fonts
  corrupted the heap.
- The glyph cache index was off by one, leaving slot 0 unused and putting every freshly
  rendered glyph outside the search range.

## Content and list handling

- **PSX/PS2 ROMs split by path.** Entries under */ROMS/PSXISO/* and */ROMS/PS2ISO/* are
  tagged with the generic ROM type by webMAN, so they never matched the PSX/PS2 filters.
- `MAX_GAMES` raised from 2000 to 3500.
- **Heapsort** replaces the O(n²) selection sort — around 85x fewer string comparisons at
  3500 titles, and one struct copy per level instead of three.
- The sorted list is cached back to *slist.bin*, so a category switch no longer re-sorts
  from scratch. The write is guarded so a capped or short read can't truncate the library.
- Fixed an inverted empty-entry filter that kept the blank records and dropped an equal
  number of real titles off the end.
- Fixed the empty-category retry re-reading the file with the wrong count and silently
  losing the tail of the library.
- Fixed a hang: the empty-category retry used to terminate only because it eventually
  landed on ALL. It is now bounded to one pass over the category list.

## Categories and navigation

- `SQUARE` cycles PSX → PS2 → PS3, driven by a single array — the side menu, the empty
  category skip and the saved-config validation all derive from it.
- The unlabelled ALL category is gone. Favorites skip the type filter instead.
- The device filter (`SQUARE`+`L2`) and reset (`SQUARE`+`R2`) are removed.
- `LEFT`/`RIGHT` move along the row and turn the page at its ends, landing on the same
  row of the next page. `UP`/`DOWN` stay on the page.
- `L2`/`R2` move five pages, so both trigger pairs scale with the grid size.
- Trigger tests use an exact match rather than a bitwise AND — the AND was firing on
  stray bits from the **PS button**, since pad data here is a raw HID report.
- `L3` grid toggle restored — `draw_page` was resetting the grid size on every redraw on
  SD, so 10x4 never stuck.
- Fixed the cursor index wrapping to 65535 when the last favorite was removed.

## Side menu and letter panel

- Closing either panel leaves it on screen until the full repaint lands, so nothing
  half-drawn is ever shown. Neither restores a saved screen strip any more.
- Cancelling the side menu no longer triggers a full reload of *slist.bin*.
- The letter panel is a narrow strip at the right edge — one character per row does not
  need the side menu's width, so the covers underneath stay visible.

## Image decoding

- **The decoder is created once per session instead of once per image.** Both loaders
  wrapped `Create`/`Destroy` around every decode; that is the session-level pair in this
  API, `Open`/`Close` is the per-file pair. Drawing one page of the grid paid for it ten
  times over, forty in the 10x4 grid, plus once per background reload.
- It matters most for JPEG, where the SPU thread is enabled — each pair was starting and
  stopping an SPU thread, which costs far more than decoding a cover. The PNG path is
  PPU-only, so the saving there is smaller.
- Both decoders are released in `stop_VSH_Menu()`, alongside `font_finalize()`.

## Info bar

- Shows the title only. webMAN builds names as *folder/file*, which for one folder per
  game is the same title twice — this keeps the part after the last slash. The path line
  is removed and the counter moved up.

# How it works

The plugin is stored in `/dev_hdd0/tmp/wm_res/slaunch.sprx`.
It is loaded dynamically by webMAN MOD and unloaded from memory when it exits to XMB.

NOTE: `/dev_hdd0/tmp` is webMAN's working directory — reinstalling or updating webMAN MOD
will overwrite the plugin, so keep a copy.

The file `/dev_hdd0/tmp/wmtmp/slist.bin` containing the list of games
is built by webMAN MOD when the XML content list is scanned. New content needs a webMAN
refresh before it appears.

A second `/dev_hdd0/tmp/wmtmp/slist1.bin` is used for favorite games.

NOTE: The internal data structure is very different to sLaunch menu in sMAN.

The menu is rendered writing directly to the XMB video buffer using the hack
developed by 3141card for VSH menu and extended by DeanK to display on full screen.
Because the UI is already sitting in the framebuffer, the screen capture is a straight
dump of that memory — the file is exactly what the TV was showing, downscaling and all.

When a game is selected, the menu sends a web command to webMAN MOD to mount the game.
The rest of the game loading process is performed by webMAN MOD and Cobra payload.

The last used settings are stored in `/dev_hdd0/tmp/wmtmp/slaunch.cfg`

## Compilation Notes

This build is compiled with the official PS3 SDK v400.001 toolchain, MinGW and Git Bash
on Windows.

`_Projects_/slaunch` has its own Makefile — the root `_Make.bat` does not build it — so
run the batch file from inside that folder. The modified sources are `slaunch.c`,
`blitting.c`, `jpg_dec.c` and `png_dec.c`.

NOTE: if a rebuild seems to change nothing, delete the `objs` folder first. The Makefile
can report `objs/slaunch.ppu.o` as up to date and relink the stale object, so the build
succeeds and hands you the previous binary. `APP_VERSION` at the top of *slaunch.c* prints
at the top of the side menu — bump it each build and you can confirm at a glance which one
the console is actually running.

The rendering changes only affect the SD path. On 720p/1080p the plugin takes the stock
code path and behaves as upstream; the rest of the fixes apply at every resolution.

## Credits

- **DeanK** - Original sLaunch GUI concept and coding.
- **aldostools** - Modification, additional features and optimizations.
- **3141card** - Video rendering functions and memory handling.
- **DarjanKrijan** - ARGB support and testing
- **Mysis** - VSH exports
