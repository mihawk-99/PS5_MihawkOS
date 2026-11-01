/* SPDX-License-Identifier: GPL-3.0-or-later */
/* steam-runtime.exe: the game entry the title starts. It starts SteamCMD (Valve's headless client) in the same
 * mihawk prefix, sends the login line for the account named on its command line (SteamCMD's own machine token,
 * ssfn, is the credential; the password is not here), waits for the console prompt, then starts the game with
 * SteamAppId and SteamGameId in its environment so the game's SteamInternal_SteamAPI_Init sees a Steam client. It waits for the
 * game, then asks SteamCMD to quit. Built from the host with the MinGW cross compiler; not part of the title.
 *
 *   steam-runtime.exe <game.exe> --appid <id> --account <account> [--directory <dir>] [--log <path>] [-- game args]
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

static int write_all(HANDLE handle, const char *text)
{
    DWORD written = 0, length = (DWORD)strlen(text);
    return WriteFile(handle, text, length, &written, NULL) && written == length;
}

static HANDLE start(const char *command, HANDLE input, HANDLE output)
{
    STARTUPINFOA info;
    PROCESS_INFORMATION process;
    memset(&info, 0, sizeof info);
    info.cb = sizeof info;
    info.dwFlags = STARTF_USESTDHANDLES;
    info.hStdInput = input;
    info.hStdOutput = output;
    info.hStdError = output;
    if (!CreateProcessA(NULL, (LPSTR)command, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &info, &process)) return NULL;
    CloseHandle(process.hThread);
    return process.hProcess;
}

int main(int argc, char **argv)
{
    const char *login_file = NULL, *game = NULL, *app_id = "250900", *account = NULL;
    char account_file[MAX_PATH];
    char steamcmd[] = "C:\\Games\\SteamCMD\\steamcmd.exe";
    char steam_line[4096], game_line[4096], extra[2048] = {};
    HANDLE in_read, in_write, out_read, out_write;
    SECURITY_ATTRIBUTES inherit = {sizeof inherit, NULL, TRUE};
    HANDLE steam = NULL, child = NULL;
    HANDLE log = INVALID_HANDLE_VALUE;
    DWORD code = 0, written;
    char buffer[2048];
    int i, extra_at = 0, waited;
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--login-file") && i + 1 < argc) login_file = argv[++i];
        else if (!strcmp(argv[i], "--game") && i + 1 < argc) game = argv[++i];
        else if (!strcmp(argv[i], "--appid") && i + 1 < argc) app_id = argv[++i];
        else if (!strcmp(argv[i], "--account") && i + 1 < argc) account = argv[++i];
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) {
            log = CreateFileA(argv[++i], GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        } else if (!strcmp(argv[i], "--")) { for (++i; i < argc && extra_at < (int)sizeof extra - 512; ++i) extra_at += snprintf(extra + extra_at, sizeof extra - extra_at, " %s", argv[i]); break; }
    }
    if (!login_file && account) {
        /* The token persists after the first sign-in: the game path needs the account name only. */
        snprintf(account_file, sizeof account_file, "steam-login-%lu.txt", (unsigned long)GetCurrentProcessId());
        login_file = account_file;
        {
            HANDLE file = CreateFileA(login_file, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
            if (file != INVALID_HANDLE_VALUE) {
                char line[512];
                snprintf(line, sizeof line, "login \"%s\"\r\n", account);
                write_all(file, line);
                CloseHandle(file);
            }
        }
    }
    if (!login_file) return 2;

    /* The page writes the login line when the user presses Enter; SteamCMD's +runscript consumes it. */
    for (waited = 0; waited < 600; ++waited) {
        if (GetFileAttributesA(login_file) != INVALID_FILE_ATTRIBUTES) break;
        Sleep(1000);
    }
    if (GetFileAttributesA(login_file) == INVALID_FILE_ATTRIBUTES) return 3;

    if (!CreatePipe(&in_read, &in_write, &inherit, 0) || !CreatePipe(&out_read, &out_write, &inherit, 0)) return 3;
    SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
    snprintf(steam_line, sizeof steam_line, "\"%s\" -nobuffer +runscript \"%s\"", steamcmd, login_file);
    steam = start(steam_line, in_read, out_write);
    if (!steam) { if (log != INVALID_HANDLE_VALUE) write_all(log, "steam-runtime: SteamCMD did not start\r\n"); return 4; }
    CloseHandle(out_write);
    CloseHandle(in_read);
    /* SteamCMD has opened the script; the credential line is not left on the console. */
    Sleep(2000);
    DeleteFileA(login_file);

    /* SteamCMD's console text is not this pipe, so its sign-in is time-bounded, not text-detected: the token
     * login takes a few seconds, and the game must start rather than wait for a line that never arrives. */
    for (i = 0; i < 20; ++i) {
        DWORD got = 0;
        if (PeekNamedPipe(out_read, NULL, 0, NULL, &got, NULL) && got) {
            if (ReadFile(out_read, buffer, sizeof buffer - 1, &written, NULL) && written) {
                buffer[written] = 0;
                if (log != INVALID_HANDLE_VALUE) write_all(log, buffer);
                WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), buffer, written, &written, NULL);
            }
        }
        Sleep(1000);
    }

    if (game) {
        /* steam_api.dll looks for a running Steam client through ActiveProcess; without it the game starts the
         * retail client instead. Point ActiveProcess at the SteamCMD's process and its client pipe. */
        if (steam) {
            HKEY key;
            DWORD steam_pid = GetProcessId(steam), universe = 1;
            if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\Valve\\Steam\\ActiveProcess", 0, NULL, 0,
                                KEY_SET_VALUE, NULL, &key, NULL) == ERROR_SUCCESS) {
                RegSetValueExA(key, "pid", 0, REG_DWORD, (const BYTE *)&steam_pid, sizeof steam_pid);
                RegSetValueExA(key, "Universe", 0, REG_DWORD, (const BYTE *)&universe, sizeof universe);
                RegCloseKey(key);
            }
        }
        char env_block[8192];
        char *env = GetEnvironmentStringsA();
        const char *at = env;
        size_t used = 0;
        while (*at) {
            size_t length = strlen(at);
            if (strncmp(at, "SteamAppId=", 11) != 0 && strncmp(at, "SteamGameId=", 12) != 0 && used + length + 2 < sizeof env_block) {
                memcpy(env_block + used, at, length + 1); used += length + 1;
            }
            at += length + 1;
        }
        FreeEnvironmentStringsA(env);
        used += (size_t)snprintf(env_block + used, sizeof env_block - used, "SteamAppId=%s", app_id) + 1;
        /* SteamGameId too: Proton's Wine looks a game up by it (docs/PROTON_GAME_KNOWLEDGE.md). */
        used += (size_t)snprintf(env_block + used, sizeof env_block - used, "SteamGameId=%s", app_id) + 1;
        env_block[used++] = 0;
        snprintf(game_line, sizeof game_line, "\"%s\"%s", game, extra);
        {
            STARTUPINFOA info; PROCESS_INFORMATION process;
            memset(&info, 0, sizeof info); info.cb = sizeof info;
            if (CreateProcessA(NULL, game_line, NULL, NULL, FALSE, 0, env_block, NULL, &info, &process)) {
                CloseHandle(process.hThread); child = process.hProcess;
            }
        }
        if (child) { WaitForSingleObject(child, INFINITE); GetExitCodeProcess(child, &code); CloseHandle(child); }
    }
    if (steam) {
        write_all(in_write, "quit\r\n");
        WaitForSingleObject(steam, 15000);
        TerminateProcess(steam, 0);
        CloseHandle(steam);
    }
    CloseHandle(in_write);
    CloseHandle(out_read);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    return (int)code;
}
