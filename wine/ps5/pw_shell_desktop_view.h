/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SHELL_DESKTOP_VIEW_H
#define PW_SHELL_DESKTOP_VIEW_H
/* The Mihawk session's desktop as Windows builds it: the Desktop folder's shell view (SHELLDLL_DefView and its list view) as a child of
 * Progman, in place of the launchers Wine's explorer paints itself (docs/WINDOWS11_PLAN.md, phase 1). The shell's own code then provides
 * what a desktop does: rubber-band selection, dragging icons, rename, sort and arrange, drag and drop with Explorer windows, the item
 * context menu, keyboard navigation and change notifications. Included by programs/explorer/desktop.c (patch 1100).
 *
 * Costs, by design: one list view and the system image lists (shared with Explorer windows); no thread of ours (the shell view listens
 * for changes itself, where the launchers needed a watcher thread and an array of icons); nothing is polled and nothing is allocated
 * per frame. The view is created once and torn down with the process. */

#include <commctrl.h>

struct pw_desktop_browser
{
    IShellBrowser IShellBrowser_iface;
    IServiceProvider IServiceProvider_iface;
    HWND frame;           /* Progman, the view's parent */
    IShellView *view;
    HWND view_window;     /* SHELLDLL_DefView */
    HWND list;            /* its SysListView32 */
    WNDPROC view_proc;    /* the view's own procedure, which ours wraps */
};

static struct pw_desktop_browser pw_desktop_browser;

static inline struct pw_desktop_browser *pw_desktop_from_browser( IShellBrowser *iface )
{
    return CONTAINING_RECORD( iface, struct pw_desktop_browser, IShellBrowser_iface );
}

static inline struct pw_desktop_browser *pw_desktop_from_service( IServiceProvider *iface )
{
    return CONTAINING_RECORD( iface, struct pw_desktop_browser, IServiceProvider_iface );
}

static HRESULT WINAPI pw_desktop_browser_QueryInterface( IShellBrowser *iface, REFIID riid, void **ppv )
{
    struct pw_desktop_browser *browser = pw_desktop_from_browser( iface );

    if (IsEqualIID( riid, &IID_IUnknown ) || IsEqualIID( riid, &IID_IOleWindow ) || IsEqualIID( riid, &IID_IShellBrowser ))
        *ppv = &browser->IShellBrowser_iface;
    else if (IsEqualIID( riid, &IID_IServiceProvider ))
        *ppv = &browser->IServiceProvider_iface;
    else
    {
        *ppv = NULL;
        return E_NOINTERFACE;
    }
    IUnknown_AddRef( (IUnknown *)*ppv );
    return S_OK;
}

/* One object for the life of the process: its reference count is only kept to be well-behaved. */
static ULONG WINAPI pw_desktop_browser_AddRef( IShellBrowser *iface ) { return 2; }
static ULONG WINAPI pw_desktop_browser_Release( IShellBrowser *iface ) { return 1; }

static HRESULT WINAPI pw_desktop_browser_GetWindow( IShellBrowser *iface, HWND *hwnd )
{
    *hwnd = pw_desktop_from_browser( iface )->frame;
    return S_OK;
}

static HRESULT WINAPI pw_desktop_browser_ContextSensitiveHelp( IShellBrowser *iface, BOOL mode ) { return E_NOTIMPL; }
static HRESULT WINAPI pw_desktop_browser_InsertMenusSB( IShellBrowser *iface, HMENU shared, OLEMENUGROUPWIDTHS *widths ) { return E_NOTIMPL; }
static HRESULT WINAPI pw_desktop_browser_SetMenuSB( IShellBrowser *iface, HMENU shared, HOLEMENU holemenu, HWND active ) { return S_OK; }
static HRESULT WINAPI pw_desktop_browser_RemoveMenusSB( IShellBrowser *iface, HMENU shared ) { return E_NOTIMPL; }
static HRESULT WINAPI pw_desktop_browser_SetStatusTextSB( IShellBrowser *iface, LPCOLESTR text ) { return S_OK; }
static HRESULT WINAPI pw_desktop_browser_EnableModelessSB( IShellBrowser *iface, BOOL enable ) { return S_OK; }
static HRESULT WINAPI pw_desktop_browser_TranslateAcceleratorSB( IShellBrowser *iface, MSG *msg, WORD id ) { return S_FALSE; }

/* The desktop does not navigate: a folder the view wants opened opens in an Explorer window. */
static HRESULT WINAPI pw_desktop_browser_BrowseObject( IShellBrowser *iface, LPCITEMIDLIST pidl, UINT flags )
{
    SHELLEXECUTEINFOW info = { sizeof(info) };

    if (!pidl) return E_INVALIDARG;
    info.fMask = SEE_MASK_IDLIST;
    info.hwnd = pw_desktop_from_browser( iface )->frame;
    info.lpVerb = L"open";
    info.lpIDList = (void *)pidl;
    info.nShow = SW_SHOWNORMAL;
    return ShellExecuteExW( &info ) ? S_OK : E_FAIL;
}

static HRESULT WINAPI pw_desktop_browser_GetViewStateStream( IShellBrowser *iface, DWORD mode, IStream **stream )
{
    *stream = NULL;
    return E_FAIL;   /* the view keeps its defaults; positions are kept by the session (below) */
}

static HRESULT WINAPI pw_desktop_browser_GetControlWindow( IShellBrowser *iface, UINT id, HWND *hwnd )
{
    *hwnd = NULL;
    return E_FAIL;
}

static HRESULT WINAPI pw_desktop_browser_SendControlMsg( IShellBrowser *iface, UINT id, UINT msg, WPARAM wp, LPARAM lp, LRESULT *ret )
{
    return E_NOTIMPL;
}

static HRESULT WINAPI pw_desktop_browser_QueryActiveShellView( IShellBrowser *iface, IShellView **view )
{
    struct pw_desktop_browser *browser = pw_desktop_from_browser( iface );

    if (!browser->view) return E_FAIL;
    IShellView_AddRef( *view = browser->view );
    return S_OK;
}

static HRESULT WINAPI pw_desktop_browser_OnViewWindowActive( IShellBrowser *iface, IShellView *view ) { return S_OK; }
static HRESULT WINAPI pw_desktop_browser_SetToolbarItems( IShellBrowser *iface, LPTBBUTTONSB buttons, UINT count, UINT flags ) { return S_OK; }

static const IShellBrowserVtbl pw_desktop_browser_vtbl =
{
    pw_desktop_browser_QueryInterface,
    pw_desktop_browser_AddRef,
    pw_desktop_browser_Release,
    pw_desktop_browser_GetWindow,
    pw_desktop_browser_ContextSensitiveHelp,
    pw_desktop_browser_InsertMenusSB,
    pw_desktop_browser_SetMenuSB,
    pw_desktop_browser_RemoveMenusSB,
    pw_desktop_browser_SetStatusTextSB,
    pw_desktop_browser_EnableModelessSB,
    pw_desktop_browser_TranslateAcceleratorSB,
    pw_desktop_browser_BrowseObject,
    pw_desktop_browser_GetViewStateStream,
    pw_desktop_browser_GetControlWindow,
    pw_desktop_browser_SendControlMsg,
    pw_desktop_browser_QueryActiveShellView,
    pw_desktop_browser_OnViewWindowActive,
    pw_desktop_browser_SetToolbarItems
};

static HRESULT WINAPI pw_desktop_service_QueryInterface( IServiceProvider *iface, REFIID riid, void **ppv )
{
    return pw_desktop_browser_QueryInterface( &pw_desktop_from_service( iface )->IShellBrowser_iface, riid, ppv );
}

static ULONG WINAPI pw_desktop_service_AddRef( IServiceProvider *iface ) { return 2; }
static ULONG WINAPI pw_desktop_service_Release( IServiceProvider *iface ) { return 1; }

static HRESULT WINAPI pw_desktop_service_QueryService( IServiceProvider *iface, REFGUID service, REFIID riid, void **ppv )
{
    if (IsEqualGUID( service, &SID_STopLevelBrowser ))
        return pw_desktop_browser_QueryInterface( &pw_desktop_from_service( iface )->IShellBrowser_iface, riid, ppv );
    *ppv = NULL;
    return E_NOINTERFACE;
}

static const IServiceProviderVtbl pw_desktop_service_vtbl =
{
    pw_desktop_service_QueryInterface,
    pw_desktop_service_AddRef,
    pw_desktop_service_Release,
    pw_desktop_service_QueryService
};

#include "pw_shell_desktop_menus.h"
#include "pw_shell_wallpaper.h"   /* the wallpaper the view paints behind its icons */

/* The view's window, wrapped. The list view has no background of its own (below): when it asks to be erased, the wallpaper is what is
 * painted there, as the system paints it for Progman. Its menus are the prototype's (pw_shell_desktop_menus.h); the shell's own
 * commands it invokes find their browser here (CWM_GETISHELLBROWSER). */
static LRESULT CALLBACK pw_desktop_view_proc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
    struct pw_desktop_browser *browser = &pw_desktop_browser;
    static UINT wallpaper_changed;

    if (!wallpaper_changed) wallpaper_changed = RegisterWindowMessageW( PW_WALLPAPER_CHANGED_MESSAGE );
    if (msg == wallpaper_changed)
    {
        /* Settings > Personalization > Background wrote a new proton.bmp: forget the picture and paint the new one now */
        pw_shell_wallpaper_forget();
        RedrawWindow( hwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW );
        return 1;
    }
    if (msg == WM_USER + 0x1111 /* shell32's SHV_CHANGE_NOTIFY, sent by SHChangeNotify */ && browser->list)
    {
        /* what each change notice did to the desktop's items (shell81-view-r5-07: a new shortcut never listed) */
        int before = SendMessageW( browser->list, LVM_GETITEMCOUNT, 0, 0 );
        LRESULT result = CallWindowProcW( browser->view_proc, hwnd, msg, wp, lp );
        WARN( "Mihawk desktop: change %#Ix: %d items, then %d\n", (ULONG_PTR)lp, before,
              (int)SendMessageW( browser->list, LVM_GETITEMCOUNT, 0, 0 ) );
        return result;
    }

    switch (msg)
    {
    case WM_ERASEBKGND:
    {
        /* how often, and how much: the user saw every icon repaint while a game launcher was in use (2026-10-12) */
        static DWORD window_start, erases;
        static LONG largest;
        RECT clip;
        DWORD now = GetTickCount();

        GetClipBox( (HDC)wp, &clip );
        erases++;
        if ((clip.right - clip.left) * (clip.bottom - clip.top) > largest) largest = (clip.right - clip.left) * (clip.bottom - clip.top);
        if (now - window_start >= 2000)
        {
            if (erases > 1) WARN( "Mihawk desktop: %lu background erases in %lu ms, the largest %ld pixels\n", erases, now - window_start, largest );
            window_start = now;
            erases = 0;
            largest = 0;
        }
        /* the picture the setting names (pw_shell_wallpaper.h), not Wine's desktop bitmap, which only the process
         * holding the server's desktop window would refresh */
        {
            RECT client;
            GetClientRect( hwnd, &client );
            if (!pw_shell_wallpaper_paint( (HDC)wp, &client )) PaintDesktop( (HDC)wp );
        }
        return 1;
    }
    case WM_CONTEXTMENU:
        pw_desktop_menu( browser, lp );
        return 0;
    case WM_USER + 7: /* CWM_GETISHELLBROWSER */
        return (LRESULT)&browser->IShellBrowser_iface;
    }
    return CallWindowProcW( browser->view_proc, hwnd, msg, wp, lp );
}

/* The desktop's list view the way Windows sets it: transparent over the wallpaper, white labels, no auto-arrange (icons stay where they
 * are put, snapped to the grid), no scroll bars, and the desktop look (LVS_EX_TRANSPARENTSHADOWTEXT: comctl32 draws the prototype's
 * plates, band and labels, patch 1105). The shell view gives a desktop list its 48 px icons and the prototype's grid itself, before it
 * places any item (patch 1102). */
static void pw_desktop_style_list( struct pw_desktop_browser *browser )
{
    LONG_PTR style = GetWindowLongPtrW( browser->list, GWL_STYLE );

    SetWindowLongPtrW( browser->list, GWL_STYLE, style & ~LVS_AUTOARRANGE );
    SendMessageW( browser->list, LVM_SETEXTENDEDLISTVIEWSTYLE,
                  LVS_EX_DOUBLEBUFFER | LVS_EX_SNAPTOGRID | LVS_EX_TRANSPARENTSHADOWTEXT,
                  LVS_EX_DOUBLEBUFFER | LVS_EX_SNAPTOGRID | LVS_EX_TRANSPARENTSHADOWTEXT );
    SendMessageW( browser->list, LVM_SETBKCOLOR, 0, CLR_NONE );
    SendMessageW( browser->list, LVM_SETTEXTBKCOLOR, 0, CLR_NONE );
    SendMessageW( browser->list, LVM_SETTEXTCOLOR, 0, RGB(255, 255, 255) );
}


/* The shell view learns of changes by explicit notifications; a file made by a program that does not send one (a launcher writing a
 * shortcut) would never appear. Windows' shell watches the Desktop folders and sends them itself, and so does this: one thread, two
 * directory watches (the user's Desktop and the public one), each an overlapped read of 4 KiB, waiting on events (no polling). */
struct pw_desktop_watch
{
    WCHAR *folder;
    HANDLE dir;
    OVERLAPPED overlapped;
    char buffer[4096] __attribute__((aligned(8)));
};

static struct pw_desktop_watch pw_desktop_watches[2];

static BOOL pw_desktop_watch_start( struct pw_desktop_watch *watch )
{
    HANDLE event = watch->overlapped.hEvent;

    memset( &watch->overlapped, 0, sizeof(watch->overlapped) );
    watch->overlapped.hEvent = event;
    ResetEvent( event );
    return ReadDirectoryChangesW( watch->dir, watch->buffer, sizeof(watch->buffer), FALSE,
                                  FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME, NULL, &watch->overlapped, NULL );
}

static BOOL pw_desktop_start_ok( struct pw_desktop_watch *watch )
{
    return pw_desktop_watch_start( watch ) || GetLastError() == ERROR_IO_PENDING;
}
/* Turn what one watch read into the notifications the view understands. */
static void pw_desktop_watch_notify( struct pw_desktop_watch *watch, DWORD bytes )
{
    const FILE_NOTIFY_INFORMATION *info = (const FILE_NOTIFY_INFORMATION *)watch->buffer;
    WCHAR old_path[MAX_PATH] = L"", path[MAX_PATH];

    if (!bytes)   /* the buffer overflowed: the view re-reads the folder */
    {
        PostMessageW( pw_desktop_browser.view_window, WM_COMMAND, 0x7103 /* FCIDM_SHVIEW_REFRESH */, 0 );
        return;
    }
    for (;;)
    {
        size_t length = info->FileNameLength / sizeof(WCHAR);

        if (wcslen( watch->folder ) + 1 + length < MAX_PATH)
        {
            size_t folder_length = wcslen( watch->folder );

            memcpy( path, watch->folder, folder_length * sizeof(WCHAR) );
            path[folder_length] = '\\';
            memcpy( path + folder_length + 1, info->FileName, length * sizeof(WCHAR) );
            path[folder_length + 1 + length] = 0;
            WARN( "Mihawk desktop: watch action %lu %s\n", info->Action, debugstr_w( path ) );
            switch (info->Action)
            {
            case FILE_ACTION_ADDED:
                SHChangeNotify( (GetFileAttributesW( path ) & FILE_ATTRIBUTE_DIRECTORY) ? SHCNE_MKDIR : SHCNE_CREATE, SHCNF_PATHW, path, NULL );
                break;
            case FILE_ACTION_REMOVED:
                SHChangeNotify( SHCNE_DELETE, SHCNF_PATHW, path, NULL );
                break;
            case FILE_ACTION_RENAMED_OLD_NAME:
                wcscpy( old_path, path );
                break;
            case FILE_ACTION_RENAMED_NEW_NAME:
                if (old_path[0]) SHChangeNotify( SHCNE_RENAMEITEM, SHCNF_PATHW, old_path, path );
                old_path[0] = 0;
                break;
            }
        }
        if (!info->NextEntryOffset) break;
        info = (const FILE_NOTIFY_INFORMATION *)((const char *)info + info->NextEntryOffset);
    }
}

static DWORD WINAPI pw_desktop_watch_thread( void *arg )
{
    HANDLE events[2];
    unsigned int i, count = 0;

    WARN( "Mihawk desktop: the folder watch thread runs\n" );
    CoInitialize( NULL );
    for (i = 0; i < 2; i++)
    {
        BOOL started;

        if (pw_desktop_watches[i].dir == INVALID_HANDLE_VALUE) continue;
        started = pw_desktop_start_ok( &pw_desktop_watches[i] );
        WARN( "Mihawk desktop: watching %s: %s (error %lu)\n", debugstr_w( pw_desktop_watches[i].folder ), started ? "yes" : "no",
              started ? 0 : GetLastError() );
        if (started) events[count++] = pw_desktop_watches[i].overlapped.hEvent;
    }
    while (count)
    {
        DWORD which = WaitForMultipleObjects( count, events, FALSE, INFINITE ), bytes = 0;
        struct pw_desktop_watch *watch;

        if (which >= count) break;
        for (i = 0; i < 2; i++)
            if (pw_desktop_watches[i].dir != INVALID_HANDLE_VALUE && pw_desktop_watches[i].overlapped.hEvent == events[which]) break;
        if (i == 2) break;
        watch = &pw_desktop_watches[i];
        if (!GetOverlappedResult( watch->dir, &watch->overlapped, &bytes, FALSE ) && GetLastError() != ERROR_NOTIFY_ENUM_DIR) break;
        WARN( "Mihawk desktop: %lu bytes of changes in %s\n", bytes, debugstr_w( watch->folder ) );
        Sleep( 100 );   /* a burst of changes is one wake-up, not hundreds */
        pw_desktop_watch_notify( watch, bytes );
        if (!pw_desktop_watch_start( watch )) break;
    }
    return 0;
}


/* The user's Desktop and the public one, watched for names appearing, going and changing. */
static void pw_desktop_watch_folders( void )
{
    const KNOWNFOLDERID *ids[2] = { &FOLDERID_Desktop, &FOLDERID_PublicDesktop };
    unsigned int i;
    HANDLE thread;

    for (i = 0; i < 2; i++)
    {
        struct pw_desktop_watch *watch = &pw_desktop_watches[i];
        WCHAR *path = NULL;

        watch->dir = INVALID_HANDLE_VALUE;
        if (FAILED( SHGetKnownFolderPath( ids[i], KF_FLAG_CREATE, NULL, &path ) )) continue;
        watch->folder = malloc( (wcslen( path ) + 1) * sizeof(WCHAR) );
        if (watch->folder) wcscpy( watch->folder, path );
        CoTaskMemFree( path );
        if (!watch->folder) continue;
        watch->dir = CreateFileW( watch->folder, FILE_LIST_DIRECTORY | SYNCHRONIZE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
                                  OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL );
        watch->overlapped.hEvent = CreateEventW( NULL, TRUE, FALSE, NULL );
        WARN( "Mihawk desktop: folder %s opened %p (error %lu)\n", debugstr_w( watch->folder ), watch->dir,
              watch->dir == INVALID_HANDLE_VALUE ? GetLastError() : 0 );
    }
    thread = CreateThread( NULL, 128 * 1024, pw_desktop_watch_thread, NULL, 0, NULL );
    WARN( "Mihawk desktop: watch thread %p (error %lu)\n", thread, thread ? 0 : GetLastError() );
    if (thread) CloseHandle( thread );
}

/* Create the folder view for the Desktop folder inside `frame` (Progman). FALSE if the shell could not make one, in which case the
 * caller keeps the launchers. */
static BOOL pw_desktop_view_create( HWND frame )
{
    struct pw_desktop_browser *browser = &pw_desktop_browser;
    FOLDERSETTINGS settings = { FVM_ICON, FWF_DESKTOP | FWF_NOCLIENTEDGE | FWF_NOSCROLL | FWF_SNAPTOGRID | FWF_ALIGNLEFT };
    IShellFolder *folder = NULL;
    RECT rect;
    HRESULT hr;

    if (browser->view) return TRUE;
    OleInitialize( NULL );   /* drag and drop needs OLE in this thread, which runs the desktop's message loop */
    browser->IShellBrowser_iface.lpVtbl = &pw_desktop_browser_vtbl;
    browser->IServiceProvider_iface.lpVtbl = &pw_desktop_service_vtbl;
    browser->frame = frame;

    if (FAILED( hr = SHGetDesktopFolder( &folder ) )) goto failed;
    hr = IShellFolder_CreateViewObject( folder, frame, &IID_IShellView, (void **)&browser->view );
    IShellFolder_Release( folder );
    if (FAILED( hr )) goto failed;

    if (!SystemParametersInfoW( SPI_GETWORKAREA, 0, &rect, 0 )) GetClientRect( frame, &rect );
    hr = IShellView_CreateViewWindow( browser->view, NULL, &settings, &browser->IShellBrowser_iface, &rect, &browser->view_window );
    if (FAILED( hr ) || !browser->view_window) goto failed;

    browser->list = FindWindowExW( browser->view_window, NULL, WC_LISTVIEWW, NULL );
    if (browser->list) pw_desktop_style_list( browser );
    if (browser->list && pw_desktop_icons_hidden()) ShowWindow( browser->list, SW_HIDE );
    browser->view_proc = (WNDPROC)SetWindowLongPtrW( browser->view_window, GWLP_WNDPROC, (LONG_PTR)pw_desktop_view_proc );
    SetWindowPos( browser->view_window, HWND_TOP, rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top,
                  SWP_SHOWWINDOW | SWP_NOACTIVATE );
    IShellView_UIActivate( browser->view, SVUIA_INPLACEACTIVATE );
    pw_desktop_watch_folders();
    WARN( "Mihawk desktop: folder view %p, list %p, %ldx%ld\n", browser->view_window, browser->list,
          (long)(rect.right - rect.left), (long)(rect.bottom - rect.top) );
    return TRUE;

failed:
    WARN( "Mihawk desktop: no folder view (%#lx); keeping the launchers\n", hr );
    if (browser->view) IShellView_Release( browser->view );
    memset( browser, 0, sizeof(*browser) );
    return FALSE;
}

/* The icons live inside the work area (the screen less the taskbar), as on Windows; the wallpaper is still Progman's, over all of the screen.
 * Called when Progman changes size (the display mode did) and when the work area does. */
static void pw_desktop_view_fit( void )
{
    struct pw_desktop_browser *browser = &pw_desktop_browser;
    RECT area;

    if (!browser->view_window || !SystemParametersInfoW( SPI_GETWORKAREA, 0, &area, 0 )) return;
    SetWindowPos( browser->view_window, 0, area.left, area.top, area.right - area.left, area.bottom - area.top,
                  SWP_NOZORDER | SWP_NOACTIVATE );
}

#endif
