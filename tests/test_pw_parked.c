/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The catalog's parked profiles (src/pw_parked.h): a catalog holding the parked Aurelia entry lists everything but it. */
#include "../src/pw_parked.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    const char *catalog[] = {"mihawk-os", "aurelia", "store-client", "a-farming-game", "aurelia-tools", "Aurelia"};
    unsigned listed = 0, aurelia = 0;
    for (unsigned i = 0; i < sizeof(catalog) / sizeof(catalog[0]); i++) {
        if (pw_parked_profile(catalog[i])) continue;
        listed++;
        aurelia += !strcmp(catalog[i], "aurelia");
    }
    /* Only the exact id is parked: a game a person named otherwise is theirs and stays listed. */
    assert(listed == 5 && aurelia == 0);
    assert(pw_parked_profile("aurelia") && !pw_parked_profile(NULL) && !pw_parked_profile(""));
    puts("pw_parked: ok");
    return 0;
}
