/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_WINE_PROCESS_H
#define PW_WINE_PROCESS_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
/* Bumped once per round of protocol changes, so a loader and an ntdll from different rounds refuse each other at the
 * bootstrap (native/proton_wine_child.c) instead of misreading a message. 0x50573511: the round that bounded the
 * memory helper, set helper scheduling and moved execute to JIT memory (the loader needs ntdll's
 * __wine_ps5_set_payload_thread_start, no longer makes PRX text executable, and reads 7 values from
 * __wine_ps5_exec_stats). 0x50573512: shared code copies (PW_CHILD_CODE_ASK, PW_CHILD_CODE_OFFER answered,
 * PW_CHILD_CODE_MAPPED), which an older title would read as a malformed message and close the helper over. */
#define PW_CHILD_PROTOCOL 0x50573512u
/* PW_CHILD_INPUT: after the bootstrap, the child makes its input page and sends it to the title in a
 * PW_CHILD_INPUT_PAGE message (pw_wine_sink.h). PW_CHILD_AUDIO: then its audio ring, in a PW_CHILD_AUDIO_RING message
 * (src/pw_audio_ring.h). Both are the child's, so nothing the title made outlives it (src/pw_shared_page.h). */
/* PW_CHILD_MEMORY: the child is the memory helper (src/pw_memory_helper.h): no Windows program, it answers the
 * title's requests for shared memory on its control socket until the title is gone. */
/* PW_CHILD_FIXED_BASE: the image this helper runs is 32-bit with its relocations stripped (src/pw_pe_arch.h), so its
 * loader gives the carrier's image at 0x400000 back before Wine starts (native/proton_payload_child.c). */
enum { PW_CHILD_INITIAL = 1, PW_CHILD_GRAPHICS = 2, PW_CHILD_GPU = 4, PW_CHILD_INPUT = 8, PW_CHILD_AUDIO = 16,
       PW_CHILD_MEMORY = 32, PW_CHILD_FIXED_BASE = 64 };
/* Alpha 7's default, "Run inside MihawkOS": ntdll asks this as a program starts (patch 2403) and passes its answer as
 * the request's flags: PW_CHILD_GRAPHICS for a program the title runs itself (wine/ps5/pw_wine_title_game.h, by its
 * path, its name, the per-game lists and what it imports, wine/ps5/pw_wine_pe_imports.h), else 0. Every library program's
 * route is logged with its reason. unix_name is the image's Unix path (NULL: its imports are not read); command_line tells a
 * Chromium game's own processes (--type=) from its main one. */
unsigned int pw_ps5_title_route(const char *unix_name, unsigned short machine, const char *image, const char *command_line);
/* The title runs the session's root itself: the helpers it starts are its game's (pw_ps5_wait_children). */
void pw_ps5_set_title_client_live(void);
struct pw_child_bootstrap {
    uint32_t protocol, size;
    uint32_t flags, argc;
    char runtime[1024], prefix[1024], cwd[1024], dllpath[2048], overrides[512], debug[512];
    char initial_args[4096];
    /* The spawning process's settings for Wine and its graphics (pw_wine_process.c names which): NAME=VALUE strings,
     * each ended by a zero, the list by an empty one. A helper had none, and Wine's display there was 800x600. */
    char environment[4096];
    /* The new process's executable and command line as Windows names them (UTF-8; an image too long is empty, a command
     * line too long keeps its start), so the title can find its catalog entry (pw_ps5_set_application_lookup) and log
     * it. 8192: Chromium's child command lines run past 2,000 characters (2026-10-11; record removed from the tree; in git history). */
    char image[1024], command_line[8192];
    /* Explicitly selected UTF-8 overrides: for the initial application its catalog entry's, from the title; for a
     * process another one started, the ones the title's lookup found for its image. Never from the requester. */
    uint32_t application_bytes;
    char application[4096];
};
struct pw_child_message { uint32_t protocol, kind; int32_t pid, value; int64_t tid; };
/* PW_CHILD_FRAME: a helper's frame page (src/pw_frame_page.h), with its descriptor, once, on its first frame. */
/* PW_CHILD_RESOLVE: a helper asks the title for a name's IPv4 address (struct pw_child_resolve), with a socket for
 * the answer (struct pw_child_resolved). */
/* PW_CHILD_MEMORY_SERVICE: after its input page and audio ring, a helper gives the title the socket on which it
 * serves its own memory (src/pw_memory_service.h), before it says it is ready, so no other process can ask before
 * the title has it. */
enum { PW_CHILD_READY = 1, PW_CHILD_SIGNAL, PW_CHILD_EXIT, PW_CHILD_SPAWN, PW_CHILD_SPAWN_REPLY, PW_CHILD_INPUT_PAGE,
       PW_CHILD_FRAME, PW_CHILD_RESOLVE, PW_CHILD_AUDIO_RING, PW_CHILD_MEMORY_SERVICE, PW_CHILD_DNS_CONFIG, PW_CHILD_WINDOW,
       PW_CHILD_DSECTION, PW_CHILD_DOOMED, PW_CHILD_CODE_ASK, PW_CHILD_CODE_OFFER, PW_CHILD_CODE_MAPPED,
       PW_CHILD_CODE_ASK_FILE, PW_CHILD_CODE_OFFER_FILE };
/* PW_CHILD_DOOMED: a helper tells the title it stamped a doom reason (value; only PW_CHILD_DOOM_GPU is sent): one
 * non-blocking send, no answer. A title from before it ignores the kind. */
/* PW_CHILD_DSECTION: a helper asks the title for the direct memory behind a page of a large anonymous section
 * (wine/ps5/pw_wine_dsection.h), with a socket for the answer. Its pager asks from a fault handler, so this goes over
 * the helper's own channel, never through a server request the faulting thread may be in the middle of. */
struct pw_child_dsection { struct pw_child_message message; uint64_t dev, ino, offset; };
struct pw_child_dsection_answer { int32_t status; int32_t pad; int64_t dmem; uint64_t chunk_offset, chunk_bytes; };
/* PW_CHILD_CODE_ASK: a helper asks the title's registry of shared code copies (wine/ps5/pw_wine_codeshare.h) for the
 * JIT object of a cluster by its size and content hash, with a socket for the answer, which carries the object's
 * descriptor when status is 0. PW_CHILD_CODE_OFFER: a helper gives the registry the object it made for such a cluster,
 * with its descriptor and a socket for the answer: 0 kept, EEXIST or ENOSPC not (the copy stays the helper's own).
 * Both come from the execute fault handler, so over the helper's own channel, as PW_CHILD_DSECTION. The title's
 * WINE_PS5_CODE_SHARE=0 answers every ask and offer ENOSYS: helpers then copy their code privately, as before.
 * PW_CHILD_CODE_MAPPED: the KiB of registry objects the helper maps now (value), sent from its telemetry when it
 * changes; no answer. The title's census counts each object once from these (pw_ps5_code_share_duplicate_bytes). */
struct pw_child_code { struct pw_child_message message; uint64_t hash, bytes; };
/* A file cluster's object: the file identity (dev, inode) and the raw offset instead of the content hash. */
struct pw_child_code_file { struct pw_child_message message; uint64_t file[3]; uint64_t bytes; };
struct pw_child_code_answer { int32_t status; int32_t pad; };
struct pw_child_spawn { struct pw_child_message message; struct pw_child_bootstrap bootstrap; };
struct pw_child_resolve { struct pw_child_message message; char name[256]; };
struct pw_child_resolved { int32_t status; unsigned char address[4]; };
int pw_ps5_spawn(int server_fd, const char *runtime, const char *prefix, const char *cwd, const char *debug, unsigned flags,
                 const char *image, const char *command_line);
/* The title: the settings a new process gets for its executable (the catalog's game.json "env", per application in
 * the Mihawk OS session). lookup fills block (pw_environment_pack's format) and returns 0, or nonzero for none. It is
 * called on the spawning threads, so it must only read. */
void pw_ps5_set_application_lookup(int (*lookup)(const char *image, const char *command_line, char *block,
                                                 size_t capacity, uint32_t *bytes));
void pw_ps5_set_graphics_start(int (*start)(int,const struct pw_child_bootstrap *));
/* The title's start of the one program it runs itself beside the GPU broker (docs/TITLE_CLIENT.md): a process whose
 * image WINE_PS5_TITLE_PROCESS names is sent there instead of to a helper, until one runs (EBUSY: a helper). */
void pw_ps5_set_title_client_start(int (*start)(int,const struct pw_child_bootstrap *));
/* The title: 1 when the image a new process runs (its Windows path) is a fixed-base 32-bit one (src/pw_pe_arch.h).
 * Called on the spawning threads for every helper but the memory helper, so it must only read. */
void pw_ps5_set_fixed_base_lookup(int (*lookup)(const char *image));
/* The title: what it says on screen about helpers (native/proton_wine.cpp's notices): a helper that could not start
 * (PW_CHILD_NOTICE_SPAWN, value its errno) and one that gave the GPU broker up (PW_CHILD_NOTICE_GPU). For every helper
 * started, before it runs, its own Game Mode (PW_CHILD_NOTICE_GAME_MODE, value 1 on, 0 off, -1 none): the
 * WINE_PS5_GAME_MODE of its library entry's settings, else of the settings it inherited (src/pw_window_direct.h).
 * Called on the spawning and monitoring threads, some holding locks: it must only record. */
enum { PW_CHILD_NOTICE_SPAWN = 1, PW_CHILD_NOTICE_GPU, PW_CHILD_NOTICE_GAME_MODE };
void pw_ps5_set_child_notice(void (*notice)(int kind, int pid, int value));
void pw_ps5_set_gpu_broker(int (*open)(int *,int),void (*close)(int,int));
/* The title: who takes a helper's frame page (attach owns the descriptor) and hears that the helper is gone. */
void pw_ps5_set_frame_sink(void (*attach)(int pid,int fd),void (*detach)(int pid));
/* A helper: give the title its frame page. 0, or -1 when this process is not a helper. */
int pw_ps5_child_frame(int fd);
/* Window page + a private ACK socket. The owner consumes the received fd and
 * answers only after extent, lifetime and capacity checks. Sender borrows fd. */
void pw_ps5_set_window_sink(int (*attach)(int pid,int fd,size_t bytes));
int pw_ps5_child_window(int fd,size_t bytes);
/* The network is the title's: only it loads libSceNet (a helper that did took the title down). The title gives the
 * resolver it implements; pw_wine_resolve answers with it in the title and asks the title from a helper. Both return
 * 0 and the IPv4 address in network order, or an EAI_ error. */
void pw_ps5_set_resolver(int (*resolve)(const char *name,unsigned char address[4]));
int pw_wine_resolve(const char *name,unsigned char address[4]);
/* Real DNS configuration from the owner, without private addresses in logs or
 * environment. family is 4 or 6; bytes are in network order. At most two of each.
 * The packet reply socket is owned until one complete reply or timeout. */
struct pw_dns_server { uint32_t family; unsigned char address[16]; };
struct pw_dns_config { uint32_t count; struct pw_dns_server servers[4]; };
struct pw_child_dns_config { int32_t status; struct pw_dns_config config; };
void pw_ps5_set_dns_config(int (*read)(struct pw_dns_config *));
int pw_wine_dns_config(struct pw_dns_config *config);
/* Install once before entering Wine/starting the root. Copies the bounded block;
 * caller storage may be released immediately. Never scan the owner's environment.
 * An empty list is two NULs. The getter stays valid for this process's lifetime. */
int pw_ps5_set_initial_environment(const char *block,size_t bytes);
const char *pw_ps5_initial_environment(void);
int pw_ps5_start_root(int server_fd,const char *runtime,const char *prefix,const char *cwd,int argc,const char *const *argv);
/* The title, before its server starts: start the memory helper. Then shared memory the title's server and its GPU
 * broker hand to helpers is made by it (pw_ps5_shared_alloc, -1 when there is no helper or it failed) and its pages
 * written through by it before the title writes them (pw_ps5_shared_touch: grow fd to `to` bytes if smaller, then
 * every page of [from, to); 0 or -1), so the title owns none of it (src/pw_shared_page.h).
 * pw_ps5_start_memory_helper: 0; -1 with ENOTCONN when the helper started but did not answer its first request in
 * time (src/pw_memory_helper.h): it is marked dead and kept, never closed, and the title must not go on, since it would
 * make its shared memory itself; -1 with another errno when there is no helper.
 * Every call below fails with ENOTCONN once the helper is marked dead, the call that marked it included (that one is
 * logged, with what was asked, the transport's errno and how long it waited); later ones fail at once. A caller that
 * would otherwise make the memory in the title must not on ENOTCONN (src/pw_shared_page.h). */
int pw_ps5_start_memory_helper(void);
/* 1 when a thread holds the memory-helper lock right now (a sample: the lock is tried and given back at once), else 0.
 * For the title's hang watch: one request holds it at most PW_MEMORY_REPLY_MS. */
int pw_ps5_memory_lock_busy(void);
int pw_ps5_shared_alloc(size_t bytes);
/* Memory-only broker backing. Wine sections can later need byte-I/O/COW. */
int pw_ps5_transport_alloc(size_t bytes);
int pw_ps5_shared_touch(int fd, size_t from, size_t to);
/* Direct memory shared with one client (src/pw_memory_helper.h, "dmem"; native/pw_dmem_share.h): bytes granted to the title
 * and to client_pid, made by the memory helper, which owns it until pw_ps5_dmem_free. 0 with *offset, or -1 with errno. */
int pw_ps5_dmem_alloc(size_t bytes, int client_pid, int64_t *offset);
int pw_ps5_dmem_free(int64_t offset, size_t bytes);
/* The title: grant a range the memory helper owns to one more pid. 0, or -1 with errno. */
int pw_ps5_dmem_grant(int64_t offset, size_t bytes, int pid);
/* The title: the server's hooks for large anonymous sections (patches 1121, 1126): register one made under its backing
 * file's identity, and release it when it goes. fd is the backing file: duplicated and held by the title until the
 * section's memory is given back, so (dev, ino) names nothing else meanwhile; the caller keeps its own. Release only
 * marks the section and never waits on the memory helper; a title thread gives the memory back once every process
 * admitted to the section has ended (wine/ps5/pw_wine_dsection.h). */
int pw_ps5_dsection_register(int fd, uint64_t dev, uint64_t ino, uint64_t size);
void pw_ps5_dsection_release(uint64_t dev, uint64_t ino);
/* A helper: the direct memory behind offset of the section the file (dev, ino) backs, granted to this process: the
 * chunk's direct-memory offset, its offset in the section and its size. 0, or -1 with errno (ENOENT: not such a
 * section, map the file). Safe in a fault handler: system calls only, on a socket of its own. */
int pw_ps5_dsection_page(uint64_t dev, uint64_t ino, uint64_t offset, int64_t *dmem, uint64_t *chunk_offset,
                         uint64_t *chunk_bytes);
/* A helper: the registry's object of a code cluster of bytes whose content hashes to hash, asked from the execute fault
 * handler (pw_wine_dmem_ps5.c): 0 with *fd (the caller's to close), or -1 with errno (ENOENT: none; ENOSYS: the title
 * keeps none; ETIMEDOUT: no answer in 1 s). pw_ps5_code_offer gives the title the object fd of such a cluster (fd stays
 * the caller's): 0 sent, or -1 with errno. Safe in a fault handler: system calls only, on sockets of their own. */
int pw_ps5_code_ask(uint64_t hash, uint64_t bytes, int *fd);
int pw_ps5_code_ask_file(const uint64_t file[3], uint64_t bytes, int *fd);
int pw_ps5_code_offer_file(const uint64_t file[3], uint64_t bytes, int fd);
int pw_ps5_code_offer(uint64_t hash, uint64_t bytes, int fd);
/* Its tag on the virtual mutex's record (pw_wine_perf_vlock_doing) while it waits up to 10 s for the title, mutex held:
 * a bounded wait the stall watch leaves out (src/pw_control_watch.h pw_control_stalled). */
#define PW_CHILD_PAGER_ASK "pager-ask"
/* The title: the latest census of the console's system memory, which the memory helper takes every half second
 * (src/pw_memory_helper.h); read from the page it writes, so the caller never waits. 0, or -1 with errno ENOENT when
 * there is no census (no memory helper, or one that takes none) and EAGAIN when none is in yet. */
struct pw_memory_census;
int pw_ps5_memory_census(struct pw_memory_census *census);
/* The first and the latest cold helper start this session, request to ready, ms (0 before one). */
void pw_ps5_start_times(uint64_t *first_ms, uint64_t *last_ms);
/* Native bootstrap verdict: -1 pending, 0 success, 1 failure. Windows keeps
 * the complete DWORD exit status in the shared wineserver. */
int pw_ps5_root_result(void);
/* The title's desktop session (src/pw_desktop_session.h): whether the root runs, and ending it on purpose (game mode);
 * its exit is then not the result above. pw_ps5_unload_root: 0 when asked, -1 with ESRCH without a root. */
int pw_ps5_root_present(void);
int pw_ps5_unload_root(void);
/* The title: the native pids of the helpers that run Windows programs (every slot with service > 0 and pid > 0, closing
 * or not), up to capacity, and in *memory_pid (may be NULL) the memory helper's, 0 when there is none. Returns how many
 * were written. Holds child_lock only; allocates nothing. */
unsigned pw_ps5_helpers(int *pids, unsigned capacity, int *memory_pid);
/* The title, before it ends the game's helpers by SIGKILL: no helper starts after this (ESHUTDOWN), and one closed
 * afterwards that does not leave is sent SIGKILL and waited for until ESRCH, so pw_ps5_wait_children returns only once
 * every helper spawned or spawning is gone. */
void pw_ps5_end_spawns(void);
/* The title: one pass of the direct-section collector (its 'gave back' line), on the way out after every helper ended,
 * so a section released in the last second is given back before the title is replaced. */
void pw_ps5_dsection_collect_now(void);
int pw_ps5_remote_close(pid_t pid);
void pw_ps5_wait_children(void);
int pw_ps5_remote_signal(pid_t pid, long tid, int signal);
int pw_ps5_child_start(int control_fd);
/* The protocol this ntdll speaks, which a helper's loader compares with its own. */
unsigned int pw_ps5_child_protocol(void);
/* The title's server: copy size bytes between buffer and address in the helper whose native pid this is (writing:
 * into the helper). 0 with *done bytes copied (short when the helper's memory was not readable or writable all the
 * way), or -1 when no helper of that pid answers. */
int pw_ps5_child_memory(int pid, int writing, unsigned long long address, void *buffer, unsigned int size,
                        unsigned int *done);
/* A helper: tell the title it ended with code (PW_CHILD_EXIT, sent once per process: after this or pw_ps5_child_exit
 * the other sends nothing). One non-blocking send, retried for about 2 s while the control socket is full; no lock
 * taken and nothing allocated, so a thread holding Wine's virtual mutex (an assertion) may call it. Nothing in the
 * title. */
void pw_ps5_child_exit_notice(unsigned int code);
void pw_ps5_child_exit(unsigned int status) __attribute__((noreturn));
/* A helper that can no longer end through Wine ends anyway. In a 2026-10-15 run (record removed from the tree; in git history) the broker client gave up after
 * 15 s (device lost), the exit never reached the server's thread kills and the console froze, while the loader's
 * control thread, which takes no Wine lock, stayed alive throughout. The reasons, each stamped once:
 *   GPU     the GPU broker's client gave the broker up for good: no answer in 15 s, the connection gone, a malformed
 *           reply, a free not taken back (graphics patch 0017, client.c broker_lost, through the lookup pw_wine_dl.c
 *           hands it). Not a Vulkan result: a VK_ERROR_DEVICE_LOST from a present the broker refused and answered
 *           stamped gpu-window32/64 in gate-hang-a-2026-10-15-01, whose programs presented on;
 *   EXIT    NtTerminateProcess of this process began (patch 1135, before its write-behind flush, which can wait on a
 *           lock a stuck thread holds);
 *   ENGINE  the 4 KiB MMU faulted inside itself with the virtual mutex held;
 *   TEST    WINETEST_DOOM_AFTER_MS (the loader);
 *   STALL   Wine's virtual mutex (outside the pager's ask, before EXIT) or the direct-memory engine's lock held 3 s in
 *           one hold (the loader's stall watch, src/pw_control_watch.h pw_control_stalled).
 * pw_ps5_child_doomed keeps the first CLOCK_MONOTONIC millisecond of each reason with one compare-exchange: no lock,
 * nothing allocated, so any thread, a signal handler too, may call it; any other reason is ignored. pw_ps5_child_doom
 * fills stamps[reason - 1] (0: not stamped) for up to count reasons with atomic loads and returns PW_CHILD_DOOMS. The
 * loader reads them every 100 ms and returns into _exit once a reason's grace is up (src/pw_control_watch.h), unless
 * the title closed the helper first. Nothing reads them in the title. */
enum { PW_CHILD_DOOM_GPU = 1, PW_CHILD_DOOM_EXIT, PW_CHILD_DOOM_ENGINE, PW_CHILD_DOOM_TEST, PW_CHILD_DOOM_STALL,
       PW_CHILD_DOOMS = 5 };
void pw_ps5_child_doomed(unsigned int reason);
unsigned int pw_ps5_child_doom(int64_t *stamps, unsigned int count);
/* Their record, over the given array (host-tested in tests/test_pw_control_watch.c): the first stamp of a reason wins,
 * a now_ms of 0 is kept as 1 (0 is "not stamped"); 1 when this call stamped it. */
static inline int pw_child_doom_stamp(int64_t *record, unsigned int reason, int64_t now_ms)
{
    int64_t none = 0;
    return reason >= 1 && reason <= PW_CHILD_DOOMS &&
           __atomic_compare_exchange_n(&record[reason - 1], &none, now_ms ? now_ms : 1, 0, __ATOMIC_SEQ_CST,
                                       __ATOMIC_RELAXED);
}
static inline unsigned int pw_child_doom_read(const int64_t *record, int64_t *stamps, unsigned int count)
{
    for (unsigned int i = 0; i < count && i < PW_CHILD_DOOMS; i++) stamps[i] = __atomic_load_n(&record[i], __ATOMIC_SEQ_CST);
    return PW_CHILD_DOOMS;
}
#endif
