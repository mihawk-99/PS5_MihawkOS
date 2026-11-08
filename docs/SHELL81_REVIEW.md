# Shell 8.1 prototype match: review of the paused agent's work (2026-11-06)

Branch `shell-prototype-81` from the published Alpha 8.1 (`ed5cd48a`). A previous agent (DeepSeek) left uncommitted
changes. This note says what was kept, what was parked, and why. Nothing here is gated or committed yet.

## Kept (looks only, inside the handoff's boundaries)

- **Quick Settings, the prototype's whole panel**: `wine/ps5/pw_shell_flyouts.c`, `themes/windows11/flyouts/quicksettings-grid-*.json`,
  `tools/build-shell-sprites.py`, `tools/measure-prototype-sprites.mjs`. Plates, glyphs and sliders are the prototype's own
  pixels. The Aero palette, wallpaper and logo are untouched.
- **Message box as the prototype draws it**: patch 2490 (user32 only), `tools/build-dialog-sprites.py`,
  `themes/windows11/dialogs/`, `tools/build-wine-ps5.sh` (one copy line). Pure drawing and layout.
- **Scorecard probe**: hover captures without the item's tooltip (`tests/shell-scorecard-probe.c`).
- **`tools/console-setup.py` pins firmware 13600007** (the base). Its comment now says so; the Pro's 10.01 is no longer
  the target of this setup.
- **`docs/PLAN.md`**: three honest sections about the refreshed 172-state reference and the first scorecard run, with one
  correction (below).

## Owner decisions of 2026-11-06 and what was built on the base

Relayed by the Design session: (1) the wallpaper picker and a "make a shortcut" button are wanted, beyond looks only;
the picker belongs in the **desktop's own Settings, Personalization > Background** (not the launcher's main menu: a first
launcher page was built, rejected, and removed); (2) shortcuts are made **from File Explorer**; (3) similar, not
pixel-exact; (4) no "Windows" wording: MihawkOS instead.

- **Wallpaper** (`src/pw_wallpaper.*`, `src/pw_settings.c`, patch-free). The shipped pictures (Bubble City by Skeuoss, the
  default and credited in NOTICE.md, plus MihawkOS's own Bubble Earth, Glass City and Aqua World) live once in the runtime
  (`share/mihawk/wallpapers`), so a prefix made before them has them too. Settings > Personalization > Background shows
  them with previews, chooses by pointer or arrow keys and Enter, and writes the title's `wallpaper.txt`
  ("wallpaper <id>", the existing one-line setting style, hand-writable; a BMP copied into the prefix's wallpaper
  folder answers to its own name). The title copies the choice over the managed `proton.bmp` before Explorer starts, so it
  shows **from the next MihawkOS start**. A live repaint needs the parked Wine patches 2492 and 2493 (win32u's background
  decode and Explorer's desktop painter): not taken, risky, listed for the owner. Browse photos fits a BMP to the screen
  and keeps it as `custom.bmp`.
- **Shortcuts** (`src/pw_shortcut.h`, `wine/ps5/pw_shell_shortcut.h`). File Explorer's **More** menu gains Create shortcut
  (a "<name> - Shortcut.lnk" on the user's Desktop, which the title's own manifest never touches) and Pin to Start (a link
  in Start's Programs plus an entry in Start's existing pin list); the desktop's classic right-click menu has both too.
  Every link carries an icon (Explorer's desktop drops one without). Not done: the toolbar has no new button of its own
  (More is its overflow), and Explorer's right-click menu has neither item (shell32's menu is Wine's).
- **Settings app** (`mihawk-settings.exe`): the parked one, repaired: it opened on System because the program's path holds
  "system32"; the account card hid the Home row; the text fell back to a serif face (the shell's face is Proton UI); the
  previews were blank (`LR_LOADFROMFILE` was missing). Start, Quick Settings, the taskbar pin and the desktop menu open it.
- **Wording**: the guided setup's and Settings' 20 strings in all 10 languages, the launcher's "Windows game library", the
  title's complaints and the appearance app now say MihawkOS or PC. `tests/test_no_windows_wording.py` fails on any user
  string with the word. **Not done**: `webui/i18n.js` (144 strings, 10 languages, a browser page) and Wine's own resource
  strings (e.g. shell32's "Windows cannot find"); the shell stays in English by design. The mechanical replacement in the
  non-English tables needs a native-speaker read.
- **Console proof on the base** (key 950f10f8671b, firmware 13600007): `shell81-wallpaper-r3-03` 9/9 (the title applied the
  choice; the desktop's pixels are within 0.0 levels of the chosen picture and 24.5 from the default; Settings drove the
  choice with the keys), `shell81-view-r3-03` 50/50 (File Explorer's More menu made both shortcuts, the desktop listed and
  dropped one, Start's pin list named the other). Scorecard `shell81-base-02`: 27.4% / 62.0%, unchanged by this batch.
- **Console-setup** now accepts both firmwares (10.01 and 13.60) instead of re-pinning one.

## Round 2 (2026-11-07): shortcuts that work, live wallpaper, wording

- **Create shortcut** (patch 2494, shell32). The item menu the owner met said "Create link" and did nothing on the
  desktop: shell32's `FCIDM_SHVIEW_CREATELINK` had no handler ("Unhandled verb"). Now it is
  "Create &shortcut" (all 49 `po` files; translated in the 10 MihawkOS languages) and makes "<name> - Shortcut.lnk" through
  `pw_shell_make_shortcut`; "Pi&n to Start" sits under it. This is the menu of File Explorer's right-click, the desktop's
  Shift+F10; Explorer's More menu and the desktop's modern menu (right-click or the Menu key) carry the same two. The
  modern menu's two items now have access keys too (S, N; patch 1190 underlines them only once keys are used, so the
  pointer's menu looks as before). The `desktop-view` probe runs both through shell32's menu object, with the Menu key and
  with Shift+F10 and their access keys, and through File Explorer's More menu, and fails on any "Create link".
- **Patch 2496: no winemenubuilder per shortcut.** Every saved .lnk started `winemenubuilder.exe` (Wine's host-menu
  mirror) as a new console helper: twelve in `shell81-view-r5-04`. None start now (`shell81-view-r5-05`).
- **Patch 2497: the desktop lists only its own folders' changes.** The 10-36 s "stalls" of `shell81-view-r5-02` to
  `-07` were no freeze: the screen was still (no change, so no frame) and a new shortcut was not listed. The desktop's
  own log of each change notice (`shell81-view-r5-08`, kept in `pw_shell_desktop_view.h`) showed why: shell32's view
  hears changes from every folder and keys them by name, so Pin to Start's link in Start's Programs became a desktop
  icon (19 items, then 20, with no Desktop change) and the real desktop shortcut of the same name made next was taken
  for it (20, then 20). The probe now times each shortcut (listed and gone within 3 s, in the evidence's
  `shortcut_ms`) and checks that Pin to Start adds no desktop icon. The `demand shared view` lines are explorer mapping
  shell32.dll as data for icons (757 maps, 65 ms of pager time in `-05`): cheap, and not the cause.
- **Open finding: no frame while a keyboard-opened desktop menu is up.** With the Menu key's menu open the title
  counted 0 frames in 10 s three times running (`shell81-view-r5-09`), even with the probe's flicker window changing
  pixels every half second; the capture asked for then arrived only after Escape (40 s later), and the desktop's next
  picture also waited. A mouse-opened menu is pictured normally (picture 4). The probe takes no picture of a keyboard
  menu; the cause (explorer's menu loop and the compositor) is not traced yet.
- **Live wallpaper repaint** (no Wine patch). Settings sends the registered "MihawkOS.WallpaperChanged" to the desktop
  view, which reloads `proton.bmp` and repaints (`wine/ps5/pw_shell_wallpaper.h`, `pw_shell_desktop_view.h`). The
  wallpaper probe compares the strip left of the Settings window with the newly chosen picture
  (`shell81-wallpaper-r4-01` 9/9). Broker, loader and frame pacing untouched. The painter first filled the view's own
  client, the work area 48 px shorter than the screen, so the picture sat 24 px higher than Wine's PaintDesktop drew it
  and the scorecard's desktop fell from 98.3% to 9.7% (`shell81-base-03`, `-04`); it now fills the screen.
- **Wording**: the WebUI's 9 English strings that said Windows now say MihawkOS ("PC game" where it named the kind of
  game), with the other 9 languages; shell32's two prompts that named Windows (patch 2495). Paths such as `C:\Windows`
  and technical names stay. The host test now reads `webui/i18n.js` and `src/pw_webui.cpp`.

## Round 3 (2026-11-07): Settings as the prototype's three screenshots

The owner asked for Settings to look exactly like the prototype's Home, Personalization and System > Display. The app
was rewritten (`src/pw_settings.c`): the prototype's title bar, account block, search box, navigation with its icons,
pill and accent bar, cards, combos, theme grid, preview and Display rows, at the prototype's geometry (client
1178 x 798). Icons, the device frame, the theme tiles, the preview window and the controls are the prototype's own
pixels (`tools/build-settings-sprites.py` from `themes/windows11/settings/*.json`, captured by
`tools/measure-prototype-sprites.mjs`); the Mica behind it is a measured row table. "Windows Update" reads "MihawkOS
Update". Every row, tile and control is an item the D-pad and ✕ reach; Background opens the picture picker that sets the
wallpaper; a theme sets its wallpaper and light or dark mode; rows with nothing behind them open a page that says it
comes later. The theme scenes are the shipped `theme-*.bmp` and Aero Meadow (day and night).

**Measured on the PC** (`tools/run-settings-render.py`: the app's `--render` on the pinned Wine with the null driver
against the prototype in Chromium; share of pixels within 1 level / within 8 / mean difference):

| Page | first render of the new layout | now (`shell81-settings-render-01`) |
| --- | --- | --- |
| Home | 82.0% / 94.8% / 6.2 | 90.0% / 96.7% / 3.86 |
| Personalization | 74.7% / 88.5% / 11.0 | 89.3% / 96.7% / 3.07 |
| System > Display | 83.8% / 97.0% / 3.9 | 92.2% / 97.4% / 3.31 |

**On the base** (`shell81-wallpaper-r5-02`, pictures 3 to 5, the window's client cropped at 374,144 and compared the
same way): Home 89.6% / 95.9% / 5.38, Personalization 83.9% / 89.4% / 11.63, Display 92.2% / 97.4% / 3.32. Home's and
Personalization's previews show the console's chosen wallpaper (the probe had just chosen Captured Motion), not the
prototype's meadow; with Glass City (`shell81-wallpaper-r5-01`, a run whose key choice failed, fixed since)
Personalization was 84.1%. The
console's computer name came back empty; Settings shows "MIHAWK-PS5" then.

The old Settings had none of these pages; on the console the scorecard's Settings state scored 15.6% light / 1.7% dark
(`shell81-base-02`).

**What still differs**: text (GDI's Proton UI glyphs and advances against Chromium's Segoe-metric text: most of the
remaining pixels); the preview's picture carries the build's version note, as the real wallpaper does; the preview
window shows the bubbles behind it (the prototype blurs them); the preview's soft shadow; the accent colour of a theme
is not applied (only its wallpaper and mode); the computer name, network state and the Display values are the
console's real ones or fixed text, so they differ from the prototype's sample text.

## Round 4 (2026-11-07): text, shortcut icons, dialogs, accent, window frame

The owner's five follow-ups, proven item by item on the base with focused probes, then one full gate for all five
(the owner's call: "Can we do all items then only 1 full gate to save time ?").

**Text smoothing** (patch 2498). Grey smoothing drew 17-level glyphs, fully hinted, blended through a per-level
range table. It now draws 256-level coverage (WINE_GGO_GRAY16) through the subpixel blender with the same value in
each channel, so there are no colour fringes and it is not ClearType, with FreeType's light hinting (vertical only).
Measured on the PC against the prototype, the text pixels alone within 8 levels
(`tools/run-settings-render.py`, new `ink_*` numbers): Home 65.4 -> 67.3%, Personalization 72.3 -> 75.6%, Display
56.8 -> 59.5%. 256 levels with the old full hinting alone was 64.7 / 71.7 / 56.0, so light hinting carries most of it.
The smoothing gamma was swept 1000-2200; Wine's default (no ramp) and 1600 measured best, so no registry change.
On the base: proton-ui32/64 now check that one glyph has more than 17 levels and no colour
(`shell81-items-a`). Settings' text there moved 80.1 -> 80.5% light and 68.3 -> 68.6% dark (ink within 8). Start and
File Explorer did not move: their text is the prototype's own glyph sprites, not GDI. Fractional advances were not
done (GDI positions glyphs on whole pixels; it is not cheap).

**Desktop shortcut icons** (patch 2499). Why it was hit or miss on the base:
- the six store `.url` shortcuts named their icon (IconFile, every file present) and Wine never read it, because the
  InternetShortcut class's default icon came first;
- the store client's own `.lnk` names no icon location, and Wine showed its default icon instead of the target's;
- a copied shortcut whose paths name a folder that is not here showed the default icon too.
shell32 now uses, in order: the icon a `.lnk` names when that file is there, else its target's own icon (a folder's,
a program's, the generic program icon for one without, a file type's), and a `.url`'s IconFile. A stored path has its
environment variables expanded, is taken relative to the shortcut, and is looked for next to the shortcut when it is
not there. A target that is nowhere keeps the default icon, logged, with no icon invented. Probe
`tests/shortcut-icons-probe.c` (gate cases shortcut-icons32/64) covers a .lnk naming an .ico, a 256 px PNG icon, a
program's icon (an installer link), a relative icon, 64- and 32-bit targets, a folder, a document type, copied .lnk and
.url files, a store .url and a missing target. Each case is checked against the expected system image list entry and
pixel-compared with Wine's default icon. On the base: 11 failures before (`shell81-icons-before`), 31/31 after on 32
and 64 bit (`shell81-items-b`, with its icon sheets `klog/shell81-items-b-shortcut-icons*-sheet.png`).

**Dialogs** (patches 2491, 2493). Prototype dialogs stay light in the dark theme. A window with the
`pw_light_frame` property gets the light caption (win32u) and the theme's MihawkLight caption buttons (uxtheme;
`tools/build-wine-theme.py` writes every light class again as `MihawkLight::` in both variants). The message box and
the task dialog set it. The task dialog also gets a white page, a #f3f3f3 footer and the prototype's 16 px main
instruction. Their text was serif because wine.inf aliased Segoe UI to Times New Roman and rewrote it at every
prefix update; it now names Proton UI (patch 2493; checked by the monospace probe). Scorecard (`shell81-base-06` ->
`shell81-base-09`): message box 53.3 -> 75.9% light, 32.2 -> 39.6% dark; error 73.3 -> 82.2 / 43.3 -> 47.4; info
74.7 -> 82.0 / 42.4 -> 45.1; task dialog 14.1 -> 51.1 / 4.7 -> 39.4. The dark message box stays below the light one
because the reference's dark capture has a darker edge and shadow around the dialog.

**Accent colour** (`src/pw_accent.h`, patch 2492). Applying a theme in Settings stores its accent where Windows keeps
it (HKCU\Software\Microsoft\Windows\DWM AccentColor) and announces it (WM_SETTINGCHANGE "ImmersiveColorSet").
The shell takes it, in the prototype's shade ramp (`js/core.js` applyAccent; host test `tests/test_pw_accent.c`, 24
shades within a level), for: Settings' navigation bar, toggles, slider and rings; Start (the shell style); the
calendar's today; File Explorer's selection plates; and the taskbar's palette and its active pill (the prototype's
sprite recoloured with the accent). A settings change now repaints the taskbar's buttons as well.
The wallpaper probe stores Sunrise's #ca5010, captures Personalization with the taskbar under it, and puts the values
back; the runner counts the fill shade in both places. `shell81-wallpaper-a4`: 48 px of Sunrise's fill shade
(#a9430d) in Settings (its navigation bar) and 40 px in the taskbar (the active pill), none in the picture taken
before with the default accent. On the way there the taskbar trace showed it had taken the accent while the pill
stayed blue: with the prototype's sprites, the taskbar draws its pills from a fixed sprite, not its palette
(`shell81-accent-dbg-01`, `shell81-accent-wait-01`).

**Window frame.** The caption buttons, border, corners and shadow already measured at about 100% (window-chrome
states). What was left was the inactive window's title bar: our compositor drew Mica on every Mica window, but
Windows and the prototype show it only on the active one. A framed window that is not active now gets its solid
inactive caption colour (#f3f3f3, #202020 dark; `src/pw_window_compose.c`, host-tested). Glass without a frame (the
effects probe's popup) keeps its material. windows-active-inactive 79.9 -> 83.7% light, 16.6 -> 20.4% dark. The rest of
that state and window-maximized is the window's content: the prototype's Notepad is the tabbed one, ours Wine's.
No Wine frame patch was taken.

**Open:**
- a grey copy of the No label inside Cancel in the scorecard's message box, which the probe moves just before the
  capture; it sits one button-step from No, so a stale part of an earlier frame, not the dialog's drawing;
- Quick Settings' "on" tiles keep the default accent (they are prototype sprites);
- the task dialog's layout is still Wine's.

Gate: shell81-gate-05 on build 0e14b849d0f8ab06 ran all 140 cases. 135 passed, 2 were skipped for the environment
(dnsapi32/64: the resolver refused TCP) and 3 failed: dump-server64-helper, title-stall64 and broker-wedge64, the last
three, each with kern.ipc.maxpipekva exceeded (the per-boot pipe leak, as in gate-02). Every case this round touches
passed: proton-ui, monospace, shortcut-icons32/64, desktop-view and wallpaper-choice. The owner had the round committed
without a passing gate (e6e44db9); the gate reruns after the console's next restart.

## Boundary finding

`console-setup-2026-11-06-01..07` ran on firmware 10010000: the **Pro** (guard file for key d6b616f17cd7 exists in `.deps/`).
Runs 08 and 09 are firmware 13600007, the base. The handoff reserves the Pro for ProsperoEden. Those Pro runs
(including `shell81-score-01` to `-04`, said in PLAN to be on the Pro) are measurements only; nothing from them is used as a
gate. This session uses the base only.

## Aero identity check

No kept change touches the wallpaper, the Mihawk logo or the Aero palette. The one place the prototype's colours already
differ is the refreshed reference itself: it is captured with Bubble City on the desktop (not the prototype's Aero Meadow),
so desktop and glass comparisons are fair.

## First measurement on the base (2026-11-06; `evidence/shell81-base-01.json`, build fingerprint 6e87e6632eb7e265)

Base PS5 (key 950f10f8671b, firmware 13600007), the kept tree above (Quick Settings grid, patch 2490, probe fix), the
refreshed 172-state reference (Bubble City, light and dark). No gate yet; not a commit candidate.

**27.4% like the prototype, 62.0% mean over the 76 scored states** (68 have no native surface, 12 did not open,
16 are not driven). By group (mean over scored): File Explorer 100.0, Start 87.2, menus 86.4, taskbar 86.1,
desktop 80.1, window chrome 74.7, flyouts 62.7, dialogs 33.0, apps 14.5. Quick Settings 93.2% light / 92.2% dark;
message box 53.3 / 32.2, task dialog 14.1 / 4.9 (patch 2490's layout is in, its caption bar and dark-theme light
palette are not); Settings 15.6 / 1.7 (the app is still `mihawk-appearance.exe`); notifications 33.2 / 32.1.

Owner's 2026-11-06 steer: look similar, not pixel-exact. This table is a progress meter, not a bar to reach.
