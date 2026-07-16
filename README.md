# RaceMenu Prisma

RaceMenu Prisma is a native SKSE companion for RaceMenu that presents the character creator through a purpose-built PrismaUI view. It is intended for long character-creation sessions, large custom morph stacks, and players who want the menu to feel like part of the game rather than a Flash-era utility window.

The project is open source under GPL-3.0. The repository contains the C++ bridge and the complete UI in `view/index.html`; there is no generated front-end bundle to reverse-engineer.

## What it changes

- A full-screen, warm fantasy character-creator interface with readable controls and larger hit targets.
- Two-column race cards with heraldic crests, including neutral crests for custom races.
- A fixed, collapsible Character & Scene frame and a separate fixed category frame, so the controls needed on every page do not disappear while browsing a long list.
- Icon-led category navigation, search, changed-only filtering, semantic steppers for discrete choices, exact numeric entry, and undo/redo.
- Dedicated paint and colour controls for hair, warpaints, body paint, hand paint, foot paint, and face paint. No native browser dropdowns or colour inputs are used.
- Preset browsing with dependency inspection, missing-mod markers, tint texture previews, and ordinary named `.jslot` saves.
- Named BodySlide export for CBBE 3BA and HIMBO. RaceMenu's current morph value is written to both BodySlide `small` and `big` values; a zeroed RaceMenu morph exports as zero, not 50%. Exported files are written to `Data\CalienteTools\BodySlide\SliderPresets`, which MO2 redirects to Overwrite.
- Scene controls for rotation, zoom, light, pose selection, undress, and a one-click return to the neutral standing pose.
- Friendly labels for common technical slider keys such as `$ECE_*`, while retaining the raw key for diagnostics.
- Optional BodySlide overdrive entry for supported custom CBBE 3BA and HIMBO morphs.

## Important: how the integration works

**RaceMenu Prisma does not modify RaceMenu.** It does not patch, replace, redistribute, or edit RaceMenu's DLLs, SWF files, scripts, translations, or assets.

RaceMenu remains loaded exactly as installed. When RaceSexMenu opens, PrismaUI places its own view above it. The original RaceMenu view stays alive underneath as a data and action adapter: the bridge reads the existing entries and replays the same delegate calls RaceMenu already uses. Closing the menu returns control cleanly to the game.

This distinction matters for support:

- A broken RaceMenu installation must be fixed before installing Prisma.
- Morph providers, presets, races, overlays, and other content that already works in RaceMenu remain owned by their original mods.
- Prisma does not ship altered RaceMenu assets and does not change RaceMenu's code path or data files.

## Requirements

- Skyrim Special Edition / Anniversary Edition with a supported SKSE build.
- [RaceMenu](https://www.nexusmods.com/skyrimspecialedition/mods/19080). Current development is tested against RaceMenu AE 0.4.20.0.
- PrismaUI.

Optional:

- CBBE 3BA and/or HIMBO, if you want to export their custom morphs as BodySlide presets.
- Mod Organizer 2 is recommended. The BodySlide export deliberately writes into the virtual `Data` directory so the result lands in MO2 Overwrite instead of an arbitrary hard-coded folder.

## Installation

Install RaceMenu, PrismaUI, and RaceMenu Prisma with your mod manager. Keep RaceMenu and PrismaUI enabled, then enable RaceMenu Prisma. There is no plugin to sort.

Open the creator through a new game, `showracemenu`, or any mod that calls the normal RaceMenu. The first data pass can take a moment when a load order injects a large number of custom sliders. Use **Refresh** after a dynamic morph package finishes loading or after a race/sex change.

## Compatibility

Prisma is compatible with normal RaceMenu content: custom races, hair packs, head parts, overlays, paints, presets, ECE slider add-ons exposed through RaceMenu, CBBE 3BA, HIMBO, and other morph providers. It reads what RaceMenu exposes rather than maintaining its own content database.

It is **not** compatible with mods that alter the RaceMenu interface itself or compete for the same RaceSexMenu presentation and scene controls. Do not combine it with:

- **RaceMenu - Nordic UI - DIP Patch**, or any Dynamic Interface Patcher / loose-SWF patch that replaces or patches `interface/racesex_menu.swf`.
- **Smooth Interface (60fps)** when its RaceMenu `racesex_menu.swf` is installed.
- **Racemenu Enhancer (FUCK-RACE)** and similar RaceMenu camera, rotation, freeze, or overlay-controller packages.
- **RaceMenu Player Rotation** and other mods that take over RaceMenu rotation/preview input.
- Any other complete RaceMenu UI replacement or RaceSexMenu SWF edit.

General UI overhauls that do not touch RaceMenu's `racesex_menu.swf` are normally fine. Enhanced Character Edit remains subject to RaceMenu's own upstream compatibility limits.

## Using BodySlide export

1. Open the CBBE 3BA Morphs or HIMBO Morphs category and set the body as desired.
2. Open **Presets** in the header.
3. Select CBBE 3BA or HIMBO in the BodySlide section.
4. Enter a new name in the BodySlide field and press **Save BodySlide preset**.
5. In MO2, move or create a mod from the file in Overwrite if you want to keep it permanently.

Only the matching custom morph callbacks are exported. A current RaceMenu value of `0.0` becomes BodySlide `0`; `1.0` becomes `100`. The same value is stored for both body weights by design.

## Building from source

Clone recursively, then build with xmake:

```powershell
git clone --recurse-submodules https://github.com/QuantumVale/racemenu-prisma.git
cd racemenu-prisma
xmake build
```

The release DLL is written to `build/windows/x64/release/RaceMenuPrisma.dll`.

For local testing, copy it to `SKSE/Plugins/RaceMenuPrisma.dll` in a dedicated mod folder, and copy `view/index.html` to `PrismaUI/views/RaceMenuPrisma/index.html` in that same folder. Do not copy anything into RaceMenu's own mod directory.

## Project layout

```text
src/main.cpp       Native SKSE bridge
view/index.html    Complete UI: HTML, CSS, and JavaScript in one file
lib/               CommonLibSSE-NG submodule
docs/              Release and support material
```

## Credits

- expired6978 for RaceMenu.
- [PrismaUI](https://github.com/PrismaUI-SKSE) by StarkMP / the PrismaUI-SKSE project, for the web-view framework and public API.
- [CommonLibSSE-NG](https://github.com/CharmedBaryon/CommonLibSSE-NG) by CharmedBaryon and contributors. This project pins its compatible NG build through the [`alandtse/CommonLibVR`](https://github.com/alandtse/CommonLibVR) submodule.
- [SKSE](https://skse.silverlock.org/) and its maintainers.

RaceMenu is required and remains the owner of its own code and assets. RaceMenu Prisma is an independent add-on and is not affiliated with or endorsed by RaceMenu's author.
