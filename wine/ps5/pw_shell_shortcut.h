/* SPDX-License-Identifier: GPL-3.0-or-later
 * "Make a shortcut" for the MihawkOS shell's File Explorer and desktop menus: a .lnk for a program, file or folder on the
 * desktop or in the Start menu's Programs folder, and a pin on Start. Names and the pin list are src/pw_shortcut.h's
 * (host-tested); this is the shell's side, included by programs/explorer's desktop.c and explorer.c.
 *
 * Where: the desktop shortcut goes in the user's own Desktop folder (CSIDL_DESKTOPDIRECTORY), which Explorer's desktop
 * lists beside the Public Desktop's, and is a file the title never touches: src/pw_shell_link.h only rewrites what
 * its manifest names. Start lists the Programs folders and both Desktops (pw_shell_start.c pw_start_scan), so a link
 * made in CSIDL_PROGRAMS is in All apps, and one pinned is named in HKCU\Software\Mihawk\Shell\StartPins, the
 * list Start already keeps (its own Pin to Start writes it). A pin the list has no room for (18) is refused and said.
 *
 * The icon: Explorer's desktop drops a shortcut that has no icon (programs/explorer/desktop.c add_launcher), so the
 * link carries the icon shell32 names for the target (SHGetFileInfo's icon location: a program's own, else the folder
 * or file type's). Nothing here writes a path the person cannot read: the link holds the target's own path. */
#ifndef PW_SHELL_SHORTCUT_H
#define PW_SHELL_SHORTCUT_H
#include <shlwapi.h>
#include "pw_shortcut.h"

enum { PW_SHORTCUT_DESKTOP, PW_SHORTCUT_START };   /* where a new shortcut goes */
static const WCHAR pw_shortcut_pins_key[] = L"Software\\Mihawk\\Shell";

/* Makes the link, its name chosen so it never replaces a file; the path made is in link (MAX_PATH units). */
static HRESULT pw_shell_make_shortcut( const WCHAR *target, int where, WCHAR *link )
{
    WCHAR folder[MAX_PATH], name[PW_SHORTCUT_NAME_MAX], icon[MAX_PATH] = L"";
    IShellLinkW *shell = NULL;
    IPersistFile *file = NULL;
    SHFILEINFOW info = {0};
    DWORD attributes = GetFileAttributesW( target );
    BOOL is_folder = attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY);
    HRESULT result;
    unsigned attempt;

    link[0] = 0;
    if (attributes == INVALID_FILE_ATTRIBUTES) return HRESULT_FROM_WIN32( ERROR_FILE_NOT_FOUND );
    if (FAILED( result = SHGetFolderPathW( NULL, where == PW_SHORTCUT_START ? CSIDL_PROGRAMS | CSIDL_FLAG_CREATE :
                                           CSIDL_DESKTOPDIRECTORY | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, folder ) ))
        return result;
    for (attempt = 1; ; ++attempt)
    {
        if (attempt > 999 || !pw_shortcut_name( (pw_u16 *)name, ARRAY_SIZE(name), (const pw_u16 *)target, is_folder, attempt ))
            return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        if (!PathCombineW( link, folder, name )) return HRESULT_FROM_WIN32( ERROR_BAD_PATHNAME );
        if (GetFileAttributesW( link ) == INVALID_FILE_ATTRIBUTES) break;
    }
    if (FAILED( result = CoCreateInstance( &CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&shell ) ))
        return result;
    IShellLinkW_SetPath( shell, target );
    if (!is_folder)
    {
        WCHAR directory[MAX_PATH];
        lstrcpynW( directory, target, MAX_PATH );
        if (PathRemoveFileSpecW( directory ) && directory[0]) IShellLinkW_SetWorkingDirectory( shell, directory );
    }
    if (SHGetFileInfoW( target, 0, &info, sizeof(info), SHGFI_ICONLOCATION ) && info.szDisplayName[0])
        lstrcpynW( icon, info.szDisplayName, MAX_PATH );
    if (icon[0]) IShellLinkW_SetIconLocation( shell, icon, info.iIcon );
    result = IShellLinkW_QueryInterface( shell, &IID_IPersistFile, (void **)&file );
    if (SUCCEEDED( result ))
    {
        result = IPersistFile_Save( file, link, TRUE );
        if (SUCCEEDED( result )) SHChangeNotify( SHCNE_CREATE, SHCNF_PATHW, link, NULL );
        IPersistFile_Release( file );
    }
    IShellLinkW_Release( shell );
    if (FAILED( result )) link[0] = 0;
    return result;
}

/* Pins path on Start: 1 pinned, 0 it was pinned, a negative number when Start's list could not take it. */
static int pw_shell_pin_to_start( const WCHAR *path )
{
    pw_u16 list[PW_SHORTCUT_PINS_MAX * PW_SHORTCUT_PATH_MAX + 4];
    DWORD size = sizeof(list), type;
    size_t units = 0;
    HKEY key;
    int added;
    LONG status = RegGetValueW( HKEY_CURRENT_USER, pw_shortcut_pins_key, L"StartPins", RRF_RT_REG_MULTI_SZ, &type, list, &size );
    if (status == ERROR_SUCCESS && size % sizeof(WCHAR) == 0) units = size / sizeof(WCHAR);
    else if (status != ERROR_FILE_NOT_FOUND && status != ERROR_SUCCESS) return -1;
    if (status == ERROR_SUCCESS && size % sizeof(WCHAR)) return -1;
    if ((added = pw_shortcut_pins_add( list, ARRAY_SIZE(list), &units, (const pw_u16 *)path )) <= 0) return added;
    if (RegCreateKeyExW( HKEY_CURRENT_USER, pw_shortcut_pins_key, 0, NULL, 0, KEY_SET_VALUE, NULL, &key, NULL ) != ERROR_SUCCESS)
        return -3;
    status = RegSetValueExW( key, L"StartPins", 0, REG_MULTI_SZ, (BYTE *)list, units * sizeof(WCHAR) );
    RegCloseKey( key );
    return status == ERROR_SUCCESS ? 1 : -3;
}
#endif
