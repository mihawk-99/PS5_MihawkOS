/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The payload checklist (src/pw_payload_check.h) on the base PS5's process table of 2026-10-29 (the official set,
 * evidence/payloads-base-2026-10-29-01.json), and on what users load besides. */
#include "../src/pw_payload_check.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define MIB(x) ((uint64_t)((x) * 1048576.0))
#define DAEMON_AT INT64_C(1793302800)   /* when the title started the Lapy daemon, and the kernel's start time for it */
static PwPayloadProcess base[] = {
    { 53, 1, "SceSysCore.elf", 0, 0 },     { 57, 53, "SceShellCore", 0, 0 },   { 69, 53, "SceRedisServer", 0, 0 },
    { 75, 53, "fs_cleaner.elf", MIB(2.7), 0 },   /* SceSysCore's: not a payload */
    { 1, 0, "mini-syscore.elf", 0, 0 },     { 0, 0, "kernel", 0, 0 },
    { 87, 69, "elfldr.elf", MIB(22.8), 0 }, { 93, 87, "pldmgr.elf", MIB(12.3), 0 }, { 97, 87, "nanodns.elf", MIB(4.1), 0 },
    { 100, 87, "kstuff.elf", MIB(7.7), 0 }, { 101, 87, "shadowmountplus.elf", MIB(25.2), 0 },
    { 102, 87, "websrv.elf", MIB(8.2), 0 }, { 103, 87, "klogsrv.elf", MIB(3.9), 0 }, { 104, 87, "ftpsrv.elf", MIB(4.1), 0 },
    /* MihawkOS's own: the title, its daemon through websrv, a helper in its carrier */
    { 120, 53, "eboot.bin", MIB(300), 0 }, { 121, 102, "payload", MIB(3.9), DAEMON_AT + 1 }, { 122, 102, "eboot.bin", MIB(90), 0 },
    { 123, 102, "console-pagetables", MIB(3), 0 },   /* the PC's page-table sampler, websrv's child */
};
enum { BASE = sizeof(base) / sizeof(base[0]), SELF = 120 };

static PwPayloadFacts good(void)
{
    PwPayloadFacts f = { 1, PW_KSTUFF_OK, "1.7beta10", "v0.34", 0, DAEMON_AT };
    return f;
}

static void advise(const PwPayloadReport *r, const PwPayloadFacts *f, char *out)
{
    pw_payload_advice(r, f, out, 1024);
    printf("  advice: %s\n", out[0] ? out : "(none)");
}

int main(void)
{
    PwPayloadReport r;
    PwPayloadFacts f = good();
    char advice[1024];

    /* ShadowMountPlus by presence only (the owner, 2026-10-31): whatever its version says, a loaded one is ready, and the
     * screen still asks for beta11 or newer */
    assert(!strcmp(pw_payload_label(PW_PAYLOAD_SHADOWMOUNT), "ShadowMountPlus (beta11 or newer)"));
    const char *versions[] = { "1.7beta4", "1.7beta4-hook-lifetime", "1.7beta11", "v1.8beta2-3-g8489dbe", "unknown",
                               "a1b2c3d", "", "1.7beta" };
    for (size_t i = 0; i < sizeof(versions) / sizeof(versions[0]); ++i) {
        f = good();
        snprintf(f.shadowmount_version, sizeof(f.shadowmount_version), "%s", versions[i]);
        pw_payload_check(base, BASE, SELF, &f, &r);
        assert(r.pass && r.running[PW_PAYLOAD_SHADOWMOUNT] && r.ok[PW_PAYLOAD_SHADOWMOUNT]);
        advise(&r, &f, advice);
        assert(!advice[0]);
    }
    /* Missing is still missing, named with the version it asks for */
    PwPayloadProcess no_smp[BASE];
    unsigned q = 0;
    for (unsigned i = 0; i < BASE; ++i)
        if (strcmp(base[i].name, "shadowmountplus.elf")) no_smp[q++] = base[i];
    f = good();
    pw_payload_check(no_smp, q, SELF, &f, &r);
    assert(!r.pass && !r.running[PW_PAYLOAD_SHADOWMOUNT] && !r.ok[PW_PAYLOAD_SHADOWMOUNT]);
    advise(&r, &f, advice);
    assert(strstr(advice, "Load ShadowMountPlus (beta11 or newer), then press CROSS"));
    f = good();

    /* The owner's base: passes, the optional three counted, ours and Sony's left out. */
    pw_payload_check(base, BASE, SELF, &f, &r);
    assert(r.pass && !r.others);
    for (int k = 0; k < PW_PAYLOAD_KNOWN; ++k) assert(r.running[k] == 1);
    assert(r.payload_resident == MIB(22.8) + MIB(12.3) + MIB(4.1) + MIB(7.7) + MIB(25.2) + MIB(8.2) + MIB(3.9) + MIB(4.1));
    advise(&r, &f, advice);
    assert(!advice[0]);

    /* Without the optional ones it still passes. */
    PwPayloadProcess bare[BASE];
    unsigned n = 0;
    for (unsigned i = 0; i < BASE; ++i)
        if (strcmp(base[i].name, "klogsrv.elf") && strcmp(base[i].name, "ftpsrv.elf") && strcmp(base[i].name, "pldmgr.elf"))
            bare[n++] = base[i];
    pw_payload_check(bare, n, SELF, &f, &r);
    assert(r.pass && !r.running[PW_PAYLOAD_KLOG]);

    /* An allowed payload neither blocks nor shows (the owner, 2026-10-31). */
    PwPayloadProcess with_allowed[BASE + 1];
    memcpy(with_allowed, base, sizeof(base));
    with_allowed[BASE] = (PwPayloadProcess){ 129, 87, "qafutkn_util_mk2.el", MIB(1), 0 };   /* as the kernel cuts it */
    pw_payload_check(with_allowed, BASE + 1, SELF, &f, &r);
    assert(r.pass && !r.others && !r.unnamed);
    advise(&r, &f, advice);
    assert(!advice[0] && !strstr(advice, "qafutkn"));
    /* the cut must not let a different name through: "qafutkn_util_mk2x" is still another payload */
    with_allowed[BASE] = (PwPayloadProcess){ 129, 87, "qafutkn_util_mk2x", MIB(1), 0 };
    pw_payload_check(with_allowed, BASE + 1, SELF, &f, &r);
    assert(!r.pass && r.others == 1);

    /* An extra payload refuses entry and is named, with its memory. */
    PwPayloadProcess extra[BASE + 2];
    memcpy(extra, base, sizeof(base));
    extra[BASE] = (PwPayloadProcess){ 130, 87, "etaHEN.elf", MIB(40), 0 };
    extra[BASE + 1] = (PwPayloadProcess){ 131, 87, "payload", MIB(3), 0 };   /* sent to elfldr itself: not ours */
    pw_payload_check(extra, BASE + 2, SELF, &f, &r);
    /* the "payload" sent to elfldr is not the daemon and cannot be named: shown as unknown, not as a blocker */
    assert(!r.pass && r.others == 1 && r.other_resident == MIB(40) && r.unnamed == 1 && r.unnamed_resident == MIB(3));
    assert(!strcmp(r.other_names[0], "etaHEN.elf") && r.other_each[0] == MIB(40));
    advise(&r, &f, advice);
    assert(strstr(advice, "1 other payload is running, using 40 MiB") && strstr(advice, "etaHEN.elf."));
    assert(strstr(advice, "1 payload shows only as \"payload\" (3 MiB)") && strstr(advice, "Restart the PS5"));

    /* The daemon is told apart by when the title started it (2026-10-31): a record that matches makes it ours */
    f = good();
    pw_payload_check(base, BASE, SELF, &f, &r);
    assert(r.pass && !r.unnamed && !r.others);
    /* No record (a daemon an older build or someone else started): it is unknown, said plainly, and does not block */
    f.daemon_started = 0;
    pw_payload_check(base, BASE, SELF, &f, &r);
    assert(r.pass && r.unnamed == 1 && r.unnamed_resident == MIB(3.9));
    advise(&r, &f, advice);
    assert(strstr(advice, "1 payload shows only as \"payload\" (4 MiB), so MihawkOS cannot tell which it is"));
    /* A record from another start (an older boot): too far from the kernel's start time, not ours */
    f.daemon_started = DAEMON_AT - 3600;
    pw_payload_check(base, BASE, SELF, &f, &r);
    assert(r.pass && r.unnamed == 1);
    /* The standard PS5 on 13.60 (2026-10-31): NanoDNS loaded through websrv is listed as "payload", started at boot
     * long before the daemon. Only the daemon is ours; NanoDNS is unknown and optional: a pass with two warnings. */
    PwPayloadProcess dns[BASE];
    unsigned d = 0;
    for (unsigned i = 0; i < BASE; ++i)
        if (strcmp(base[i].name, "nanodns.elf")) dns[d++] = base[i];
    dns[d++] = (PwPayloadProcess){ 97, 102, "payload", MIB(4.1), DAEMON_AT - 900 };
    f = good();
    pw_payload_check(dns, d, SELF, &f, &r);
    assert(r.pass && r.unnamed == 1 && r.unnamed_resident == MIB(4.1) && !r.running[PW_PAYLOAD_NANODNS] && !r.others);
    advise(&r, &f, advice);
    assert(strstr(advice, "NanoDNS was not detected") && strstr(advice, "shows only as \"payload\""));
    assert(!strstr(advice, "Load") && !strstr(advice, "other payload"));

    /* Missing ones are named to load. */
    PwPayloadProcess few[] = { { 53, 1, "SceSysCore.elf", 0, 0 }, { 69, 53, "SceRedisServer", 0, 0 },
                               { 87, 69, "elfldr.elf", 0, 0 }, { 102, 87, "websrv.elf", 0, 0 } };
    f.kstuff = PW_KSTUFF_ABSENT;
    f.shadowmount_version[0] = 0;
    pw_payload_check(few, 4, SELF, &f, &r);
    assert(!r.pass && r.ok[PW_PAYLOAD_ELFLDR] && r.ok[PW_PAYLOAD_WEBSRV] && !r.ok[PW_PAYLOAD_KSTUFF]);
    advise(&r, &f, advice);
    /* NanoDNS is optional: it is not in the blocking "Load ..." list, only its own warning. */
    assert(strstr(advice, "Load Kstuff Lite 1.11 Beta or later, ShadowMountPlus (beta11 or newer), then press CROSS"));
    assert(!strstr(advice, "NanoDNS, then press CROSS") && strstr(advice, "NanoDNS was not detected"));
    assert(!strstr(advice, "Restart"));

    /* A missing NanoDNS alone does not block: the full set minus NanoDNS still passes, with only a warning. */
    PwPayloadProcess no_dns[BASE];
    unsigned m = 0;
    for (unsigned i = 0; i < BASE; ++i)
        if (strcmp(base[i].name, "nanodns.elf")) no_dns[m++] = base[i];
    f = good();
    pw_payload_check(no_dns, m, SELF, &f, &r);
    assert(r.pass && !r.running[PW_PAYLOAD_NANODNS] && !r.ok[PW_PAYLOAD_NANODNS]);
    advise(&r, &f, advice);
    assert(strstr(advice, "NanoDNS was not detected") && !strstr(advice, "Load") && !strstr(advice, "Restart"));

    /* Too old a Kstuff: restart, as nothing can be unloaded; ShadowMountPlus's old version blocks nothing */
    f = good();
    snprintf(f.shadowmount_version, sizeof(f.shadowmount_version), "1.7beta4");
    f.kstuff = PW_KSTUFF_OLD;
    pw_payload_check(base, BASE, SELF, &f, &r);
    assert(!r.pass && r.ok[PW_PAYLOAD_SHADOWMOUNT] && !r.ok[PW_PAYLOAD_KSTUFF]);
    advise(&r, &f, advice);
    assert(strstr(advice, "older than Kstuff Lite 1.11") && !strstr(advice, "ShadowMountPlus") && strstr(advice, "Restart the PS5"));
    assert(!strstr(advice, "Load"));

    /* kstuff's process there but no kekcall answers. */
    f = good();
    f.kstuff = PW_KSTUFF_ABSENT;
    pw_payload_check(base, BASE, SELF, &f, &r);
    advise(&r, &f, advice);
    assert(!r.pass && strstr(advice, "does not answer"));

    /* A tester's of 2026-10-29: the access daemon stopped (no dev.conf), so the title had no root and listed itself
     * alone ("processes=1 kstuff=absent shadowmountplus=1.7beta10 websrv=v0.34") while every payload ran. Nothing may be
     * called missing: what answered on the loopback shows running, and the advice names the access problem. */
    PwPayloadProcess alone[] = { { SELF, 53, "eboot.bin", MIB(300), 0 } };
    f = good();
    f.kstuff = PW_KSTUFF_ABSENT;
    f.data_denied = 1;
    pw_payload_check(alone, 1, SELF, &f, &r);
    assert(!r.pass && !r.listed && !r.others);
    assert(r.running[PW_PAYLOAD_SHADOWMOUNT] && r.ok[PW_PAYLOAD_SHADOWMOUNT] && r.running[PW_PAYLOAD_WEBSRV] && r.ok[PW_PAYLOAD_WEBSRV]);
    assert(!r.running[PW_PAYLOAD_ELFLDR] && !r.running[PW_PAYLOAD_KSTUFF] && !r.running[PW_PAYLOAD_NANODNS]);
    advise(&r, &f, advice);
    assert(strstr(advice, "could not get access to /data") && strstr(advice, "DEV_SERVER=127.0.0.1"));
    assert(!strstr(advice, "Load") && !strstr(advice, "older") && !strstr(advice, "other payload"));
    /* The same with a title that has /data but still saw nothing: the general words, still nothing called missing */
    f.data_denied = 0;
    pw_payload_check(alone, 1, SELF, &f, &r);
    advise(&r, &f, advice);
    assert(!r.pass && strstr(advice, "could not see") && !strstr(advice, "Load"));

    /* Nothing listed fails, never passes blind. */
    f = good();
    f.listed = 0;
    pw_payload_check(NULL, 0, SELF, &f, &r);
    assert(!r.pass);
    advise(&r, &f, advice);
    assert(strstr(advice, "could not see"));

    /* The way through a failing check: offered only on a fail; TRIANGLE held 5 s, a let-go starts over; the log line
     * names what failed. dt 0.125 adds up exactly. */
    f = good();
    pw_payload_check(base, BASE, SELF, &f, &r);
    assert(r.pass && !pw_payload_skip_offered(&r));   /* a pass offers nothing */
    f.kstuff = PW_KSTUFF_ABSENT;
    PwPayloadProcess plus[BASE + 1];
    memcpy(plus, base, sizeof(base));
    plus[BASE] = (PwPayloadProcess){ 130, 87, "extra.elf", MIB(6), 0 };
    pw_payload_check(plus, BASE + 1, SELF, &f, &r);
    assert(!r.pass && pw_payload_skip_offered(&r) && r.others == 1);
    float held = 0.0f;
    for (int i = 0; i < 39; ++i) assert(!pw_payload_skip_hold(&held, 1, 0.125f));   /* 4.875 s: not yet */
    assert(!pw_payload_skip_hold(&held, 0, 0.125f) && held == 0.0f);                 /* let go: starts over */
    for (int i = 0; i < 39; ++i) assert(!pw_payload_skip_hold(&held, 1, 0.125f));
    assert(pw_payload_skip_hold(&held, 1, 0.125f) && held == 5.0f);                  /* the 40th frame: 5 s */
    char line[512];
    pw_payload_skip_line(&r, line, sizeof(line));
    printf("  skip: %s\n", line);
    char expected[512];
    snprintf(expected, sizeof(expected), "skipped by the player after 5 s hold; missing: %s; extra: extra.elf",
             pw_payload_label(PW_PAYLOAD_KSTUFF));
    assert(!strcmp(line, expected));   /* the optional NanoDNS is never called missing */
    /* nothing listed: nothing called missing; a short buffer stays terminated */
    f = good();
    pw_payload_check(alone, 1, SELF, &f, &r);
    pw_payload_skip_line(&r, line, sizeof(line));
    assert(strstr(line, "missing: unknown (the processes could not be seen); extra: none"));
    assert(pw_payload_skip_line(&r, line, 16) == 15 && strlen(line) == 15);

    puts("test_pw_payload_check: ok");
    /* The banner: the recommended payloads (about 90 MiB) say nothing; 200 MiB does (the owner, 2026-11-01) */
    assert(!pw_payloads_notice(64) && !pw_payloads_notice(90) && !pw_payloads_notice(199));
    assert(pw_payloads_notice(200) && pw_payloads_notice(512));
    return 0;
}
