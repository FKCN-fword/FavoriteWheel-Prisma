# Favorite Wheel - PrismaUI display build

Personal-use fork of BlackMesa79's Favorite Wheel (GPL-3.0). All wheel logic is unchanged
(input hook, favorites, equip/use, time control, quick slots). Only the ImGui renderer was replaced:
`src/PrismaView.cpp` pushes the wheel state as JSON to a PrismaUI view
(`PrismaUI/views/FavoriteWheel/index.html`), because the original ImGui drawing is not visible
when SkyrimUpscaler is active.

Requirements: Skyrim SE 1.5.97 (or the runtimes the original supports), SKSE, Address Library, PrismaUI.

Install: disable/remove the original Favorite Wheel mod (same DLL name), then install this one.
Existing `FavoriteWheel.ini` settings and co-save data are reused.

Not included in this first PrismaUI version: F2 settings screen (edit the INI), outfit preset editor,
skin textures. Log: `Documents/My Games/Skyrim Special Edition/SKSE/FavoriteWheel.log`.
