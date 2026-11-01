#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The regression battery: what the production install must still do after a change.

Each case is one ordinary launch through tools/run-console.py on the installed title and runtime (tools/
build-production.sh, tools/deploy-wine.py, ps5/tools/deploy.sh), with nothing swapped in; it writes its own
evidence/<run>.json and klog/<run>*.log. The battery writes evidence/<name>.json with every case's verdict.

Cases, in the order they run:

  title layout (the default: the program in the title, the processes it starts in helpers)
    pe64          wine-probe.exe: signals, FP/AVX, fixed memory, JIT, threads, events
    d3d11         d3d11-probe.exe: DXVK in the title, frames and a readback
    opengl        gl-vertex-probe.exe: OpenGL in the title
    children      process-probe.exe: a child's lifecycle, arguments, environment, inherited handles, exit codes
    descendants   process-probe.exe --descendants: three generations through helpers
    file-budget   process-probe.exe --file-budget: 450 files open in four processes (the budget is about 250)
    console-children  process-probe.exe: no conhost.exe for a console nobody sees, one for CREATE_NEW_CONSOLE
    dns           dns-probe.exe: names through the console's resolver, and an HTTP answer over TCP
    tls           tls-probe.exe: HTTPS through WinHTTP (schannel over GnuTLS, crypt32's roots), an expired
                  certificate refused
    library-entry library entry 1 (if installed): an OpenGL game for 60 s, then stopped from the title

  launcher layout (every process in a helper, drawing through the title's GPU broker)
    launcher-children     process-probe.exe
    launcher-render       process-probe.exe --graphics-broker: DXVK in two children and their descendants
    launcher-file-budget  process-probe.exe --file-budget
    launcher-tls          tls-probe.exe in a helper against owned trusted/untrusted LAN endpoints
    launcher-desktop      desktop-probe.exe: a GDI window shown by the title's compositor, the touchpad's pointer and
                          clicks reaching it
    memory-guard          memory-guard-probe.exe --hold=300: asked to close by the title's guard over the console's
                          system memory, its limit lowered to 450 MiB
    pe32, wow64           32-bit programs under WoW64: the CPU side, and the runtime around them (child, registry,
                          audio, XInput)
    hid32                 the DualSense over HID in a 32-bit program
    vk32, gl32, d3d11-32  32-bit Vulkan with mapped memory, OpenGL through Zink, Direct3D 11 through DXVK

  tools/run-battery.py --name NAME [--only CASE ...] [--skip CASE ...] [--gate] [--grouped]

--grouped: the selected cases of each group in GROUPS share one launch (tests/gate-sequence-probe.c runs them one
after another, each its own Windows process), at the place of the group's first case; each keeps its own verdict.

The fault-injection and ending cases (ON_REQUEST) run only when named in --only, one per run.

To stop at the next completed case, create .deps/NAME.stop. The current
console runner finishes normally; an interrupted battery never passes. It also
stops before a case the console guard would refuse (tools/console_guard.py:
a drop, storage damage, an unverified deploy), instead of running every
remaining case into the same refusal.
"""
import argparse
import importlib.util
import json
import runpy
import re
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import console_guard  # noqa: E402

LAUNCHER = ['--env', 'PROTON_LAYOUT=launcher']
# The player's MihawkOS settings a launch can name itself (native/proton_wine.cpp: "this game's or launch's own"), at
# their defaults, so a gate does not depend on what the player last chose: the owner's "controller dualsense" hid
# XInput's controller 0 and failed wow64 (gate-0036-pro-01, 2026-10-27). A case that names one keeps its own; the
# runners that test a setting (Game Mode, controls, logging, mouse) set theirs. Logging and the controls mapping have
# no launch override: a gate still reads the player's.
PLAYER_DEFAULTS = ('WINE_PS5_CONTROLLER=xbox-360', 'WINE_PS5_GAME_MODE=1', 'PROTON_120HZ=1')


def player_defaults(case=()):
    """['--env', NAME=VALUE, ...] for each player default the case does not name itself."""
    named = {word.split('=', 1)[0] for word in case if isinstance(word, str) and '=' in word}
    return [word for default in PLAYER_DEFAULTS if default.split('=', 1)[0] not in named for word in ('--env', default)]
# Explicit initial settings: arbitrary application keys, empty/UTF-8 values,
# case-folded precedence and PATH must survive registry provisioning.
INITIAL_ENV = [item for setting in [
    'MIHAWK_ENV_ROOT=launch selected', 'MIHAWK_ENV_UTF8=café Ω',
    'MIHAWK_ENV_EMPTY=', 'MIHAWK_ENV_CASE=first', 'mihawk_env_case=last wins',
    r'PATH=C:\Selected Tools;C:\windows\system32'] for item in ['--env',setting]]
CASES = [
    ('shell32', ['--arch','32']),
    ('shell64', ['--arch','64']),
    ('work-area32', ['--exe','work-area32-probe.exe','--marker','PS5_PROTON_WORK_AREA'] + LAUNCHER),
    ('work-area64', ['--exe','work-area64-probe.exe','--marker','PS5_PROTON_WORK_AREA'] + LAUNCHER),
    ('wallpaper32', ['--exe','wallpaper32-probe.exe','--marker','PS5_PROTON_WALLPAPER'] + LAUNCHER),
    ('wallpaper64', ['--exe','wallpaper64-probe.exe','--marker','PS5_PROTON_WALLPAPER'] + LAUNCHER),
    ('cursor-scheme32', ['--exe','cursor-scheme32-probe.exe','--marker','PS5_PROTON_CURSOR_SCHEME'] + LAUNCHER),
    ('cursor-scheme64', ['--exe','cursor-scheme64-probe.exe','--marker','PS5_PROTON_CURSOR_SCHEME'] + LAUNCHER),
    ('theme-parts32', ['--exe','theme-parts32-probe.exe','--marker','PS5_PROTON_THEME_PARTS'] + LAUNCHER),
    ('theme-parts64', ['--exe','theme-parts64-probe.exe','--marker','PS5_PROTON_THEME_PARTS'] + LAUNCHER),
    ('monospace32', ['--exe','monospace32-probe.exe','--marker','PS5_PROTON_MONOSPACE'] + LAUNCHER),
    ('monospace64', ['--exe','monospace64-probe.exe','--marker','PS5_PROTON_MONOSPACE'] + LAUNCHER),
    ('gpu-window32', ['--arch','32','--graphics']),
    ('gpu-window64', ['--arch','64','--graphics']),
    ('dxvk-window32', ['--arch','32','--graphics','--backend','dxvk']),
    ('dxvk-window64', ['--arch','64','--graphics','--backend','dxvk']),
    # The broker's host-visible buffers from the application's direct memory, all given back (patch 0009).
    ('host-dmem64', ['--arch','64','--graphics','--backend','dxvk','--dmem']),
    # The helper refuses every range after the first 20: the broker makes the rest as shared memory, and the program still passes.
    ('host-dmem-fallback64', ['--arch','64','--graphics','--backend','dxvk','--dmem','--fallback','--env','PROTON_GPU_TEST_DMEM_REFUSE=20']),
    # Judged with the fast copy off: their layered-window blends read wrong in the after-flip capture while the screen showed
    # them right (the owner, every gate; alpha7-gate-pro-2026-11-01 failed, wc64-fastcopy0-pro-2026-11-01 passed).
    ('window-compose32', ['--arch','32', '--env', 'PROTON_GPU_FAST_COPY=0']),
    ('window-compose64', ['--arch','64', '--env', 'PROTON_GPU_FAST_COPY=0']),
    # DWM corners, borders, shadows, acrylic/Mica glass and live thumbnails on the displayed frame.
    ('window-effects32', ['--arch','32']),
    ('window-effects64', ['--arch','64']),
    ('window-snapshot32', ['--path',r'C:\windows\system32\mihawk.exe','--args','C:\\window-snapshot32-probe.exe','--marker','PS5_PROTON_WINDOW_SNAPSHOT','--env','PROTON_WINDOW_SNAPSHOT_PROBE=1'] + LAUNCHER),
    ('window-snapshot64', ['--path',r'C:\windows\system32\mihawk.exe','--args','C:\\window-snapshot64-probe.exe','--marker','PS5_PROTON_WINDOW_SNAPSHOT','--env','PROTON_WINDOW_SNAPSHOT_PROBE=1'] + LAUNCHER),
    ('initial-env32', ['--exe','initial-env32-probe.exe','--marker','PS5_PROTON_INITIAL_ENV'] + INITIAL_ENV + LAUNCHER),
    ('initial-env64', ['--exe','initial-env64-probe.exe','--marker','PS5_PROTON_INITIAL_ENV'] + INITIAL_ENV + LAUNCHER),
    # The guided setup's language reaches Windows programs and their children (patch 1230; docs/GUIDED_SETUP.md)
    ('locale64', ['--exe','locale64-probe.exe','--marker','PS5_PROTON_LOCALE','--env','WINE_PS5_LOCALE=fr_FR.UTF-8'] + LAUNCHER),
    ('appearance32', ['--arch','32']),
    ('appearance64', ['--arch','64']),
    ('theme-notify32', ['--exe','theme-notify32-probe.exe','--marker','PS5_PROTON_THEME_NOTIFY_PROBE']),
    ('theme-notify64', ['--exe','theme-notify64-probe.exe','--marker','PS5_PROTON_THEME_NOTIFY_PROBE']),
    ('proton-ui32', ['--exe', 'proton-ui32-probe.exe', '--marker', 'PS5_PROTON_UI_PROBE'] + LAUNCHER),
    ('proton-ui64', ['--exe', 'proton-ui64-probe.exe', '--marker', 'PS5_PROTON_UI_PROBE'] + LAUNCHER),
    # No 12 s holds: nothing here reads the census samples they are for (tests/memory-class-probe.c).
    ('memory-classes', ['--exe', 'memory-class-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP',
                        '--args=--hold-ms=0'] + LAUNCHER),
    ('graphics-teardown', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP',
                           '--args=--graphics-teardown', '--watchdog', '120'] + LAUNCHER),
    # Alpha 7's default, "Run inside MihawkOS": library copies of the renderer, none named. game64.exe must run in the
    # title; a second 64-bit copy (the title takes one), the 32-bit copy and the opted-out copy in helpers, each drawing
    # and ending with 0 (process-probe --title-games). First passed on the base, rung8-base-2026-10-31-fallbacks.
    ('title-games', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP', '--args=--title-games',
                     '--env', 'WINE_PS5_TITLE_OPTOUT=optout64.exe', '--env', 'PS5_PROTON_PROBE_FRAMES=3600',
                     '--expect-title-game', 'game64.exe', '--watchdog', '150'] + LAUNCHER),
    # A game's launcher in a library folder (a copy of process-probe: no graphics import) starts the renderer and exits:
    # its imports keep the launcher in a helper, the renderer takes the title, and the session outlives the launcher.
    ('launcher-pair', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP', '--args=--launcher-pair',
                       '--env', 'PS5_PROTON_PROBE_FRAMES=1200', '--expect-title-game', 'game64.exe', '--watchdog', '180'] + LAUNCHER),
    # A game in the title whose parent is alive in a helper (tests/parent-query-probe.c, Wine 2426): what it learns of the
    # parent (basic, WoW64 and memory counters, the PEB read, its modules and their names) is as good as of itself.
    ('parent-query64-title', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP', '--args=--parent-query',
                              '--expect-title-game', 'game64.exe', '--watchdog', '180'] + LAUNCHER),
    # A library game whose DLL is missing (tests/missing-import-probe.c): the title takes it, Wine's loader fails at once
    # (c0000135), and the session must end in order with the title alive, not with MihawkOS (a tester's demo, 2026-11-01).
    # The overlay beside it is held at its start (WINE_PS5_TEST_SPAWN_STALL, wine/ps5/pw_spawn_wait.h), as the console's
    # wedged loader held the store overlay's: the title's end lets that start go after 30 s instead of waiting for it.
    ('missing-import64-title', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP', '--args=--missing-import',
                                '--env', 'WINE_PS5_TEST_SPAWN_STALL=overlay64.exe',
                                '--expect-title-game', 'game64.exe', '--watchdog', '150'] + LAUNCHER),
    # A Chromium-based game (a renderer copy with Chromium's resources beside it): started as one process (patch 2404's
    # --single-process), in a helper, drawing and ending with 0. The 2D NW.js game this stands for: evidence/moonstone-base-2026-10-31-helper-single.json.
    ('chromium-game', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP', '--args=--chromium-game',
                       '--env', 'PS5_PROTON_PROBE_FRAMES=240', '--watchdog', '150'] + LAUNCHER),
    ('dnsapi32', ['--exe', 'dnsapi32-probe.exe', '--marker', 'PS5_PROTON_DNSAPI'] + LAUNCHER),
    ('dnsapi64', ['--exe', 'dnsapi64-probe.exe', '--marker', 'PS5_PROTON_DNSAPI'] + LAUNCHER),
    # Media Foundation playback of a generated 1080p60 H.264 + AAC MP4 (winedmo over FFmpeg, patches 2001-2063): a
    # Direct3D 12 game dereferenced NULL when its intro video's source could not be made (2026-10-16).
    ('mf-video32', ['--exe', 'mf-video32-probe.exe', '--marker', 'PS5_PROTON_MF_VIDEO'] + LAUNCHER),
    ('mf-video64', ['--exe', 'mf-video64-probe.exe', '--marker', 'PS5_PROTON_MF_VIDEO'] + LAUNCHER),
    ('network-adapters32', ['--exe', 'network-adapters32-probe.exe', '--marker', 'PS5_PROTON_NETWORK_ADAPTERS'] + LAUNCHER),
    ('network-adapters64', ['--exe', 'network-adapters64-probe.exe', '--marker', 'PS5_PROTON_NETWORK_ADAPTERS'] + LAUNCHER),
    ('socket-options32', ['--exe', 'socket-options32-probe.exe', '--marker', 'PS5_PROTON_SOCKET_OPTIONS'] + LAUNCHER),
    ('socket-options64', ['--exe', 'socket-options64-probe.exe', '--marker', 'PS5_PROTON_SOCKET_OPTIONS'] + LAUNCHER),
    # Native payload mappings must never be advertised as free Windows address space.
    ('virtual-query32', ['--exe', 'virtual-query32-probe.exe', '--marker', 'PS5_PROTON_VIRTUAL_QUERY']),
    ('virtual-query64', ['--exe', 'virtual-query64-probe.exe', '--marker', 'PS5_PROTON_VIRTUAL_QUERY']),
    # WriteFile's contents, sizes and visibility to other handles and processes (the PS5 writes through a mapping, patch 1007).
    ('file-write32', ['--exe', 'file-write32-probe.exe', '--marker', 'PS5_PROTON_FILE_WRITE']),
    ('file-write64', ['--exe', 'file-write64-probe.exe', '--marker', 'PS5_PROTON_FILE_WRITE']),
    # Directories opened relative to a handle, as .NET lists a folder, with 4400 files held first so the title's server
    # descriptors pass 4096: pw_wine_cwd's path table stopped there and a .NET game's fonts were refused (2026-10-24).
    ('relative-dir32', ['--exe', 'relative-dir32-probe.exe', '--marker', 'PS5_PROTON_NT_RELDIR_PROBE'] + LAUNCHER),
    ('relative-dir64', ['--exe', 'relative-dir64-probe.exe', '--marker', 'PS5_PROTON_NT_RELDIR_PROBE'] + LAUNCHER),
    # Thousands of files past the console's ~256 descriptors (parked and reopened, pw_wine_fdv.h) and 6000 events held together,
    # renamed and deleted while open, and a path past MAX_PATH used in the other case (tests/handles-paths-probe.c).
    ('handles-paths32', ['--exe', 'handles-paths32-probe.exe', '--marker', 'PS5_PROTON_HANDLES_PATHS'] + LAUNCHER),
    ('handles-paths64', ['--exe', 'handles-paths64-probe.exe', '--marker', 'PS5_PROTON_HANDLES_PATHS'] + LAUNCHER),
    # A cancelled overlapped pipe read leaves the pipe working (ntdll's pipe test on the console, 2026-10-25: the next read
    # answered c0000024 and the other end's write 232), byte and message mode (tests/pipe-cancel-probe.c).
    ('pipe-cancel32', ['--exe', 'pipe-cancel32-probe.exe', '--marker', 'PS5_PROTON_PIPE_CANCEL'] + LAUNCHER),
    ('pipe-cancel64', ['--exe', 'pipe-cancel64-probe.exe', '--marker', 'PS5_PROTON_PIPE_CANCEL'] + LAUNCHER),
    # SGDT, SIDT, SLDT, SMSW and STR run, as on Windows and Linux: the console's CPU refuses them in user mode (UMIP)
    # and a store game's anti-tamper `sgdt [rsp]` ended it (2026-10-27); patch 2102 emulates them (tests/umip-probe.c).
    ('umip32', ['--exe', 'umip32-probe.exe', '--marker', 'PS5_PROTON_UMIP'] + LAUNCHER),
    ('umip64', ['--exe', 'umip64-probe.exe', '--marker', 'PS5_PROTON_UMIP'] + LAUNCHER),
    # A byte-range lock is freed with the handle that took it, and StgOpenStorage takes under 2 s (tests/lock-probe.c).
    ('lock32', ['--exe', 'lock32-probe.exe', '--marker', 'PS5_PROTON_LOCK'] + LAUNCHER),
    ('lock64', ['--exe', 'lock64-probe.exe', '--marker', 'PS5_PROTON_LOCK'] + LAUNCHER),
    # An exception at the bottom of a deep stack is dispatched, walked down or entered past the guard with no probe
    # (tests/stack-limit-probe.c; patch 2093: kernel32 heap's 512 KiB of locals ended the process).
    ('stack-limit64', ['--exe', 'stack-limit64-probe.exe', '--marker', 'PS5_PROTON_STACK_LIMIT'] + LAUNCHER),
    # Another process's memory read and written through the helpers' memory service, a suspended child's included, as
    # launchers, Chromium's sandbox and hooking libraries do (tests/remote-memory-probe.c, patch 0942).
    ('remote-memory64', ['--exe', 'remote-memory-probe.exe', '--marker', 'PS5_PROTON_REMOTE_MEMORY_PROBE'] + LAUNCHER),
    # What a program is told about RAM (a game launcher's check said 665 MB).
    ('system-memory32', ['--exe', 'system-memory32-probe.exe', '--marker', 'PS5_PROTON_SYSTEM_MEMORY']),
    ('system-memory64', ['--exe', 'system-memory64-probe.exe', '--marker', 'PS5_PROTON_SYSTEM_MEMORY']),
    # A written image page reports PAGE_READWRITE (Chromium's protected-memory check; a game launcher's renderers).
    ('image-protect32', ['--exe', 'image-protect32-probe.exe', '--marker', 'PS5_PROTON_IMAGE_PROTECT']),
    ('image-protect64', ['--exe', 'image-protect64-probe.exe', '--marker', 'PS5_PROTON_IMAGE_PROTECT']),
    # A small shared writable section off the 16 KiB host page (GameMaker's .mydata): the image starts (patch 2406).
    ('shared-section32', ['--exe', 'shared-section32-probe.exe', '--marker', 'PS5_PROTON_SHARED_SECTION']),
    ('shared-section64', ['--exe', 'shared-section64-probe.exe', '--marker', 'PS5_PROTON_SHARED_SECTION']),
    # A suspended thread's context is on its stack (patch 1172): CEF's stack sampler read from one in the Unix stack
    # and died (browser-cluster16k-2026-10-16-01), and every GC or crash reporter that walks a stack would.
    ('suspend-context64', ['--exe', 'stack-probe.exe', '--marker', 'PS5_PROTON_STACK_PROBE']),
    # A thread killed while its execute fault converts code keeps no lock (patch 1171; winedevice.exe's exit stall).
    ('kill-convert', ['--exe', 'kill-convert-probe.exe', '--marker', 'PS5_PROTON_KILL_CONVERT']),
    # Diagnostic until the extended address band is available on the console. Its 13.5 GiB section is in direct memory,
    # mapped a chunk at a time and given back whole once the probe has ended (patches 1121, 1125, 1126).
    ('large-shared-section', ['--exe', 'section-view-probe.exe', '--marker', 'PS5_PROTON_LARGE_SECTION',
                              '--expect-direct-section'] + LAUNCHER),
    ('late-library', ['--exe', 'late-library-probe.exe', '--marker', 'PS5_PROTON_LATE_LIBRARY'] + LAUNCHER),
    ('guest-address-space', ['--exe', 'guest-address-space-probe.exe', '--marker', 'PS5_PROTON_GUEST_ADDRESS_SPACE']),
    ('controls-menu', ['--controls', 'remapped']),
    # The launcher says why the last session ended: a failed session's reason, a crashed one's marker, and the runtime
    # it was built with (tools/run-launcher-ended.py; docs/SELF_INSTALL.md, phase 1).
    ('launcher-ended', []),
    # A game folder copied into C:\Games gets its desktop shortcut with no game.json; one with two programs gets none
    # (tools/run-games-autodetect.py, src/pw_game_folders.h; docs/SELF_INSTALL.md, phase 2).
    ('games-autodetect', []),
    # Settings > Logging: the desktop session's folder, complete and masked, once on; its off leg is skipped here
    # (tools/run-detailed-logs.py, src/pw_detailed_logs.h).
    ('detailed-logs', ['--only', 'on']),
    # The guided setup walked as a first run, in Japanese, to Preparing, on a set-up console: nothing of the owner's is
    # touched (tools/run-launcher.py --setup; docs/GUIDED_SETUP.md).
    ('setup-tour', ['--setup', '--frames', '1500']),
    # The Steam guide (src/pw_steam_guide.h): its five launcher pages in English, each kept as a picture
    # (tools/run-launcher.py --steam-guide-pictures), and the five screens its sessions draw over the client's windows,
    # shown through the GPU broker's cover over a running program and captured (the title's showcase; docs/STEAM.md).
    ('steam-guide-pages', ['--steam-guide-pictures', 'en-US', '--frames', '160']),
    ('steam-guide-screens', ['--path', r'C:\windows\notepad.exe', '--marker', 'PS5_PROTON_STEAM_GUIDE_SHOWCASE',
                             '--env', 'PROTON_STEAM_GUIDE_SHOWCASE=en-US', '--seconds', '60'] + LAUNCHER),
    ('mouse-controls', []),
    # Manual launches have no test-runner stdout override. Handles must accept
    # complete writes, and redirected descendants must keep their output pipes.
    ('manual-stdout', ['--exe', 'stdout-probe.exe', '--no-stdout'] + LAUNCHER),
    ('redirected-stdout', ['--exe', 'stdout-probe.exe', '--no-stdout', '--args=--redirected'] + LAUNCHER),
    # Both architectures must import the graphics/ICU baseline and draw real
    # GDI/DirectWrite glyphs on the very first launch in a new prefix.
    ('prefix-fresh64', ['--arch', '64']),
    ('prefix-fresh32', ['--arch', '32']),
    ('pe64', ['--exe', 'wine-probe.exe', '--marker', 'PS5_PROTON_PE64_PROBE']),
    # Windows synchronization through the session's sync device, on by default: semantics, then a thread terminated
    # while it waits, whose wait must not take the next set (src/pw_ntsync.c).
    ('sync64', ['--exe', 'sync-probe.exe', '--marker', 'PS5_PROTON_SYNC_PROBE']),
    # A program opened from File Explorer starts in its own folder (shell32 1184): a Direct3D 12 game opened from a File
    # Explorer window started in Explorer's folder, missed its shader cache and stayed on a black window.
    ('shell-open64', ['--exe', 'shell-open-probe.exe', '--marker', 'PS5_PROTON_SHELL_OPEN', '--args=--open']),
    # A 32-bit program under WoW64 (docs/IA32_DESIGN.md, M3): in a helper, whose loader gives ntdll the kernel's
    # sigaction (patches 0945 and 0946; the probe prefix's syswow64 comes from tools/stage-wine.py).
    ('pe32', ['--exe', 'pe32-probe.exe', '--marker', 'PS5_PROTON_PE32_PROBE'] + LAUNCHER),
    # An old installer's link, /fixed at 0x400000: it loads there because the payload loader gave the carrier's image
    # back (native/proton_payload_child.c; docs/IA32_DESIGN.md, "A fixed-base program at 0x400000").
    ('fixed-base32', ['--exe', 'fixed-base32-probe.exe', '--marker', 'PS5_PROTON_FIXED_BASE32'] + LAUNCHER),
    ('telemetry-teardown', ['--path', r'C:\window-probe.exe', '--telemetry-hold', '--watchdog', '90']),
    # The helper's backstop (src/pw_control_watch.h, patch 1135): the run's program, told to doom itself 3 s into its
    # loader's watch, is ended by its loader 5 s later without Wine, nothing asks it to close, and the title comes back.
    ('doom-backstop', ['--path', r'C:\window-probe.exe', '--env', 'WINETEST_DOOM_AFTER_MS=3000', '--watchdog', '90']
                      + LAUNCHER),
    ('native-signals', ['--exe', 'pe32-probe.exe', '--marker', 'PS5_PROTON_NATIVE_SIGNAL_PROBE',
                        '--env', 'WINE_PS5_NATIVE_SIGNALS=1'] + LAUNCHER),
    ('desktop-overlap', []),
    # What a 32-bit program needs beyond its process (docs/IA32_DESIGN.md, M4 step 2): a 32-bit child process
    # (patch 0948), the prefix's 32-bit registry view (0949), audio through the driver's 32-bit entry points and
    # XInput through the patched i386 modules.
    ('wow64', ['--exe', 'wow64-probe.exe', '--marker', 'PS5_PROTON_WOW64_PROBE'] + LAUNCHER),
    # The DualSense through the i386 HID drivers in a 32-bit program's own process (patch 0950, syswow64\drivers).
    ('hid32', ['--exe', 'hid32-probe.exe', '--marker', 'PS5_PROTON_HID_PROBE', '--env', 'WINE_PS5_CONTROLLER=dualsense']
              + LAUNCHER),
    # 32-bit rendering (docs/IA32_DESIGN.md, M4 step 3), each in a helper: Vulkan memory mapped below 4 GiB through
    # the GPU broker's placed mappings and a GPU fill read back; OpenGL through the i686 Mesa/Zink; Direct3D 11
    # through the i386 DXVK (about two minutes of frames).
    ('vk32', ['--exe', 'vk32-probe.exe', '--marker', 'PS5_PROTON_VK_PROBE'] + LAUNCHER),
    ('gl32', ['--exe', 'gl-vertex32-probe.exe', '--marker', 'PS5_PROTON_GL_VERTEX_PROBE'] + LAUNCHER),
    ('input32', ['--exe', 'input-message32-probe.exe', '--marker', 'PS5_PROTON_INPUT_MESSAGE_PROBE'] + LAUNCHER),
    ('input64', ['--exe', 'input-message64-probe.exe', '--marker', 'PS5_PROTON_INPUT_MESSAGE_PROBE'] + LAUNCHER),
    # Burst upload of 384 MiB, pixel checks, presentation and release/reuse through production Zink.
    ('gl-upload32', ['--exe', 'gl-upload32-probe.exe', '--marker', 'PS5_PROTON_GL_UPLOAD_PROBE', '--watchdog', '240'] + LAUNCHER),
    ('gl-upload64', ['--exe', 'gl-upload64-probe.exe', '--marker', 'PS5_PROTON_GL_UPLOAD_PROBE', '--watchdog', '240'] + LAUNCHER),
    ('d3d11-32', ['--exe', 'd3d11-32-probe.exe', '--marker', 'PS5_PROTON_D3D11_PROBE'] + LAUNCHER),
    # Direct3D 12's feature levels (tests/d3d12-levels-probe.c): in a helper, then in the title beside the broker.
    ('d3d12-levels64', ['--exe', 'd3d12-levels-probe.exe', '--marker', 'PS5_PROTON_D3D12_LEVELS'] + LAUNCHER),
    ('d3d12-levels64-title', ['--exe', 'd3d12-levels-probe.exe', '--marker', 'PS5_PROTON_D3D12_LEVELS',
                              '--env', 'WINE_PS5_TITLE_PROCESS=d3d12-levels-probe.exe'] + LAUNCHER),
    # 10,000 files open for writing at once from a helper (tests/many-files-probe.c): the title's descriptor table keeps
    # room for them (wine/ps5/pw_wine_codeshare.h's cap and release).
    ('many-files64', ['--exe', 'many-files-probe.exe', '--marker', 'PS5_PROTON_MANY_FILES', '--watchdog', '600'] + LAUNCHER),
    # Direct3D 12 reserved resources past the old 4096-range cap (tests/d3d12-sparse-probe.c, graphics patch 0044):
    # in a helper, as the racing game that hit it ran, and in the title.
    # 20 MB of a game's Wine output in the title (tests/log-flood-probe.c): the session's logs keep their start and their
    # end, and stay bounded (src/pw_capped_log.h).
    ('log-flood64-title', ['--exe', 'log-flood-probe.exe', '--marker', 'PS5_PROTON_LOG_FLOOD', '--watchdog', '600',
                           '--expect-log-ends', '--expect-title-game', 'log-flood-probe.exe', '--env', 'WINEDEBUG=warn+debugstr',
                           '--env', 'WINE_PS5_TITLE_PROCESS=log-flood-probe.exe'] + LAUNCHER),
    ('d3d12-sparse64', ['--exe', 'd3d12-sparse-probe.exe', '--marker', 'PS5_PROTON_D3D12_SPARSE'] + LAUNCHER),
    ('d3d12-sparse64-title', ['--exe', 'd3d12-sparse-probe.exe', '--marker', 'PS5_PROTON_D3D12_SPARSE',
                              '--env', 'WINE_PS5_TITLE_PROCESS=d3d12-sparse-probe.exe'] + LAUNCHER),
    # Direct3D 12's shared resources without Win32 external memory (tests/d3d12-shared-probe.c), in the title as games are.
    ('d3d12-shared64-title', ['--exe', 'd3d12-shared-probe.exe', '--marker', 'PS5_PROTON_D3D12_SHARED',
                              '--env', 'WINE_PS5_TITLE_PROCESS=d3d12-shared-probe.exe'] + LAUNCHER),
    # Direct3D 12's heaps over a program's own memory (tests/d3d12-existing-heap-probe.c, Wine patch 2425), in the title,
    # the only process whose memory the GPU reaches: GPU copies both ways, through the program's protection changes.
    ('d3d12-existing-heap64-title', ['--exe', 'd3d12-existing-heap-probe.exe', '--marker', 'PS5_PROTON_D3D12_EXISTING_HEAP',
                                     '--env', 'WINE_PS5_TITLE_PROCESS=d3d12-existing-heap-probe.exe'] + LAUNCHER),
    # A texture shared between a Direct3D 9Ex and a Direct3D 11 device of one process (tests/d3d9ex-d3d11-share-probe.c): the
    # measurement before a DXVK change; PASS, NOSHARE and FAIL are all recorded, none is a gate case yet.
    ('d3d9ex-d3d11-share64', ['--exe', 'd3d9ex-d3d11-share-probe.exe', '--marker', 'PS5_PROTON_D3D9_D3D11_SHARE'] + LAUNCHER),
    # The on-screen keyboard's text reaching a game window, no person (tests/osk-typed-probe.c): the title types the fixed line
    # 20 s in (WINE_PS5_OSK_TEST=type, native/proton_wine.cpp), in a game in the title and in one in a helper. And the system
    # dialog itself: WINE_PS5_OSK_TEST=open shows it and ends it 3 s later; read the title log for "keyboard opened" and
    # "keyboard ended by=title" (no marker: nothing in the game can see the dialog).
    ('osk-typed64-title', ['--exe', 'osk-typed-probe.exe', '--marker', 'PS5_PROTON_OSK_TYPED',
                           '--env', 'WINE_PS5_TITLE_PROCESS=osk-typed-probe.exe', '--env', 'WINE_PS5_OSK_TEST=type'] + LAUNCHER),
    ('osk-typed64-helper', ['--exe', 'osk-typed-probe.exe', '--marker', 'PS5_PROTON_OSK_TYPED',
                            '--env', 'WINE_PS5_OSK_TEST=type'] + LAUNCHER),
    ('osk-open64', ['--exe', 'window-probe.exe', '--marker', 'PS5_PROTON_WINDOW_PROBE', '--env', 'WINE_PS5_OSK_TEST=open'] + LAUNCHER),
    ('d3d11', ['--exe', 'd3d11-probe.exe', '--marker', 'PS5_PROTON_D3D11_PROBE']),
    # The broker's fast copy (src/pw_speed_defaults.h), on by default since Alpha 7: the frame a Direct3D 11 program presents
    # reaches the screen. Its capture reads what was scanned out and must show the frame, not black (w3-ab-b-pro-2026-11-01).
    ('fast-copy-capture', ['--exe', 'd3d11-probe.exe', '--marker', 'PS5_PROTON_D3D11_PROBE', '--env', 'PROTON_GPU_FAST_COPY=1',
                           '--env', 'PS5_PROTON_PROBE_FRAMES=1200', '--pad-script', 'tests/pad-scripts/fast-copy-capture.txt',
                           '--expect-captures-lit', '90']),
    ('opengl', ['--exe', 'gl-vertex-probe.exe', '--marker', 'PS5_PROTON_GL_VERTEX_PROBE', '--env', 'WINE_PS5_OPENGL=1']),
    ('children', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_PROCESS_PROBE']),
    ('payload-eight', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_PROCESS_PROBE',
                       '--args=--payload-eight', '--watchdog', '300']),
    ('payload-memory', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP',
                        '--args=--payload-memory', '--watchdog', '300'] + LAUNCHER),
    ('payload-vm', ['--exe', 'vm-protect-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP'] + LAUNCHER),
    # The 4 KiB MMU's truth (patch 1134, docs/MMU4K.md section 12), in a helper: a store into a read-only 4 KiB page
    # beside a writable one faults at the store before it lands, once a page; #UD and #DE in a run; a straddling store
    # faults at the read-only page's first byte. 32-bit code keeps 1122's step, so only the store truth there.
    ('vm-truth32', ['--exe', 'vm-protect32-probe.exe', '--marker', 'PS5_PROTON_VM_PROTECT_TRUTH',
                    '--args=--truth'] + LAUNCHER),
    ('vm-truth64', ['--exe', 'vm-protect-probe.exe', '--marker', 'PS5_PROTON_VM_PROTECT_TRUTH',
                    '--args=--truth'] + LAUNCHER),
    ('descendants', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_PROCESS_PROBE', '--args=--descendants']),
    ('file-budget', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_PROCESS_PROBE', '--args=--file-budget',
                     '--watchdog', '300']),
    # A console program a program without a console starts, and one with CREATE_NO_WINDOW, get no console host
    # (a helper's place); CREATE_NEW_CONSOLE still gets one (patch 0935).
    ('console-children', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_PROCESS_PROBE',
                          '--args=--console-children']),
    ('dns', ['--exe', 'dns-probe.exe', '--marker', 'PS5_PROTON_DNS_PROBE']),
    ('tls', ['--exe', 'tls-probe.exe', '--marker', 'PS5_PROTON_TLS_PROBE']),
    ('library-entry', ['--profile', '1', '--seconds', '60']),
    # In the launcher layout the program is a helper: its verdict is its exit code, which the title reports.
    ('launcher-children', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP'] + LAUNCHER),
    ('launcher-render', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP',
                         '--args=--graphics-broker', '--watchdog', '240'] + LAUNCHER),
    ('launcher-file-budget', ['--exe', 'process-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP',
                              '--args=--file-budget', '--watchdog', '300'] + LAUNCHER),
    ('launcher-tls', []),
    # The installer slice's fast kinds (Inno Setup, a Steam install script) in an owned throwaway prefix, each check against
    # the PC's Wine (evidence/installer-slice/host-*.json); fixtures: tools/build-installer-fixtures.sh.
    ('installers-fast', ['console', 'inno', 'steam']),
    # A GDI window in a helper: its frames reach the screen through the title's compositor, and the touchpad's
    # pointer and clicks (a pad script) reach it through the desktop.
    ('launcher-desktop', []),
    ('desktop-session', []),
    # The desktop session with no program, ended by the run's budget: the title's stop logs it off and the owner
    # restarts cleanly. A close to the foreground window reached one of Explorer's own, and the title ended the
    # session by SIGKILL 10 s later (browser-desktop-2026-10-18-01 and -02; patch 1214).
    ('desktop-stop', ['--path', r'C:\windows\system32\mihawk.exe', '--seconds', '30', '--env', 'MIHAWK_DESKTOP=1'] + LAUNCHER),
    # The Mihawk desktop as the shell's folder view: input drives it, the shell's behaviour and its pictures are checked (patches 1100 to 1103).
    ('desktop-view', []),
    # MihawkBrowser starts, shows its window, loads a page that needs no network (about:blank, which still takes a renderer
    # process and CEF's whole start-up, V8's 1 TiB reservation included) and closes. alpha-2 shipped a browser that never
    # started on the standard PS5 because no case started it.
    ('mihawk-browser', ['--script', 'load about:blank; wait 5000; exit', '--seconds', '90']),
    # Game mode: a full-screen window flipped directly, Explorer unloaded while it holds the screen and restored.
    ('game-mode64', ['--api', 'opengl']),
    ('game-mode-dxvk64', ['--api', 'dxvk']),
    # The title's guard over the console's system memory (src/pw_memory_guard.h): a window in a helper holds 300 MiB,
    # which takes the census well past a limit lowered to 450 MiB (the end mark out of reach), and passes when the
    # title asks it to close.
    ('memory-guard', ['--exe', 'memory-guard-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP', '--watchdog', '200',
                      '--args=--hold=300', '--env', 'PROTON_SYSTEM_MEMORY_LIMIT=450',
                      '--env', 'WINE_PS5_PAYLOAD_DMEM=0',
                      '--env', 'PROTON_SYSTEM_MEMORY_END=2000'] + LAUNCHER),
    # Not in the gate: runs made on request (klog/salvage-2026-10-14/review/plan.md), each approved on its own.
    # A 256 MiB section in direct memory, its 128 chunks given back; then the memory helper stalled before the third
    # chunk, which must mark it dead once after 5 s and end the program, never wedge the title.
    ('section-dmem', ['--exe', 'section-memory-probe.exe', '--marker', 'PS5_PROTON_SECTION_MEMORY',
                      '--env', 'PS5_PROBE_SECTION_MIB=256', '--env', 'PROTON_STOP_TERMINATE=0', '--seconds', '60',
                      '--watchdog', '120', '--expect-direct-section'] + LAUNCHER),
    ('memory-stall', ['--exe', 'section-memory-probe.exe', '--marker', 'PS5_PROTON_SECTION_MEMORY',
                      '--env', 'PS5_PROBE_SECTION_MIB=256', '--env', 'PROTON_MEMORY_TEST_STALL=3',
                      '--env', 'PROTON_STOP_TERMINATE=0', '--seconds', '60', '--watchdog', '120'] + LAUNCHER),
    # Two threads storing 100,000 times into one host page while a third re-protects it every 100 us (patches 1122,
    # 1134), and the 4 KiB MMU's speed (bound 0.6 ms for 1,024 stores; with --env WINE_PS5_MMU4K=0 recorded unbound)
    # and its re-arm race (a page made read-only takes no store before its access violation, 100,000 cycles).
    ('payload-vm-contention', ['--exe', 'vm-protect-probe.exe', '--marker', 'PS5_PROTON_OWNER_BOOTSTRAP',
                               '--args=--contention', '--env', 'PROTON_STOP_TERMINATE=0', '--watchdog', '120'] + LAUNCHER),
    ('vm-speed64', ['--exe', 'vm-protect-probe.exe', '--marker', 'PS5_PROTON_VM_PROTECT_SPEED', '--args=--speed',
                    '--env', 'PROTON_STOP_TERMINATE=0', '--watchdog', '120'] + LAUNCHER),
    ('vm-rearm64', ['--exe', 'vm-protect-probe.exe', '--marker', 'PS5_PROTON_VM_PROTECT_REARM', '--args=--rearm',
                    '--env', 'PROTON_STOP_TERMINATE=0', '--watchdog', '120'] + LAUNCHER),
    # An assertion under the virtual mutex ends its helper with STATUS_ASSERTION_FAILURE (wine/ps5/pw_wine_dl_libc.c).
    ('assert-probe', ['--exe', 'section-memory-probe.exe', '--env', 'WINE_PS5_ASSERT_PROBE=1',
                      '--env', 'PS5_PROBE_SECTION_MIB=512', '--env', 'PROTON_STOP_TERMINATE=0', '--seconds', '60',
                      '--watchdog', '120'] + LAUNCHER),
    # A game that ignores the close request is ended by SIGKILL to its helpers (src/pw_hang_breaker.h): with no GPU
    # client, with one presenting, and by the hang breaker on a frame stall with the census taken as frozen. They join
    # the gate once each has passed on the console.
    ('end-hang64', ['--exe', 'hang-probe.exe', '--args=--hold=90', '--marker', 'PS5_PROTON_HANG_PROBE', '--seconds', '20',
                    '--watchdog', '120', '--expect-ended'] + LAUNCHER),
    ('end-present64', ['--exe', 'gl-present-probe.exe', '--args=--hold=120', '--marker', 'PS5_PROTON_GL_PRESENT_PROBE',
                       '--seconds', '20', '--watchdog', '150', '--expect-ended'] + LAUNCHER),
    ('breaker64', ['--exe', 'gl-present-probe.exe', '--args=--hold=120 --pause=25', '--marker',
                   'PS5_PROTON_GL_PRESENT_PROBE', '--seconds', '90', '--watchdog', '150', '--expect-ended',
                   # No stop request before the breaker's verdict: the stop path would end the game first.
                   '--exit-stall', '100',
                   '--env', 'PROTON_HANG_STALL_MS=10000', '--env', 'PROTON_HANG_BREAKER_TEST_CENSUS_FREEZE_S=20'] + LAUNCHER),
    # The broker wedge (src/pw_hang_breaker.h): the GPU broker holds its client lock 40 s once, 10 s into the probe's
    # presents (graphics patch 0017's test hook); at 20 samples held the flight recorder ends the game, the call named.
    # By design the probe's held submit gets no answer, so its GPU client gives the broker up at 15 s and stamps the
    # helper's GPU reason (graphics patch 0017), whose 5 s grace ends near the same moment: either may end the helper
    # (run-console.py backstop_allowed). The title returns only because the hold ends: until then the helper's GPU
    # close waits on that lock (pw_ps5_wait_children says so every 30 s). It joined the gate after its first console pass
    # (l2-broker-wedge-2026-10-16-01).
    ('broker-wedge64', ['--exe', 'gl-present-probe.exe', '--args=--hold=120', '--marker', 'PS5_PROTON_GL_PRESENT_PROBE',
                        '--seconds', '90', '--watchdog', '150', '--expect-ended', '--exit-stall', '100',
                        '--env', 'PROTON_TEST_BROKER_HOLD_S=40'] + LAUNCHER),
    # A lost device: the broker answers the submission after 100 with a failure, as a failed one, and stays wedged, and
    # the program sees its device lost. An answered call is no lost broker: nothing stamps the GPU reason (graphics
    # patch 0017), no backstop may end the helper, the broker records test_submit_failure and the title comes back.
    ('gpu-loss', ['--exe', 'd3d11-probe.exe', '--marker', 'PS5_PROTON_D3D11_PROBE',
                  '--env', 'PROTON_GPU_TEST_FAIL_SUBMIT=100', '--watchdog', '120'] + LAUNCHER),
]
# Run only when named in --only, one per run (docs/CONSOLE_QUEUE.md: one risky run per boot, each on request).
ON_REQUEST = {'section-dmem', 'memory-stall', 'payload-vm-contention', 'vm-speed64', 'vm-rearm64', 'assert-probe',
              'end-hang64', 'end-present64', 'breaker64', 'gpu-loss'}


# The gate (tools/gate.py): what every commit of code needs the console to pass first. A new required check is added
# here, and from then on no code is committed without it.
GATE_CASES = ['shell32', 'shell64', 'work-area32', 'work-area64', 'wallpaper32', 'wallpaper64', 'cursor-scheme32', 'cursor-scheme64', 'theme-parts32', 'theme-parts64', 'monospace32', 'monospace64', 'gpu-window32', 'gpu-window64', 'dxvk-window32', 'dxvk-window64', 'host-dmem64', 'host-dmem-fallback64', 'window-compose32', 'window-compose64', 'window-effects32', 'window-effects64', 'window-snapshot32', 'window-snapshot64', 'initial-env32', 'initial-env64', 'locale64', 'appearance32', 'appearance64', 'theme-notify32', 'theme-notify64', 'proton-ui32', 'proton-ui64', 'memory-classes', 'graphics-teardown', 'title-games', 'launcher-pair', 'chromium-game', 'dnsapi32', 'dnsapi64', 'mf-video32', 'mf-video64', 'network-adapters32', 'network-adapters64', 'socket-options32', 'socket-options64', 'late-library', 'large-shared-section', 'virtual-query32', 'virtual-query64', 'image-protect32', 'image-protect64', 'shared-section32', 'shared-section64', 'suspend-context64', 'kill-convert', 'stack-limit64', 'remote-memory64', 'system-memory32', 'system-memory64', 'file-write32', 'file-write64', 'relative-dir32', 'relative-dir64', 'umip32', 'umip64', 'manual-stdout', 'redirected-stdout', 'prefix-fresh64', 'prefix-fresh32',
              'pe64', 'payload-eight', 'payload-memory', 'payload-vm', 'descendants', 'launcher-render', 'launcher-tls',
              'sync64', 'shell-open64',
              # A write tracker's truth on 16 KiB host pages (patch 1134): no write reported that did not happen,
              # each one faulting at the store (docs/MMU4K.md I1, I2).
              'vm-truth32', 'vm-truth64',
              # The touchpad's pointer and clicks reaching a window in a helper: the person at the console found
              # the pointer frozen in the triangle test and over a game launcher's desktop (2026-10-07), where it had worked.
              'launcher-desktop', 'desktop-overlap', 'desktop-session', 'desktop-stop', 'desktop-view', 'mihawk-browser', 'game-mode64', 'game-mode-dxvk64', 'mouse-controls',
              'controls-menu', 'launcher-ended', 'games-autodetect', 'detailed-logs', 'setup-tour',
              'steam-guide-pages', 'steam-guide-screens',
              # A game whose helpers fill the console's system memory is closed before the console swaps itself to a
              # stop: a game launcher's did that on 2026-10-07.
              'memory-guard',
              # A 32-bit program under WoW64, natively in compatibility mode (docs/IA32_DESIGN.md, M3): every change
              # to the FS switch, the signal entry or the image placement it rests on is checked here.
              'pe32', 'fixed-base32', 'native-signals', 'telemetry-teardown', 'wow64', 'vk32', 'gl32',
              # A helper Wine can no longer end ends itself (patch 1135): a run of 2026-10-15's did not, and the
              # console froze.
              'doom-backstop',
              # A broker that stops answering ends the game, and the title comes back (graphics 0017, src/pw_hang_breaker.h):
              # a run of 2026-10-15's never answered and froze the console; l2-broker-wedge-2026-10-16-01 passed.
              'broker-wedge64',
              # HID's nested INPUT32 must be widened too (0955); otherwise device reports become message zero
              # and a normal PeekMessage loop stalls even when texture uploads and SwapBuffers succeed.
              'input32', 'gl-upload32', 'osk-typed64-title', 'osk-typed64-helper', 'osk-open64', 'many-files64', 'log-flood64-title', 'd3d12-existing-heap64-title', 'parent-query64-title', 'missing-import64-title', 'fast-copy-capture']


# Grouped launches (--grouped). A short probe spends about 1 s of its 14 s launch on its own checks
# (gate-hang-final-2026-10-16-01: Wine's boot about 6 s, the return to the launcher and its close the rest), so the
# probes of a group run in one launch, each still its own Windows process, in CASES order. Each is judged on its own:
# its exit status, its marker's first verdict line in its window of the helper logs, and no more than 15 s from that
# verdict to its end (run-console.py's exit stall); and on everything run-console.py checks of the launch they shared,
# so a launch that fails fails each of them. A group holds only cases whose run is --exe, --marker, --args and
# --watchdog (sequence_entry). A launch of its own stays for what tests the launch, the exit, a prefix's creation, an
# environment, a layout or a runner of its own, or a verdict rule of its own; the gate's other cases still check the
# whole lifecycle, and so does each grouped launch.
GROUPS = {
    # No graphics. Each probe puts back what it changes in the probe prefix (work area, wallpaper, theme, pointer scheme).
    'probes': ['work-area32', 'work-area64', 'wallpaper32', 'wallpaper64', 'theme-notify32', 'theme-notify64',
               'cursor-scheme32', 'cursor-scheme64', 'theme-parts32', 'theme-parts64', 'monospace32', 'monospace64',
               'proton-ui32', 'proton-ui64', 'dnsapi32', 'dnsapi64', 'mf-video32', 'mf-video64',
               'network-adapters32', 'network-adapters64',
               'socket-options32', 'socket-options64', 'virtual-query32', 'virtual-query64', 'file-write32',
               'file-write64', 'relative-dir32', 'relative-dir64', 'system-memory32', 'system-memory64', 'image-protect32', 'image-protect64', 'shared-section32', 'shared-section64',
               'suspend-context64', 'kill-convert', 'stack-limit64', 'remote-memory64', 'late-library', 'pe64', 'pe32', 'fixed-base32', 'wow64', 'input32',
               'payload-vm',
               'vm-truth32', 'vm-truth64', 'descendants', 'payload-eight', 'memory-classes', 'sync64',
               'shell-open64'],
    # payload-memory runs in a launch of its own: its 4 GiB on top of a group's helpers took a fresh Pro's page tables
    # to 1,526 free of 9,216 and the guard ended the group (gpu2m-pro-gate-2026-10-29-01).
    # The GPU broker's clients one after another, as a launcher's games are.
    'graphics': ['graphics-teardown', 'vk32', 'gl32', 'gl-upload32', 'launcher-render'],
}
# --session: the groups' cases in the persistent test session (tests/wt-session.c, tools/run-wine-session.py), 4 at a
# time, each its own Windows process with its own output; owner's request 2026-10-25. A case that changes or reads
# state every process shares (the display and its work area, the desktop and wallpaper, the theme, the pointer scheme,
# fonts, input and focus, the shell), or that measures or fills memory, runs alone, by the sweep's rule
# (tests/wine-tests-serial-ps5.json); so does every GPU broker client, as a launcher's games are.
SESSION_JOBS = 4
SESSION_SERIAL = {'work-area32', 'work-area64', 'wallpaper32', 'wallpaper64', 'theme-notify32', 'theme-notify64',
                  'cursor-scheme32', 'cursor-scheme64', 'theme-parts32', 'theme-parts64', 'monospace32', 'monospace64',
                  'proton-ui32', 'proton-ui64', 'input32', 'shell-open64', 'system-memory32', 'system-memory64',
                  'payload-eight', 'memory-classes', 'kill-convert'} | set(GROUPS['graphics'])
OWNER = 'PS5_PROTON_OWNER_BOOTSTRAP'
EXIT_STALL = 15   # run-console.py --exit-stall's default
SEQUENCE_RECORD = re.compile(r':debugstr:OutputDebugStringA "PS5_PROTON_GATE_SEQUENCE (begin|end) '
                             r'case=([\w-]+)(?: status=([\w-]+))?')
WINE_TIME = re.compile(r'^(\d+\.\d{3}):[\da-f]{4}:', re.M)
WINE_RECORD = re.compile(r'(\d+)\.(\d{3}):[\da-f]{4,}:[\da-f]{4,}:')


def wine_records(text):
    """Each line's Wine debug record, (time, the line from its timestamp on), wherever in the line it starts: native
    text written in pieces can land in front of a record another thread wrote whole (gate-release-2026-10-19-01's
    kill-convert PASS behind "wine-ps5: kill thread 18bee"). Text that ends in digits runs them into the record's
    seconds ("18f24" + "2340.735"), so a record that does not start its line takes the reading of its seconds nearest
    the record before it. Text before a record is never read as the record's."""
    last = None
    for line in text.splitlines():
        if not (m := WINE_RECORD.search(line)):
            continue
        seconds, millis = m.groups()
        readings = [float(f'{seconds[i:]}.{millis}') for i in range(len(seconds))]
        last = readings[0] if not m.start() or last is None else min(readings, key=lambda at: abs(at - last))
        yield last, line[m.start():]


def sequence_entry(name, case):
    """A grouped case's command line, marker and budget in seconds, from its run-console.py arguments: its watchdog
    (run-console.py's default 150 s) less the 25 s run-console.py keeps to stop a launch through the title."""
    found = {'--exe': None, '--marker': None, '--args': None, '--watchdog': '150'}
    rest = list(case)
    while rest:
        flag = rest.pop(0)
        key, equals, value = flag.partition('=')
        if key in found and (equals or rest):
            found[key] = value if equals else rest.pop(0)
        elif flag == '--env' and rest[:1] == ['PROTON_LAYOUT=launcher']:   # the default layout
            rest.pop(0)
        else:
            raise ValueError(f'{name} needs a launch of its own: {flag}')
    if not found['--exe'] or not found['--marker']:
        raise ValueError(f'{name} needs a launch of its own: no --exe and --marker')
    command = 'C:\\' + found['--exe'] + (' ' + found['--args'] if found['--args'] else '')
    return command, found['--marker'], int(float(found['--watchdog'])) - 25


def sequence_outcomes(run, members):
    """Each member's own record, from the helper logs of its launch (klog/<run>-wine-child-<pid>.log): the sequence's
    begin and end lines for it; its marker's first PASS or FAIL line between them, in pid order as run-console.py reads
    a launch's logs (the owner's marker is its exit status, as the title's verdict on a program is); and the helpers
    that started in its window. members: (case, marker)."""
    logs = {int(path.stem.rsplit('-', 1)[1]): path.read_text(errors='replace')
            for path in (ROOT / 'klog').glob(run + '-wine-child-*.log')}
    records = {pid: list(wine_records(text)) for pid, text in logs.items()}
    windows = {}
    for found in records.values():
        for at, record in found:
            if m := SEQUENCE_RECORD.search(record):
                what, case, status = m.groups()
                windows.setdefault(case, {})[what] = at
                if what == 'end':
                    windows[case]['status'] = status
    starts = {pid: float(m.group(1)) for pid, text in logs.items() if (m := WINE_TIME.search(text))}
    outcomes = {}
    for name, marker in members:
        window = windows.get(name, {})
        begin, end, status = window.get('begin'), window.get('end'), window.get('status')
        verdict = verdict_at = None
        if end is not None and marker == OWNER:
            verdict = 'PASS' if status == '0' else 'FAIL'
        elif end is not None:
            for pid in sorted(records):
                for at, record in records[pid]:
                    if (f'{marker} PASS' in record or f'{marker} FAIL' in record) and begin <= at <= end:
                        verdict, verdict_at = 'PASS' if f'{marker} PASS' in record else 'FAIL', at
                        break
                if verdict:
                    break
        stall = verdict_at is not None and end - verdict_at > EXIT_STALL
        outcomes[name] = {'status': status, 'probe_verdict': verdict, 'exit_stall': stall,
                          'seconds': round(end - begin, 1) if end is not None else None,
                          'helpers': sorted(pid for pid, at in starts.items() if end is not None and begin <= at <= end),
                          'probe_passed': status == '0' and verdict == 'PASS' and not stall}
    return outcomes


def session_outcomes(run, members, entries):
    """The groups' cases run in one persistent session: each case's exit status and its marker's first verdict line in
    its own output (the owner's marker is its exit status, as in a grouped launch). members: (case, case args)."""
    spec = importlib.util.spec_from_file_location('run_wine_session', ROOT / 'tools/run-wine-session.py')
    wts = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(wts)
    support = runpy.run_path(str(ROOT / 'tools/run-prefix-test.py'))
    settings = support['ps5_console'].load_settings()
    procs = lambda: ' count=0 ' not in support['ps5_console'].ps5vkctl_command(settings, 'procs', timeout=5) + ' '
    queued = [{'id': session_id(n), 'serial': n in SESSION_SERIAL, 'timeout': entries[n][2], 'command': entries[n][0],
               'attempts': 0} for n, _ in members]
    case_of = {session_id(n): n for n, _ in members}
    got = {}
    try:
        # the launch's verdict comes from the helpers' OutputDebugString records, which err+all alone turns off
        wts.session(queued, None, SESSION_JOBS, run, ['WINEDEBUG=err+all,warn+debugstr,+timestamp,+pid', *PLAYER_DEFAULTS],
                    support['connect'], settings,
                    procs=procs, on_result=lambda ident, code, ms, how, text: got.__setitem__(case_of[ident], (code, ms, how, text)),
                    run_name=run)
    except SystemExit as error:
        print(f'  session: {error}', flush=True)
    # A probe whose verdict went only through buffered stdio or OutputDebugString is read from the helpers' logs, by the
    # Windows process id wt-session appends to its output (dnsapi: printf, then ExitProcess without a flush).
    debug = {}
    for path in (ROOT / 'klog').glob(run + '-wine-child-*.log'):
        for _, record in wine_records(path.read_text(errors='replace')):
            if (m := re.match(r'[\d.]+:([0-9a-f]+):[0-9a-f]+:warn:debugstr:OutputDebugStringA "(.*)', record)):
                debug.setdefault(m.group(1), []).append(m.group(2))
    outcomes = {}
    for name, _ in members:
        code, ms, how, text = got.get(name, (None, None, 'no result', ''))
        pid = re.search(r'^wt-session: process ([0-9a-f]+)$', text, re.M)
        outcomes[name] = session_verdict(entries[name][1], code, ms, how,
                                         text + '\n' + '\n'.join(debug.get(pid.group(1), []) if pid else []))
    if (gone := pipes_gone(run)):
        for outcome in outcomes.values():
            outcome.update(pipe_memory_exhausted=gone, probe_passed=False)
    return outcomes


def tally(cases):
    """(passed, skipped, failed) of a battery's cases. A case whose probe passed every check it could judge but skipped
    one for a reason outside the console (environment_skip: dnsapi's "resolver refused TCP") is neither: it is marked
    skipped, with its reason, and does not hold the gate; a failure always does."""
    skipped = []
    for case in cases:
        if case.get('passed') and case.get('environment_skip'):
            case['passed'], case['skipped'] = False, case['environment_skip']
        if case.get('skipped'):
            skipped.append(f"{case['case']}: {case['skipped']}")
    passed = sum(bool(c.get('passed')) for c in cases)
    return passed, skipped, len(cases) - passed - len(skipped)


def session_id(name):
    """A case's name in the session: a 32-bit and a 64-bit case of one probe are twins there (wt-session's group is what
    follows the first '-'), so they never run together; they share fixed paths (file-write32 and file-write64 wrote one
    file at once and both failed, gate-b5-session-2026-10-25-02)."""
    base, bits = re.match(r'(.*?)(32|64)?$', name).groups()
    return f'{bits}-{base}' if bits and base else f'x-{name}'


def session_verdict(marker, code, ms, how, text):
    """One case of the session: its marker's first PASS or FAIL line in its own output, or, for the owner's marker,
    its exit status; it passes when it exited by itself with 0 and PASS."""
    verdict, skip = None, None
    for line in text.splitlines():
        if f'{marker} PASS' in line or f'{marker} FAIL' in line:
            verdict = 'PASS' if f'{marker} PASS' in line else 'FAIL'
            # A check the probe could not judge for a reason outside the console (dnsapi: the network's resolver did
            # not answer over TCP) is kept as such, beside the verdict, never folded into it.
            if (m := re.search(r'skipped=([1-9]\d*)', line)):
                skip = f'{m.group(1)} check(s) skipped for the environment: ' + line.strip()[:160]
            break
    if marker == OWNER and code is not None:
        verdict = 'PASS' if code == 0 else 'FAIL'
    outcome = {'status': None if code is None else str(code), 'probe_verdict': verdict, 'how': how,
               'seconds': None if ms is None else round(ms / 1000, 1),
               'probe_passed': code == 0 and how == 'exited' and verdict == 'PASS'}
    if skip:
        outcome['environment_skip'] = skip
    return outcome


# The console's pipe memory ran out (kern.ipc.maxpipekva): every pipe made after it fails, and with them the helpers'
# memory service, so every ReadProcessMemory and WriteProcessMemory, until the console restarts (2026-10-26: every
# remote-memory step failed, ntdll virtual's and kernel32 loader's reads of a child with them). A run that saw it says
# nothing about the code: it fails, saying the console needs a restart.
PIPES_GONE = 'kern.ipc.maxpipekva exceeded'


def pipes_gone(run):
    """How many times the run's klog says the console's pipe memory ran out."""
    log = ROOT / 'klog' / (run + '.log')
    return log.read_text(errors='replace').count(PIPES_GONE) if log.exists() else 0


def launch_outcome(run, result, started):
    """What a launch's runner and its records say, as every case's outcome holds it."""
    outcome = {'runner_status': result.returncode, 'seconds': round(time.monotonic() - started, 1)}
    path = ROOT / 'evidence' / (run + '.json')
    if path.exists():
        run_record = json.loads(path.read_text())
        outcome.update({k: run_record.get(k) for k in ('result', 'probe_verdict', 'returned_to_launcher',
                                                     'exit_stall', 'passed')})
        outcome['run_seconds'] = run_record.get('seconds')
        outcome['launch_readiness'] = run_record.get('launch_readiness')
        outcome['stages'] = run_record.get('stages')
        if isinstance(outcome['run_seconds'], (int, float)):
            outcome['runner_overhead_seconds'] = round(max(0, outcome['seconds'] - outcome['run_seconds']), 1)
    log = ROOT / 'klog' / (run + '.log')
    if log.exists():
        text = log.read_text(errors='replace')
        outcome['layout'] = (re.findall(r'Wine: layout (\w+)', text) or [None])[-1]
        outcome['fault_records'] = len(re.findall(r'A user thread receives a fatal signal|GPU_FAULT|gpu fault',
                                                  text, re.I))
        outcome['helpers'] = len(re.findall(r'wine-ps5: child ready native=', text))
        outcome['protocol_errors'] = len(re.findall(r'Protocol error: process', text))
        outcome['pipe_memory_exhausted'] = text.count(PIPES_GONE)
    # Wine's search for room (try_map_free_area) goes on to the next window when a reservation is refused (EEXIST) and prints this
    # line, and stops, on any other answer. Patch 1231's first version passed the kernel's ENOMEM on for a hold of its own in the
    # range: every search that met one failed (libcef's 1 TiB sandbox reservation, MihawkBrowser on both consoles, alpha-2). A line
    # of it in any helper's log of any launch fails the launch.
    outcome['search_errors'] = sum(len(re.findall(r'err:virtual:try_map_free_area mmap\(\) error', child.read_text(errors='replace')))
                                   for child in (ROOT / 'klog').glob(run + '-wine-child-*.log'))
    outcome['passed'] = result.returncode == 0 and bool(outcome.get('passed')) and not outcome.get('fault_records') and \
        not outcome.get('protocol_errors') and not outcome['search_errors'] and not outcome.get('pipe_memory_exhausted')
    return outcome


def runner_of(name):
    """The tools/ script a case's own launch runs (tools/gate_quick.py maps a changed runner to its cases)."""
    return ('run-shell.py' if name in ('shell32', 'shell64') else
            'run-prefix-baseline.py' if name.startswith('prefix-fresh') else
            'run-appearance.py' if name.startswith('appearance') else
            'run-launcher.py' if name in ('controls-menu', 'setup-tour', 'steam-guide-pages') else
            'run-launcher-ended.py' if name == 'launcher-ended' else
            'run-games-autodetect.py' if name == 'games-autodetect' else
            'run-detailed-logs.py' if name == 'detailed-logs' else
            'run-mouse-controls.py' if name == 'mouse-controls' else
            'run-window-compose.py' if name in ('window-compose32','window-compose64','gpu-window32','gpu-window64','dxvk-window32','dxvk-window64','host-dmem64','host-dmem-fallback64') else
            'run-window-effects.py' if name in ('window-effects32','window-effects64') else
            'run-desktop-overlap.py' if name == 'desktop-overlap' else
            'run-desktop-session.py' if name == 'desktop-session' else
            'run-desktop-view.py' if name == 'desktop-view' else
            'run-browser.py' if name == 'mihawk-browser' else
            'run-game-mode.py' if name.startswith('game-mode') else
            'run-desktop-input.py' if name == 'launcher-desktop' else
            'run-tls-fixture.py' if name == 'launcher-tls' else
            'run-installer-slice.py' if name == 'installers-fast' else
            'run-wine-slice.py' if name.startswith('wine-') else 'run-console.py')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--name', required=True, help='the battery run: evidence/<name>.json, cases <name>-<case>')
    parser.add_argument('--only', nargs='*', default=[], help='run only these cases')
    parser.add_argument('--skip', nargs='*', default=[], help='leave these cases out')
    parser.add_argument('--gate', action='store_true', help='run the gate cases (GATE_CASES), as tools/gate.py does')
    parser.add_argument('--grouped', action='store_true', help="run each group's cases in one launch (GROUPS)")
    parser.add_argument('--grouped-launches', action='store_true',
                        help='the groups in one launch each, their cases one after another (before 2026-10-25); '
                             '--grouped alone runs them in the persistent session')
    parser.add_argument('--session', action='store_true',
                        help="with --grouped: run the groups' cases in one persistent session, 4 at a time (SESSION_SERIAL alone)")
    parser.add_argument('--alone', nargs='*', default=[],
                        help='with --grouped: these cases leave their group for a launch of their own (tools/gate_quick.py: '
                             'a case that holds GiBs of memory never shares a session)')
    args = parser.parse_args()
    # the owner's choice (2026-10-25): grouped cases run in the persistent session unless --grouped-launches asks for a
    # launch per group, as before
    if args.grouped_launches:
        args.grouped, args.session = True, False
    elif args.grouped:
        args.session = True
    if args.gate:
        if args.only or args.skip:
            parser.error('the gate runs all of its cases')
        args.only = list(GATE_CASES)
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.name):
        parser.error('use a plain run name')
    known = {name for name, _ in CASES}
    for name in args.only + args.skip + args.alone:
        if name not in known:
            parser.error(f'unknown case {name}')
    if len(ON_REQUEST.intersection(args.only)) > 1:
        parser.error('one on-request case per run (docs/CONSOLE_QUEUE.md: one risky run per boot)')
    evidence = ROOT / 'evidence' / (args.name + '.json')
    if evidence.exists():
        parser.error('use a new battery name')
    selected = [(name, case) for name, case in CASES
                if (name in args.only if args.only else name not in ON_REQUEST) and name not in args.skip]
    # What runs, in order: each case its own launch, or with --grouped a group's selected cases in one.
    group_of = {name: group for group, names in GROUPS.items() for name in names} if args.grouped else {}
    if args.session:
        if not args.grouped:
            parser.error('--session goes with --grouped')
        group_of = {name: 'session' for name in group_of}   # every group's cases in the one session
    for name in args.alone:
        group_of.pop(name, None)
    units = []
    for name, case in selected:
        if name not in group_of:
            units.append((name, [(name, case)]))
        elif group_of[name] not in [unit for unit, _ in units]:
            units.append((group_of[name], [(n, c) for n, c in selected if group_of.get(n) == group_of[name]]))
    try:
        entries = {name: sequence_entry(name, case) for unit, members in units if unit in GROUPS or unit == 'session'
                   for name, case in members}
    except ValueError as error:
        parser.error(str(error))
    expected = [name for _, members in units for name, _ in members]
    stop_file = ROOT / '.deps' / (args.name + '.stop')
    batch_started = time.monotonic()
    record = {'battery': args.name, 'expected_cases': expected,
              'revision': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT,
              text=True).strip(), 'dirty': bool(subprocess.check_output(['git', 'status', '--porcelain', '--',
              'native', 'wine', 'src', 'ps5/src', 'tests'], cwd=ROOT, text=True).strip()), 'cases': []}
    index = 0
    for unit, members in units:
        name, case = members[0]
        if stop_file.exists():
            stop_file.unlink()
            record['stop_reason'] = 'requested at a case boundary'
            break
        refused = console_guard.refusal(console_guard.load(console_guard.state_path(ROOT)))
        if refused:
            record['stop_reason'] = refused
            print(f'== Stopped before {name}: {refused}', flush=True)
            break
        run = f'{args.name}-{unit}'
        started = time.monotonic()
        if unit == 'session':
            print(f'== Tests {index + 1}-{index + len(members)}/{len(selected)}: one session, {SESSION_JOBS} at a time ({run})',
                  flush=True)
            own = session_outcomes(run, members, entries)
            launch = launch_outcome(run, subprocess.CompletedProcess([], 0), started)
            for member, _ in members:
                index += 1
                outcome = {**launch, 'case': member, 'run': run, 'group': 'session', 'launch_passed': launch['passed'],
                           'launch_seconds': launch['seconds'], **own[member]}
                outcome['passed'] = bool(launch['passed'] and own[member]['probe_passed'])
                record['cases'].append(outcome)
                print(f'== Test {index}/{len(selected)}: {member} {"PASS" if outcome["passed"] else "FAIL"} '
                      f'({outcome["seconds"]} s in the session)', flush=True)
            evidence.write_text(json.dumps(record, indent=2) + '\n')
            if launch.get('result') not in ('finished', None) and not launch.get('returned_to_launcher'):
                record['stopped_after'] = unit
                break
            continue
        if unit in GROUPS:
            sequence = ROOT / '.deps' / (run + '-sequence.txt')
            sequence.write_text(''.join(f'{n}\t{entries[n][2]}\t{entries[n][0]}\n' for n, _ in members))
            print(f'== Tests {index + 1}-{index + len(members)}/{len(selected)}: {unit}, one launch ({run})', flush=True)
            # The sequence's own budgets end a probe that hangs; this watchdog is the backstop. The player's settings
            # at their defaults (PLAYER_DEFAULTS): wow64's XInput check needs controller 0 (src/pw_controller.h).
            result = subprocess.run([sys.executable, str(ROOT / 'tools/run-console.py'), '--name', run,
                                     *player_defaults(),
                                     '--exe', 'gate-sequence.exe', '--sequence', str(sequence), '--watchdog',
                                     str(120 + sum(entries[n][2] for n, _ in members))], cwd=ROOT)
            launch = launch_outcome(run, result, started)
            own = sequence_outcomes(run, [(n, entries[n][1]) for n, _ in members])
            for member, _ in members:
                index += 1
                outcome = {**launch, 'case': member, 'run': run, 'group': unit, 'launch_passed': launch['passed'],
                           'launch_seconds': launch['seconds'], **own[member]}
                outcome['passed'] = bool(launch['passed'] and own[member]['probe_passed'])
                record['cases'].append(outcome)
                print(f'== Test {index}/{len(selected)}: {member} {"PASS" if outcome["passed"] else "FAIL"} '
                      f'({outcome["seconds"]} s in {unit})', flush=True)
            evidence.write_text(json.dumps(record, indent=2) + '\n')
            if launch.get('result') not in ('finished', None) and not launch.get('returned_to_launcher'):
                record['stopped_after'] = unit
                break
            continue
        index += 1
        print(f'== Test {index}/{len(selected)}: {name} ({run})', flush=True)
        runner = runner_of(name)
        pinned = player_defaults(case) if runner == 'run-console.py' else []
        result = subprocess.run([sys.executable, str(ROOT / 'tools' / runner), '--name', run] + case + pinned, cwd=ROOT)
        outcome = {'case': name, 'run': run, **launch_outcome(run, result, started)}
        record['cases'].append(outcome)
        evidence.write_text(json.dumps(record, indent=2) + '\n')
        print(f'== Test {index}/{len(selected)}: {name} '
              f'{"PASS" if outcome["passed"] else "FAIL"} ({outcome["seconds"]} s)', flush=True)
        if outcome.get('result') not in ('finished', None) and not outcome.get('returned_to_launcher'):
            # A title that did not come back holds the console: nothing more can run.
            record['stopped_after'] = name
            break
    record['complete'] = [c['case'] for c in record['cases']] == expected
    record['seconds'] = round(time.monotonic() - batch_started, 1)
    passed_count, skipped, failed_count = tally(record['cases'])
    record['passed'] = record['complete'] and bool(record['cases']) and failed_count == 0
    if skipped:
        record['skipped'] = skipped
    evidence.write_text(json.dumps(record, indent=2) + '\n')
    print(f'== Battery: {len(record["cases"])}/{len(selected)} completed, {passed_count} passed, '
          + (f'{len(skipped)} skipped for the environment ({"; ".join(skipped)}), ' if skipped else '')
          + f'{failed_count} failed ({record["seconds"]} s)', flush=True)
    if any(c.get('pipe_memory_exhausted') for c in record['cases']):
        print(f'== The console\'s pipe memory ran out ({PIPES_GONE}): restart it before the next run', flush=True)
    print(json.dumps({'battery': args.name, 'passed': record['passed'],
                      'cases': {c['case']: 'skipped' if c.get('skipped') else c['passed'] for c in record['cases']}},
                     indent=2))
    return 0 if record['passed'] else 1


if __name__ == '__main__':
    sys.exit(main())
