/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A claim of the title (wine/ps5/pw_title_claim.h): the title's answer decides, whatever the busy callback leaves in errno. */
#include "../wine/ps5/pw_title_claim.h"
#include <assert.h>
#include <stdio.h>

static int calls;
/* As the title's busy did for a program with no Steam app id: its lookup fails, errno is ENOENT, and it says no. */
static int not_steam(const void *request) { (void)request; ++calls; errno = ENOENT; return 0; }
static int play_request(const void *request) { (void)request; ++calls; errno = ENOENT; return 1; }

int main(void)
{
    /* taken: the callback is not asked */
    calls = 0;
    assert(pw_title_claim(0, 0, not_steam, NULL) == PW_TITLE_CLAIM_TITLE && !calls);
    /* the title's place is taken: a helper, though the callback clobbered errno (launcher-render's second program) */
    errno = EBUSY;
    assert(pw_title_claim(-1, EBUSY, not_steam, NULL) == PW_TITLE_CLAIM_HELPER && calls == 1 && errno == ENOENT);
    /* a Steam play request: refused */
    assert(pw_title_claim(-1, EBUSY, play_request, NULL) == PW_TITLE_CLAIM_REFUSED);
    /* no callback (an older title): a helper */
    assert(pw_title_claim(-1, EBUSY, NULL, NULL) == PW_TITLE_CLAIM_HELPER);
    /* another failure: failed, the callback not asked */
    calls = 0;
    assert(pw_title_claim(-1, ENOMEM, play_request, NULL) == PW_TITLE_CLAIM_FAILED && !calls);
    puts("test_pw_title_claim: ok");
    return 0;
}
