/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_SHELL_DESKTOP_MENUS_H
#define PW_SHELL_DESKTOP_MENUS_H
/* The desktop's menus as the approved prototype offers them (prototypes/windows11-shell/js/desktop.js, desktopMenu and itemMenu):
 * Windows 11's menu on a right-click or the menu key, the classic one with Shift (Shift+F10, Shift+right-click) or from "Show more
 * options". Win32u draws them as the prototype does (patch 1190); their icons are the prototype's (pw_menu_icons.h,
 * tools/build-menu-sprites.py). Every item does what the shell can do, and the prototype's items it has no action for are left
 * out: New Shortcut, Paste shortcut, Open in new window, Open file location, Empty Recycle Bin, Compress to ZIP
 * file. Create shortcut and Pin to Start (pw_shell_shortcut.h) are in this menu and, from shell32's item menu (patch 2494), in the classic one and File Explorer's. Share stays as the prototype shows it for a file it cannot share: disabled; so does Open with's Choose another app, as
 * the prototype shows it with no app picker (Wine's, SHOpenWithDialog, is a stub). View's icon sizes, Auto arrange and
 * Align to grid are the shell view's own settings (IFolderView2, shell32 patch 1102), which keeps and lays them out itself. Included by pw_shell_desktop_view.h (patch 1100), which hands it the desktop's WM_CONTEXTMENU. */

#include <shellapi.h>
#include "pw_menu_icons.h"
#include "pw_shell_shortcut.h"

enum
{
    PW_DESKTOP_PASTE = 1, PW_DESKTOP_SHOW_ICONS, PW_DESKTOP_SORT /* + 0..3: name, size, type, date */,
    PW_DESKTOP_REFRESH = PW_DESKTOP_SORT + 4, PW_DESKTOP_NEW_FOLDER, PW_DESKTOP_NEW_TEXT, PW_DESKTOP_DISPLAY,
    PW_DESKTOP_PERSONALIZE, PW_DESKTOP_TERMINAL, PW_DESKTOP_MORE, PW_DESKTOP_CUT, PW_DESKTOP_COPY, PW_DESKTOP_RENAME,
    PW_DESKTOP_SHARE, PW_DESKTOP_DELETE, PW_DESKTOP_OPEN, PW_DESKTOP_COPY_PATH, PW_DESKTOP_PROPERTIES, PW_DESKTOP_NOTEPAD,
    PW_DESKTOP_ICON_SIZE /* + 0..2: large, medium, small */, PW_DESKTOP_AUTO_ARRANGE = PW_DESKTOP_ICON_SIZE + 3,
    PW_DESKTOP_ALIGN_TO_GRID, PW_DESKTOP_CHOOSE_APP, PW_DESKTOP_MAKE_SHORTCUT, PW_DESKTOP_PIN_START,
};
/* The prototype's icon sizes, in View's order (js/desktop.js ICON: large 96, medium 48, small 32). */
static const int pw_desktop_icon_sizes[] = {96, 48, 32};

#define PW_DESKTOP_HIDE_ICONS_KEY L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Advanced"

/* The menus are light or dark as win32u draws them: by COLOR_MENU. */
static BOOL pw_desktop_menu_dark( void )
{
    COLORREF menu = GetSysColor( COLOR_MENU );
    return ((GetRValue( menu ) * 54 + GetGValue( menu ) * 183 + GetBValue( menu ) * 19) >> 8) < 128;
}

/* A prototype icon as a 20 x 20 premultiplied bitmap, made once per theme. */
static HBITMAP pw_desktop_menu_icon( int index )
{
    static HBITMAP icons[2][PW_MENU_ICON_COUNT];
    BOOL dark = pw_desktop_menu_dark();

    if (!icons[dark][index])
    {
        BITMAPINFO info = {{sizeof(info.bmiHeader), 20, -20, 1, 32, BI_RGB}};
        void *bits;

        if ((icons[dark][index] = CreateDIBSection( NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0 )))
            memcpy( bits, pw_menu_icons[dark][index], sizeof(pw_menu_icons[dark][index]) );
    }
    return icons[dark][index];
}

/* Appends an item: text NULL and icon < 0 is a separator; text NULL with an icon is a command of the command row. */
static void pw_desktop_menu_add( HMENU menu, UINT id, const WCHAR *text, int icon, UINT state, HMENU submenu )
{
    MENUITEMINFOW item = {sizeof(item), MIIM_ID | MIIM_STATE};

    item.wID = id;
    item.fState = state;
    if (text)
    {
        item.fMask |= MIIM_STRING;
        item.dwTypeData = (WCHAR *)text;
    }
    if (icon >= 0)
    {
        item.fMask |= MIIM_BITMAP;
        item.hbmpItem = pw_desktop_menu_icon( icon );
    }
    if (submenu)
    {
        item.fMask |= MIIM_SUBMENU;
        item.hSubMenu = submenu;
    }
    if (!text && icon < 0)
    {
        /* the prototype's tidy(): no separator first, last or twice */
        int count = GetMenuItemCount( menu );
        MENUITEMINFOW last = {sizeof(last), MIIM_FTYPE};
        if (!count || (GetMenuItemInfoW( menu, count - 1, TRUE, &last ) && (last.fType & MFT_SEPARATOR))) return;
        item.fMask = MIIM_FTYPE;
        item.fType = MFT_SEPARATOR;
    }
    InsertMenuItemW( menu, GetMenuItemCount( menu ), TRUE, &item );
}

static void pw_desktop_menu_end( HMENU menu )
{
    int count = GetMenuItemCount( menu );
    MENUITEMINFOW last = {sizeof(last), MIIM_FTYPE};

    if (count && GetMenuItemInfoW( menu, count - 1, TRUE, &last ) && (last.fType & MFT_SEPARATOR)) DeleteMenu( menu, count - 1, MF_BYPOSITION );
}

static BOOL pw_desktop_can_paste( void )
{
    return IsClipboardFormatAvailable( CF_HDROP ) || IsClipboardFormatAvailable( RegisterClipboardFormatW( CFSTR_SHELLIDLISTW ));
}

/* The desktop's background: View, Sort by, Refresh, New, Display settings, Personalize, Open in Terminal, Show more options. */
static HMENU pw_desktop_background_menu( struct pw_desktop_browser *browser, BOOL classic )
{
    static const WCHAR *sorts[] = {L"Name", L"Size", L"Item type", L"Date modified"};
    static const WCHAR *sizes[] = {L"Large icons\tCtrl+Shift+2", L"Medium icons\tCtrl+Shift+3", L"Small icons\tCtrl+Shift+4"};
    HMENU menu = CreatePopupMenu(), view = CreatePopupMenu(), sort = CreatePopupMenu(), create = CreatePopupMenu();
    BOOL paste = pw_desktop_can_paste();
    IFolderView2 *folder = NULL;
    FOLDERVIEWMODE mode;
    DWORD flags = 0;
    int size = 0;

    if (SUCCEEDED( IShellView_QueryInterface( browser->view, &IID_IFolderView2, (void **)&folder ) ))
    {
        IFolderView2_GetViewModeAndIconSize( folder, &mode, &size );
        IFolderView2_GetCurrentFolderFlags( folder, &flags );
        IFolderView2_Release( folder );
    }
    for (int i = 0; folder && i < ARRAY_SIZE(sizes); i++)
    {
        MENUITEMINFOW radio = {sizeof(radio), MIIM_FTYPE, MFT_RADIOCHECK};
        pw_desktop_menu_add( view, PW_DESKTOP_ICON_SIZE + i, sizes[i], -1, size == pw_desktop_icon_sizes[i] ? MFS_CHECKED : 0, NULL );
        SetMenuItemInfoW( view, PW_DESKTOP_ICON_SIZE + i, FALSE, &radio );
    }
    pw_desktop_menu_add( view, 0, NULL, -1, 0, NULL );
    if (folder)
    {
        pw_desktop_menu_add( view, PW_DESKTOP_AUTO_ARRANGE, L"Auto arrange icons", -1, (flags & FWF_AUTOARRANGE) ? MFS_CHECKED : 0, NULL );
        pw_desktop_menu_add( view, PW_DESKTOP_ALIGN_TO_GRID, L"Align icons to grid", -1, (flags & FWF_SNAPTOGRID) ? MFS_CHECKED : 0, NULL );
    }
    pw_desktop_menu_add( view, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( view, PW_DESKTOP_SHOW_ICONS, L"Show desktop icons", -1,
                         IsWindowVisible( browser->list ) ? MFS_CHECKED : 0, NULL );
    for (int i = 0; i < ARRAY_SIZE(sorts); i++) pw_desktop_menu_add( sort, PW_DESKTOP_SORT + i, sorts[i], -1, 0, NULL );
    pw_desktop_menu_add( create, PW_DESKTOP_NEW_FOLDER, L"Folder", PW_MENU_ICON_FOLDER, 0, NULL );
    pw_desktop_menu_add( create, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( create, PW_DESKTOP_NEW_TEXT, L"Text Document", PW_MENU_ICON_TEXT, 0, NULL );
    if (classic)
    {
        pw_desktop_menu_add( menu, 0, L"&View", -1, 0, view );
        pw_desktop_menu_add( menu, 0, L"S&ort by", -1, 0, sort );
        pw_desktop_menu_add( menu, PW_DESKTOP_REFRESH, L"R&efresh", -1, 0, NULL );
        pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
        pw_desktop_menu_add( menu, PW_DESKTOP_PASTE, L"&Paste", -1, paste ? 0 : MFS_DISABLED, NULL );
        pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
        pw_desktop_menu_add( menu, 0, L"Ne&w", -1, 0, create );
        pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
        pw_desktop_menu_add( menu, PW_DESKTOP_DISPLAY, L"&Display settings", PW_MENU_ICON_DISPLAY, 0, NULL );
        pw_desktop_menu_add( menu, PW_DESKTOP_PERSONALIZE, L"Pe&rsonalize", PW_MENU_ICON_PERSONALIZE, 0, NULL );
        return menu;
    }
    if (paste)
    {
        pw_desktop_menu_add( menu, PW_DESKTOP_PASTE, NULL, PW_MENU_ICON_PASTE, 0, NULL );
        pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    }
    pw_desktop_menu_add( menu, 0, L"View", PW_MENU_ICON_VIEW, 0, view );
    pw_desktop_menu_add( menu, 0, L"Sort by", PW_MENU_ICON_SORT, 0, sort );
    pw_desktop_menu_add( menu, PW_DESKTOP_REFRESH, L"Refresh", PW_MENU_ICON_REFRESH, 0, NULL );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( menu, 0, L"New", PW_MENU_ICON_NEW, 0, create );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( menu, PW_DESKTOP_DISPLAY, L"Display settings", PW_MENU_ICON_DISPLAY, 0, NULL );
    pw_desktop_menu_add( menu, PW_DESKTOP_PERSONALIZE, L"Personalize", PW_MENU_ICON_PERSONALIZE, 0, NULL );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( menu, PW_DESKTOP_TERMINAL, L"Open in Terminal", PW_MENU_ICON_TERMINAL, 0, NULL );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( menu, PW_DESKTOP_MORE, L"Show more options\tShift+F10", PW_MENU_ICON_MORE, 0, NULL );
    return menu;
}

/* The selection: its items (relative to the desktop, which is the root, so also absolute) and what they are. */
struct pw_desktop_selection
{
    LPITEMIDLIST items[64];
    UINT count;
    BOOL movable;   /* every item is a file or a folder (not This PC or the Recycle Bin) */
    BOOL files;     /* ... and one of them is not a folder */
    BOOL file;      /* one item, a file that is neither a folder nor a shortcut */
    BOOL open_with; /* ... and not a program: Open with */
    BOOL text;      /* ... a .txt: Open with offers Notepad */
};

static void pw_desktop_selection_free( struct pw_desktop_selection *s )
{
    for (UINT i = 0; i < s->count; i++) ILFree( s->items[i] );
    s->count = 0;
}

static void pw_desktop_selection( struct pw_desktop_browser *browser, struct pw_desktop_selection *s )
{
    IFolderView *view = NULL;
    IShellFolder *desktop = NULL;
    int index = -1;

    memset( s, 0, sizeof(*s) );
    if (FAILED( IShellView_QueryInterface( browser->view, &IID_IFolderView, (void **)&view ) )) return;
    SHGetDesktopFolder( &desktop );
    s->movable = TRUE;
    while (s->count < ARRAY_SIZE(s->items) && (index = SendMessageW( browser->list, LVM_GETNEXTITEM, index, LVNI_SELECTED )) >= 0)
    {
        SFGAOF attributes = SFGAO_FILESYSTEM | SFGAO_FOLDER | SFGAO_LINK;
        PITEMID_CHILD item;

        if (FAILED( IFolderView_Item( view, index, &item ) )) continue;
        s->items[s->count++] = item;
        if (!desktop || FAILED( IShellFolder_GetAttributesOf( desktop, 1, (LPCITEMIDLIST *)&item, &attributes ) )) attributes = 0;
        if (!(attributes & SFGAO_FILESYSTEM)) s->movable = FALSE;
        else if (!(attributes & SFGAO_FOLDER)) s->files = TRUE;
        s->file = s->count == 1 && (attributes & SFGAO_FILESYSTEM) && !(attributes & (SFGAO_FOLDER | SFGAO_LINK));
    }
    if (s->count != 1) s->file = FALSE;
    if (!s->count) s->movable = FALSE;
    if (s->file)
    {
        WCHAR path[MAX_PATH], *dot;

        dot = SHGetPathFromIDListW( s->items[0], path ) ? wcsrchr( path, '.' ) : NULL;
        if (dot && wcschr( dot, '\\' )) dot = NULL;
        s->open_with = !dot || wcsicmp( dot, L".exe" );
        s->text = dot && !wcsicmp( dot, L".txt" );
    }
    if (desktop) IShellFolder_Release( desktop );
    IFolderView_Release( view );
}

/* Selected items: the command row (Cut, Copy, Rename, Share, Delete), Open, Open with, Copy as path, Properties, Edit in Notepad,
 * Show more options. Open with is a .txt's submenu of Notepad and Choose another app, another file's Choose another app. */
static HMENU pw_desktop_item_menu( const struct pw_desktop_selection *s )
{
    HMENU menu = CreatePopupMenu(), apps;
    BOOL one = s->count == 1;

    if (s->movable)
    {
        pw_desktop_menu_add( menu, PW_DESKTOP_CUT, NULL, PW_MENU_ICON_CUT, 0, NULL );
        pw_desktop_menu_add( menu, PW_DESKTOP_COPY, NULL, PW_MENU_ICON_COPY, 0, NULL );
    }
    if (one) pw_desktop_menu_add( menu, PW_DESKTOP_RENAME, NULL, PW_MENU_ICON_RENAME, 0, NULL );
    if (s->movable && s->files) pw_desktop_menu_add( menu, PW_DESKTOP_SHARE, NULL, PW_MENU_ICON_SHARE, MFS_DISABLED, NULL );
    if (s->movable) pw_desktop_menu_add( menu, PW_DESKTOP_DELETE, NULL, PW_MENU_ICON_DELETE, 0, NULL );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( menu, PW_DESKTOP_OPEN, L"Open\tEnter", -1, 0, NULL );
    if (s->open_with && s->text && (apps = CreatePopupMenu()))
    {
        pw_desktop_menu_add( apps, PW_DESKTOP_NOTEPAD, L"Notepad", PW_MENU_ICON_NOTEPAD, 0, NULL );
        pw_desktop_menu_add( apps, 0, NULL, -1, 0, NULL );
        pw_desktop_menu_add( apps, PW_DESKTOP_CHOOSE_APP, L"Choose another app", -1, MFS_DISABLED, NULL );
        pw_desktop_menu_end( apps );
        pw_desktop_menu_add( menu, 0, L"Open with", PW_MENU_ICON_OPEN_WITH, 0, apps );
    }
    else if (s->open_with) pw_desktop_menu_add( menu, PW_DESKTOP_CHOOSE_APP, L"Open with", PW_MENU_ICON_OPEN_WITH, MFS_DISABLED, NULL );
    if (s->movable) pw_desktop_menu_add( menu, PW_DESKTOP_COPY_PATH, L"Copy as path\tCtrl+Shift+C", PW_MENU_ICON_COPY_PATH, 0, NULL );
    if (one && s->movable)
    {
        pw_desktop_menu_add( menu, PW_DESKTOP_MAKE_SHORTCUT, L"Create &shortcut", -1, 0, NULL );   /* access keys underlined only once keys are used (patch 1190) */
        pw_desktop_menu_add( menu, PW_DESKTOP_PIN_START, L"Pi&n to Start", -1, 0, NULL );
    }
    if (one) pw_desktop_menu_add( menu, PW_DESKTOP_PROPERTIES, L"Properties\tAlt+Enter", PW_MENU_ICON_PROPERTIES, 0, NULL );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    if (s->file) pw_desktop_menu_add( menu, PW_DESKTOP_NOTEPAD, L"Edit in Notepad", PW_MENU_ICON_NOTEPAD, 0, NULL );
    pw_desktop_menu_add( menu, 0, NULL, -1, 0, NULL );
    pw_desktop_menu_add( menu, PW_DESKTOP_MORE, L"Show more options\tShift+F10", PW_MENU_ICON_MORE, 0, NULL );
    pw_desktop_menu_end( menu );
    return menu;
}

static BOOL pw_desktop_path( LPCITEMIDLIST item, WCHAR *path );

/* Create shortcut beside the item, or Pin to Start (a link in Start's Programs, pinned). */
static void pw_desktop_shortcut( const struct pw_desktop_selection *s, BOOL pin )
{
    WCHAR path[MAX_PATH], link[MAX_PATH];
    if (s->count != 1 || !pw_desktop_path( s->items[0], path )) return;
    if (FAILED( pw_shell_make_shortcut( path, pin ? PW_SHORTCUT_START : PW_SHORTCUT_DESKTOP, link ) ))
        MessageBoxW( NULL, L"MihawkOS could not make the shortcut.", L"Desktop", MB_OK | MB_ICONERROR );
    else if (pin && pw_shell_pin_to_start( link ) < 0)
        MessageBoxW( NULL, L"Start has no room for another pinned app. Unpin one first.", L"Desktop", MB_OK | MB_ICONINFORMATION );
}

/* Runs a verb of the selection's or the background's own context menu (shell32), as its classic menu would. */
static void pw_desktop_verb( struct pw_desktop_browser *browser, UINT which, const char *verb )
{
    CMINVOKECOMMANDINFO info = {sizeof(info)};
    IContextMenu *menu = NULL;

    if (FAILED( IShellView_GetItemObject( browser->view, which, &IID_IContextMenu, (void **)&menu ) )) return;
    info.hwnd = browser->view_window;
    info.lpVerb = verb;
    info.nShow = SW_SHOWNORMAL;
    IContextMenu_InvokeCommand( menu, &info );
    IContextMenu_Release( menu );
}

/* The classic menu of the selection is the shell's own, with every verb it has. */
static void pw_desktop_shell_menu( struct pw_desktop_browser *browser, const struct pw_desktop_selection *s, POINT at )
{
    CMINVOKECOMMANDINFO info = {sizeof(info)};
    IContextMenu *shell = NULL;
    HMENU menu;
    UINT id;

    if (FAILED( IShellView_GetItemObject( browser->view, SVGIO_SELECTION, &IID_IContextMenu, (void **)&shell ) )) return;
    if ((menu = CreatePopupMenu()) &&
        SUCCEEDED( IContextMenu_QueryContextMenu( shell, menu, 0, 1, 0x7fff, CMF_NORMAL | (s->count == 1 ? CMF_CANRENAME : 0) ) ))
    {
        if ((id = TrackPopupMenu( menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, browser->view_window, NULL )))
        {
            info.hwnd = browser->view_window;
            info.lpVerb = MAKEINTRESOURCEA( id - 1 );
            info.nShow = SW_SHOWNORMAL;
            IContextMenu_InvokeCommand( shell, &info );
        }
    }
    if (menu) DestroyMenu( menu );
    IContextMenu_Release( shell );
}

static BOOL pw_desktop_path( LPCITEMIDLIST item, WCHAR *path )
{
    return SHGetPathFromIDListW( item, path );
}

/* Copy as path: each item's path in quotes, one per line. */
static void pw_desktop_copy_paths( struct pw_desktop_browser *browser, const struct pw_desktop_selection *s )
{
    WCHAR path[MAX_PATH], *text;
    size_t used = 0, size = s->count * (MAX_PATH + 4) + 1;
    HGLOBAL memory;

    if (!(memory = GlobalAlloc( GMEM_MOVEABLE, size * sizeof(WCHAR) ))) return;
    text = GlobalLock( memory );
    for (UINT i = 0; i < s->count; i++)
        if (pw_desktop_path( s->items[i], path ))
            used += swprintf( text + used, size - used, L"%s\"%s\"", used ? L"\r\n" : L"", path );
    text[used] = 0;
    GlobalUnlock( memory );
    if (OpenClipboard( browser->view_window ))
    {
        EmptyClipboard();
        if (!SetClipboardData( CF_UNICODETEXT, memory )) GlobalFree( memory );
        CloseClipboard();
    }
    else GlobalFree( memory );
}

/* New > Text Document: "New Text Document.txt" (or "... (2).txt"), selected with its name being edited, as Windows makes it. */
static void pw_desktop_new_text( struct pw_desktop_browser *browser )
{
    WCHAR folder[MAX_PATH], path[MAX_PATH];
    LPITEMIDLIST item;
    HANDLE file = INVALID_HANDLE_VALUE;

    if (!SHGetSpecialFolderPathW( NULL, folder, CSIDL_DESKTOPDIRECTORY, TRUE )) return;
    for (int n = 1; n < 1000 && file == INVALID_HANDLE_VALUE; n++)
    {
        if (n == 1) swprintf( path, MAX_PATH, L"%s\\New Text Document.txt", folder );
        else swprintf( path, MAX_PATH, L"%s\\New Text Document (%d).txt", folder, n );
        file = CreateFileW( path, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL );
        if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS) return;
    }
    if (file == INVALID_HANDLE_VALUE) return;
    CloseHandle( file );
    SHChangeNotify( SHCNE_CREATE, SHCNF_PATHW | SHCNF_FLUSH, path, NULL );
    if (SUCCEEDED( SHParseDisplayName( path, NULL, &item, 0, NULL ) ))
    {
        IShellView_SelectItem( browser->view, ILFindLastID( item ),
                               SVSI_DESELECTOTHERS | SVSI_EDIT | SVSI_ENSUREVISIBLE | SVSI_FOCUSED | SVSI_SELECT );
        ILFree( item );
    }
}

/* View > Show desktop icons, kept where Windows keeps it (Explorer\Advanced, HideIcons). */
static void pw_desktop_show_icons( struct pw_desktop_browser *browser, BOOL show )
{
    DWORD hide = !show;

    ShowWindow( browser->list, show ? SW_SHOWNA : SW_HIDE );
    RegSetKeyValueW( HKEY_CURRENT_USER, PW_DESKTOP_HIDE_ICONS_KEY, L"HideIcons", REG_DWORD, &hide, sizeof(hide) );
}

static BOOL pw_desktop_icons_hidden( void )
{
    DWORD hide = 0, size = sizeof(hide);

    return !RegGetValueW( HKEY_CURRENT_USER, PW_DESKTOP_HIDE_ICONS_KEY, L"HideIcons", RRF_RT_REG_DWORD, NULL, &hide, &size ) && hide;
}

/* View's icon size, Auto arrange and Align to grid: the shell view lays the icons out again and keeps the setting. */
static void pw_desktop_view_setting( struct pw_desktop_browser *browser, UINT id )
{
    DWORD flags = 0, flag = id == PW_DESKTOP_AUTO_ARRANGE ? FWF_AUTOARRANGE : FWF_SNAPTOGRID;
    IFolderView2 *folder;

    if (FAILED( IShellView_QueryInterface( browser->view, &IID_IFolderView2, (void **)&folder ) )) return;
    if (id < PW_DESKTOP_ICON_SIZE + ARRAY_SIZE(pw_desktop_icon_sizes))
        IFolderView2_SetViewModeAndIconSize( folder, FVM_ICON, pw_desktop_icon_sizes[id - PW_DESKTOP_ICON_SIZE] );
    else if (SUCCEEDED( IFolderView2_GetCurrentFolderFlags( folder, &flags ) ))
        IFolderView2_SetCurrentFolderFlags( folder, flag, (flags & flag) ^ flag );
    IFolderView2_Release( folder );
}

static void pw_desktop_run( struct pw_desktop_browser *browser, const WCHAR *program, const WCHAR *arguments, const WCHAR *directory )
{
    ShellExecuteW( browser->view_window, NULL, program, arguments, directory, SW_SHOWNORMAL );
}

/* WM_CONTEXTMENU on the desktop: where (screen coordinates; -1, -1 from the keyboard), with Shift for the classic menu. */
static void pw_desktop_menu( struct pw_desktop_browser *browser, LPARAM where )
{
    BOOL classic = GetKeyState( VK_SHIFT ) < 0;
    struct pw_desktop_selection s;
    POINT at = {(short)LOWORD( where ), (short)HIWORD( where )};
    HMENU menu;
    UINT id = 0;

    pw_desktop_selection( browser, &s );
    if (where == (LPARAM)-1)
    {
        /* the keyboard: the focused item's middle, or 8 px into the desktop (js/desktop.js keyboardMenu) */
        int focus = s.count ? SendMessageW( browser->list, LVM_GETNEXTITEM, -1, LVNI_FOCUSED | LVNI_SELECTED ) : -1;
        RECT box = {LVIR_BOUNDS};

        if (focus >= 0 && SendMessageW( browser->list, LVM_GETITEMRECT, focus, (LPARAM)&box ))
        {
            /* the item is its plate (the prototype's element; comctl32's hit test, patch 1105): 1 px inside its cell
             * (LVIR_BOUNDS) and 2 px narrower, as tall as its label makes it, which the hit test finds */
            int inside = box.top + 1, outside = box.bottom;

            while (outside - inside > 1)
            {
                LVHITTESTINFO hit = {{(box.left + box.right - 2) / 2, (inside + outside) / 2}};

                if (SendMessageW( browser->list, LVM_HITTEST, 0, (LPARAM)&hit ) == focus) inside = hit.pt.y;
                else outside = hit.pt.y;
            }
            at.x = (box.left + box.right - 1) / 2;
            at.y = (box.top + 1 + outside + 1) / 2;
            MapWindowPoints( browser->list, NULL, &at, 1 );
        }
        else
        {
            GetWindowRect( browser->view_window, &box );
            at.x = box.left + 8;
            at.y = box.top + 8;
        }
    }
    for (;;)
    {
        if (s.count && classic)
        {
            pw_desktop_shell_menu( browser, &s, at );
            break;
        }
        menu = s.count ? pw_desktop_item_menu( &s ) : pw_desktop_background_menu( browser, classic );
        id = TrackPopupMenu( menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, at.x, at.y, 0, browser->view_window, NULL );
        DestroyMenu( menu );
        if (id != PW_DESKTOP_MORE) break;
        classic = TRUE;   /* "Show more options": the classic menu at the same point */
    }
    if (id >= PW_DESKTOP_SORT && id < PW_DESKTOP_SORT + 4)
    {
        /* the view sorts by the column (shell32's 0x30 + column); the desktop lays its icons out again in that order */
        SendMessageW( browser->view_window, WM_COMMAND, 0x30 + id - PW_DESKTOP_SORT, 0 );
        SendMessageW( browser->list, LVM_ARRANGE, LVA_ALIGNLEFT, 0 );
    }
    else switch (s.count && classic ? 0 : id)
    {
    case PW_DESKTOP_PASTE: pw_desktop_verb( browser, SVGIO_BACKGROUND, "paste" ); break;
    case PW_DESKTOP_SHOW_ICONS: pw_desktop_show_icons( browser, !IsWindowVisible( browser->list ) ); break;
    case PW_DESKTOP_ICON_SIZE: case PW_DESKTOP_ICON_SIZE + 1: case PW_DESKTOP_ICON_SIZE + 2:
    case PW_DESKTOP_AUTO_ARRANGE: case PW_DESKTOP_ALIGN_TO_GRID: pw_desktop_view_setting( browser, id ); break;
    case PW_DESKTOP_REFRESH: IShellView_Refresh( browser->view ); break;
    case PW_DESKTOP_NEW_FOLDER: pw_desktop_verb( browser, SVGIO_BACKGROUND, CMDSTR_NEWFOLDERA ); break;
    case PW_DESKTOP_NEW_TEXT: pw_desktop_new_text( browser ); break;
    case PW_DESKTOP_DISPLAY: pw_desktop_run( browser, L"mihawk-settings.exe", L"system", NULL ); break;
    case PW_DESKTOP_PERSONALIZE: pw_desktop_run( browser, L"mihawk-settings.exe", L"personalization", NULL ); break;
    case PW_DESKTOP_TERMINAL:
    {
        WCHAR folder[MAX_PATH];
        if (SHGetSpecialFolderPathW( NULL, folder, CSIDL_DESKTOPDIRECTORY, FALSE )) pw_desktop_run( browser, L"cmd.exe", NULL, folder );
        break;
    }
    case PW_DESKTOP_CUT: pw_desktop_verb( browser, SVGIO_SELECTION, "cut" ); break;
    case PW_DESKTOP_COPY: pw_desktop_verb( browser, SVGIO_SELECTION, "copy" ); break;
    case PW_DESKTOP_DELETE: pw_desktop_verb( browser, SVGIO_SELECTION, "delete" ); break;
    case PW_DESKTOP_PROPERTIES: pw_desktop_verb( browser, SVGIO_SELECTION, "properties" ); break;
    case PW_DESKTOP_RENAME:
        IShellView_SelectItem( browser->view, s.items[0], SVSI_DESELECTOTHERS | SVSI_EDIT | SVSI_ENSUREVISIBLE | SVSI_FOCUSED | SVSI_SELECT );
        break;
    case PW_DESKTOP_OPEN:
        for (UINT i = 0; i < s.count; i++)
        {
            SHELLEXECUTEINFOW info = {sizeof(info), SEE_MASK_IDLIST | SEE_MASK_INVOKEIDLIST};
            info.hwnd = browser->view_window;
            info.lpIDList = s.items[i];
            info.nShow = SW_SHOWNORMAL;
            ShellExecuteExW( &info );
        }
        break;
    case PW_DESKTOP_COPY_PATH: pw_desktop_copy_paths( browser, &s ); break;
    case PW_DESKTOP_MAKE_SHORTCUT:
    case PW_DESKTOP_PIN_START: pw_desktop_shortcut( &s, id == PW_DESKTOP_PIN_START ); break;
    case PW_DESKTOP_NOTEPAD:
    {
        WCHAR path[MAX_PATH], arguments[MAX_PATH + 2];
        if (pw_desktop_path( s.items[0], path ))
        {
            swprintf( arguments, ARRAY_SIZE(arguments), L"\"%s\"", path );
            pw_desktop_run( browser, L"notepad.exe", arguments, NULL );
        }
        break;
    }
    }
    pw_desktop_selection_free( &s );
}

#endif
