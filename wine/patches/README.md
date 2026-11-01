# PS5 Wine patch series

Reviewed 2026-10-07. The ordered series adapts GE-Proton's pinned Wine to PS5:
shared wineserver, user driver, Vulkan/display, memory/signals, files, audio,
controller interfaces, payload children/descendants and their lifecycle.

Production uses the payload owner/broker layout, default Mesa/Zink and helper
audio. Old no-process/headless-only descriptions refer to earlier patch stages.
Patch 0936's sparse recommit behavior now has focused console VM evidence;
qualification is no longer host-only. See
[current processes/memory](../../docs/WINDOWS_PROCESSES.md).

## Provenance and scope

The series derives from prospero-win's LGPL-2.1-or-later PS5 work, rebased onto
Wine `1c78596625336a73dbfc035dbbcf0dbdd95b06eb`, GE-Proton11-7's pin.
Retain authorship, dates, licence headers and rebase notes. Source provenance
and dependency obligations are in [NOTICE.md](../../NOTICE.md) and
[LICENSING.md](../../LICENSING.md).

The IA-32 translator patches 0601, 0610 and 0710 were not imported. The work promoted from `win-32bit` to local `main`
runs PE32 natively under WoW64; its measured qualification is in
[IA32_DESIGN.md](../../docs/IA32_DESIGN.md). Deferred OpenGL/diagnostic patches are references, not additional
active patches to apply blindly.

Patches 2001-2062 are GE-Proton's ffmpeg + winedmo video rework, imported from the fork's `patches/ge-video-rework` (HEAD `e24b389e`, built on the same Wine pin) and applied after the PS5 series, as GE applies them after its own: winegstreamer is gone and winedmo demuxes, decodes, converts and muxes on FFmpeg for Media Foundation, DirectShow and the WM reader. 62 of the 66 apply; the RSX3D audio shim (one game's, a new DLL) and the three winepulse patches (no PulseAudio here) are left out. Four needed a rebase, each noted in its header: 2001 deletes a winegstreamer file Valve's tree still changes, 2004 keeps 0700's lock-free wait, 2006's wine.inf context differs, and the hunks no console build reads (MAINTAINERS, the macOS preloader, the GitLab image, winepulse) are dropped. Patches 2063 and 2064 are this project's fixes on top, both found with GE-Proton11-7's own Linux build: the source reader reports a decoder's new frame size (1920x1088 with a 1080 aperture) instead of keeping the old one, and MFShutdown waits for the work queues' callbacks without the lock a callback ending a source reader needs (it deadlocked in 5 of 8 runs). Patch 2065 counts, on the PS5 only, what winedmo's demuxers open, seek and read from their byte streams, for the helper's 10 s telemetry (`wine-child: media`). winedmo's Unix side is built by tools/build-wine-ps5.sh over the pinned FFmpeg ([NOTICE.md](../../NOTICE.md#media-winedmo-and-ffmpeg)).

Patch 0955 fixes the nested `INPUT32` in WoW64's internal hardware-input call, using
the same widening helper as public `SendInput`. HID reports otherwise become message
zero and stall ordinary message pumps. Paired raw-input and rendering probes qualify
the fix; see [CONFORMANCE.md](../../docs/CONFORMANCE.md).

Patch 0956 prepares the shared runtime-owned prefix bundle after wineboot and
before application imports. Fonts, ICU, Mesa, DirectX DLLs and i386 HID drivers
use one versioned installer; see [SHARED_PREFIX.md](../../docs/SHARED_PREFIX.md).

Patch 0957 preserves prior native signal handlers in SDK threads without a
Wine TEB, while Wine threads keep WoW64's raw, host-FS-first signal entry.
Patch 0958 retains each GDI window's pixels and composes in server Z order, so
background repaints cannot overwrite dialogs and closure reveals the background. Qualification is
tracked in [PLAN.md](../../docs/PLAN.md), including failures.

Patches 0992–0994 carry DWM surface state: dwmapi attributes, frame margins and
thumbnails reach owner-checked server state exported in snapshot v4 (0992);
uxtheme buffered-paint alpha and composited theme text implement painting over
glass (0993); the Mihawk taskbar becomes Mica with live window previews (0994).
See [Mihawk OS](../../docs/MIHAWK_OS.md#dwm-surfaces-rounded-corners-materials-and-previews-2026-10-10).

Patch 0995 makes the hidden shell idle (game mode, step 2): the Mihawk taskbar
follows window events instead of rescanning every window each second and
repaints only buttons that changed, its clock wakes once a minute, and Explorer
starts tabtip.exe only where it has work (its `SteamDeck=1` variable).

Patch 0996 lets Explorer be the title's desktop session root (game mode, step
3): with `MIHAWK_SESSION=1` it enables its shell on Default, initializes the
session wallpaper (formerly the Windows `mihawk.exe`), and makes its desktop a
Windows-style Progman window while the system keeps the desktop window, so the
title can end and restart Explorer; the Mihawk taskbar follows window creation
and destruction through window events, adopts windows already open, and
broadcasts `TaskbarCreated` so programs add their notification icons again
after a restart. Patch 0997 keeps that shell window the bottom-most window
whatever a `SetWindowPos` asks (`HWND_BOTTOM` lands just above it), marks it as
the snapshot's desktop background (publishing only when one is set, not on
`GetShellWindow`), and sends it the `WM_PARENTNOTIFY(WM_DESTROY)` the thread-less
desktop window would drop, so the taskbar forgets windows of a process that
ended without destroying them.

Patch 0998 gives a process another one started its catalog settings: every game
lives in the `mihawk` prefix and starts from its desktop shortcut as Explorer's
child. The PS5 spawn path passes the new process's executable and command line
to the title, which finds its `game.json` entry (`src/pw_app_environment.h`); the
loader sets the settings on the Unix side and `init_startup_info` on the Windows
side, through 0974's overlay, now one function for both.

Patch 0959 consults the live native address map when a Windows query finds no
Wine view. Its host-tested helper subtracts Wine-owned reservations, so native
carrier mappings are unavailable without hiding space Wine can allocate.
Production builds and the 127-patch replay pass; console qualification is pending.

## Applying and changing the series

The builders apply `*.patch` files in filename order. Never hand-edit a vendor
checkout as the final change. `tools/check-patches.sh` replays the entire series
in a temporary tree; `--write` regenerates [PATCH_REBASE.md](../../docs/PATCH_REBASE.md).
The active filenames and freshly generated replay report are authoritative;
do not rely on old hand-maintained patch counts.

`tools/rebase-next.sh` is a controlled rebase helper for the pinned work tree.
Resolve rejected hunks by intent, preserve attribution and inspect every changed
call/ABI. A clean apply or hunk count is not proof of correct runtime behavior.

After Wine changes, rebuild the Windows modules selected by the patched trees
and the matching native PRXs. Production's patched-module list is derived by
`tools/wine-patched-modules.py`; missing PE modules previously hid input fixes.
Use the production builder/deploy/gate sequence before a code commit.

## Checks

`tools/test-rebase.py` exercises rebase helpers in throwaway repositories.
`tools/check-wine-host.sh` validates an exported build with pinned local registry
inputs. Focused portable tests and the exact-source console gate qualify behavior.
The older 54/95-patch status and detailed rebase examples remain in
[the patch history](../../docs/history/2026-10-07/wine/patches/README.md).

The `mihawk-os` theme slice adds 0972 for ordinary themed caption buttons,
retaining classic fallback and existing hit geometry, and 0973 for shared
appearance receivers. The latter refreshes process-local theme metadata and
system-colour GDI caches before application notifications without republishing
registry preferences or metrics; retained HTHEME ownership is preserved.
Paired receiver probes reproduce the old cache failure and qualify the new
behavior. The complete pinned replay currently passes **141/141 patches**;
console code-commit qualification is tracked in the current PLAN gate record.

### 1172: a thread suspended in a system call's return reports the call's context (2026-10-16)

`__wine_syscall_dispatcher`'s return moves `%rsp` into the frame before it restores the program's registers, and from
there `is_inside_syscall` says no, so a suspend landing in that window reported the Unix registers: `%rip` in ntdll.so,
`%rsp` the frame + 0x70 in the kernel stack (upstream has the same window). CEF 150's stack sampler copied the main
thread's stack from that `%rsp` to `StackBase`; on the PS5 the kernel stack lies right below the Windows stack, so the
copy ran into the no-access page at the Windows stack's bottom (2026-10-16; record removed from the tree; in git history). 1172 starts such a
return again from the frame, so the thread suspends as one inside the call does; the return value, `%r15` and the
instrumentation tail's return address, which are outside the frame there, are found by four labels. Host, the pinned
Wine with only `signal_x86_64.c` rebuilt, `tests/stack-probe.exe` (gate case `suspend-context64`): 4405 of 144952
suspended contexts outside the stack without it, 0 of 151117 and 0 of 147189 with it, no register or return value lost.
Console (gate-2026-10-18-item3-01, 86 of 86, build 4cd26debd444632b): 46429 samples in 5 s, 0 outside the stack, 0 lost.

### 1173: a PS5 window surface flush takes no user lock (2026-10-18)

win32u flushes a surface under the surface's lock, from any thread that pumps messages, and `update_surface_region`
takes the user lock and then that lock. The driver's flush asked `get_window_long` (the user lock) for WS_EX_LAYERED
before publishing an input shape (0990): MihawkBrowser's UI thread and Chromium's occlusion-tracker thread deadlocked
in the desktop session (2026-10-18, record removed from the tree; in git history: both last requests 96,746 ms old, `get_window_region` and
`get_message`, neither in a server nor a sync-device wait; a blank or frozen page, a close that never arrived). The
driver keeps the style in its own window record, from WindowPosChanged and SetWindowStyle.

### 1137-1139: the Windows 11 prototype's chrome, pointers and monospace face (2026-10-16)

1137 makes user32 load each system cursor from the file the user's scheme names in `HKCU\Control Panel\Cursors`
(one shared cursor per process; an animated one survives `DestroyCursor`). 1138 lets the session root name the
Mihawk pointer scheme and Cascadia Mono (console face, Consolas substitute) once per prefix, before it creates a
window. 1139 is Windows 11's window frame: a framed top-level window's visible rect excludes the invisible part of
its resize border (the server's hit test keeps the window rect, so the frame still sizes it), the compositor draws
the default #66757575 border, the title bar is SM_CYCAPTION with no line, caption buttons are SM_CXSIZE x
SM_CYCAPTION from the inside rect's top right with hot tracking (`down = 2` through the theme hook), and
`DWMWA_EXTENDED_FRAME_BOUNDS` answers the visible bounds. Host oracle: `tests/theme-parts-probe.c`; console:
`gate-2026-10-16-win11-01` and `-02` (on the f3aa632 base).

### 1102, 1105, 1183: the desktop's icons as the prototype draws them (2026-10-16, 2026-10-17)

The Mihawk desktop is shell32's folder view in Progman (1100). 1102 gives that view the prototype's grid at its three icon sizes
(`src/pw_desktop_icons.h`: 32, 48 or 96 px icons in 75 x 75, 75 x 91 or 123 x 139 cells, with the desktop look, set before
any item is placed or painted; the list no longer auto-arranges), shows This PC and the Recycle Bin first, then the user's
items, keeps each icon's cell by name (`IconPositions` version 3, in 1/256 of a cell, so a size change keeps it) and moves
dragged icons to the nearest free cell. The icon size, Auto arrange and Align to grid are the desktop's view settings, kept
where Windows keeps them (`Shell\Bags\1\Desktop`: IconSize, FFlags) and changed through `IFolderView2`
(SetViewModeAndIconSize, SetCurrentFolderFlags) or Ctrl+Shift+1-4. Before, Wine's list arranged every icon in list order on
its first paint and on the redraw after filling (seen in a host trace), so a saved place did not hold (1105 skips that
arrange for the desktop). 1105 draws an icon view with `LVS_EX_TRANSPARENTSHADOWTEXT` as the prototype's desktop, sized by
the list's icon size: the item's plate for hover (the list now tracks the item under the pointer), selected, selected under
the pointer and selected without focus, the rubber band, and labels laid out as Blink lays them out and drawn from the
prototype's own glyphs in three layers (two shadows, then the text). Plates, band and glyphs are measured
(`themes/windows11/desktop`, through `tools/build-desktop-sprites.py`); a name with an unmeasured character keeps GDI's text.
1183 adds a 96 px list to shell32's icon cache for the large icons (Wine's lists stop at 48, then 256; ILD_SCALE is
unimplemented). Host test: `tests/test_pw_desktop_icons.c` (at each size: the text equal to Chromium's in every solid pixel,
the shadows within its spread, the grid equal to the prototype's boxes).

1184 starts a program opened from a folder in that folder, as Windows does. A Direct3D 12 game opened from a File Explorer
window started in Explorer's folder, so vkd3d-proton found no `vkd3d-proton.cache` beside the game ("Failed to map
read-only cache") and the game sat on a black window building every pipeline again; started in its folder it reached
its title screen. The shell view gave ShellExecuteEx the folder only when the folder, asked the attributes of its own
absolute ID list, said it was in the file system, and the item menu's verbs gave none; both now give the folder's path
whenever it has one. Checked by `tests/shell-open-probe.c` (gate case shell-open64), which opens itself through an
ExplorerBrowser's view both ways, the view's own open (Enter, a double click) and the item menu's default Open (its
right-click menu), and compares each opened copy's working folder with its own. Without 1184 the menu's copy starts in
the Windows folder, on upstream Wine 11.20 on Linux as well (`view=C:\shell-open menu=C:\windows`); the view's already
started in its folder, on the console too.

### 1214: the title's stop logs off the desktop session (2026-10-18)

1214 adds input type 8 to the title's server input (0931): the desktop session's log off, WM_CLOSE to the shell window
(Progman), as Start's Power does; with no shell window (Explorer unloaded in game mode) WM_CLOSE to the foreground window,
as type 4. The title's stop sent type 4, and in a desktop session with no program left the foreground window was one of
Explorer's own: Explorer took the close and went on, and the title ended the session by SIGKILL 10 s later
(two runs of 2026-10-18; record removed from the tree; in git history). Checked by gate case `desktop-stop`.

### 1210-1213: tooltips and the desktop's infotips as the prototype draws them (2026-10-18)

1210 draws every program's plain tooltip (no balloon or title, the system's font, colours and margins, characters the
prototype's glyphs cover) as the prototype's `.tooltip` (`src/pw_tooltip.h`): laid out as Blink lays it out (pre-line
white space, at most 320 px wide), its text from the prototype's own glyphs (`themes/windows11/tooltip`, through
`tools/build-tooltip-glyphs.py`) over the flyout's acrylic with 4 px corners, placed as `ui.position` places it (20 px
below the pointer, 8 px inside the work area), shown after 500 ms or 100 ms within 400 ms of another tip hiding, and
kept hidden after a press until the pointer reaches another tool. Without the compositor it is drawn opaque. 1211 gives
the list view infotips (`LVS_EX_INFOTIP`: one whole-list tool, `LVN_GETINFOTIP` for the item under the pointer, popped
when that item changes). 1212 makes the desktop ask for them and answers with the prototype's texts (the Recycle Bin's
description; a file's type, size and date modified; a folder's date created) unless the item has an `IQueryInfo`. 1213
gives a 4 px flyout the tooltip's `--shadow-8` (`PW_SHADOW_TOOLTIP`). Host test: `tests/test_pw_tooltip.c` (the box
Chromium's to the pixel and the text within a level, light and dark; the placement).

### 1150-1151: File Explorer as the prototype draws it (2026-10-16)

1150 hands the Mihawk File Explorer window to `wine/ps5/pw_shell_explorer.h`, which draws the prototype's Windows 11
Explorer: the client area takes the caption (WM_NCCALCSIZE keeps only the frame), a 48 px title bar of Mica Alt glass
(DWMSBT_TABBEDWINDOW, the compositor's `PW_BACKDROP_MICA_ALT` made the prototype's `.mica-alt`) with one tab and
46 x 48 caption buttons, the address row, the command bar, the navigation pane, Home (Quick access, Favorites, the
newest files of the user's folders) and the status bar, all laid out by `src/pw_explorer_layout.h` (equal to the
prototype's boxes) and, at 96 DPI, drawn with the prototype's own pixels (`themes/windows11/explorer`, through
`tools/build-explorer-sprites.py`). The shell view keeps the files: its list loses its client edge and, in details
view, takes the prototype's columns, column header and 32 px rows. Without a folder Explorer starts on Home; the
window's title is the location's name and " - File Explorer". Since then, all in `pw_shell_explorer.h`: the icon views
are the prototype's tiles (the list keeps the places `pw_explorer_tiles` gives), tabs are one browser each, Filter is
the browser's site answering `ICommDlgBrowser::IncludeObject` (Wine's ExplorerBrowser asks its site on every
navigation), and the details pane is the prototype's; the search box is drawn, not wired. 1151 makes shell32's
`IFolderView::ItemCount` count the selection for `SVGIO_SELECTION` (it counted every item, so Explorer's commands and
status bar took an unselected folder for a fully selected one).

### 1152: a navigation shows the new folder only once it is drawn as the prototype's (2026-10-16)

shell32's `CreateViewWindow` shows and updates the new view inside ExplorerBrowser's navigation, before the browser
reports it, so every folder to folder navigation first painted Wine's classic list. 1152 passes the browser's
navigation pending, view created and failed events to `pw_shell_explorer.h`, which holds the browser's window from
drawing (`WM_SETREDRAW`) from the start until the new view is styled; the old folder stays on screen meanwhile. Each new
view logs `pw-explorer: view created held= visible=`, and desktop-session requires every one invisible when created.

### 1200: the active window and every window's theme in the snapshot (2026-10-16)

A framed window's shadow is the prototype's `--shadow-window` while it is active and `--shadow-window-inactive`
otherwise, each in light and dark (`src/pw_window_effects.h`). 1200 marks the foreground thread's active window with
`PW_WINDOW_ACTIVE` and reports every top-level window's theme (`DWMWA_USE_IMMERSIVE_DARK_MODE`) as its material, not
only a glass window's; activating a window, bringing one to the foreground or attaching thread input republishes the
snapshot. `tests/window-effects-probe.c` checks that the foreground window's row, and only it, is marked.

### 1162: the Mihawk mark at every DPI (2026-10-18)

The Start button shows the project's own Mihawk mark (`themes/windows11/start.svg`, original artwork), not the Windows
logo, a Microsoft trademark, which it showed from 2026-10-16. One function draws it at every DPI
(`src/pw_taskbar_logo.h`; `tests/test_taskbar_logo.py`: within 1.5 levels on average of librsvg's drawing of the SVG at
22, 33 and 44 px), 22 px in the 40 px button at 96 DPI. There the prototype's hover and press plates go under it, as the
captured Start sprites it replaces did; at another DPI it replaces the PP mark's polygon.

### 1140-1141: the taskbar as the prototype draws it (2026-10-16)

1140 lays Explorer's taskbar out as the approved prototype does (`src/pw_taskbar_layout.h`, equal to its boxes) and,
at 96 DPI, draws the prototype's own pixels in each slot (`wine/ps5/pw_shell_taskbar.h`, sprites from
`themes/windows11/taskbar` through `tools/build-shell-sprites.py`): the search pill, Task View, the pinned File
Explorer, Settings, Notepad and Terminal (a pinned app's windows show on its pin; others follow), the hidden-icons
chevron, the status button and the two-line clock, with the prototype's plates and running pills (Start's mark is drawn: 1162). The clock's text is
the prototype's own glyphs placed on Skia's quarter-pixel pen grid (`src/pw_text_glyphs.h`, glyphs from
`themes/windows11/taskbar/clock-*.json` through `tools/build-shell-glyphs.py`), and its width lays the tray out; a
character without a measured glyph (another locale) keeps GDI's drawing. 1141 lets the
taskbar ask for a backdrop past Windows' own values (5), which the compositor draws as the prototype's taskbar
acrylic (`PW_BACKDROP_TASKBAR`, `src/pw_window_effects.h`) instead of Mica. Search opens Start; Task View, the hidden
icons and Quick Settings are drawn, not yet wired.

### 1160-1161: Start, Search and the taskbar's flyouts as the prototype draws them (2026-10-16)

1160 lets a window ask for two more backdrops past Windows' values: 6, the prototype's flyout acrylic-base (Start and
Search), and 7, its acrylic (Quick Settings, the notification centre and calendar, the hidden icons). The snapshot
reports them as `PW_BACKDROP_FLYOUT_BASE` and `PW_BACKDROP_FLYOUT` with `PW_SHADOW_FLYOUT` and the flyout stroke
(`src/pw_window_effects.h`): the taskbar's filter chain over a blur of only what lies under the flyout, the tint inside
the 1 px stroke, `--shadow-28`. 1161 wires the taskbar's search pill to Start's Search view, the status button to
Quick Settings, the clock to the notification centre and calendar, and the chevron to the hidden icons (the
notification area's own icon windows, which wait off the bar in the prototype's layout). The surfaces themselves are
`wine/ps5/pw_shell_start.c` and `wine/ps5/pw_shell_flyouts.c`, their fixed chrome the prototype's own pixels
(`themes/windows11/start`, `themes/windows11/flyouts`). Task View stays drawn, not wired.

### 1170: DXCore's adapters are GPUs (2026-10-16)

`IDXCoreAdapter::IsAttributeSupported` answered FALSE to everything, so Chromium's NPU scan (CEF 150 in
MihawkBrowser, `gpu/config/gpu_info_collector_win.cc`) took the console's GPU for an NPU and read its driver
description "PS5" into an eight-byte pointer: 0x00355350, and the browser's GPU thread faulted at startup
(2026-10-16; record removed from the tree; in git history). Every adapter dxcore lists is a wined3d GPU, so it now reports Direct3D 11 and 12
graphics and core compute.

### 1171: a kill waits for the fault handler to return (2026-10-16)

`quit_handler` ends a thread outside a syscall at once, and the trap and fault handlers run outside one, on the
signal stack beside the TEB. Both take the virtual mutex unmasked, and 1127's code conversion runs under it through
the dmem engine's lock, so a kill landing mid-conversion ended the thread with both held. With 16 KiB windows
winedevice.exe's exit stalled this way three times of three (`gate-2026-10-16-browser-01`: the lock's owner was the
first thread the exit killed, 004c, which printed "killed" and never released it). SIGQUIT joins those handlers'
mask, as `ps5_mmu4k_enter` already masks it for a run: the kill lands when the handler returns, in user mode with
nothing held or in a syscall where the thread is marked. The suspend handler stays killable.

### 0974: explicit initial application environment

Carries caller-selected catalog/launch settings separately from the owner's
runtime allowlist. Applies them after initial prefix/registry provisioning with
UTF-8 decoding and existing Wine case-insensitive environment replacement.
Removes all case variants before installing one selected value. A child's own
startup block remains authoritative except for the settings the title gives it
from the catalog (0998). The matching bootstrap ABI is in
`wine/ps5/pw_wine_process.h`; transport bounds are host-tested in `src/pw_environment`.
Paired console probes are required gate cases; qualification is recorded in the
current plan rather than inferred from the Linux oracle or a successful build.

`0975` publishes a bounded copy of Wine's authoritative input-desktop window
list, topmost first, after relevant requests and asynchronous window destruction.
Rows carry full HWND, lifetime generation, native/Windows process ownership,
owner HWND, visible geometry and independent visibility/unsupported-region flags.
Broker readers only copy the record under a separate mutex; they never walk
server objects. The private window-info response carries the matching lifetime
and native owner. The first slice qualifies this metadata with opposite-architecture
children, movement, hide/minimize/restore, destruction and explicit overflow;
it does not enable per-window pixel composition.


Patch 0976 publishes GDI surfaces by the server-authored HWND generation in the
opt-in independent-window route, including the actual local Explorer root and
Wine's padded surface offsets. Helper pages are ACKed and bounded; the broker
retains stable copies and uses patch 0975's window state. The legacy default,
GPU HWND association and shaped/layered content have separate proof limits in
[MIHAWK_OS.md](../../docs/MIHAWK_OS.md).

Patch 0977 publishes a coherent native cursor coordinate pair from Wine's
canonical server updates and input-desktop switches. SetCursorPos, SendInput
and title injection share the same position. Native display threads read copied
coordinates and never walk Wine objects. Both architectures now pass 19/19
mixed-window checks, including five actual pointer captures, in the complete
50/50 exact-source gate. Full cursor shape/visibility parity remains open.

Patch 0978 binds native Vulkan surfaces to full Wine HWND lifetimes and adds
canonical screen-space client clips plus independently rendered child HWNDs to
the version-2 snapshot. Client-surface registration remains Wine's; the broker
copies retained GPU pixels after its existing fence and matches the owner-bound
helper PID. The nested GPU probes and 52-case gate are not yet console-qualified.

Patch 0979 preserves ordinary parent GDI backing when the opt-in brokered HWND
route acquires a GPU pixel format. Captions and GDI children retain their normal
Wine surfaces and DC clips; GPU planes keep their canonical clips. It is pending
console qualification alongside 0978.

Patch 0980 keeps the primary-monitor work-area reservation in the existing
desktop shared object. PS5 SPI and monitor queries use its coherent raw rectangle
instead of a per-client cache, preserving normal DPI conversion and monitor
clipping. A successful SPI_SETWORKAREA sends the requested change notification
without requiring a registry write. The opposite-architecture diagnostic checks
new and already-running clients, monitor information, maximization and restoration.
Console qualification is pending; this does not yet implement the taskbar UI.


### 0988–0990: retained shapes, alpha and input coverage

0988 exports whole-window regions independently from GPU-client clips in the
version-3 snapshot. The compositor preserves empty regions and refuses invalid
or overflowing snapshots before touching the destination.

0989 carries Wine's existing surface alpha/colour-key coverage through version-2
retained pages. The page format is immutable; changing between opaque and
premultiplied pixels uses the existing acknowledged replacement path. Constant
opacity is applied once, including opacity-only `UpdateLayeredWindow` calls.
Top-level GDI layered windows are supported by this candidate; layered GPU or
child windows remain explicitly unsupported.

0990 publishes bounded coverage to the owning server HWND generation. Wine's
existing authoritative hit-test uses it without replacing application-visible
window regions. Ownership, dimensions and a 32 MiB total coverage budget are
checked before replacement; destruction releases coverage. This fixes the
observed zero-alpha click interception in both architectures. Focused console
results and the required full-battery status are in [PLAN.md](../../docs/PLAN.md);
these patches remain uncommitted until exact-source qualification completes.


0991 resets driver-owned opacity/input state when `WS_EX_LAYERED` changes,
before Wine's normal surface update. The expanded lifecycle probe exposed a
stale 50% factor after style removal; the new callback follows Wine's existing
attribute-reset contract. Focused requalification is pending in PLAN.md.

Patch 1127 runs a helper's code from JIT shared memory, the only memory a payload's kernel lets execute
(exec-kernel-2026-10-14-01 to -03). At an execute fault Wine allows in a private or image view, `virtual_handle_fault`
converts the faulting host page and its neighbours of the same protection, within one 64 KiB window (four host pages;
256 KiB before 2026-10-16: `evidence/cluster-ab-2026-10-16-ab.json`; 16 KiB waits for a gate), through
`__wine_ps5_exec_convert`; a fault the host already allows runs again under the virtual mutex, replacing 0954's
pre-lock wait; an execute fault nothing can serve (a section view) is counted. An image view's code goes through
`__wine_ps5_exec_convert_image` instead, which maps a copy another helper made of the same bytes when the title's
registry has one (shared code copies, `wine/ps5/pw_wine_codeshare.h`, docs/PLAN.md; host-tested, not run on the console).
The hook is host-tested and `vm-protect-probe` cases E1-E7 pass on the pinned desktop Wine; it has not run on the console.

Patch 1128 answers the hang of 2026-10-15 (late-library: "unwire timeout at 0xc80000000" while a helper with RADV up
unmapped a bare 28 GiB reservation; the freezes of the earlier emulator experiment (removed) had the same signature). `unmap_area` no longer unmaps a range the
direct-memory engine owns: it becomes a reservation and a reserved area again (the address pool still unmaps its
chunks). The engine (`wine/ps5/pw_wine_dmem.c`) never maps over, unmaps or reserves again over a kernel entry it did
not make: a commit over one is refused (EEXIST; `set_vprot` puts the page bytes back as they were, and a
`MEM_REPLACE_PLACEHOLDER` commit fails with the placeholder free again), a reservation in a kept reserved area is asked
about first, a release goes around it, a hole in a bare part is reserved again before it is trusted, and each one
becomes a system view at the next `map_view` or VirtualQuery (which reads it as occupied even before). The section
pager asks the same of every fault's range before its fixed mapping (`PwWineSectionOps.foreign`). What was wired at
0xc80000000 is not known; klog now lists the entries of every span of 1 GiB or more Wine unmaps, lays over or keeps.
Host-tested (`tests/test_pw_wine_dmem_ps5.c` models the crash; `tests/test_wine_virtual_guard.c` runs the patch's
virtual.c code); it has not run on the console.

Patches 1129 and 1130 close the paths an audit of every host memory call a helper makes found around the engine's
ownership (docs/PLAN.md, "The PS5 memory backend"). 1129: `NtLockVirtualMemory` wires nothing on PS5 (the engine lays,
converts and unmaps its runs without unlocking), the address pool's give-back is kept reserved like every other release
(it was a bare unmap of up to the size asked), MEM_RESET keeps the pages instead of an unmeasured madvise, and
the launcher client library's `.text` protection goes through `NtProtectVirtualMemory`. 1130: before a placed GPU mapping, win32u gives
the committed range back to a bare reservation the engine forgets (ntdll's `__wine_ps5_placed_range_bare`, under the
virtual mutex with its signals blocked), so the GPU client's wired mapping lies over a reservation and the engine treats
it as foreign; graphics patch 0013 makes the client map only over a bare reservation. The range is also lent
(`__wine_ps5_dmem_lend`) until Wine releases it, foreign by record, since the owner's shared memory over a reservation
is invisible to the kernel query; 0014 had placed direct memory only, which failed host-dmem-fallback64, and 0016 places
shared memory again. 1131: a helper (`WINE_PS5_CHILD`) never starts
win32u's AGC OpenGL stack, whose `eglInitialize` mapped wired GPU memory where the kernel chose; a helper's OpenGL is
Mesa/Zink. Host-tested by `tests/test_crash_replays.c` against `tests/fake_ps5_kernel.c` and
`tests/test_helper_opengl.py`; on the console (v1-backend and v2-backend-gate, 2026-10-15) 65 of 71 gate cases
passed; 1130's placed range failed host-dmem-fallback64 (below).

Patch 1132 keeps the DOS area (0x10000-0x110000) a bare reservation on PS5. Upstream maps it read-write for apps that
touch it unallocated; with 1128's guard VirtualQuery read that mapping as one not Wine's, and a 32-bit program found no
free block below 16 MiB (v3-gap-2026-10-15: "gap 0x10000-0x110000 reads occupied: protection 0x3").

1130 now also lends the placed range (`__wine_ps5_dmem_lend`): the engine treats it as foreign by record until Wine
releases it, since the owner's shared memory reads as the reservation under it; with graphics patch 0016 a WoW64
program's shared-memory buffer is placed again (v4-lend-2026-10-15: host-dmem-fallback64 passed, 20 made, 63 refused).

Patch 1133 fixes free_ranges_insert_view for a view that begins in a 64 KiB granule its neighbour keeps (an emulator splits
placeholders on 16 KiB pages): the lookup found the free range ending at that granule, the removal shrank to nothing,
and the free range the view runs into stayed listed under it. A HashLink game's 20 MiB section view at 0x2002c4000 left
0x2002d0000-0x2016c0000 free and create_view asserted 24 s in (2026-10-15; record removed from the tree; in git history); with the patch the game reached
gameplay (2026-10-15). Host: the "free-range order" replay fails without it.

Patch 1123 (a read-only page beside a written one reported to the program as written before the store was stepped) is
removed: its false reports made the earlier emulator experiment (removed) upload stale guest bytes over images the GPU drew (a Unity game's menu glyphs
as white blocks, 2026-10-15; record removed from the tree; in git history), and a write tracker must see the access violation at the store itself.
1125, 1127 and 1128 were regenerated without it (hunk positions and context only). Patch 1134 runs such stores through
the 4 KiB MMU instead (docs/MMU4K.md, wine/ps5/pw_wine_mmu4k.h): in 1122's branch of virtual_handle_fault a helper's
write fault starts a run on the faulting thread, outside the signal handler, which executes the program's instructions
one at a time with the stores to writable 4 KiB pages of held host pages going through an alias of the same direct
memory, and ends before any store into a protected page so that it faults natively; a run that cannot move takes
1122's step. Titles, WoW64 threads and `WINE_PS5_MMU4K=0` keep 1122's step. The census prints a `wine-child: mmu4k`
line. Host-tested (`tests/test_pw_wine_mmu4k.c` against the PC's 4 KiB pages, `tests/test_crash_replays.c`); the
console probes (`vm-protect-probe --truth/--speed/--contention/--rearm`, `section-view-probe`) pass on the PC under the
system Wine; nothing of it has run on the console.

Patch 1135 lets a helper end itself when Wine cannot: NtTerminateProcess stamps an exit before its write-behind flush
(`pw_ps5_child_doomed`, `wine/ps5/pw_wine_process.h`), and the GPU broker's client stamps GPU when it gives the broker
up for good (graphics patch 0017, through the lookup `wine/ps5/pw_wine_dl.c` hands it; not at win32u's
VK_ERROR_DEVICE_LOST, which a present the broker refused and answered returned in gate-hang-a-2026-10-15-01, the
programs presenting on); the loader's control thread, which takes no Wine lock, returns into `_exit` 5 s after a GPU
stamp or 20 s after an exit stamp unless the title closed the helper first (`src/pw_control_watch.h`). It also
sends `unix_private.h`'s mutex_lock and mutex_unlock for the virtual mutex to a lock-free owner record
(`wine/ps5/pw_wine_perf.h`): the census prints a `wine-child: vlock` line, the log thread a `wine-child: stall` line
once the mutex or the direct-memory engine's lock has been held 2 s, and image_stats is skipped while one owner has held
the mutex 1 s (a 2026-10-15 run, record removed from the tree; in git history, froze with the census stuck there for 160 s and no record of the holder).
Host-tested (`tests/test_pw_wine_perf.c`, `tests/test_pw_control_watch.c`, `tests/test_module_wiring.py`); the first
version ran in gate-hang-a-2026-10-15-01: 72 of 75 passed; the failures were a root-capture race in the window-effects
probe and the console resolver's SERVFAIL for dnsapi32/64 (docs/PLAN.md, "Hang safety, commit A"); this version has not
run on the console.

Patch 1136 makes a 4 KiB MMU run hand Wine's virtual mutex to a waiting thread: the run holds the mutex from its start
to its exit, and every 256 instructions reads 1135's count of threads blocked on it (`pw_wine_perf_vlock_waiters`),
ending with the exit `yield` when one waits. The unlock and relock it replaces handed the mutex over 0 times in 50 on
the host and emptied the run's page table each time; who takes the mutex after a `yield` is the mutex's and the
scheduler's choice (host: a blocked thread first 49-50 times in 50 after an 80 us gap; console not measured). No run
starts once `process_exiting` is set (mutex_lock does nothing then), a run that asked just before holds the mutex
with plain `pthread_mutex_lock`/`unlock` so runs still exclude each other, and a fault in the engine's own code, which
leaves the run's lock held, stamps
`PW_CHILD_DOOM_ENGINE` so the loader ends the helper 5 s later. With it, `wine/ps5/pw_wine_dmem_ps5.c`'s heap blocks
SIGUSR1, SIGQUIT and SIGINT across its direct-memory calls, which hold the engine's spin lock. Host-tested
(`tests/test_pw_wine_mmu4k.c` M5, M6, M8, M8-yield; `tests/test_crash_replays.c`, `tests/test_module_wiring.py`); not
run on the console.

Patch 1180 gives the title and every helper one synchronization device. Patch 0890's device kept its objects in the
title's heap and has been off since programs moved into helpers (patch 0925 left it out there), so each helper's
SetEvent, ResetEvent and wait was a round trip to the title's server thread: a Direct3D 12 game made 40k to 634k server
calls per 10 s, mostly `event_op` and `select` at 63 to 1600 us each, and drew 1 to 25 frames a second
(2026-10-16; record removed from the tree; in git history). The objects now live in shared memory the memory helper makes (`src/pw_ntsync.h`): the
title formats the region named by `WINE_PS5_NTSYNC_REGION` before its server starts, and the server sends that
descriptor to each helper with its first thread's reply (bit 2 of `inproc_device` says it follows), as upstream sends
the ntsync device. A helper that cannot join stops at once, since its waits on the device's objects would all fail at
the server. The title takes a helper out of the device before it kills it (or at its exit message) and once it has
ended. Its waits leave the queues at the first. Their places are kept until the second, since its threads may still
sleep on their words: a live wait that took one slept behind the dead thread, which the kernel wakes first, and a
restored Explorer held a session open that way (`stress-gmdx-2026-10-18-11`). The lock names its holder's process,
so a helper told gone and killed while a thread on its way in held it has it taken back at the second call. On by
default since a5a0d88
(`src/pw_speed_defaults.h`); `WINE_PS5_NTSYNC=0` turns it off for the whole session. Host-tested
(`tests/test_pw_ntsync.c`: processes started afresh, a helper gone while it waits and while it spins through the lock,
a live wait on the handle of a helper told gone whose thread still sleeps). On the console a Direct3D 12 game went from 30.5 to 60.0
fps with it on, its server round trips from 1.27 M to 36.6 k per 10 s (two runs of 2026-10-17,
record removed from the tree; in git history); the runs are in `docs/PLAN.md`.

Patch 1187 says which process publishes which display rate. Every process that first takes the desktop window
publishes the PS5 driver's one mode from its own `WINE_PS5_REFRESH`, and the last one wins; with `PROTON_120HZ=1` a
program in the Mihawk OS session was offered only 60 Hz although its own variable said 120 (`modes-p2-2026-10-17-02`).
One `wine-ps5: display published ...` line per publish names the rate, the variable and the native process.

Patch 1188 makes a virtual desktop offer the display's rate. The Mihawk OS session is a Wine virtual desktop, where
win32u reports the driver's source as detached and adds its own: its starting mode had the rate forced to 60, and
Proton's emulate_modeset collapsed the virtual modes (at 60 and the display's rate) to that mode and rebuilt them from
its 60, so with every process publishing 1920x1080@120 a program was still offered only 60 Hz
(`modes-p2-2026-10-17-03`). The virtual source now starts at the display's rate and keeps its generated modes.

Patch 1189 names what keeps a Mihawk OS session from ending, in the title's hang snapshot. The session ends when its last
user process does; in `gate-2026-10-18-defaults-01` game-mode-dxvk64's session did not end after its game's process
ended (once in 12 runs with every speed switch on). The snapshot now prints the counts the server's shutdown is decided
on and every process with its system mark, running threads, end and image name. Diagnostic only.

Patch 1240 makes the Windows user Mihawk. Proton's advapi32 answers GetUserNameA/W with a fixed `steamuser`, and every
profile path follows from it (shell32's `%USERPROFILE%`, wineboot's ProfileImagePath and Volatile Environment,
LookupAccountSid's first user), so a new prefix's profile is `C:\users\Mihawk`. A prefix made before v3.0.0-alpha-3 is
moved once by the title before Wine starts on it: the folder, its registry's paths and a link from the old name
(`src/pw_wine_profile.h`, `tests/test_pw_wine_profile.c`). The Unix `USER` the title gives Wine, and so
`WINEUSERNAME`, is Mihawk too. A new and a moved prefix passed on the base PS5 (`docs/PLAN.md`).

Patch 1241 publishes the display only from a process whose mode is new. Publishing (`update_display_cache( TRUE )`)
enumerates the Vulkan GPUs and loads RADV into the process, about 30 MiB of the console's system memory, and every
process that took the desktop window did it: a menu builder, a console host, an installer and a launcher's update
dialog each held a 40-43 MiB Unix heap where a process without it held 9 MiB (the Pro, 2026-10-20).
The desktop window now carries the mode last published (width, height, rate), and a process publishes only when its
own differs, so the last rate still wins as patch 1187 describes; the others read the display from the registry.

Patch 2407 makes memory asked to be exported ordinary memory when there is no host handle type. Every game's Vulkan
on the console is the GPU broker's client, which offers no external memory. So an allocation with
`VkExportMemoryAllocateInfo` got no host handle and no D3DKMT resource, and failed with
`VK_ERROR_OUT_OF_HOST_MEMORY`: a Direct3D 12 game that makes a shared resource at start closed. The export request is
now dropped and the memory is ordinary. `vkGetMemoryWin32HandleKHR` on memory that was neither exported nor imported
returns `VK_ERROR_INVALID_EXTERNAL_HANDLE`. VKD3D-Proton's side is `platform/vkd3d-proton/patches/0003`;
`tests/d3d12-shared-probe.c` is the console check. The problem was found by reading prospero-win 0054 as PC5 carries it
(bornaradusin/PC5 at `81323dc3`, LGPL-2.1); this patch is independently written.

Patches 2408 and 2409 give games the console GPU's identity (`src/pw_gpu_identity.h`, with graphics patch 0045, which
makes the GPU client's RADV report the PCI ID of the retail RDNA2 Radeon of the console model's class: 1002:73DF on the
standard PS5, 1002:73BF on the Pro). RADV reported 1002:0000, and a survival-horror demo refused it with an "Unsupported graphics
card" box. 2408: win32u adopts Vulkan's PCI ID for the PS5 driver's GPU, and names it from the console's identity
("AMD Radeon PlayStation 5 GPU") rather than from `gpu_device_name`'s table; Vulkan's deviceName follows. 2409: AGS
took the family, compute units and clocks from libdrm, which the console has not, and reported a GCN4 GPU with none.
It now reports the model's RDNA2 figures when Vulkan's device is the console's (its PCI ID and that name). Host test:
`tests/test_pw_gpu_identity.c`.

Patch 2425 keeps the GPU's access to host memory a program gives it. A recompiled console game stopped at once in the
title: "the D3D12 driver cannot open heaps over existing memory" (a tester, 2026-11-01). VKD3D-Proton builds Direct3D
12's existing heaps on VK_EXT_external_memory_host, which the GPU client's RADV now offers in the title's own process,
the only one whose memory the GPU reaches (PS5_Mesa's `client-host-memory-winsys.patch`, PS5_Vulkan's client). The
client gives the pages GPU read and write with one `sceKernelMprotect`, and the kernel keeps those bits with the CPU's.
So win32u records each import with ntdll before it is made (`__wine_ps5_gpu_keep`, under the virtual mutex) and
removes the record when the memory is freed. The PS5 memory layer keeps GPU read and write on recorded pages in every
protection change. It takes them away, with a protection change, before a mapping there is replaced or unmapped, and
when the import ends (`wine/ps5/pw_wine_gpu_keep.h`). The first 8 imports are logged. Host tests:
`tests/test_pw_wine_gpu_keep.c`, and the gpu-kept case of `tests/test_pw_wine_dmem_ps5.c`. Console probe:
`tests/d3d12-existing-heap-probe.c` (battery case `d3d12-existing-heap64-title`).

Patch 2420 makes an execute fault raised to the program say what was there (the first 8). Logged: the 4 KiB page's
and host page's vprot; the view (none, private, image or section, with base, size and protection); the kernel's
protection of the page (`__wine_ps5_kernel_prot`, -1 with nothing mapped); and the conversions so far. A game in the
title that died on code said only "Unhandled page fault on execute access" (a tester's Elden Ring, 2026-11-01).
The title keeps no fault record for it, and a page Wine never let run gets no PS5 line otherwise. The record tells a
jump to memory nothing allocated, data run as code, and code the console would not run apart. No behaviour changes.
Host test: `tests/test_pw_wine_dmem_ps5.c` (the kernel query).

Patch 2410 makes FAudio's `FAPOBase_AddRef` and `FAPOBase_Release` atomic, as Windows' `XAPOBase` is: a game that
attaches one effect to voices it creates and destroys on several threads lost updates between them, the count reached
zero while a voice held the effect, and the mixer read freed memory. `tests/test_pc5_apobase.py` compiles FAudio's own
`FAPOBase.c` from the pin with the patch and runs eight threads on one effect (`PC5_SHOW_OLD=1` shows the pin failing it:
count 2 and the destructor run). Written from the problem PC5's prospero-win 0002 describes (NOTICE.md, "PC5"); host-tested
only.

Patch 2411 makes calls on a destroyed XAudio2 voice do nothing, as on Windows: a destroyed voice has no FAudio voice
(`voice_is_gone`), every method of the source, submix and mastering voices answers with nothing when it is gone (59 of them start with
`RETURN_IF_GONE` or a variant; getters return zeros; a second `DestroyVoice` is ignored), a new voice's sends leave out a destroyed voice,
and the destroyed voice's object goes to a new voice only ten seconds after the destroy (`slot_is_free`, `GetTickCount64`).
`tests/test_pc5_xaudio_destroyed.py` holds those invariants on the pinned file with the patch applied and runs the reuse rule
as written (the voices themselves cannot run on the host); the file compiles for XAudio2 versions 4, 7 and 9 against the
build tree's generated headers. Written from the problem PC5's prospero-win 0007 describes; host-tested only.

Patch 2412 keeps the process memory counts (patch 0905) for a second. Each query walked every view's pages with
`virtual_mutex` held, the mutex every allocation and page fault waits for; a game that asks from several threads every
few milliseconds (an Unreal Engine game's memory stats) kept it taken. A query within a second of the last walk now gets
that walk's counts without the mutex, one walker runs at a time, and the peaks are kept per walk.
`tests/test_pc5_vm_counts.py` cuts the function out of patches 0905 and 2412 and runs both on a made-up list of views: the
values, the walk times and a threaded run (4 threads asking every 10 ms against a thread taking the mutex): on the host
model the old function made 683 walks in 3 s and the allocator's lock waited 4.2 ms at p99 (13.8 ms worst) and got a
quarter of the locks, the new one made 3 walks and waited 0 ms at p99. The walk's cost is a model, not the console's: the
console run is still to do (8 threads polling `GetProcessMemoryInfo` every 10 ms while one allocates). Written from the problem
PC5's prospero-win 0075 describes; host-tested only.

Patch 2413 lets `SetPixelFormat` succeed for the format the window already has. Windows keeps a window's first format
(the same one again succeeds, another fails); Wine's builtin opengl32 does this itself, but gdi32 hands the call to
whichever opengl32 is loaded, and Mesa's WGL, loaded natively here for Zink, returns false on every second call. A game that sets
the format twice (Godot 4 does, in `window_create` and again in `_create_context`) stopped at "Failed to create an OpenGL
window". `tests/test_pc5_setpixelformat.py` compiles the pinned gdi32 `opengl.c` with the patch on the host, in front of an
opengl32 stand-in that refuses a second call as Mesa's `DrvSetPixelFormat` does (`PC5_SHOW_OLD=1` shows the pin failing the
repeat). Written from the problem PC5's prospero-win 0066 describes; host-tested only.

Patch 2414 stops `register_builtin_classes` waiting on another thread. Its body, run in a `pthread_once`, takes the loader
lock (each class's cursor and user32's part load through callbacks into user32), so a thread holding the lock in a `DllMain`
that made its first window waited for the thread that waited for the lock. The first thread now registers and runs
user32's part and says when it is done; a thread that finds it running registers the classes itself and leaves user32's
part to the first. `tests/test_pc5_builtin_classes.py` runs the two functions' text, cut from the pinned `class.c` with and
without the patch, against stand-ins that take the loader lock where win32u calls back into user32: the pin hangs
(`PC5_SHOW_OLD=1`), the patch gets through, and eight threads register the classes once. No game reproduces it in this
architecture yet; this closes a latent lock order, host-tested only. Written from the problem PC5's prospero-win 0066
describes.

`native/proton_wine.cpp` gives every game `OPENSSL_ia32cap=:~0x20000000`, which leaves OpenSSL's SHA-instruction code out: OpenSSL 1.0.2
to 1.0.2k, which games bundle (PC5 names Unreal Engine 4.13 to 4.21), crashes on a CPU that has those instructions, as the console's
does, and Proton sets the same for every game. The title's own settings were at `PW_WINE_START_MAX_ENV` (16), so it is now 32, a
`static_assert` in `native/proton_wine.cpp` keeps the table inside it, and `tests/test_pw_wine_start.c` checks that 17 settings go
through and one more than the limit is refused (it fails at 16). Written from the problem PC5's prospero-win 0025 describes;
nothing was run on a console, and no game that crashes this way has been named here.

Patch 2415 clears the nested-task flag (EFLAGS bit 14) a program set, on the fault it causes, and runs the instruction again, as
Windows and Linux do: a CPU check (Far Cry's) sets it with `popf`, the console's kernel cannot return to code with it set, and the
fault was reported as an access violation reading 0xFFFFFFFFFFFFFFFF. It sits in `segv_handler` ahead of patch 2102's UMIP decoder, and
`pw_umip_clear_nested_task` in `src/pw_umip.h` decides (`tests/test_pw_umip.c` covers the bits it keeps and that a retry cannot loop).
Written from the problem PC5's prospero-win 0062 describes; the handler itself compiles on the host but nothing ran on a console.

Patch 2416 gives a large heap block a page of room after its data before its region is rounded up to 64 KiB: a program that writes
one element past the end of one (harmless on Windows, whose heap leaves slack) faulted when the data ended exactly where the region did.
`tests/test_pc5_heap_room.py` evaluates the sizing expression cut from the pinned `heap.c` with and without the patch for sizes that end on
a 64 KiB boundary and for sizes that overflow (`PC5_SHOW_OLD=1` shows the pin leaving no room). `HeapSize` is unchanged. Written from
the problem PC5's prospero-win 0077 describes; the expression is tested, not ntdll's heap.

Patch 2417 makes the missing parents of a folder a program creates under `C:\users\<name>\`: a game copied from a PC was never installed
here, so the folders its installer makes there are missing, and Call of Juarez Gunslinger's engine stopped on an uncaught C++ exception when
`AppData\Local\Techland\COJGunslinger` could not be made. `NtCreateFile` makes the parent (which makes its own), then tries again.
`pw_user_folder_parent_length` in `src/pw_user_folder.h` (staged beside `file.c` by `tools/build-wine-ps5.sh`) says where the parent ends and
refuses the users folder, a user's own folder, other drives and folders, and names with empty, `.` or `..` components
(`tests/test_pw_user_folder.c`, in `tools/host-tests.sh`). Written from the problem PC5's prospero-win 0061 describes; the
`NtCreateFile` change compiles on the host but nothing ran on a console.

Patch 2418 lets the controller answer a game's dialog box. The title sends Cross and the D-pad's presses and releases as
`PW_WINE_INPUT_DIALOG_KEY` events (type 9 of `pw_wineserver_input`) carrying Enter's and the arrows' key codes; the server delivers a press as
a key only while the active window is a dialog box (class #32770) and the release of one it delivered, so a game that uses these
buttons itself never gets a stray Enter or arrow. `src/pw_dialog_keys.h` (staged beside `queue.c` by `tools/build-wine-ps5.sh`) holds the key
codes and the deliver-or-drop rule (`tests/test_pw_dialog_keys.c`, in `tools/host-tests.sh`). Only the in-process server takes the event; the
in-title input queue drops it. The console keyboard that PC5 also types text with (an IME dialog and Unicode key events) is not part of
this. Written from the problem PC5's prospero-win 0013 describes; the server code compiles on the host, nothing ran on a console.

Patch 2419 lets the title read the dialog box in front. A game in the title owns the display, so a message box it opens is drawn
by nobody and the game looks hung until the title's watchdog ends it (a survival-horror demo's "Unsupported graphics card",
2026-11-01). `ps5_windows_publish`, on the server thread, records the foreground's active window when it is a dialog box (class
#32770): its caption and each child's text, one per line in UTF-8; `pw_wineserver_dialog` copies that out under the snapshot's
lock. The title polls it once a second while a game runs in the title, logs the box, draws its words over the game
(`PW_STEAM_SCREEN_DIALOG`, `src/pw_steam_screen.c`, through the guide's cover) with Cross to answer (patch 2418), and counts the box as
shown for the watchdog. A window's text change marks the snapshot for rebuilding too.

`tests/d3d9ex-d3d11-share-probe.c` (built by `tools/build-wine-probe.sh`, case `d3d9ex-d3d11-share64` in `tools/run-battery.py`, marker
`PS5_PROTON_D3D9_D3D11_SHARE`) measures what Koei Tecmo's movie players need and DXVK cannot do without Win32 external memory: a Direct3D 9Ex device
makes a shared render-target texture and clears it, a Direct3D 11 device opens its handle with `OpenSharedResource`, copies it and reads a pixel.
It prints `PASS` (the colour arrived), `NOSHARE` (D3D9 or D3D11 refused honestly: what a game sees today, after DXVK patches 0003 and 0005) or
`FAIL`, with each step's HRESULT. It is the measurement a DXVK change needs first; PC5 shares the image inside the process with a change to `d3d9` and
`d3d11` (its patches/dxvk 0001 to 0003), which this project does not copy and has not written. The probe builds for x86-64 with the recipe's
`-Werror`; it has not run (no GPU Wine runs on a host, nothing on a console).

Patch 2421 makes the PS5 high area (patch 0600) as large as the kernel grants. A 64-bit game that reserves tens of GiB at once (28 GiB,
48 GiB, a .NET runtime's 256 GiB) was refused: the area was one 16 GiB reservation, and a view that did not fit fell through to an
anonymous mmap the console refuses. `mmap_init` now asks for 512 GiB at the high base and halves until the kernel grants the size, down to
16 GiB; `PW_PS5_HIGH_RESERVE_GB` gives the size tried first (0 keeps 16 GiB), and a `wine-ps5: high area N GiB` line says what was
granted. It reserves the high area before the view heap's, so the kernel cannot place that inside it. Protection tables already come from the
native heap (0960). `tests/test_pw_high_area.py` cuts the sizing functions out of the patch and checks them against a kernel that grants
at most N bytes. Not run on a console: the first Pro run reads the `high area` line and tries a game that asks for more than 16 GiB at once.

Patch 2422 stops `UnhandledExceptionFilter` from starting a debugger in a PS5 title or helper (the starter sets `WINE_PS5_NTDLL_DIR` for both). The runtime has no
winedbg, and the start reads AeDebug and allocates from the process heap, which a crashing thread's neighbour may hold: the crash froze the title instead of ending
it. The game's own filter still runs first; then the exception is logged ("no debugger starts in a PS5 title") and the thread's handler ends the process.
`tests/test_pw_no_debugger.py` cuts `ps5_no_debugger` out of the patch and runs it against a fake environment, and checks the order in the filter.

Patch 2423 tells a game's windows when its controller connects or disconnects: the PS5 driver's `ProcessEvents` posts `WM_DEVICECHANGE` with `DBT_DEVNODES_CHANGED` to
the top-level windows when `pw_wine_pad_changes` (`wine/ps5/pw_wine_sink.[ch]`) moved. The title counts each connect or disconnect as it sets the pad (and wakes the input
fd); a helper counts the differences between its looks at the input page, the first look being the baseline. `tests/test_pw_wine_sink.c` covers both. The controller stays connected while the system
has the DualSense (pw-01), so the overlay posts nothing. If the plug and play service already sends this in our prefix, a game gets it twice; the Pro probe is a Win32 program
logging `WM_DEVICECHANGE` while the DualSense is switched off and on.

Patch 2424 lets the title type text into the game as Unicode keys: a new input type (`PW_WINE_INPUT_CHAR`, 10) carries one UTF-16 unit, which the server queues as a
`KEYEVENTF_UNICODE` key (a VK_PACKET key, then WM_CHAR) for the focused window and the in-process driver sends the same way, so a line with accents, Cyrillic or CJK arrives intact where
virtual keys could not. It is the delivery half of the on-screen keyboard (`src/pw_osk.[ch]`, `native/pw_osk_ps5.[ch]`; Settings > Controls > On-screen keyboard, Off by default, saved as a
`keyboard <gesture>` line of controls.txt only when chosen). `tests/test_pw_osk.c` covers each gesture (and that a single stick press, one finger or a gone pad fire nothing), the pad hand-over,
the typing and the dialog's sequence against a fake console. The Settings row, its ten languages, the WebUI field and the title's wiring are the next patch.

Patch 2426 gives another process pages and makes the log say what a program learns of it. The server counts a process's memory from /proc, which the console lacks, so every process but the
caller's reported a working set of zero; `NtQueryInformationProcess(ProcessVmCounters)` now reports a floor (16 MiB working set, 256 MiB virtual) when the server has nothing, because a live
Windows process always holds pages. And the +process trace shows who a program asks about and what it was told: `NtOpenProcess` logs the pid, access and result, and a query on a process that is
not the caller's logs one line per distinct answer (pid, parent, exit status; working set; wow64; image name; or the failing status). Written after a tester's Witcher 3 (DirectX 12) log, where
the game inspects another process and ends itself with exit code 0; `tests/test_pw_foreign_process.py` runs the two functions cut from the patch. Not run on a console.

Patch 2427 answers `NtQueryInformationProcess(ProcessCommandLineInformation)` (class 60), which was not known here at all: every caller got `STATUS_INVALID_INFO_CLASS`. It
answers as Windows does, a `UNICODE_STRING` followed by the text: the caller's own command line from its parameters, another process's through `NtReadVirtualMemory` along PEB,
ProcessParameters and CommandLine; a buffer too small gets `STATUS_INFO_LENGTH_MISMATCH` and the needed length. The gate's chromium-game reads its child's command line this way
(alpha7-gate-pro-2026-11-01 failed on it); `tests/test_pw_command_line.py` runs the answer's layout cut from the patch.
