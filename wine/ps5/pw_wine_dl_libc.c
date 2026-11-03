/* SPDX-License-Identifier: GPL-3.0-or-later */
/* dlfcn for Wine's Unix side on PS5. ntdll.so links this file, so its own
 * dlopen of a unix library (and win32u's, through the ntdll.so import) loads
 * a PRX through pw_wine_dl instead of the firmware's dynamic linker, which
 * cannot resolve an application module's exports. dlopen(NULL) and the
 * RTLD_* pseudo-handles search every loaded module. dladdr names the loaded
 * or adopted module containing an address, which is how ntdll finds its own
 * directory. */
#include "pw_wine_dl.h"
#include "pw_wine_fdv.h"
#include "pw_wine_process.h"
#include "pw_wine_prx.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

extern int32_t sceKernelLoadStartModule(const char *,size_t,const void *,uint32_t,const void *,int *);
extern int sceKernelGetModuleInfo(int32_t,void *);
extern int sceKernelStopUnloadModule(int32_t,size_t,const void *,uint32_t,const void *,int *);

/* Only a payload installs this hook. Sony helpers keep their native loader. */
static int (*payload_module)(int32_t);
int __wine_ps5_set_payload_module(int (*prepare)(int32_t))
{
    if(!prepare)return -1;
    payload_module=prepare;return 0;
}
/* A payload wires each module's segments it cannot write (payload_module, pw_prx_wired): they are unwired before the kernel
 * tears the module down, never under it (tearing down wired memory is what hung the console, "unwire timeout"). */
static int stop_unload(int32_t handle,size_t argc,const void *argv,uint32_t flags,const void *option,int *result)
{
    if(payload_module) {
        unsigned char info[PW_PRX_MODULE_INFO_BYTES]={0};
        uint64_t size=sizeof(info);
        memcpy(info,&size,sizeof(size));
        if(sceKernelGetModuleInfo(handle,info)>=0)(void)pw_prx_unwire(info,munlock);
    }
    return sceKernelStopUnloadModule(handle,argc,argv,flags,option,result);
}
static int32_t load_module(const char *path,size_t argc,const void *argv,uint32_t flags,
                           const void *option,int *result)
{
    int32_t handle=sceKernelLoadStartModule(path,argc,argv,flags,option,result);
    if(handle>0 && payload_module && payload_module(handle)) {
        stop_unload(handle,0,NULL,0,NULL,result);return -1;
    }
    return handle;
}

static pthread_once_t once=PTHREAD_ONCE_INIT;
static int self;

/* WINE_PRX_DIR names the fallback directory for modules that are not staged
 * beside the path Wine asks for; the title's sce_module is the default. */
/* A failed dlopen names what it tried on stderr, which reaches ps5log
 * through ntdll's output sink (patch 0560). */
extern ssize_t wine_write_stderr(const void *,size_t);
static void report(const char *line)
{
    /* One write with its newline: a second one let other threads' records land before it (gate-release-2026-10-19-01). */
    char text[PW_WINE_DL_REPORT+1];
    int length=snprintf(text,sizeof(text),"%s\n",line);
    if(length>0)wine_write_stderr(text,(size_t)length<sizeof(text)?(size_t)length:sizeof(text)-1);
}
/* ntdll's heap area (wine/ps5/pw_wine_dmem_ps5.c): the in-process server's own heap takes its blocks from it. */
extern int __wine_ps5_heap_backing(void *(**map)(size_t),void (**unmap)(void *,size_t));
static void configure(void)
{
    const PwWineDlOps ops={load_module,sceKernelGetModuleInfo,
                           stop_unload,report,pw_wine_fdv_table(),__wine_ps5_heap_backing};
    const char *dir=getenv("WINE_PRX_DIR");
    pw_wine_dl_configure(&ops,dir && *dir?dir:"/app0/sce_module");
}
/* Where an assertion fired (pw_wine_compat_libc.c's __assert): the return
 * addresses found on the stack above the call, as module+offset, the offset
 * from the module's first segment, which the unstripped .shared.elf of the
 * build resolves with llvm-addr2line. A scan, not an unwind: a few words that
 * only look like addresses may appear.
 * A helper then ends without returning: the asserting thread may hold Wine's
 * virtual mutex, and abort() raised an exception that came back still holding
 * it, which wedged the process (2026-10-14; record removed from the tree; in git history). The title
 * hears the end first (PW_CHILD_EXIT, STATUS_ASSERTION_FAILURE) and closes the
 * helper, so a scan stuck on the loader's lock cannot keep it alive; the frame
 * lines may then be cut short. In the title the hook returns and __assert
 * aborts, as before. */
extern void (*pw_assert_trace)(const char *,const char *,const char *,int);
static void assert_trace(const char *expression,const char *function,const char *file,int line)
{
    static int scanned;
    char message[512];
    snprintf(message,sizeof(message),"Assertion failed: %s (%s: %s: %d)",expression,function?function:"?",file,line);
    report(message);
    pw_ps5_child_exit_notice(0xc0000420u);
    if(!__atomic_exchange_n(&scanned,1,__ATOMIC_SEQ_CST)) {   /* once: the scan itself may assert */
        const uintptr_t *word=__builtin_frame_address(0);
        unsigned printed=0;
        for(unsigned i=0;i<384 && printed<32;i++) {
            PwWineDlInfo found;
            const char *name;
            if(!pw_wine_dl_addr((const void *)word[i],&found))continue;
            name=strrchr(found.path,'/');
            snprintf(message,sizeof(message),"  at %s+0x%lx",name?name+1:found.path,
                     (unsigned long)(word[i]-(uintptr_t)found.base));
            report(message);
            printed++;
        }
    }
    if(getenv("WINE_PS5_CHILD"))pw_ps5_child_exit(0xc0000420u);
}
/* The title's first call may be pw_wine_dl_adopt, which bypasses the
 * wrappers below, so configure as the module starts as well. */
__attribute__((constructor)) static void configure_on_start(void){pw_assert_trace=assert_trace;pthread_once(&once,configure);}
static void *resolve_handle(void *handle)
{
    return handle==&self || handle==RTLD_DEFAULT || handle==RTLD_NEXT || handle==RTLD_SELF?
        PW_WINE_DL_DEFAULT:handle;
}

void *dlopen(const char *path,int mode)
{
    (void)mode;pthread_once(&once,configure);
    return path?pw_wine_dl_open(path):&self;
}
void *dlsym(void *__restrict handle,const char *__restrict name)
{
    pthread_once(&once,configure);
    return pw_wine_dl_sym(resolve_handle(handle),name);
}
int dlclose(void *handle)
{
    return handle==&self?0:pw_wine_dl_close(handle);
}
char *dlerror(void)
{
    return (char *)pw_wine_dl_error();
}
int dladdr(const void *address,Dl_info *info)
{
    PwWineDlInfo found;
    if(!info || !pw_wine_dl_addr(address,&found))return 0;
    info->dli_fname=found.path;info->dli_fbase=(void *)(uintptr_t)found.base;
    info->dli_sname=NULL;info->dli_saddr=NULL;
    return 1;
}
