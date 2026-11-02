/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The main menu's update line (src/pw_update_notice.h): every step and failure in every language, filled. */
#include "../src/pw_update_notice.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const char *line(const PwSetupLanguage *language, PwUpdateNoticePhase phase, uint64_t done, uint64_t total,
                        PwUpdateFailure failure)
{
    static char out[512];
    const PwUpdateNotice notice = {phase, "01.001.000", done, total, failure};
    pw_update_notice_text(language, &notice, out, sizeof(out));
    assert(!strchr(out, '{'));   /* every placeholder filled */
    return out;
}

int main(void)
{
    size_t count = 0, checked = 0;
    const PwSetupLanguage *all = pw_setup_languages(&count);
    for (size_t i = 0; i < count; ++i) {
        if (!all[i].text) continue;
        const PwSetupLanguage *language = &all[i];
        /* 120 of 530 MB, 22 percent; without a size, the MB so far; the unpack's percent */
        const char *text = line(language, PW_UPDATE_NOTICE_DOWNLOADING, 120ull << 20, 530ull << 20, PW_UPDATE_FAIL_NONE);
        assert(strstr(text, "01.001.000") && strstr(text, "120") && strstr(text, "530") && strstr(text, "22"));
        text = line(language, PW_UPDATE_NOTICE_DOWNLOADING, 120ull << 20, 0, PW_UPDATE_FAIL_NONE);
        assert(strstr(text, "120") && !strstr(text, "530") && !strstr(text, "%"));
        text = line(language, PW_UPDATE_NOTICE_UNPACKING, 3, 4, PW_UPDATE_FAIL_NONE);
        assert(strstr(text, "01.001.000") && strstr(text, "75"));
        assert(strstr(line(language, PW_UPDATE_NOTICE_READY, 0, 0, PW_UPDATE_FAIL_NONE), "01.001.000"));
        assert(strstr(line(language, PW_UPDATE_NOTICE_VERIFYING, 9, 9, PW_UPDATE_FAIL_NONE), "01.001.000"));
        assert(strstr(line(language, PW_UPDATE_NOTICE_AVAILABLE, 0, 0, PW_UPDATE_FAIL_NONE), "01.001.000"));
        assert(*line(language, PW_UPDATE_NOTICE_CHECKING, 0, 0, PW_UPDATE_FAIL_NONE));
        /* each failure says something of its own; none and a failure with no kind say nothing */
        char seen[5][512];
        for (int kind = PW_UPDATE_FAIL_NETWORK; kind <= PW_UPDATE_FAIL_PREPARE; ++kind) {
            text = line(language, PW_UPDATE_NOTICE_FAILED, 0, 0, (PwUpdateFailure)kind);
            assert(*text);
            for (int other = PW_UPDATE_FAIL_NETWORK; other < kind; ++other) assert(strcmp(seen[other - 1], text));
            snprintf(seen[kind - 1], sizeof(seen[0]), "%s", text);
        }
        assert(!*line(language, PW_UPDATE_NOTICE_FAILED, 0, 0, PW_UPDATE_FAIL_NONE));
        assert(!*line(language, PW_UPDATE_NOTICE_NONE, 0, 0, PW_UPDATE_FAIL_NONE));
        ++checked;
    }
    assert(checked == 10);
    /* a done past the total reads 100, not more */
    assert(strstr(line(pw_setup_default_language(), PW_UPDATE_NOTICE_UNPACKING, 9, 4, PW_UPDATE_FAIL_NONE), "100%"));
    printf("pw_update_notice: every step and failure in %zu languages\n", checked);
    return 0;
}
