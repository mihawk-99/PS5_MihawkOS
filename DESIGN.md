---
name: Mihawk OS Windows 11 Shell
description: The Windows 11 desktop, played straight; a browser design prototype that Wine's native shell on the PS5 is measured against.
colors:
  # Accent ramp: Windows 11's default Blue. Theme-independent; js/core.js recomputes it when the user picks another accent.
  SystemAccentColor: "#0078d4"
  SystemAccentColorLight1: "#0093f9"
  SystemAccentColorLight2: "#60cdff"
  SystemAccentColorLight3: "#99ebff"
  SystemAccentColorDark1: "#005fb8"
  SystemAccentColorDark2: "#003e92"
  SystemAccentColorDark3: "#001a68"
  # Accent fills and accent text, resolved from the ramp (light uses the dark ramp, dark uses the light ramp).
  AccentFillColorDefault-light: "#005fb8"
  AccentFillColorDefault-dark: "#60cdff"
  AccentFillColorSecondary-light: "rgba(0, 95, 184, 0.9)"
  AccentFillColorSecondary-dark: "rgba(96, 205, 255, 0.9)"
  AccentFillColorTertiary-light: "rgba(0, 95, 184, 0.8)"
  AccentFillColorTertiary-dark: "rgba(96, 205, 255, 0.8)"
  AccentTextFillColorPrimary-light: "#003e92"
  AccentTextFillColorPrimary-dark: "#99ebff"
  TextOnAccentFillColorPrimary-light: "#ffffff"
  TextOnAccentFillColorPrimary-dark: "#000000"
  TextOnAccentFillColorSelectedText: "#ffffff"
  # Text (WinUI Common_themeresources_any.xaml, MIT, @ 4c45fb8e)
  TextFillColorPrimary-light: "rgba(0, 0, 0, 0.894)"
  TextFillColorPrimary-dark: "#ffffff"
  TextFillColorSecondary-light: "rgba(0, 0, 0, 0.62)"
  TextFillColorSecondary-dark: "rgba(255, 255, 255, 0.773)"
  TextFillColorTertiary-light: "rgba(0, 0, 0, 0.447)"
  TextFillColorTertiary-dark: "rgba(255, 255, 255, 0.529)"
  TextFillColorDisabled-light: "rgba(0, 0, 0, 0.361)"
  TextFillColorDisabled-dark: "rgba(255, 255, 255, 0.365)"
  # Control and subtle fills
  ControlFillColorDefault-light: "rgba(255, 255, 255, 0.702)"
  ControlFillColorDefault-dark: "rgba(255, 255, 255, 0.059)"
  ControlFillColorSecondary-light: "rgba(249, 249, 249, 0.502)"
  ControlFillColorSecondary-dark: "rgba(255, 255, 255, 0.082)"
  ControlFillColorTertiary-light: "rgba(249, 249, 249, 0.302)"
  ControlFillColorTertiary-dark: "rgba(255, 255, 255, 0.031)"
  ControlFillColorDisabled-light: "rgba(249, 249, 249, 0.302)"
  ControlFillColorDisabled-dark: "rgba(255, 255, 255, 0.043)"
  ControlFillColorInputActive-light: "#ffffff"
  ControlFillColorInputActive-dark: "rgba(30, 30, 30, 0.702)"
  ControlStrongFillColorDefault-light: "rgba(0, 0, 0, 0.447)"
  ControlStrongFillColorDefault-dark: "rgba(255, 255, 255, 0.545)"
  ControlAltFillColorSecondary-light: "rgba(0, 0, 0, 0.024)"
  ControlAltFillColorSecondary-dark: "rgba(0, 0, 0, 0.098)"
  SubtleFillColorSecondary-light: "rgba(0, 0, 0, 0.035)"
  SubtleFillColorSecondary-dark: "rgba(255, 255, 255, 0.059)"
  SubtleFillColorTertiary-light: "rgba(0, 0, 0, 0.024)"
  SubtleFillColorTertiary-dark: "rgba(255, 255, 255, 0.039)"
  # Strokes
  ControlStrokeColorDefault-light: "rgba(0, 0, 0, 0.059)"
  ControlStrokeColorDefault-dark: "rgba(255, 255, 255, 0.071)"
  ControlStrokeColorSecondary-light: "rgba(0, 0, 0, 0.161)"
  ControlStrokeColorSecondary-dark: "rgba(255, 255, 255, 0.094)"
  ControlStrongStrokeColorDefault-light: "rgba(0, 0, 0, 0.447)"
  ControlStrongStrokeColorDefault-dark: "rgba(255, 255, 255, 0.545)"
  CardStrokeColorDefault-light: "rgba(0, 0, 0, 0.059)"
  CardStrokeColorDefault-dark: "rgba(0, 0, 0, 0.098)"
  SurfaceStrokeColorDefault: "rgba(117, 117, 117, 0.4)"
  SurfaceStrokeColorFlyout-light: "rgba(0, 0, 0, 0.059)"
  SurfaceStrokeColorFlyout-dark: "rgba(0, 0, 0, 0.2)"
  DividerStrokeColorDefault-light: "rgba(0, 0, 0, 0.059)"
  DividerStrokeColorDefault-dark: "rgba(255, 255, 255, 0.082)"
  FocusStrokeColorOuter-light: "rgba(0, 0, 0, 0.894)"
  FocusStrokeColorOuter-dark: "#ffffff"
  FocusStrokeColorInner-light: "rgba(255, 255, 255, 0.702)"
  FocusStrokeColorInner-dark: "rgba(0, 0, 0, 0.702)"
  # Cards, layers, solid backgrounds, smoke
  CardBackgroundFillColorDefault-light: "rgba(255, 255, 255, 0.702)"
  CardBackgroundFillColorDefault-dark: "rgba(255, 255, 255, 0.051)"
  CardBackgroundFillColorSecondary-light: "rgba(246, 246, 246, 0.502)"
  CardBackgroundFillColorSecondary-dark: "rgba(255, 255, 255, 0.031)"
  LayerFillColorDefault-light: "rgba(255, 255, 255, 0.502)"
  LayerFillColorDefault-dark: "rgba(58, 58, 58, 0.298)"
  SolidBackgroundFillColorBase-light: "#f3f3f3"
  SolidBackgroundFillColorBase-dark: "#202020"
  SolidBackgroundFillColorSecondary-light: "#eeeeee"
  SolidBackgroundFillColorSecondary-dark: "#1c1c1c"
  SolidBackgroundFillColorTertiary-light: "#f9f9f9"
  SolidBackgroundFillColorTertiary-dark: "#282828"
  SolidBackgroundFillColorQuarternary-light: "#ffffff"
  SolidBackgroundFillColorQuarternary-dark: "#2c2c2c"
  SmokeFillColorDefault: "rgba(0, 0, 0, 0.302)"
  # Status
  SystemFillColorSuccess-light: "#0f7b0f"
  SystemFillColorSuccess-dark: "#6ccb5f"
  SystemFillColorCaution-light: "#9d5d00"
  SystemFillColorCaution-dark: "#fce100"
  SystemFillColorCritical-light: "#c42b1c"
  SystemFillColorCritical-dark: "#ff99a4"
  # Materials (tokens.css; recipes in css/base.css)
  mica-luminosity-light: "#f3f3f3"
  mica-luminosity-dark: "#202020"
  mica-tint-light: "rgba(252, 252, 252, 0.5)"
  mica-tint-dark: "rgba(38, 38, 38, 0.89)"
  mica-alt-luminosity-light: "#e6e6e6"
  mica-alt-luminosity-dark: "#1a1a1a"
  mica-alt-tint-light: "rgba(242, 242, 242, 0.65)"
  mica-alt-tint-dark: "rgba(30, 30, 30, 0.89)"
  mica-inactive-light: "#f3f3f3"
  mica-inactive-dark: "#202020"
  acrylic-default-light: "rgba(252, 252, 252, 0.85)"
  acrylic-default-dark: "rgba(44, 44, 44, 0.96)"
  acrylic-base-light: "rgba(243, 243, 243, 0.9)"
  acrylic-base-dark: "rgba(32, 32, 32, 0.92)"
  acrylic-taskbar-light: "rgba(243, 243, 243, 0.85)"
  acrylic-taskbar-dark: "rgba(28, 28, 28, 0.97)"
  wallpaper-fallback-light: "#a8c6ea"
  wallpaper-fallback-dark: "#0b1e3a"
  # Fixed colours Windows draws regardless of theme and accent (named local tokens in their modules)
  caption-close-red: "#c42b1c"
  taskbar-attention-orange: "#f7630c"
  desktop-label-white: "#ffffff"
  desktop-rubber-band: "#0078d7"
  start-mark-top-light: "#3cb6f3"
  start-mark-bottom-light: "#0067c0"
  start-mark-top-dark: "#6fd5ff"
  start-mark-bottom-dark: "#1a8ee6"
typography:
  caption:
    fontFamily: "Segoe UI Variable Text, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "12px"
    fontWeight: 400
    lineHeight: "16px"
  body:
    fontFamily: "Segoe UI Variable Text, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "14px"
    fontWeight: 400
    lineHeight: "20px"
  body-strong:
    fontFamily: "Segoe UI Variable Text, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "14px"
    fontWeight: 600
    lineHeight: "20px"
  body-large:
    fontFamily: "Segoe UI Variable Text, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "18px"
    fontWeight: 400
    lineHeight: "24px"
  subtitle:
    fontFamily: "Segoe UI Variable Display, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "20px"
    fontWeight: 600
    lineHeight: "28px"
  title:
    fontFamily: "Segoe UI Variable Display, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "28px"
    fontWeight: 600
    lineHeight: "36px"
  title-large:
    fontFamily: "Segoe UI Variable Display, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "40px"
    fontWeight: 600
    lineHeight: "52px"
  display:
    fontFamily: "Segoe UI Variable Display, Segoe UI Variable, Segoe UI, Proton UI, Selawik, system-ui, sans-serif"
    fontSize: "68px"
    fontWeight: 600
    lineHeight: "92px"
rounded:
  none: "0px"
  indicator: "1.5px"
  list-item: "3px"
  control: "4px"
  overlay: "8px"
  window: "8px"
  toggle: "10px"
  taskbar-search: "16px"
  start-search: "18px"
spacing:
  hairline: "1px"
  plate-inset: "2px"
  taskbar-gap: "4px"
  resize-border: "8px"
  flyout-gap: "12px"
  card-padding: "16px"
  titlebar-margin: "16px"
  control-height: "32px"
  titlebar: "32px"
  taskbar-item: "40px"
  caption-button: "46px"
  titlebar-tall: "48px"
  taskbar: "48px"
  desktop-grid: "75px"
  explorer-sidebar: "165px"
  quick-settings-width: "360px"
  start-height: "566px"
  start-width: "600px"
components:
  button-standard:
    backgroundColor: "{colors.ControlFillColorDefault-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "4px 11px 6px"
    height: "32px"
  button-standard-hover:
    backgroundColor: "{colors.ControlFillColorSecondary-light}"
  button-standard-pressed:
    backgroundColor: "{colors.ControlFillColorTertiary-light}"
    textColor: "{colors.TextFillColorSecondary-light}"
  button-accent:
    backgroundColor: "{colors.AccentFillColorDefault-light}"
    textColor: "{colors.TextOnAccentFillColorPrimary-light}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "4px 11px 6px"
    height: "32px"
  button-accent-hover:
    backgroundColor: "{colors.AccentFillColorSecondary-light}"
  button-accent-pressed:
    backgroundColor: "{colors.AccentFillColorTertiary-light}"
  button-subtle-hover:
    backgroundColor: "{colors.SubtleFillColorSecondary-light}"
    rounded: "{rounded.control}"
  textbox:
    backgroundColor: "{colors.ControlFillColorDefault-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "5px 10px 5px 11px"
    height: "32px"
  textbox-focus:
    backgroundColor: "{colors.ControlFillColorInputActive-light}"
  card:
    backgroundColor: "{colors.CardBackgroundFillColorDefault-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    rounded: "{rounded.control}"
    padding: "16px"
  menu:
    backgroundColor: "{colors.acrylic-default-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.body}"
    rounded: "{rounded.overlay}"
    padding: "2px 0"
  menu-item:
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "0 11px 1px"
    height: "32px"
  menu-item-hover:
    backgroundColor: "{colors.SubtleFillColorSecondary-light}"
  tooltip:
    backgroundColor: "{colors.acrylic-default-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.caption}"
    rounded: "{rounded.control}"
    padding: "5px 8px 7px"
  nav-item-selected:
    backgroundColor: "{colors.SubtleFillColorSecondary-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.body}"
    rounded: "{rounded.control}"
    padding: "0 12px 1px"
    height: "36px"
  toggle-on:
    backgroundColor: "{colors.AccentFillColorDefault-light}"
    rounded: "{rounded.toggle}"
    width: "40px"
    height: "20px"
  caption-button:
    textColor: "{colors.TextFillColorPrimary-light}"
    rounded: "{rounded.none}"
    width: "46px"
    height: "32px"
  caption-button-hover:
    backgroundColor: "{colors.SubtleFillColorSecondary-light}"
  caption-button-close-hover:
    backgroundColor: "{colors.caption-close-red}"
    textColor: "{colors.TextOnAccentFillColorSelectedText}"
  taskbar:
    backgroundColor: "{colors.acrylic-taskbar-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.caption}"
    height: "48px"
  taskbar-button-hover:
    backgroundColor: "{colors.ControlFillColorDefault-light}"
    rounded: "{rounded.control}"
    size: "40px"
  start-panel:
    backgroundColor: "{colors.acrylic-base-light}"
    textColor: "{colors.TextFillColorPrimary-light}"
    typography: "{typography.body}"
    rounded: "{rounded.overlay}"
    width: "600px"
    height: "566px"
---

# Design System: Mihawk OS Windows 11 Shell

## Overview

**Creative North Star: "Windows 11, Played Straight"**

The visual world is Windows 11 itself, pinned by the user ("a very faithful replica of Windows 11"). There is no house style laid over it and no reinterpretation: Windows' own colour resources, materials, radii, type ramp, glyphs and motion are the system, and a surface is right when it lands where Windows puts it. Success is a person on a couch, at 1920×1080 on a TV, who cannot tell which parts are Windows and which are ours, by look or by behaviour.

This document records the browser **design prototype** at `prototypes/windows11-shell/` (static HTML/CSS/JS, no build step): desktop, taskbar, Start, Search, Task View and Alt+Tab, Quick Settings, notifications and calendar, File Explorer, Settings, Notepad, Calculator, Terminal, Task Manager, shell dialogs, the lock and session screens, and a gallery of classic Win32 controls. **It is not shipped code.** The shipped product is native: Wine's Win32 shell (Explorer, taskbar, Start) drawn through Wine's PS5 user driver. The prototype is the reference that native shell is measured against, and it proves only what it shows in a browser; every native claim is proven on the console separately.

Density is Windows' own: a 32 px control height, 12 px Caption text in shell chrome, 14 px Body in content, hairline strokes, and translucent layers whose depth comes from material and soft elevation shadows rather than from colour blocks. Light and dark are two complete themes of the same resources; the user's accent restyles every accent fill live.

**Key Characteristics:**
- Every colour is a WinUI theme resource referenced by its verbatim name, light and dark.
- Windows are Mica, transient surfaces are Acrylic, the taskbar is its own Acrylic, and each has a solid fallback.
- Two radii carry the whole shell: 8 px for windows and overlays, 4 px for controls; square when maximised or snapped.
- Hierarchy comes from the Windows type ramp at two weights, Regular and Semibold.
- Motion is Windows' grammar: 250 / 167 / 83 ms, entering on (0, 0, 0, 1), exiting on (1, 0, 1, 1).
- Nothing Microsoft-owned (fonts, icons, artwork, wallpaper, binaries) is in the repository.

### Token sources and trust tiers

Every number traces to a source with a trust tier, registered in `docs/windows11-reference/SOURCES.md`. `css/tokens.css` is **generated** by `prototypes/windows11-shell/tools/gen-tokens.py` from `docs/windows11-reference/winui-colors.json`, `tier-a.json` and `themes/windows11/appearance.json`; it is never edited by hand.

- **Tier A, exact (a mismatch is our bug):** Microsoft Learn design pages (geometry, title bar, layering and elevation, timing and easing, typography) and MIT-licensed Microsoft source: WinUI `Common_themeresources_any.xaml` at `4c45fb8e` (83 colour resources per theme), WinUI per-control resources in `winui-controls.json` (331 numeric resources over 40 controls), and Windows Terminal's `MinMaxCloseControl.xaml` at `7d06b26f` (caption buttons 46 px wide, 10 px glyphs, close hover #C42B1C, 150 / 100 ms hover-out).
- **Tier B, good (the user's approved reference and documented Win32 defaults):** `themes/windows11/appearance.json` (taskbar 48 px, Start 600×566, Explorer sidebar 165 px, the approved Mica, Mica Alt and Acrylic target colours) and registry defaults (desktop grid 75 px from `IconSpacing` −1125 twips). Desktop icon sizes 96 / 48 / 32 and the 46×32 caption box are still marked "to verify".
- **Tier C, approximate (about 1 px):** measured from official Microsoft images or video, kept as numbers with their URL. In the build: the point-to-point curve `cubic-bezier(0.8, 0, 0.2, 1)`, and shell animation timings that Microsoft does not publish.
- **Ours, labelled as such:** the mapping of Windows' elevation numbers onto Fluent 2's MIT shadow tokens, the window shadow, the Mica tints fitted over Bloom, and the CSS Acrylic approximation. Each says so in a comment beside its value.

### Motion grammar

Windows' timing and easing (Tier A) carry through every surface; `Shell.animate` defaults to 250 ms on the entering curve, and CSS uses `--dur-normal | fast | faster` with `--ease-in | ease-out`.

- **Durations:** normal 250 ms (enter, slide, geometry), fast 167 ms (exit, fades, state slides), faster 83 ms (hover fills in, quick fades).
- **Curves:** entering `cubic-bezier(0, 0, 0, 1)` (fast out, slow in); exiting `cubic-bezier(1, 0, 1, 1)` (slow out, fast in); point-to-point `cubic-bezier(0.8, 0, 0.2, 1)` (Tier C), used for the NavigationView selection pill's 400 ms stretch and the taskbar's auto-hide slide.
- **Hover plates:** fade in over 83 ms. Shell plates (taskbar buttons, Start items, caption buttons, Snap zones) fade back out over 150 ms, Windows Terminal's caption value; WinUI controls fade linearly at 83 ms both ways.
- **Signature motions:** window open scales 0.9→1 with a fade (250 ms, entering); close scales to 0.95 and fades (167 ms, exiting); minimise and restore fly to and from the taskbar button. Start and Search rise 40 px (transform 250 ms entering, fade 167 ms linear) and fall back on the exiting curve at 167 ms. Menus and submenus reveal by sliding out from behind the edge that faces their anchor, clipped at that edge (up to 56 px, 250 ms entering) while fading in over 83 ms; a ComboBox list unfolds from its selected row (250 ms entering); an expander's body slides down from under its header (250 ms entering, 167 ms exiting). Snap previews grow from the pointer (250 ms entering). Toggle thumbs slide 167 ms. Taskbar glyphs shrink to 0.9 while pressed (Start to 0.82) and settle on release with a barely visible spring, `cubic-bezier(0.34, 1.2, 0.64, 1)` over 250 ms.
- **Reduced motion:** with `prefers-reduced-motion` or Settings' Animation effects off, every transition, animation and delay is 0 ms and surfaces land in their final state at once.

## Colors

The palette is WinUI's neutral, alpha-layered resource set, light and dark, with one user accent; colour almost never fills a surface, it tints translucent layers and marks state.

### Primary
- **Windows Blue accent** (SystemAccentColor ramp, default #0078D4): the user's accent, recomputed live from Settings > Personalization > Colors (an HSL approximation in `js/core.js` that lands the default on Windows' shipped Light2 and Dark1). Light theme fills with the dark ramp (AccentFillColorDefault = Dark1), accent text one step darker (Dark2). Dark theme fills with the light ramp (AccentFillColorDefault = Light2), accent text Light3. Text on accent is white in light and black in dark. Used for accent buttons, checked toggles, checkboxes and radios, slider fill, the focused textbox's bottom stroke, the NavigationView and ComboBox selection pills, the focused app's 16×3 taskbar pill, the calendar's today circle, progress, badges and text selection.

### Neutral
- **Text fills** (TextFillColorPrimary, Secondary, Tertiary, Disabled): black at 89 / 62 / 45 / 36 % in light, white at 100 / 77 / 53 / 37 % in dark. Primary for content and titles; Secondary for placeholders, shortcuts, chevrons and pressed text; Tertiary for an inactive window's title.
- **Control fills** (ControlFillColorDefault, Secondary, Tertiary, Disabled, InputActive): the rest, hover, pressed and disabled plates of buttons, textboxes, combo boxes, clickable cards and taskbar buttons; InputActive is the focused textbox.
- **Subtle fills** (SubtleFillColorSecondary, Tertiary): the hover and pressed plates of anything that is clear at rest: menu items, nav items, caption buttons, command-row buttons, subtle buttons.
- **Strokes** (ControlStrokeColorDefault, Secondary, ControlStrongStrokeColorDefault, CardStrokeColorDefault, SurfaceStrokeColorDefault, SurfaceStrokeColorFlyout, DividerStrokeColorDefault): 1 px hairlines. SurfaceStrokeColorDefault rims windows; SurfaceStrokeColorFlyout rims menus, flyouts and tooltips; ControlStrongStroke is the textbox underline and the unchecked toggle and checkbox border.
- **Backgrounds** (CardBackgroundFillColorDefault, Secondary, LayerFillColorDefault, SolidBackgroundFillColorBase to Quarternary, SmokeFillColorDefault): settings cards and expander bodies, in-window layers, solid windows and the transparency-off fallbacks, and the smoke behind modal surfaces.
- **Focus strokes** (FocusStrokeColorOuter, Inner): the two-ring keyboard focus visual.

### Status
- **Success, Caution, Critical** (SystemFillColorSuccess, Caution, Critical): progress states (paused is Caution, error is Critical), InfoBadges and dialog icons; Critical is the same red as the caption close plate in light theme.

### Fixed colours
Windows draws a few colours that no WinUI resource carries and that ignore theme or accent. Each is a named custom property at the top of its own module, with its Windows source in a comment:
- **Caption close red** (#C42B1C, both themes): the close button's hover plate on windows, Snap Assist and Task View.
- **Attention orange** (#F7630C): the taskbar's flash and pill when an app requests attention.
- **Desktop label white and plates:** desktop labels are white with a soft dark text shadow, and the hover, selected and focus plates are translucent white or pale blue with a 1 px lighter border, the same in both themes because they sit on the wallpaper.
- **Desktop rubber band** (#0078D7, `COLOR_HIGHLIGHT`): the selection rectangle, which does not follow the accent.
- **Mihawk mark** (face #FFFFFF with a #C9D3DD edge, amber frame #FFB020, navy M #0E2B46; `themes/windows11/start.svg`): the Start button's glyph, the project's own mark at 22 px, the same in both themes and every accent.
- **Classic Win32 palette** (Windows controls app only): GetSysColor defaults and aero.msstyles part colours, named after the system colour or class/part (`--COLOR_BTNFACE` #F0F0F0 and so on), because classic programs ignore the accent and, mostly, dark mode.

### Named Rules
**The Resource Name Rule.** Every colour is a WinUI resource referenced by its verbatim name (`var(--SubtleFillColorSecondary)`), so light, dark and the accent restyle everything live. A literal exists only where Windows itself draws a fixed colour no resource carries, and then it is a named local token with its source beside it.

**The Accent Ramp Rule.** Light theme fills with the darker ramp (Dark1) and writes accent text with Dark2; dark theme fills with the lighter ramp (Light2) and writes accent text with Light3. Never fill with SystemAccentColor itself.

## Typography

**UI Font:** Segoe UI Variable Text when installed on the viewing PC, otherwise Proton UI (with Selawik, then system-ui)
**Display Font:** Segoe UI Variable Display when installed, otherwise the same Proton UI faces

**Character:** a humanist sans set small and quiet, Windows' own voice: Caption-sized chrome and Body-sized content, with Semibold doing all of the emphasis.

**Font substitution.** No Microsoft font is in the repository. The prototype declares the family "Proton UI" from the five Selawik TTFs in `themes/windows11/fonts/upstream/` (Light 300, Semilight 350, Regular 400, Semibold 600, Bold 700; OFL-1.1). Selawik is the open font metrically compatible with Segoe UI. The native shell uses the built Proton UI derivative (`tools/build-proton-ui-font.py`: renamed family, kerning pairs, same outlines). Segoe UI Variable is used only when the PC viewing the prototype already has it. Glyph shapes therefore differ from Windows by design; metrics and the ramp do not.

**Monospace and emoji.** Terminal and Notepad use Cascadia Mono, Microsoft's Terminal face under the SIL OFL 1.1, self-hosted from release v2407.24 (`assets/fonts/cascadia/`, after Consolas, which is used when installed, as Windows Notepad does). The emoji panel draws Fluent Emoji in the Color style that Windows 11 uses (MIT, `microsoft/fluentui-emoji` at `1ffb34c`, the 347 the panel shows, `assets/emoji/`). Choosing an emoji inserts the character, so text fields render it in the viewer's emoji font.

### Hierarchy
- **Display** (600, 68px/92px): reserved; the ramp's top step.
- **Title Large** (600, 40px/52px): large figures, such as Settings' Date & time clock.
- **Title** (600, 28px/36px): page titles (the Settings breadcrumb header, Task Manager's page headings) and the sign-in name.
- **Subtitle** (600, 20px/28px): dialog titles, Settings' home and device headers, Search's best-match name, Task Manager's title, Calculator's mode.
- **Body Large** (400, 18px/24px): used sparingly, for Task Manager's Performance figures.
- **Body Strong** (600, 14px/20px): group headers (Start's "Pinned" and "Recommended"), flyout and notification titles, the calendar heading, clipboard-panel and dialog section headings. Written inline as Body at weight 600; there is no separate token.
- **Body** (400, 14px/20px): content, controls, menu items, nav items, search fields.
- **Caption** (400, 12px/16px): the shell's workhorse: window titles, taskbar text and the two-line clock, tooltips, menu shortcuts, Start pin labels, status bars.

Sentence case everywhere. Minimums are Windows': 12 px Regular, 14 px Semibold.

### Named Rules
**The Two Weights Rule.** Regular (400) and Semibold (600) carry the shell. Hierarchy comes from the ramp's sizes and the Display optical cut, never from Bold; Semilight (350) appears only on the Shut Down dialog's wordmark, where Windows draws it.

**The Ramp-Only Rule.** Every text size is a step of the Windows ramp. A size outside it is allowed only where a specific Windows surface draws one (the lock-screen clock and date, which scale with the screen; the Calculator display; the classic task dialog's 16 px instruction and 15 px command links; the Shut Down dialog's 28 px wordmark).

## Layout

The target is 1920×1080 at 96 DPI; every surface works from 360×640 to 2560×1440, recomputes on resize, keeps itself inside the viewport above the taskbar, and never causes page scroll. Layout is Windows' own fixed metrics, not a fluid grid.

- **Layers and z-order** (bottom to top, `Shell.layer(name)`): wallpaper 0, desktop 1, windows 2, taskbar 3, flyouts 4, overlays 5, menus 6, tooltips 7. Every module mounts into its layer; nothing else sets z-index across layers. Within the windows layer, z-order follows focus.
- **Work area:** the viewport minus the taskbar reserve (48 px, or 0 when the taskbar auto-hides). Windows, desktop columns and snap zones all live inside it.
- **Desktop:** a 75 px grid (Tier B) down the left, icon sizes 96 / 48 / 32 px for Large / Medium / Small; columns reflow with the viewport height and never sit under the taskbar.
- **Taskbar:** 48 px; 40×40 buttons with 2 px side margins (4 px gaps); the group is centred by default or left-aligned by setting; the 220×32 search pill; the tray at right with the two-line clock; an 8 px "Show desktop" sliver at the far edge. Below about 700 px the tray collapses and apps overflow into a chevron flyout.
- **Start and Search:** 600×566, 12 px above the taskbar, clamped to the viewport; six pin columns of 91×76 cells.
- **Flyouts** (Quick Settings, notifications and calendar): anchored bottom-right with 12 px gaps; Quick Settings is 360 px wide.
- **Windows:** title bar 32 px (48 px tall variant for title bars carrying tabs or a search box: Explorer, Settings, Notepad, Terminal, Task Manager); icon 16 px at a 16 px margin, then the Caption title 16 px further; caption buttons 46 px wide, full title-bar height. Invisible 8 px resize borders sit outside the visible frame. Snap targets are halves, quarters and the top edge; Snap Layouts follow the screen's aspect.
- **Responsive collapses:** Settings' nav pane becomes a hamburger overlay below about 1000 px; Explorer's nav pane hides below about 640 px window width and its command-bar labels collapse into "…".

## Elevation & Depth

Depth is a hybrid: materials say what a surface is (a window, a transient flyout, the taskbar), soft layered shadows say how high it sits, and 1 px hairline strokes define every edge. Nothing is flat-coloured on top of the wallpaper.

### Materials
- **Mica** (windows): the wallpaper, pre-blurred once per wallpaper and attached `fixed` so it lines up with the screen, under a luminosity layer that keeps the wallpaper's hue but pins its lightness, under a tint. Windows' recipe (luminosity #F3F3F3, tint #F3F3F3 at 0.5 in light), with the tints fitted so a window over Bloom lands on the approved appearance.json Mica (#EEF2F8 light, #22262D dark). An inactive window's Mica fades to the solid `mica-inactive` colour over 167 ms, as Windows does.
- **Mica Alt** (windows with tabbed title bars, such as File Explorer): the same recipe with a darker luminosity layer (and, in light, a stronger tint), landing near #E3E8F0 / #1B1E24.
- **Acrylic** (menus, tooltips, ComboBox popups, Quick Settings, notifications and toasts, thumbnails, the OSD and clipboard panel, Snap Layouts and Snap Assist): a translucent tint over a 30 px backdrop blur, plus a generated 128 px noise texture at 2 % in overlay blend. CSS cannot luminosity-blend with a backdrop, so in light theme the blurred backdrop's contrast is squeezed (0.15) and lifted (1.95) instead; light flyouts land near #F7F9FC wherever they open. Dark theme blurs and saturates (125 %) under a near-opaque tint.
- **Acrylic base** (Start and Search): the denser base tint of the same recipe.
- **Taskbar acrylic:** its own tint; in dark theme it is the darkest shell layer (about #1C1C1C–#202020, measured over Bloom), never lighter than the flyouts above it.
- **Transparency effects off** (Settings > Personalization > Colors): every material turns solid. Acrylic becomes SolidBackgroundFillColorQuarternary, Acrylic base becomes SolidBackgroundFillColorBase, Mica and Mica Alt become `mica-inactive`, the taskbar becomes SolidBackgroundFillColorSecondary.

### Shadow Vocabulary
Microsoft publishes elevation values (window and dialog 128, flyout 32, tooltip 16, card 8, control 2) but not the shadows behind them. The build maps them onto Fluent 2's MIT shadow tokens, at double the opacity in dark theme:
- **Control** (`box-shadow: 0 0 2px rgba(0,0,0,0.12), 0 1px 2px rgba(0,0,0,0.14)`): small raised keys and chips, such as the Win+Z layout number keys.
- **Card / tooltip** (`box-shadow: 0 0 2px rgba(0,0,0,0.12), 0 4px 8px rgba(0,0,0,0.14)`): tooltips and lifted cards.
- **Drag** (`box-shadow: 0 0 2px rgba(0,0,0,0.12), 0 8px 16px rgba(0,0,0,0.14)`): an item lifted while dragged, such as a Quick Settings tile.
- **Flyout** (`box-shadow: 0 0 8px rgba(0,0,0,0.12), 0 14px 28px rgba(0,0,0,0.14)`): every `.surface-flyout` (menus, Start, Search, Quick Settings, notifications), the snap preview and Snap Assist.
- **Dialog** (`box-shadow: 0 0 8px rgba(0,0,0,0.12), 0 32px 64px rgba(0,0,0,0.14)`): modal surfaces: the UAC card, in-app content dialogs (Notepad, Settings, Task Manager) and Task View's window grid.
- **Window, active** (`box-shadow: 0 0 0 1px rgba(0,0,0,0.06), 0 2px 8px rgba(0,0,0,0.1), 0 16px 48px rgba(0,0,0,0.22)`) and **inactive** (`0 0 0 1px rgba(0,0,0,0.05), 0 2px 6px rgba(0,0,0,0.08), 0 8px 24px rgba(0,0,0,0.12)`): the window frame, cross-fading over 167 ms on focus. A maximised window has no shadow and no border.

### Named Rules
**The Material Hierarchy Rule.** Windows are Mica (Mica Alt under a tabbed title bar), transient surfaces are Acrylic (Start and Search on Acrylic base), the taskbar is its own Acrylic. Use the `.mica`, `.mica-alt`, `.acrylic`, `.acrylic-base` and `.surface-flyout` classes; never re-derive a recipe.

**The Solid Fallback Rule.** Every material has a solid twin under `data-transparency="off"`, and Mica turns solid whenever its window is inactive. A surface that cannot fall back is unfinished.

## Shapes

The form language is Windows 11 geometry (Tier A): softly rounded rectangles in two sizes, 1 px hairlines, and square edges wherever a surface meets the screen.

- **Overlay and window corners (8 px):** windows, menus, flyouts, Start and Search, notification cards, Snap Layouts, the snap preview.
- **Control corners (4 px):** buttons, textboxes, combo boxes, cards and expanders, nav items, hover plates, tooltips, taskbar buttons; 3 px for ComboBox items, as WinUI declares.
- **Square:** a maximised or snapped window, caption buttons (full-bleed backplates), and anywhere a straight edge meets a straight edge (an open expander's header and body, the two halves of a split button).
- **Pills:** the 3 px selection and running indicators (1.5 px radius; 3×16 vertical in NavigationView and ComboBox, 16×3 under SelectorBar items and the focused taskbar app, 6×3 grey for a running one); the 40×20 toggle track (10 px); the taskbar search (220×32, 16 px) and Start search (36 px tall, 18 px) pills; round avatars, radios and the progress ring.
- **Strokes:** every edge is a 1 px hairline. Raised controls carry WinUI's elevation border: the bottom edge darker in light theme, the top edge lighter in dark theme. Text inputs carry a strong bottom stroke that turns into a 2 px accent line on focus.
- **Scrollbars:** Windows 11 overlay style, thin at rest and wider on hover, ControlStrongFillColorDefault on a clear track.

### Named Rules
**The Two Radii Rule.** 8 px for anything top-level, 4 px for anything inside it. Corners go square only where Windows squares them: maximised, snapped, or straight edge against straight edge.

## Components

Every component is the WinUI or shell control it names, with WinUI's metrics (`winui-controls.json`) and its rest, hover, pressed, disabled and focus-visible states. Classes live in `css/ui.css`; small behaviours in `Shell.ui`.

### Buttons
- **Shape:** gently rounded (4 px), 32 px tall, padding 4/11/6 px (WinUI ButtonPadding 11,5,11,6 less the 1 px border).
- **Standard:** ControlFillColorDefault plate with the elevation border; Body text. Hover ControlFillColorSecondary; pressed ControlFillColorTertiary with Secondary text; disabled ControlFillColorDisabled with Disabled text.
- **Accent:** AccentFillColorDefault with TextOnAccentFillColorPrimary; hover and pressed step to the 90 % and 80 % accent fills.
- **Subtle:** clear at rest, SubtleFillColorSecondary on hover, Tertiary pressed. Icon-only buttons are 32×32.
- **Hyperlink:** accent text on a subtle plate (inline variant underlined); the link hand cursor belongs to hyperlinks and links only.
- **Focus:** WinUI's focus visual, a 2 px FocusStrokeColorOuter ring with a 1 px FocusStrokeColorInner ring, outside the control.

### Selection controls
- **ToggleSwitch:** 40×20 track with a 1 px strong stroke; the 12 px thumb grows to 14 px on hover and stretches to 17×14 pressed, sliding 167 ms on the entering curve. On, the track fills with the accent and the thumb turns TextOnAccent. "On" / "Off" label beside it.
- **CheckBox and RadioButton:** 20 px boxes (4 px radius, or round) on ControlAltFillColorSecondary with a strong stroke; checked fills with the accent. The check draws in from the left over 167 ms; the radio dot scales from 0, grows on hover (×1.17) and shrinks pressed (×0.83).
- **Slider:** a 4 px track with the accent fill; a 20 px thumb whose accent dot shows inside a solid ring that thins on hover and thickens pressed.

### Inputs / Fields
- **Style:** 32 px, 4 px radius, ControlFillColorDefault, 1 px ControlStrokeColorDefault with a ControlStrongStroke bottom edge; placeholder in Secondary text.
- **Focus:** the field turns ControlFillColorInputActive and its bottom becomes a 2 px accent line; no outer focus ring.
- **SearchBox:** the textbox with Fluent's 16 px search glyph at right; optional clear button. **ComboBox:** a standard button with a chevron that nudges down 2 px when pressed; its Acrylic list opens over the control with the selected item aligned, the selected row marked by the 3×16 accent pill.
- **Disabled:** ControlFillColorDisabled with Disabled text.

### Cards / Containers
- **Settings card:** 4 px radius, CardBackgroundFillColorDefault, 1 px CardStrokeColorDefault, 16 px padding; cards stack with 4 px gaps. A clickable card takes ControlFillColorSecondary on hover.
- **Expander:** 48 px header card with a 32 px chevron button that rotates 180°; its body (CardBackgroundFillColorSecondary, 16 px padding) slides out from under the header.
- **Shadow strategy:** cards are flat; depth comes from the card fill against Mica.

### Navigation
- **NavigationView item:** 36 px, 2/4 px margins, 4 px radius, glyph and Body label. Selected and hover take SubtleFillColorSecondary; the 3×16 accent pill at the left edge stretches toward the next item and settles (400 ms) when the selection moves.
- **SelectorBar / segmented:** text items with a 16×3 accent pill under the selected one.
- **Title-bar tabs** (Explorer, Notepad, Terminal) sit in the 48 px tall title bar.

### Menus (MenuFlyout)
- **Surface:** Acrylic, 8 px radius, SurfaceStrokeColorFlyout hairline, flyout shadow, 2 px vertical padding, minimum 200 px wide, kept inside the viewport.
- **Items:** 32 px rows whose 28 px hover plate sits 2 px inside (4 px radius, SubtleFillColorSecondary); a 16 px icon column at left, Body label, right-aligned Caption shortcut in Secondary text, submenu chevron; check and radio marks; separators are 1 px DividerStrokeColorDefault; group headers in Caption Semibold for jump lists.
- **Command row:** Windows 11's Cut / Copy / Rename / Share / Delete row of 40 px icon cells at the top (or bottom when the menu opens upward).
- **Behaviour:** full keyboard (arrows, Enter, Esc, Left/Right for submenus, first-letter access), access-key underlines only with keyboard cues, submenus on hover, reveal from the anchor side.

### Tooltips
- Acrylic, 4 px radius, SurfaceStrokeColorFlyout hairline, tooltip shadow, Caption text, padding 5/8/7 px, at most 320 px wide. Shown after 500 ms of hover (100 ms when another tip just closed), above the pointer in the taskbar and below it elsewhere; hidden on press.

### Flyouts
- `.surface-flyout`: 8 px radius, SurfaceStrokeColorFlyout hairline, flyout shadow, on Acrylic. One transient per group is open at a time (Start, Search, Quick Settings, notifications and Task View share one group; context menus another); a press outside light-dismisses it, and pressing its own trigger toggles it.

### Windows
- **Frame:** Mica (default), Mica Alt, solid or clear body; 1 px SurfaceStrokeColorDefault rim; 8 px radius; active and inactive shadows.
- **Title bar:** 32 px (48 px tall variant); Caption title; drag from empty space, double-click to maximise, right-click for the system menu. Inactive: title in Tertiary text, icon at 60 %, caption glyphs in Disabled text.
- **Caption buttons:** 46 px wide, square backplates, 10 px glyphs drawn as 1 px strokes. Minimise and maximise hover SubtleFillColorSecondary, pressed Tertiary; close hovers caption close red with white glyph. Hover-in 83 ms, hover-out 150 ms background and 100 ms glyph.
- **Snap:** the translucent accent-tinted Acrylic preview grows from the pointer; Snap Layouts open after about 500 ms over Maximize or on Win+Z, zones fill with the accent on hover; Snap Assist offers the other windows for the remaining zone.
- **Not responding:** after about 5 s the title reads "(Not Responding)" and the client area washes white (50 %) in both themes, as DWM's ghost window does.

### Taskbar
- 48 px taskbar acrylic with a 1 px DividerStroke top line. Buttons are 40×40 with a 4 px radius: clear at rest, ControlFillColorDefault plate plus ControlStrokeColorDefault hairline on hover, open or active, Secondary over an active one, Tertiary pressed. Running apps show a 6×3 grey pill, the focused app a 16×3 accent pill, widening over 250 ms. Progress replaces the pill (green, yellow paused, red error); attention flashes orange seven times and then holds the orange pill.

### Desktop items
- Icon over a white, text-shadowed label of up to two lines (the full name when selected). Hover, selected, selected-and-hover and focus plates are translucent white or pale blue with a 1 px lighter border; the plate fades out more slowly than it arrives. Shortcuts carry the 16 px arrow badge.

### Icons and cursors
- **Command glyphs:** Fluent System Icons, MIT, `microsoft/fluentui-system-icons` at `cd332ec1f90cda2fad16f86754f8da52f1e9ab30`, 20 px Regular (and a few Filled) drawn in `currentColor`; each file's upstream path and SHA-256 are in `assets/icons/fluent/upstream.json`. Only the names listed in `CONTRACT.md` are used.
- **Object icons:** colour icons for folders, drives, documents and apps. The user-approved originals from `themes/windows11/icons/` (copied with their hashes) plus originals authored for the prototype in the same style; new ones are recorded in the icon manifest.
- **Caption glyphs:** drawn as 10 px vector paths in `js/wm.js`, matching Windows' minimise, maximise, restore and close shapes; no Segoe Fluent Icons font is used.
- **Cursors:** original SVG artwork in the Windows pointer scheme's shape language (white fill, 1 px black outline, soft pointer shadow), 32 px with recorded hotspots; busy and working rings animate in 12 stepped frames. Windows shows the arrow, not the hand, over buttons, menus and the taskbar.

### Classic Win32 controls (Windows controls app)
- The visual target for the Wine msstyles theme that classic programs draw with: push buttons, check boxes, edits, combo boxes, list and tree views, tabs, trackbars, progress bars, scroll bars, menus and dialogs as Windows 11 draws them in a non-WinUI program. Their colours are the classic palette, not WinUI resources; in dark mode only the controls that opt into the dark Explorer themes change, so the gallery is honest about where Wine's dark theme goes beyond Windows.

## Do's and Don'ts

### Do:
- **Do** reference colours by their WinUI resource name (`var(--ControlFillColorDefault)`) so light, dark and the accent restyle every surface live.
- **Do** regenerate `css/tokens.css` with `tools/gen-tokens.py` after changing `docs/windows11-reference/` or `themes/windows11/appearance.json`, and register any new number with its source and tier before using it.
- **Do** use the material classes in `css/base.css` (`.mica`, `.mica-alt`, `.acrylic`, `.acrylic-base`, `.surface-flyout`) and give every new material a solid fallback under `data-transparency="off"`.
- **Do** enter on `cubic-bezier(0, 0, 0, 1)` and exit on `cubic-bezier(1, 0, 1, 1)`, at 250 / 167 / 83 ms, through `Shell.animate` or the `--dur-*` and `--ease-*` tokens, and land at once when reduced motion or Animation effects off is set.
- **Do** keep 8 px corners on top-level surfaces and 4 px on controls, and square them when a window is maximised or snapped.
- **Do** keep every surface inside the viewport above the taskbar, from 360×640 to 2560×1440, without page scroll.
- **Do** keep the accent off the Start mark and off what Windows keeps fixed: the caption close red, the desktop rubber band, the attention orange and classic Win32 controls.
- **Do** show the arrow cursor over buttons, menu items and the taskbar; the link hand belongs to hyperlinks.

### Don't:
- **Don't** put Microsoft fonts, icons, artwork, wallpaper or binaries in the repository; the desktop's wallpaper is Bubble City by Skeuoss, used with the artist's permission (`themes/windows11/wallpapers/`, since 2026-10-31; NOTICE.md); the browser prototype's `serve.sh` still draws the earlier Aero Meadow with `tools/make-mihawk-wallpaper.py` (light and night).
- **Don't** hand-edit `css/tokens.css` or write ad-hoc hex values in a module; a fixed Windows colour gets a named local token with its source.
- **Don't** replace Mica or Acrylic with flat stand-ins, or render a control as a generic web widget; each control is the WinUI or Win32 control it names, with its metrics and states.
- **Don't** add curves or durations outside Windows' grammar; the taskbar glyph's release spring is Windows' own and stays there.
- **Don't** set text in Bold or at sizes off the Windows ramp.
- **Don't** present a Tier C or fitted value as an official Microsoft number.
- **Don't** treat the prototype as shipped code or as proof about the PS5; the native shell's look, cost and behaviour are proven on the console.
