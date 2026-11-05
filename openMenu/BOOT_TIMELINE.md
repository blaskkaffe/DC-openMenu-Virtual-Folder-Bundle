# The first ten seconds after boot

What the menu does from `main()` until it has run for about ten seconds, in order, checked against the BIOS (decompile:
`gui_main` 0x8C0100C0, `gui_init` 0x8C0101E0, `gui_frame_update` 0x8C010700) and against KallistiOS (master: `vmu.c`,
`maple_irq.c`, `maple_queue.c`, `thread.c`, `genwait.c`, `pvr_scene.c`). Times are not measured here (no console in the cloud
session); the debug log's boot marks (`dbglog_boot`) and `HITCH` lines measure them on the console.

## How the BIOS does it

- `gui_init` sets up Ninja, `njInitTexture`, and `gui_register_rom_textures()` registers every ROM texture **before the first
  frame**. Nothing is loaded while the menu is on screen.
- The loop (`gui_frame_update`) reads the pads, steps the VMU state machines (the Shinobi `bu` library is asynchronous: the menu
  never waits for a memory card), updates the text surfaces, runs every object's script and draws it. A frame that took more than
  32 ms runs the logic a second time (catch-up), so the animation keeps its speed.
- The menu fades in from the flat start colour over 32 frames.

## KallistiOS facts that matter here

- `vmu_block_read` / `vmu_block_write` **block** the calling thread until the reply (genwait, 100 ms timeout). Maple frames are
  only sent at the next vblank (`maple_queue_flush` in the VBL IRQ), so every block costs at least one vblank. `vmu_draw_lcd`
  does not block (queues and returns).
- A thread woken from an interrupt (the menu thread waiting in `pvr_wait_ready`) is only made READY; the switch to it happens
  at the next scheduler tick (`thd_timer_hnd` -> `thd_schedule`), or at once when the running thread yields (`thd_pass`,
  `thd_sleep`, a blocking call). So a worker thread that spins without yielding delays the menu by up to a tick, whatever its
  priority. Workers must sleep or yield, never busy-wait.
- `pvr_wait_ready` sleeps (genwait), it does not spin.

## Before the first frame (boot time, nothing on screen yet or the start colour)

| # | Step | Where | Notes |
|---|------|-------|-------|
| 1 | `serial_log_banner` | main.c | one line on the serial port (blocking at 115200, a few ms) |
| 2 | `maple_wait_scan` | main.c | waits for the first maple scan |
| 3 | `video_init` | video.c | VGA 60 Hz, PAL 50 Hz, NTSC 60 Hz |
| 4 | `bios_rom_init` | main.c | parses the ROM in place |
| 5 | `savefile_init` | settings | reads OPENMENU.SYS from the VMUs / SD (blocking maple reads), VMU time sync if on (up to 10 s timeout per card), draws the VMU logo |
| 6 | `uis_apply_card_defaults` | ui_settings | |
| 7 | `list_read_default`, `list_folder_init`, `list_set_folder_root`, `favorites_init`, `uis_clock_migrate` | main.c | the game list from the card, sorted; favourites file |
| 8 | `gfx_init` | gfx.c | `pvr_init`, vertex DMA buffers, bios surfaces |
| 9 | `gfx_load_logo` (LOGO.PVR), `gfx_load_logo_texts` | gfx.c | the online letters from the embedded masks; **now also the cropped / dark ROM logos** (before: built in the first frame that needed them) |
| 10 | META.DAT (`db_load_DAT`) | ui_bios | |
| 11 | `sound_init` | sound.c | driver to sound RAM, **250 ms sleep** for the driver to start, banks |
| 12 | `bmenu_init`, `bmenu_show_main`, `set_icon_labels`, `update_clock` | ui_bios | the scripts are created, the globe icon set |
| 13 | **new: `bmenu_prewarm`** | bios_menu.c | runs a copy of the scripts 60 frames and "draws" it into `gfx_warm_sink`: all main-menu models, motions and ROM textures (and the clouds) are loaded now. Before, each was loaded in the frame where it first appeared, i.e. during the fade-in (ROM textures are copied from the uncached ROM, models parsed). As the BIOS's `gui_register_rom_textures`. The menu itself is not advanced (checked on the host: same object state after 90 frames with and without). |
| 14 | **new: `gfx_preload_art_index`** | gfx.c | the tables of DISC/ICON/BOX DATs; before, read on the menu thread when the game list first opened |
| 15 | **moved: `clock_extender_tick`** | settings | the Clock extender's flash write; before, in frame 1 of the loop |
| 16 | 3 frames in the start colour | ui_bios | |
| 17 | boot marks to the serial port | ui_bios | two lines, blocking (serial) |
| 18 | `svmu_begin_backup` | serial_vmu.c | only when the last game left LASTDISC.TXT (Serial VMU on) |

## The loop, first seconds

| When | What | Notes |
|------|------|-------|
| every frame | `input_poll`, handlers, `svmu_tick`, `bmenu_update` steps (time-based, like the BIOS catch-up), `online_tick`, `clock_extender_tick` (no-op now), `draw_frame` (`pvr_wait_ready`, `art_poll`, surface GC, scene) | |
| frames 1-3 | the BIOS scripts create the icons (frame 2) and captions (frame 3) | models / textures are already loaded (step 13) |
| frames 1-32 (+hold) | fade from the start colour to the gradient | |
| after the fade | **moved: `online_boot`** (Auto-Connect thread) | before: right before the loop, so the device probe ran during the fade-in |
| frame 1 | `dcnow_vmu_tick`: with Online on, the VMU icon variant changes 0 -> 1 and the VMU screens are redrawn | `vmu_draw_lcd` does not block: no stall |
| every second | `update_clock`: header text surface redrawn and uploaded (256x32) only when the text changed | small |
| ~2 s | `vmu_lcd_check_insertions` first check | **changed**: the cards already present are only remembered (savefile_init drew them); before, every card was drawn again at 2 s |
| Serial VMU backup pending | 256 VMU block reads + the 128 KB SD write | **changed**: on a thread. Before, one blocking block read per frame on the menu thread: each waits for the next vblank, so the menu ran at 30 fps for about 8 s, and the SD write at the end stalled once more |
| Auto-Connect | device probe: serial port first (modem/none), then modem dial or adapter bring-up, then time sync, player list, DreamPi link, all on threads at PRIO_DEFAULT+4 | **changed**: `serial_port_open` busy-waited 2 x 800,000 nops with the CPU; now `thd_sleep(10)` twice. Other waits in the workers already yield (`thd_pass`, `thd_sleep`, `poll`) |
| online | header fetch, online games, VMU screen (on a thread / non-blocking) | |

## Not changed (yet)

- `sound_init`'s 250 ms sleep and the VMU reads of `savefile_init`: boot time only (screen not up yet).
- Opening a screen for the first time (Play, File, Settings, Internet) still loads that screen's models and textures in its first
  frames; the same prewarm can be done per screen if the HITCH log shows it.
- The restore before a game reads the 128 KB image on the menu thread (a modal popup is up; one dropped frame).
- KOS's own drivers (modem, libppp reader thread, BBA, DHCP) are not ours; if the HITCH log still shows long frames while
  connecting, they are the next suspects.

## How to check on the console

Debug overlay (X on the main menu), then B for the log page: the boot marks now include `warm` and `art` (their cost moved to
before the menu shows), and the `HITCH` lines list every frame longer than 1.5 refreshes with what it spent the time on. Compare
a boot with Online off and with Auto-Connect, and one with a Serial VMU backup pending.
