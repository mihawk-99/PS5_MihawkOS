# The guided setup: MihawkOS's first launch

The friendly front end of [SELF_INSTALL.md](SELF_INSTALL.md) phases 5 to 7: a step-by-step tour like a new console's,
inside the title's own launcher. It runs before any Wine runtime exists, so it is the launcher's code (native Vulkan and
the UI kit), not the Mihawk OS shell's. Status lives in [PLAN.md](PLAN.md); this file is the design.

| Part | File |
| --- | --- |
| When it shows, where each step goes, the languages and their text, the record | `src/pw_setup.{h,c}`, host test `tests/test_pw_setup.c` |
| The screens, the sounds, the pointer, the install engine's interface, the sky's pipeline and timer | `examples/ps5proton/setup_tour.hpp` |
| The sky | `shaders/glsl/ps5proton/aero.{vert,frag}` |
| The flags (lipis/flag-icons, MIT, pinned) | `ps5/assets/flags/`, `gen_setup` in `ps5/tools/generate_assets.py` |
| Pictures and their checks | `tools/test-setup-ui.py` |

## When it shows

A new user installs MihawkOS by copying its folder to `/data/homebrew` and opening the tile (the owner's decision,
2026-10-18). The title makes `dev.conf` and its folders itself, so the tour is the first thing a user sees.

`pw_setup_start` decides, from what the console has:

- **Never without `/data`:** nothing could be saved, and the launcher's own complaint says why.
- **Back from its first session** (the `setup-preparing` marker): Done, or Preparing again with "did not finish".
- **On the first launch:** `/data/ps5proton/setup.txt` has no `done=1`.
- **At Installing** when the runtime is missing or another build's (an updated title folder: the copy alone, then the
  library), **at Preparing** when the `mihawk` prefix is missing.
- **When asked:** Settings, Guided setup.

The first setup cannot be skipped (the owner, 2026-10-31): there is no Skip button and no △ shortcut. `done=1` is
written only when Done shows, after Before you play, so a console turned off on the way comes back to the tour. After
that, the tour comes back only when the runtime or the prefix is missing, or when Settings asks. A re-run from Settings
cannot be left either ("It should be impossible to leave there too"): Settings clears `done` as it starts, so a title
closed on the way comes back to the tour, and with everything installed it is Welcome, language, Continue, Before you
play, Done. A step that fails keeps its own Try again and Back, and nothing was added to move past it (the owner: no
safeguard that offers a way forward). The counter has six steps: Before you play is 5 of 6, Done 6 of 6.

**A first run on a set-up console** is played by the marker `/data/ps5proton/setup-first-run`. The tour shows the
runtime and the prefix as missing and installs with the fake engine, so nothing is written over the runtime. Preparing
starts a real session, which ends at once because the prefix is ready. The marker is removed at the tour's end. The
owner's prefix, runtime and saves are never touched.

## The steps

| Step | What it shows | Ways out |
| --- | --- | --- |
| Welcome | "Welcome to MihawkOS", ten minutes, the First Light theme (low, this step only) | Get started |
| Language | Flag tiles with each language's own name; only languages with complete text | ✕ chooses and goes on; ○ back |
| Installing | Starts by itself after the language: the runtime from the title folder's `runtime-pieces/`, every file checked. Percent, bar, "612 of 1,190 MB · 118 MB/s"; the bubbles calm down | Stop; on a problem its sentence and Try again, nothing else |
| Preparing Mihawk OS | What the first start does and how long; the controller note | Start |
| Before you play | What MihawkOS is (a compatibility layer, an alpha) and, in a warning box, the games it cannot run: DRM such as Denuvo, anti-cheat or kernel-level protection such as Easy Anti-Cheat or BattlEye; they won't start or they close. Step 8 (`PW_SETUP_LIMITS`), after FTP, so the other numbers stay | ✕ held 10 s (`PW_SETUP_HOLD_SECONDS`): a ring around the ✕ fills, letting go empties it, and a hold counts only if it starts on the page. Nothing else goes on |
| Done | "You're all set" and two tiles: Install Steam (focused first) and Go to the game library | Install Steam opens the Steam guide at its intro ("Not now" there lands in the library); the library tile closes the tour into the library, where Mihawk OS is one press away |

**No How to install, USB or FTP step** since 2026-10-31 (the owner: "package everything in a single PPSA title file,
and have the setup place the runtime by itself"). Their numbers, 2, 3 and 7, are not reused, so every other step keeps
its number in the logs; `tools/run-launcher.py --setup` now expects 0, 1, 4 and 5.

**Controls:**
- Controller first: the D-pad moves to the nearest target in that direction, ✕ chooses, ○ goes back.
- The touchpad moves a pointer that hovers and focuses; a touchpad click chooses.
- The step's targets are one list, so the pad and the pointer cannot disagree.

**The first session.** Start writes `setup-preparing` and starts Mihawk OS. The title ends that session itself once the
prefix is made (`.update-timestamp` ends in ` wow64`). So nobody can open a game in it: in a new prefix's first session,
games never get the DualSense (README "The Mihawk OS prefix"). The launcher then comes back at Before you play.

**The first session's cover** (`src/pw_setup.h`). While wineboot makes the prefix, Explorer draws nothing, so the
session showed a black screen with the pointer for its minute (first-install-unattended-2026-10-19-04: 75 s, "frames
shown 0"). Now the screen keeps the setup's look:
- **The launcher:** Start's frame is the cover: the sky, the card, "Building your Windows drive", "about a minute", and
  the three stages' words (Windows's settings, .NET support, finishing up) in the chosen language, with no button,
  hints or pointer. That frame is saved as `/data/ps5proton/setup-cover.ppm` (1920x1080), then the title restarts.
- **The title:** in the session with the `setup-preparing` marker, it skips the loading screen and loads that picture.
  Ten times a second it draws each stage's mark over it (`pw_setup_cover_draw`: a green check when done, a turning
  ring when current, a grey ring to come) and hands the frame to the GPU broker (graphics patch 0030). The broker shows
  it instead of the desktop, without the pointer, until the session ends; then the picture is removed.
- **The stages come from real signals:** each Windows process the session starts (`pw_setup_build_next`: a command
  line naming mono starts .NET support, the next one that does not starts finishing up), and the prefix's stamp.
- **Without the picture** (not saved, or not 1920x1080), the session shows the desktop as before.
- **The frame counts stay the program's:** while the cover is up, the broker's flip count stays where it was, so the
  hang breaker and "frames shown" see what they saw before.

## Typed problems and what the tour says

The tour talks to the install engine through `SetupEngine` (`find`, `start`, `cancel`, `state`). Every problem it
reports is typed, so the tour says it in the chosen language with a way out:

| Problem | Says (English) | Button |
| --- | --- | --- |
| `no_pieces` | This copy of MihawkOS has no Windows runtime in its folder. Copy the whole PPSA99170 folder of the release to /data/homebrew again, then try again. | Try again |
| `partial` | The PPSA99170 folder has {have} of {pieces} runtime pieces. Copy the whole folder again, then try again. | Try again |
| `no_space` | Your PS5 needs {needed} free and has {available}... | Try again |
| `read_failed` | A runtime piece in the PPSA99170 folder could not be read... What is already installed is kept. | Try again |
| `damaged` | A runtime piece in the PPSA99170 folder is damaged or from another version... | Try again |
| `storage_full`, `write_failed` | Your PS5's storage is full / refused a write... | Try again |
| `cancelled` | The install stopped. What is already installed is kept. | Try again |
| `no_manifest` | This copy of MihawkOS cannot install a Windows runtime. Reinstall MihawkOS. | Try again |

**The engine** is the installer's (`ps5/src/proton_install.h`, SELF_INSTALL phase 5, on main since a2b5617).
`InstallSetupEngine` reads the pieces from the title folder's `runtime-pieces/` alone (`proton_install_find("title")`,
which never falls back to a download): `tools/package-install.py` puts them there, so a release is one folder to copy.
Game helpers cannot read the title folder, so the copy into `/data/ps5proton` is needed. The engine checks every piece's
and every file's SHA-256 against the title's `runtime-pack.txt` as it streams, and a file already in place with the
right size and SHA-256 is not written again, so an update copies only what changed. A problem keeps the step with its
sentence and Try again, which looks again and starts again; nothing moves past it (the owner: no safeguard that offers
a way forward). The size it shows is the engine's `bytes`, the pinned manifest's size. A download's problems cannot
come from the folder: they read as `read_failed`. `FakeSetupEngine` plays the install, writing
nothing, in tests and in the simulated first run.

## Text and languages

**The keys.** Each key is an enum in `src/pw_setup.h`; each language is a designated-initializer table in
`src/pw_setup.c`.
- Placeholders are `{1}`..`{9}`.
- Sizes go through `pw_setup_gigabytes`, which writes "1.19 GB" or "1,19 Go".
- `tests/test_pw_setup.c` refuses a language unless it has every key, the same placeholders as English, and only
  characters the launcher's fonts carry.

**The languages,** in the owner's order (2026-10-18; Polish added 2026-10-26):
- English (US), French, Spanish, German, Italian, Russian, Chinese (Simplified), Japanese, Portuguese (Brasil, the
  flag set has no Portugal flag) and Polish. Ten tiles, two rows of five.
- All ten have complete text, written by agents, the Steam guide's included (docs/STEAM.md, "The guide"). Each needs a
  native speaker's review before a release.
- Mihawk OS's own shell stays English. The launcher around the tour stays English too, so the text names the
  launcher's English "Settings".

**The fonts**, measured from the `.huifont` glyph tables:

| Script | The kit's bakes (113 glyphs, ASCII) | What the setup draws with now |
| --- | --- | --- |
| French, Spanish, German, Italian, Portuguese, Polish | accents missing (é, ñ, ß, ç...) | the launcher's faces baked with the kit's `european` set (403 glyphs): all there |
| Russian (Cyrillic) | missing | the same faces: there |
| Chinese, Japanese | missing; Inter and Montserrat have no Han or kana | Noto Sans SC / JP Medium, baked by `tools/bake-huifont.cpp` with only the characters their text uses (318 and 291 glyphs, about 1.3 MB each) |
| Vietnamese, Korean (not planned now) | missing | Vietnamese needs U+01A0-01B0 and U+1EA0-1EF9 added to a bake (Inter has them); Korean needs a Hangul face |

How the faces are made:
- `ps5/ui/setup-kit.sh` re-bakes Inter Regular, Inter SemiBold and Montserrat Medium with the pinned kit's own baker
  (`tools/font-baker`, set `european`, 2048-wide atlas). It does this at export, so the whole launcher draws accents,
  game names included.
- A seventh face and more fit beside the kit's six font slots: the kit draws them as ordinary textures.
- `lines()` in the tour breaks Chinese and Japanese between characters, and keeps closing punctuation on its line.
  The kit's own wrap breaks at spaces only.

**The chosen language and Windows.** The chosen language also sets a new `mihawk` prefix's Windows locale, so games
default to it. The design, still to be built and proven on the console:

1. **At prefix creation:** `native/proton_wine.cpp:1059-1087` writes `prefixes/mihawk/.pw-language` from
   `setup.txt`.
2. **Every session:** the title sets `WINE_PS5_LOCALE=<locale>` from that pin. The variable joins the helpers' inherit
   list (`wine/ps5/pw_wine_inherit.h`).
3. **In Wine:** a PS5 patch in ntdll's `init_locale` takes it as `LC_CTYPE` and `LC_MESSAGES`, except in Explorer's
   helper (`MIHAWK_SESSION` set). The shell then stays English while games get the language.
4. **Keyboard:** Wine derives the keyboard layout id from the user locale (`win32u` `get_locale_kbd_layout`), so
   `GetKeyboardLayout` reports French. Typed characters still follow the PS5 driver's US key map; an AZERTY map is
   separate work.

## Look and sound

- **Theme:** the Gloss theme (the launcher's default).
- **The sky:** Aero Meadow (`tools/make-mihawk-wallpaper.py`, the desktop's wallpaper until 2026-10-25), alive and drawn lighter for a page with
  text on it. One fragment shader draws it under the kit's layers, every frame from the clock, at the launcher's rate.
  - the sun's glow and rays, two drifting ribbons of light, the horizon haze;
  - the far and near hills, with the wallpaper's shapes and greens;
  - 24 bubbles in three depths, rising at 14-32 px/s, with the desktop's pink-to-cyan rims, the grass in their glass,
    and a window highlight. The far ones are softer and slower; during the install they fade a little.
  - Dark ink reads on the sky (7:1 at the top) and on the hint row's grass (7:1).
- **The sky's GPU cost:** two timestamps per frame, averaged over 240 frames, logged as `setup sky: <ms> ms GPU a frame`.
- **Sounds:** the kit's glass set (ProsperoEden's sounds, GPL-3.0-or-later) for focus, select, back, error, saved,
  notify and welcome.
- **Music:** MihawkOS's own pieces, Bubble Garden, Glass Meadow and Sky Lounge, made by `tools/make-mihawk-music.py`
  (seeded synthesis; Frutiger Aero pads, bells, marimba, celesta and bubbles; seamless loops at -18 LUFS; OGG from
  pinned libvorbis). The owner chose all three on 2026-10-18; they replaced the kit's First Light.
  - `examples/ps5proton/launcher_music.hpp` plays one piece at a time, looping, under the tour and the launcher's
    menus, and crossfades over 1.6 s on a switch. It feeds the kit's MusicTrack into the mixer's two music decks.
  - R1 in the library, or Settings > Music, goes to the next piece or Off. The name shows for a moment, top right.
    The choice is kept in `hui/music.txt`, beside the theme's. A first run starts with Bubble Garden.
  - The sound stops with the kit's audio when the launcher ends for a game or a session.
  - `tests/test_mihawk_music.py` renders the pieces again and compares every byte with what the title ships.

## Proof

**Host:**
- `tools/host-tests.sh`: 128 passed, `test_pw_setup` and `test_pw_wine_inherit` among them.
- `tools/test-setup-ui.py`: every step and error state, in all ten languages, and the gate's walk; Try again after a
  folder without pieces starts the install.
- `tools/test-launcher-ui.py --render`: the library, Settings and themes, unchanged.

**Console**, build 07421f38 (main 22e156f plus this), 2026-10-18. The klog had no maxpipekva, TTY(STDIN) or errno=5
line before, between or after the runs.
- [setup-tour-2026-10-18-02](../evidence/setup-tour-2026-10-18-02.json), PASS:
  - the first-run marker set, then Welcome, Japanese, How to install, USB, Check drive, Install (the fake engine) and
    Preparing: steps 0 to 5 (since 2026-10-31 the walk is Welcome, Japanese, Installing by itself, Preparing: 0, 1, 4
    and 5);
  - 1500 frames at 59.9 fps on the launcher's 3840x2160 swapchain, with no faults or driver errors;
  - the sky's GPU time: 0.51 ms a frame. [Run 01](../evidence/setup-tour-2026-10-18-01.json) measured 1.75 ms,
    before the bubbles' fixed numbers stopped being hashed per pixel.
- [locale64-2026-10-18-01](../evidence/locale64-2026-10-18-01.json), PASS: with `WINE_PS5_LOCALE=fr_FR.UTF-8`, a
  64-bit program and the child it starts both report:
  - user and system locale fr-FR;
  - UI language 040c;
  - keyboard layout 040c040c.

**Not yet shown:**
- that Explorer stays English while games take the language;
- a walk with the owner at the controller, the touchpad pointer included;
- the real engine installing from a stick;
- Preparing's real first session ending itself.

**The launcher's frame rate:** it presents at 60 Hz, so the tour animates at 60 frames a second. At 0.51 ms a frame,
the sky would also fit at 120 Hz.
