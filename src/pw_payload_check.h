/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef PW_PAYLOAD_CHECK_H
#define PW_PAYLOAD_CHECK_H
/*
 * The payload checklist: MihawkOS runs only with the official payload set, and nothing more (the owner, 2026-10-29: most
 * users' problems came from other payloads, and every payload holds memory games need). Every start takes it
 * (ps5/src/proton_payloads.cpp gathers the facts; the launcher shows it and refuses entry until it passes; a game start
 * that fails it returns to the launcher).
 *
 *   required  elfldr, Kstuff Lite 1.11 Beta or later, ShadowMountPlus (beta11 or newer: shown, not checked), websrv
 *   optional  NanoDNS (the owner, 2026-10-31: warn but do not block), klogsrv, ftpsrv, pldmgr (2026-10-29: pldmgr allowed)
 *   ours      what MihawkOS starts itself: the Lapy daemon, its helpers, the tiles' worker, the harness's ps5vkctl
 *   unnamed   a payload the kernel lists only as "payload": it is shown and said plainly, and does not block
 * Any other payload fails it.
 *
 * The Lapy daemon is listed as "payload" (websrv's child; payload-quick-2026-10-29-02), and so can be NanoDNS loaded
 * through websrv on 13.60 (2026-10-31). The name alone cannot tell them apart, so the daemon is the websrv child named
 * "payload" that started when the title started it: the title records the time (PwPayloadFacts.daemon_started) and the
 * kernel's start time must be within PW_PAYLOAD_DAEMON_SLACK_S of it. Every other "payload" is unnamed.
 *
 * How each is known, measured on the base PS5 (CFI-1215A, 13.60) with the official set, 2026-10-29
 * (evidence/payloads-base-2026-10-29-01.json): every payload is elfldr's child, by its file name (elfldr.elf, whose
 * parent is SceRedisServer; kstuff.elf, shadowmountplus.elf, websrv.elf, nanodns.elf, klogsrv.elf, ftpsrv.elf,
 * pldmgr.elf). Resident: shadowmountplus 25.2 MiB, elfldr 22.8, pldmgr 12.3, websrv 8.2, kstuff 7.7, nanodns 4.1,
 * ftpsrv 4.1, klogsrv 3.9. Versions:
 *   ShadowMountPlus  by its process only (the owner, 2026-10-31: a tester's newer one was not detected by its version).
 *                    The label asks for beta11 or newer; nothing compares versions. POST {} to
 *                    127.0.0.1:10101/api/v1/version ("shadowmount_version":"1.7beta10" on the base) is shown when it
 *                    answers, and counts as loaded when the title cannot list processes. 1.7beta6 and later keep
 *                    ShellCore's hook pages resident (upstream 35fff9fc, the fix of the owner's PR 139).
 *   websrv           GET 127.0.0.1:8080/version answers {"tag": "v0.34", ...}; no minimum, shown only.
 *   Kstuff           by its process only. kstuff-lite has no version call; v1.11 is the first to answer kekcall 7, but
 *                    the title cannot ask: a raw system call from a title ends it (SYSTEM_ILLEGAL_FUNCTION_CALL,
 *                    payload-quick-2026-10-29-01). Its minimum version is shown, not checked; a helper could ask.
 * A payload is pw_memory_is_payload's (src/pw_memory_helper.h): not started by SceSysCore and not named "Sce...".
 */
#include <stddef.h>
#include <stdint.h>

/* The payloads' memory past which the launcher and a session say "closing them frees room". The recommended set uses about
 * 90 MiB (the owner, 2026-11-01: "only if payloads are using an unreasonable amount such as 200MiB"), and was warned of at
 * 64. Both places ask this, so they cannot drift apart. The "payloads use N MiB" log line is written whatever the amount. */
#define PW_PAYLOADS_NOTICE_MIB 200
static inline int pw_payloads_notice(unsigned long long mib) { return mib >= PW_PAYLOADS_NOTICE_MIB; }

#ifdef __cplusplus
extern "C" {
#endif

/* One process, as kern.proc lists it. */
typedef struct {
    int pid, ppid;
    char name[20];      /* ki_comm */
    uint64_t resident;  /* bytes */
    int64_t started;    /* ki_start, seconds since the epoch (0: unknown) */
} PwPayloadProcess;

/* How far the daemon's kernel start time may be from the title's record of starting it. */
#define PW_PAYLOAD_DAEMON_SLACK_S 5

/* The payloads the checklist knows. The ones before PW_PAYLOAD_REQUIRED have an ok[] slot; of those, NanoDNS is optional
 * (it warns but does not block pass, see pw_payload_check), the rest block. */
enum {
    PW_PAYLOAD_ELFLDR, PW_PAYLOAD_KSTUFF, PW_PAYLOAD_SHADOWMOUNT, PW_PAYLOAD_WEBSRV, PW_PAYLOAD_NANODNS,
    PW_PAYLOAD_REQUIRED,
    PW_PAYLOAD_KLOG = PW_PAYLOAD_REQUIRED, PW_PAYLOAD_FTP, PW_PAYLOAD_PLDMGR,
    PW_PAYLOAD_KNOWN
};

/* What kstuff's kekcalls said. */
enum { PW_KSTUFF_ABSENT, PW_KSTUFF_OLD, PW_KSTUFF_OK };

/* What the title found besides the processes. A version is "" when nothing answered. */
typedef struct {
    int listed;                    /* 1 when kern.proc answered; 0 fails the check (nothing can be seen) */
    int kstuff;                    /* PW_KSTUFF_...: OK when its process runs (the title cannot tell OLD) */
    char shadowmount_version[32];  /* shown only, never compared */
    char websrv_version[32];
    int data_denied;               /* 1 when the title has no /data (no root): it then sees its own process alone */
    int64_t daemon_started;        /* when a title started the Lapy daemon, seconds since the epoch; 0: not recorded */
} PwPayloadFacts;

#define PW_PAYLOAD_OTHERS 8
typedef struct {
    int running[PW_PAYLOAD_KNOWN];
    uint64_t resident[PW_PAYLOAD_KNOWN];
    int ok[PW_PAYLOAD_REQUIRED];             /* running, and new enough where a version is required */
    unsigned others;                          /* other payloads (all of them counted; the first PW_PAYLOAD_OTHERS named) */
    char other_names[PW_PAYLOAD_OTHERS][20];
    uint64_t other_each[PW_PAYLOAD_OTHERS];   /* bytes, each named one's */
    uint64_t other_resident;                  /* bytes, all of them */
    uint64_t payload_resident;                /* bytes: every payload's, known or not, ours left out */
    unsigned unnamed;                         /* payloads listed only as "payload", not the daemon: shown, never blocking */
    uint64_t unnamed_resident;                /* bytes, all of them */
    int listed;                               /* 0: the processes could not be seen (no list, or not even SceSysCore in it:
                                                 a title without root sees itself alone); nothing is said missing then,
                                                 and only what answered on the loopback is shown running */
    int pass;
} PwPayloadReport;

/* The report for these processes (self: the title's pid; its children are its own). */
void pw_payload_check(const PwPayloadProcess *list, unsigned count, int self, const PwPayloadFacts *facts,
                      PwPayloadReport *out);
/* The payload's name for the screen, with its minimum version where one is required. */
const char *pw_payload_label(int which);
/* What the user must do, in plain words, one sentence per problem; "" when the check passed. Returns the length. */
size_t pw_payload_advice(const PwPayloadReport *report, const PwPayloadFacts *facts, char *out, size_t size);

/* A way through a failing check (the owner, 2026-10-31: "add a hold button for 5 seconds to skip it in case the user
 * doesn't pass the checklist. Not giving a way through causes frustration"). Offered only while the check fails;
 * TRIANGLE held PW_PAYLOAD_SKIP_SECONDS without letting go goes on, for this start only: nothing remembers it, and the
 * next start checks again. A pass offers nothing. */
#define PW_PAYLOAD_SKIP_SECONDS 5.0f
int pw_payload_skip_offered(const PwPayloadReport *report);
/* The hold after a frame of dt seconds: dt more while held, 0 once let go (a new hold starts over); 1 once it reached
 * PW_PAYLOAD_SKIP_SECONDS. */
int pw_payload_skip_hold(float *seconds, int held, float dt);
/* The log line of a skip, with what failed: "skipped by the player after 5 s hold; missing: <labels>; extra: <names>".
 * Returns the length (truncated to size - 1). */
size_t pw_payload_skip_line(const PwPayloadReport *report, char *out, size_t size);

#ifdef __cplusplus
}
#endif
#endif
