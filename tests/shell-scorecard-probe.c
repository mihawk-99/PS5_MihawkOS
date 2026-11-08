/* SPDX-License-Identifier: GPL-3.0-or-later
 * The shell scorecard's console half (tools/run-shell-scorecard.py): run in the desktop session (run-console.py
 * --profile mihawk-os), it puts the native shell in each state the approved prototype's reference set has
 * (.impeccable/reference-1920/manifest.json, bbbf3c6) that the native shell can reach, in light then dark, and has the
 * title capture the display in each. Windows go where the prototype puts them (tools/measure-prototype-windows.mjs),
 * the pointer where the prototype's interaction put it; a state with no pointer has it in the top right corner.
 *
 * Each capture logs "shell-scorecard capture=N state=S theme=T pointer=X,Y" before it is taken; a state whose surface
 * did not open logs "shell-scorecard missing state=S theme=T reason=R" and takes no capture. The host compares and
 * masks; this program only reaches states. It makes disposable fixtures (Read me.txt on the Desktop, in the prototype's
 * cell for it, and a shortcut in TEMP), shows Start's default pins, and puts the user's pins, icon cells, the fixtures
 * and the theme back at the end.
 *
 *   shell-scorecard64-probe --capture=Z:\data\ps5proton\logs\game-frame.ppm
 *
 * Prints PS5_PROTON_SHELL_SCORECARD done captures=N missing=M. */
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <dwmapi.h>
#include <string.h>
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static unsigned captures, missing;
static const char *theme = "light";
static char capture_base[MAX_PATH];
static const WCHAR pin_key[] = L"Software\\Mihawk\\Shell";
static WCHAR readme[MAX_PATH], shortcut[MAX_PATH];
static HWND tray;

static void line(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}
static void pump(DWORD milliseconds)
{
    DWORD start = GetTickCount();
    do { MSG message; while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); } Sleep(10); }
    while (GetTickCount() - start < milliseconds);
}
/* The title saves a complete numbered PPM after its displayed flip (as tests/shell-surfaces-probe.c asks for it). */
static BOOL capture_display(unsigned number)
{
    char stem[MAX_PATH], path[MAX_PATH], request[MAX_PATH], *dot = NULL;
    if (!capture_base[0] || lstrlenA(capture_base) >= MAX_PATH - 24) return FALSE;
    lstrcpyA(stem, capture_base);
    for (char *c = stem; *c; ++c) if (*c == '.') dot = c;
    if (dot) *dot = 0;
    wsprintfA(path, "%s-%02u.ppm", stem, number); wsprintfA(request, "%s.broker-request", capture_base);
    DeleteFileA(path);
    HANDLE file = CreateFileA(request, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || !CloseHandle(file)) return FALSE;
    DWORD started = GetTickCount(); unsigned complete_ticks = 0;
    while (GetTickCount() - started < 10000)
    {
        WIN32_FILE_ATTRIBUTE_DATA image;
        BOOL complete = GetFileAttributesExA(path, GetFileExInfoStandard, &image) && !image.nFileSizeHigh &&
                        image.nFileSizeLow == 1920 * 1080 * 3 + 17;
        if (complete && ++complete_ticks == 3) return TRUE;
        if (!complete) complete_ticks = 0;
        pump(20);
    }
    DeleteFileA(request); return FALSE;
}
static void shot(const char *state)
{
    char text[160]; POINT pointer = {0, 0};
    pump(300); GetCursorPos(&pointer);
    wsprintfA(text, "shell-scorecard capture=%u state=%s theme=%s pointer=%ld,%ld\n", ++captures, state, theme, pointer.x, pointer.y);
    line(text);
    if (!capture_display(captures)) { wsprintfA(text, "shell-scorecard note: capture %u timed out\n", captures); line(text); }
}
static void point(int x, int y);
static void park(void);
/* The title takes a capture request up only about once a second, so a hover state's frame is drawn long after its
 * 250 ms: the item's tooltip (500 ms) is up by then and the reference's is not. Hiding the tooltip control keeps the
 * state's own plate and removes the tip the reference does not have; no mouse event follows, so it stays hidden. */
static BOOL CALLBACK hide_tip(HWND window, LPARAM unused)
{
    WCHAR name[64];
    (void)unused;
    if (IsWindowVisible(window) && GetClassNameW(window, name, ARRAY_SIZE(name)) && !lstrcmpiW(name, L"tooltips_class32"))
        ShowWindow(window, SW_HIDE);
    return TRUE;
}
static void hide_tooltips(void) { EnumWindows(hide_tip, 0); }

/* Capture a hover state without the item's tooltip: the title picks a capture request up only about once a second,
 * so the frame is drawn after the 500 ms tip delay (Start's said "Start" in every capture), while the reference's
 * 250 ms hover has no tip. Pump past the delay, hide the tip, then request. */
static void shot_soon(const char *state)
{
    char text[160]; POINT pointer = {0, 0};
    pump(600); hide_tooltips(); GetCursorPos(&pointer);
    wsprintfA(text, "shell-scorecard capture=%u state=%s theme=%s pointer=%ld,%ld\n", ++captures, state, theme, pointer.x, pointer.y);
    line(text);
    if (!capture_display(captures)) { wsprintfA(text, "shell-scorecard note: capture %u timed out\n", captures); line(text); }
}
/* The pointer parked first dismisses any tip the previous state raised; 450 ms lets comctl32's 100 ms reshow window
 * (a tip within 400 ms of another closing) expire, so this hover waits the initial 500 ms like the reference's. */
static void hover(const char *state, int x, int y)
{
    park(); pump(450); point(x, y); pump(200); shot_soon(state);
}

static void absent(const char *state, const char *reason)
{
    char text[200]; ++missing;
    wsprintfA(text, "shell-scorecard missing state=%s theme=%s reason=%s\n", state, theme, reason); line(text);
}

/* ---------- input, as a person's hand sends it ---------- */
static void mouse(DWORD flags, int x, int y)
{
    INPUT input = {0};
    input.type = INPUT_MOUSE; input.mi.dx = x * 65535 / 1919; input.mi.dy = y * 65535 / 1079;
    input.mi.dwFlags = flags | MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &input, sizeof(input));
}
static void point(int x, int y) { mouse(0, x, y); pump(60); }
static void park(void) { point(1910, 8); }
static void click(int x, int y) { point(x, y); mouse(MOUSEEVENTF_LEFTDOWN, x, y); pump(60); mouse(MOUSEEVENTF_LEFTUP, x, y); pump(200); }
static void right_click(int x, int y) { point(x, y); mouse(MOUSEEVENTF_RIGHTDOWN, x, y); pump(60); mouse(MOUSEEVENTF_RIGHTUP, x, y); pump(300); }
static void key(WORD code, WORD modifier)
{
    INPUT events[4] = {{0}}; unsigned n = 0;
    if (modifier) { events[n].type = INPUT_KEYBOARD; events[n++].ki.wVk = modifier; }
    events[n].type = INPUT_KEYBOARD; events[n++].ki.wVk = code;
    events[n].type = INPUT_KEYBOARD; events[n].ki.wVk = code; events[n++].ki.dwFlags = KEYEVENTF_KEYUP;
    if (modifier) { events[n].type = INPUT_KEYBOARD; events[n].ki.wVk = modifier; events[n++].ki.dwFlags = KEYEVENTF_KEYUP; }
    SendInput(n, events, sizeof(INPUT)); pump(150);
}

/* ---------- windows ---------- */
static HWND wait_class(const WCHAR *cls, const WCHAR *title, BOOL visible, DWORD ms)
{
    DWORD start = GetTickCount(); HWND window;
    do { window = FindWindowW(cls, title); if (window && !!IsWindowVisible(window) == visible) return window; pump(20); }
    while (GetTickCount() - start < ms);
    return visible ? NULL : window;
}
struct owned { DWORD pid; const WCHAR *suffix; HWND owner, window; };
static BOOL CALLBACK find_owned(HWND window, LPARAM param)
{
    struct owned *o = (struct owned *)param; DWORD pid = 0; WCHAR title[MAX_PATH];
    GetWindowThreadProcessId(window, &pid);
    if (pid != o->pid || !IsWindowVisible(window)) return TRUE;
    if (o->owner) { if (GetWindow(window, GW_OWNER) != o->owner) return TRUE; }
    else if (o->suffix)
    {
        int n = GetWindowTextW(window, title, MAX_PATH), s = lstrlenW(o->suffix);
        if (n < s || lstrcmpW(title + n - s, o->suffix)) return TRUE;
    }
    else if (GetWindow(window, GW_OWNER)) return TRUE;
    o->window = window; return FALSE;
}
/* A visible top-level window of a process: unowned, (suffix) any whose title ends with it, or (owner) one it owns. */
static HWND wait_window_of(DWORD pid, const WCHAR *suffix, HWND owner, DWORD ms)
{
    struct owned o = {pid, suffix, owner, NULL}; DWORD start = GetTickCount();
    do { EnumWindows(find_owned, (LPARAM)&o); if (o.window) return o.window; pump(50); } while (GetTickCount() - start < ms);
    return NULL;
}
static HWND wait_owned(DWORD pid, const WCHAR *suffix, DWORD ms) { return wait_window_of(pid, suffix, NULL, ms); }
/* Where the prototype's window is: its visible frame (DWM's bounds, inside the invisible resize border) at x, y and,
 * when cx is not 0, cx x cy. */
static void place(HWND window, int x, int y, int cx, int cy)
{
    RECT outer, visible;
    GetWindowRect(window, &outer);
    if (FAILED(DwmGetWindowAttribute(window, DWMWA_EXTENDED_FRAME_BOUNDS, &visible, sizeof(visible)))) visible = outer;
    if (!cx) { cx = visible.right - visible.left; cy = visible.bottom - visible.top; }
    SetWindowPos(window, HWND_TOP, x - (visible.left - outer.left), y - (visible.top - outer.top),
                 cx + (outer.right - outer.left) - (visible.right - visible.left),
                 cy + (outer.bottom - outer.top) - (visible.bottom - visible.top), SWP_SHOWWINDOW);
    SetForegroundWindow(window); pump(400);
}
static BOOL launch(const WCHAR *image, const WCHAR *argument, PROCESS_INFORMATION *process, DWORD flags)
{
    WCHAR command[3 * MAX_PATH]; STARTUPINFOW startup = {sizeof(startup)};
    if (argument) wsprintfW(command, L"\"%s\" \"%s\"", image, argument); else wsprintfW(command, L"\"%s\"", image);
    return CreateProcessW(NULL, command, NULL, NULL, FALSE, flags, NULL, NULL, &startup, process);
}
static void finish(PROCESS_INFORMATION *process, HWND window)
{
    if (window && IsWindow(window)) PostMessageW(window, WM_CLOSE, 0, 0);
    if (WaitForSingleObject(process->hProcess, 5000) != WAIT_OBJECT_0) TerminateProcess(process->hProcess, 1);
    WaitForSingleObject(process->hProcess, 2000);
    CloseHandle(process->hThread); CloseHandle(process->hProcess); pump(300);
}
static WCHAR *system_path(WCHAR *path, const WCHAR *name, BOOL windows)
{
    if (windows) GetWindowsDirectoryW(path, MAX_PATH); else GetSystemDirectoryW(path, MAX_PATH);
    PathAppendW(path, name); return path;
}
static HWND tray_child(const WCHAR *text)
{
    HWND child = NULL; WCHAR title[64];
    while ((child = FindWindowExW(tray, child, NULL, NULL)))
        if (GetWindowTextW(child, title, ARRAY_SIZE(title)) && !lstrcmpW(title, text) && IsWindowVisible(child)) return child;
    return NULL;
}
static void middle(HWND window, int *x, int *y)
{
    RECT r; GetWindowRect(window, &r); *x = (r.left + r.right) / 2; *y = (r.top + r.bottom) / 2;
}

/* ---------- the theme ---------- */
typedef HRESULT (WINAPI *open_theme_fn)(const WCHAR *, const WCHAR *, const WCHAR *, HANDLE *, DWORD);
typedef HRESULT (WINAPI *apply_theme_fn)(HANDLE, char *, HWND);
typedef HRESULT (WINAPI *close_theme_fn)(HANDLE);
typedef HRESULT (WINAPI *theme_name_fn)(WCHAR *, int, WCHAR *, int, WCHAR *, int);
static HRESULT apply_theme(const WCHAR *path, const WCHAR *colour)
{
    HMODULE module = LoadLibraryW(L"uxtheme.dll"); HANDLE file = NULL; HRESULT result = E_NOTIMPL;
    open_theme_fn open = module ? (open_theme_fn)GetProcAddress(module, MAKEINTRESOURCEA(2)) : NULL;
    apply_theme_fn apply = module ? (apply_theme_fn)GetProcAddress(module, MAKEINTRESOURCEA(4)) : NULL;
    close_theme_fn close = module ? (close_theme_fn)GetProcAddress(module, MAKEINTRESOURCEA(3)) : NULL;
    if (open && apply && close)
    {
        if (!path) result = apply(NULL, NULL, NULL);
        else if (SUCCEEDED(result = open(path, colour, L"NormalSize", &file, 0))) { result = apply(file, NULL, NULL); close(file); }
    }
    pump(500); return result;
}

/* ---------- modal dialogs, each on a thread of its own while this one captures ---------- */
struct box { const WCHAR *title, *text; UINT type; };
static DWORD WINAPI box_thread(void *param)
{
    const struct box *b = param; return MessageBoxW(NULL, b->text, b->title, b->type | MB_SETFOREGROUND);
}
static DWORD WINAPI run_thread(void *unused)
{
    typedef void (WINAPI *run_fn)(HWND, HICON, const WCHAR *, const WCHAR *, const WCHAR *, UINT);
    run_fn run = (run_fn)GetProcAddress(GetModuleHandleW(L"shell32.dll"), MAKEINTRESOURCEA(61));
    (void)unused; if (run) run(NULL, NULL, NULL, NULL, NULL, 0);
    return 0;
}
static DWORD WINAPI open_thread(void *param)
{
    WCHAR file[MAX_PATH] = L""; OPENFILENAMEW ofn = {sizeof(ofn)};
    ofn.lpstrFile = file; ofn.nMaxFile = MAX_PATH; ofn.lpstrFilter = L"Text Documents (*.txt)\0*.txt\0All Files (*.*)\0*.*\0";
    ofn.Flags = OFN_EXPLORER | OFN_ENABLESIZING;
    return param ? GetSaveFileNameW(&ofn) : GetOpenFileNameW(&ofn);
}
/* Wine's SHObjectProperties is a stub (shell-scorecard-2026-10-16-01); the Properties verb opens the sheet on this thread. */
static DWORD WINAPI properties_thread(void *param)
{
    SHELLEXECUTEINFOW execute = {sizeof(execute)};
    execute.fMask = SEE_MASK_INVOKEIDLIST | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    execute.lpVerb = L"properties"; execute.lpFile = param; execute.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW(&execute);
}
/* Task dialogs need comctl32 6: the thread activates a context with the probe's own manifest for it. */
static DWORD WINAPI task_thread(void *unused)
{
    static const char manifest[] = "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\"><dependency><dependentAssembly>"
        "<assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" "
        "processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/></dependentAssembly></dependency></assembly>";
    typedef HRESULT (WINAPI *task_fn)(const TASKDIALOGCONFIG *, int *, int *, BOOL *);
    static const TASKDIALOG_BUTTON buttons[] = {
        {100, L"Play in full screen\nBest performance. Press Win to return to the desktop."},
        {101, L"Play in a window\nKeep the taskbar and other windows in view."}};
    WCHAR path[MAX_PATH]; DWORD written; ULONG_PTR cookie = 0; HANDLE file, context; HMODULE comctl; task_fn task;
    TASKDIALOGCONFIG config = {sizeof(config)}; ACTCTXW actctx = {sizeof(actctx)};
    (void)unused;
    GetTempPathW(MAX_PATH, path); PathAppendW(path, L"mihawk-scorecard.manifest");
    file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    if (file == INVALID_HANDLE_VALUE) return 0;
    WriteFile(file, manifest, sizeof(manifest) - 1, &written, NULL); CloseHandle(file);
    actctx.lpSource = path;
    if ((context = CreateActCtxW(&actctx)) == INVALID_HANDLE_VALUE) return 0;
    ActivateActCtx(context, &cookie);
    if ((comctl = LoadLibraryW(L"comctl32.dll")) && (task = (task_fn)GetProcAddress(comctl, "TaskDialogIndirect")))
    {
        config.dwFlags = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION | TDF_USE_HICON_MAIN;
        config.pszWindowTitle = L"Mihawk OS"; config.hMainIcon = LoadIconW(NULL, (const WCHAR *)IDI_QUESTION);
        config.pszMainInstruction = L"How do you want to start Example Game?";
        config.pszContent = L"The game can take the whole screen or run in a window on the desktop.";
        config.pButtons = buttons; config.cButtons = ARRAY_SIZE(buttons);
        config.pszVerificationText = L"Don't ask me again";
        task(&config, NULL, NULL, NULL);
    }
    DeactivateActCtx(0, cookie); ReleaseActCtx(context); DeleteFileW(path);
    return 0;
}
/* The dialog a thread opens, whose title ends with title: moved where the prototype's is, captured, cancelled. */
static void dialog(const char *state, LPTHREAD_START_ROUTINE run, void *param, const WCHAR *title, int x, int y, int cx, int cy)
{
    HANDLE thread = CreateThread(NULL, 0, run, param, 0, NULL);
    HWND window = thread ? wait_owned(GetCurrentProcessId(), title, 6000) : NULL;
    if (!window) absent(state, "the dialog did not open");
    else { place(window, x, y, cx, cy); park(); shot(state); PostMessageW(window, WM_COMMAND, IDCANCEL, 0); }
    if (thread && WaitForSingleObject(thread, 4000) != WAIT_OBJECT_0 && window) { PostMessageW(window, WM_CLOSE, 0, 0); WaitForSingleObject(thread, 3000); }
    if (thread) CloseHandle(thread);
    pump(200);
}

/* ---------- the states ---------- */
static BOOL CALLBACK find_start(HWND child, LPARAM param)
{
    WCHAR title[32]; GetWindowTextW(child, title, 32);
    if (!lstrcmpW(title, L"Start")) *(HWND *)param = child;
    return TRUE;
}
static void taskbar_states(void)
{
    HWND start = NULL, panel, pin; int x, y;
    park(); shot("desktop");
    EnumChildWindows(tray, find_start, (LPARAM)&start);
    if (!start) { absent("taskbar-start-hover", "no Start button"); return; }
    middle(start, &x, &y);
    hover("taskbar-start-hover", x, y);
    park(); pump(450); point(x, y); mouse(MOUSEEVENTF_LEFTDOWN, x, y); pump(80); shot_soon("taskbar-start-pressed");
    mouse(MOUSEEVENTF_LEFTUP, x, y);
    if (!(panel = wait_class(L"MihawkStart", NULL, TRUE, 5000))) { absent("start-opened-by-click", "Start did not open"); return; }
    pump(300); shot("start-opened-by-click");
    if ((pin = GetDlgItem(panel, 200)) && IsWindowVisible(pin)) { middle(pin, &x, &y); hover("start-pin-hover", x, y); }
    else absent("start-pin-hover", "Start has no first pin");
    park(); pump(250); shot("start");
    PostMessageW(panel, WM_CLOSE, 0, 0); wait_class(L"MihawkStart", NULL, FALSE, 3000);
}
/* A taskbar button's flyout, opened by a click as the pointer makes it (tests/shell-surfaces-probe.c), the pointer away. */
static void flyout(const char *state, UINT id, const WCHAR *cls)
{
    HWND button = GetDlgItem(tray, id), window;
    if (!button) { absent(state, "no taskbar button"); return; }
    PostMessageW(button, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(4, 4)); PostMessageW(button, WM_LBUTTONUP, 0, MAKELPARAM(4, 4));
    if (!(window = wait_class(cls, NULL, TRUE, 5000))) { absent(state, "the flyout did not open"); return; }
    shot(state);
    PostMessageW(window, WM_CLOSE, 0, 0); wait_class(cls, NULL, FALSE, 3000);
}
static BOOL menu_open(DWORD ms) { return wait_class(L"#32768", NULL, TRUE, ms) != NULL; }
static void menu_close(void) { for (int i = 0; i < 3 && wait_class(L"#32768", NULL, TRUE, 50); ++i) key(VK_ESCAPE, 0); pump(200); }
/* The desktop's items at the prototype's grid (Recycle Bin second: its icon's box 2,93 73 x 73). */
/* The desktop list's tooltip, which shows its infotips (patch 1211). */
static HWND desktop_tip(void)
{
    HWND view = FindWindowExW(FindWindowExW(FindWindowW(L"Progman", NULL), NULL, L"SHELLDLL_DefView", NULL), NULL,
                              L"SysListView32", NULL);
    return view ? (HWND)SendMessageW(view, LVM_GETTOOLTIPS, 0, 0) : NULL;
}
static void desktop_states(void)
{
    HWND tip;
    click(960, 500); park();
    /* the reference's pointer is the plate's centre, 38.5,129.5, whose tip the prototype places at 39,150. The title takes
     * a capture up within 120 ticks of the ask (a second at 120 Hz), after the tip's 500 ms, so the hover is shot after
     * the tip's autopop (5 s), which leaves it hidden while the pointer rests */
    point(39, 130); pump(700); shot("desktop-icon-infotip");
    tip = desktop_tip();
    for (DWORD start = GetTickCount(); tip && IsWindowVisible(tip) && GetTickCount() - start < 7000;) pump(20);
    if (tip && IsWindowVisible(tip)) line("shell-scorecard note: the infotip did not pop\n");
    shot("desktop-icon-hover");
    click(38, 129); park(); pump(250); shot("desktop-icon-selected");
    point(400, 40); mouse(MOUSEEVENTF_LEFTDOWN, 400, 40); pump(60);
    for (int i = 1; i <= 10; ++i) { mouse(0, 400 - 38 * i, 40 + 29 * i); pump(30); }
    pump(200); shot("desktop-rubber-band");
    mouse(MOUSEEVENTF_LEFTUP, 20, 330); park(); pump(250); shot("desktop-multi-selected");
    click(960, 500);
    /* as the reference set was made (the design worktree's capture.mjs): the menu at 600,400; then another at 700,400
     * with the pointer on its first item, View, for 700 ms, whose submenu opens on hover (the prototype's View row is
     * 605,403 257 x 32 in a menu at 600,400) */
    right_click(600, 400);
    if (menu_open(3000)) { park(); shot("menu-desktop"); } else absent("menu-desktop", "no menu");
    menu_close();
    right_click(700, 400);
    if (menu_open(3000)) { point(833, 419); pump(700); shot("menu-desktop-view-submenu"); }
    else absent("menu-desktop-view-submenu", "no menu");
    menu_close();
    /* the first file's menu: Read me selected by typing its name ("Re" alone is the Recycle Bin), then the menu key's
     * menu (the prototype's keyboardMenu(false); Shift+F10 is its classic menu) */
    click(960, 500); key('R', 0); key('E', 0); key('A', 0); key(VK_APPS, 0);
    if (menu_open(3000)) { park(); shot("menu-desktop-item"); } else absent("menu-desktop-item", "no menu");
    menu_close(); click(960, 500); park();
}
static void explorer_states(void)
{
    WCHAR path[MAX_PATH]; PROCESS_INFORMATION process; HWND window, button; int x, y;
    static const struct { const char *state; int x; } captions[] = {
        {"caption-minimize-hover", 1394}, {"caption-maximize-hover", 1440}, {"caption-close-hover", 1486}};
    if (!launch(system_path(path, L"explorer.exe", TRUE), NULL, &process, 0)) { absent("explorer", "explorer.exe did not start"); return; }
    if (!(window = wait_owned(process.dwProcessId, NULL, 15000))) { absent("explorer", "no window"); finish(&process, NULL); return; }
    place(window, 410, 166, 1100, 700); park(); pump(500); shot("explorer");
    for (unsigned i = 0; i < ARRAY_SIZE(captions); ++i) { point(captions[i].x, 191); pump(250); shot(captions[i].state); }
    mouse(MOUSEEVENTF_LEFTDOWN, 1486, 191); pump(150); shot("caption-close-pressed");
    point(900, 500); mouse(MOUSEEVENTF_LEFTUP, 900, 500); pump(200); park();
    /* the reference's taskbar states from here on have File Explorer open */
    if ((button = tray_child(L"Settings"))) { middle(button, &x, &y); point(x, y); pump(250); shot("taskbar-app-hover"); park(); pump(300); }
    else absent("taskbar-app-hover", "no Settings pin");
    if ((button = tray_child(L"File Explorer")))
    {
        middle(button, &x, &y); point(x, y);
        if (wait_class(L"MihawkTaskbarPreview", NULL, TRUE, 3000)) shot("taskbar-thumbs"); else absent("taskbar-thumbs", "no preview");
        park(); pump(500);
    }
    else absent("taskbar-thumbs", "no File Explorer button");
    /* Notepad active over the inactive File Explorer, then alone */
    PROCESS_INFORMATION notepad; HWND note;
    if (launch(system_path(path, L"notepad.exe", FALSE), readme, &notepad, 0))
    {
        if ((note = wait_owned(notepad.dwProcessId, NULL, 10000)))
        {
            place(note, 512, 228, 960, 640); park(); shot("windows-active-inactive");
            PostMessageW(window, WM_CLOSE, 0, 0); WaitForSingleObject(process.hProcess, 5000); window = NULL;
            SetForegroundWindow(note); park(); pump(300); shot("notepad");
        }
        else { absent("windows-active-inactive", "no Notepad window"); absent("notepad", "no window"); }
        finish(&notepad, note);
    }
    else { absent("windows-active-inactive", "notepad.exe did not start"); absent("notepad", "notepad.exe did not start"); }
    finish(&process, window);
}
static void notepad_prompt(void)
{
    WCHAR path[MAX_PATH]; PROCESS_INFORMATION process; HWND window, edit, prompt;
    if (!launch(system_path(path, L"notepad.exe", FALSE), NULL, &process, 0)) { absent("notepad-save-prompt", "notepad.exe did not start"); return; }
    if ((window = wait_owned(process.dwProcessId, NULL, 10000)) && (edit = FindWindowExW(window, NULL, L"Edit", NULL)))
    {
        place(window, 480, 196, 960, 640);
        SendMessageW(edit, WM_SETTEXT, 0, (LPARAM)L"Welcome to Mihawk OS."); SendMessageW(edit, EM_SETMODIFY, TRUE, 0);
        PostMessageW(window, WM_CLOSE, 0, 0);
        if ((prompt = wait_window_of(process.dwProcessId, NULL, window, 5000)))  /* the main window's message box */
        { park(); shot("notepad-save-prompt"); PostMessageW(prompt, WM_COMMAND, IDNO, 0); }
        else absent("notepad-save-prompt", "no prompt");
    }
    else absent("notepad-save-prompt", "no window");
    finish(&process, NULL);
}
static void dialog_states(void)
{
    /* the prototype's three message boxes, in its words, where it puts them */
    static const struct box save = {L"Notepad", L"The text in the Untitled file has changed.\n\nDo you want to save the changes?", MB_YESNOCANCEL | MB_ICONWARNING};
    static const struct box error = {L"launcher.exe", L"The code execution cannot proceed because launcher64.dll was not found. Reinstalling the program may fix this problem.", MB_OK | MB_ICONERROR};
    static const struct box info = {L"Mihawk OS", L"The prefix for Example Game is ready.", MB_OK | MB_ICONINFORMATION};
    WCHAR documents[MAX_PATH];
    dialog("msgbox", box_thread, (void *)&save, L"Notepad", 804, 425, 0, 0);
    dialog("msgbox-error", box_thread, (void *)&error, L"launcher.exe", 590, 433, 0, 0);
    dialog("msgbox-info", box_thread, (void *)&info, L"Mihawk OS", 812, 433, 0, 0);
    dialog("taskdialog", task_thread, NULL, L"Mihawk OS", 740, 361, 0, 0);
    dialog("run", run_thread, NULL, L"Run", 12, 815, 0, 0);
    dialog("fileopen", open_thread, NULL, L"Open", 480, 216, 960, 600);
    dialog("filesave", open_thread, (void *)1, L"Save As", 480, 216, 960, 600);
    dialog("properties", properties_thread, readme, L" Properties", 770, 297, 0, 0);
    if (SUCCEEDED(SHGetFolderPathW(NULL, CSIDL_PERSONAL, NULL, 0, documents)))
        dialog("properties-folder", properties_thread, documents, L" Properties", 770, 311, 0, 0);
    else absent("properties-folder", "no Documents");
    dialog("properties-drive", properties_thread, (void *)L"C:\\", L" Properties", 770, 258, 0, 0);
    if (shortcut[0]) dialog("properties-shortcut", properties_thread, shortcut, L" Properties", 770, 300, 0, 0);
    else absent("properties-shortcut", "no shortcut fixture");
}
static void app_states(void)
{
    WCHAR path[MAX_PATH]; PROCESS_INFORMATION process; HWND window, tabs;
    if (launch(system_path(path, L"mihawk-settings.exe", FALSE), L"personalization/colors", &process, 0))
    {
        if ((window = wait_owned(process.dwProcessId, NULL, 10000)))
        {
            place(window, 370, 116, 1180, 800); park(); shot("settings");
            ShowWindow(window, SW_MAXIMIZE); pump(500); park(); shot("window-maximized");
        }
        else absent("settings", "no window");
        finish(&process, window);
    }
    else absent("settings", "mihawk-settings.exe did not start");
    if (launch(system_path(path, L"cmd.exe", FALSE), NULL, &process, CREATE_NEW_CONSOLE))
    {
        if ((window = wait_class(L"WineConsoleClass", NULL, TRUE, 10000))) { place(window, 470, 236, 980, 560); park(); shot("terminal"); }
        else absent("terminal", "no console window");
        TerminateProcess(process.hProcess, 0); finish(&process, NULL);
    }
    else absent("terminal", "cmd.exe did not start");
    if (launch(system_path(path, L"taskmgr.exe", FALSE), NULL, &process, 0))
    {
        if ((window = wait_owned(process.dwProcessId, NULL, 10000)))
        {
            place(window, 480, 166, 960, 700); park(); shot("taskmgr");
            /* Performance, the third tab: focus the tab control with a click on its first tab, then arrows */
            if ((tabs = FindWindowExW(window, NULL, WC_TABCONTROLW, NULL)))
            {
                RECT r; GetWindowRect(tabs, &r); click(r.left + 24, r.top + 10); key(VK_RIGHT, 0); key(VK_RIGHT, 0);
                park(); pump(500); shot("taskmgr-perf");
            }
            else absent("taskmgr-perf", "no tabs");
        }
        else absent("taskmgr", "no window");
        finish(&process, window);
    }
    else absent("taskmgr", "taskmgr.exe did not start");
}
static void states(void)
{
    taskbar_states();
    flyout("search", 0xffe0, L"MihawkStart");   /* the taskbar's fixed buttons (patch 1140): search, status, clock, chevron */
    flyout("quicksettings", 0xffe3, L"MihawkQuickSettings");
    flyout("notifications", 0xfff1, L"MihawkNotifications");
    flyout("taskbar-tray", 0xffe2, L"MihawkHiddenIcons");
    desktop_states();
    explorer_states();
    notepad_prompt();
    dialog_states();
    app_states();
}

/* ---------- fixtures ---------- */
static BOOL write_readme(void)
{
    static const char text[] = "Welcome to Mihawk OS.\r\n\r\nThis is a prototype of the Mihawk 11 desktop for the PS5 Wine runtime.";
    WCHAR desktop[MAX_PATH]; DWORD written; HANDLE file; BOOL okay;
    if (FAILED(SHGetFolderPathW(NULL, CSIDL_DESKTOPDIRECTORY, NULL, 0, desktop))) return FALSE;
    PathCombineW(readme, desktop, L"Read me.txt");
    if ((file = CreateFileW(readme, GENERIC_WRITE, 0, NULL, CREATE_NEW, 0, NULL)) == INVALID_HANDLE_VALUE) { readme[0] = 0; return FALSE; }
    okay = WriteFile(file, text, sizeof(text) - 1, &written, NULL); CloseHandle(file); return okay;
}
static BOOL write_shortcut(void)
{
    IShellLinkW *link; IPersistFile *persist; WCHAR target[MAX_PATH]; BOOL okay = FALSE;
    GetTempPathW(MAX_PATH, shortcut); PathAppendW(shortcut, L"Mihawk scorecard Notepad.lnk");
    if (FAILED(CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link))) { shortcut[0] = 0; return FALSE; }
    IShellLinkW_SetPath(link, system_path(target, L"notepad.exe", FALSE));
    if (SUCCEEDED(IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&persist)))
    { okay = SUCCEEDED(IPersistFile_Save(persist, shortcut, TRUE)); IPersistFile_Release(persist); }
    IShellLinkW_Release(link);
    if (!okay) shortcut[0] = 0;
    return okay;
}

/* "Read me" in the prototype's cell for it (its seventh: column 0, row 6, the item menu's place): its saved cell first in
 * the desktop's IconPositions (Wine patch 1102: version 2, then per icon its column, row, name length and name, DWORD
 * aligned; the first saved claim on a cell wins), then the desktop refreshed, which places icons from them again. */
static const WCHAR positions_key[] = L"Software\\Mihawk\\Shell\\Desktop";
static BYTE *positions_saved;
static DWORD positions_size, positions_type;
static LONG positions_result = ERROR_FILE_NOT_FOUND;
static void refresh_desktop(void) { click(960, 500); key(VK_F5, 0); pump(1500); park(); }
static void pin_readme(void)
{
    static const WCHAR name[] = L"Read me.txt";
    const DWORD length = ARRAY_SIZE(name) - 1, entry = 3 * sizeof(DWORD) + ((length * sizeof(WCHAR) + 3) & ~3u);
    DWORD rest = 0, *pin; BYTE *value;
    positions_result = RegGetValueW(HKEY_CURRENT_USER, positions_key, L"IconPositions", RRF_RT_REG_BINARY, &positions_type, NULL, &positions_size);
    if (positions_result == ERROR_SUCCESS && (!(positions_saved = HeapAlloc(GetProcessHeap(), 0, positions_size)) ||
        RegGetValueW(HKEY_CURRENT_USER, positions_key, L"IconPositions", RRF_RT_REG_BINARY, &positions_type, positions_saved, &positions_size)))
    { positions_result = ERROR_INVALID_DATA; line("shell-scorecard note: the desktop's icon cells could not be saved; Read me stays where it is\n"); return; }
    if (positions_result == ERROR_SUCCESS && positions_size > sizeof(DWORD) && *(DWORD *)positions_saved == 2) rest = positions_size - sizeof(DWORD);
    if (!(value = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(DWORD) + entry + rest))) return;
    pin = (DWORD *)value; pin[0] = 2; pin[1] = 0; pin[2] = 6; pin[3] = length;
    memcpy(pin + 4, name, length * sizeof(WCHAR));
    if (rest) memcpy(value + sizeof(DWORD) + entry, positions_saved + sizeof(DWORD), rest);
    RegSetKeyValueW(HKEY_CURRENT_USER, positions_key, L"IconPositions", REG_BINARY, value, sizeof(DWORD) + entry + rest);
    HeapFree(GetProcessHeap(), 0, value);
    refresh_desktop();
}
/* The user's cells back, and the desktop placed from them. */
static void unpin_readme(void)
{
    HKEY key_handle;
    if (positions_result == ERROR_SUCCESS)
        RegSetKeyValueW(HKEY_CURRENT_USER, positions_key, L"IconPositions", positions_type, positions_saved, positions_size);
    else if (positions_result == ERROR_FILE_NOT_FOUND && !RegOpenKeyExW(HKEY_CURRENT_USER, positions_key, 0, KEY_SET_VALUE, &key_handle))
    { RegDeleteValueW(key_handle, L"IconPositions"); RegCloseKey(key_handle); }
    else return;
    refresh_desktop();
}

void mainCRTStartup(void)
{
    const char *arguments = GetCommandLineA(); char text[160]; HWND shell = NULL; HKEY key_handle;
    WCHAR managed[MAX_PATH], prior_file[MAX_PATH], prior_colour[64], prior_size[64];
    for (const char *c = arguments; *c; ++c)
        if ((c == arguments || c[-1] == ' ') && !strncmp(c, "--capture=", 10))
        {
            const char *value = c + 10, *end = value;
            while (*end && *end != ' ') ++end;
            if (end - value >= MAX_PATH - 24) ExitProcess(2);
            lstrcpynA(capture_base, value, (int)(end - value) + 1); break;
        }
    DWORD begun = GetTickCount();
    do { pump(100); shell = GetShellWindow(); } while (!IsWindow(shell) && GetTickCount() - begun < 15000);
    if (!(tray = wait_class(L"Shell_TrayWnd", NULL, TRUE, 10000))) { line("PS5_PROTON_SHELL_SCORECARD failed: no taskbar\n"); ExitProcess(1); }
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    HMODULE themes = LoadLibraryW(L"uxtheme.dll");
    theme_name_fn theme_name = themes ? (theme_name_fn)GetProcAddress(themes, "GetCurrentThemeName") : NULL;
    BOOL prior = theme_name && SUCCEEDED(theme_name(prior_file, MAX_PATH, prior_colour, 64, prior_size, 64));
    /* Start shows its default pins for the captures; the user's come back afterwards. */
    BYTE *saved = NULL; DWORD saved_size = 0, saved_type = 0;
    LONG saved_result = RegGetValueW(HKEY_CURRENT_USER, pin_key, L"StartPins", RRF_RT_ANY, &saved_type, NULL, &saved_size);
    if (saved_result == ERROR_SUCCESS && (!(saved = HeapAlloc(GetProcessHeap(), 0, saved_size)) ||
        RegGetValueW(HKEY_CURRENT_USER, pin_key, L"StartPins", RRF_RT_ANY, &saved_type, saved, &saved_size)))
    { line("PS5_PROTON_SHELL_SCORECARD failed: the user's pins could not be saved\n"); ExitProcess(1); }
    if (saved_result == ERROR_SUCCESS && !RegOpenKeyExW(HKEY_CURRENT_USER, pin_key, 0, KEY_SET_VALUE, &key_handle))
    { RegDeleteValueW(key_handle, L"StartPins"); RegCloseKey(key_handle); }
    if (!write_readme()) line("shell-scorecard note: no Read me.txt fixture on the Desktop\n");
    if (!write_shortcut()) line("shell-scorecard note: no shortcut fixture\n");
    pump(1500);  /* the desktop shows the new file */
    if (readme[0]) pin_readme();
    system_path(managed, L"resources\\themes\\light\\light.msstyles", TRUE);
    for (int pass = 0; pass < 2; ++pass)
    {
        theme = pass ? "dark" : "light";
        if (FAILED(apply_theme(managed, pass ? L"Dark" : L"Blue"))) { absent("all", "the managed theme did not apply"); continue; }
        pump(1000);
        states();
    }
    if (readme[0]) DeleteFileW(readme);
    unpin_readme();
    if (shortcut[0]) DeleteFileW(shortcut);
    if (saved_result == ERROR_SUCCESS && !RegCreateKeyExW(HKEY_CURRENT_USER, pin_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key_handle, NULL))
    { RegSetValueExW(key_handle, L"StartPins", 0, saved_type, saved, saved_size); RegCloseKey(key_handle); }
    apply_theme(prior ? prior_file : NULL, prior_colour);
    wsprintfA(text, "PS5_PROTON_SHELL_SCORECARD done captures=%u missing=%u\n", captures, missing); line(text);
    if (IsWindow(shell)) PostMessageW(shell, WM_CLOSE, 0, 0);
    ExitProcess(0);
}
