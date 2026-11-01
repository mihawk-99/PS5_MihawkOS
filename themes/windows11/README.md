# Windows-style Wine theme and Proton UI

`source/` contains the editable LGPL-2.1-or-later Fluent Light resources supplied
in the local `FluentLight-Wine` reference. Base: Wine Light, copyright 2021–2024
Zhiyi Zhang for CodeWeavers and Wine contributors. Original input hashes are in
`reference.json`; the sibling reference is never modified. Our resource compiler
rebuilds the module without modifying a vendor binary. The primary font is
**Proton UI**, with 9-point captions and default UI text.

Build and check:

```sh
python3 tools/build-proton-ui-font.py
python3 tools/build-wine-theme.py
python3 tools/build-mihawk-icons.py
```

Outputs are in `.deps/windows11-theme/`. `stage-wine.py` installs the managed
default at `C:\windows\resources\themes\light\light.msstyles` and the five fonts
in the shared baseline's `C:\windows\Fonts`. Existing prefixes receive the same
versioned files before program imports. A user's selected alternate theme is
preserved. The shared `appearance.json` owns light/dark colours and metrics. Host checks
independently parse all 964 resources and resolve 956 bitmap references across
both variants. This does not establish rendering correctness on the PS5.

## Font

Proton UI is a derivative of openly licensed **Selawik**, an upstream replacement
for Segoe UI. The immutable five TTF inputs and OFL notice are in `fonts/upstream/`;
`fonts/upstream.json` pins WinJS revision and SHA-256 for every input. The builder
is the complete reproducible modification: distinct family/PostScript names,
five weight identities, 815 Latin/accented kerning pairs per face, and removal of
the stale binary signature. It preserves the outlines, advances, combining-mark
positioning and original copyright/author/license notices. Optical spacing
choices are ours, not values copied from a proprietary font.

The fonts and their builder remain **OFL-1.1**, separately from the LGPL theme.
They can be bundled under the terms of the included OFL. No proprietary Segoe
font is an input or output. Regular, Bold, Light, Semilight and Semibold have 348
encoded characters each. Other scripts use the runtime's existing fallback
fonts; there are no custom italic or variable optical-size masters in this
version. A program asking for an unrelated font keeps its own selection.

The PE32 and PE64 `proton-ui*-probe.exe` programs verify normal discovery without
private registration, actual family and weight, glyphs, kerning and raster ink.
Pinned Wine on Linux passes 36 checks in each architecture; see
`evidence/proton-ui-host-2026-10-08-02.json`. The console also passes all 36 checks in each architecture with clean owner
restart (2026-10-08; record removed from the tree, in git history). These are font/API
and bitmap checks, not a native Windows 11 pixel-equivalence claim.

## Wallpaper and fidelity

The desktop's wallpaper is Bubble City by Skeuoss (`wallpapers/skeuoss-bubble-city.jpg`, used with the artist's
permission since 2026-10-31, under no open licence: NOTICE.md), stamped with the version note and staged as a
1920×1080 `proton.bmp` by `tools/build-wine-theme.py`, again only when the picture, the stamp or the note changes.
Before it the desktop showed the project's own Bubble Earth (`tools/make-aero-wallpapers.py`, seeded,
LGPL-2.1-or-later) and Aero Meadow (`tools/make-mihawk-wallpaper.py`, before 2026-10-25). Aero Meadow replaced the owner's local copy of Windows 11's Bloom on
2026-10-18, which was never vendored or licensed for redistribution, and every
build ships the project's own wallpaper. Without the bitmap the desktop keeps a plain background. Patch 0966 supplies
the legacy flattened route's startup background beneath retained windows;
opaque window pixels remain opaque. It decodes
once per GDI process, using a bounded row buffer and a desktop-sized cache.
Dynamic Windows wallpaper APIs are not implemented by that legacy startup default.
The independent-window route now uses the real Explorer desktop painter. Its
headless starter sets the normal Wine wallpaper preference once, preserving
existing nonempty and subsequent empty choices. The optional-asset follow-up
allows a missing local BMP to retain the plain background; paired Linux Wine
probes pass 13/13 with the asset present and absent. Both console architectures pass 13/13 in the complete 50-case gate; the actual
mixed-window captures also match Explorer's retained background.

The reference retains Wine's classic control implementation and many original
metrics. It does not supply Mica, live blur, WinUI controls, or rounded outer
windows. Proton UI is a close open substitute, not Segoe UI Variable. No native
Windows 11 pixel comparison has been made, so **pixel-perfect 1:1 fidelity is
not claimed**. App-drawn controls and a launcher's web UI (CEF) may choose their own styling.

## Managed appearance and icons

The real `mihawk-appearance.exe` selector uses Wine's existing theme manager,
persistence and notifications. The managed theme has light (`Blue`) and dark
(`Dark`) variants. Appearance does not override an alternate theme on startup.
Explorer creates an Appearance shortcut only when the managed executable exists
and preserves a same-named user shortcut. The session starter remains headless:
GUI imports would initialize an implicit desktop before its configuration.

`icons/` contains fourteen approved original objects and thirty-four pinned MIT
Fluent commands, with source hashes and provenance. The icon builder produces
82 object/light/dark resource groups at 16, 20, 24, 32, 48 and 64 pixels. Patch
0967 maps shell-owned objects into existing Wine resource IDs; application icons
remain theirs. Resource packaging and small-size host inspection pass; actual
light/dark icon transparency and theme controls pass in both PE32 and PE64.
The nonclient-font fix passes both 1,009-check probes; desktop shortcut interaction remains to qualify. These checks do not establish complete Windows 11 shell parity.

## The prototype's visual system (2026-10-16)

The approved Windows 11 prototype is the visual reference; [PROTOTYPE_DIFF.md](../../docs/windows11-reference/PROTOTYPE_DIFF.md)
lists what changed and what still differs.

- `caption/glyphs.json` holds the prototype's caption buttons as it renders them (every state, light and dark,
  measured by `tools/measure-prototype-caption.mjs`); `tools/pw_caption_buttons.py` fits the Window.*Button
  bitmaps to those pixels with Wine's arithmetic. `controls/controls.json` holds the controls gallery's check
  boxes, radio buttons and push buttons (`tools/measure-prototype-controls.mjs`). Classic programs' system colours
  and the parts the gallery names come from `appearance.json` `classic`.
- `cursors/` is the Mihawk pointer scheme (original SVGs from the prototype, hashes in `manifest.json`), built by
  `tools/build-cursors.py` into `.deps/windows11-cursors` and staged in `C:\windows\Cursors`.
- `fonts/cascadia/` is Cascadia Mono Regular and Bold, unmodified (OFL-1.1, `upstream.json`), staged in
  `C:\windows\Fonts` as the monospace face.

## Native everyday shell

The centered taskbar, Start and Explorer extend this same appearance system.
`appearance.json` remains the canonical token source; the build generates the
native constants used by the shell helpers. The approved reference's 48-pixel
taskbar, 600×566 Start panel and 165-pixel Explorer sidebar are retained at
1920×1080 and 96 DPI. Body controls use the current system menu font and
semibold section headings, preserving the managed nine-point Proton UI family.
Alternate themes retain their system colours and fonts.

The shell uses the existing owned object icons and pinned Fluent command icons.
It refreshes light/dark glyphs with the live theme; focus, hover, selection and
disabled states remain native control states. Disabled Explorer command glyphs
use alpha-aware image-list drawing at half opacity to preserve their strokes.
Explorer's embedded public executable icon matches its runtime folder resources;
the taskbar caches the real executable icon for foreign processes. Dynamic
foreign per-window icon changes remain unsupported by this implementation.

The native build retains square opaque surfaces for this batch. The reference's
blur, shadows, rounded outer clipping and previews remain later work; fallback
surfaces do not establish a new visual system. No private wallpaper or font was
copied from the HTML reference. `PRODUCT.md`, `DESIGN.md` and
`.impeccable/design.json` are absent pre-existing documentation drift and are
not repaired by this ordinary extension.

Functional and visual proof are tracked together in
[Mihawk OS](../../docs/MIHAWK_OS.md#everyday-shell-batch). Paired PE32/PE64 client
probe results qualify only their named checks and captures, not universal
Windows 11 parity. The final correction's focused pair passes 70 checks per
architecture and all eight captures in 70.7 seconds; final visual review resolves
all seven scored findings, including complete muted disabled arrows. The
[58-case exact-source gate](../../evidence/mihawk-shell-gate-2026-10-09-08.json)
passes in 1,748.1 seconds with zero recorded CPU/GPU fatal faults or
process-protocol errors. The integrated batch is qualified for its local commit.
