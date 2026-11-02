/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pw_update_notice.h"
#include <stdio.h>

static unsigned percent(uint64_t done, uint64_t total)
{
    return total ? (unsigned)((done >= total ? total : done) * 100 / total) : 0;
}

size_t pw_update_notice_text(const PwSetupLanguage *language, const PwUpdateNotice *notice, char *out, size_t size)
{
    char mb_done[24], mb_total[24], share[8];
    const char *version = notice->version ? notice->version : "";
    snprintf(mb_done, sizeof(mb_done), "%llu", (unsigned long long)(notice->done >> 20));
    snprintf(mb_total, sizeof(mb_total), "%llu", (unsigned long long)(notice->total >> 20));
    snprintf(share, sizeof(share), "%u", percent(notice->done, notice->total));
    const char *args[4] = {version, mb_done, mb_total, share};
    PwSetupText key;
    switch (notice->phase) {
    case PW_UPDATE_NOTICE_CHECKING: key = PW_TEXT_UPDATE_CHECKING; break;
    case PW_UPDATE_NOTICE_AVAILABLE: key = PW_TEXT_UPDATE_AVAILABLE; break;
    case PW_UPDATE_NOTICE_DOWNLOADING:
        key = notice->total ? PW_TEXT_UPDATE_DOWNLOADING : PW_TEXT_UPDATE_DOWNLOADING_SO_FAR;
        break;
    case PW_UPDATE_NOTICE_VERIFYING: key = PW_TEXT_UPDATE_VERIFYING; break;
    case PW_UPDATE_NOTICE_UNPACKING: key = PW_TEXT_UPDATE_UNPACKING, args[1] = share; break;
    case PW_UPDATE_NOTICE_READY: key = PW_TEXT_UPDATE_READY; break;
    case PW_UPDATE_NOTICE_FAILED:
        switch (notice->failure) {
        case PW_UPDATE_FAIL_NETWORK: key = PW_TEXT_UPDATE_FAIL_NETWORK; break;
        case PW_UPDATE_FAIL_DOWNLOAD: key = PW_TEXT_UPDATE_FAIL_DOWNLOAD; break;
        case PW_UPDATE_FAIL_STORAGE: key = PW_TEXT_UPDATE_FAIL_STORAGE; break;
        case PW_UPDATE_FAIL_SIGNATURE: key = PW_TEXT_UPDATE_FAIL_SIGNATURE; break;
        case PW_UPDATE_FAIL_PREPARE: key = PW_TEXT_UPDATE_FAIL_PREPARE; break;
        default: key = PW_TEXT_COUNT; break;
        }
        break;
    default: key = PW_TEXT_COUNT; break;
    }
    if (key == PW_TEXT_COUNT) {
        if (size) *out = '\0';
        return 0;
    }
    return pw_setup_fill(out, size, pw_setup_text(language, key), args, 4);
}
