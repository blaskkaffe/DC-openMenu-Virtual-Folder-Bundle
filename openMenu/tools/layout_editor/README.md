# Layout editor

`index.html` is a stand-alone, single-file WYSIWYG editor for the BIOS-style menu's screens (640x480): open it in any browser, no build or server needed.

- **Screens**: panels, text, 3D model placeholders, feature-logo strips, lists and hotspots. Drag, resize, change the z order (layers), hide, lock. Selectable items get a hitbox with the button and what it does (green tags).
- **Settings menu order**: the rows of Settings (taken from `rows[]` in `src/openmenu/src/ui_settings.c`) in their groups. Drag rows between groups, reorder groups, rename groups, hide rows.
- Work is kept in the browser (localStorage). **Export JSON** / **Import** move it between browsers; the file (`format: "dc-layout"`) holds `screens` and `settingsMenu`.

The menu does not read the JSON yet: an exported file is the spec the code is changed to (and, later, the input of a loader). When rows are added to `ui_settings.c`, update `SETT_DEFAULT` in `index.html` (a saved order keeps working: rows it does not know are added at the end of their own group).

A hosted copy (with a shared save) is kept as a claude.ai artifact by the repository owner.
