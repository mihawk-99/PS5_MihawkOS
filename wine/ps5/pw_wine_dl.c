/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_wine_dl.h"
#include "pw_wine_prx.h"
#include <pthread.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct Module {
    char path[PW_WINE_DL_MAX_PATH];
    int32_t handle;uint32_t references,segment_count;
    int owned;                 /* loaded here, so unloaded here */
    PwPrxSegment segments[PW_PRX_MAX_SEGMENTS];
    const PwPrxDescriptor *descriptor;
} Module;

static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static Module modules[PW_WINE_DL_MAX_MODULES];
static uint32_t order[PW_WINE_DL_MAX_MODULES],loaded;
static PwWineDlOps ops;
static char module_dir[PW_WINE_DL_MAX_PATH];
/* The paths the last failed open tried, for the ops' report callback. */
static char report[PW_WINE_DL_REPORT];
static size_t report_used;
/* The per-thread error lives in a pthread key: a PRX has no static TLS
 * block, and _Thread_local would need the C library's emulated TLS. */
static pthread_once_t error_once=PTHREAD_ONCE_INIT;
static pthread_key_t error_key;
static int error_key_ready;

static void create_error_key(void){error_key_ready=!pthread_key_create(&error_key,NULL);}
static void set_error(const char *error)
{
    pthread_once(&error_once,create_error_key);
    if(error_key_ready)pthread_setspecific(error_key,error);
}

void pw_wine_dl_configure(const PwWineDlOps *new_ops,const char *dir)
{
    pthread_mutex_lock(&lock);
    if(new_ops)ops=*new_ops;
    module_dir[0]=0;
    if(dir && strlen(dir)<sizeof(module_dir))strcpy(module_dir,dir);
    pthread_mutex_unlock(&lock);
}
const char *pw_wine_dl_error(void)
{
    pthread_once(&error_once,create_error_key);
    if(!error_key_ready)return NULL;
    const char *error=pthread_getspecific(error_key);
    pthread_setspecific(error_key,NULL);
    return error;
}
/* "dir/name.so" -> "dir/name.prx"; with a module dir, "<dir>/name.prx". */
static int prx_path(const char *path,const char *directory,char *out)
{
    size_t length=strlen(path);
    const char *base=strrchr(path,'/');base=base?base+1:path;
    size_t stem=length>=3 && !strcmp(path+length-3,".so")?length-3:length;
    if(directory) {
        size_t dir_length=strlen(directory),name=stem-(size_t)(base-path);
        if(dir_length+1+name+4>=PW_WINE_DL_MAX_PATH)return 0;
        memcpy(out,directory,dir_length);out[dir_length]='/';
        memcpy(out+dir_length+1,base,name);memcpy(out+dir_length+1+name,".prx",5);
        return 1;
    }
    if(stem+4>=PW_WINE_DL_MAX_PATH)return 0;
    memcpy(out,path,stem);memcpy(out+stem,".prx",5);return 1;
}
/* Appends text to the report, truncating at its end. */
static void append(const char *text)
{
    size_t length=strlen(text);
    if(report_used+length>=sizeof(report))length=sizeof(report)-1-report_used;
    memcpy(report+report_used,text,length);report_used+=length;report[report_used]=0;
}
/* The directory of a module's path, for a bare name's lookup beside it. */
static int directory_of(const char *path,char *out)
{
    const char *slash=strrchr(path,'/');
    if(!slash)return 0;
    memcpy(out,path,(size_t)(slash-path));out[slash-path]=0;
    return 1;
}
static Module *find(const char *path)
{
    for(uint32_t i=0;i<loaded;i++)if(!strcmp(modules[order[i]].path,path))return &modules[order[i]];
    return NULL;
}
/* A helper's own log gets one line before and one after each module load (src/pw_start_failure.h): a load the kernel
 * stops (errno 92, the pager error at a module's text) never returns, and the last "loading" line names the module.
 * Through report, one write each: ntdll's fprintf reaches the log in pieces (848 spliced lines in
 * gate-2026-10-19-splice-01). Only a helper, whose report is the loader's plain log write, so it may run under lock. */
static void trace_load(const char *path,int loading,int32_t handle)
{
    char line[PW_WINE_DL_REPORT];
    if(!ops.report || !getenv("WINE_PS5_CHILD"))return;
    if(loading)snprintf(line,sizeof(line),"pw_wine_dl: loading %s",path);
    else snprintf(line,sizeof(line),"pw_wine_dl: %s %s handle=%#x",handle>=0?"loaded":"refused",path,(unsigned)handle);
    ops.report(line);
}
static int32_t load_one(const char *path)
{
    int result=0;
    append(" ");append(path);
    /* Every builtin DLL without a Unix side asks for one (18 per DirectX-setup process): asking the loader for a file
     * that is not there cost 7.5 ms a miss, two tries each (installer-dxsetup-2026-10-29-163636); a stat costs none. */
    if(access(path,F_OK))return -1;
    trace_load(path,1,0);
    const int32_t handle=ops.load_start?ops.load_start(path,0,NULL,0,NULL,&result):-1;
    trace_load(path,0,handle);
    return handle;
}
/* A Windows program in a Sony-managed helper process (WINE_PS5_CHILD) loads a module's helper build when the
 * runtime has one beside it: "name-helper.prx" for "name.prx". win32u's has no OpenGL driver, whose system
 * libraries a helper cannot use, and libvulkan's is the GPU broker's client in place of RADV. Each is linked under
 * the plain name, so the modules that need it find it by that name. The one program the title runs itself beside the
 * broker (WINE_PS5_TITLE_CLIENT, docs/TITLE_CLIENT.md) takes the same builds: its graphics are a helper's. */
static int32_t try_load(const char *path)
{
    size_t length=strlen(path);
    const char *title_client=getenv("WINE_PS5_TITLE_CLIENT");
    if((getenv("WINE_PS5_CHILD") || (title_client && !strcmp(title_client,"1"))) && length>4 && length+7<PW_WINE_DL_MAX_PATH && !strcmp(path+length-4,".prx")) {
        char variant[PW_WINE_DL_MAX_PATH];
        memcpy(variant,path,length-4);memcpy(variant+length-4,"-helper.prx",12);
        if(!access(variant,R_OK))return load_one(variant);
    }
    return load_one(path);
}
/* Describes a loaded module and enters it in the registry; on failure the
 * module is unloaded only when this loader loaded it. */
static Module *register_module(const char *path,int32_t handle,int owned)
{
    uint8_t info[PW_PRX_MODULE_INFO_BYTES];PwPrxSegment segments[PW_PRX_MAX_SEGMENTS];
    uint32_t count=0;const PwPrxDescriptor *descriptor=NULL;int result=0;
    memset(info,0,sizeof(info));
    {uint64_t size=PW_PRX_MODULE_INFO_BYTES;memcpy(info,&size,sizeof(size));}
    if(!ops.module_info || ops.module_info(handle,info)<0 ||
       pw_prx_parse_module_info(info,NULL,segments,&count)!=PW_PRX_OK ||
       pw_prx_find_descriptor(segments,count,&descriptor)!=PW_PRX_OK) {
        if(owned && ops.stop_unload)(void)ops.stop_unload(handle,0,NULL,0,NULL,&result);
        append(": loaded, but it has no export descriptor");
        set_error("module has no export descriptor");return NULL;
    }
    uint32_t slot=0;while(modules[slot].references)slot++;
    Module *module=&modules[slot];
    strcpy(module->path,path);module->handle=handle;module->references=1;module->owned=owned;
    module->segment_count=count;memcpy(module->segments,segments,sizeof(segments));
    module->descriptor=descriptor;order[loaded++]=slot;
    return module;
}
static void *open_locked(const char *path)
{
    Module *module=find(path);
    if(module){module->references++;return module;}
    if(loaded==PW_WINE_DL_MAX_MODULES){set_error("too many modules");return NULL;}
    char candidate[PW_WINE_DL_MAX_PATH],directory[PW_WINE_DL_MAX_PATH];int32_t handle=-1;
    int bare=!strchr(path,'/');
    /* A bare name is never tried as given (the console refuses relative
     * paths): the module dir first, then beside every module already loaded
     * or adopted, as a library's RPATH finds a sibling. */
    if(!bare && prx_path(path,NULL,candidate))handle=try_load(candidate);
    if(handle<0 && module_dir[0] && prx_path(path,module_dir,candidate))handle=try_load(candidate);
    for(uint32_t i=0;bare && handle<0 && i<loaded;i++)
        if(directory_of(modules[order[i]].path,directory) && strcmp(directory,module_dir) &&
           prx_path(path,directory,candidate))
            handle=try_load(candidate);
    if(handle<0){append(": not found");set_error("module not found");return NULL;}
    if(!(module=register_module(path,handle,1)))return NULL;
    int (*start)(size_t,const void *)=(int (*)(size_t,const void *))
        (uintptr_t)pw_prx_lookup(module->descriptor,"module_start");
    {
        void (*share)(const void *)=(void (*)(const void *))(uintptr_t)pw_prx_lookup(module->descriptor,"pw_module_share");
        if(share && ops.share)share(ops.share);
        /* It may only keep it: this lock is held, and pw_wine_dl_sym takes it. */
        void (*lookup)(void *(*)(void *,const char *))=(void (*)(void *(*)(void *,const char *)))(uintptr_t)
            pw_prx_lookup(module->descriptor,"pw_module_lookup");
        if(lookup)lookup(pw_wine_dl_sym);
    }
    if(start)(void)start(0,NULL);
    return module;
}
/* Opens that found a module and those that found none (every builtin DLL without a Unix side asks once), as calls
 * and nanoseconds including the wait for this lock; lock-free reads for the helper's life summary. */
static uint64_t open_found_calls,open_found_ns,open_missed_calls,open_missed_ns;
unsigned pw_wine_dl_timing(uint64_t *out,unsigned count)
{
    const uint64_t values[4]={__atomic_load_n(&open_found_calls,__ATOMIC_RELAXED),__atomic_load_n(&open_found_ns,__ATOMIC_RELAXED),
                              __atomic_load_n(&open_missed_calls,__ATOMIC_RELAXED),__atomic_load_n(&open_missed_ns,__ATOMIC_RELAXED)};
    for(unsigned i=0;i<count && i<4;i++)out[i]=values[i];
    return 4;
}
void *pw_wine_dl_open(const char *path)
{
    if(!path || !*path || strlen(path)>=PW_WINE_DL_MAX_PATH){set_error("invalid path");return NULL;}
    char line[PW_WINE_DL_REPORT];
    struct timespec begin,end;
    clock_gettime(CLOCK_MONOTONIC,&begin);
    pthread_mutex_lock(&lock);
    report_used=0;report[0]=0;
    append("pw_wine_dl: cannot open ");append(path);append(", tried");
    void *handle=open_locked(path);
    if(!handle)memcpy(line,report,report_used+1);
    pthread_mutex_unlock(&lock);
    clock_gettime(CLOCK_MONOTONIC,&end);
    {
        const uint64_t ns=(uint64_t)((end.tv_sec-begin.tv_sec)*1000000000ll+(end.tv_nsec-begin.tv_nsec));
        __atomic_add_fetch(handle?&open_found_calls:&open_missed_calls,1,__ATOMIC_RELAXED);
        __atomic_add_fetch(handle?&open_found_ns:&open_missed_ns,ns,__ATOMIC_RELAXED);
    }
    /* One line per failure, outside the lock: the report may log. */
    if(!handle && ops.report)ops.report(line);
    return handle;
}
void *pw_wine_dl_sym(void *handle,const char *name)
{
    if(!name){set_error("invalid symbol");return NULL;}
    const void *address=NULL;
    pthread_mutex_lock(&lock);
    if(handle==PW_WINE_DL_DEFAULT) {
        for(uint32_t i=0;i<loaded && !address;i++)
            address=pw_prx_lookup(modules[order[i]].descriptor,name);
    } else {
        Module *module=handle;
        if(module>=modules && module<modules+PW_WINE_DL_MAX_MODULES && module->references)
            address=pw_prx_lookup(module->descriptor,name);
    }
    pthread_mutex_unlock(&lock);
    if(!address)set_error("symbol not found");
    return (void *)(uintptr_t)address;
}
int pw_wine_dl_close(void *handle)
{
    Module *module=handle;int status=0;
    pthread_mutex_lock(&lock);
    if(!module || module<modules || module>=modules+PW_WINE_DL_MAX_MODULES || !module->references) {
        set_error("invalid handle");status=-1;
    } else if(!--module->references) {
        int result=0;
        if(module->owned && ops.stop_unload &&
           ops.stop_unload(module->handle,0,NULL,0,NULL,&result)<0) {
            set_error("unload failed");status=-1;
        }
        uint32_t slot=(uint32_t)(module-modules),i=0;
        while(order[i]!=slot)i++;
        memmove(&order[i],&order[i+1],(loaded-i-1)*sizeof(order[0]));loaded--;
        memset(module,0,sizeof(*module));
    }
    pthread_mutex_unlock(&lock);
    return status;
}

void *pw_wine_dl_adopt(const char *path,int32_t module_handle)
{
    if(!path || !*path || strlen(path)>=PW_WINE_DL_MAX_PATH){set_error("invalid path");return NULL;}
    pthread_mutex_lock(&lock);
    Module *module=find(path);
    if(module)module->references++;
    else if(loaded==PW_WINE_DL_MAX_MODULES)set_error("too many modules");
    else module=register_module(path,module_handle,0);
    pthread_mutex_unlock(&lock);
    return module;
}
int pw_wine_dl_addr(const void *address,PwWineDlInfo *info)
{
    int found=0;
    if(!info)return 0;
    pthread_mutex_lock(&lock);
    for(uint32_t i=0;i<loaded && !found;i++) {
        const Module *module=&modules[order[i]];
        for(uint32_t s=0;s<module->segment_count && !found;s++) {
            uintptr_t base=(uintptr_t)module->segments[s].address;
            if((uintptr_t)address>=base && (uintptr_t)address-base<module->segments[s].size) {
                *info=(PwWineDlInfo){module->path,module->segments[0].address};found=1;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    if(!found)set_error("address not in any module");
    return found;
}
