// SPDX-License-Identifier: GPL-3.0-or-later
// No graphics imports or title exit. The optional GPU probe variant initializes
// libc solely for the existing cooperative filesystem-elevation client.
// Sony owns this process; the requesting title closes its returned service ID.
#include <stdint.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>
#include "proton_gpu_broker_probe.h"
#include "proton_flex_probe.h"
#ifdef PROTON_NATIVE_GPU_ELEVATION
#include <ps5platform/elevation.h>
extern void _init_env(void *);
#endif
#ifdef PROTON_NATIVE_VULKAN_HELPER
#include "../wine/ps5/pw_wine_prx.h"
#include "../src/pw_gpu_broker.h"
extern int sceKernelLoadStartModule(const char *,size_t,const void *,unsigned,const void *,int *);
extern int sceKernelGetModuleInfo(int,void *);
extern int sceKernelReserveVirtualRange(void **,size_t,int,size_t);
extern int sceKernelMunmap(void *,size_t);
#include <stdio.h>
#include <stdlib.h>
static int bootstrap_failure(unsigned stage,int error)
{
    PwGpuMessage message={.magic=PW_GPU_MAGIC,.operation=PW_GPU_INIT,.sequence=1,
        .argument={(uint64_t)getpid(),stage,(uint32_t)error}};
    (void)send(3,&message,sizeof(message),MSG_NOSIGNAL);return -1;
}
static int vulkan_helper(unsigned client)
{
    static const struct ps5_elevation_config grant = {
        "/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 60, 50000
    };
    int elevated=ps5_elevation_request_with(&grant,PS5_ELEVATION_FILESYSTEM);
    if(elevated)return bootstrap_failure(1,elevated);
    // As the Wine child loader does: the owner's host ranges, reserved before the client module maps anything.
    if(pw_gpu_reserve_host_ranges(sceKernelReserveVirtualRange,sceKernelMunmap))setenv("WINE_PS5_GPU_RESERVED","1",1);
    int result=0;
    int module=sceKernelLoadStartModule("/data/homebrew/PPSA99170/gpu-client.prx",0,0,0,0,&result);
    if(module<0)return bootstrap_failure(2,module);
    union { uint64_t alignment;char bytes[PW_PRX_MODULE_INFO_BYTES]; } info={0};
    *(uint64_t *)info.bytes=sizeof(info.bytes);
    PwPrxSegment segments[PW_PRX_MAX_SEGMENTS];uint32_t count=0;
    const PwPrxDescriptor *descriptor=0;
    if(sceKernelGetModuleInfo(module,info.bytes) ||
       pw_prx_parse_module_info(info.bytes,0,segments,&count) ||
       pw_prx_find_descriptor(segments,count,&descriptor))return bootstrap_failure(3,-1);
    int (*start)(size_t,const void *)=(void *)pw_prx_lookup(descriptor,"module_start");
    int (*probe)(int,unsigned)=(void *)pw_prx_lookup(descriptor,"proton_gpu_vulkan_probe");
    if(!start || !probe || start(0,0))return bootstrap_failure(4,-1);
    return probe(3,client);
}
#endif

extern int sceKernelUsleep(unsigned);
extern int64_t sceKernelGetDirectMemorySize(void);
extern int sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
extern int sceKernelReserveVirtualRange(void **,size_t,int,size_t);
extern int sceKernelMapDirectMemory(void **,size_t,int,int,int64_t,size_t);
extern int sceKernelMunmap(void *,size_t);
extern int sceKernelReleaseDirectMemory(int64_t,size_t);
static volatile int started;
struct Handshake { char identity[33]; int32_t pid, app, socket_type, allocation_status; int64_t physical; uint64_t allocation_bytes; };

static int ready(int fd, short events)
{
    struct pollfd descriptor = {fd, events, 0};
    return poll(&descriptor, 1, 5000) == 1 && (descriptor.revents & events);
}

static void log_stage(const char *message)
{
    unsigned size = 0;
    while (message[size]) ++size;
    (void)write(2, message, size);
}

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { ++a; ++b; }
    return *a == *b;
}

static void gpu_helper(void)
{
    void *alias=0;size_t bytes=0;
    struct ProtonGpuProbeReply reply={PROTON_GPU_PROBE_MAGIC,getpid(),0,0,0,0};
#ifdef PROTON_NATIVE_GPU_ELEVATION
    static const struct ps5_elevation_config grant = {
        "/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 60, 50000
    };
    reply.elevation=ps5_elevation_request_with(&grant,PS5_ELEVATION_FILESYSTEM);
    reply.status=reply.elevation;
#else
    reply.elevation=-1;
#endif
    reply.value=geteuid();
    if(!ready(3,POLLOUT) || send(3,&reply,sizeof(reply),MSG_NOSIGNAL)!=sizeof(reply))return;
    if(reply.status)return;
    for(;;) {
        struct ProtonGpuProbeMessage message={0};
        struct iovec vector={&message,sizeof(message)};
        union { struct cmsghdr align; char bytes[CMSG_SPACE(4*sizeof(int))]; } control={0};
        struct msghdr packet={0};packet.msg_iov=&vector;packet.msg_iovlen=1;
        packet.msg_control=control.bytes;packet.msg_controllen=sizeof(control.bytes);
        if(!ready(3,POLLIN))break;
        const ssize_t received=recvmsg(3,&packet,0);
        int shared_fd=-1,fd_count=0;
        for(struct cmsghdr *h=CMSG_FIRSTHDR(&packet);h;h=CMSG_NXTHDR(&packet,h)) {
            if(h->cmsg_level!=SOL_SOCKET || h->cmsg_type!=SCM_RIGHTS || h->cmsg_len<CMSG_LEN(0))continue;
            size_t count=(h->cmsg_len-CMSG_LEN(0))/sizeof(int);
            int *fds=(int *)CMSG_DATA(h);
            for(size_t n=0;n<count;++n) { if(!fd_count)shared_fd=fds[n];else close(fds[n]);++fd_count; }
        }
        if(received!=sizeof(message) || message.magic!=PROTON_GPU_PROBE_MAGIC || fd_count>1 ||
           (packet.msg_flags&(MSG_TRUNC|MSG_CTRUNC))) { if(shared_fd>=0)close(shared_fd);break; }
        reply.status=-1;reply.value=0;
        if(message.operation==PROTON_GPU_MAP && !alias && message.bytes>=16384 &&
           message.bytes<=33554432 && !(message.bytes&16383) &&
           ((message.physical>=0 && shared_fd<0) || (message.physical==-1 && shared_fd>=0)) &&
           message.address>=0x200000000ull && message.address<(1ull<<47)-message.bytes && !(message.address&16383)) {
            void *address=(void *)(uintptr_t)message.address;
            reply.map_stage=1;reply.value=geteuid();
            reply.status=sceKernelReserveVirtualRange(&address,message.bytes,0,16384);
            if(!reply.status && address==(void *)(uintptr_t)message.address) {
                // The helper receives only CPU access. The owner has the GPU mapping.
                reply.map_stage=2;
                if(shared_fd>=0) {
                    void *mapped=mmap(address,message.bytes,PROT_READ|PROT_WRITE,MAP_SHARED|MAP_FIXED,shared_fd,0);
                    reply.status=mapped==MAP_FAILED ? -1 : 0;
                } else reply.status=sceKernelMapDirectMemory(&address,message.bytes,3,0x10,message.physical,16384);
                if(!reply.status) { alias=address;bytes=message.bytes; }
                else sceKernelMunmap(address,message.bytes);
            } else if(!reply.status) {
                sceKernelMunmap(address,message.bytes);reply.status=-1;
            }
        } else if(message.operation==PROTON_GPU_SUBMIT && alias && !(message.reserved&~3u)) {
            proton_gpu_probe_packet(alias,(uint64_t)(uintptr_t)alias+((message.reserved&2) ? bytes-1024 : 1024),message.value);
            if(message.reserved&1)((uint32_t *)alias)[0]^=1; // Deliberately rejected; never submitted.
            __asm__ volatile("mfence");
            reply.status=0;
        } else if(message.operation==PROTON_GPU_UNMAP && alias) {
            reply.status=sceKernelMunmap(alias,bytes);if(!reply.status)alias=0;
        }
        if(shared_fd>=0)close(shared_fd);
        if(!ready(3,POLLOUT) || send(3,&reply,sizeof(reply),MSG_NOSIGNAL)!=sizeof(reply))break;
        if(message.operation==PROTON_GPU_SUBMIT && !reply.status) {
            struct ProtonGpuProbeReply completed={0};
            if(!ready(3,POLLIN) || recv(3,&completed,sizeof(completed),0)!=sizeof(completed) ||
               completed.magic!=PROTON_GPU_PROBE_MAGIC || completed.pid!=reply.pid)break;
            volatile uint32_t *marker=(volatile uint32_t *)((char *)alias+((message.reserved&2) ? bytes-1024 : 1024));
            __asm__ volatile("clflushopt %0; mfence" : "+m"(*marker));
            reply.value=*marker;
            reply.status=(message.reserved&1) ? !(completed.status==22 && reply.value==0)
                                         : !(completed.status==0 && reply.value==message.value);
            if(!ready(3,POLLOUT) || send(3,&reply,sizeof(reply),MSG_NOSIGNAL)!=sizeof(reply))break;
        }
        if(message.operation==PROTON_GPU_UNMAP)break;
    }
    if(alias)sceKernelMunmap(alias,bytes);
}

/* The memory-ceiling probe (native/proton_gpu_memory_probe.cpp): ranges at the owner's addresses, either shared
 * chunks mapped and touched here or reservations with no access, which the owner's GPU uses. */
extern int sceKernelAvailableFlexibleMemorySize(size_t *);
static unsigned flexible_mib(void)
{
    size_t available = 0;
    return sceKernelAvailableFlexibleMemorySize(&available) ? 0 : (unsigned)(available >> 20);
}
struct MemoryRange { uint64_t address, bytes; int mapped; int64_t physical; };
static int receive_message(struct ProtonGpuProbeMessage *message, int *fd)
{
    struct iovec vector = {message, sizeof(*message)};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(4 * sizeof(int))]; } control = {0};
    struct msghdr packet = {0};
    packet.msg_iov = &vector; packet.msg_iovlen = 1;
    packet.msg_control = control.bytes; packet.msg_controllen = sizeof(control.bytes);
    *fd = -1;
    if (!ready(3, POLLIN)) return 0;
    const ssize_t received = recvmsg(3, &packet, 0);
    int fd_count = 0;
    for (struct cmsghdr *h = CMSG_FIRSTHDR(&packet); h; h = CMSG_NXTHDR(&packet, h)) {
        if (h->cmsg_level != SOL_SOCKET || h->cmsg_type != SCM_RIGHTS || h->cmsg_len < CMSG_LEN(0)) continue;
        const size_t count = (h->cmsg_len - CMSG_LEN(0)) / sizeof(int);
        int *fds = (int *)CMSG_DATA(h);
        for (size_t n = 0; n < count; ++n) { if (!fd_count) *fd = fds[n]; else close(fds[n]); ++fd_count; }
    }
    if (received != sizeof(*message) || message->magic != PROTON_GPU_PROBE_MAGIC || fd_count > 1 ||
        (packet.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
        if (*fd >= 0) close(*fd);
        *fd = -1;
        return 0;
    }
    return 1;
}
static int inside_mapped(const struct MemoryRange *ranges, unsigned count, uint64_t address, uint64_t bytes)
{
    for (unsigned i = 0; i < count; ++i)
        if (ranges[i].mapped && address >= ranges[i].address && bytes <= ranges[i].bytes &&
            address - ranges[i].address <= ranges[i].bytes - bytes) return 1;
    return 0;
}
static void memory_helper(void)
{
    static struct MemoryRange ranges[160];
    unsigned count = 0;
    struct ProtonGpuProbeReply reply = {PROTON_GPU_PROBE_MAGIC, getpid(), 0, 0, 0, 0};
#ifdef PROTON_NATIVE_GPU_ELEVATION
    static const struct ps5_elevation_config grant = {
        "/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 60, 50000
    };
    reply.elevation = ps5_elevation_request_with(&grant, PS5_ELEVATION_FILESYSTEM);
    reply.status = reply.elevation;
#else
    reply.elevation = -1;
#endif
    reply.value = flexible_mib();
    if (!ready(3, POLLOUT) || send(3, &reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply) || reply.status) return;
    for (;;) {
        struct ProtonGpuProbeMessage message = {0};
        int fd = -1;
        if (!receive_message(&message, &fd)) break;
        const uint64_t address = message.address, bytes = message.bytes;
        const int range_valid = bytes >= 16384 && !(bytes & 16383) && !(address & 16383) && address >= 0x200000000ull &&
                                address < (1ull << 47) && bytes <= (1ull << 47) - address;
        reply.status = -1; reply.value = 0; reply.map_stage = 0;
        if (message.operation == PROTON_GPU_MEM_MAP && fd >= 0 && count < 160 && range_valid && bytes <= (64ull << 20)) {
            void *at = (void *)(uintptr_t)address;
            reply.map_stage = 1;
            if (!sceKernelReserveVirtualRange(&at, bytes, 0, 16384) && at == (void *)(uintptr_t)address) {
                reply.map_stage = 2;
                if (mmap(at, bytes, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd, 0) == at) {
                    reply.map_stage = 3;
                    // value bit 0: map only, leaving every page untouched (what a mapping costs before any fault).
                    if (!(message.value & 1))
                        for (uint64_t page = 0; page < bytes; page += 16384)
                            ((volatile uint32_t *)(uintptr_t)(address + page))[1] = (uint32_t)(page >> 14);
                    ranges[count].address = address; ranges[count].bytes = bytes; ranges[count].mapped = 1;
                    ranges[count].physical = -1; ++count;
                    reply.status = 0;
                } else sceKernelMunmap(at, bytes);
            } else if (at && at != (void *)(uintptr_t)address) sceKernelMunmap(at, bytes);
            reply.value = flexible_mib();
        } else if (message.operation == PROTON_GPU_MEM_RESERVE && fd < 0 && count < 160 && range_valid &&
                   bytes <= (4ull << 30)) {
            void *at = (void *)(uintptr_t)address;
            reply.map_stage = 1;
            if (!sceKernelReserveVirtualRange(&at, bytes, 0, 16384) && at == (void *)(uintptr_t)address) {
                ranges[count].address = address; ranges[count].bytes = bytes; ranges[count].mapped = 0;
                ranges[count].physical = -1; ++count;
                reply.status = 0; reply.map_stage = 2;
            } else if (at && at != (void *)(uintptr_t)address) sceKernelMunmap(at, bytes);
            reply.value = flexible_mib();
        } else if ((message.operation == PROTON_GPU_MEM_DMEM || message.operation == PROTON_GPU_MEM_DMAP) && fd < 0 &&
                   count < 160 && range_valid && bytes <= (64ull << 20)) {
            // DMEM: the helper's own direct memory, mapped for its CPU at address, every page marked. DMAP: someone
            // else's direct memory (message.physical), with the protection and flags the owner names in value.
            // elevation carries the kernel's own result; value the physical offset in 64 KiB units (DMEM) or the
            // address mapped in 16 KiB units (DMAP).
            const int own = message.operation == PROTON_GPU_MEM_DMEM;
            const int protection = own ? 3 : (int)(message.value & 0xff), flags = own ? 0x10 : (int)(message.value >> 16);
            int64_t physical = own ? -1 : message.physical;
            reply.elevation = 0;
            if (own) {
                reply.elevation = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), bytes, 2u << 20, 12,
                                                                &physical);
                if (reply.elevation) physical = -1;
            }
            if (physical >= 0) {
                reply.map_stage = 1;
                void *at = (void *)(uintptr_t)address;
                const int reserved = !(flags & 0x10) ||
                                     (!sceKernelReserveVirtualRange(&at, bytes, 0, 16384) && at == (void *)(uintptr_t)address);
                if (!(flags & 0x10)) at = NULL;
                if (reserved) {
                    reply.map_stage = 2;
                    reply.elevation = sceKernelMapDirectMemory(&at, bytes, protection, flags, physical, 16384);
                    if (!reply.elevation && at) {
                        reply.map_stage = 3;
                        if (own)
                            for (uint64_t page = 0; page < bytes; page += 16384)
                                ((volatile uint32_t *)((uintptr_t)at + page))[2] = 0xc0de0000u | (uint32_t)(page >> 14);
                        ranges[count].address = (uint64_t)(uintptr_t)at; ranges[count].bytes = bytes;
                        ranges[count].mapped = (protection & 3) == 3; ranges[count].physical = own ? physical : -1;
                        ++count;
                        reply.status = 0;
                        reply.value = (uint32_t)(own ? (uint64_t)physical >> 16 : (uint64_t)(uintptr_t)at >> 14);
                    } else if (flags & 0x10) sceKernelMunmap((void *)(uintptr_t)address, bytes);
                } else if (at && at != (void *)(uintptr_t)address) sceKernelMunmap(at, bytes);
                if (reply.status && own) { sceKernelReleaseDirectMemory(physical, bytes); }
            }
        } else if (message.operation == PROTON_GPU_MEM_BUILD && fd < 0 && bytes == 32 && !(address & 3) &&
                   inside_mapped(ranges, count, address, 32)) {
            proton_gpu_probe_packet((uint32_t *)(uintptr_t)address, (uint64_t)message.physical, message.value);
            __asm__ volatile("mfence");
            for (uint64_t line = address & ~63ull; line < address + 32; line += 64)
                __asm__ volatile("clflushopt %0" : "+m"(*(volatile char *)(uintptr_t)line));
            __asm__ volatile("mfence");
            reply.status = 0;
        } else if (message.operation == PROTON_GPU_MEM_READ && fd < 0 && bytes == 4 && !(address & 3) &&
                   inside_mapped(ranges, count, address, 4)) {
            volatile uint32_t *word = (volatile uint32_t *)(uintptr_t)address;
            __asm__ volatile("clflushopt %0; mfence" : "+m"(*word));
            reply.value = *word;
            reply.status = 0;
        } else if (message.operation == PROTON_GPU_MEM_RELEASE && fd < 0) {
            reply.status = 0;
            for (unsigned i = 0; i < count;) {
                const int selected = !bytes || (ranges[i].address >= address && ranges[i].bytes <= bytes &&
                                                ranges[i].address - address <= bytes - ranges[i].bytes);
                if (!selected) { ++i; continue; }
                if (sceKernelMunmap((void *)(uintptr_t)ranges[i].address, ranges[i].bytes)) reply.status = -1;
                if (ranges[i].physical >= 0 && sceKernelReleaseDirectMemory(ranges[i].physical, ranges[i].bytes))
                    reply.status = -1;
                ranges[i] = ranges[--count];
            }
            reply.map_stage = (int)count;
            reply.value = flexible_mib();
        }
        if (fd >= 0) close(fd);
        if (!ready(3, POLLOUT) || send(3, &reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply)) break;
    }
    for (unsigned i = 0; i < count; ++i) {
        sceKernelMunmap((void *)(uintptr_t)ranges[i].address, ranges[i].bytes);
        if (ranges[i].physical >= 0) sceKernelReleaseDirectMemory(ranges[i].physical, ranges[i].bytes);
    }
}

/* The open-file budget probe (native/proton_fd_budget_probe.cpp): headroom, files opened and kept, files received and
 * handed back. The path is the title's own param.json, under whichever root this process sees. */
extern int *__error(void);
static const char *budget_path(void)
{
    static const char *const paths[] = {"/app0/sce_sys/param.json", "/mnt/sandbox/PPSA99170_000/app0/sce_sys/param.json"};
    for (unsigned i = 0; i < 2; ++i) {
        const int fd = open(paths[i], O_RDONLY);
        if (fd >= 0) { close(fd); return paths[i]; }
    }
    return 0;
}
static unsigned budget_files(void)
{
    unsigned files = 0;
    for (int fd = 0; fd < 1024; ++fd) {
        struct stat status;
        if (!fstat(fd, &status) && S_ISREG(status.st_mode)) ++files;
    }
    return files;
}
static void budget_helper(void)
{
    static int kept[PROTON_FD_BUDGET_CAP], scratch[PROTON_FD_BUDGET_CAP];
    unsigned count = 0;
    const char *path = budget_path();
    struct ProtonGpuProbeReply reply = {PROTON_GPU_PROBE_MAGIC, getpid(), path ? 0 : -1, budget_files(), -1, 0};
    if (!ready(3, POLLOUT) || send(3, &reply, sizeof(reply), MSG_NOSIGNAL) != sizeof(reply) || reply.status) return;
    for (;;) {
        struct ProtonGpuProbeMessage message = {0};
        int fd = -1;
        if (!receive_message(&message, &fd)) break;
        int give = -1;
        reply.status = -1; reply.value = 0; reply.elevation = 0; reply.map_stage = 0;
        if (message.operation == PROTON_FD_BUDGET_HEADROOM && fd < 0) {
            unsigned n = 0;
            for (; n < PROTON_FD_BUDGET_CAP; ++n)
                if ((scratch[n] = open(path, O_RDONLY)) < 0) { reply.elevation = *__error(); break; }
            for (unsigned i = 0; i < n; ++i) close(scratch[i]);
            reply.value = n;
            reply.status = 0;
        } else if (message.operation == PROTON_FD_BUDGET_OPEN && fd < 0) {
            unsigned n = 0;
            while (n < message.value && count < PROTON_FD_BUDGET_CAP) {
                const int opened = open(path, O_RDONLY);
                if (opened < 0) { reply.elevation = *__error(); break; }
                kept[count++] = opened; ++n;
            }
            reply.value = n;
            reply.status = n == message.value ? 0 : -1;
        } else if (message.operation == PROTON_FD_BUDGET_RECEIVE && fd >= 0 && count < PROTON_FD_BUDGET_CAP) {
            kept[count++] = fd; fd = -1;
            reply.status = 0;
        } else if (message.operation == PROTON_FD_BUDGET_GIVE && fd < 0 && count) {
            give = kept[--count];
            reply.status = 0;
        } else if (message.operation == PROTON_FD_BUDGET_CLOSE && fd < 0) {
            while (count) close(kept[--count]);
            reply.status = 0;
        }
        if (fd >= 0) close(fd);
        reply.map_stage = (int)budget_files();
        struct iovec vector = {&reply, sizeof(reply)};
        union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
        struct msghdr packet = {0};
        packet.msg_iov = &vector; packet.msg_iovlen = 1;
        if (give >= 0) {
            packet.msg_control = control.bytes; packet.msg_controllen = sizeof(control.bytes);
            struct cmsghdr *header = CMSG_FIRSTHDR(&packet);
            header->cmsg_level = SOL_SOCKET; header->cmsg_type = SCM_RIGHTS; header->cmsg_len = CMSG_LEN(sizeof(int));
            *(int *)CMSG_DATA(header) = give;
        }
        const int sent = ready(3, POLLOUT) && sendmsg(3, &packet, MSG_NOSIGNAL) == sizeof(reply);
        if (give >= 0) close(give); /* The owner holds the only copy now. */
        if (!sent) break;
    }
    while (count) close(kept[--count]);
}

/* The flexible-memory probe's helper (native/proton_flex_probe.h): its own and its pool's flexible figures at the
 * start, after 64 MiB of direct memory is mapped and written, after 64 MiB of anonymous memory is, and after a 16 MiB
 * JIT object is; and which ways to executable memory hold here. It holds all of it until the title releases it. */
extern int sceKernelGetResidentFmemCount(int, uint64_t *, uint64_t *);
static void flex_point(struct ProtonFlexReply *reply, unsigned point)
{
    uint64_t resident = 0, mapped = 0;
    size_t available = 0;
    const int counted = sceKernelGetResidentFmemCount(getpid(), &resident, &mapped);
    reply->resident[point] = counted ? -1 : (int64_t)resident;
    reply->mapped[point] = counted ? -1 : (int64_t)mapped;
    reply->available[point] = sceKernelAvailableFlexibleMemorySize(&available) ? -1 : (int64_t)available;
    int cpu_total = -1, cpu_free = -1, gpu_total = -1, gpu_free = -1;
    if (!sceKernelGetPageTableStats(&cpu_total, &cpu_free, &gpu_total, &gpu_free)) {
        reply->pt_cpu_total[point] = cpu_total;
        reply->pt_cpu_free[point] = cpu_free;
        reply->pt_gpu_total = gpu_total;
        reply->pt_gpu_free = gpu_free;
    } else {
        reply->pt_cpu_total[point] = -1;
        reply->pt_cpu_free[point] = -1;
    }
}
static void flex_touch(void *at, uint64_t bytes)
{
    for (uint64_t offset = 0; offset < bytes; offset += 16384) ((volatile char *)at)[offset] = 1;
}
static void flex_helper(void)
{
    struct ProtonFlexReply reply = {0};
    reply.magic = PROTON_FLEX_MAGIC; reply.pid = getpid();
    reply.elevation = -1;
    reply.pt_gpu_total = reply.pt_gpu_free = -1;
    reply.jit80_made = reply.jit80_mapped = reply.jit80_protect = reply.jit80_value = -1;
    reply.nofile_cur = reply.nofile_max = reply.nofile_raise_rc = reply.nofile_raise_cur = -1;
#ifdef PROTON_NATIVE_GPU_ELEVATION
    static const struct ps5_elevation_config grant = {
        "/download0/elevate_proc", "/download0", "/download0/lapy_owned_result", "/data", 60, 50000
    };
    reply.elevation = ps5_elevation_request_with(&grant, PS5_ELEVATION_FILESYSTEM);
    if (geteuid() != 0) {
        (void)seteuid(geteuid());
        for (int wait = 0; wait < 150 && geteuid() != 0; ++wait) sceKernelUsleep(20000);
    }
#endif
    reply.euid = (int32_t)geteuid();
    struct rlimit rl = {0};
    if (!getrlimit(RLIMIT_NOFILE, &rl)) {
        reply.nofile_cur = (int32_t)rl.rlim_cur;
        reply.nofile_max = (int32_t)rl.rlim_max;
        struct rlimit raised = { 4096, rl.rlim_max > 4096 ? rl.rlim_max : 4096 };
        reply.nofile_raise_rc = setrlimit(RLIMIT_NOFILE, &raised);
        if (!getrlimit(RLIMIT_NOFILE, &rl)) reply.nofile_raise_cur = (int32_t)rl.rlim_cur;
    }
    flex_point(&reply, FLEX_START);
    int64_t physical = -1;
    void *direct = 0;
    reply.direct_alloc = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(), PROTON_FLEX_BYTES, 2u << 20, 12, &physical);
    if (!reply.direct_alloc) reply.direct_map = sceKernelMapDirectMemory(&direct, PROTON_FLEX_BYTES, PROT_READ | PROT_WRITE, 0, physical, 2u << 20);
    if (!reply.direct_alloc && !reply.direct_map) flex_touch(direct, PROTON_FLEX_BYTES);
    flex_point(&reply, FLEX_DIRECT);
    void *anon = mmap(0, PROTON_FLEX_BYTES, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    reply.anon_map = anon == MAP_FAILED ? -1 : 0;
    if (!reply.anon_map) flex_touch(anon, PROTON_FLEX_BYTES);
    flex_point(&reply, FLEX_ANON);
    int jit = -1;
    reply.jit_made = sceKernelJitCreateSharedMemory("pw-flex-16m", 16u << 20, 7, &jit);
    if (!reply.jit_made) {
        void *code = mmap(0, 16u << 20, PROT_READ | PROT_WRITE, MAP_SHARED, jit, 0);
        if (code != MAP_FAILED) flex_touch(code, 16u << 20);
        else reply.jit_made = -1;
    }
    flex_point(&reply, FLEX_JIT);
    int jit80 = -1;
    reply.jit80_made = sceKernelJitCreateSharedMemory("pw-flex-80m", 80u << 20, 7, &jit80);
    if (!reply.jit80_made) {
        void *code80 = mmap(0, 80u << 20, PROT_READ | PROT_WRITE, MAP_SHARED, jit80, 0);
        reply.jit80_mapped = code80 == MAP_FAILED ? -1 : 0;
        if (!reply.jit80_mapped) {
            static const unsigned char body[] = {0xb8, 0x2a, 0x00, 0x00, 0x00, 0xc3};
            for (unsigned i = 0; i < sizeof(body); ++i) ((volatile unsigned char *)code80)[i] = body[i];
            reply.jit80_protect = sceKernelMprotect(code80, 16384, PROT_READ | PROT_EXEC);
            void *s = 0, *e = 0;
            int prot = 0;
            if (!reply.jit80_protect && !sceKernelQueryMemoryProtection(code80, &s, &e, &prot) && (prot & PROT_EXEC))
                reply.jit80_value = ((int (*)(void))(uintptr_t)code80)();
        }
        if (jit80 >= 0) close(jit80);
    }
    for (unsigned way = 0; way < FLEX_EXEC_WAYS; ++way) proton_flex_exec_try(way, &reply.exec[way]);
    if (ready(3, POLLOUT) && send(3, &reply, sizeof(reply), MSG_NOSIGNAL) == sizeof(reply)) {
        int32_t owned_id = -1;
        int asked = 0;
        for (int wait = 0; wait < 12 && !asked; ++wait) asked = ready(3, POLLIN); /* the title holds it 10 s first */
        if (asked && recv(3, &owned_id, sizeof(owned_id), 0) == sizeof(owned_id) && owned_id > 0) {
            const int32_t completed = 37;
            if (ready(3, POLLOUT)) (void)send(3, &completed, sizeof(completed), MSG_NOSIGNAL);
        }
    }
    /* Held until Sony closes the process; the title measures its pool after the close. */
}

__attribute__((noreturn)) void _start(void *parameters, void *teardown)
{
    (void)teardown;
#ifdef PROTON_NATIVE_GPU_ELEVATION
    _init_env(parameters);
#endif
    started = 1;
    const int argc = *(const int *)parameters;
    char **argv = (char **)((char *)parameters + sizeof(uint64_t));
    char entry[] = "[PS5 service helper] entry argc=?\n";
    entry[sizeof(entry) - 3] = argc >= 0 && argc <= 9 ? '0' + argc : 'X';
    (void)write(2, entry, sizeof(entry) - 1);
#ifdef PROTON_NATIVE_VULKAN_HELPER
    if(argc>=3 && argc<=7 && equal(argv[argc-3],"--service-native-vulkan-helper") &&
       argv[argc-1][0]>='0' && argv[argc-1][0]<='1' && !argv[argc-1][1]) {
        const int result=vulkan_helper((unsigned)(argv[argc-1][0]-'0'));
        log_stage(result ? "gpu-vulkan loader FAIL\n" : "gpu-vulkan loader PASS\n");
        for(;;)sceKernelUsleep(100000);
    }
#endif
    if(argc>=2 && argc<=6 && equal(argv[argc-2],"--service-native-gpu-helper")) {
        gpu_helper();for(;;)sceKernelUsleep(100000);
    }
    if (argc >= 2 && argc <= 6 && equal(argv[argc - 2], "--service-native-budget-helper")) {
        budget_helper();
        for (;;) sceKernelUsleep(100000);
    }
    if (argc >= 2 && argc <= 6 && equal(argv[argc - 2], "--service-native-memory-helper")) {
        memory_helper();
        for (;;) sceKernelUsleep(100000);
    }
    if (argc >= 2 && argc <= 6 && equal(argv[argc - 2], "--service-native-flex-helper")) {
        flex_helper();
        for (;;) sceKernelUsleep(100000);
    }
    // Sony may prepend the image pathname. Match our explicit final two
    // arguments rather than assuming the service's argv[0] convention.
    if (argc >= 2 && argc <= 6 && equal(argv[argc - 2], "--service-native-helper")) {
        log_stage("[PS5 service helper] args matched\n");
        struct Handshake reply = {0};
        reply.app = -1; reply.physical = -1;
        int valid = 1;
        for (unsigned i = 0; i < 32; ++i) {
            const char c = argv[argc - 1][i];
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) { valid = 0; break; }
            reply.identity[i] = c;
        }
        // GetParentSocket's reviewed implementation is the constant 3.
        // Validate the socket ourselves, without importing SystemService's dependencies.
        socklen_t length = sizeof(reply.socket_type);
        if (valid && argv[argc - 1][32] == 0 &&
            getsockopt(3, SOL_SOCKET, SO_TYPE, &reply.socket_type, &length) == 0 &&
            reply.socket_type == SOCK_SEQPACKET) {
            log_stage("[PS5 service helper] fd3 socket valid\n");
            reply.pid = getpid();
            // Hold 1 MiB until Sony closes the process. The parent measures
            // available direct memory during and after each owned child.
            reply.allocation_bytes = 1024 * 1024;
            reply.allocation_status = sceKernelAllocateDirectMemory(0, sceKernelGetDirectMemorySize(),
                reply.allocation_bytes, 65536, 12, &reply.physical);
            if (ready(3, POLLOUT) && send(3, &reply, sizeof(reply), MSG_NOSIGNAL) == sizeof(reply)) {
                log_stage("[PS5 service helper] reply sent\n");
                int32_t owned_id = -1;
                if (ready(3, POLLIN) && recv(3, &owned_id, sizeof(owned_id), 0) == sizeof(owned_id) && owned_id > 0) {
                    log_stage("[PS5 service helper] release received\n");
                    const int32_t completed = 37; // Protocol completion, not a native exit code.
                    if (ready(3, POLLOUT)) (void)send(3, &completed, sizeof(completed), MSG_NOSIGNAL);
                }
            }
        }
    }
    log_stage("[PS5 service helper] waiting for service close\n");
    for (;;) sceKernelUsleep(100000);
}
