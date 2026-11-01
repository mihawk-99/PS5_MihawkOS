/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The hang breaker's decision and the SIGKILL ending (src/pw_hang_breaker.h). */
#include "pw_hang_breaker.h"
#include <errno.h>
#include <signal.h>
#include <string.h>

int pw_hang_breaker_decide(const PwHangSample *sample, PwHangVerdict *verdict)
{
    if (!sample || !verdict) return EINVAL;
    const uint64_t stall = sample->stall_ms && sample->stall_ms < PW_HANG_STALL_MS ? sample->stall_ms : PW_HANG_STALL_MS;
    *verdict = sample->census_known && sample->present_age_ms >= stall && sample->census_age_ms >= stall
        ? PW_HANG_STALLED : PW_HANG_FINE;
    return 0;
}

int pw_hang_breaker_title_decide(const PwHangSample *sample, int shown, PwHangVerdict *verdict)
{
    if (!sample || !verdict) return EINVAL;
    const uint64_t stall = sample->stall_ms && sample->stall_ms < PW_HANG_TITLE_STALL_MS ? sample->stall_ms : PW_HANG_TITLE_STALL_MS;
    *verdict = shown && sample->present_age_ms >= stall ? PW_HANG_STALLED : PW_HANG_FINE;
    return 0;
}

int pw_hang_breaker_wedged(unsigned samples, uint64_t span_ms)
{
    return samples >= PW_HANG_WEDGE_S && span_ms >= (PW_HANG_WEDGE_S - 1) * 1000ull;
}

int pw_hang_breaker_end(const int *pids, unsigned count, int self, PwHangSignal signal, uint64_t now_ms, PwHangEnd *end)
{
    if (!signal || !end || (!pids && count) || count > PW_HANG_MAX_HELPERS) return EINVAL;
    memset(end, 0, sizeof(*end));
    end->count = count;
    end->sent_ms = now_ms;
    for (unsigned i = 0; i < count; ++i) {
        end->pids[i] = pids[i];
        if (pids[i] <= 1 || pids[i] == self) {
            end->sent[i] = PW_HANG_SKIPPED;
            continue;
        }
        end->sent[i] = signal(pids[i], SIGKILL);
        if (end->sent[i] != ESRCH) end->waiting |= 1u << i;
    }
    return 0;
}

unsigned pw_hang_breaker_gone(PwHangEnd *end, PwHangSignal signal, uint64_t now_ms)
{
    if (!end || !signal) return 0;
    for (unsigned i = 0; i < end->count; ++i) {
        if (!(end->waiting & (1u << i))) continue;
        end->last[i] = signal(end->pids[i], 0);
        if (end->last[i] != ESRCH) continue;
        end->waiting &= ~(1u << i);
        end->gone_ms[i] = now_ms - end->sent_ms;
    }
    return (unsigned)__builtin_popcount(end->waiting);
}
