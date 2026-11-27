# GD MENU Card Manager (this fork)

Sonik's GD MENU Card Manager with Derek Pascarella's openMenu changes (1.7.1), and this repository's changes on top:

- It ships this repository's openMenu (`tools/openMenu/menu_data/1ST_READ.BIN` is replaced at build time).
- **Artwork**, **Back** and **Spine** columns: each shows whether the game has a front cover (BOX.DAT), back cover
  (BACK.DAT) or spine label (SPINE.DAT). Click one to open the artwork window on that picture.
- The scraper (ScreenScraper, libretro thumbnails) for covers, backs, disc art and the text in META.DAT / INFO.DAT:
  see `SCRAPER.md`.
- The updater looks at this repository's releases.

## Downloads

From the repository's releases: Windows 10/11 x64 (zip, needs the .NET 6 Desktop Runtime), Linux x64 (self-contained
tar.gz), macOS Apple Silicon and Intel (app bundles, ad-hoc signed: if macOS refuses to open the app, right-click it and
choose Open, or run `xattr -cr GDMENUCardManager.app`).

## Building

`.github/workflows/build-card-manager.yml` builds all of them (it first builds openMenu). With `release=true` it also
publishes a release named after `src/GDMENUCardManager.Core/version.txt`, with the notes in
`.github/release-notes/<version>.md`.

For usage (adding games, virtual folders, artwork, disc types, sorting) see the
[original project](https://github.com/DerekPascarella/openMenu-Virtual-Folder-Bundle).
