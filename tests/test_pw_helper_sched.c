/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "../src/pw_helper_sched.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* An environment block of the given entries: each NUL-terminated, then an empty one. */
static size_t block_of(char *out, size_t size, const char *const *entries)
{
    size_t used = 0;
    for (; *entries; ++entries) {
        const size_t length = strlen(*entries) + 1;
        assert(used + length < size);
        memcpy(out + used, *entries, length);
        used += length;
    }
    out[used++] = 0;
    return used;
}

static PwHelperSched read_of(const char *const *entries)
{
    char block[512];
    PwHelperSched settings;
    pw_helper_sched_read(block, block_of(block, sizeof(block), entries), &settings);
    return settings;
}

static void read_cases(void)
{
    PwHelperSched s;
    pw_helper_sched_read(NULL, 0, &s);
    /* Unset: the measured defaults (2026-10-16; record removed from the tree), the loader's main thread above the program's. */
    assert(s.round_robin == 1 && s.service == 700 && s.program == 767 && !s.cpus && !s.census && !s.error);
    s = read_of((const char *const[]){"DXVK_HUD=1", "WINE_PS5_HELPER_SCHEDULE=keep", NULL});
    assert(s.round_robin == 1 && !s.error);
    s = read_of((const char *const[]){"WINE_PS5_HELPER_SCHED=keep", NULL});
    assert(!s.round_robin && !s.error);
    s = read_of((const char *const[]){"WINE_PS5_HELPER_SCHED=bogus", NULL});
    assert(s.round_robin == 1 && s.error == EINVAL);
    s = read_of((const char *const[]){"WINE_PS5_HELPER_PRIORITY=700,705", NULL});
    assert(s.service == 700 && s.program == 705 && !s.error);
    s = read_of((const char *const[]){"WINE_PS5_HELPER_PRIORITY=256,767", NULL});
    assert(s.service == 256 && s.program == 767 && !s.error);
    static const char *const refused[] = {"705,700", "100,900", "700", "700,7x0", "255,700", "700,768", ",700", "700,",
                                          "00700,701", "-1,700", ""};
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        char entry[64];
        snprintf(entry, sizeof(entry), "WINE_PS5_HELPER_PRIORITY=%s", refused[i]);
        s = read_of((const char *const[]){entry, NULL});
        assert(s.service == PW_HELPER_SCHED_SERVICE_DEFAULT && s.program == PW_HELPER_SCHED_PROGRAM_DEFAULT &&
               s.error == EINVAL);
    }
    s = read_of((const char *const[]){"WINE_PS5_HELPER_CPUS=0x1fff", NULL});
    assert(s.cpus == 0x1fff && !s.error);
    s = read_of((const char *const[]){"WINE_PS5_HELPER_CPUS=E000", NULL});
    assert(s.cpus == 0xe000 && !s.error);
    static const char *const masks[] = {"0", "0x", "0x0", "1fffz", "0x10000000000000000", ""};
    for (size_t i = 0; i < sizeof(masks) / sizeof(masks[0]); ++i) {
        char entry[64];
        snprintf(entry, sizeof(entry), "WINE_PS5_HELPER_CPUS=%s", masks[i]);
        s = read_of((const char *const[]){entry, NULL});
        assert(!s.cpus && s.error == EINVAL);
    }
    s = read_of((const char *const[]){"WINE_PS5_SCHED_CENSUS=1", NULL});
    assert(s.census == 1 && !s.error);
    s = read_of((const char *const[]){"WINE_PS5_SCHED_CENSUS=5", NULL});
    assert(!s.census && s.error == EINVAL);
    /* One refused value keeps the others. */
    s = read_of((const char *const[]){"WINE_PS5_HELPER_SCHED=rr", "WINE_PS5_HELPER_CPUS=nope", "WINE_PS5_HELPER_PRIORITY=700,701",
                                      NULL});
    assert(s.round_robin && s.service == 700 && s.program == 701 && !s.cpus && s.error == EINVAL);
    /* An entry not terminated within bytes is never read past them: the block sits at the end of its allocation, so
     * an over-read is an AddressSanitizer error. */
    static const char unterminated[] = "WINE_PS5_HELPER_SCHED=keep";
    char *tail = malloc(sizeof(unterminated) - 1);
    assert(tail);
    memcpy(tail, unterminated, sizeof(unterminated) - 1);
    pw_helper_sched_read(tail, sizeof(unterminated) - 1, &s);
    assert(s.round_robin == 1 && !s.error);
    free(tail);
    /* The entries after an empty one are not read. */
    static const char after_end[] = "A=1\0\0WINE_PS5_HELPER_SCHED=keep\0";
    pw_helper_sched_read(after_end, sizeof(after_end), &s);
    assert(s.round_robin == 1);
}

static void plan_cases(void)
{
    PwHelperSched s = {.round_robin = 1};
    PwHelperSchedPlan plan;
    /* keep: nothing changes, whatever else is set. */
    PwHelperSched keep = {.round_robin = 0, .service = 700, .program = 705, .cpus = 0x1fff};
    assert(!pw_helper_sched_plan(&keep, PW_HELPER_SCHED_PROGRAM, 1, 1, 700, 0xffff, &plan) && !plan.change);
    assert(!pw_helper_sched_plan(&keep, PW_HELPER_SCHED_SERVICE, 1, 1, 700, 0xffff, &plan) && !plan.change);
    /* rr, FIFO 700 at entry and for a plain thread: the program's threads RR at 700, the loader's left. */
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 700, 0xffff, &plan));
    assert(plan.change && plan.policy == PW_HELPER_SCHED_RR && plan.priority == 700 && !plan.cpus);
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_SERVICE, 1, 1, 700, 0xffff, &plan) && !plan.change);
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 720, 0xffff, &plan) && plan.priority == 720);
    /* S,P: both tiers. */
    s.service = 700, s.program = 705;
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_SERVICE, 1, 1, 700, 0xffff, &plan));
    assert(plan.change && plan.policy == PW_HELPER_SCHED_RR && plan.priority == 700);
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 700, 0xffff, &plan));
    assert(plan.change && plan.policy == PW_HELPER_SCHED_RR && plan.priority == 705);
    /* An unmeasured class is left alone. */
    assert(pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 2, 700, 0xffff, &plan) == EINVAL && !plan.change);
    assert(pw_helper_sched_plan(&s, PW_HELPER_SCHED_SERVICE, 2, 1, 700, 0xffff, &plan) == EINVAL && !plan.change);
    s.program = 0;
    assert(pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 100, 0xffff, &plan) == EINVAL && !plan.change);
    assert(pw_helper_sched_plan(&s, 7, 1, 1, 700, 0xffff, &plan) == EINVAL && !plan.change);
    /* CPUs: inside the helper's own set only. */
    s.cpus = 0x1fff;
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 700, 0xffff, &plan) && plan.cpus == 0x1fff);
    s.cpus = 0x10000;
    assert(pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 700, 0xffff, &plan) == EINVAL);
    assert(plan.change && plan.policy == PW_HELPER_SCHED_RR && plan.priority == 700 && !plan.cpus);
    s.cpus = 0x3;
    assert(pw_helper_sched_plan(&s, PW_HELPER_SCHED_PROGRAM, 1, 1, 700, 0, &plan) == EINVAL && plan.change && !plan.cpus);
    assert(!pw_helper_sched_plan(&s, PW_HELPER_SCHED_SERVICE, 1, 1, 700, 0, &plan) && !plan.cpus);
}

static void census_cases(void)
{
    const int websrv = 96, elfldr = 88;
    assert(pw_helper_sched_wanted("klogsrv.elf", 97, elfldr, websrv, elfldr));
    assert(pw_helper_sched_wanted("shadowmountplus.elf", 101, elfldr, websrv, elfldr));
    assert(pw_helper_sched_wanted("SceSpZeroConf", 113, websrv, websrv, elfldr));
    assert(pw_helper_sched_wanted("payload", 107, websrv, websrv, elfldr));
    assert(pw_helper_sched_wanted("websrv.elf", websrv, 1, websrv, elfldr));
    assert(pw_helper_sched_wanted("elfldr.elf", elfldr, 70, websrv, -1) == 0);
    assert(pw_helper_sched_wanted("elfldr.elf", elfldr, 70, websrv, elfldr));
    assert(pw_helper_sched_wanted("eboot.bin", 109, 55, websrv, elfldr));
    assert(pw_helper_sched_wanted("SceShellCore", 58, 55, websrv, elfldr));
    assert(pw_helper_sched_wanted("SceShellUI", 59, 55, websrv, elfldr));
    assert(pw_helper_sched_wanted("SceSysCore.elf", 55, 1, websrv, elfldr));
    assert(!pw_helper_sched_wanted("SceSvmd", 30, 1, websrv, elfldr));
    assert(!pw_helper_sched_wanted("SceSpZeroConf", 67, 55, websrv, elfldr));
    assert(!pw_helper_sched_wanted("kernel", 0, 0, -1, -1));

    PwHelperSchedGroup groups[2];
    unsigned count = 0;
    assert(!pw_helper_sched_group(groups, 2, &count, 10, 700, 0x1fff));
    assert(!pw_helper_sched_group(groups, 2, &count, 10, 700, 0x1fff));
    assert(!pw_helper_sched_group(groups, 2, &count, 2, 700, 0x1fff));
    assert(pw_helper_sched_group(groups, 2, &count, 0, ESRCH, 0) == -1);
    assert(!pw_helper_sched_group(groups, 2, &count, 2, 700, 0x1fff));
    assert(count == 2 && groups[0].threads == 2 && groups[1].type == 2 && groups[1].threads == 2);
}

/* The title's game: Windows priorities above normal step up from P, never to the title's own 700. */
static void windows_priority_cases(void)
{
    assert(pw_helper_sched_windows_priority(8, 767) == 767);    /* normal */
    assert(pw_helper_sched_windows_priority(1, 767) == 767);    /* idle stays at the floor, not lower */
    assert(pw_helper_sched_windows_priority(6, 767) == 767);    /* lowest */
    assert(pw_helper_sched_windows_priority(9, 767) == 766);    /* above normal */
    assert(pw_helper_sched_windows_priority(10, 767) == 765);   /* highest */
    assert(pw_helper_sched_windows_priority(15, 767) == 762);   /* time critical */
    assert(pw_helper_sched_windows_priority(31, 767) == 761);   /* realtime class */
    for (int e = 0; e <= 31; ++e) {
        const int p = pw_helper_sched_windows_priority(e, 767);
        assert(p > PW_HELPER_SCHED_SERVICE_DEFAULT && p <= 767);
        assert(e == 0 || p <= pw_helper_sched_windows_priority(e - 1, 767));   /* never lower for a higher priority */
        assert(pw_helper_sched_windows_priority(e, 703) >= 701);               /* a raised P still stays below 700 */
    }
    assert(pw_helper_sched_windows_priority(15, 0) == 762);     /* an unset or wild P is the default */
    assert(pw_helper_sched_windows_priority(15, 700) == 762 && pw_helper_sched_windows_priority(15, 900) == 762);
}

int main(void)
{
    windows_priority_cases();
    read_cases();
    plan_cases();
    census_cases();
    puts("helper scheduling: settings, plans and the census's choices passed");
    return 0;
}
