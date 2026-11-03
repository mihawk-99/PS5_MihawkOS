/* SPDX-License-Identifier: GPL-3.0-or-later
 *
 * An HTTPS request the way Xbox Live's HTTP client (libHttpClient's WinHTTP task) makes one: an asynchronous session
 * opened with WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY and no agent, its TLS versions set, then per request
 * WinHttpSetTimeouts, WinHttpConnect, WinHttpOpenRequest, a status callback and WinHttpSendRequest, the answer read in
 * the callback. It shows WinHTTP working in a helper and inside MihawkOS: a game whose requests all failed there
 * (WinHttpConnect NULL with the last error 0) had its own in-process component answering WinHttpConnect, which this
 * probe, making the same calls, does not have (2026-11-03). Run once on the main thread and once on a thread-pool
 * thread, as the client's task queue does. The server's certificate is not checked (the fixture's
 * root is the TLS probe's business). "PS5_PROTON_WINHTTP <thread> <step> ok=... error=..." lines, then PASS when both
 * requests answered 200, else FAIL. Usage: winhttp-probe.exe <host> <port>. Kernel32, user32 and WinHTTP only. */
#include <windows.h>
#include <winhttp.h>

static void say(const char *text)
{
    DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), text, lstrlenA(text), &written, NULL);
    OutputDebugStringA(text);
}

static WCHAR host[256];
static INTERNET_PORT port;
static HINTERNET session;

struct request {
    const char *thread;
    HANDLE done;
    DWORD status, error;
};

static int step(const char *thread, const char *name, BOOL ok)
{
    char line[160];
    wsprintfA(line, "PS5_PROTON_WINHTTP %s %s ok=%d error=%lu\n", thread, name, ok != 0, ok ? 0 : GetLastError());
    say(line);
    return ok;
}

static void CALLBACK on_status(HINTERNET handle, DWORD_PTR context, DWORD status, void *info, DWORD length)
{
    struct request *request = (struct request *)context;
    DWORD size = sizeof(request->status);
    (void)length;
    if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE) {
        if (!WinHttpReceiveResponse(handle, NULL)) { request->error = GetLastError(); SetEvent(request->done); }
    } else if (status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) {
        if (!WinHttpQueryHeaders(handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                                 &request->status, &size, WINHTTP_NO_HEADER_INDEX))
            request->error = GetLastError();
        SetEvent(request->done);
    } else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        request->error = ((WINHTTP_ASYNC_RESULT *)info)->dwError;
        SetEvent(request->done);
    }
}

/* One request, the client's calls in its order; the status code, or 0. */
static DWORD perform(const char *thread)
{
    struct request request = {thread, CreateEventA(NULL, TRUE, FALSE, NULL), 0, 0};
    DWORD ignore = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                   SECURITY_FLAG_IGNORE_CERT_CN_INVALID | SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    HINTERNET connection = NULL, handle = NULL;
    char line[160];

    if (step(thread, "timeouts", WinHttpSetTimeouts(session, 30000, 30000, 30000, 30000)) &&
        step(thread, "connect", (connection = WinHttpConnect(session, host, port, 0)) != NULL) &&
        step(thread, "open-request", (handle = WinHttpOpenRequest(connection, L"GET", L"/", NULL, WINHTTP_NO_REFERER,
                                                                  WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                                  WINHTTP_FLAG_ESCAPE_DISABLE | WINHTTP_FLAG_SECURE)) != NULL) &&
        step(thread, "security-flags", WinHttpSetOption(handle, WINHTTP_OPTION_SECURITY_FLAGS, &ignore, sizeof(ignore))) &&
        step(thread, "callback", WinHttpSetStatusCallback(handle, on_status, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0) !=
                                 WINHTTP_INVALID_STATUS_CALLBACK) &&
        step(thread, "send", WinHttpSendRequest(handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0,
                                                (DWORD_PTR)&request))) {
        if (WaitForSingleObject(request.done, 30000)) request.error = WAIT_TIMEOUT;
        wsprintfA(line, "PS5_PROTON_WINHTTP %s answer status=%lu error=%lu\n", thread, request.status, request.error);
        say(line);
    }
    if (handle) {
        WinHttpSetStatusCallback(handle, NULL, WINHTTP_CALLBACK_FLAG_ALL_NOTIFICATIONS, 0);
        WinHttpCloseHandle(handle);
    }
    if (connection) WinHttpCloseHandle(connection);
    CloseHandle(request.done);
    return request.status;
}

static DWORD pooled;
static DWORD WINAPI on_pool(void *done)
{
    pooled = perform("pool");
    SetEvent(done);
    return 0;
}

void mainCRTStartup(void)
{
    /* The arguments after the program's name: <host> <port>. */
    const WCHAR *arguments = GetCommandLineW(), *p;
    BOOL quoted = FALSE;
    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_1 |
                      WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2, main_status = 0;
    unsigned n = 0;
    HANDLE done = CreateEventA(NULL, TRUE, FALSE, NULL);
    char line[160];

    for (p = arguments; *p && (quoted || *p != ' '); ++p) if (*p == '"') quoted = !quoted;
    while (*p == ' ') ++p;
    while (*p && *p != ' ' && n < ARRAYSIZE(host) - 1) host[n++] = *p++;
    while (*p == ' ') ++p;
    while (*p >= '0' && *p <= '9') port = (INTERNET_PORT)(port * 10 + (*p++ - '0'));
    if (!n || !port) {
        say("PS5_PROTON_WINHTTP FAIL: usage winhttp-probe.exe <host> <port>\n");
        ExitProcess(2);
    }

    if (step("main", "open", (session = WinHttpOpen(NULL, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                                    WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC)) != NULL) &&
        step("main", "protocols", WinHttpSetOption(session, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols)))) {
        main_status = perform("main");
        if (!QueueUserWorkItem(on_pool, done, WT_EXECUTEDEFAULT) || WaitForSingleObject(done, 60000))
            say("PS5_PROTON_WINHTTP pool did not finish\n");
        WinHttpCloseHandle(session);
    }
    wsprintfA(line, "PS5_PROTON_WINHTTP %s: main %lu, pool %lu\n", main_status == 200 && pooled == 200 ? "PASS" : "FAIL",
              main_status, pooled);
    say(line);
    ExitProcess(main_status == 200 && pooled == 200 ? 0 : 1);
}
