# Does Proton's per-game knowledge reach the games MihawkOS runs? (2026-10-31)

Host-only study: nothing was run on a console, nothing is committed. Sources read: the Wine tree this project builds
(`.deps/wine-proton`, pin `1c78596625336a73dbfc035dbbcf0dbdd95b06eb`, Wine 11.0 with the PS5 series on top), the
fork's `proton` script (`../proton-ge-custom/proton`, GE-Proton11-7) and `umu-protonfixes` at the commit the fork pins
(`27f5c708ec4d4150fbff97acb30a5b21ad893994`, read from GitHub into a scratch folder, never executed).

## Verdict

1. **Wine's built-in fixes did not fire for any game started by the title.** They look the game up by the environment
   name `SteamGameId` (65 lines in 52 files; `SteamAppId` adds 10 more lines), and no launch path of the title set
   it. Only Steam itself, for the games it starts, was expected to (not measured; §1.3).
2. **The proton script's per-game lists are not used at all**, and most of them would not matter here (§2).
3. **umu-protonfixes is a usable reference but only about a third of it is data** (§3). About half of its fix files
   install Windows redistributables with winetricks, which is not a data problem.
4. Of the seven games the project's records name (a 2D roguelike, a 2D platformer, a 2D metroidvania, a 3D horror
   remake, a grand-strategy game, a JRPG remaster and an action roguelite; their app ids are in the records), **none
   has a hack in Wine's dlls, a fix file in protonfixes, or an entry in the proton script's lists except the 2D
   roguelike** (a Steam Input
   VID/PID spoof for the pad, which MihawkOS's own pad path does not use). So the gap is real but it has not cost those
   games anything; it will cost the next ones.
5. The environment change (§1.4) is small, host-tested and uncommitted. It needs one console look before it is
   committed (§1.5), because setting the ids also changes what the Steam API sees.

## 1. The `SteamGameId` gap

### 1.1 What reads it

| Where | Lines | Notes |
| --- | --- | --- |
| `dlls/win32u`, `opengl32`, `ntdll`, `kernelbase`, `user32` | 25 | the core: window/DC/input quirks, `hacks_init` (write-copy, fsync switches, address-space limit), command-line appends, OpenGL |
| `dlls/winegstreamer`, `mfreadwrite`, `mfsrcsnk`, `mfmediaengine`, `quartz`, `qasf`, `winedmo`, `windows.media.speech` | 23 | media; this project replaced winegstreamer with winedmo (patches 2001-2004), so some of these may never run |
| `dlls/winex11.drv` | 7 | dead on the PS5 (no X11) |
| the rest (`d3d8`, `ddraw`, `gdiplus`, `mscoree`, `amd_ags_x64`, `mshtml`, `ws2_32`, `rsaenh`, `sechost`, ...) | 20 | per-game DLL fixes |

Several have a game-independent switch beside the id (`WINE_HEAP_DELAY_FREE`, `WINE_SIMULATE_WRITECOPY`,
`WINE_FSYNC_YIELD_TO_WAITERS`, `PROTON_LIMIT_ADDRESS_SPACE`, `WINE_SHRINK_ENV`, ...): setting the switch reaches the
same code without the id. That is the data route of §2.

The Unix-side readers (`ntdll/unix/loader.c:2377 hacks_init`, `ntdll/unix/virtual.c`, `opengl32/unix_wgl.c`) read the
process's **Unix** environment; the PE-side readers read the **Windows** one. In stock Wine a child's Unix environment
is its parent's (Proton adds `sync_env_var_to_unix`, `kernelbase/process.c:653`, for two Ubisoft variables only), so an
id has to reach both sides.

### 1.2 Which launch gets which id today

| Launch | Code path | `SteamGameId` | `SteamAppId` |
| --- | --- | --- | --- |
| The title's game from the launcher (library entry, profile) | `read_game` (`ps5/src/proton_launch.cpp:563`, env from `game.json`) → `setenv` (`native/proton_wine.cpp:2044`) and the initial application block (`:2307`, ntdll patches 0974/0998 put it in the Windows environment) | **no**, unless the entry's `env` lists it by hand | **no**, same |
| A game started from the Mihawk desktop (helper) | `title_application_lookup` (`native/proton_wine.cpp:871`) → `pw_app_environment_block` → bootstrap (`wine/ps5/pw_wine_process.c:798`) → loader Unix env + Windows env | **no**, same | **no** |
| A program a game starts (launcher → game, crash handler, ...) | the helper inherits only the names in `pw_wine_inherited` (`wine/ps5/pw_wine_inherit.h`); neither Steam name was listed | **dropped on the Unix side** even when the parent had it | same |
| A game Steam starts inside Wine | Steam builds the child's Windows environment itself; the Unix side is the helper's: the allow-list above, plus the entry block only if the exe is in the catalog | Windows side: **expected from Steam, not measured** (no record in `evidence/` or `klog/` has either name). Unix side: **no** | same |
| SteamCMD route (`tools/steam-runtime.c`) | `CreateProcess` with its own environment block | **no** | **yes**, Windows side only |
| `tools/run-console.py --program` (tests) | `native/proton_wine.cpp:1858` replaces the exe, keeps the profile's env | **no** | **no** |

`tools/register-aurelia-games.py` writes library entries (`name`, `executable`, `directory`, `prefix`, no `env`) from
`appmanifest_*.acf`; it knows the app id and throws it away.

### 1.3 What is not measured

- That the Steam client in Wine sets both names for its games, and what its children's Unix environment holds. A
  probe is queued (§6, step 2).
- What setting the ids does to Steamworks: with `SteamAppId` set, `SteamAPI_RestartAppIfNecessary` answers "launched
  by Steam" and `SteamAPI_Init` looks for the Steam client; without it a game whose DRM asks to restart through
  Steam may try to start the client. The change should make that better, not worse, but it is untested here.

### 1.4 The change (uncommitted, in this worktree)

The id comes from what the installation already says; nothing is written by hand and no game is named in code.

- `src/pw_steam_appid.{h,c}` (portable, no allocation): `pw_steam_appid_of(host_exe)` finds
  `steamapps/common/<folder>/` in the program's path, reads the library's `appmanifest_*.acf` files and takes the id of
  the one whose `installdir` names that folder (lowest id if two do); failing that, the `steam_appid.txt` beside the
  program (the file Steamworks itself reads). `pw_steam_id_words()` turns an id into the two env words, **an id the
  entry's own `env` sets wins and fills the other name**, a blanked or non-numeric (64-bit game id) value is respected.
- `ProtonGame::steam_words` + `proton_add_steam_ids()` (`ps5/src/proton_launch.{h,cpp}`): kept apart from `env` so
  the WebUI never writes them back into `game.json`. Called at a launch (after the `--program` override, so the ids
  are the running program's) and when the desktop's application table is built, never from the catalog listing
  (it reads a few small files).
- `native/proton_wine.cpp`: the words are given beside `game.env` in the three places that use it (title `setenv`,
  initial application block, desktop application table), with a log line `Wine: Steam id from the installation: ...`.
- `wine/ps5/pw_wine_inherit.h`: `SteamGameId=` and `SteamAppId=` join the names a helper inherits.
- `tools/steam-runtime.c`: sets `SteamGameId` beside `SteamAppId` (not compiled here: no MinGW on this host).
- `ps5/CMakeLists.txt`, `tools/host-tests.sh`: the new source and test are listed.

Host tests: `tests/test_pw_steam_appid.c` (id rule; manifest parsing incl. nested `installdir`, escapes, cut text;
library lookup at depth, case-insensitive folder; no manifest / outside a library; two manifests; `steam_appid.txt` in
any case, bad content, manifest wins; the word rules incl. explicit ids and a blanked name) and an addition to
`tests/test_pw_wine_inherit.c`. `bash tools/host-tests.sh steam_appid wine_inherit app_environment path_nocase`:
4 passed. The C++ (`proton_launch.cpp`) was syntax-checked against stubbed console headers; `native/proton_wine.cpp`
was reviewed by diff only (it needs the console toolchain).

Rejected: reading the catalog id as the app id (`docs/STEAM.md` calls entry `250900` the model, but nothing makes a
numeric folder name an app id: "profile 10" exists); a `game.json` `appid` field (the existing `env` words already
express an explicit id and win).

Not done, on purpose: the case "Steam starts a game that is not in the catalog" on the **Unix** side. It needs a Wine
patch (Proton's own trick: copy the two names from the child's Windows environment into the spawner's Unix
environment in `CreateProcessInternalW`, with the allow-list above carrying them). §6, step 3.

### 1.5 Risk and the console look it needs

The ids make the hacks fire for the games they name, and they were written and checked on Linux. Not one of the
seven games above is named, so today's test games are unaffected by the hacks; the Steamworks side effect (§1.3) is
what could differ. One run each of the 2D platformer (manifest in the owner's library) and the 2D roguelike, reading the new log line and a
launch with `--env SteamGameId=292120` (a visible `HACK: fsync: yield to waiters` from `hacks_init`) answers it.

## 2. The proton script's per-game settings

The script has 35 `if appid in [...]` lists plus single-id tests. "Matters on PS5?" asks whether the switch acts in
code that runs here (Wine's own PE/Unix code and DXVK/vkd3d-proton over RADV: yes) or only in a Linux-only piece (X11,
Wayland, gamescope, PulseAudio, NVIDIA/Intel/Steam Deck GPU strings, winebus hidraw, FEX, Steam Input: no). Verified in
the pinned Wine where a line number is given; the others by reading the script.

| Proton option (script line) | Ids | Setting | Matters? | Why |
| --- | --- | --- | --- | --- |
| nomfdxgiman (1576) | 5 | `WINE_DO_NOT_CREATE_DXGI_DEVICE_MANAGER=1` | yes | read in `mfplat/main.c:9290`, Wine-internal |
| noopwr (1590) | 23 | `WINE_DISABLE_VULKAN_OPWR=1` | **no** | read only in `winex11.drv/init.c:556` |
| noforcelgadd (1622) | 3 | `WINE_LARGE_ADDRESS_AWARE=0` | yes, with a global default to check | Proton sets `=1` for every game (forcelgadd default); read in `kernel32/heap.c:53`, `ntdll/unix/virtual.c:5728`; this project never sets it |
| hidevggpu (1629), hideintelgpu (1635) | 2, 1 | `WINE_HIDE_VANGOGH_GPU`, `WINE_HIDE_INTEL_GPU` | no | other vendors' GPUs |
| heapdelayfree (1640) | 8 | `WINE_HEAP_DELAY_FREE=1` | yes | `ntdll/loader.c:4662`, tolerates use-after-free |
| heapzeromemory (1652) | 3 | `WINE_HEAP_ZERO_MEMORY=1` | yes | `ntdll/loader.c:4670` |
| heaptopdown (1659) | 2 | `WINE_HEAP_TOP_DOWN=1` | yes | `ntdll/loader.c:4675` |
| nofsync+noesync (1665) | 5 | `WINEESYNC/WINEFSYNC=0` | no | already `0` for every game (`native/proton_wine.cpp:2034`); sync is the in-process device |
| northstar (1675) | 1 | `wsock32=n,b` when the command line has `-northstar` | yes | a DLL override |
| disablenvapi (1682, 1695) | 8, 14 | dxvk-nvapi off | no | no NVIDIA on the PS5 |
| hidenvgpu (1720), forcenvapi (1726) | 2, 2 | NVIDIA hiding / forcing | no | NVIDIA only |
| hideapu (1732) | 1 | `WINE_HIDE_APU=1` | yes | `amd_ags_x64/amd_ags_x64_main.c:736`; the PS5's GPU is an AMD APU (unmeasured) |
| fnad3d11 (1737) | 7 | `FNA3D_FORCE_DRIVER=D3D11` | **yes** | FNA games on DXVK over RADV, the D3D path this project runs (not measured for an FNA game) |
| noxalia (1754) | 5 | `PROTON_USE_XALIA=0` | no | Xalia is not shipped |
| nohardwarescheduling (1763) | 2 | `WINE_DISABLE_HARDWARE_SCHEDULING=1` | yes | `win32u/d3dkmt.c:744` |
| store hints for three PC stores (1771) | by store | `WINE_SIMULATE_WRITECOPY=1` | **yes** | `ntdll/unix/loader.c:2461`; the same loader already matches one store client by name; no store hint reaches the title |
| `PROTON_SONY_DUALSENSE_AS_DUALSHOCK4` (1884) | 17 | winebus identity | no | MihawkOS's pad path is its own XInput feed (patch 2400, `WINE_PS5_CONTROLLER`) |
| `PROTON_SONY_DUALSHOCK4_V2_AS_V1` (1918), `PROTON_ENABLE_MHWILDS_USB_AUDIO` (1932), `PROTON_DEATH_STRANDING_*` (1940, 1948) | 3, 1, 2, 1 | winebus / USB audio / haptics | no | same |
| `WINE_DISABLE_EXE_ASLR` (1926) | 1 | | no (for now) | no reader in the pinned Wine's dlls/programs/server |
| `WINE_HIDE_AMD_GPU` (1970) | 1 | | no | would hide the console's only adapter |
| `WINE_DISABLE_GAMESCOPE_MAX_SIZE_HACK` (1975) | 2 | | no | gamescope |
| `PROTON_USE_WOW64` (1999) | 1 | | no | already always `WINEARCH=wow64` |
| `PROTON_MEDIACONV_NO_VIDEO` (2042) | 7 | Valve's media converter blank video | no | winegstreamer/media converter replaced by winedmo (2001) |
| `default_cpu_limit` (1784) | 20 | `WINE_CPU_TOPOLOGY` 1/4/8/16/28/30 | partly | `native/proton_wine.cpp:343` already derives it; only limits below the PS5's CPU count (1, 4) matter |
| `OPENSSL_ia32cap=~0x20000000` (2326) | 56 | disables RDRAND in bundled OpenSSL (AMD Zen RDRAND hang) | **yes, measure** | the largest list; the PS5's CPU is Zen 2; GOG's default fix sets it for every GOG game |
| `ddraw=n,b` (2386) | 5 | DLL override | yes | data |
| `dinput`, `winmm`, `gameinput` (2396-2404) | 1 each | DLL overrides | yes | data |
| `PROTON_LIMIT_RESOLUTIONS` (2408, 2412) | 1, 6 | mode list cap 16 / 32 | yes | `win32u/sysparams.c:6215`, mode list comes from `WINE_PS5_DESKTOP` |
| `PROTON_SPOOF_STEAMINPUT_VIDPID` (2423) | 3 (incl. the 2D roguelike) | winebus | no | pad path |
| `atiadlxx=b` (2732) | 1 | DLL override | yes | data |
| `PROTON_LIMIT_ADDRESS_SPACE` (2738) | 3 | caps user space at `0x7000_0000_0000` | yes, harmless | `ntdll/unix/virtual.c:2766`; cheap |
| Imperiums `WINE_MONO_HIDETYPES` (2537) | 1 | | yes | wine-mono |
| CoD BO3 / Cold War / Undecember via `steam.exe` (2793) | 3 | runs the game through Steam | no | the project's Steam is separate |
| God of War Ragnarok hidraw (2524), Teardown mimalloc (2755, aarch64), IL-2 `localconfig.vdf` (1335), Locoland `steam.exe` (1839), per-id fonts (1092), FEX profile (2145) | 1 each | | no | Steam Deck, aarch64, Steam userdata, FEX |
| `opencl=n,d` for all but two ids (1842) | all | DLL override | check | a global default; MihawkOS's defaults are `d3d*=n;bcrypt=b` only |

Other global (not id-keyed) Proton defaults this project does not set: `steam.exe=b`, `dotnetfx35.exe=b`,
`dotnetfx35setup.exe=b`, `beclient*.dll=b,n`, `winebth.sys=d`, `opencl=n,d`, `WINE_LARGE_ADDRESS_AWARE=1`. Each is one
line; whether it matters is a console question.

**Carry as data, not code.** Everything marked yes is one of four shapes: an env word, a DLL override, a command-line
append, a registry value. A single table keyed by Steam app id (`appid  kind  name  value  source`) generated from the
script and from protonfixes (§3) by a tool that parses them with `ast` (no execution), compiled into the title as a
sorted array, looked up where `proton_add_steam_ids` already runs. Precedence: the entry's own `env` > the table.
The compat-option → env mapping (about 20 options) lives in the tool, once.

## 3. umu-protonfixes as a reference library

- **Source and pin.** `github.com/Open-Wine-Components/umu-protonfixes` (renamed from `open-wine-components`; the fork's
  `.gitmodules` uses the old spelling), default branch `master`, pinned by the fork at
  `27f5c708ec4d4150fbff97acb30a5b21ad893994` ("update umu-database", 2026-09-15). Use that commit, not `master`
  (pushed again 2026-10-30).
- **Licence: BSD-2-Clause** (`LICENSE`: "Copyright (c) 2018, Chris Simons"). The `proton` script is Valve's BSD-3-Clause
  (`LICENSE.proton`, "Copyright (c) 2018-2022, Valve Corporation"). Both allow redistribution of source and derived
  data with the notice and disclaimer kept and no endorsement; neither conflicts with GPL-3.0-or-later.
- **NOTICE.md row to add when something is taken** (Pinned upstreams table, rule 6): `umu-protonfixes` |
  `Open-Wine-Components/umu-protonfixes` `27f5c708ec4d4150fbff97acb30a5b21ad893994`, read by `tools/<extractor>`; only
  the per-game values (env names/values, DLL overrides, arguments) are carried as data, no Python is vendored |
  BSD-2-Clause, Copyright (c) 2018 Chris Simons; same for the `proton` script's lists (BSD-3-Clause, Valve). The
  extractor records the pin in the generated file's header. Nothing is vendored now.
- **What is in it** (468 fix files at the pin, 311 Steam, 78 GOG, 36 UMU, 27 EGS, 16 other stores; counted by pattern
  over the calls each file makes, so ±a few):

| Class | Files | Share | Portable as data? |
| --- | --- | --- | --- |
| env / DLL override / argument / `replace_command` only | 147 | 31% | **yes** |
| the same plus registry values, or registry only | 19 | 4% | yes (registry as data) |
| winetricks / protontricks only | 171 | 37% | no: installs redistributables |
| winetricks plus other steps | 68 | 15% | the data part only |
| host-path / Linux steps (ini and xml edits in the game folder, save import, `create_dos_device`, DOSBox config, EAC/BattlEye runtime install, `patch_libcuda`), with or without data | 57 | 12% | the data part only |
| nothing to apply | 6 | 1% | n/a |

  In total 250 of 468 files (153 of the 311 Steam ones) contain at least one env/DLL/argument/registry call. The
  winetricks verbs are few and repeated: `d3dcompiler_47` (47), `d3dcompiler_43` (26), `directplay` (22), `vcrun2019`
  (19), `physx` (14), `vcrun2022` (13), `corefonts`, `d3dx9_43`, `dsound`, `mfc42`; 384 verb calls in 239 files.
  Those need a redistributables pipeline (download from Microsoft, run silently in the prefix, once), which this project
  already does slowly for Steam; not a data job, and unsized here.
- **Env names in its data part** (95 uses): `PROTON_DLL_COPY` 18, `PROTON_OLD_GL_STRING` 12, `SteamDeck` 10,
  `PULSE_LATENCY_MSEC` 10, `WINE_DISABLE_VULKAN_OPWR` 4, `PROTON_USE_D7VK` 3, `PROTON_ENABLE_INT3_HACK` 3, then singles.
  Roughly half are Linux-only (Pulse, Steam Deck, gamescope, radeonsi, X11); the extractor needs a deny-list of names,
  and `PROTON_OLD_GL_STRING` / `MESA_EXTENSION_MAX_YEAR` need a check against Zink.
- **DLL overrides** (82 uses, 31 names): `ddraw` 14, `dinput` 13, `winmm` 8: the game's own wrapper DLLs in its folder.
- **Keys.** `gamefixes-steam/<appid>.py`; GOG/EGS/UMU files are keyed by store ids mapped through `umu-database.csv`
  (not read here), which MihawkOS cannot know without a store hint. So only the Steam half (311 files) is reachable by
  the id of §1.

## 4. What the change does and does not give

It makes every Wine hack and every future table row **possible**; it adds no fix by itself. A game runs from
`steamapps/common` with a manifest, or has `steam_appid.txt`, or has the ids in its `env`; anything else (a GOG copy
without the file) still gets no id, and the answer there is the entry's `env`.

## 5. Plan

| Step | Work | Hours (host) | Console |
| --- | --- | --- | --- |
| 1 | Land §1.4: gate it, commit (owner decides) | 0.5 | one gate, 2 game runs (the 2D platformer and the 2D roguelike) and the hack probe, ~1 h |
| 2 | Measure the unmeasured: what Steam gives its games (a probe that prints both environments, started by Steam), and the Unix side in a child (`SteamGameId=292120` shows `HACK: fsync: yield to waiters` in the log) | 1 | ~1 h |
| 3 | Only if step 2 shows the Unix side empty for Steam-started games: Wine patch `CreateProcessInternalW` → `__wine_set_unix_env` for the two names (Proton's `sync_env_var_to_unix` pattern, race with two simultaneous launches noted) | 2-3 | gate + build |
| 4 | Extractor `tools/extract-game-fixes.py`: `ast`-parse the proton script's lists and the Steam half of protonfixes into `data/game-fixes.tsv` (header: sources, pins, licences), with the deny-list of §3; host test over a fixture and a count check | 5 | none |
| 5 | `src/pw_game_fixes.{h,c}`: sorted array generated from the TSV at build, lookup by app id, merged into `proton_add_steam_ids` (entry env wins); host tests incl. an id that must produce nothing | 3 | none |
| 6 | NOTICE.md rows (two), `LICENSING.md` line, doc | 0.5 | none |
| 7 | Global defaults of §2 (`WINE_LARGE_ADDRESS_AWARE=1`, the DLL override list) as a measured A/B on one game | 1.5 | ~2 h |
| 8 | RDRAND (`OPENSSL_ia32cap`): find a game on this console that uses bundled OpenSSL and hangs, or declare it unneeded | 1 | ~1 h |
| — | Redistributable verbs (`d3dcompiler_47`, `vcrun*`, `physx`, `directplay`): a different design (installer pipeline), not sized | — | — |

Steps 1-6 ≈ 12 h host work plus ~2 h console; 7-8 add ~2.5 h and ~3 h console. The table (4-6) is worth doing only
after step 1 is in, since it feeds the same function.

## 6. Console questions queued (not run)

Added to `docs/CONSOLE_QUEUE.md`: steps 1 and 2 above, so the next console session can answer them in one go.
