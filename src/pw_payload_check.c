/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The payload checklist (pw_payload_check.h). */
#include "pw_payload_check.h"
#include "pw_memory_helper.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const known_names[PW_PAYLOAD_KNOWN] = {
    "elfldr", "kstuff", "shadowmountplus", "websrv", "nanodns", "klogsrv", "ftpsrv", "pldmgr"
};
static const char *const labels[PW_PAYLOAD_KNOWN] = {
    "elfldr", "Kstuff Lite 1.11 Beta or later", "ShadowMountPlus (beta11 or newer)", "websrv", "NanoDNS (optional)",
    "klog", "ftpsrv", "pldmgr"
};
/* MihawkOS's own: the Lapy daemon, the helpers' loader and helpers (in Sony's carrier, eboot.bin), the tiles' worker,
 * the test harness's control payload, and the PC tools' read-only samplers (tools/console-*.py). */
static const char *const ours[] = { "lapy-root-daemon", "proton-payload-child", "child", "worker", "ps5vkctl", "eboot.bin",
                                    "console-pagetables", "console-memory", "console-processes" };
/* Allowed and not shown (the owner, 2026-10-31): a payload the owner runs that games do not mind. */
static const char *const allowed[] = { "qafutkn_util_mk2" };

/* name without a trailing ".elf" equals want; the kernel keeps 19 characters of a process name, so a long one ends
 * in ".el", ".e" or "." (qafutkn_util_mk2.el, 2026-10-31) */
static int named(const char *name, const char *want)
{
    const size_t n = strlen(want);
    return !strncmp(name, want, n) && !strncmp(name + n, ".elf", strlen(name + n));
}

const char *pw_payload_label(int which)
{
    return which >= 0 && which < PW_PAYLOAD_KNOWN ? labels[which] : "";
}

void pw_payload_check(const PwPayloadProcess *list, unsigned count, int self, const PwPayloadFacts *facts,
                      PwPayloadReport *out)
{
    memset(out, 0, sizeof(*out));
    int syscore = 0, websrv = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (!strncmp(list[i].name, "SceSysCore", 10)) syscore = list[i].pid;
        if (named(list[i].name, "websrv")) websrv = list[i].pid;
    }
    /* A tester's title without root listed itself alone ("processes=1") and called every payload missing (2026-10-29) */
    out->listed = facts->listed && syscore > 0;
    if (!out->listed) {
        out->running[PW_PAYLOAD_SHADOWMOUNT] = facts->shadowmount_version[0] != 0;
        out->running[PW_PAYLOAD_WEBSRV] = facts->websrv_version[0] != 0;
    }
    for (unsigned i = 0; out->listed && i < count; ++i) {
        const PwPayloadProcess *p = &list[i];
        if (p->pid == self || p->ppid == self || p->pid == syscore || !pw_memory_is_payload(p->ppid, syscore, p->name))
            continue;
        int mine = 0;
        for (size_t k = 0; k < sizeof(ours) / sizeof(ours[0]); ++k) mine |= named(p->name, ours[k]);
        for (size_t k = 0; k < sizeof(allowed) / sizeof(allowed[0]); ++k) mine |= named(p->name, allowed[k]);
        /* "payload" is what the kernel calls the Lapy daemon and, on 13.60, NanoDNS loaded through websrv: the daemon
         * is the websrv child that started when a title started it (pw_payload_check.h), any other is unnamed */
        const int generic = named(p->name, "payload");
        const int64_t apart = p->started - facts->daemon_started;
        mine |= generic && websrv > 0 && p->ppid == websrv && facts->daemon_started > 0 && p->started > 0 &&
                apart <= PW_PAYLOAD_DAEMON_SLACK_S && apart >= -PW_PAYLOAD_DAEMON_SLACK_S;
        if (mine) continue;
        out->payload_resident += p->resident;
        if (generic) {
            out->unnamed++;
            out->unnamed_resident += p->resident;
            continue;
        }
        int which = -1;
        for (int k = 0; k < PW_PAYLOAD_KNOWN && which < 0; ++k)
            if (named(p->name, known_names[k])) which = k;
        if (which >= 0) {
            out->running[which]++;
            out->resident[which] += p->resident;
            continue;
        }
        if (out->others < PW_PAYLOAD_OTHERS) {
            snprintf(out->other_names[out->others], sizeof(out->other_names[0]), "%s", p->name);
            out->other_each[out->others] = p->resident;
        }
        out->others++;
        out->other_resident += p->resident;
    }
    out->ok[PW_PAYLOAD_ELFLDR] = out->running[PW_PAYLOAD_ELFLDR] > 0;
    /* kstuff lives in the kernel once loaded: its kekcalls are the proof, its process only a sign */
    out->ok[PW_PAYLOAD_KSTUFF] = facts->kstuff == PW_KSTUFF_OK;
    /* Loaded is enough (the owner, 2026-10-31: testers' newer ShadowMountPlus was not detected by its version); the
     * label still asks for beta11 or newer */
    out->ok[PW_PAYLOAD_SHADOWMOUNT] = out->running[PW_PAYLOAD_SHADOWMOUNT] > 0;
    out->ok[PW_PAYLOAD_WEBSRV] = out->running[PW_PAYLOAD_WEBSRV] > 0;
    out->ok[PW_PAYLOAD_NANODNS] = out->running[PW_PAYLOAD_NANODNS] > 0;
    out->pass = out->listed && !out->others;
    /* NanoDNS is optional (the owner, 2026-10-31): a missing one warns but never blocks. On 13.60 it can run under the
     * generic "payload" name through websrv, which the daemon shortcut above counts as ours, so it is easily misdetected. */
    for (int k = 0; k < PW_PAYLOAD_REQUIRED; ++k) if (k != PW_PAYLOAD_NANODNS) out->pass &= out->ok[k];
}

static void add(char *out, size_t size, size_t *used, const char *text)
{
    const int n = snprintf(out + *used, *used < size ? size - *used : 0, "%s%s", *used ? " " : "", text);
    if (n > 0) *used += (size_t)n;
}

size_t pw_payload_advice(const PwPayloadReport *r, const PwPayloadFacts *facts, char *out, size_t size)
{
    char line[400];
    size_t used = 0;
    if (size) out[0] = 0;
    if (!r->listed) {
        if (facts->data_denied)
            add(out, size, &used, "MihawkOS could not get access to /data, so it cannot check your payloads. Check that "
                "/data/homebrew/dev.conf holds the two lines DEV_SERVER=127.0.0.1 and DEV_PORT=8080, then restart MihawkOS.");
        else
            add(out, size, &used, "MihawkOS could not see which payloads are running. Restart the PS5, load the payloads "
                "listed here, then open MihawkOS again.");
        return used < size ? used : size - 1;
    }
    int missing = 0, restart = r->others > 0;
    for (int k = 0; k < PW_PAYLOAD_REQUIRED; ++k) missing += k != PW_PAYLOAD_NANODNS && !r->ok[k] && !r->running[k];
    if (missing) {
        int n = snprintf(line, sizeof(line), "Load");
        for (int k = 0, said = 0; k < PW_PAYLOAD_REQUIRED; ++k)
            if (k != PW_PAYLOAD_NANODNS && !r->ok[k] && !r->running[k] && !(k == PW_PAYLOAD_KSTUFF && facts->kstuff == PW_KSTUFF_OLD))
                n += snprintf(line + n, sizeof(line) - (size_t)n, "%s %s", said++ ? "," : "", labels[k]);
        if (n > 4) {
            snprintf(line + n, sizeof(line) - (size_t)n, ", then press CROSS to check again.");
            add(out, size, &used, line);
        }
    }
    /* NanoDNS is optional: warn, never block (the owner, 2026-10-31). */
    if (!r->ok[PW_PAYLOAD_NANODNS] && !r->running[PW_PAYLOAD_NANODNS])
        add(out, size, &used, "NanoDNS was not detected. It is optional: MihawkOS still runs, but some online features "
            "may not work.");
    /* Seen only as "payload": said as it is, never guessed (the owner, 2026-10-31) */
    if (r->unnamed) {
        snprintf(line, sizeof(line), "%u payload%s %s only as \"payload\" (%llu MiB), so MihawkOS cannot tell which %s. "
                 "On firmware 13.60 NanoDNS loaded through websrv shows like this, which is fine; if you loaded "
                 "anything else, restart the PS5 and load only the payloads listed here.", r->unnamed,
                 r->unnamed == 1 ? "" : "s", r->unnamed == 1 ? "shows" : "show",
                 (unsigned long long)((r->unnamed_resident + (1u << 19)) >> 20), r->unnamed == 1 ? "it is" : "they are");
        add(out, size, &used, line);
    }
    if (facts->kstuff == PW_KSTUFF_OLD) {
        add(out, size, &used, "Your Kstuff is older than Kstuff Lite 1.11 Beta.");
        restart = 1;
    } else if (facts->kstuff == PW_KSTUFF_ABSENT && r->running[PW_PAYLOAD_KSTUFF]) {
        add(out, size, &used, "Kstuff is running but does not answer.");
        restart = 1;
    }
    if (r->others) {
        int n = snprintf(line, sizeof(line), "%u other payload%s %s running, using %llu MiB that games need:", r->others,
                         r->others == 1 ? "" : "s", r->others == 1 ? "is" : "are",
                         (unsigned long long)((r->other_resident + (1u << 19)) >> 20));
        for (unsigned k = 0; k < r->others && k < PW_PAYLOAD_OTHERS; ++k)
            n += snprintf(line + n, sizeof(line) - (size_t)n, "%s %s", k ? "," : "", r->other_names[k]);
        snprintf(line + n, sizeof(line) - (size_t)n, "%s.", r->others > PW_PAYLOAD_OTHERS ? " and more" : "");
        add(out, size, &used, line);
    }
    if (restart)
        add(out, size, &used, "Restart the PS5, then load only the payloads listed here: a restart is the sure way to "
            "close or replace a payload.");
    return used < size ? used : size - 1;
}

int pw_payload_skip_offered(const PwPayloadReport *report)
{
    return report && !report->pass;
}

int pw_payload_skip_hold(float *seconds, int held, float dt)
{
    *seconds = held ? *seconds + dt : 0.0f;
    return *seconds >= PW_PAYLOAD_SKIP_SECONDS;
}

static void skip_add(char *out, size_t size, size_t *used, const char *text)
{
    const int n = snprintf(out + *used, *used < size ? size - *used : 0, "%s", text);
    if (n > 0) *used += (size_t)n;
    if (*used >= size) *used = size ? size - 1 : 0;
}

size_t pw_payload_skip_line(const PwPayloadReport *report, char *out, size_t size)
{
    char part[64];
    size_t used = 0;
    if (size) out[0] = '\0';
    snprintf(part, sizeof(part), "skipped by the player after %.0f s hold; missing: ", (double)PW_PAYLOAD_SKIP_SECONDS);
    skip_add(out, size, &used, part);
    int listed = 0;
    if (!report->listed) {   /* nothing could be seen: nothing is called missing (as on the page) */
        skip_add(out, size, &used, "unknown (the processes could not be seen)");
        listed = 1;
    }
    for (int k = 0; report->listed && k < PW_PAYLOAD_REQUIRED; ++k) {
        if (k == PW_PAYLOAD_NANODNS || report->ok[k]) continue;   /* NanoDNS is optional */
        if (listed++) skip_add(out, size, &used, ", ");
        skip_add(out, size, &used, pw_payload_label(k));
    }
    if (!listed) skip_add(out, size, &used, "none");
    skip_add(out, size, &used, "; extra: ");
    const unsigned named = report->others < PW_PAYLOAD_OTHERS ? report->others : PW_PAYLOAD_OTHERS;
    for (unsigned k = 0; k < named; ++k) {
        if (k) skip_add(out, size, &used, ", ");
        skip_add(out, size, &used, report->other_names[k]);
    }
    if (report->others > named) {
        snprintf(part, sizeof(part), "%s%u more", named ? ", " : "", report->others - named);
        skip_add(out, size, &used, part);
    }
    if (report->unnamed) {
        snprintf(part, sizeof(part), "%s%u unknown (\"payload\")", report->others ? ", " : "", report->unnamed);
        skip_add(out, size, &used, part);
    }
    if (!report->others && !report->unnamed) skip_add(out, size, &used, "none");
    return used;
}
