/* SPDX-License-Identifier: GPL-3.0-or-later
 * Standard Windows GDI discovery, face identity, weight, kerning and raster ink
 * for the shared Proton UI family. No private registration or per-app override:
 * the normal Windows Fonts directory must be enough. Run as PE32 and PE64. */
#include <windows.h>
static unsigned checks, failures;
static void check(int okay, const char *name)
{
    char line[200]; ++checks; if (!okay) ++failures;
    wsprintfA(line,"proton-ui: %s %s\n",name,okay?"ok":"FAIL");
    OutputDebugStringA(line);
    DWORD written; WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),line,lstrlenA(line),&written,NULL);
}
void mainCRTStartup(void)
{
    static const WCHAR *faces[] = {L"Proton UI",L"Proton UI",L"Proton UI Light",
                                   L"Proton UI Semilight",L"Proton UI Semibold"};
    static const int weights[] = {400,700,300,350,600};
    static const WCHAR text[] = L"Settings Library Play AV To Yo 0123456789 Caf\x00e9";
    HDC dc = CreateCompatibleDC(NULL); BITMAPINFO info = {0}; void *bits = NULL;
    info.bmiHeader.biSize=sizeof(info.bmiHeader);info.bmiHeader.biWidth=720;
    info.bmiHeader.biHeight=-80;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;
    HBITMAP bmp = CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,NULL,0);
    check(dc && bmp && bits,"bitmap resources");
    if (!dc || !bmp || !bits) ExitProcess(1);
    HGDIOBJ old_bitmap=SelectObject(dc,bmp);
    for (unsigned i=0;i<ARRAYSIZE(faces);++i) {
        HFONT font=CreateFontW(-24,0,0,0,weights[i],0,0,0,DEFAULT_CHARSET,0,0,
                               ANTIALIASED_QUALITY,0,faces[i]);
        check(font != NULL,"create font");if(!font)continue;
        HGDIOBJ old=SelectObject(dc,font);WCHAR actual[80]={0};TEXTMETRICW tm={0};
        check(GetTextFaceW(dc,ARRAYSIZE(actual),actual)>0 && !lstrcmpW(actual,faces[i]),"resolved family");
        check(GetTextMetricsW(dc,&tm) && tm.tmWeight==weights[i],"resolved weight");
        WORD glyphs[ARRAYSIZE(text)]={0};int missing=0;
        DWORD count=GetGlyphIndicesW(dc,text,ARRAYSIZE(text)-1,glyphs,GGI_MARK_NONEXISTING_GLYPHS);
        for(unsigned n=0;n<ARRAYSIZE(text)-1;++n)if(glyphs[n]==0xffff)++missing;
        check(count!=GDI_ERROR && !missing,"Latin glyphs");
        check(GetKerningPairsW(dc,0,NULL)>0,"GDI kerning pairs");
        PatBlt(dc,0,0,720,80,WHITENESS);SetTextColor(dc,RGB(20,20,20));SetBkMode(dc,TRANSPARENT);
        check(TextOutW(dc,4,12,text,ARRAYSIZE(text)-1),"draw text");GdiFlush();
        unsigned ink=0,coloured=0,levels=0;static BYTE seen[256];ZeroMemory(seen,sizeof(seen));
        for(unsigned n=0;n<720*80;++n){DWORD p=((DWORD*)bits)[n]&0xffffff;if(p==0xffffff)continue;++ink;
            if((p&0xff)!=((p>>8)&0xff)||(p&0xff)!=((p>>16)&0xff))++coloured;}
        check(ink>100,"raster ink");
        /* grey smoothing (patch 2498): one glyph alone (no overlap blends twice) shows more than the 17 coverage levels
           it had, and no pixel has colour (no ClearType fringe) */
        PatBlt(dc,0,0,720,80,WHITENESS);TextOutW(dc,4,12,L"O",1);GdiFlush();
        for(unsigned n=0;n<720*80;++n){DWORD p=((DWORD*)bits)[n]&0xffffff;if(p!=0xffffff&&!seen[p&0xff]++)++levels;}
        check(levels>17,"one glyph has more than 17 grey levels");
        check(!coloured,"grey only, no colour fringe");
        SelectObject(dc,old);DeleteObject(font);
    }
    SelectObject(dc,old_bitmap);DeleteObject(bmp);DeleteDC(dc);
    char line[200];wsprintfA(line,"PS5_PROTON_UI_PROBE %s checks=%u failures=%u\n",failures?"FAIL":"PASS",checks,failures);
    OutputDebugStringA(line);DWORD written;
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),line,lstrlenA(line),&written,NULL);
    ExitProcess(failures?1:0);
}
