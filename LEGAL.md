# Legal notes

Plain-language notes about what this project is, what it is not, and who is responsible for what. They are not legal
advice. The licence terms themselves are in [`LICENSE`](LICENSE) and [`LICENSING.md`](LICENSING.md); pins and
attributions are in [`NOTICE.md`](NOTICE.md); where the platform knowledge came from is in
[`docs/PROVENANCE.md`](docs/PROVENANCE.md).

## No affiliation

MihawkOS (this repository, PS5_Proton), the homebrew title, and Mihawk OS, the desktop it runs, are independent hobby
projects. They are not affiliated with, sponsored by or endorsed by Sony Interactive Entertainment, Microsoft, Valve,
Shueisha or Toei Animation, or by any game publisher.

## What this project is

- **A compatibility layer.** A runtime, built on Wine and Proton's components, that lets Windows programs run.
- **A homebrew title.** A PS5 program that hosts that runtime and a Windows-style desktop called Mihawk OS.
- **Source code and build scripts.** The repository holds this project's own code, patches to open-source projects,
  and scripts that fetch pinned open-source inputs and check them by hash.

## What it does not contain

The repository and any build of it contain no:

- game files, saves, keys or disc images;
- console firmware or parts of it, and no dumps of firmware, games or system software;
- exploits, and no instructions for running one;
- Sony SDK files, official Sony headers or libraries, or leaked documents.

It does carry one program that only works on a console that already runs homebrew, built from its public source: the
Lapy daemon (MIT source, linked with the payload SDK's GPL-3.0-or-later runtime), which lets this title read
`/data`. ShadowMountPlus, which mounts the title's folder, is a separate
project that the user installs; MihawkOS does not ship or change it.

The tested setup needs a console that runs homebrew. The user sets that up, with tools from their own authors; the
README only records which ones the tested console ran. The user also brings their own games.

## Your responsibility

You are responsible for whether modifying your console, and running a given game on it this way, is lawful where you
live, and for having the right to the games and programs you use. Use only copies you own. Modifying a console may
break its warranty or terms of service, and the console can hang or need a factory reset (see the README's warnings).

## Trademarks

| Mark | Owner |
| --- | --- |
| PlayStation, PS5, DualSense | Sony Interactive Entertainment Inc. |
| Windows | Microsoft Corporation |
| Steam, Proton | Valve Corporation |

Other names of games, programs and companies belong to their owners. These names are used only to say what the
project runs on and is compatible with, and which desktop style the Mihawk OS theme follows; no logo of these owners
is used.

## Mihawk OS is not Windows

Mihawk OS is a desktop drawn by this project on top of Wine. It is not Microsoft Windows and contains no proprietary
Microsoft code, icons, wallpapers, sounds or fonts, and no Windows logo: the Start button shows the project's own
Mihawk mark (`themes/windows11/start.svg`, original artwork). Its look follows Windows 11 from public references
(sizes and timings Microsoft publishes, colour values from WinUI's open source) through a browser prototype written
here, whose pixels the native shell is measured against.

Some things Microsoft itself published under open-source licences are used, each pinned with its licence in
[`NOTICE.md`](NOTICE.md): Fluent System Icons (MIT), the Selawik font that Proton UI is derived from (OFL-1.1),
Cascadia Mono (OFL-1.1), colour and size values from WinUI (MIT, values only), and the open-source .NET libraries
inside Wine Mono (MIT), which Wine installs into each prefix. The browser prototype also shows Fluent Emoji (MIT).
The desktop's wallpaper, which is also the title's PS5 home-screen background (`ps5/sce_sys/pic0.dds`, `pic1.dds`),
is Bubble City by Skeuoss (`themes/windows11/wallpapers/skeuoss-bubble-city.jpg`), used with the artist's permission
(below). The project's own wallpapers, Bubble Earth, Glass City and Aqua World
(`tools/make-aero-wallpapers.py`) and Aero Meadow (`tools/make-mihawk-wallpaper.py`), are original artwork. No Windows
wallpaper is included (the browser prototype can link a user's own local copy of one, which is never part of the
repository).

The name "Mihawk" is the author's handle. The project uses no One Piece names, characters or artwork beyond it.

## Licences

- **This project's own code** is GPL-3.0-or-later ([`LICENSING.md`](LICENSING.md)), the owner's decision of
  2026-10-15. A few files keep another licence, each named there: the files that carry imported upstream code, the
  Wine theme, the fourteen original icons and the Mihawk mark in `themes/windows11/` (LGPL-2.1-or-later), the
  template files that say MIT, and one OFL-1.1 file.
- **The title as distributed** is GPL-3.0: it links the GPL-3.0-or-later platform layer and launcher UI module.
- **Patches to an upstream project keep that project's licence**: the Wine patch series is LGPL-2.1-or-later, like
  Wine.
- **Every upstream keeps its own licence and notices**: Wine, DXVK, VKD3D-Proton, Mesa, FFmpeg, GnuTLS and the rest are
  listed with their pins in [`NOTICE.md`](NOTICE.md), and their licence texts ship beside the binaries: the runtime's
  `share/licenses/` and the title's `licenses/`.
- **Source ships with any binary.** A binary release comes with its corresponding source: this repository at the same
  revision, its patch series and the pins that rebuild every input.
- **The desktop's wallpaper is not covered** by any of these licences. Bubble City is the work of the artist Skeuoss
  and is used and redistributed with their permission, given to the project's author in a Discord message on
  2026-10-31 ("sure, use everything you want", in answer to a request to feature their artwork with credit). The
  permission covers MihawkOS; it does not license the picture to anyone else. Its record is in
  [`NOTICE.md`](NOTICE.md). The picture may be stock art that Skeuoss's archive re-hosts rather than their own; the
  owner weighed that and decided on 2026-10-31 to publish it in public releases: "It is fine publishing it".
- **Windows programs and games are not covered** by any of these licences. Running a program on the layer is use;
  its own licence and terms apply.
