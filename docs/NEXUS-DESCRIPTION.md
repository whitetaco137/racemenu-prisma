# RaceMenu Prisma — Nexus page copy

Use this file as the page body. Replace the four `SCREENSHOT` markers with the Nexus image URLs after uploading the category screenshots.

```bbcode
[center][size=6][b][color=#e4bd72]RACEMENU PRISMA[/color][/b][/size]
[size=3]A character creator built for a serious Skyrim load order.[/size][/center]

[center][img]SCREENSHOT_HEADER[/img][/center]

[size=4][b]RaceMenu is still the best character creator Skyrim has. Prisma gives it a front end that can keep up with a modern mod list.[/b][/size]

Hundreds of sliders, custom races, layered paints, 3BA or HIMBO morphs, presets from years of saves: none of that belongs in a tiny scrolling Flash panel. RaceMenu Prisma puts the workbench back in the character creator. The controls you reach for constantly stay where they belong, categories are easy to read, and the actual character remains the focus.

[color=#e4bd72][size=5][b]Highlights[/b][/size][/color]

[list]
[*][b]Fantasy presentation, not a web dashboard.[/b] Warm materials, gilded detail, heraldic race cards, readable type, and controls sized for actual play.[/*]
[*][b]Built for large load orders.[/b] Search, category icons, changed-only filtering, friendly labels for technical slider keys, and a layout that stays usable with several hundred entries.[/*]
[*][b]Better character workflow.[/b] Fixed Character & Scene and Appearance frames, two-column race selection, male/female controls with distinct colour treatment, semantic steppers, precise numeric entry, undo/redo, and collapsible sections.[/*]
[*][b]Paint without guesswork.[/b] Dedicated texture and colour handling for hair, warpaint, body paint, hand paint, foot paint, and face paint, with texture previews where available.[/*]
[*][b]Presets that tell you what they need.[/b] Browse, save, inspect used plugins and head parts, flag missing dependencies, and preview tint textures.[/*]
[*][b]BodySlide export from the character in front of you.[/b] Save a named CBBE 3BA or HIMBO preset directly from current RaceMenu morphs. Prisma writes the same value to BodySlide's small and big weights and places the XML in MO2 Overwrite.[/*]
[*][b]Useful scene tools.[/b] Rotation, zoom, light, poses, undress, and a one-click neutral standing pose.[/*]
[/list]

[center][img]SCREENSHOT_RACES[/img][/center]

[color=#e4bd72][size=5][b]The important technical point[/b][/size][/color]

[b]RaceMenu Prisma does not modify RaceMenu.[/b]

It does [b]not[/b] replace or patch RaceMenu's DLLs, SWF files, scripts, translations, or assets. It does not redistribute RaceMenu files. It does not alter RaceMenu code.

Prisma runs as a separate PrismaUI overlay above the normal RaceSexMenu. The original RaceMenu interface stays loaded underneath and continues to do its own job. Prisma reads the slider and category data RaceMenu exposes, then forwards your actions back through RaceMenu's existing delegate calls.

That means your existing races, morph packages, overlays, hair packs, and presets remain RaceMenu content. Prisma is the presentation layer and workflow upgrade on top of it.

[color=#e4bd72][size=5][b]BodySlide export[/b][/size][/color]

The BodySlide panel has its own name field and Save button. Choose [b]CBBE 3BA[/b] or [b]HIMBO[/b], name the preset, and save it.

The result is written to:

[code]Data\CalienteTools\BodySlide\SliderPresets[/code]

When Skyrim is launched through MO2, that path is virtualized and the file appears in [b]Overwrite[/b]. Create a new mod from it or move it into your preferred BodySlide preset mod.

RaceMenu `0.0` exports as BodySlide `0`. RaceMenu `1.0` exports as `100`. Prisma deliberately writes the current value to both `small` and `big`, so the preset has the same morph at both body weights. Supported overdrive values are preserved instead of silently clipped.

[center][img]SCREENSHOT_MORPHS[/img][/center]

[color=#e4bd72][size=5][b]Requirements[/b][/size][/color]

[list]
[*]Skyrim Special Edition / Anniversary Edition and a matching SKSE build.[/*]
[*][url=https://www.nexusmods.com/skyrimspecialedition/mods/19080]RaceMenu[/url]. Prisma is tested with RaceMenu AE 0.4.20.0.[/*]
[*]PrismaUI.[/*]
[/list]

Optional: CBBE 3BA and/or HIMBO are required only for their respective BodySlide export buttons.

[color=#e4bd72][size=5][b]Installation[/b][/size][/color]

[list=1]
[*]Install RaceMenu and PrismaUI normally.[/*]
[*]Install RaceMenu Prisma with MO2 or Vortex and enable it.[/*]
[*]Open character creation through a new game, [code]showracemenu[/code], or your usual mod.[/*]
[/list]

There is no ESP to sort. If a dynamic morph package injects after the menu opens, press [b]Refresh[/b] once it has finished loading.

[color=#e4bd72][size=5][b]Compatibility and conflicts[/b][/size][/color]

Prisma is designed to work with normal RaceMenu content: custom races, hair and head-part packs, overlays, paints, presets, ECE slider add-ons exposed through RaceMenu, CBBE 3BA, HIMBO, and other morph providers.

[b]Do not install Prisma together with mods that replace, patch, or take control of RaceMenu's own interface or scene controls.[/b] In particular:

[list]
[*][b]RaceMenu - Nordic UI - DIP Patch[/b], and any Dynamic Interface Patcher or loose-SWF patch for [code]interface/racesex_menu.swf[/code].[/*]
[*][b]Smooth Interface (60fps)[/b] if its RaceMenu [code]racesex_menu.swf[/code] is installed.[/*]
[*][b]Racemenu Enhancer (FUCK-RACE)[/b] and similar camera, rotation, freeze, or overlay-controller packages.[/*]
[*][b]RaceMenu Player Rotation[/b] and other mods that take over RaceMenu preview input.[/*]
[*]Any complete RaceMenu UI replacement or RaceSexMenu SWF edit.[/*]
[/list]

General UI overhauls that do not touch RaceMenu's [code]racesex_menu.swf[/code] are normally fine. ECE remains limited by RaceMenu's own upstream compatibility.

[color=#e4bd72][size=5][b]Support notes[/b][/size][/color]

If original RaceMenu cannot open or is already malfunctioning, fix that first. Prisma deliberately leaves RaceMenu intact; it is not a repair patch for a broken RaceMenu installation.

When reporting a bug, include your game runtime, RaceMenu version, PrismaUI version, whether the issue happens on a clean profile, and the relevant SKSE log.

[color=#e4bd72][size=5][b]Source and credits[/b][/size][/color]

The C++ bridge and complete UI source are available under GPL-3.0 at [url=https://github.com/QuantumVale/racemenu-prisma]GitHub[/url].

Thanks to expired6978 for RaceMenu, the PrismaUI project, SKSE, and CommonLibSSE-NG. RaceMenu Prisma is an independent add-on; RaceMenu and its files remain the property of their respective author.

[center][img]SCREENSHOT_PRESETS[/img][/center]
```
