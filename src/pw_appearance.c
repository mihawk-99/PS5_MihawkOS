/* SPDX-License-Identifier: GPL-3.0-or-later
 * A real Wine appearance selector. Wine's uxtheme remains authoritative:
 * ApplyTheme persists ThemeManager and broadcasts WM_THEMECHANGED to existing
 * windows. Alternate themes are not replaced on startup. The diagnostic mode
 * owns only the probe prefix and restores its prior theme before exiting.
 * Alpha icons are resource data shared by PE32/PE64, never executable imports.
 */
#define UNICODE
#include <windows.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <vssym32.h>
#include "appearance.h"
#include "icons.h"

typedef HRESULT (WINAPI *open_theme_fn)(const WCHAR *, const WCHAR *, const WCHAR *, HANDLE *, DWORD);
typedef HRESULT (WINAPI *apply_theme_fn)(HANDLE, char *, HWND);
typedef HRESULT (WINAPI *close_theme_fn)(HANDLE);
static HMODULE themes, icons;
static HWND window, light_button, dark_button, status_label;
static HFONT font;
static BOOL dark, labels = TRUE, probing;
static unsigned checks, failures;
static WCHAR prior_file[MAX_PATH], prior_color[64], prior_size[64];
static HRESULT prior_theme;
static WCHAR capture_base[MAX_PATH], capture_path[MAX_PATH], capture_request[MAX_PATH];
static DWORD capture_started;
static unsigned capture_phase, capture_complete_ticks;
static HICON objects[14], commands[34];
static HICON caption_icon;
static const WCHAR *object_names[] = {L"Folder", L"Documents", L"Downloads", L"Pictures", L"Music", L"Videos",
    L"Desktop", L"Games", L"Open folder", L"This PC", L"Empty bin", L"Full bin", L"Drive", L"Document"};

static void check(BOOL passed, const char *name)
{
    checks++; failures += !passed;
    if (!passed) {
        char line[180]; wsprintfA(line, "appearance: FAIL %s error=%lu\n", name, GetLastError());
        OutputDebugStringA(line);
    }
}

static HRESULT apply(const WCHAR *path, const WCHAR *color, const WCHAR *size)
{
    open_theme_fn open_theme = (open_theme_fn)GetProcAddress(themes, MAKEINTRESOURCEA(2));
    close_theme_fn close_theme = (close_theme_fn)GetProcAddress(themes, MAKEINTRESOURCEA(3));
    apply_theme_fn apply_theme = (apply_theme_fn)GetProcAddress(themes, MAKEINTRESOURCEA(4));
    HANDLE file = NULL;
    HRESULT result;
    if (!open_theme || !close_theme || !apply_theme) return E_NOTIMPL;
    if (!path) return apply_theme(NULL, NULL, window);
    result = open_theme(path, color, size, &file, 0);
    if (SUCCEEDED(result)) { result = apply_theme(file, NULL, window); close_theme(file); }
    return result;
}

static void update_icons(void)
{
    for (unsigned i = 0; i < 34; i++) {
        if (commands[i]) DestroyIcon(commands[i]);
        commands[i] = LoadImageW(icons, MAKEINTRESOURCEW((dark ? 3000 : 2000) + i), IMAGE_ICON, 24, 24, 0);
    }
    HICON previous = caption_icon;
    caption_icon = LoadImageW(icons, MAKEINTRESOURCEW(dark ? PW_ICON_DARK_SETTINGS : PW_ICON_LIGHT_SETTINGS),
                              IMAGE_ICON, 20, 20, 0);
    if (window) SendMessageW(window, WM_SETICON, ICON_SMALL, (LPARAM)caption_icon);
    if (previous) DestroyIcon(previous);
}

static void sync_appearance(void)
{
    WCHAR file[MAX_PATH], color[64], size[64], managed[MAX_PATH];
    GetWindowsDirectoryW(managed, MAX_PATH);
    lstrcatW(managed, L"\\resources\\themes\\light\\light.msstyles");
    BOOL selected = SUCCEEDED(GetCurrentThemeName(file, MAX_PATH, color, 64, size, 64)) &&
            !lstrcmpiW(file, managed);
    COLORREF pane=GetSysColor(COLOR_WINDOW);
    dark = selected ? !lstrcmpW(color,L"Dark") :
           GetRValue(pane)*54+GetGValue(pane)*183+GetBValue(pane)*19 < 128*256;
    SendMessageW(light_button, BM_SETCHECK, selected && !lstrcmpW(color,L"Blue") ? BST_CHECKED : BST_UNCHECKED, 0);
    SendMessageW(dark_button, BM_SETCHECK, selected && !lstrcmpW(color,L"Dark") ? BST_CHECKED : BST_UNCHECKED, 0);
    update_icons();
}

static BOOL select_appearance(BOOL selected_dark)
{
    WCHAR path[MAX_PATH];
    GetWindowsDirectoryW(path, MAX_PATH);
    lstrcatW(path, L"\\resources\\themes\\light\\light.msstyles");
    HRESULT result = apply(path, selected_dark ? L"Dark" : L"Blue", L"NormalSize");
    if (FAILED(result)) {
        SetWindowTextW(status_label, L"The theme could not be applied. Existing appearance was retained.");
        return FALSE;
    }
    dark = selected_dark;
    SendMessageW(light_button, BM_SETCHECK, dark ? BST_UNCHECKED : BST_CHECKED, 0);
    SendMessageW(dark_button, BM_SETCHECK, dark ? BST_CHECKED : BST_UNCHECKED, 0);
    SetWindowTextW(status_label, L"Saved for this MihawkOS environment. Existing windows receive the theme change.");
    update_icons();
    InvalidateRect(window, NULL, TRUE);
    return TRUE;
}

/* Compare actual nonclient pixels with the selected Window theme parts.
 * This detects a classic fallback even when panes, icons and theme APIs pass.
 * Windows 11's geometry: SM_CXSIZE x SM_CYCAPTION buttons side by side from the
 * inside rectangle's top right, close first (Wine patch 1139). One button is
 * hovered through the real pointer, as a user would: maximize in light, close
 * in dark. Emit reference pixels and the strip for the console capture verdict. */
static void qualify_caption(void)
{
    RECT bounds;
    HDC actual = GetWindowDC(window), expected = actual ? CreateCompatibleDC(actual) : NULL;
    const int parts[] = {WP_CLOSEBUTTON, WP_MAXBUTTON, WP_MINBUTTON};
    const UINT hits[] = {HTCLOSE, HTMAXBUTTON, HTMINBUTTON};
    HTHEME theme = OpenThemeData(NULL,L"Window");
    BOOL ready=actual && expected && theme && GetWindowRect(window,&bounds);
    check(ready,"caption reference setup");
    if (!ready) goto done;
    int width=GetSystemMetrics(SM_CXSIZE), height=GetSystemMetrics(SM_CYCAPTION);
    int frame_x=GetSystemMetrics(SM_CXFRAME), frame_y=GetSystemMetrics(SM_CYFRAME);
    check(width==PW_APPEARANCE_CAPTION_BUTTON_WIDTH && height==PW_APPEARANCE_CAPTION_HEIGHT+1,"Mihawk 11 caption button size");
    UINT hot=dark ? HTCLOSE : HTMAXBUTTON;
    POINT pointer={bounds.right-frame_x-(dark ? 0 : width)-4,bounds.top+frame_y+height-4};
    SetCursorPos(pointer.x,pointer.y);
    SendMessageW(window,WM_NCMOUSEMOVE,hot,MAKELPARAM(pointer.x,pointer.y));
    RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW|RDW_ALLCHILDREN);
    BOOL active=GetActiveWindow()==window;
    COLORREF base=GetSysColor(active ? COLOR_ACTIVECAPTION : COLOR_INACTIVECAPTION);
    HBITMAP bitmap=CreateCompatibleBitmap(actual,width,height);
    check(bitmap!=NULL,"caption reference bitmap");
    if (!bitmap) goto done;
    HBITMAP previous=SelectObject(expected,bitmap);
    for (unsigned i=0;i<3;++i) {
        int right=bounds.right-bounds.left-frame_x-(int)i*width, left=right-width, top=frame_y;
        int state=(hits[i]==hot ? CBS_HOT : CBS_NORMAL)+(active ? 0 : 4);
        RECT rectangle={0,0,width,height};
        HBRUSH brush=CreateSolidBrush(base);FillRect(expected,&rectangle,brush);DeleteObject(brush);
        check(IsThemePartDefined(theme,parts[i],0) &&
              SUCCEEDED(DrawThemeBackground(theme,expected,parts[i],state,&rectangle,NULL)),
              "caption theme part renders");
        COLORREF button_background=GetPixel(expected,0,0);
        unsigned matching=0, ink=0, emitted=0;
        for (int y=0;y<height;++y) for (int x=0;x<width;++x) {
            COLORREF reference=GetPixel(expected,x,y), pixel=GetPixel(actual,left+x,top+y);
            matching+=reference!=CLR_INVALID && reference==pixel;
            if (reference!=CLR_INVALID && button_background!=CLR_INVALID &&
                x>=width/2-8 && x<width/2+8 && y>=height/2-8 && y<height/2+8 &&
                (abs((int)GetRValue(reference)-(int)GetRValue(button_background))>=24 ||
                 abs((int)GetGValue(reference)-(int)GetGValue(button_background))>=24 ||
                 abs((int)GetBValue(reference)-(int)GetBValue(button_background))>=24)) {
                ++ink;
                if (emitted<8) {
                    char line[180];wsprintfA(line,"appearance: caption %s part=%d sample=%ld,%ld expected=%u,%u,%u\n",
                        dark?"dark":"light",parts[i],bounds.left+left+x,bounds.top+top+y,
                        GetRValue(reference),GetGValue(reference),GetBValue(reference));
                    OutputDebugStringA(line);++emitted;
                }
            }
        }
        char line[180];wsprintfA(line,"appearance: caption %s part=%d state=%d matching=%u/%u ink=%u\n",
                                dark?"dark":"light",parts[i],state,matching,width*height,ink);OutputDebugStringA(line);
        check(ink>=8 && matching==(unsigned)(width*height),"actual caption matches its theme part");
    }
    char strip[200];wsprintfA(strip,"appearance: caption-strip %s left=%ld top=%ld bar=%u,%u,%u hot=%s pointer=%ld,%ld active=%d\n",
        dark?"dark":"light",bounds.right-frame_x-3*width,bounds.top+frame_y,GetRValue(base),GetGValue(base),GetBValue(base),
        dark?"close":"max",pointer.x,pointer.y,active);
    OutputDebugStringA(strip);
    SelectObject(expected,previous);DeleteObject(bitmap);
done:
    if (theme) CloseThemeData(theme);
    if (expected) DeleteDC(expected);
    if (actual) ReleaseDC(window,actual);
}

/* Repaint must be idempotent even when Windows supplies no erase message.
 * Compare one alpha blend on an opaque backing with the actual client pixels. */
static void qualify_repaint(void)
{
    HDC actual=GetDC(window), expected=actual?CreateCompatibleDC(actual):NULL;
    HBITMAP bitmap=actual?CreateCompatibleBitmap(actual,24,24):NULL;
    BOOL ready=actual && expected && bitmap && commands[0];
    check(ready,"alpha repaint reference setup");
    if(ready) {
        HBITMAP previous=SelectObject(expected,bitmap);
        RECT rectangle={0,0,24,24};
        FillRect(expected,&rectangle,GetSysColorBrush(COLOR_WINDOW));
        check(DrawIconEx(expected,0,0,commands[0],24,24,0,NULL,DI_NORMAL),"alpha repaint reference image");
        /* Explicitly omit RDW_ERASE: the paint handler owns every dirty pixel. */
        for(unsigned repaint=0;repaint<3;++repaint)
            RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_UPDATENOW);
        unsigned matching=0;
        for(int y=0;y<24;++y)for(int x=0;x<24;++x) {
            COLORREF reference=GetPixel(expected,x,y);
            matching+=reference!=CLR_INVALID && reference==GetPixel(actual,40+x,470+y);
        }
        check(matching==24*24,"repainting without erase preserves alpha icon pixels");
        char line[160];wsprintfA(line,"appearance: alpha repaint %s matching=%u/576\n",dark?"dark":"light",matching);
        OutputDebugStringA(line);
        SelectObject(expected,previous);
    }
    if(bitmap)DeleteObject(bitmap);
    if(expected)DeleteDC(expected);
    if(actual)ReleaseDC(window,actual);
}

static void qualify_appearance(void)
{
    WCHAR file[MAX_PATH], color[64], size[64];
    check(SUCCEEDED(GetCurrentThemeName(file, MAX_PATH, color, 64, size, 64)), "current theme");
    check(!lstrcmpW(color, dark ? L"Dark" : L"Blue"), "selected colour variant");
    char colours[180];
    wsprintfA(colours,"appearance: %s pane=%06lx text=%06lx\n",dark ? "dark" : "light",
              (unsigned long)GetSysColor(COLOR_WINDOW),(unsigned long)GetSysColor(COLOR_WINDOWTEXT));
    OutputDebugStringA(colours);
    /* a classic program's window and text: the prototype's Windows controls gallery (appearance.json classic) */
    check(GetSysColor(COLOR_WINDOW) == (dark ? PW_APPEARANCE_CLASSIC_DARK_WINDOW : PW_APPEARANCE_CLASSIC_LIGHT_WINDOW), "pane colour");
    check(GetSysColor(COLOR_WINDOWTEXT) == (dark ? PW_APPEARANCE_CLASSIC_DARK_TEXT : PW_APPEARANCE_CLASSIC_LIGHT_TEXT), "text colour");
    NONCLIENTMETRICSW metrics = {0};
    metrics.cbSize = sizeof(metrics);
    check(SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0), "nonclient metrics");
    check(metrics.lfCaptionFont.lfHeight == -12 && metrics.lfMenuFont.lfHeight == -12 &&
          metrics.lfMessageFont.lfHeight == -12, "nine-point system fonts remain twelve logical pixels");
    check(!lstrcmpW(metrics.lfCaptionFont.lfFaceName, L"Proton UI"), "system caption font family");
    check(metrics.iCaptionWidth == PW_APPEARANCE_CAPTION_BUTTON_WIDTH && metrics.iCaptionHeight == PW_APPEARANCE_CAPTION_HEIGHT, "caption metrics");
    HDC dc = GetDC(window);
    check(GetDeviceCaps(dc, LOGPIXELSX) == 96 && GetDeviceCaps(dc, LOGPIXELSY) == 96, "96 logical DPI");
    ReleaseDC(window, dc);
    check(GetSystemMetrics(SM_CXSCREEN) == 1920 && GetSystemMetrics(SM_CYSCREEN) == 1080, "desktop dimensions");
    POINT origin = {0,0}; ClientToScreen(window,&origin);
    char position[150]; wsprintfA(position,"appearance: %s client-origin=%ld,%ld\n",dark?"dark":"light",origin.x,origin.y);
    OutputDebugStringA(position);
    qualify_caption();
    /* ApplyTheme posts WM_THEMECHANGED. A synchronous repaint must already
     * use the current system colour before that notification is dispatched. */
    dc = GetDC(window);
    check(GetPixel(dc,20,195) == GetSysColor(COLOR_WINDOW), "painted pane uses current system colour");
    ReleaseDC(window,dc);
    qualify_repaint();
    /* Decode every object and both glyph variants at every packaged size. */
    const int sizes[] = {16,20,24,32,48,64};
    for (unsigned category = 0; category < 3; category++) {
        unsigned count = category ? 34 : 14, base = category ? category == 1 ? 2000 : 3000 : 1000;
        for (unsigned i = 0; i < count; i++) for (unsigned s = 0; s < 6; s++) {
            HICON icon = LoadImageW(icons, MAKEINTRESOURCEW(base+i), IMAGE_ICON, sizes[s], sizes[s], 0);
            ICONINFO info = {0}; BITMAP bitmap = {0};
            BOOL okay = icon && GetIconInfo(icon, &info) && info.hbmColor &&
                GetObjectW(info.hbmColor, sizeof(bitmap), &bitmap) &&
                bitmap.bmWidth == sizes[s] && bitmap.bmHeight == sizes[s];
            check(okay, "resource icon dimensions");
            if (info.hbmColor) DeleteObject(info.hbmColor);
            if (info.hbmMask) DeleteObject(info.hbmMask);
            if (icon) DestroyIcon(icon);
        }
    }
}

/* Diagnostic-only rendezvous with the owner's existing display capture.
 * A theme stays unchanged until the complete numbered image exists; API work
 * and first-frame latency therefore cannot move a capture into another phase. */
static void finish_probe(void)
{
    DeleteFileW(capture_request);
    check(SUCCEEDED(SUCCEEDED(prior_theme) ? apply(prior_file,prior_color,prior_size) : apply(NULL,NULL,NULL)),
          "restore previous theme");
    DestroyWindow(window);
}

/* The capture must show a settled frame. A theme change posts its own repaints, and the broker composes whatever the
 * window has published when the request lands: in gate-d3d12-census-2026-10-17-02 appearance32 it composed the caption
 * between its maximize and minimize buttons (the minimize glyph missing), and in gate-ntsync-2026-10-17-02 appearance32
 * without the icon previews RedrawWindow had just painted (a window's surface is published when its thread waits,
 * win32u's wait_objects). So every queued message is handled, every update region painted, and nothing arrives for
 * 250 ms (at most 3 s), its waits publishing the surface, before a capture is asked for. */
static void settle(void)
{
    DWORD started=GetTickCount(), quiet=started;
    while (GetTickCount()-started<3000 && GetTickCount()-quiet<250) {
        MSG message; RECT update;
        while (PeekMessageW(&message,NULL,0,0,PM_REMOVE)) {
            quiet=GetTickCount();
            if (message.message==WM_QUIT) { PostQuitMessage((int)message.wParam); return; }
            if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message);DispatchMessageW(&message); }
        }
        if (GetUpdateRect(window,&update,FALSE)) { UpdateWindow(window);quiet=GetTickCount(); }
        MsgWaitForMultipleObjects(0,NULL,FALSE,10,QS_ALLINPUT);
    }
}

static void request_capture(void)
{
    WCHAR saved[MAX_PATH];
    DWORD length=lstrlenW(capture_base);
    if (!length || length>=MAX_PATH-24) { check(FALSE,"display capture path");finish_probe();return; }
    lstrcpyW(capture_request,capture_base);
    lstrcatW(capture_request,L".broker-request");
    lstrcpyW(saved,capture_base);
    WCHAR *dot=NULL;for (WCHAR *p=saved;*p;p++) if (*p==L'.') dot=p;
    if (dot) *dot=0;
    lstrcpyW(capture_path,saved);
    lstrcatW(capture_path,capture_phase==1 ? L"-01.ppm" : L"-02.ppm");
    settle();
    RedrawWindow(window,NULL,NULL,RDW_INVALIDATE|RDW_FRAME|RDW_UPDATENOW|RDW_ALLCHILDREN);
    settle();
    HANDLE file=CreateFileW(capture_request,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
    check(file!=INVALID_HANDLE_VALUE,"request displayed capture");
    if (file==INVALID_HANDLE_VALUE) { finish_probe();return; }
    CloseHandle(file);capture_started=GetTickCount();capture_complete_ticks=0;
    SetTimer(window,1,250,NULL);
}

static LRESULT CALLBACK procedure(HWND hwnd, UINT message, WPARAM w, LPARAM l)
{
    if (message == WM_THEMECHANGED) {
        sync_appearance();
        InvalidateRect(hwnd, NULL, TRUE);
    } else if (message == WM_SYSCOLORCHANGE) {
        InvalidateRect(hwnd, NULL, TRUE);
    } else if (message == WM_CTLCOLORSTATIC) {
        SetTextColor((HDC)w, GetSysColor(COLOR_WINDOWTEXT));
        SetBkColor((HDC)w, GetSysColor(COLOR_WINDOW));
        return (LRESULT)GetSysColorBrush(COLOR_WINDOW);
    } else if (message == WM_ERASEBKGND) {
        RECT rectangle; GetClientRect(hwnd, &rectangle);
        FillRect((HDC)w, &rectangle, GetSysColorBrush(COLOR_WINDOW)); return 1;
    } else if (message == WM_COMMAND) {
        if (LOWORD(w) == 1) select_appearance(FALSE);
        if (LOWORD(w) == 2) select_appearance(TRUE);
        if (LOWORD(w) == 3) { labels = SendMessageW((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED; InvalidateRect(hwnd,NULL,TRUE); }
    } else if (message == WM_PAINT) {
        PAINTSTRUCT paint; HDC dc = BeginPaint(hwnd, &paint);
        /* A repaint can arrive without WM_ERASEBKGND. Reset the dirty backing
         * before alpha artwork/text so repeated painting remains idempotent. */
        FillRect(dc,&paint.rcPaint,GetSysColorBrush(COLOR_WINDOW));
        HFONT old = SelectObject(dc, font);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, GetSysColor(COLOR_WINDOWTEXT));
        const WCHAR *heading = L"Desktop icon previews";
        TextOutW(dc,32,205,heading,lstrlenW(heading));
        for (unsigned i = 0; i < 14; i++) {
            int x = 40+(i%7)*115, y = 238+(i/7)*92;
            DrawIconEx(dc,x+22,y,objects[i],48,48,0,NULL,DI_NORMAL);
            if (labels) TextOutW(dc,x,y+52,object_names[i],lstrlenW(object_names[i]));
        }
        heading = L"Interface icon previews"; TextOutW(dc,32,434,heading,lstrlenW(heading));
        for (unsigned i = 0; i < 34; i++) DrawIconEx(dc,40+(i%17)*46,470+(i/17)*40,commands[i],24,24,0,NULL,DI_NORMAL);
        SelectObject(dc, old); EndPaint(hwnd, &paint); return 0;
    } else if (message == WM_TIMER && probing) {
        WIN32_FILE_ATTRIBUTE_DATA image;
        BOOL complete=GetFileAttributesExW(capture_path,GetFileExInfoStandard,&image) &&
                      !image.nFileSizeHigh && image.nFileSizeLow>=1920*1080*3+17;
        capture_complete_ticks=complete ? capture_complete_ticks+1 : 0;
        BOOL timed_out=GetTickCount()-capture_started>=30000;
        if (capture_complete_ticks<3 && !timed_out) return 0;
        KillTimer(hwnd,1);check(!timed_out,"display capture completes before theme transition");
        if (capture_phase==1 && !timed_out) {
            capture_phase=2;
            check(select_appearance(TRUE), "apply dark through normal selector");
            qualify_appearance();
            OutputDebugStringA("PS5_PROTON_APPEARANCE_PHASE dark\n");
            request_capture();
        } else {
            finish_probe();
        }
    } else if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,message,w,l);
}

static HWND control(const WCHAR *type, const WCHAR *text, DWORD style, int x, int y, int width, int height, unsigned id)
{
    HWND result = CreateWindowW(type,text,WS_CHILD|WS_VISIBLE|style,x,y,width,height,window,(HMENU)(UINT_PTR)id,NULL,NULL);
    if (result) SendMessageW(result,WM_SETFONT,(WPARAM)font,TRUE);
    return result;
}

void mainCRTStartup(void)
{
    WCHAR path[MAX_PATH];
    static WCHAR command[4096];
    lstrcpynW(command,GetCommandLineW(),4096);
    probing = FALSE;
    for (WCHAR *p=command; *p; p++) {
        if (p!=command && p[-1]!=L' ') continue;
        if (!wcsncmp(p,L"--probe",7) && (!p[7] || p[7]==L' ')) probing=TRUE;
        if (!wcsncmp(p,L"--capture=",10)) {
            WCHAR *end=p+10;while (*end && *end!=L' ') end++;
            if (end-(p+10)<MAX_PATH) lstrcpynW(capture_base,p+10,end-(p+10)+1);
        }
    }
    themes = LoadLibraryW(L"uxtheme.dll");
    GetWindowsDirectoryW(path,MAX_PATH); lstrcatW(path,L"\\resources\\mihawk\\icons.dll");
    icons = LoadLibraryExW(path,NULL,LOAD_LIBRARY_AS_DATAFILE);
    if (!themes || !icons) { OutputDebugStringA("PS5_PROTON_APPEARANCE_PROBE FAIL: theme/icon resources missing\n"); ExitProcess(1); }
    prior_theme = GetCurrentThemeName(prior_file,MAX_PATH,prior_color,64,prior_size,64);
    dark = SUCCEEDED(prior_theme) && !lstrcmpW(prior_color,L"Dark");
    font = CreateFontW(-14,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Proton UI");
    for (unsigned i=0;i<14;i++) objects[i] = LoadImageW(icons,MAKEINTRESOURCEW(1000+i),IMAGE_ICON,48,48,0);
    update_icons();
    INITCOMMONCONTROLSEX common = {sizeof(common), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    if (!InitCommonControlsEx(&common)) ExitProcess(1);
    WNDCLASSW type = {0};
    type.lpfnWndProc=procedure;type.hInstance=GetModuleHandleW(NULL);type.lpszClassName=L"MihawkAppearance";
    type.hCursor=LoadCursorW(NULL,IDC_ARROW);
    if (!RegisterClassW(&type)) ExitProcess(1);
    window = CreateWindowW(type.lpszClassName,L"Appearance",WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,480,180,900,650,NULL,NULL,type.hInstance,NULL);
    if (!window) ExitProcess(1);
    SendMessageW(window,WM_SETICON,ICON_SMALL,(LPARAM)caption_icon);
    control(L"STATIC",L"Appearance",0,32,22,350,24,0);
    control(L"STATIC",L"Choose the appearance of this MihawkOS environment",0,32,56,700,24,0);
    light_button=control(L"BUTTON",L"Light",BS_AUTORADIOBUTTON|WS_TABSTOP|WS_GROUP,32,90,150,32,1);
    dark_button=control(L"BUTTON",L"Dark",BS_AUTORADIOBUTTON|WS_TABSTOP,192,90,150,32,2);
    sync_appearance();
    HWND labels_button=control(L"BUTTON",L"Show icon labels",BS_AUTOCHECKBOX|WS_TABSTOP,32,132,240,32,3);
    SendMessageW(labels_button,BM_SETCHECK,BST_CHECKED,0);
    status_label=control(L"STATIC",L"Changes apply immediately and persist across session restart.",0,32,172,800,25,0);
    ShowWindow(window,SW_SHOW); UpdateWindow(window);
    if (probing) {
        check(font && light_button && dark_button && labels_button && status_label, "real controls created");
        check(select_appearance(FALSE), "apply light through normal selector");
        check(GetWindowTheme(light_button) != NULL, "version-six themed controls");
        qualify_appearance(); OutputDebugStringA("PS5_PROTON_APPEARANCE_PHASE light\n");
        capture_phase=1;request_capture();
    }
    MSG message;
    while (GetMessageW(&message,NULL,0,0)>0) {
        if (!IsDialogMessageW(window,&message)) { TranslateMessage(&message);DispatchMessageW(&message); }
    }
    for (unsigned i=0;i<14;i++) if(objects[i]) DestroyIcon(objects[i]);
    for (unsigned i=0;i<34;i++) if(commands[i]) DestroyIcon(commands[i]);
    if (caption_icon) DestroyIcon(caption_icon);
    if (font) DeleteObject(font);
    FreeLibrary(icons);FreeLibrary(themes);
    if (probing) {
        char line[150];wsprintfA(line,"PS5_PROTON_APPEARANCE_PROBE %s checks=%u failures=%u\n",failures?"FAIL":"PASS",checks,failures);
        OutputDebugStringA(line);
    }
    ExitProcess(failures?1:0);
}
