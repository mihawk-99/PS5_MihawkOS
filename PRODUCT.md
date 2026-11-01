# Product

<!-- impeccable:product-schema 1 -->

## Platform

web

## Stack

Static HTML/CSS/JS, no build step and no dependencies (user's answer, 2026-10-15), for design prototypes under `prototypes/`. The shipped product is native: Wine's Win32 shell (Explorer, taskbar, Start) drawn through Wine's PS5 user driver; a prototype is the reference the native shell is measured against, not shipped code.

## Users

One person running Windows games and programs on their own jailbroken PS5 through this Wine runtime ("Mihawk OS"), on a TV at 1920×1080, with a USB mouse and keyboard or the DualSense touchpad. They expect the desktop to look and behave like Windows 11 so nothing needs learning.

## Product Purpose

Launch 64-bit and 32-bit Windows programs on the PS5 and give them a desktop that feels like Windows 11: desktop icons and shortcuts, a taskbar, Start, File Explorer and Settings. Success is a user who could not tell which parts are Windows and which are ours, by look or by behaviour.

## Positioning

A Windows 11 desktop that runs on a PS5 through Wine, built from Windows 11's published design numbers and open assets, with no Microsoft binaries, fonts or artwork.

## Operating Context

Played from a couch on a TV. Games run full screen and the desktop unloads during a game; the shell is what the user sees between games. The user has no Windows 11 machine; the reference is Microsoft's published numbers (`docs/windows11-reference/`) and the user's own judgement of prototypes on their PC.

## Capabilities and Constraints

- Fidelity target: Windows 11 layout, spacing, proportions, colours, corner radii, shadows, states, motion and behaviour (`docs/WINDOWS11_PLAN.md`).
- No Microsoft fonts, icons, wallpapers or binaries in the repository. Glyph shapes differ by design: the font is Proton UI (a Selawik derivative, OFL), object icons are the repository's original set, command glyphs are MIT Fluent System Icons at a pinned revision.
- Canonical tokens: `themes/windows11/appearance.json`, `docs/windows11-reference/` (Tier A/B/C numbers with sources).
- Initial target 1920×1080, 96 DPI; 125/150% scaling is later work.
- Every native claim is proven on the console (AGENTS.md rule 7); prototypes prove only what they show in a browser.
- Undecided: controller-driven desktop navigation in prototypes (the POC of 2026-10-15 is mouse and keyboard only).

## Brand Commitments

- The visual world is Windows 11, played straight. The user's words: "a very faithful replica of Windows 11".
- The user approved the original object icons and the Fluent command glyphs in `themes/windows11/icons/`, and the Proton UI font.
- The wallpaper is Bubble City by Skeuoss (`themes/windows11/wallpapers/skeuoss-bubble-city.jpg`, used with the artist's permission; the owner's choice on 2026-10-31, NOTICE.md). It replaced the project's own Bubble Earth (`tools/make-aero-wallpapers.py`, 2026-10-25, with Glass City and Aqua World beside it), which replaced the project's Aero Meadow (`tools/make-mihawk-wallpaper.py`, light and night), which had replaced the owner's local copy of Windows 11's Bloom on 2026-10-18, never vendored.

## Evidence on Hand

- Reference numbers: `docs/windows11-reference/tier-a.json`, `tier-b.json`, `winui-colors.json` (83 colour resources per theme), `winui-controls.json`.
- Tokens: `themes/windows11/appearance.json`. Icons: `themes/windows11/icons/`. Font inputs: `themes/windows11/fonts/upstream/` (Selawik TTFs, OFL).
- No Windows 11 screenshots are kept; Tier C numbers carry their source URL only.

## Product Principles

1. Windows 11's behaviour is the specification; when in doubt, do what Windows does.
2. Every number traces to a source with a trust tier.
3. Nothing Microsoft-owned enters the repository.
4. The desktop never costs a game performance or memory.
