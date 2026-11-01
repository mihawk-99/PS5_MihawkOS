/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <windows.h>
#include <winternl.h>
#include <tlhelp32.h>

struct reply { DWORD pid, flags; };
static volatile LONG running = 1;
static DWORD WINAPI worker(void *unused)
{
    (void)unused;
    while (InterlockedCompareExchange(&running,1,1)) Sleep(1);
    return 0;
}
static int contains(const char *text, const char *word)
{
    for (; *text; ++text) {
        unsigned i=0; while (word[i] && text[i]==word[i]) ++i;
        if (!word[i]) return 1;
    }
    return 0;
}
static unsigned long long number(const char **p)
{
    unsigned long long value=0;
    while (**p==' ') ++*p;
    while (**p>='0' && **p<='9') { value=value*10+(**p-'0'); ++*p; }
    return value;
}
static void append_number(char **p, unsigned long long value)
{
    char digits[24]; unsigned used=0;
    do { digits[used++]='0'+value%10; value/=10; } while(value);
    *(*p)++=' '; while(used) *(*p)++=digits[--used]; **p=0;
}
static void verdict(int ok, const char *stage)
{
    char error[80]="process-probe: last_error"; char *end=error;while(*end)++end;
    append_number(&end,GetLastError());*end++='\n';*end=0;
    OutputDebugStringA(error); OutputDebugStringA(stage);
    OutputDebugStringA(ok ? "PS5_PROTON_PROCESS_PROBE PASS\n" : "PS5_PROTON_PROCESS_PROBE FAIL\n");
    ExitProcess(ok ? 0 : 1);
}
static DWORD parent_handles(void)
{
    typedef NTSTATUS (WINAPI *Query)(SYSTEM_INFORMATION_CLASS,void *,ULONG,ULONG *);
    Query query=(Query)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQuerySystemInformation");
    static char buffer[65536] __attribute__((aligned(8)));
    if(!query || query(SystemHandleInformation,buffer,sizeof(buffer),NULL)) return ~0u;
    SYSTEM_HANDLE_INFORMATION *info=(void *)buffer;
    DWORD count=0;
    for(ULONG i=0;i<info->Count;++i) if(info->Handle[i].OwnerPid==GetCurrentProcessId()) ++count;
    return count;
}
/* The count once it holds still. Wine's own threads open and close handles of their own while a process starts (the
 * HID bus, plug and play notices), and a count taken in the middle of that is not the program's: gate-2026-10-07-07
 * counted 55 before two rendering children and 53 after, where four earlier runs counted 53 and 53. Three equal counts
 * 100 ms apart, or the last of 5 s of them. */
static DWORD settled_handles(void)
{
    DWORD last=parent_handles(),same=0;
    for(unsigned tries=0;tries<50 && same<3;++tries) {
        Sleep(100);
        const DWORD now=parent_handles();
        same=now==last ? same+1 : 0;
        last=now;
    }
    return last;
}
static void handle_balance(DWORD before, int strict, const char *stage)
{
    DWORD after=settled_handles();
    if(before==~0u || after==~0u) verdict(0,stage);
    char line[96]="process-probe: parent handles before";char *p=line;while(*p)++p;
    append_number(&p,before);const char *label=" after";while(*label)*p++=*label++;
    append_number(&p,after);*p++='\n';*p=0;OutputDebugStringA(line);
    if(strict && before!=after) verdict(0,stage);
}
struct tree_reply { DWORD pid,parent,level,flags; };
static int launch_descendant(unsigned level,HANDLE pipe,HANDLE done,HANDLE ready,DWORD root,unsigned orphan,PROCESS_INFORMATION *pi)
{
    char line[512]="C:\\process-probe.exe --descendant-node";char *p=line;while(*p)++p;
    append_number(&p,level);append_number(&p,(ULONG_PTR)pipe);append_number(&p,(ULONG_PTR)done);
    append_number(&p,GetCurrentProcessId());append_number(&p,root);append_number(&p,orphan);append_number(&p,(ULONG_PTR)ready);
    const char *quote=" \"two words\"";while(*quote)*p++=*quote++;*p=0;
    WCHAR wide[512];STARTUPINFOW startup={0};startup.cb=sizeof(startup);
    return MultiByteToWideChar(CP_UTF8,0,line,-1,wide,512) &&
           CreateProcessW(NULL,wide,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,pi);
}
static void descendant_node(const char *args)
{
    unsigned level=(unsigned)number(&args);
    HANDLE pipe=(HANDLE)(ULONG_PTR)number(&args),done=(HANDLE)(ULONG_PTR)number(&args);
    DWORD parent=(DWORD)number(&args),root=(DWORD)number(&args);
    unsigned orphan=(unsigned)number(&args);HANDLE ready=(HANDLE)(ULONG_PTR)number(&args);
    struct tree_reply r={GetCurrentProcessId(),parent,level,0};DWORD bytes=0;
    typedef NTSTATUS (WINAPI *Query)(HANDLE,PROCESSINFOCLASS,void *,ULONG,ULONG *);
    Query query=(Query)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQueryInformationProcess");
    PROCESS_BASIC_INFORMATION basic={0};char cookie[32];
    if(r.pid && r.pid!=parent && r.pid!=root)r.flags|=1;
    if(contains(GetCommandLineA(),"\"two words\""))r.flags|=2;
    if(GetEnvironmentVariableA("PS5_PROCESS_COOKIE",cookie,sizeof(cookie))==12 && contains(cookie,"native-child"))r.flags|=4;
    if(query && !query(GetCurrentProcess(),ProcessBasicInformation,&basic,sizeof(basic),NULL) && (ULONG_PTR)basic.Reserved3==parent)r.flags|=8;
    HANDLE parent_handle=orphan && level==1 ? OpenProcess(SYNCHRONIZE,FALSE,parent) : NULL;
    if(r.flags!=15 || (orphan && level==1 && (!parent_handle || WaitForSingleObject(parent_handle,0)!=WAIT_TIMEOUT)) ||
       !WriteFile(pipe,&r,sizeof(r),&bytes,NULL) || bytes!=sizeof(r) || (ready && !SetEvent(ready)))verdict(0,"stage=descendant-entry\n");
    if(parent_handle) {
        if(WaitForSingleObject(parent_handle,10000)!=WAIT_OBJECT_0)verdict(0,"stage=descendant-parent-exit\n");
        CloseHandle(parent_handle);
        OutputDebugStringA("PS5_PROTON_DESCENDANT_SURVIVED_PARENT PASS\n");
    }
    if(level) {
        SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE started=CreateEventA(&sa,TRUE,FALSE,NULL);
        PROCESS_INFORMATION pi={0};DWORD code=0;
        HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);
        if(!thread || !started || !launch_descendant(level-1,pipe,done,started,root,orphan,&pi))verdict(0,"stage=descendant-create\n");
        if(WaitForSingleObject(started,15000)!=WAIT_OBJECT_0)verdict(0,"stage=descendant-ready\n");
        CloseHandle(started);
        if(!(orphan && level==2)) {
            if(WaitForSingleObject(pi.hProcess,20000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess,&code) || code!=0x5100+level-1)verdict(0,"stage=descendant-exit-code\n");
        }
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);
        InterlockedExchange(&running,0);WaitForSingleObject(thread,10000);CloseHandle(thread);
    } else if(!SetEvent(done))verdict(0,"stage=descendant-shared-event\n");
    OutputDebugStringA("PS5_PROTON_DESCENDANT_NODE PASS\n");
    ExitProcess(0x5100+level);
}
static void descendant_root(const char *cmd)
{
    HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);
    SetEnvironmentVariableA("PS5_PROCESS_COOKIE","native-child");
    /* Set after Wine bootstrap; its own helper processes remain unchanged. */
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");
    unsigned cycles=contains(cmd,"--descendants-repeat") ? 4 : 1;
    unsigned orphan=contains(cmd,"--descendants-parent-exit");
    for(unsigned cycle=0;cycle<cycles;++cycle) {
        DWORD before=settled_handles(),code=0;
        SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE read=NULL,write=NULL;
        if(!thread || !CreatePipe(&read,&write,&sa,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0))verdict(0,"stage=descendant-pipe\n");
        HANDLE done=CreateEventA(&sa,TRUE,FALSE,NULL);PROCESS_INFORMATION pi={0};
        if(!done || !launch_descendant(2,write,done,NULL,GetCurrentProcessId(),orphan,&pi))verdict(0,"stage=descendant-root-create\n");
        if(WaitForSingleObject(pi.hProcess,30000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess,&code) || code!=0x5102 ||
           WaitForSingleObject(done,30000)!=WAIT_OBJECT_0)verdict(0,"stage=descendant-root-wait\n");
        DWORD parent=GetCurrentProcessId(),seen[3]={0};
        for(unsigned i=0;i<3;++i) {
            struct tree_reply r={0};DWORD bytes=0;
            if(!ReadFile(read,&r,sizeof(r),&bytes,NULL) || bytes!=sizeof(r) || r.flags!=15 || r.level!=2-i || r.parent!=parent || r.pid==GetCurrentProcessId())verdict(0,"stage=descendant-chain-identity\n");
            for(unsigned j=0;j<i;++j)if(seen[j]==r.pid)verdict(0,"stage=descendant-distinct-pid\n");
            seen[i]=r.pid;parent=r.pid;
        }
        if(seen[0]!=pi.dwProcessId)verdict(0,"stage=descendant-root-pid\n");
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);CloseHandle(read);CloseHandle(write);CloseHandle(done);
        handle_balance(before,cycle>0,"stage=descendant-handle-balance\n");
        OutputDebugStringA("process-probe: three descendant generations complete\n");
    }
    InterlockedExchange(&running,0);WaitForSingleObject(thread,10000);CloseHandle(thread);
    verdict(1,"stage=descendant-complete\n");
}
static void java_probe(void)
{
    WCHAR java[1024],classes[1024],command[2400];
    DWORD java_length=GetEnvironmentVariableW(L"PS5_PROCESS_JAVA",java,1024);
    DWORD class_length=GetEnvironmentVariableW(L"PS5_PROCESS_CLASSES",classes,1024);
    if(!java_length || java_length>=1024 || !class_length || class_length>=1024)verdict(0,"stage=java-configuration\n");
    const WCHAR *parts[]={L"\"",java,L"\" -Xint -Xms16m -Xmx64m -XX:+UseSerialGC -XX:ActiveProcessorCount=2 -Djava.awt.headless=true -cp \"",classes,L"\" JavaProcessProbe"};
    WCHAR *end=command;
    for(unsigned i=0;i<5;++i)for(const WCHAR *p=parts[i];*p;++p)*end++=*p;
    *end=0;
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");
    SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};HANDLE read=NULL,write=NULL;
    if(!CreatePipe(&read,&write,&sa,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0))verdict(0,"stage=java-pipe\n");
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,NULL);
    STARTUPINFOW startup={0};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
    startup.hStdInput=input;startup.hStdOutput=startup.hStdError=write;
    PROCESS_INFORMATION pi={0};
    if(input==INVALID_HANDLE_VALUE || !CreateProcessW(java,command,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,classes,&startup,&pi))verdict(0,"stage=java-start\n");
    CloseHandle(write);CloseHandle(input);CloseHandle(pi.hThread);
    char output[32768];DWORD used=0,count=0,code=0;
    while(used<sizeof(output)-1 && ReadFile(read,output+used,sizeof(output)-1-used,&count,NULL) && count)used+=count;
    output[used]=0;CloseHandle(read);
    /* Log verdict lines, not the large synthetic pipe payload. */
    for(char *line=output;*line;) {
        char *next=line;while(*next && *next!='\n')++next;
        char saved=*next;*next=0;
        if(line[0]=='J' && line[1]=='A' && line[2]=='V' && line[3]=='A')OutputDebugStringA(line);
        *next=saved;line=saved ? next+1 : next;
    }
    if(WaitForSingleObject(pi.hProcess,10000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess,&code) || code ||
       !contains(output,"JAVA_PROCESS_ROOT PASS")) { OutputDebugStringA(output);verdict(0,"stage=java-result\n"); }
    CloseHandle(pi.hProcess);verdict(1,"stage=java-processbuilder-complete\n");
}
static void graphics_parent(int check_limit)
{
    /* OutputDebugString lazily opens its reporting directory/mutex. Warm it
     * before the handle baseline, as repeated lifecycle probes already do. */
    OutputDebugStringA("graphics-parent: starting lifecycle check\n");
    DWORD before=settled_handles(),bytes=0,child_pid=0,code=~0u;
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};
    HANDLE read,write;
    if(!CreatePipe(&read,&write,&security,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0))
        verdict(0,"stage=graphics-pipe\n");
    char value[32]={0},*p=value;append_number(&p,GetCurrentProcessId());
    SetEnvironmentVariableA("PS5_GRAPHICS_PARENT",value+1);
    p=value;append_number(&p,(ULONG_PTR)write);
    SetEnvironmentVariableA("PS5_GRAPHICS_PIPE",value+1);
    SetEnvironmentVariableA("PS5_PROCESS_COOKIE","native-child");
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");
    SetEnvironmentVariableA("WINE_PS5_GRAPHICS_CHILD","1");
    WCHAR command[]=L"C:\\graphics-render-probe.exe";
    STARTUPINFOW startup={0};PROCESS_INFORMATION child={0};startup.cb=sizeof(startup);
    if(!CreateProcessW(NULL,command,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,&child))
        verdict(0,"stage=graphics-create\n");
    CloseHandle(write);
    int ok=ReadFile(read,&child_pid,sizeof(child_pid),&bytes,NULL) && bytes==sizeof(child_pid) &&
        child_pid==child.dwProcessId && child_pid!=GetCurrentProcessId();
    CloseHandle(read);
    if(check_limit) {
        PROCESS_INFORMATION rejected={0};WCHAR again[]=L"C:\\graphics-render-probe.exe";
        SetLastError(0);
        if(CreateProcessW(NULL,again,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,&rejected) || GetLastError()!=ERROR_BUSY)
            verdict(0,"stage=second-graphics-rejection\n");
        OutputDebugStringA("PS5_PROTON_GRAPHICS_SECOND_REJECTED PASS\n");
    }
    ok=ok && WaitForSingleObject(child.hProcess,60000)==WAIT_OBJECT_0 &&
        GetExitCodeProcess(child.hProcess,&code) && code==0;
    CloseHandle(child.hThread);CloseHandle(child.hProcess);
    handle_balance(before,1,"stage=graphics-handles\n");
    verdict(ok,"stage=graphics-child-render-exit\n");
}
/* Alpha 7's default ("Run inside MihawkOS", wine/ps5/pw_wine_title_game.h): copies of the renderer in a library folder,
 * none named to the title. game64.exe goes into the title; while it runs, second64.exe (the title takes one), game32.exe
 * (32-bit) and optout64.exe (the run's WINE_PS5_TITLE_OPTOUT) go to helpers. The title and the helpers' logs say where
 * each ran; here each helper copy must draw and end with 0. The session ends with game64.exe. */
static void title_games_parent(void)
{
    static const WCHAR *const copies[][2]={
        {L"C:\\d3d11-probe.exe",L"C:\\users\\Mihawk\\Games\\Probe\\game64.exe"},
        {L"C:\\d3d11-probe.exe",L"C:\\users\\Mihawk\\Games\\Probe\\second64.exe"},
        {L"C:\\d3d11-32-probe.exe",L"C:\\users\\Mihawk\\Games\\Probe\\game32.exe"},
        {L"C:\\d3d11-probe.exe",L"C:\\users\\Mihawk\\Games\\Probe\\optout64.exe"}};
    CreateDirectoryW(L"C:\\users\\Mihawk\\Games",NULL);CreateDirectoryW(L"C:\\users\\Mihawk\\Games\\Probe",NULL);
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");
    SetEnvironmentVariableA("PS5_PROTON_PROBE_FRAMES","8");   /* the helpers' copies; the title's reads the title's own */
    PROCESS_INFORMATION children[4]={{0}};
    int okay=1;
    for(unsigned i=0;i<4;i++) {
        if(!CopyFileW(copies[i][0],copies[i][1],FALSE))verdict(0,"stage=title-games-copy\n");
        STARTUPINFOW startup={0};startup.cb=sizeof(startup);
        WCHAR command[96];lstrcpyW(command,copies[i][1]);
        if(!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&children[i]))verdict(0,"stage=title-games-create\n");
        char line[96]="title-games: started copy";char *p=line;while(*p)++p;
        append_number(&p,i);append_number(&p,children[i].dwProcessId);*p++='\n';*p=0;OutputDebugStringA(line);
        if(!i)Sleep(3000);   /* game64.exe in the title first */
    }
    for(unsigned i=1;i<4;i++) {
        DWORD code=~0u;
        okay &= WaitForSingleObject(children[i].hProcess,90000)==WAIT_OBJECT_0 && GetExitCodeProcess(children[i].hProcess,&code) && !code;
        char line[96]="title-games: copy";char *p=line;while(*p)++p;
        append_number(&p,i);p[0]=' ';p[1]='e';p[2]='x';p[3]='i';p[4]='t';p+=5;append_number(&p,code);*p++='\n';*p=0;OutputDebugStringA(line);
        CloseHandle(children[i].hThread);CloseHandle(children[i].hProcess);
    }
    if(okay)OutputDebugStringA("PS5_PROTON_TITLE_GAMES PASS: the helpers' copies drew and ended\n");
    verdict(okay,"stage=title-games\n");
}
static void graphics_broker_parent(void)
{
    OutputDebugStringA("graphics-broker: starting two independent renderers\n");
    DWORD before=settled_handles(),bytes=0,pids[2]={0},code=~0u;
    SECURITY_ATTRIBUTES security={sizeof(security),NULL,TRUE};HANDLE read,write;
    if(!CreatePipe(&read,&write,&security,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0))verdict(0,"stage=broker-pipe\n");
    char value[32]={0},*p=value;append_number(&p,GetCurrentProcessId());SetEnvironmentVariableA("PS5_GRAPHICS_PARENT",value+1);
    p=value;append_number(&p,(ULONG_PTR)write);SetEnvironmentVariableA("PS5_GRAPHICS_PIPE",value+1);
    SetEnvironmentVariableA("PS5_PROCESS_COOKIE","native-child");
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");SetEnvironmentVariableA("WINE_PS5_GRAPHICS_CHILD","1");
    SetEnvironmentVariableA("PS5_GRAPHICS_BROKER","1");SetEnvironmentVariableA("PS5_GRAPHICS_DEPTH","0");
    /* 8 frames, unless the run named its own (the title watchdog's healthy case draws for minutes); waited for in kind. */
    char named[16]={0};unsigned frames=0;
    GetEnvironmentVariableA("PS5_PROTON_PROBE_FRAMES",named,sizeof(named));
    for(const char *d=named;*d>='0' && *d<='9';++d)frames=frames*10+(unsigned)(*d-'0');
    if(!frames) { frames=8;SetEnvironmentVariableA("PS5_PROTON_PROBE_FRAMES","8"); }
    const DWORD wait=60000+frames*50;
    SetEnvironmentVariableA("PS5_PROTON_PROBE_WIDTH","1920");SetEnvironmentVariableA("PS5_PROTON_PROBE_HEIGHT","1080");
    STARTUPINFOW startup={0};PROCESS_INFORMATION children[2]={{0}};startup.cb=sizeof(startup);
    for(unsigned i=0;i<2;i++) {
        WCHAR command[]=L"C:\\graphics-render-probe.exe";
        if(!CreateProcessW(NULL,command,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,&children[i]))verdict(0,"stage=broker-create\n");
    }
    CloseHandle(write);
    for(unsigned i=0;i<2;i++)if(!ReadFile(read,&pids[i],sizeof(pids[i]),&bytes,NULL) || bytes!=sizeof(pids[i]))verdict(0,"stage=broker-pid-pipe\n");
    CloseHandle(read);
    int okay=pids[0]!=pids[1] && ((pids[0]==children[0].dwProcessId && pids[1]==children[1].dwProcessId) ||
        (pids[1]==children[0].dwProcessId && pids[0]==children[1].dwProcessId));
    for(unsigned i=0;i<2;i++) {
        okay &= WaitForSingleObject(children[i].hProcess,wait)==WAIT_OBJECT_0 && GetExitCodeProcess(children[i].hProcess,&code) && !code;
        CloseHandle(children[i].hThread);CloseHandle(children[i].hProcess);
    }
    handle_balance(before,1,"stage=broker-handles\n");
    if(okay)OutputDebugStringA("PS5_PROTON_GRAPHICS_BROKER PASS: two rendering children and rendering descendants\n");
    verdict(okay,"stage=broker-graphics-lifecycle\n");
}
/* One 1920x1080 rendering child per cycle. Both cycles reach a readback and
 * Present before exit; the second uses Windows TerminateProcess with a live
 * device. Logical process completion is checked here, native absence and
 * GPU drain must additionally pass the owner's/broker's evidence checks. */
static void graphics_teardown_parent(int owner_cancel)
{
    SECURITY_ATTRIBUTES sa = {sizeof(sa), NULL, TRUE};
    SetEnvironmentVariableA("PS5_PROCESS_COOKIE", "native-child");
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS", "1");
    SetEnvironmentVariableA("PS5_GRAPHICS_BROKER", "1");
    SetEnvironmentVariableA("PS5_PROTON_PROBE_FRAMES", "4");
    SetEnvironmentVariableA("PS5_PROTON_PROBE_WIDTH", "1920");
    SetEnvironmentVariableA("PS5_PROTON_PROBE_HEIGHT", "1080");
    for (unsigned cycle = 0; cycle < 2; ++cycle) {
        const DWORD before = settled_handles();
        HANDLE read = NULL, write = NULL;
        HANDLE ready = CreateEventA(&sa, TRUE, FALSE, NULL);
        HANDLE finish = CreateEventA(&sa, TRUE, FALSE, NULL);
        if (!ready || !finish || !CreatePipe(&read, &write, &sa, 0) ||
            !SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0)) verdict(0, "stage=teardown-handles\n");
        char value[32], *p = value; value[0] = 0; append_number(&p, GetCurrentProcessId());
        SetEnvironmentVariableA("PS5_GRAPHICS_PARENT", value + 1);
        p = value; append_number(&p, (ULONG_PTR)write); SetEnvironmentVariableA("PS5_GRAPHICS_PIPE", value + 1);
        p = value; append_number(&p, (ULONG_PTR)ready); SetEnvironmentVariableA("PS5_TEARDOWN_READY", value + 1);
        p = value; append_number(&p, (ULONG_PTR)finish); SetEnvironmentVariableA("PS5_TEARDOWN_FINISH", value + 1);
        WCHAR command[] = L"C:\\graphics-render-probe.exe";
        STARTUPINFOW startup = {0}; startup.cb = sizeof(startup);
        PROCESS_INFORMATION child = {0}; DWORD reported = 0, bytes = 0, code = ~0u;
        if (!CreateProcessW(NULL, command, NULL, NULL, TRUE, DETACHED_PROCESS, NULL, NULL, &startup, &child))
            verdict(0, "stage=teardown-create\n");
        CloseHandle(write);
        if (WaitForSingleObject(ready, 25000) != WAIT_OBJECT_0 ||
            !ReadFile(read, &reported, sizeof(reported), &bytes, NULL) ||
            bytes != sizeof(reported) || reported != child.dwProcessId)
            verdict(0, "stage=teardown-render-ready\n");
        if (owner_cancel) {
            /* The owner must cancel the whole session while this child waits
             * with a verified live GPU device. No parent-side close request. */
            OutputDebugStringA("PS5_PROTON_GRAPHICS_OWNER_CANCEL READY: parent and rendered child alive\n");
            Sleep(60000);
            verdict(0, "stage=owner-cancellation-not-delivered\n");
        }
        OutputDebugStringA(cycle ? "graphics-teardown: request=TerminateProcess\n" :
                                  "graphics-teardown: request=normal-exit\n");
        if (!(cycle ? TerminateProcess(child.hProcess, 0x4567) : SetEvent(finish)))
            verdict(0, "stage=teardown-request\n");
        if (WaitForSingleObject(child.hProcess, 10000) != WAIT_OBJECT_0 ||
            !GetExitCodeProcess(child.hProcess, &code) || code != (cycle ? 0x4567u : 0u))
            verdict(0, "stage=teardown-exit-code\n");
        CloseHandle(child.hThread); CloseHandle(child.hProcess);
        CloseHandle(read); CloseHandle(ready); CloseHandle(finish);
        Sleep(3000); /* bounded observation interval for native monitor teardown */
        /* The first GUI child lazily opens the host Wine desktop/station.
         * As in descendant_root, measure warmup and require balance afterward. */
        handle_balance(before, cycle > 0, "stage=teardown-handle-balance\n");
        OutputDebugStringA(cycle ? "PS5_PROTON_GRAPHICS_TEARDOWN PASS: terminated rendering child\n" :
                                  "PS5_PROTON_GRAPHICS_TEARDOWN PASS: normal rendering child\n");
    }
    verdict(1, "stage=graphics-teardown-complete\n");
}
/* A real launcher and its game, as the descendant route carries them. C:\spawn-game.txt holds
 * KEY=VALUE lines (a file, as the title's environment does not reach this first helper): COMMAND
 * (a Java launcher, which starts Java) is started in DIR with descendants enabled. The game, a child of
 * the launcher named GAME_EXE, is awaited, then the line MARKER in its log LOG (written since the start),
 * then the game's window (class WINDOW_CLASS) and PLAY_SECONDS of play; WM_CLOSE then asks the game to quit as its own close button does (a Java game saves the world
 * on the way out), and the game and the launcher are awaited. Nothing is killed while it may still
 * be drawing. */
typedef BOOL (WINAPI *EnumWindowsFn)(WNDENUMPROC,LPARAM);
typedef int (WINAPI *GetClassNameFn)(HWND,LPSTR,int);
typedef DWORD (WINAPI *GetWindowThreadProcessIdFn)(HWND,DWORD *);
typedef BOOL (WINAPI *PostMessageFn)(HWND,UINT,WPARAM,LPARAM);
struct game_window { const char *cls; GetClassNameFn class_name; GetWindowThreadProcessIdFn owner; HWND hwnd; DWORD pid; };
static BOOL CALLBACK find_game_window(HWND hwnd,LPARAM data)
{
    struct game_window *w=(void *)data;char name[64]={0};
    if(w->class_name(hwnd,name,sizeof(name)) && contains(name,w->cls)) {
        w->hwnd=hwnd;w->owner(hwnd,&w->pid);return FALSE;
    }
    return TRUE;
}
static void spawn_say(const char *text,unsigned long long value)
{
    char line[256]="spawn-game: ";char *p=line;while(*p)++p;
    while(*text && p<line+200)*p++=*text++;
    *p=0;append_number(&p,value);*p++='\n';*p=0;OutputDebugStringA(line);
}
static int log_contains(const char *path,const char *marker)
{
    HANDLE file=CreateFileA(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,NULL,OPEN_EXISTING,0,NULL);
    if(file==INVALID_HANDLE_VALUE)return 0;
    static char chunk[65537];DWORD got=0;int found=0;LARGE_INTEGER size={0};
    if(GetFileSizeEx(file,&size)) {
        LARGE_INTEGER at;at.QuadPart=size.QuadPart>65536 ? size.QuadPart-65536 : 0;
        if(SetFilePointerEx(file,at,NULL,FILE_BEGIN) && ReadFile(file,chunk,65536,&got,NULL)) {
            for(DWORD i=0;i<got;++i)if(!chunk[i])chunk[i]=' ';
            chunk[got]=0;found=contains(chunk,marker);
        }
    }
    CloseHandle(file);return found;
}
static char spawn_settings[8192];
static int setting(const char *key,char *out,unsigned size)
{
    for(const char *line=spawn_settings;*line;) {
        unsigned i=0;while(key[i] && line[i]==key[i])++i;
        if(!key[i] && line[i]=='=') {
            const char *v=line+i+1;unsigned n=0;
            while(v[n] && v[n]!='\n' && v[n]!='\r' && n+1<size){out[n]=v[n];++n;}
            out[n]=0;return n>0;
        }
        while(*line && *line!='\n')++line;
        if(*line)++line;
    }
    return 0;
}
static unsigned env_number(const char *name,unsigned fallback)
{
    char value[24];const char *p=value;
    if(!setting(name,value,sizeof(value)))return fallback;
    return (unsigned)number(&p);
}
static HANDLE spawn_launcher;
static void spawn_fail_before_game(const char *stage)
{
    if(spawn_launcher && WaitForSingleObject(spawn_launcher,0)==WAIT_TIMEOUT) {
        OutputDebugStringA("spawn-game: no game is drawing; ending the launcher\n");
        TerminateProcess(spawn_launcher,0x4c);WaitForSingleObject(spawn_launcher,10000);
    }
    verdict(0,stage);
}
static int log_is_fresh(const char *path,const FILETIME *since)
{
    WIN32_FILE_ATTRIBUTE_DATA data;
    if(!GetFileAttributesExA(path,GetFileExInfoStandard,&data))return 0;
    return CompareFileTime(&data.ftLastWriteTime,since)>0;
}
/* The game: a child of the launcher by its executable's name. Found with a process snapshot, which needs
 * kernel32 only: user32 and the DLLs it brings stay unloaded until the game is in its world, as the title
 * and all its children share one count of files opened by path, DLL images included (2026-10; record removed from the tree). */
static DWORD find_game_process(DWORD launcher,const char *exe,DWORD skip)
{
    HANDLE snap=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);PROCESSENTRY32 entry;DWORD found=0;
    if(snap==INVALID_HANDLE_VALUE)return 0;
    entry.dwSize=sizeof(entry);
    for(BOOL more=Process32First(snap,&entry);more && !found;more=Process32Next(snap,&entry))
        if(entry.th32ParentProcessID==launcher && entry.th32ProcessID!=skip && !lstrcmpiA(entry.szExeFile,exe))found=entry.th32ProcessID;
    CloseHandle(snap);return found;
}
static void spawn_game(void)
{
    static char command[2048],directory[512],log_path[512],marker[128],cls[64],exe[64];
    WCHAR wide[2048],wide_dir[512];
    HANDLE file=CreateFileA("C:\\spawn-game.txt",GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,0,NULL);DWORD got=0;
    if(file==INVALID_HANDLE_VALUE || !ReadFile(file,spawn_settings,sizeof(spawn_settings)-1,&got,NULL))verdict(0,"stage=spawn-settings-file\n");
    CloseHandle(file);spawn_settings[got]=0;
    if(!setting("COMMAND",command,sizeof(command)) || !setting("DIR",directory,sizeof(directory)) ||
       !setting("LOG",log_path,sizeof(log_path)) || !setting("MARKER",marker,sizeof(marker)) ||
       !setting("WINDOW_CLASS",cls,sizeof(cls)) || !setting("GAME_EXE",exe,sizeof(exe)))verdict(0,"stage=spawn-settings\n");
    const unsigned world_seconds=env_number("WORLD_SECONDS",600),play_seconds=env_number("PLAY_SECONDS",60);
    const unsigned exit_seconds=env_number("EXIT_SECONDS",120),fresh_log=env_number("LOG_FRESH",1);
    if(!MultiByteToWideChar(CP_UTF8,0,command,-1,wide,2048) || !MultiByteToWideChar(CP_UTF8,0,directory,-1,wide_dir,512))
        verdict(0,"stage=spawn-wide\n");
    FILETIME started;GetSystemTimeAsFileTime(&started);
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");
    STARTUPINFOW startup={0};PROCESS_INFORMATION launcher={0};startup.cb=sizeof(startup);
    OutputDebugStringA("spawn-game: starting the launcher\n");
    if(!CreateProcessW(NULL,wide,NULL,NULL,FALSE,0,NULL,wide_dir,&startup,&launcher))verdict(0,"stage=spawn-launcher\n");
    spawn_say("launcher pid",launcher.dwProcessId);spawn_launcher=launcher.hProcess;
    DWORD code=0,game_pid=0,skip=0;HANDLE game=NULL;unsigned waited=0,game_seen=0,in_world=0;
    for(;waited<world_seconds && !in_world;++waited) {
        if(WaitForSingleObject(launcher.hProcess,1000)==WAIT_OBJECT_0) {
            GetExitCodeProcess(launcher.hProcess,&code);spawn_say("launcher exited before the game's world, code",code);
            verdict(0,"stage=spawn-launcher-exited\n");
        }
        if(!game && (game_pid=find_game_process(launcher.dwProcessId,exe,skip))) {
            game=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,game_pid);game_seen=waited;
            spawn_say("game process seconds",waited);spawn_say("game pid",game_pid);
        }
        if(game && WaitForSingleObject(game,0)==WAIT_OBJECT_0) {
            GetExitCodeProcess(game,&code);CloseHandle(game);game=NULL;
            /* The launcher checks its Java first, in a process of the same name that exits at once with 0. */
            if(!code && waited-game_seen<20) { spawn_say("a short run of the game executable ended (a check), code",code);skip=game_pid;continue; }
            spawn_say("game exited before its world, code",code);
            spawn_fail_before_game("stage=spawn-game-exited\n");
        }
        in_world=game && (!fresh_log || log_is_fresh(log_path,&started)) && log_contains(log_path,marker);
    }
    if(!in_world) {
        spawn_say("no world after seconds",waited);
        if(!game)spawn_fail_before_game("stage=spawn-no-game\n");
    } else {
        spawn_say("world seconds",waited);
        OutputDebugStringA("PS5_PROTON_SPAWN_GAME WORLD\n");
    }
    /* Only now user32: the game's window, for the close request as its own close button makes it. */
    HMODULE user=LoadLibraryA("user32.dll");
    struct game_window w={cls,user ? (GetClassNameFn)GetProcAddress(user,"GetClassNameA") : NULL,
                          user ? (GetWindowThreadProcessIdFn)GetProcAddress(user,"GetWindowThreadProcessId") : NULL,NULL,0};
    EnumWindowsFn enum_windows=user ? (EnumWindowsFn)GetProcAddress(user,"EnumWindows") : NULL;
    PostMessageFn post=user ? (PostMessageFn)GetProcAddress(user,"PostMessageA") : NULL;
    if(!w.class_name || !w.owner || !enum_windows || !post)verdict(0,"stage=spawn-user32\n");
    for(unsigned i=0;i<30 && !w.hwnd;++i) { enum_windows(find_game_window,(LPARAM)&w);if(!w.hwnd)Sleep(1000); }
    if(!w.hwnd)verdict(0,"stage=spawn-game-window\n");
    spawn_say("game window pid",w.pid);
    if(in_world)Sleep(play_seconds*1000);
    if(WaitForSingleObject(game,0)==WAIT_OBJECT_0)verdict(0,"stage=spawn-game-gone-before-close\n");
    if(!post(w.hwnd,0x0010 /* WM_CLOSE */,0,0))verdict(0,"stage=spawn-close-request\n");
    OutputDebugStringA("spawn-game: asked the game window to close\n");
    if(WaitForSingleObject(game,exit_seconds*1000)!=WAIT_OBJECT_0)verdict(0,"stage=spawn-game-exit\n");
    GetExitCodeProcess(game,&code);spawn_say("game exit code",code);CloseHandle(game);
    const DWORD game_code=code;
    if(WaitForSingleObject(launcher.hProcess,60000)!=WAIT_OBJECT_0) {
        OutputDebugStringA("spawn-game: the launcher stayed after its game; ending it\n");
        TerminateProcess(launcher.hProcess,0x4c);WaitForSingleObject(launcher.hProcess,10000);
    }
    GetExitCodeProcess(launcher.hProcess,&code);spawn_say("launcher exit code",code);
    CloseHandle(launcher.hThread);CloseHandle(launcher.hProcess);
    if(in_world && !game_code)OutputDebugStringA("PS5_PROTON_SPAWN_GAME PASS\n");
    verdict(in_world && !game_code,"stage=spawn-game-complete\n");
}
/* --file-budget: more Windows files open at once, across processes, than the console's budget of about 250 open
 * files for a title and its children (fd-budget-01). The root makes 450 files of 8 KiB, holds 150 of them open and
 * starts three descendants that hold 100 each; once all 450 are open, every process reads each of its files four
 * times, 512 bytes at the file pointer, so a file the server parked and reopened between two reads must come back
 * at the right place. Each 512-byte block names its file and block. Every handle stays open until all have read. */
enum { BUDGET_FILES = 450, BUDGET_ROOT = 150, BUDGET_CHILD = 100, BUDGET_BLOCK = 512, BUDGET_ROUNDS = 4 };
static void budget_name(unsigned index,char *out)
{
    const char *base="C:\\file-budget\\f";char *p=out;while(*base)*p++=*base++;
    *p++='0'+index/100;*p++='0'+index/10%10;*p++='0'+index%10;*p=0;
}
static void budget_block(unsigned index,unsigned block,unsigned char *data)
{
    for(unsigned i=0;i<BUDGET_BLOCK;++i)data[i]=(unsigned char)(index*7+block*31+i);
    ((DWORD *)data)[0]=index;((DWORD *)data)[1]=block;
}
static void budget_say(const char *text,unsigned long long a,unsigned long long b)
{
    char line[160]="file-budget: ";char *p=line;while(*p)++p;while(*text)*p++=*text++;
    append_number(&p,a);append_number(&p,b);*p++='\n';*p=0;OutputDebugStringA(line);
}
/* Opens files first..first+count-1 into handles; how many opened. */
static unsigned budget_open(unsigned first,unsigned count,HANDLE *handles,DWORD *error)
{
    unsigned n=0;*error=0;
    for(;n<count;++n) {
        char name[64];budget_name(first+n,name);
        handles[n]=CreateFileA(name,GENERIC_READ,FILE_SHARE_READ,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
        if(handles[n]==INVALID_HANDLE_VALUE) { *error=GetLastError();break; }
    }
    return n;
}
/* Reads every handle's next block, BUDGET_ROUNDS times; the blocks that did not match. */
static unsigned budget_read(unsigned first,unsigned count,HANDLE *handles,DWORD *error)
{
    unsigned bad=0;*error=0;
    for(unsigned round=0;round<BUDGET_ROUNDS;++round)
        for(unsigned n=0;n<count;++n) {
            unsigned char data[BUDGET_BLOCK],expected[BUDGET_BLOCK];DWORD bytes=0;
            budget_block(first+n,round,expected);
            if(!ReadFile(handles[n],data,BUDGET_BLOCK,&bytes,NULL) || bytes!=BUDGET_BLOCK) { if(!*error)*error=GetLastError();++bad;continue; }
            for(unsigned i=0;i<BUDGET_BLOCK;++i)if(data[i]!=expected[i]) { ++bad;break; }
        }
    return bad;
}
static void file_budget_child(const char *args)
{
    const unsigned first=(unsigned)number(&args),count=(unsigned)number(&args);
    static HANDLE handles[BUDGET_CHILD];DWORD error=0;
    HANDLE opened=OpenSemaphoreA(SEMAPHORE_MODIFY_STATE,FALSE,"ps5-file-budget-opened");
    HANDLE go=OpenEventA(SYNCHRONIZE,FALSE,"ps5-file-budget-go"),done=OpenEventA(SYNCHRONIZE,FALSE,"ps5-file-budget-done");
    HANDLE read=OpenSemaphoreA(SEMAPHORE_MODIFY_STATE,FALSE,"ps5-file-budget-read");
    if(!opened || !go || !done || !read || count>BUDGET_CHILD)ExitProcess(2);
    const unsigned n=budget_open(first,count,handles,&error);
    budget_say("child opened (count, error)",n,error);
    ReleaseSemaphore(opened,1,NULL);
    if(WaitForSingleObject(go,180000)!=WAIT_OBJECT_0)ExitProcess(3);
    const unsigned bad=n==count ? budget_read(first,count,handles,&error) : count;
    budget_say("child read (bad blocks, error)",bad,error);
    ReleaseSemaphore(read,1,NULL);
    WaitForSingleObject(done,180000);
    for(unsigned i=0;i<n;++i)CloseHandle(handles[i]);
    ExitProcess(n==count && !bad ? 0x600d : 0x100+(n<count ? 1 : 2));
}
static void file_budget_root(void)
{
    static HANDLE handles[BUDGET_ROOT];static unsigned char data[BUDGET_BLOCK*BUDGET_ROUNDS*4];
    DWORD error=0,bytes=0;
    SetEnvironmentVariableA("WINE_PS5_DESCENDANTS","1");
    if(!CreateDirectoryA("C:\\file-budget",NULL) && GetLastError()!=ERROR_ALREADY_EXISTS)verdict(0,"stage=file-budget-directory\n");
    for(unsigned i=0;i<BUDGET_FILES;++i) {
        char name[64];budget_name(i,name);
        for(unsigned b=0;b<BUDGET_ROUNDS*4;++b)budget_block(i,b,data+b*BUDGET_BLOCK);
        HANDLE file=CreateFileA(name,GENERIC_WRITE,0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
        if(file==INVALID_HANDLE_VALUE || !WriteFile(file,data,sizeof(data),&bytes,NULL) || bytes!=sizeof(data)) {
            budget_say("could not make file (index, error)",i,GetLastError());verdict(0,"stage=file-budget-make\n");
        }
        CloseHandle(file);
    }
    OutputDebugStringA("file-budget: 450 files made\n");
    HANDLE opened=CreateSemaphoreA(NULL,0,16,"ps5-file-budget-opened"),read=CreateSemaphoreA(NULL,0,16,"ps5-file-budget-read");
    HANDLE go=CreateEventA(NULL,TRUE,FALSE,"ps5-file-budget-go"),done=CreateEventA(NULL,TRUE,FALSE,"ps5-file-budget-done");
    if(!opened || !read || !go || !done)verdict(0,"stage=file-budget-sync\n");
    const unsigned held=budget_open(0,BUDGET_ROOT,handles,&error);
    budget_say("root opened (count, error)",held,error);
    PROCESS_INFORMATION children[3]={0};unsigned started=0;
    for(unsigned c=0;c<3;++c) {
        char line[160]="C:\\process-probe.exe --file-budget-child";char *p=line;while(*p)++p;
        append_number(&p,BUDGET_ROOT+c*BUDGET_CHILD);append_number(&p,BUDGET_CHILD);
        WCHAR wide[160];STARTUPINFOW startup={0};startup.cb=sizeof(startup);
        if(MultiByteToWideChar(CP_UTF8,0,line,-1,wide,160) &&
           CreateProcessW(NULL,wide,NULL,NULL,FALSE,DETACHED_PROCESS,NULL,NULL,&startup,&children[c]))++started;
        else budget_say("child not started (index, error)",c,GetLastError());
    }
    unsigned arrived=0;
    for(;arrived<started;++arrived)if(WaitForSingleObject(opened,180000)!=WAIT_OBJECT_0)break;
    budget_say("all opened (children, of)",arrived,started);
    SetEvent(go);
    const unsigned bad=held==BUDGET_ROOT ? budget_read(0,BUDGET_ROOT,handles,&error) : BUDGET_ROOT;
    budget_say("root read (bad blocks, error)",bad,error);
    unsigned reads=0;
    for(;reads<arrived;++reads)if(WaitForSingleObject(read,180000)!=WAIT_OBJECT_0)break;
    SetEvent(done);
    unsigned passed=0;
    for(unsigned c=0;c<started;++c) {
        DWORD code=0;
        if(WaitForSingleObject(children[c].hProcess,60000)==WAIT_OBJECT_0 && GetExitCodeProcess(children[c].hProcess,&code) && code==0x600d)++passed;
        budget_say("child exit (index, code)",c,code);
        CloseHandle(children[c].hThread);CloseHandle(children[c].hProcess);
    }
    for(unsigned i=0;i<held;++i)CloseHandle(handles[i]);
    unsigned removed=0;
    for(unsigned i=0;i<BUDGET_FILES;++i) { char name[64];budget_name(i,name);removed+=DeleteFileA(name)!=0; }
    budget_say("removed (files, directory)",removed,RemoveDirectoryA("C:\\file-budget")!=0);
    budget_say("summary (open handles, children passed)",held+(started==3 && passed==3 ? 300 : 0),passed);
    verdict(held==BUDGET_ROOT && !bad && started==3 && passed==3 && reads==3,"stage=file-budget-complete\n");
}
/* --process-limit: how many helper processes the console runs at once, whether that is a count or a memory budget,
 * and how soon an ended helper's place can be taken again (docs/WINDOWS_PROCESSES.md, "How many processes"). Each
 * child (--limit-child INDEX MEGABYTES) commits and touches its megabytes, says it is ready and waits for its own
 * event; the root starts children one at a time until the console refuses one or sixteen run. It does that with
 * 1 MB children and with 128 MB children, then ends one child and times how long until a new one starts. */
#define LIMIT_MAX 16
static void limit_say(const char *text,unsigned long long a,unsigned long long b)
{
    char line[200]="process-limit: ";char *p=line;while(*p)++p;while(*text)*p++=*text++;
    append_number(&p,a);append_number(&p,b);*p++='\n';*p=0;OutputDebugStringA(line);
}
static void limit_event_name(unsigned index,char *out)
{
    const char *base="ps5-limit-end-";char *p=out;while(*base)*p++=*base++;
    if(index>=10)*p++='0'+index/10;*p++='0'+index%10;*p=0;
}
static void limit_child(const char *args)
{
    const unsigned index=(unsigned)number(&args),megabytes=(unsigned)number(&args);
    char name[32];limit_event_name(index,name);
    HANDLE end=OpenEventA(SYNCHRONIZE,FALSE,name),ready=OpenSemaphoreA(SEMAPHORE_MODIFY_STATE,FALSE,"ps5-limit-ready");
    unsigned char *memory=megabytes ? VirtualAlloc(NULL,(SIZE_T)megabytes<<20,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE) : NULL;
    if(memory) for(SIZE_T i=0;i<((SIZE_T)megabytes<<20);i+=4096)memory[i]=(unsigned char)i;
    if(!end || !ready)ExitProcess(2);
    ReleaseSemaphore(ready,1,NULL);
    WaitForSingleObject(end,600000);
    ExitProcess(megabytes && !memory ? 3 : 0x600d);
}
/* Starts child index with megabytes; 1 when it started and said it is ready. */
static int limit_start(unsigned index,unsigned megabytes,HANDLE ready,PROCESS_INFORMATION *pi,DWORD *error)
{
    char line[160]="C:\\process-probe.exe --limit-child";char *p=line;while(*p)++p;
    append_number(&p,index);append_number(&p,megabytes);
    WCHAR wide[160];STARTUPINFOW startup={0};startup.cb=sizeof(startup);
    *error=0;
    if(!MultiByteToWideChar(CP_UTF8,0,line,-1,wide,160) ||
       !CreateProcessW(NULL,wide,NULL,NULL,FALSE,DETACHED_PROCESS,NULL,NULL,&startup,pi)) { *error=GetLastError();return 0; }
    if(WaitForSingleObject(ready,120000)!=WAIT_OBJECT_0) { *error=WAIT_TIMEOUT;return 0; }
    return 1;
}
static unsigned limit_round(unsigned megabytes,HANDLE ready,PROCESS_INFORMATION *children,DWORD *refusal)
{
    unsigned started=0;
    *refusal=0;
    for(unsigned i=0;i<LIMIT_MAX;++i) {
        DWORD error=0;
        if(!limit_start(i,megabytes,ready,&children[i],&error)) { *refusal=error;break; }
        ++started;
        limit_say("child running (index, megabytes)",i,megabytes);
    }
    return started;
}
static unsigned limit_end(unsigned first,unsigned count,HANDLE *ends,PROCESS_INFORMATION *children)
{
    unsigned clean=0;
    for(unsigned i=first;i<first+count;++i) SetEvent(ends[i]);
    for(unsigned i=first;i<first+count;++i) {
        DWORD code=0;
        if(WaitForSingleObject(children[i].hProcess,60000)==WAIT_OBJECT_0 && GetExitCodeProcess(children[i].hProcess,&code) &&
           code==0x600d)++clean;
        else limit_say("child did not end cleanly (index, code)",i,code);
        CloseHandle(children[i].hThread);CloseHandle(children[i].hProcess);
        ResetEvent(ends[i]);
    }
    return clean;
}
/* Eight real PE64 children remain blocked on distinct events simultaneously. */
static void payload_eight_root(int owner_close)
{
    PROCESS_INFORMATION children[8]={0};HANDLE ends[8],processes[8];
    HANDLE ready=CreateSemaphoreA(NULL,0,8,"ps5-limit-ready");
    unsigned started=0,alive=0;DWORD error=0;
    if(!ready)verdict(0,"stage=payload-eight-semaphore\n");
    for(unsigned i=0;i<8;++i) { char name[32];limit_event_name(i,name);ends[i]=CreateEventA(NULL,TRUE,FALSE,name);
        if(!ends[i])verdict(0,"stage=payload-eight-event\n"); }
    Sleep(5000);
    for(unsigned i=0;i<8;++i) {
        if(!limit_start(i,1,ready,&children[i],&error)) {
            if(children[i].hProcess) { TerminateProcess(children[i].hProcess,1);WaitForSingleObject(children[i].hProcess,10000);CloseHandle(children[i].hProcess);CloseHandle(children[i].hThread); }
            break;
        }
        processes[i]=children[i].hProcess;++started;
    }
    for(unsigned i=0;i<started;++i) { DWORD code=0;alive+=GetExitCodeProcess(processes[i],&code) && code==STILL_ACTIVE; }
    const int simultaneous=started==8 && alive==8 && WaitForMultipleObjects(8,processes,FALSE,0)==WAIT_TIMEOUT;
    limit_say("payload simultaneous children (ready, alive)",started,alive);
    if(owner_close && simultaneous) {
        HANDLE marker=CreateFileA("C:\\payload-owner-ready",GENERIC_WRITE,0,NULL,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,NULL);
        DWORD bytes=0;const DWORD count=8;
        if(marker==INVALID_HANDLE_VALUE || !WriteFile(marker,&count,sizeof(count),&bytes,NULL) || bytes!=sizeof(count))verdict(0,"stage=payload-owner-ready-file\n");
        CloseHandle(marker);OutputDebugStringA("PS5_PROTON_PAYLOAD_OWNER_READY children=8\n");
        for(;;)Sleep(1000); /* The native owner deliberately replaces itself with every child alive. */
    }
    const unsigned clean=limit_end(0,started,ends,children);
    limit_say("payload children ended (clean, error)",clean,error);
    for(unsigned i=0;i<8;++i)CloseHandle(ends[i]);CloseHandle(ready);
    verdict(simultaneous && clean==8,"stage=payload-eight-complete\n");
}
static void process_limit_root(void)
{
    static PROCESS_INFORMATION children[LIMIT_MAX];
    HANDLE ends[LIMIT_MAX];
    HANDLE ready=CreateSemaphoreA(NULL,0,LIMIT_MAX,"ps5-limit-ready");
    for(unsigned i=0;i<LIMIT_MAX;++i) { char name[32];limit_event_name(i,name);ends[i]=CreateEventA(NULL,TRUE,FALSE,name);
        if(!ends[i])verdict(0,"stage=process-limit-sync\n"); }
    if(!ready)verdict(0,"stage=process-limit-sync\n");
    Sleep(5000);   /* wineboot, started with the session, ends first */
    DWORD refusal_light=0,refusal_heavy=0;
    const unsigned light=limit_round(1,ready,children,&refusal_light);
    limit_say("1 MB children running at once (count, refusal error)",light,refusal_light);
    const unsigned light_clean=limit_end(0,light,ends,children);
    const unsigned heavy=limit_round(128,ready,children,&refusal_heavy);
    limit_say("128 MB children running at once (count, refusal error)",heavy,refusal_heavy);
    /* The place of an ended child: end the first, then start a new one as soon as the console takes it. */
    unsigned reused=0;unsigned long long waited=0;
    if(heavy) {
        SetEvent(ends[0]);
        DWORD code=0;
        WaitForSingleObject(children[0].hProcess,60000);GetExitCodeProcess(children[0].hProcess,&code);
        CloseHandle(children[0].hThread);CloseHandle(children[0].hProcess);ResetEvent(ends[0]);
        const ULONGLONG start=GetTickCount64();
        while(GetTickCount64()-start<20000) {
            DWORD error=0;
            if(limit_start(0,128,ready,&children[0],&error)) { reused=1;break; }
            Sleep(100);
        }
        waited=GetTickCount64()-start;
        limit_say("an ended child's place taken again (yes, ms)",reused,waited);
        if(!reused) { children[0].hProcess=GetCurrentProcess();children[0].hThread=NULL; }
    }
    const unsigned heavy_clean=reused || !heavy ? limit_end(0,heavy,ends,children) : limit_end(1,heavy-1,ends,children)+1;
    limit_say("children ended cleanly (1 MB round, 128 MB round)",light_clean,heavy_clean);
    limit_say("summary (1 MB, 128 MB children at once)",light,heavy);
    verdict(light>0 && heavy>0 && light_clean==light && heavy_clean==heavy && reused,"stage=process-limit-complete\n");
}
/* --spawn-wait: a spawn the console refuses waits for a helper's place (WINE_PS5_SPAWN_WAIT, run with 4 s). The root
 * fills the places (a start that fails after the wait marks the count), then starts one more while a thread ends a
 * child 3 s later, which must start once that place is free; then one more with every place taken, which must fail
 * only after the wait. */
static HANDLE wait_release;
static DWORD WINAPI wait_release_later(void *unused)
{
    (void)unused;
    Sleep(3000);
    SetEvent(wait_release);
    return 0;
}
static void spawn_wait_root(void)
{
    static PROCESS_INFORMATION children[LIMIT_MAX];
    HANDLE ends[LIMIT_MAX];
    HANDLE ready=CreateSemaphoreA(NULL,0,LIMIT_MAX,"ps5-limit-ready");
    for(unsigned i=0;i<LIMIT_MAX;++i) { char name[32];limit_event_name(i,name);ends[i]=CreateEventA(NULL,TRUE,FALSE,name);
        if(!ends[i])verdict(0,"stage=spawn-wait-sync\n"); }
    if(!ready)verdict(0,"stage=spawn-wait-sync\n");
    Sleep(5000);
    DWORD refusal=0;
    ULONGLONG start=GetTickCount64();
    const unsigned places=limit_round(1,ready,children,&refusal);
    limit_say("places filled, refused after (count, ms)",places,GetTickCount64()-start);
    if(!places || places>=LIMIT_MAX-1)verdict(0,"stage=spawn-wait-fill\n");
    /* One more, while child 0 ends in 3 s. */
    wait_release=ends[0];
    HANDLE releaser=CreateThread(NULL,0,wait_release_later,NULL,0,NULL);
    DWORD error=0;
    start=GetTickCount64();
    const int freed_start=limit_start(places,1,ready,&children[places],&error);
    const unsigned long long freed_ms=GetTickCount64()-start;
    limit_say("a start while a place came free 3 s later (started, ms)",freed_start,freed_ms);
    WaitForSingleObject(releaser,10000);CloseHandle(releaser);
    DWORD code=0;
    WaitForSingleObject(children[0].hProcess,60000);GetExitCodeProcess(children[0].hProcess,&code);
    CloseHandle(children[0].hThread);CloseHandle(children[0].hProcess);
    /* One more again: every place is taken and none comes free. */
    start=GetTickCount64();
    PROCESS_INFORMATION extra={0};
    const int full_start=limit_start(LIMIT_MAX-1,1,ready,&extra,&error);
    const unsigned long long full_ms=GetTickCount64()-start;
    limit_say("a start with every place taken (started, ms)",full_start,full_ms);
    limit_say("  its error (code, -)",error,0);
    if(full_start) { SetEvent(ends[LIMIT_MAX-1]);WaitForSingleObject(extra.hProcess,60000);CloseHandle(extra.hThread);CloseHandle(extra.hProcess); }
    const unsigned clean=limit_end(1,places,ends,children);
    limit_say("children ended cleanly (count, of)",clean,places);
    verdict(freed_start && freed_ms>=2500 && freed_ms<15000 && !full_start && full_ms>=3500 && clean==places,
            "stage=spawn-wait-complete\n");
}
/* --console-children: a console program started by a program without a console (as a launcher's tool is), with
 * CREATE_NO_WINDOW, and with CREATE_NEW_CONSOLE. A console nobody can see costs a helper (conhost.exe), so the first
 * two start with none and write through the handles they are given; CREATE_NEW_CONSOLE still makes one (patch 0935).
 * The root gives up its own console first, then for each start counts conhost.exe processes while the child runs,
 * reads what the child wrote, and takes the child's word on whether it has a console (exit 0xc0 with, 0xc1 without). */
static unsigned count_processes(const char *exe)
{
    HANDLE snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);
    PROCESSENTRY32 entry;entry.dwSize=sizeof(entry);
    unsigned count=0;
    if(snapshot==INVALID_HANDLE_VALUE)return ~0u;
    for(BOOL more=Process32First(snapshot,&entry);more;more=Process32Next(snapshot,&entry))
        if(!lstrcmpiA(entry.szExeFile,exe))++count;
    CloseHandle(snapshot);
    return count;
}
static void console_child(void)
{
    HANDLE end=OpenEventA(SYNCHRONIZE,FALSE,"ps5-console-end"),ready=OpenSemaphoreA(SEMAPHORE_MODIFY_STATE,FALSE,"ps5-console-ready");
    const int has_console=GetConsoleCP()!=0;
    DWORD written=0;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),"console-child hello\n",20,&written,NULL);
    if(ready)ReleaseSemaphore(ready,1,NULL);
    if(end)WaitForSingleObject(end,60000);
    ExitProcess(has_console ? 0xc0 : 0xc1);
}
static void console_children_root(void)
{
    static const struct { DWORD flags; const char *name; int expect_console; } starts[]={
        {0,"no flags, parent without a console",0},{CREATE_NO_WINDOW,"CREATE_NO_WINDOW",0},
        {CREATE_NEW_CONSOLE,"CREATE_NEW_CONSOLE",1}};
    HANDLE end=CreateEventA(NULL,TRUE,FALSE,"ps5-console-end"),ready=CreateSemaphoreA(NULL,0,4,"ps5-console-ready");
    if(!end || !ready)verdict(0,"stage=console-sync\n");
    FreeConsole();
    const unsigned before=count_processes("conhost.exe");
    limit_say("conhost.exe before (count, -)",before,0);
    unsigned good=0;
    for(unsigned i=0;i<3;++i) {
        SECURITY_ATTRIBUTES inherit={sizeof(inherit),NULL,TRUE};
        HANDLE read_end=NULL,write_end=NULL;
        if(!CreatePipe(&read_end,&write_end,&inherit,0))verdict(0,"stage=console-pipe\n");
        SetHandleInformation(read_end,HANDLE_FLAG_INHERIT,0);
        STARTUPINFOW startup={0};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESTDHANDLES;
        startup.hStdInput=NULL;startup.hStdOutput=write_end;startup.hStdError=write_end;
        WCHAR line[]=L"C:\\process-probe.exe --console-child";
        PROCESS_INFORMATION pi;
        ResetEvent(end);
        if(!CreateProcessW(NULL,line,NULL,NULL,TRUE,starts[i].flags,NULL,NULL,&startup,&pi)) {
            limit_say("console child not started (start, error)",i,GetLastError());CloseHandle(read_end);CloseHandle(write_end);continue;
        }
        CloseHandle(write_end);
        const int arrived=WaitForSingleObject(ready,60000)==WAIT_OBJECT_0;
        const unsigned during=count_processes("conhost.exe");
        char text[64]={0};DWORD got=0;
        ReadFile(read_end,text,20,&got,NULL);
        SetEvent(end);
        DWORD code=0;
        WaitForSingleObject(pi.hProcess,60000);GetExitCodeProcess(pi.hProcess,&code);
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(read_end);
        const int wrote=got==20 && text[0]=='c' && text[14]=='h';
        const int console=code==0xc0, extra=during!=~0u && during>before;
        char label[120]="console start: ";char *p=label;while(*p)++p;for(const char *n=starts[i].name;*n;)*p++=*n++;*p=0;
        OutputDebugStringA(label);OutputDebugStringA("\n");
        limit_say("  conhost.exe while it ran (count, more than before)",during,extra);
        limit_say("  the child wrote through its handles, has a console (yes, yes)",wrote,console);
        if(arrived && wrote && console==starts[i].expect_console && extra==starts[i].expect_console)++good;
    }
    limit_say("summary (starts as expected, of)",good,3);
    verdict(good==3,"stage=console-children-complete\n");
}
/* Two helpers hold 2 GiB each, twice. Verify every physical page, a decommit's
 * zero-filled replacement and its neighbours, then release before reusing the
 * capacity. This must exceed the system pool without relying on swapping. */
static unsigned char *payload_memory_allocate(void)
{
    const SIZE_T bytes=(SIZE_T)2<<30;
    unsigned char *p=VirtualAlloc(NULL,bytes,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!p)verdict(0,"stage=payload-memory-allocate\n");
    for(SIZE_T at=0;at<bytes;at+=16384) {
        p[at]=(unsigned char)(at/16384);p[at+16383]=(unsigned char)~p[at];
    }
    return p;
}
static void payload_memory_release(unsigned char *p)
{
    const SIZE_T bytes=(SIZE_T)2<<30,at=bytes/2;
    for(SIZE_T i=0;i<bytes;i+=16384)
        if(p[i]!=(unsigned char)(i/16384) || p[i+16383]!=(unsigned char)~p[i])
            verdict(0,"stage=payload-memory-readback\n");
    if(!VirtualFree(p+at,16384,MEM_DECOMMIT) ||
       VirtualAlloc(p+at,16384,MEM_COMMIT,PAGE_READWRITE)!=p+at)
        verdict(0,"stage=payload-memory-recommit\n");
    for(SIZE_T i=0;i<16384;++i)if(p[at+i])verdict(0,"stage=payload-memory-zero\n");
    if(p[at-16384]!=(unsigned char)((at-16384)/16384) ||
       p[at+16384]!=(unsigned char)((at+16384)/16384) || !VirtualFree(p,0,MEM_RELEASE))
        verdict(0,"stage=payload-memory-release\n");
}
static void payload_memory_child(const char *args)
{
    HANDLE ready=(HANDLE)(ULONG_PTR)number(&args),done=(HANDLE)(ULONG_PTR)number(&args);
    unsigned char *p=payload_memory_allocate();
    if(!SetEvent(ready) || WaitForSingleObject(done,60000)!=WAIT_OBJECT_0)
        verdict(0,"stage=payload-memory-child-wait\n");
    payload_memory_release(p);
    verdict(1,"stage=payload-memory-child-complete bytes=2147483648\n");
}
static void payload_memory_root(void)
{
    for(unsigned cycle=0;cycle<2;++cycle) {
        SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
        HANDLE ready=CreateEventA(&sa,TRUE,FALSE,NULL),done=CreateEventA(&sa,TRUE,FALSE,NULL);
        unsigned char *p=payload_memory_allocate();
        char line[256]="C:\\process-probe.exe --payload-memory-child";char *end=line;while(*end)++end;
        append_number(&end,(ULONG_PTR)ready);append_number(&end,(ULONG_PTR)done);
        WCHAR wide[256];STARTUPINFOW startup={0};startup.cb=sizeof(startup);
        PROCESS_INFORMATION pi={0};DWORD code=1;
        if(!ready || !done || !MultiByteToWideChar(CP_UTF8,0,line,-1,wide,256) ||
           !CreateProcessW(NULL,wide,NULL,NULL,TRUE,DETACHED_PROCESS,NULL,NULL,&startup,&pi) ||
           WaitForSingleObject(ready,60000)!=WAIT_OBJECT_0)
            verdict(0,"stage=payload-memory-child-ready\n");
        payload_memory_release(p);
        if(!SetEvent(done) || WaitForSingleObject(pi.hProcess,60000)!=WAIT_OBJECT_0 ||
           !GetExitCodeProcess(pi.hProcess,&code) || code)
            verdict(0,"stage=payload-memory-child-exit\n");
        CloseHandle(pi.hProcess);CloseHandle(pi.hThread);CloseHandle(ready);CloseHandle(done);
    }
    verdict(1,"stage=payload-memory-complete simultaneous_bytes=4294967296 cycles=2\n");
}
/* The launcher-and-game pair (docs/TITLE_CLIENT.md, "Choosing the game"): a copy of this program, which imports no
 * graphics API, is a game's launcher in a library folder; it starts a copy of the renderer and exits at once, as a
 * launcher does. The renderer must take the title's place, and the session must outlive the launcher. This root waits
 * for the renderer and passes when it drew and ended with 0. */
static void launcher_pair_start(void)
{
    STARTUPINFOW startup={0};PROCESS_INFORMATION game={0};startup.cb=sizeof(startup);
    WCHAR command[]=L"C:\\users\\Mihawk\\Games\\Pair\\game64.exe";
    if(!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&game))ExitProcess(2);
    OutputDebugStringA("launcher-pair: the launcher started the game and exits\n");
    ExitProcess(0);
}
static void launcher_pair_root(void)
{
    CreateDirectoryW(L"C:\\users\\Mihawk\\Games",NULL);CreateDirectoryW(L"C:\\users\\Mihawk\\Games\\Pair",NULL);
    if(!CopyFileW(L"C:\\process-probe.exe",L"C:\\users\\Mihawk\\Games\\Pair\\start.exe",FALSE) ||
       !CopyFileW(L"C:\\d3d11-probe.exe",L"C:\\users\\Mihawk\\Games\\Pair\\game64.exe",FALSE))verdict(0,"stage=pair-copy\n");
    STARTUPINFOW startup={0};PROCESS_INFORMATION launcher={0};startup.cb=sizeof(startup);
    WCHAR command[]=L"C:\\users\\Mihawk\\Games\\Pair\\start.exe --launcher-pair-start";
    if(!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&launcher))verdict(0,"stage=pair-launcher\n");
    DWORD game_pid=0,code=~0u;
    for(unsigned i=0;i<200 && !(game_pid=find_game_process(launcher.dwProcessId,"game64.exe",0));++i)Sleep(100);
    HANDLE game=game_pid ? OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,game_pid) : NULL;
    const int launcher_ended=WaitForSingleObject(launcher.hProcess,30000)==WAIT_OBJECT_0 && GetExitCodeProcess(launcher.hProcess,&code) && !code;
    OutputDebugStringA(launcher_ended ? "launcher-pair: the launcher ended with 0\n" : "launcher-pair: the launcher did not end with 0\n");
    code=~0u;
    const int game_ended=game && WaitForSingleObject(game,180000)==WAIT_OBJECT_0 && GetExitCodeProcess(game,&code) && !code;
    if(launcher_ended && game_ended)OutputDebugStringA("PS5_PROTON_LAUNCHER_PAIR PASS: the game outlived its launcher and ended with 0\n");
    verdict(launcher_ended && game_ended,"stage=launcher-pair\n");
}
/* A library game whose DLL is missing (tests/missing-import-probe.c): the title takes it, Wine's loader fails at once, and
 * the game ends with STATUS_DLL_NOT_FOUND as on Windows. This root waits for that exit code; the title must then end the
 * session in order (its PS5_PROTON_OWNER_BOOTSTRAP line), not die with the game (a tester's demo, 2026-11-01). */
/* A game in the title whose parent is alive in a helper (tests/parent-query-probe.c): it asks what it learns of this
 * process and of itself, and ends with 0 when every answer is as good for the parent. This root stays its parent until
 * then (a store game ended itself only when its parent was alive: witcher3-dx12-parent-pro-2026-11-01). */
static void parent_query_root(void)
{
    CreateDirectoryW(L"C:\\users\\Mihawk\\Games",NULL);CreateDirectoryW(L"C:\\users\\Mihawk\\Games\\ParentQuery",NULL);
    if(!CopyFileW(L"C:\\parent-query-probe.exe",L"C:\\users\\Mihawk\\Games\\ParentQuery\\game64.exe",FALSE))
        verdict(0,"stage=parent-query-copy\n");
    STARTUPINFOW startup={0};PROCESS_INFORMATION game={0};startup.cb=sizeof(startup);
    WCHAR command[]=L"C:\\users\\Mihawk\\Games\\ParentQuery\\game64.exe";
    if(!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&game))verdict(0,"stage=parent-query-create\n");
    DWORD code=~0u;
    const int ended=WaitForSingleObject(game.hProcess,120000)==WAIT_OBJECT_0 && GetExitCodeProcess(game.hProcess,&code) && !code;
    verdict(ended,"stage=parent-query\n");
}
static DWORD WINAPI missing_import_overlay(void *unused)
{
    (void)unused;
    STARTUPINFOW startup={0};PROCESS_INFORMATION overlay={0};startup.cb=sizeof(startup);
    WCHAR command[]=L"C:\\overlay64.exe";
    OutputDebugStringA("missing-import: asking for the overlay beside the game\n");
    if(CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&overlay)) {
        OutputDebugStringA("missing-import: the overlay started\n");
        CloseHandle(overlay.hProcess);CloseHandle(overlay.hThread);
    } else OutputDebugStringA("missing-import: the overlay did not start\n");
    return 0;
}
static void missing_import_root(void)
{
    CreateDirectoryW(L"C:\\users\\Mihawk\\Games",NULL);CreateDirectoryW(L"C:\\users\\Mihawk\\Games\\Missing",NULL);
    if(!CopyFileW(L"C:\\missing-import-probe.exe",L"C:\\users\\Mihawk\\Games\\Missing\\game64.exe",FALSE))
        verdict(0,"stage=missing-copy\n");
    STARTUPINFOW startup={0};PROCESS_INFORMATION game={0};startup.cb=sizeof(startup);
    WCHAR command[]=L"C:\\users\\Mihawk\\Games\\Missing\\game64.exe";
    if(!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&game))verdict(0,"stage=missing-create\n");
    /* The store client starts its overlay beside each game it starts: a second graphics program, to a helper (the title's
     * place is the game's). Its start was in flight, held by the console's wedged loader, when the demo's loader failed;
     * the battery holds it too (WINE_PS5_TEST_SPAWN_STALL=overlay64.exe), so it is asked from a thread of its own. */
    if(!CopyFileW(L"C:\\d3d11-probe.exe",L"C:\\overlay64.exe",FALSE))verdict(0,"stage=missing-overlay-copy\n");
    HANDLE overlay=CreateThread(NULL,0,missing_import_overlay,NULL,0,NULL);
    if(!overlay)verdict(0,"stage=missing-overlay-thread\n");
    DWORD code=~0u;
    const int ended=WaitForSingleObject(game.hProcess,60000)==WAIT_OBJECT_0 && GetExitCodeProcess(game.hProcess,&code) &&
                    code==0xc0000135;
    char line[96]="missing-import: the game's exit code ";char *end=line;while(*end)++end;
    append_number(&end,code);*end++='\n';*end=0;OutputDebugStringA(line);
    if(ended)OutputDebugStringA("PS5_PROTON_MISSING_IMPORT PASS: the game ended with STATUS_DLL_NOT_FOUND\n");
    verdict(ended,"stage=missing-import\n");
}
/* A Chromium-based game (docs/TITLE_CLIENT.md, "Choosing the game"): a copy of the renderer in a library folder with
 * Chromium's resources.pak, icudtl.dat and V8 snapshot beside it. Kernelbase patch 2404 must start it as one process
 * (the title's WINE_PS5_CHROMIUM_ARGS, --single-process, appended to its command line), and it must draw and end with 0
 * in its helper (it never takes the title's place). This root reads the child's command line back to see the switch. */
static void chromium_game_root(void)
{
    static const WCHAR *const marks[]={L"C:\\users\\Mihawk\\Games\\Chromium\\resources.pak",
        L"C:\\users\\Mihawk\\Games\\Chromium\\icudtl.dat",L"C:\\users\\Mihawk\\Games\\Chromium\\v8_context_snapshot.bin"};
    CreateDirectoryW(L"C:\\users\\Mihawk\\Games",NULL);CreateDirectoryW(L"C:\\users\\Mihawk\\Games\\Chromium",NULL);
    if(!CopyFileW(L"C:\\d3d11-probe.exe",L"C:\\users\\Mihawk\\Games\\Chromium\\game.exe",FALSE))verdict(0,"stage=chromium-copy\n");
    for(unsigned i=0;i<3;i++) {
        HANDLE mark=CreateFileW(marks[i],GENERIC_WRITE,0,NULL,CREATE_ALWAYS,0,NULL);
        if(mark==INVALID_HANDLE_VALUE)verdict(0,"stage=chromium-marks\n");
        CloseHandle(mark);
    }
    STARTUPINFOW startup={0};PROCESS_INFORMATION game={0};startup.cb=sizeof(startup);
    WCHAR command[]=L"\"C:\\users\\Mihawk\\Games\\Chromium\\game.exe\"";
    if(!CreateProcessW(NULL,command,NULL,NULL,FALSE,0,NULL,NULL,&startup,&game))verdict(0,"stage=chromium-create\n");
    typedef LONG (WINAPI *Query)(HANDLE,int,void *,ULONG,ULONG *);
    Query query=(Query)GetProcAddress(GetModuleHandleA("ntdll.dll"),"NtQueryInformationProcess");
    static unsigned char line[4096];ULONG got=0;
    const UNICODE_STRING *text=(const UNICODE_STRING *)line;
    int single=0;
    if(query && !query(game.hProcess,60 /* ProcessCommandLineInformation */,line,sizeof(line),&got) && text->Buffer)
        for(const WCHAR *at=text->Buffer;at+16<=text->Buffer+text->Length/sizeof(WCHAR) && !single;++at)
            single=!memcmp(at,L"--single-process",16*sizeof(WCHAR));
    OutputDebugStringA(single ? "chromium-game: started as one process (--single-process)\n" :
                                "chromium-game: no --single-process on its command line\n");
    DWORD code=~0u;
    const int ended=WaitForSingleObject(game.hProcess,120000)==WAIT_OBJECT_0 && GetExitCodeProcess(game.hProcess,&code) && !code;
    if(single && ended)OutputDebugStringA("PS5_PROTON_CHROMIUM_GAME PASS: one process, drew and ended with 0\n");
    verdict(single && ended,"stage=chromium-game\n");
}
void mainCRTStartup(void)
{
    const char *cmd=GetCommandLineA(), *child=NULL;
    for(const char *p=cmd;*p;++p) {
        const char *q=p,*key="--payload-memory-child ";while(*key && *key==*q){++key;++q;}
        if(!*key)payload_memory_child(q);
    }
    if(contains(cmd,"--payload-memory"))payload_memory_root();
    if(contains(cmd,"--owner-failed"))ExitProcess(0xffffffffu);
    if(contains(cmd,"--spawn-game"))spawn_game();
    for(const char *p=cmd;*p;++p) {
        const char *q=p,*key="--file-budget-child ";while(*key && *key==*q){++key;++q;}
        if(!*key)file_budget_child(q);
    }
    if(contains(cmd,"--file-budget"))file_budget_root();
    for(const char *p=cmd;*p;++p) {
        const char *q=p,*key="--limit-child ";while(*key && *key==*q){++key;++q;}
        if(!*key)limit_child(q);
    }
    if(contains(cmd,"--payload-owner-close"))payload_eight_root(1);
    if(contains(cmd,"--payload-eight"))payload_eight_root(0);
    if(contains(cmd,"--process-limit"))process_limit_root();
    if(contains(cmd,"--spawn-wait"))spawn_wait_root();
    if(contains(cmd,"--console-children"))console_children_root();
    if(contains(cmd,"--console-child"))console_child();
    if(contains(cmd,"--graphics-owner-cancel"))graphics_teardown_parent(1);
    if(contains(cmd,"--graphics-teardown"))graphics_teardown_parent(0);
    if(contains(cmd,"--title-games"))title_games_parent();
    if(contains(cmd,"--chromium-game"))chromium_game_root();
    if(contains(cmd,"--missing-import"))missing_import_root();
    if(contains(cmd,"--parent-query"))parent_query_root();
    if(contains(cmd,"--launcher-pair-start"))launcher_pair_start();
    if(contains(cmd,"--launcher-pair"))launcher_pair_root();
    if(contains(cmd,"--graphics-broker"))graphics_broker_parent();
    if(contains(cmd,"--graphics"))graphics_parent(contains(cmd,"--graphics-console"));
    if(contains(cmd,"--java"))java_probe();
    for(const char *p=cmd;*p;++p) {
        const char *q=p,*key="--descendant-node ";while(*key && *key==*q){++key;++q;}
        if(!*key)descendant_node(q);
    }
    if(contains(cmd,"--descendants"))descendant_root(cmd);
    for (const char *p=cmd; *p; ++p) if(contains(p,"--child ")) {
        // Find the exact occurrence without relying on argv or a C runtime.
        const char *q=p, *s="--child "; while(*s && *q==*s) {++q;++s;}
        if(!*s) { child=q; break; }
    }
    if(child) {
        HANDLE pipe=(HANDLE)(ULONG_PTR)number(&child);
        HANDLE event=(HANDLE)(ULONG_PTR)number(&child);
        HANDLE hidden=(HANDLE)(ULONG_PTR)number(&child);
        DWORD parent=(DWORD)number(&child), bytes=0;
        char env[32];
        const int parent_exit=contains(cmd,"--parent-exit-child");
        HANDLE parent_handle=parent_exit ? OpenProcess(SYNCHRONIZE,FALSE,parent) : NULL;
        struct reply r={GetCurrentProcessId(),0};
        if(parent_handle && WaitForSingleObject(parent_handle,0)==WAIT_TIMEOUT) r.flags|=16;
        if(r.pid!=parent && r.pid) r.flags|=1;
        if(GetEnvironmentVariableA("PS5_PROCESS_COOKIE",env,sizeof(env))==12 &&
           contains(env,"native-child")) r.flags|=2;
        if(contains(cmd,"\"two words\"")) r.flags|=4;
        SetLastError(0);
        if(!SetEvent(hidden) && GetLastError()==ERROR_INVALID_HANDLE) r.flags|=8;
        if(WriteFile(pipe,&r,sizeof(r),&bytes,NULL) && bytes==sizeof(r) && SetEvent(event)) {
            if(parent_exit) {
                int survived=WaitForSingleObject(parent_handle,10000)==WAIT_OBJECT_0;
                CloseHandle(parent_handle);
                OutputDebugStringA(survived ? "PS5_PROTON_PROCESS_SURVIVED_PARENT PASS\n" : "PS5_PROTON_PROCESS_SURVIVED_PARENT FAIL\n");
                ExitProcess(survived ? 0x1234 : 3);
            }
            if(contains(cmd,"--linger")) { Sleep(30000); ExitProcess(99); }
            ExitProcess(0x1234);
        }
        ExitProcess(2);
    }
    HANDLE thread=CreateThread(NULL,0,worker,NULL,0,NULL);
    SetEnvironmentVariableA("PS5_PROCESS_COOKIE","native-child");
    const int suspend=contains(cmd,"--suspended"), terminate=contains(cmd,"--terminate");
    const int parent_exit=contains(cmd,"--parent-exit");
    const int disabled=contains(cmd,"--disabled");
    const int cycles=disabled ? 0 : contains(cmd,"--repeat") ? 4 : 1;
    if(disabled) for(int i=0;i<3;++i) {
        DWORD before=settled_handles();
        WCHAR existing[]=L"C:\\process-probe.exe";
        STARTUPINFOW si={0};PROCESS_INFORMATION pi={0};si.cb=sizeof(si);
        if(CreateProcessW(NULL,existing,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi) ||
           GetLastError()!=ERROR_NOT_SUPPORTED) verdict(0,"stage=default-disabled\n");
        handle_balance(before,1,"stage=disabled-handle-balance\n");
    }
    for(int cycle=0;cycle<cycles;++cycle) {
        DWORD before=settled_handles();
        SECURITY_ATTRIBUTES sa={sizeof(sa),NULL,TRUE};
        HANDLE read=NULL,write=NULL;
        if(!thread || !CreatePipe(&read,&write,&sa,0) || !SetHandleInformation(read,HANDLE_FLAG_INHERIT,0)) verdict(0,"stage=pipe\n");
        HANDLE event=CreateEventA(&sa,TRUE,FALSE,NULL),hidden=CreateEventA(NULL,TRUE,FALSE,NULL);
        char line[512]="C:\\process-probe.exe --child";
        char *end=line; while(*end)++end;
        append_number(&end,(ULONG_PTR)write); append_number(&end,(ULONG_PTR)event);
        append_number(&end,(ULONG_PTR)hidden); append_number(&end,GetCurrentProcessId());
        const char *extra=parent_exit ? " \"two words\" --parent-exit-child" : terminate ? " \"two words\" --linger" : " \"two words\"";
        while(*extra)*end++=*extra++; *end=0;
        WCHAR wide[512];
        if(!MultiByteToWideChar(CP_UTF8,0,line,-1,wide,512)) verdict(0,"stage=wide-command-line\n");
        STARTUPINFOW startup={0}; PROCESS_INFORMATION pi={0}; startup.cb=sizeof(startup);
        OutputDebugStringA("process-probe: calling CreateProcess\n");
        if(!event || !hidden || !CreateProcessW(NULL,wide,NULL,NULL,TRUE,
            CREATE_NO_WINDOW|(suspend?CREATE_SUSPENDED:0),NULL,NULL,&startup,&pi)) verdict(0,"stage=create\n");
        OutputDebugStringA("process-probe: CreateProcess returned\n");
        if(!pi.dwProcessId || pi.dwProcessId==GetCurrentProcessId()) verdict(0,"stage=identity\n");
        if(suspend && (WaitForSingleObject(event,100)!=WAIT_TIMEOUT || ResumeThread(pi.hThread)!=1)) verdict(0,"stage=suspend-resume\n");
        if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0 || WaitForSingleObject(hidden,0)!=WAIT_TIMEOUT) verdict(0,"stage=events\n");
        struct reply r={0}; DWORD bytes=0,code=0;
        if(!ReadFile(read,&r,sizeof(r),&bytes,NULL) || bytes!=sizeof(r) || r.pid!=pi.dwProcessId || r.flags!=(parent_exit?31:15)) verdict(0,"stage=inheritance-args-env\n");
        if(parent_exit) verdict(1,"stage=parent-exit-with-live-child\n");
        if(terminate && (!GetExitCodeProcess(pi.hProcess,&code) || code!=STILL_ACTIVE || !TerminateProcess(pi.hProcess,0x4567))) verdict(0,"stage=terminate\n");
        if(WaitForSingleObject(pi.hProcess,10000)!=WAIT_OBJECT_0 || !GetExitCodeProcess(pi.hProcess,&code) || code!=(terminate?0x4567:0x1234)) verdict(0,"stage=exit-code\n");
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);CloseHandle(read);CloseHandle(write);CloseHandle(event);CloseHandle(hidden);
        handle_balance(before,cycle>0,"stage=handle-balance\n");
        OutputDebugStringA("process-probe: child lifecycle complete\n");
    }
    DWORD before_missing=settled_handles();
    char missing[]="C:\\process-probe-never-exists.exe";
    STARTUPINFOA si={0}; PROCESS_INFORMATION pi={0}; si.cb=sizeof(si);
    if(CreateProcessA(NULL,missing,NULL,NULL,FALSE,CREATE_NO_WINDOW,NULL,NULL,&si,&pi) || GetLastError()!=ERROR_FILE_NOT_FOUND) verdict(0,"stage=missing-image\n");
    handle_balance(before_missing,1,"stage=missing-handle-balance\n");
    InterlockedExchange(&running,0); WaitForSingleObject(thread,10000); CloseHandle(thread);
    verdict(1,"stage=complete\n");
}
