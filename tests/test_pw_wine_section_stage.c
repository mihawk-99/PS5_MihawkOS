/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The words a failed shared-view fault logs for its stage (wine/ps5/pw_wine_section.h, Wine patch 2429). */
#include <assert.h>
#include <string.h>
#include "../wine/ps5/pw_wine_section.h"

int main(void)
{
    assert(!strcmp(pw_wine_section_stage_word(0), "none"));
    assert(!strcmp(pw_wine_section_stage_word(5), "the kernel refused the mapping"));
    assert(!strcmp(pw_wine_section_stage_word(8), "the title's answer failed"));
    assert(!strcmp(pw_wine_section_stage_word(9), "a mapping that is not Wine's lies in the range"));
    assert(!strcmp(pw_wine_section_stage_word(10), "unknown"));
    assert(!strcmp(pw_wine_section_stage_word(~0u), "unknown"));
    return 0;
}
