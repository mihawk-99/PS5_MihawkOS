<div align="center">

# MihawkOS

<p>
<picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/platform-night.svg"><img src="docs/media/readme/badges/platform-day.svg" height="22" alt="Platform: PlayStation 5"></picture>
<a href="LICENSE"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/license-night.svg"><img src="docs/media/readme/badges/license-day.svg" height="22" alt="License: GPL-3.0-or-later"></picture></a>
<a href="https://github.com/mihawk-99/PS5_MihawkOS/releases"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/status-night.svg"><img src="docs/media/readme/badges/status-day.svg" height="22" alt="Status: alpha, experimental"></picture></a>
<a href="#compatibility-and-known-limits"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/tested-on-night.svg"><img src="docs/media/readme/badges/tested-on-day.svg" height="22" alt="Tested on: PS5 Pro firmware 10.01 and PS5 firmware 13.40 and 13.60"></picture></a>
<a href="#what-runs"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/runs-night.svg"><img src="docs/media/readme/badges/runs-day.svg" height="22" alt="Runs: Windows x64 and x86 programs, x86 through WoW64"></picture></a>
<a href="NOTICE.md"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/built-on-night.svg"><img src="docs/media/readme/badges/built-on-day.svg" height="22" alt="Built on: Wine from GE-Proton11-7"></picture></a>
<a href="#first-launch"><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/badges/languages-night.svg"><img src="docs/media/readme/badges/languages-day.svg" height="22" alt="Languages: 10"></picture></a>
</p>

**Your Windows games on the TV. A desktop of their own around them.**

64-bit and 32-bit Windows programs · Steam, built in · A browser companion for your phone

[Download](https://github.com/mihawk-99/PS5_MihawkOS/releases) · [Get started](#get-started) · [Steam](#steam-built-in) · [What runs](#what-runs) · [Known limits](#compatibility-and-known-limits)

</div>

## MihawkOS is a passion project, not a piracy project.

It exists so you can play the games you own on the console you own.

- **Piracy is not condoned.**
- **No games, saves, keys, firmware or exploits are included, and they never will be.**
- **Use only games and programs you have the right to run**: your own copies, bought or installed by you.
- **Requests for, or links to, games, keys, firmware or exploits are not welcome** in this project's issues or discussions.

---

<p align="center">
  <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/banner-night.svg"><img src="docs/media/readme/banner-day.svg" width="960" alt="The MihawkOS mark over the project's own Frutiger Aero artwork: a sky with clouds and soap bubbles above green hills"></picture>
</p>

<p align="center"><strong>Windows programs on a PlayStation 5, started from a desktop of their own.</strong><br>Pick a game from the launcher, or open the desktop and start it from its shortcut, as on a PC.</p>

MihawkOS is a homebrew title for PS5 consoles that already run homebrew, made by [Mihawk](https://github.com/mihawk-99).
Underneath is a Wine runtime with Proton's graphics stack. On top is **Mihawk OS**, a Windows-style desktop with a
taskbar, Start, File Explorer and a web browser of its own.

**Releases** are on the [Releases](https://github.com/mihawk-99/PS5_MihawkOS/releases) page. Each one says what
changed and which consoles it was tested on.

**New in Alpha 7:**
- **Games run inside MihawkOS.** A 64-bit game from Steam's library or your Games folder now runs in MihawkOS's own
  process instead of a helper, with MihawkOS's larger page-table pool (16,384, against the helpers' 8,192). 32-bit
  games, a second game while one runs, and a game's launcher, installers and crash reporters still run in helpers.
  MihawkOS picks the game by what it is: a program that draws with Direct3D, Vulkan or OpenGL, not a launcher,
  installer or crash reporter. A game built on Chromium (Electron or NW.js) runs as one process. Press TRIANGLE on a
  game to choose, for that game, to run it the old way.
- **One folder to install, and it updates itself.** The Windows runtime ships inside `PPSA99170`: copy the folder, open
  MihawkOS, and it installs the runtime by itself. To update, replace the folder and open MihawkOS: it writes only what
  changed, and `/data/ps5proton` (your prefix, games, Steam and settings) is left alone. A signed update check asks
  GitHub for new releases.
- **A new main menu.** A tile grid on a sky, Files to start any program on C:, Options for each game, and ten
  languages. An **on-screen keyboard** (off by default; Settings, Controls) types into games.
- **Frames reach the screen sooner.** Each finished frame is copied to the TV in under 1 ms instead of about 7.4 ms.
- **Direct3D 12:** feature level 12_1; games see the real GPU's name and model; up to 32,768 reserved graphics memory
  pieces; games that place graphics memory over their own memory (existing heaps) can.
- **Behaves more like Windows:** a game's message boxes show on screen and CROSS answers them; a program can read
  another program's command line and its parent's details; a game missing a DLL ends with that file's name instead of
  leaving MihawkOS stuck; a controller that drops and comes back is found again.
- **Games:** Undertale, Deltarune and other GameMaker games start; Deltarune's sound stays clean across chapters; The
  Witcher 3's DirectX 12 version plays when started from Files or the library; started from the Mihawk OS desktop it
  still closes itself at once (it inspects the program that started it), a known limit.
- **Steam:** big downloads no longer close it (over 10,000 files open at once), and it warns before starting when the
  console is too low on memory.
- **A game that stops drawing for 3 minutes inside MihawkOS is ended**, and MihawkOS returns to its launcher.
- **The guided setup can no longer be skipped.** A new page, **Before you play**, says which games MihawkOS cannot run
  (DRM such as Denuvo, anti-cheat); hold ✕ for 10 seconds to go on.
- **Payloads:** the checklist can be passed by holding TRIANGLE for 5 seconds; NanoDNS is optional; ShadowMountPlus is
  checked by presence only; the payload memory notice shows only at 200 MiB or more.
- **Logs keep their last minutes**, so a crash report shows what happened just before.
- **Looks:** a new Frutiger Aero title icon, and the Bubble City wallpaper by Skeuoss.

Alpha 7 itself was tested on the PS5 Pro (firmware 10.01); earlier features were also shown on a standard PS5.

> [!WARNING]
> **Needs the official payload set, and nothing more.** At every start MihawkOS checks for Kstuff Lite 1.11 Beta or
> later, [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) beta11 or newer, elfldr and websrv
> (NanoDNS, klog, ftpsrv and pldmgr may also run). It stays on its payload check while one is missing or too old, or while any
> other payload runs, and says what to load or close. It sees that kstuff and ShadowMountPlus run, not their versions:
> load Kstuff Lite 1.11 Beta or later and ShadowMountPlus beta11 or newer yourself. MihawkOS does not ship or change any of them.

<a id="what-it-is"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/divider-night.svg"><img src="docs/media/readme/divider-day.svg" width="960" alt=""></picture><br><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-desktop-night.svg"><img src="docs/media/readme/orb-desktop-day.svg" width="34" height="34" alt=""></picture> Two things that ship together

- **A desktop.** Mihawk OS is a Windows-style desktop with a taskbar, Start with search and pins, File Explorer, a
  desktop of shortcuts and MihawkBrowser. A shortcut appears for each game you copy in. Its wallpaper, and the PS5 home-screen
  background, is Bubble City by the artist Skeuoss, used with their permission; its music and its mark are the project's own.
- **A runtime.** Wine with Proton's components: DXVK and VKD3D-Proton for Direct3D, Mesa's Zink for OpenGL. It is pinned
  from GE-Proton and patched so it runs on the console. Each Windows program runs in a process of its own. MihawkOS
  itself owns the screen, the controller, the sound and the GPU that every program draws through.

A full-screen game takes the screen directly (**Game Mode**), and the desktop comes back when it ends.

<a id="at-a-glance"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-this-pc-night.svg"><img src="docs/media/readme/orb-this-pc-day.svg" width="34" height="34" alt=""></picture> At a glance

| Made for your library |
| --- |
| <img src="themes/windows11/icons/original/games.svg" width="36" height="36" alt=""> **Windows games, 64-bit and 32-bit.** Direct3D 11 and 12, OpenGL and video, with the DualSense as an Xbox controller. |
| <img src="themes/windows11/icons/original/app-generic.svg" width="36" height="36" alt=""> **Steam, built in.** Download, install and sign in once with your phone. After that, Big Picture opens from the launcher. |
| <img src="themes/windows11/icons/original/app-browser.svg" width="36" height="36" alt=""> **A browser companion.** Add games, find their art, make home-screen tiles, back up saves and change settings from a phone or computer. |
| <img src="themes/windows11/icons/original/downloads.svg" width="36" height="36" alt=""> **Guided from the first launch.** One folder to copy: a setup in ten languages installs the runtime from it. |

**Explore:** [Get started](#get-started) · [Steam](#steam-built-in) · [WebUI](#webui) · [Add games](#add-your-games) · [Controls](#controls) · [What runs](#what-runs) · [Your files](#your-files) · [Known limits](#compatibility-and-known-limits) · [Build](#build-from-source)

<a id="get-started"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-downloads-night.svg"><img src="docs/media/readme/orb-downloads-day.svg" width="34" height="34" alt=""></picture> Get started

1. **Set up the console's homebrew tools.** Load the official payload set: **Kstuff Lite 1.11 Beta** or later,
   **elfldr**, **websrv** and **ShadowMountPlus beta11** or newer (NanoDNS is optional), and no other payload. You set these up yourself, with tools from their own authors; see
   [Every time you play](#every-time-you-play) for the tested set. This project does not provide, link to or explain
   a jailbreak.
2. **Install the title.** Unzip the release's one download and copy its `PPSA99170` folder to
   `/data/homebrew/PPSA99170/`, over FTP from a phone or computer, or with a file manager on the console. The folder is
   about 0.6 GB: the Windows runtime is inside it. Leave about 4 GB free on the console for the runtime and your first
   Windows drive. ShadowMountPlus needs about 45 seconds to register a new title. A refusal in that window is not a
   crash.
3. **Open MihawkOS from the home screen.** The guided setup walks you through the rest:
   - choose your language;
   - let it install the Windows runtime from its own folder, about a minute;
   - let it build your Windows drive, about a minute;
   - then choose **Install Steam**, which guides you through Steam, or **Go to the game library**, where Mihawk OS
     is a press away.
4. **Add your games.** Scan the WebUI's QR code with your phone and follow **Add a game**, or set up
   [Steam](#steam-built-in) from the launcher.

No games, keys or firmware are included. Use your own copies of the games and programs you run.

### Scan to connect

| Where | What the QR code opens |
| --- | --- |
| **The launcher's library**, under the side panel | The WebUI, at `http://<PS5 address>:6767` |
| **Settings → Web UI** in the launcher | The same, larger |

<a id="first-launch"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-this-pc-night.svg"><img src="docs/media/readme/orb-this-pc-day.svg" width="34" height="34" alt=""></picture> Your first launch

The first time you open MihawkOS, it shows a short guided setup, like a new console's.

| Step | What it does |
| --- | --- |
| **Welcome** | About ten minutes from here to your desktop. The setup cannot be skipped. |
| **Language** | English (US), Français, Español, Deutsch, Italiano, Русский, 简体中文, 日本語, Português (Brasil) and Polski. New Windows drives take this language too, so games default to it. |
| **Installing** | Starts by itself: the runtime is copied from the MihawkOS folder and every file is checked. The size, the speed and the time left. A folder copied without its pieces, or a full disk, is said plainly, with Try again. |
| **Preparing Mihawk OS** | Builds your Windows drive, about a minute, with each stage ticked off as it finishes. |
| **Before you play** | What MihawkOS is, and the games it cannot run: games with DRM such as Denuvo, and games with anti-cheat or kernel-level protection such as Easy Anti-Cheat or BattlEye. Hold ✕ for 10 seconds to continue. |
| **Done** | **Install Steam** (the Steam guide) or **Go to the game library**. Steam stays on the library's Steam row for later. |

Use the D-pad and Cross, or the touchpad as a pointer. Any problem is said in plain words with a way out: a drive the
PS5 cannot read, an incomplete folder, too little space. Open the setup again at any time from **Settings → Guided
setup**.

<a id="steam-built-in"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/divider-night.svg"><img src="docs/media/readme/divider-day.svg" width="960" alt=""></picture><br><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-app-generic-night.svg"><img src="docs/media/readme/orb-app-generic-day.svg" width="34" height="34" alt=""></picture> Steam, built in

Choose **Steam** in the launcher's library. The first time, a guided setup in the same style says what will happen and
keeps saying it until you are signed in:

1. **Download.** Steam's installer comes from Valve's servers, checked before it runs. Nothing of Valve's is in this
   repository or a release.
2. **Install.** About half a minute when MihawkOS is already set up; a first install also builds your Windows drive.
3. **First start.** Steam updates itself, which takes about five minutes. A still screen is normal; the guide shows the
   download and the time.
4. **Sign in once.** Open the Steam app on your phone, tap the shield and scan the code. MihawkOS closes Steam as soon
   as your sign-in is saved.
5. **Big Picture.** From then on, Steam opens straight into Big Picture, made for the controller.

Big Picture stutters less. Steam's built-in browser runs as one process, and its code budget now grows when it needs
to. On a standard PS5 (firmware 13.60) at 120 Hz, the frames that took longer than 20 ms fell from 79 to 38 in the first
minute, from 162 to 66 while scrolling and from 74 to 60 in the menus. Separate browser processes drew it more smoothly
still (7 slow frames while scrolling), but each process takes part of the console's system page tables, and with them
Steam alone left too few for a game to start.

Steam games start in about half the time they used to: Steam's own file check is skipped, and a game's file reads are
much cheaper. In the measured run, a 2D game launched from its library entry reached steady play in 66 seconds,
down from 120.

**Settings → Controller** chooses the controller programs see: **Xbox 360** (the default), **Xbox One** or
**DualSense**. Steam then shows that controller's button glyphs.

The guide's words are in all ten languages. It needs an internet connection and the console's clock set right.
MihawkOS is not affiliated with Valve.

<a id="webui"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/divider-night.svg"><img src="docs/media/readme/divider-day.svg" width="960" alt=""></picture><br><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-app-browser-night.svg"><img src="docs/media/readme/orb-app-browser-day.svg" width="34" height="34" alt=""></picture> Your browser companion

While the launcher is on screen, MihawkOS serves a page on port **6767** for a phone or computer on the same network.
Scan the QR code, or type `http://<PS5 address>:6767`. Every page guides one task in plain words; the technical fields
stay under a game's **Advanced settings**. The flag in the header chooses its language.

| Page | What you can do |
| --- | --- |
| <img src="themes/windows11/icons/original/games.svg" width="24" height="24" alt=""> **My games** | See every game with its picture and whether it is ready to play. Open one to change its name, its program, its picture, and how it plays: Game Mode and controller, for that game alone. |
| <img src="themes/windows11/icons/original/folder-open.svg" width="24" height="24" alt=""> **Add a game** | Three steps: choose the game's folder on your computer, copy it to the console (a stopped copy carries on), and set it up with its program suggested. |
| <img src="themes/windows11/icons/original/pictures.svg" width="24" height="24" alt=""> **Picture and details** | Look the game up in the Steam store by name and keep its art and details, or use your own JPEG or PNG. |
| <img src="themes/windows11/icons/original/desktop.svg" width="24" height="24" alt=""> **Home screen** | Make a tile for the PS5 home screen that opens MihawkOS straight into one game, with its icon drawn from the game's art. The tile appears a few seconds after MihawkOS closes. |
| <img src="themes/windows11/icons/original/documents.svg" width="24" height="24" alt=""> **Saves** | Download a game's save folder as a ZIP, and put one back. |
| <img src="themes/windows11/icons/original/music.svg" width="24" height="24" alt=""> **Settings** | Controller, Game Mode, 120 Hz output, theme and music, mouse buttons and detailed logs, the same as the launcher's. |
| <img src="themes/windows11/icons/original/document.svg" width="24" height="24" alt=""> **Help** | What to do when something goes wrong, and a bug report ZIP with every address masked. |

The page stops when a game or the desktop starts, so it holds no memory while you play. A banner says so, and how to
come back.

> The WebUI is a local HTTP service without a login. It refuses requests from other sites and needs a token for every
> change, but use it on a trusted network and do not forward port **6767** to the internet.

<a id="add-your-games"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-games-night.svg"><img src="docs/media/readme/orb-games-day.svg" width="34" height="34" alt=""></picture> Add your games

**The easiest way: the WebUI.** **Add a game** copies the folder and sets it up for you.

**The quick way: copy the folder.** Put the game's folder in
`/data/ps5proton/prefixes/mihawk/drive_c/Games/<Name>` (that is `C:\Games\<Name>`) over FTP. If the folder holds one
64-bit or 32-bit `.exe` at its top, the next Mihawk OS session puts a shortcut named after the folder on the desktop.
Uninstallers and crash handlers are not counted.

**With an entry.** A game whose program sits in a subfolder, a folder with several programs, or a program that needs
arguments or settings needs a `game.json`. The WebUI writes it for you; by hand, it is
`/data/ps5proton/games/<id>/game.json`:

```json
{
  "name": "My Game",
  "detail": "Second line in the library",
  "executable": "C:\\Games\\MyGame\\MyGame.exe",
  "directory": "C:\\Games\\MyGame",
  "prefix": "mihawk"
}
```

<details>
<summary><strong>Every key in game.json</strong></summary>

| Key | Meaning |
| --- | --- |
| `name`, `detail` | The two lines shown for the entry. Without `name`, the folder name is used. |
| `executable` | Absolute Windows path, `C:\...` or `Z:\...`. The launcher checks the file is there. |
| `arguments` | One string, split at spaces; double quotes keep words together. |
| `directory` | The *Start in* folder, an absolute Windows path. |
| `prefix` | Prefix folder id, up to 64 letters, digits, `_` or `-`. Defaults to `mihawk`. |
| `env` | A list of `"NAME=VALUE"` strings. A game's own choices: `WINE_PS5_GAME_MODE=1` or `=0` (Game Mode) and `WINE_PS5_CONTROLLER=xbox-360`, `xbox-one` or `dualsense` (the controller); they also reach the programs the game starts. `WINE_PS5_TITLE_PROCESS=<program>.exe` runs that one 64-bit program inside MihawkOS's own process, where its memory is not drawn from the console's shared pool. `WINE_PS5_OPENGL=1` marks an OpenGL program. Words starting `PROTON_` or `MIHAWK_` belong to the title and are not applied from the desktop. |
| `desktop` | `false` leaves the program off the desktop. |
| `launcher` | `false` keeps the program out of the launcher's list (the WebUI's "In MihawkOS's game list" unticked). It still opens from the desktop or a home-screen tile. |

</details>

An entry with a problem gets no shortcut. Its reason is in `logs/title-last.log` (`game <id> (<name>): <reason>`), and
the WebUI shows it under **Why?**. Only x86 programs run, 64-bit or 32-bit; a 32-bit one runs through WoW64.

**Start a game without the desktop.** Choose its entry in the launcher. It starts full screen, and returns to the
launcher when it ends.

<a id="controls"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-app-generic-night.svg"><img src="docs/media/readme/orb-app-generic-day.svg" width="34" height="34" alt=""></picture> Controls

### The launcher

The main menu is a grid of tiles on a sky: Mihawk OS first and largest, Steam, your games (ten a page after the first),
and the Web UI's QR code. Under them, a dock: Settings, Files and Payloads. The menu speaks the language chosen in the
guided setup, and so does Settings.

| Button | Does |
| --- | --- |
| D-pad, Cross | Choose a tile, then open it |
| Left stick | Moves the pointer; the tile under it is chosen |
| L1, R1 | The page before or after |
| Triangle | A game's own options: Game Mode, controller, and whether it runs inside MihawkOS |
| Square | Themes for Settings' pages: thirty of them. Gloss / Skeuomorphic is the default. |
| Circle | Back |
| Options, held 1 s | Close the launcher |

- **Settings** (the dock): Controls, Game Mode, Controller, 120 Hz output, Music, Guided setup, Logging and Web UI.
- **Files** (the dock) shows Mihawk OS's C: drive. Cross on a program starts it, as a game in the library starts, without
  the desktop. Files only looks: it changes and deletes nothing.

- **Controls** assigns the left, right and middle mouse clicks to DualSense buttons. The default is left click on the
  touchpad click.
- **Game Mode** is on by default. A full-screen game's frames go straight to the screen, and the desktop unloads while
  it plays. If a game's screen stays black, turn it off for that game: it then shows inside the desktop.
- **120 Hz output** applies to every game, from the next launch.

### The Mihawk OS desktop

- **The touchpad** is a trackpad: move to point, click to left-click, click with two fingers to right-click, hold the
  click and move to drag.
- **A USB keyboard and mouse** work from the start of a session, through a USB-C hub in the tests. One plugged in later
  is found within 3 seconds.
- **Start → End session** returns to the launcher. Use it rather than the PS button: settings are saved every 30
  seconds, so a session ended from outside can lose up to 30 seconds of them.
- **While a game runs**, Options goes to the game.

<a id="what-runs"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/divider-night.svg"><img src="docs/media/readme/divider-day.svg" width="960" alt=""></picture><br><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-games-night.svg"><img src="docs/media/readme/orb-games-day.svg" width="34" height="34" alt=""></picture> What runs

Every line here is something a console run showed, on a **PS5 Pro (firmware 10.01)** or a **standard PS5 (firmware
13.40 and 13.60)**. It is not a compatibility list: each game needs its own run. The proof behind each line is in
[docs/FEATURES.md](docs/FEATURES.md), [docs/PLAN.md](docs/PLAN.md) and `evidence/`.

| Area | What the console has shown |
| --- | --- |
| Windows programs | 64-bit programs, and 32-bit ones natively through WoW64; up to eight at once, with the programs they start |
| Graphics | Direct3D 11 through DXVK, Direct3D 12 through VKD3D-Proton, OpenGL through Mesa's Zink, and video through Media Foundation; Game Mode hands a full-screen game the screen; a large 3D game on Direct3D 12 held about 117 frames a second |
| Steam | Installed, signed in by phone and opened in Big Picture on the standard PS5; its install and sign-in window inside the PS5 Pro's memory limit |
| Input and sound | The DualSense as an Xbox 360 or Xbox One controller or as itself, the touchpad as a pointer, USB keyboard and mouse; sound through the title's mixer |
| Network | Name lookups, HTTP and strict HTTPS from Windows programs |
| Desktop | Taskbar, Start with search and pins, File Explorer, a desktop folder view, MihawkBrowser |
| When things go wrong | PS5 notifications during a session, repeated in the launcher afterwards; a notice when the console's memory is getting full; a memory guard that closes a runaway program before the console stalls |

### What changed since the third alpha

- **Memory.** A game can be set to run inside MihawkOS's own process, which takes it off the console's shared pool. Each
  helper's converted code is held to a budget: 64 MiB to start, raised only when code it let go keeps coming back,
  for games, Steam and the launchers alike. Steam's web helper went from 234-250 MiB to 173-193 MiB, and each
  helper starts about 8 MiB lighter.
- **A memory notice that knows the console.** "The PS5's memory is getting full" now appears at the point where each
  firmware starts to swap, not at one figure for all.
- **Helpers start the same way on every firmware.** They load Wine's modules with MihawkOS's own loader, so
  they no longer depend on the jailbreak's support for fake-signed programs. That support was the cause of the
  "errno 92" stop testers reported on firmwares such as 11.00, 12.40 and 13.x.
- **Wine's own tests, on the console.** Wine's conformance suite runs on the console, each test file compared with the
  same file on a PC: 1,337 of 1,708 test files match ([docs/CONFORMANCE.md](docs/CONFORMANCE.md)).
- **Fixes found that way, for every program:** Windows file attributes (hidden, read-only, system) kept on a disk that
  has none; symbolic links and junctions; programs told about folder changes at once; each 4 KiB page's protection
  honoured; programs that debug the programs they start; a 32-bit video demuxer; the directory reparse tag; block
  ciphers started without an IV; crash dumps of a live process; and guards that keep MihawkOS running when one program
  misbehaves.
- **Fonts.** Chinese text in its right weight, and games that bring their own fonts.
- **OpenGL.** SwapBuffers with no current context, copies between windows, and a context deleted while another thread
  still uses it, all now answered as Windows answers them.
- **Privileged processor instructions.** Programs that use SGDT, SIDT, SLDT, SMSW or STR in their start-up code no
  longer crash on the PS5: MihawkOS answers them the way Linux does on a PC.
- **Vulkan.** A program that asks for the "immediate" or "mailbox" present mode gets its window, presented in step with
  the display, instead of a refusal. A Vulkan window reports its own drawing area, not the whole screen.
- **Start-up.** USB keyboard and mouse support loads first, which fixed a tester's console where neither worked.
- **Looks.** The Frutiger Aero loading screen and the Bubble Earth wallpaper (Bubble City by Skeuoss since Alpha 7).

<a id="your-files"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/divider-night.svg"><img src="docs/media/readme/divider-day.svg" width="960" alt=""></picture><br><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-folder-night.svg"><img src="docs/media/readme/orb-folder-day.svg" width="34" height="34" alt=""></picture> Your files

`C:` in Mihawk OS is `prefixes/mihawk/drive_c`, and `Z:` is the console's root, so `/data/...` is also
`Z:\data\...`. Over FTP, everything lives under `/data`:

```text
/data
├── homebrew/
│   ├── dev.conf                        two lines for the /data access service
│   ├── PPSA99170/                      the MihawkOS title
│   └── PPSA99300…PPSA99399/            home-screen tiles the WebUI made, one game each
└── ps5proton/
    ├── runtime/ge11-7/                 the Wine runtime
    ├── install/                        where the FTP install step reads the runtime from
    ├── prefixes/
    │   └── mihawk/                     Mihawk OS: the desktop and every program
    │       └── drive_c/Games/<Name>/   your games, C:\Games\<Name>
    ├── games/<id>/game.json            the library, one entry per game
    ├── covers/                         art the WebUI saved
    ├── logs/                           every log the title and Wine write
    ├── caches/                         shader caches
    └── *.txt                           your choices: controls, Game Mode, controller, 120 Hz, logging
```

> [!WARNING]
> `prefixes/mihawk` holds every game in `C:\Games`, everything installed in it, Steam, and your saves. Deleting it
> deletes all of them. Back up your saves from the WebUI before you remove anything.

Updates keep your prefix, your games and your settings. The first session of a new prefix is MihawkOS's own: the guided
setup ends it once your Windows drive is built. A game started in a brand-new prefix's first session did not get the
DualSense; in later sessions it did.

<a id="every-time-you-play"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-drive-night.svg"><img src="docs/media/readme/orb-drive-day.svg" width="34" height="34" alt=""></picture> Every time you play

1. **Cold-boot the console.** Never use rest mode: it unloads the payloads, and only a real restart brings them back.
2. **Load Kstuff Lite 1.11 Beta** or later, then **elfldr**, **websrv** and **ShadowMountPlus beta11** or newer
   (NanoDNS is optional). Keep elfldr loaded while you play: MihawkOS starts its `/data` access service and every Windows helper
   through it.
3. **Open MihawkOS.** Its payload check lists each payload. If one is missing it says which to load; press CROSS to
   check again. If one is too old or an extra payload runs, restart the PS5 and load only the set above.

What the tested consoles ran alongside MihawkOS. This repository ships none of them; each comes from its own project.

| Payload | Required | Minimum | Seen on the base PS5, 2026-10-29 | Port | Needed for |
| --- | --- | --- | --- | --- | --- |
| [Kstuff Lite](https://github.com/EchoStretch/kstuff-lite) | Yes | 1.11 Beta | 7.7 MiB | – | Everything |
| [elfldr](https://github.com/ps5-payload-dev/elfldr) | Yes | – | 22.8 MiB | through websrv | Always |
| [websrv](https://github.com/ps5-payload-dev/websrv) | Yes | – | v0.34, 8.2 MiB | 8080 | Always |
| NanoDNS | Optional | – | 4.1 MiB | – | The official set; without it MihawkOS still runs |
| [ShadowMountPlus](https://github.com/drakmor/ShadowMountPlus) | Yes | beta11 (asked, not checked) | 1.7beta10, 25.2 MiB | 10101 | Starting from the home screen, WebUI tiles |
| [ftpsrv](https://github.com/ps5-payload-dev/ftpsrv) | Optional | – | 4.1 MiB | 2121 | Copying the title, the runtime or games over FTP |
| [klogsrv](https://github.com/ps5-payload-dev/klogsrv) | Optional | – | 3.9 MiB | 3232 | The PC tools, and the kernel log in **Settings → Logging** |
| pldmgr | Optional | – | 12.3 MiB | – | Loading the payloads |

Any other payload keeps MihawkOS on its payload check, etaHEN among them: each one holds memory games need. A payload the
console lists only as "payload" cannot be told apart, so the check shows it as unknown and lets you in (on firmware 13.60,
NanoDNS loaded through websrv looks like this). The set above
held 88 MiB on the base PS5 (`evidence/payloads-base-2026-10-29-01.json`).

> [!WARNING]
> **Never load OnionHEN.** The PC setup tool stops if the kernel log mentions it. Restart without it.

MihawkOS starts the `/data` access service (the Lapy daemon) itself. Never start a second copy.

<a id="compatibility-and-known-limits"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-document-night.svg"><img src="docs/media/readme/orb-document-day.svg" width="34" height="34" alt=""></picture> Compatibility and known limits

MihawkOS was tested on a **PS5 Pro on firmware 10.01** and a **standard PS5 on firmware 13.40 and 13.60**. Other models
and firmwares are reported by testers, not proven here.

- **A game that freezes on its loading screen may not be closed by itself yet.** Close MihawkOS from the PS button menu.
  A fix comes in the first update after Alpha 7.
- **The Witcher 3 started from the Mihawk OS desktop closes itself at once.** Start it from the library or Files.
- **Pipe memory runs out after very many program starts.** Each boot, the console's kernel has a fixed budget of pipe
  memory, and every Windows program start uses some that is never given back. After very many starts, programs stop
  starting and MihawkOS says **Restart the PS5**. A restart is the only cure.
- **The PS5 Pro has less memory for Windows programs.** Its memory guard steps in at about 870 MiB, against about
  1,800 MiB on the standard PS5. Steam and the tested games fit; a launcher with a heavy built-in browser can be closed
  by the memory guard during its start.
- **Translations still need a native speaker's review.** All ten languages are complete, written with machine help.
  Mihawk OS's own desktop and the launcher around the setup stay in English.
- **The per-boot setup is yours to do.** The payload set after every cold boot, as above. Rest
  mode needs a full restart.
- **ShadowMountPlus quirks.** Before 1.7beta6, another homebrew could refuse to start after MihawkOS closed
  (`0x80940033`); 1.7beta6 and later keep ShellCore's launch hook in memory, the fix MihawkOS asks for by asking for
  beta11 or newer ([docs/LAUNCH_AFTER_EXIT.md](docs/LAUNCH_AFTER_EXIT.md)). It installs and removes nothing while MihawkOS runs, which
  is why a new home-screen tile appears only after MihawkOS closes.
- **Some games stop at an error of their own at start-up**, the same one they show under Proton on a PC.
- **Not supported:** kernel drivers and anti-cheat. Rumble is not proven. The desktop's focus does not follow the D-pad.
- **To update MihawkOS**, replace the `PPSA99170` folder with the new release's and open it: it updates its runtime by
  itself.

**For a bug report,** turn on **Settings → Logging**, reproduce the problem, open MihawkOS again and download the bug
report from the WebUI's **Help** page. It masks addresses, but check it before you share it, and never post your
console's address.

<details>
<summary><strong>Troubleshooting</strong></summary>

| You see | Do this |
| --- | --- |
| *No access to /data: …* | The line says why: websrv not answering, `dev.conf` missing, or the access service not answering. Check elfldr and websrv are loaded, then reopen. After rest mode, restart the PS5. |
| *The Wine runtime is not installed …* | Run the guided setup again (**Settings → Guided setup**), or re-run the PC setup tool. |
| A notification, *MihawkOS: A program could not start …* with *Restart the PS5* | The boot's pipe memory is used up. Restart the console. |
| A notification, *The PS5's memory is getting full …* | Games may slow down while the console swaps. Close what you do not need. |
| A notification, *The PS5's memory is nearly full …* | The memory guard asked the program using the most to close, or ended it, before the console stalled. |
| A notification, *A program stopped: the GPU did not answer it* | That program's helper waited 15 s for the GPU and was ended. `logs/sessions/0/` has which. |
| A folder has no shortcut | It holds more than one `.exe` at its top. Set its program in the WebUI, or write a `game.json`. |
| The home-screen tile refuses to start right after install | ShadowMountPlus needs about 45 seconds to register a new title. Wait and try again. |
| A game's screen stays black | Turn Game Mode off for that game in the WebUI, or in Settings, and start it again. |
| A game sees no controller | **Settings → Controller**: choose Xbox 360. DualSense shows the pad only to programs made for it. |
| *The last session … did not end normally* | It crashed, froze or lost power. Copy `logs/title-last.log` and `logs/wine-last.log` (and their `-end` files) off before the next session moves them. |
| The console hangs | Wait at least 3 minutes before cutting power. |

Logs are in `/data/ps5proton/logs/`: `title-last.log`, `wine-last.log`, and `sessions/0`, `1`, `2` for the last three
sessions. A long session keeps each log's start and its end: past 4 MiB (title) or 8 MiB (Wine) the log goes on in
`title-end.log` or `wine-end.log`, and `title-end.1.log` or `wine-end.1.log` holds the part just before. The Web UI's bug
report includes them.

</details>

---

<a id="build-from-source"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/divider-night.svg"><img src="docs/media/readme/divider-day.svg" width="960" alt=""></picture><br><picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-drive-night.svg"><img src="docs/media/readme/orb-drive-day.svg" width="34" height="34" alt=""></picture> Build from source

<details>
<summary><strong>Build requirements and commands</strong></summary>

Read [AGENTS.md](AGENTS.md) first: the rules, the commit gate and the sibling repositories the build expects. The
owner builds on CachyOS (Arch Linux) on an x86_64 PC; other distributions are untested. A first build downloads well
over 1 GB and builds Wine for hours.

**Host tools**

- **clang, clang++, lld and LLVM 23.1.3** (`llvm-ar`, `llvm-ranlib`, `llvm-objcopy`, `llvm-config`). The version
  matters: the Lapy daemon the title ships is pinned by SHA-256, and another compiler gives another file, which
  `tools/build-production.sh` refuses. If your system has a numbered `llvm-config-*`, set `LLVM_CONFIG` to the 23.1.3 one.
- **Build tools:** git, curl, wget, unzip, tar, xz, zstd, patch, make, cmake, ninja, meson, pkg-config, autoconf, bison,
  flex, perl, glslang, a C compiler named `cc`, the zlib headers and the FreeType development files.
- **Python 3.12 or later** with Pillow, pefile, fontTools, numpy, mako, MarkupSafe, PyYAML, packaging and cryptography.
- **ImageMagick 7.1.2-32 Q16-HDRI with librsvg**, for the desktop's icons. `tools/fetch-all.sh` unpacks Arch Linux's
  package of that release on Arch-based hosts; elsewhere, put a build of that exact release on `PATH`.
- **Not needed on the system:** the Windows cross compiler and Wine; `tools/fetch-all.sh` puts the pinned ones in
  `.deps/`.

**Sources**, cloned next to each other:

```text
workspace/
├── PS5_MihawkOS/         this repository
├── PS5_Vulkan/           RADV's link recipe and the console tools; holds .env
├── PS5_Mesa/             Mesa, for RADV and Zink
├── PS5_PayloadSDK/       the payload SDK fork and its platform layer
└── PS5-Lapy-JB-Daemon/   the /data access daemon
```

```sh
git clone https://github.com/mihawk-99/PS5_MihawkOS.git
git clone https://github.com/mihawk-99/PS5_Vulkan.git
git clone https://github.com/mihawk-99/PS5_Mesa.git
git clone https://github.com/mihawk-99/PS5_PayloadSDK.git
git clone https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon.git
```

The build reads PS5_Mesa, PS5_PayloadSDK and the Lapy daemon at their pinned revisions with `git archive`, so a plain
clone of each is enough. PS5_Vulkan's working tree must stay at its pin
(`tools/setup-graphics-foundation.sh` names it), because the PC tools import its `tools/`; build its
`runtime/libc.prx` and `build/host/ps5-native-tool` there with its own `tools/setup-native-dependencies.sh` and `make`.
[PS5_VKHomebrewUI](https://github.com/mihawk-99/PS5_VKHomebrewUI), the launcher's UI kit, is read from beside them or
fetched at its pin.

**Build, test, deploy:**

```sh
bash tools/fetch-all.sh                         # once: every pinned input, checked by SHA-256 (hours: host Wine)
bash tools/fetch-all.sh --check                 # changes nothing: each pin's hash and its URL
bash tools/build-production.sh --jobs 8         # the title in dist/PPSA99170/, the runtime in .deps/staged/
bash tools/host-tests.sh                        # host tests
python3 tools/console-setup.py --check          # read only: what the console is missing
python3 tools/console-setup.py                  # deploy the title and runtime, and read every file back
python3 tools/gate.py run --name <unique-run>   # the console battery, before a code commit
python3 tools/publication-check.py all          # no addresses, secrets, game data or game names
```

The console's address goes in `../PS5_Vulkan/.env` as `PS5_HOST`, git-ignored; the tools mask it in what they print.
Title builds are reproducible: the same sources give the same files, and the deploy sends every program whose code
changed, even when its size did not. `PS5_PROTON_CACHE=/path/to/folder` reuses downloads you already have when their
name and SHA-256 match.

The hooks (`git config core.hooksPath tools/git-hooks`) refuse a code commit the console battery did not pass on
exactly that build, and run the privacy guard on every commit.

**Runtime switches for builders**, set in an entry's `env`: `WINE_PS5_CODE_BUDGET_MIB` overrides a helper's code
budget (0 turns it off); `WINE_PS5_PRX_LOADER=0` asks the kernel to load Wine's modules instead of MihawkOS's loader,
for an A/B.

Where to read next: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) (who owns what, how a program starts),
[docs/PLAN.md](docs/PLAN.md) (what is being built and what is proven),
[docs/WINDOWS_PROCESSES.md](docs/WINDOWS_PROCESSES.md), [docs/FLEXIBLE_MEMORY.md](docs/FLEXIBLE_MEMORY.md),
[docs/STEAM.md](docs/STEAM.md), [docs/WEBUI.md](docs/WEBUI.md), [docs/GUIDED_SETUP.md](docs/GUIDED_SETUP.md),
[docs/CONFORMANCE.md](docs/CONFORMANCE.md) and [docs/MIHAWK_OS.md](docs/MIHAWK_OS.md).

</details>

<details>
<summary><strong>Authors and acknowledgements</strong></summary>

MihawkOS stands on other people's work. Every upstream is pinned by revision or hash and keeps its own licence; the
full list, with every pin, is [NOTICE.md](NOTICE.md), and where the platform knowledge came from is
[docs/PROVENANCE.md](docs/PROVENANCE.md).

| Part | Project | Licence |
| --- | --- | --- |
| This title, its runtime integration and desktop | [Mihawk](https://github.com/mihawk-99): PS5_MihawkOS, [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan), [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa), [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK), [PS5_VKHomebrewUI](https://github.com/mihawk-99/PS5_VKHomebrewUI) | GPL-3.0-or-later; the forks keep their upstreams' |
| PS5 Wine patch series and platform knowledge | [prospero-win](https://github.com/mpereiraesaa/prospero-win) by mpereiraesaa | LGPL-2.1-or-later |
| Wine | [ValveSoftware/wine](https://github.com/ValveSoftware/wine) and wine-staging, pinned from GE-Proton11-7 through [mihawk-99/proton-ge-custom](https://github.com/mihawk-99/proton-ge-custom), a fork of [GloriousEggroll/proton-ge-custom](https://github.com/GloriousEggroll/proton-ge-custom) | LGPL-2.1-or-later |
| Unicode data and default fonts | GE-Proton11-7's ICU 68 Windows DLLs and font collection (Liberation, Source Han Sans, Noto, Ume and Wine's own) | Unicode/ICU licence; OFL-1.1; GPL-2.0 with font exception; Ume's licence; Apache-2.0 |
| Direct3D 8 to 11 | [DXVK](https://github.com/doitsujin/dxvk) by Philip Rebohle and contributors | zlib |
| Direct3D 12 | [VKD3D-Proton](https://github.com/HansKristian-Work/vkd3d-proton) | LGPL-2.1-or-later |
| Vulkan driver and OpenGL | Mesa's RADV and Zink, through PS5_Vulkan and PS5_Mesa | MIT |
| Title-side OpenGL | [PS5 OpenGL SDK](https://github.com/darkxex/ps5-opengl) 1.0.2 by BlackBearReloaded (mirror; the original repository is gone) | GPL-3.0-or-later |
| Media playback | [FFmpeg](https://ffmpeg.org/) 8.1.3, built without GPL or non-free parts, and GE-Proton's winedmo rework | LGPL-2.1-or-later |
| TLS | [GnuTLS](https://gnutls.org/) with [Nettle](https://www.lysator.liu.se/~nisse/nettle/) for Windows programs; [Mbed TLS](https://github.com/Mbed-TLS/mbedtls) for the title's own downloads | LGPL-2.1-or-later; LGPL-3.0-or-later or GPL-2.0-or-later; Apache-2.0 |
| Name lookups | [c-ares](https://github.com/c-ares/c-ares) 1.34.8 | MIT |
| MihawkBrowser's engine | [Chromium Embedded Framework](https://github.com/chromiumembedded/cef) 150, unmodified | BSD-3-Clause, with its `CREDITS.html` |
| .NET and HTML for Windows programs | [Wine Mono](https://gitlab.winehq.org/mono/wine-mono) and [Wine Gecko](https://gitlab.winehq.org/wine/wine-gecko) | MIT with component notices; MPL-2.0 |
| Title foundation | [PS5_VulkanTemplate](https://github.com/mihawk-99/PS5_VulkanTemplate), from [SaschaWillems/Vulkan](https://github.com/SaschaWillems/Vulkan) | MIT |
| Launcher themes, components and sounds | BlackBearReloaded's ps5-homebrew-ui, through PS5_VKHomebrewUI; Inter and Montserrat faces | GPL-3.0-or-later; OFL-1.1 and Bitstream Vera |
| Platform layer and PRX tools | PS5_PayloadSDK, from [ps5-payload-dev/sdk](https://github.com/ps5-payload-dev/sdk) by John Törnblom, and [ps5-native-app-boilerplate](https://github.com/BlackBearReloaded/ps5-native-app-boilerplate) by BlackBearReloaded | GPL-3.0-or-later |
| `/data` access | [PS5-Lapy-JB-Daemon](https://github.com/mpereiraesaa/PS5-Lapy-JB-Daemon) by Arksama / Team PHU and contributors | MIT |
| The WebUI and home-screen tiles | Designed after [PS5_RetroArch](https://github.com/mihawk-99/PS5_RetroArch)'s WebUI and forwarders; served by [GNU libmicrohttpd](https://www.gnu.org/software/libmicrohttpd/) 1.0.10 | GPL-3.0-or-later; LGPL-2.1-or-later |
| Updates and runtime install | [Monocypher](https://monocypher.org/) (signature checks) and [Zstandard](https://github.com/facebook/zstd) (decompression) | BSD-2-Clause; BSD-3-Clause |
| Store client backend, JSON and QR codes | [Aurelia](https://github.com/Drackrath/Aurelia) 0.1.40 (pinned, unmodified), [jsmn](https://github.com/zserge/jsmn), [QR Code generator](https://github.com/nayuki/QR-Code-generator) by Nayuki | GPL-3.0; MIT; MIT |
| The setup's flags and CJK faces | [flag-icons](https://github.com/lipis/flag-icons); Noto Sans SC and JP from [google/fonts](https://github.com/google/fonts) | MIT; OFL-1.1 |
| Desktop type and glyphs | Proton UI, derived from [Selawik](https://github.com/microsoft/Selawik); [Fluent System Icons](https://github.com/microsoft/fluentui-system-icons); [Cascadia Mono](https://github.com/microsoft/cascadia-code) | OFL-1.1; MIT; OFL-1.1 |
| Desktop theme | Mihawk Light, derived from Wine Light by Zhiyi Zhang for CodeWeavers and Wine contributors | LGPL-2.1-or-later |
| Desktop wallpaper and home-screen background | *Bubble City* by Skeuoss | Used with the artist's permission; not under an open licence ([NOTICE.md](NOTICE.md)) |

Pictures and details the WebUI saves come from the Steam store at your request and stay on your own console; the
artwork remains its publishers'. This README's banner, dividers and heading orbs (`docs/media/readme/`) are drawn for the project, around the
desktop's own original icons (`themes/windows11/icons/original/`) and the Mihawk mark.

</details>

<a id="license-and-third-party-terms"></a>

## <picture><source media="(prefers-color-scheme: dark)" srcset="docs/media/readme/orb-documents-night.svg"><img src="docs/media/readme/orb-documents-day.svg" width="34" height="34" alt=""></picture> License and third-party terms

**MihawkOS itself** is free software under the **GNU General Public License v3.0 or later** ([LICENSE](LICENSE),
[LICENSING.md](LICENSING.md)). The few files that keep another licence are named there: the files carrying imported
upstream code, the Wine theme, the original icons, the Mihawk mark and the wallpapers (LGPL-2.1-or-later), the template
files that say MIT, and one OFL-1.1 file. Patches to an upstream project keep that project's licence: the Wine patch
series is LGPL-2.1-or-later, like Wine.

Every upstream keeps its own licence and notices, and its licence text ships beside the binaries: the runtime's
`share/licenses/` and the title's `licenses/`. A binary release comes with its corresponding source: this repository
at the same revision, its patch series and the pins that rebuild every input. Windows programs and games are not
covered by any of these licences; running a program on the layer is use, and its own terms apply.

**No games, saves, keys, firmware or exploits are included, and piracy is not condoned.** You are responsible for
having the right to the programs you run, and for whether modifying your console is lawful where you live. The full
notes are in [LEGAL.md](LEGAL.md).

This is an independent homebrew project, not affiliated with, sponsored by or endorsed by Sony Interactive
Entertainment, Microsoft, Valve or any game publisher. PlayStation, PS5 and DualSense are trademarks of Sony Interactive
Entertainment Inc.; Windows is a trademark of Microsoft Corporation; Steam and Proton are trademarks of Valve
Corporation. Other names belong to their owners and are used only to say what the project runs on and works with.

Mihawk OS is not Windows. It contains no proprietary Microsoft code, fonts, icons, wallpapers or sounds, and no Windows
logo: the Start button shows the project's own Mihawk mark. Its look follows Windows 11 from public references, and the
open-source Microsoft assets it uses are credited above. The name "Mihawk" is the author's handle.

<p align="center">made by <a href="https://github.com/mihawk-99">Mihawk</a> · Built on the work of the Wine, Proton and PS5 homebrew communities.</p>
