# Scrape Artwork and Info

The **Scrape** button in the Card Manager's toolbar (openMenu mode, in both the Windows and the Avalonia version) finds
covers, disc labels and game information online. It shrinks the pictures and converts them to the Dreamcast's PVR format
with the same encoder as hand-picked artwork. Nothing reaches the SD card until **Save Changes**. A whole run is one
**Undo** step.

## Menu

The **Scrape** button opens a small menu:

- **Scrape Games...**: sources, what to get (front, back, disc label, info), which games, region and text language,
  then a run with progress and a log.
- **ScreenScraper Account...**: user name and password (with Show password), a link to create a free account,
  **Test login** (shows the account name, requests used today out of the daily limit, and threads), and the developer ID
  under Advanced. This is like the scraper account screen in EmulationStation and Recalbox. The Scrape Games window shows
  which account is set, and its **Account...** button opens the same window.

## Sources

| Source | Finds games by | Gives | Needs |
|---|---|---|---|
| ScreenScraper (screenscraper.fr), asked first | serial and image file name, then a title search | front, back, disc label, synopsis, developer, publisher, release date, genres, players, rating | a developer ID (see below); a free user account raises the daily limit |
| libretro thumbnails (github.com/libretro-thumbnails/Sega_-_Dreamcast) | title, matched against Redump names | front covers only | nothing |

The region comes from the disc's IP.BIN (a PAL disc gets European art and dates), or from the Region box. Titles are
compared without brackets, punctuation and "the", with roman numerals as digits; numbers must agree ("Crazy Taxi" never
matches "Crazy Taxi 2"). Demo, trial and passport discs are only used when nothing else matches. When ScreenScraper knows
the game, its regional names ("Jet Grind Radio") are also tried in libretro.

ScreenScraper answers and downloaded pictures are kept in `scrape_cache` in the user's application data folder
(`%APPDATA%\GDMENUCardManager` on Windows, `~/.config/GDMENUCardManager` on Linux and macOS). A second run costs no
requests. The settings, including the ScreenScraper password, are in `scraper.json` in the same folder, never on the
SD card.

### Developer ID

ScreenScraper needs a developer ID and password for every program that uses its API. None is committed to this
repository. The **Build Card Manager** workflow puts one into the build from the repository secrets `SS_DEVID` and
`SS_DEVPASSWORD` when they exist. Otherwise users enter their own under **Advanced** (ScreenScraper hands them out on its
forum). Without one, only libretro front covers are found.

## What goes where

| Picked | Written to | Notes |
|---|---|---|
| Front cover | `BOX.DAT` (256x256) and `ICON.DAT` (128x128) | Derek's openMenu reads these too |
| Back cover | `BACK.DAT` (256x256, BOX.DAT's format) | new; for the back of the 3D case |
| Disc label | `DISCART.DAT` (128x128, ICON.DAT's format) in the menu data | when the menu is built, it fills `DISC.DAT` for the games whose own disc label (0GDTEX.PVR) cannot be read; round pictures are centred on a transparent square, not stretched |
| Description and info | `META.DAT`: description (ASCII, cut at a sentence to 375 characters), genre bits, players | only empty fields are filled, unless **Replace** is ticked; VMU blocks, accessories and network are never touched |
| | `INFO.DAT` (new) | release date, developer, publisher, genre text, players, rating (0-20), regional title |

### INFO.DAT format

The same container as META.DAT: `"DAT\x01"`, entry size (0x100), entry count, reserved. Then 16-byte index entries (a
10-character serial, 2 reserved bytes, a file number), and each entry at file number × 0x100. The menu's `dat_reader.c`
reads it as it reads the others (checked on the host). Strings are ASCII and NUL-terminated.

| Offset | Size | Field |
|---|---|---|
| 0x00 | 2 | year (little-endian, 0 = unknown) |
| 0x02 | 1 | month |
| 0x03 | 1 | day |
| 0x04 | 60 | developer |
| 0x40 | 60 | publisher |
| 0x7C | 60 | genre, as text ("Action, Sports") |
| 0xB8 | 8 | players ("1-4") |
| 0xC0 | 1 | rating, 0..20 |
| 0xC4 | 60 | title in the chosen region |

## Not yet

- The BIOS-style openMenu does not read `BACK.DAT` or `INFO.DAT` yet: the back of the case stays blank and the year
  sort still uses the IP.BIN date. That is menu-side work (`gfx.c` art loader, `ui_bios.c` information panel).
- No per-game "scrape this one" button in the Artwork window: select the game and choose **Selected games**.
- Tests: `src/GDMENUCardManager.ScraperTests` (offline: matching against real libretro names, a ScreenScraper answer,
  the META/INFO entries, PVR sizes) runs in the workflow. The live ScreenScraper API has not been called from here (no
  developer ID). The first real run is the test.
