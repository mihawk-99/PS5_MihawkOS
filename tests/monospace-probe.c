/* SPDX-License-Identifier: GPL-3.0-or-later
 * Headless PE32/64 oracle of the monospace face: the session root's initializer (wine/ps5/pw_shell_monospace.h, the
 * very function) against the real registry, then GDI resolving Cascadia Mono, by its name and for Consolas, as a
 * fixed-pitch TrueType face with a bold, as the program after the session would. Owns only the probe prefix: the
 * values and the marker are restored before it returns. */
#include "../wine/ps5/pw_shell_monospace.h"
static unsigned checks,failures;
static void say(const char *text)
{
    DWORD written;WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,lstrlenA(text),&written,NULL);OutputDebugStringA(text);
}
static void check(BOOL okay,const char *name)
{
    char line[256];++checks;if(!okay)++failures;
    wsprintfA(line,"monospace %s: %s\n",okay?"PASS":"FAIL",name);say(line);
}
static BYTE saved[3][2*MAX_PATH];
static DWORD saved_size[3],saved_type[3],saved_status[3];
static void save(HKEY key,const WCHAR *name,unsigned slot)
{
    saved_size[slot]=sizeof(saved[slot]);
    saved_status[slot]=RegQueryValueExW(key,name,NULL,&saved_type[slot],saved[slot],&saved_size[slot]);
}
static BOOL restore(HKEY key,const WCHAR *name,unsigned slot)
{
    if(saved_status[slot]==ERROR_FILE_NOT_FOUND){LONG s=RegDeleteValueW(key,name);return s==ERROR_SUCCESS||s==ERROR_FILE_NOT_FOUND;}
    return RegSetValueExW(key,name,0,saved_type[slot],saved[slot],saved_size[slot])==ERROR_SUCCESS;
}
static BOOL value_is(HKEY key,const WCHAR *name,const WCHAR *want)
{
    WCHAR value[MAX_PATH];DWORD size=sizeof(value),type;
    return RegQueryValueExW(key,name,NULL,&type,(BYTE *)value,&size)==ERROR_SUCCESS&&type==REG_SZ&&!lstrcmpW(value,want);
}
/* The face GDI resolves for a name, and whether it is fixed-pitch TrueType of the asked weight. */
static BOOL resolves(const WCHAR *name,int weight)
{
    HDC dc=CreateCompatibleDC(NULL);
    HFONT font=CreateFontW(-16,0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,name);
    HGDIOBJ previous=SelectObject(dc,font);
    WCHAR face[LF_FACESIZE];TEXTMETRICW metrics;SIZE i,m;
    BOOL okay=GetTextFaceW(dc,LF_FACESIZE,face)&&!lstrcmpW(face,L"Cascadia Mono")&&GetTextMetricsW(dc,&metrics)&&
              !(metrics.tmPitchAndFamily&TMPF_FIXED_PITCH)&&(metrics.tmPitchAndFamily&TMPF_TRUETYPE)&&
              metrics.tmWeight==weight&&GetTextExtentPoint32W(dc,L"iiii",4,&i)&&GetTextExtentPoint32W(dc,L"MMMM",4,&m)&&i.cx==m.cx;
    char line[200];
    wsprintfA(line,"monospace: %ls weight %d -> %ls pitch=%02x weight=%ld iiii=%ld MMMM=%ld\n",name,weight,face,
              metrics.tmPitchAndFamily,metrics.tmWeight,i.cx,m.cx);say(line);
    SelectObject(dc,previous);DeleteObject(font);DeleteDC(dc);
    return okay;
}
/* The font file GDI selects for a name: its size and a checksum of its bytes. GDI reports a substitute under the
 * requested name, so the file, not the face string, says which font it is. */
static BOOL font_file(const WCHAR *name,DWORD *size,DWORD *sum)
{
    HDC dc=CreateCompatibleDC(NULL);
    HFONT font=CreateFontW(-16,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_TT_ONLY_PRECIS,CLIP_DEFAULT_PRECIS,
                           ANTIALIASED_QUALITY,FIXED_PITCH|FF_MODERN,name);
    HGDIOBJ previous=SelectObject(dc,font);
    static BYTE data[1<<20];
    *size=GetFontData(dc,0,0,NULL,0);*sum=0;
    BOOL okay=*size!=GDI_ERROR&&*size<=sizeof(data)&&GetFontData(dc,0,0,data,*size)==*size;
    for(DWORD i=0;okay&&i<*size;i++)*sum=*sum*31+data[i];
    SelectObject(dc,previous);DeleteObject(font);DeleteDC(dc);
    return okay;
}
static BOOL same_file(const WCHAR *asked,const WCHAR *face)
{
    DWORD a_size=0,a_sum=0,f_size=0,f_sum=0;
    BOOL okay=font_file(asked,&a_size,&a_sum)&&font_file(face,&f_size,&f_sum)&&a_size==f_size&&a_sum==f_sum;
    char line[200];wsprintfA(line,"monospace: %ls selects %lu bytes sum %08lx, %ls %lu bytes sum %08lx\n",
                             asked,a_size,a_sum,face,f_size,f_sum);say(line);
    return okay;
}
/* A process started after the substitute exists resolves Consolas to Cascadia Mono (GDI reads it at start). */
static BOOL consolas_in_child(void)
{
    WCHAR command[MAX_PATH+16];STARTUPINFOW startup;PROCESS_INFORMATION process;ZeroMemory(&startup,sizeof(startup));startup.cb=sizeof(startup);DWORD code=1;
    GetModuleFileNameW(NULL,command+1,MAX_PATH);command[0]=L'"';lstrcatW(command,L"\" --consolas");
    if(!CreateProcessW(NULL,command,NULL,NULL,TRUE,0,NULL,NULL,&startup,&process))return FALSE;
    DWORD waited=WaitForSingleObject(process.hProcess,60000);GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hProcess);CloseHandle(process.hThread);
    char line[96];wsprintfA(line,"monospace: the later program waited=%lu exit=%lu\n",waited,code);say(line);
    return code==0;
}
void mainCRTStartup(void)
{
    HKEY settings,substitutes,console;
    const WCHAR *arguments=GetCommandLineW();
    for(const WCHAR *p=arguments;*p;p++)if(!lstrcmpW(p,L"--consolas"))
        ExitProcess(same_file(L"Consolas",L"Cascadia Mono")&&same_file(L"Segoe UI",L"Proton UI")?0:1);
    if(RegCreateKeyExW(HKEY_CURRENT_USER,L"Software\\MihawkOS",0,NULL,0,KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&settings,NULL)||
       RegCreateKeyExW(HKEY_LOCAL_MACHINE,L"Software\\Microsoft\\Windows NT\\CurrentVersion\\FontSubstitutes",0,NULL,0,
                       KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&substitutes,NULL)||
       RegCreateKeyExW(HKEY_CURRENT_USER,L"Console",0,NULL,0,KEY_QUERY_VALUE|KEY_SET_VALUE,NULL,&console,NULL))ExitProcess(2);
    save(settings,L"MonospaceInitialized",0);save(substitutes,L"Consolas",1);save(console,L"FaceName",2);
    WCHAR path[MAX_PATH];GetWindowsDirectoryW(path,MAX_PATH);lstrcatW(path,L"\\Fonts\\CascadiaMono-Bold.ttf");
    check(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES,"Cascadia Mono is staged in C:\\windows\\Fonts");
    RegDeleteValueW(settings,L"MonospaceInitialized");RegDeleteValueW(substitutes,L"Consolas");RegDeleteValueW(console,L"FaceName");
    check(pw_shell_prepare_monospace()==ERROR_SUCCESS,"the face initializes through the real registry");
    check(value_is(substitutes,L"Consolas",L"Cascadia Mono"),"Consolas is substituted by Cascadia Mono");
    check(value_is(console,L"FaceName",L"Cascadia Mono"),"the console's face is Cascadia Mono");
    /* before this process loads any font: a program that waits on another one's font cache stalls on a host */
    /* Segoe UI, the most common UI face, is Proton UI in the prefix's font replacements (wine.inf, patch 2493), not the
       Times New Roman Wine named: the message box and every program asking for it drew serif text (shell81-base-08). */
    {
        HKEY replacements;
        check(!RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Wine\\Fonts\\Replacements",0,KEY_QUERY_VALUE,&replacements)&&
              value_is(replacements,L"Segoe UI",L"Proton UI"),"Segoe UI is replaced by Proton UI, not Times New Roman");
        RegCloseKey(replacements);
    }
    check(consolas_in_child(),"a program started afterwards gets Cascadia Mono for Consolas and Proton UI for Segoe UI");
    check(resolves(L"Cascadia Mono",FW_NORMAL)&&resolves(L"Cascadia Mono",FW_BOLD),"Cascadia Mono resolves, fixed pitch, regular and bold");
    RegDeleteValueW(settings,L"MonospaceInitialized");RegDeleteValueW(substitutes,L"Consolas");
    RegSetValueExW(console,L"FaceName",0,REG_SZ,(BYTE *)L"Lucida Console",sizeof(L"Lucida Console"));
    check(pw_shell_prepare_monospace()==ERROR_SUCCESS&&value_is(console,L"FaceName",L"Lucida Console"),"a chosen console face is kept");
    BOOL restored=restore(settings,L"MonospaceInitialized",0)&&restore(substitutes,L"Consolas",1)&&restore(console,L"FaceName",2);
    check(restored,"the probe restores the values and the marker");
    RegCloseKey(settings);RegCloseKey(substitutes);RegCloseKey(console);
    char line[160];wsprintfA(line,"PS5_PROTON_MONOSPACE %s checks=%u failures=%u\n",failures?"FAIL":"PASS",checks,failures);
    say(line);ExitProcess(failures?1:0);
}
