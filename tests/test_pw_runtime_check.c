/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The runtime check (src/pw_runtime_check.h) and what the guided setup makes of it (src/pw_setup.h), with the tester's
 * case of 2026-10-29: Alpha 4's title, an older build's runtime and helper, the desktop started from a home-screen
 * tile. That runtime is another build's, so no Wine start may go on, and the setup's next start is its install step. */
#include "../src/pw_runtime_check.h"
#include "../src/pw_setup.h"
#include <assert.h>
#include <stdio.h>

static const char alpha4[] = "44256bd6cbad237311f3817f37c9a8c0278441807691abad1d1a704751527e99\n";
static const char older[] = "cdfbcfc68c75eeda3175244722618b34978e8a295a7e4f4c7b4d86d85b5f9ba8\n";

/* The facts the launcher gathers (examples/ps5proton/setup_tour.hpp): the runtime counts as installed only as this
 * build's. */
static PwSetupFacts facts(PwRuntimeState runtime)
{
    PwSetupFacts f = {0};
    f.data = 1; f.done = 1; f.prefix = 1;
    f.runtime = runtime == PW_RUNTIME_THIS_BUILD;
    return f;
}

int main(void)
{
    /* The stamps: equal (white space aside), other, missing, unpinned. */
    assert(pw_runtime_state(1, alpha4, alpha4) == PW_RUNTIME_THIS_BUILD);
    assert(pw_runtime_state(1, "  44256bd6cbad237311f3817f37c9a8c0278441807691abad1d1a704751527e99", alpha4)
           == PW_RUNTIME_THIS_BUILD);
    assert(pw_runtime_state(1, older, alpha4) == PW_RUNTIME_OTHER_BUILD);
    assert(pw_runtime_state(1, NULL, alpha4) == PW_RUNTIME_OTHER_BUILD);     /* an install cut short: stamp removed */
    assert(pw_runtime_state(1, "", alpha4) == PW_RUNTIME_OTHER_BUILD);
    assert(pw_runtime_state(1, "44256bd6", alpha4) == PW_RUNTIME_OTHER_BUILD);
    assert(pw_runtime_state(0, alpha4, alpha4) == PW_RUNTIME_MISSING);
    assert(pw_runtime_state(0, NULL, NULL) == PW_RUNTIME_MISSING);
    assert(pw_runtime_state(1, older, NULL) == PW_RUNTIME_THIS_BUILD);       /* a development title pins nothing */
    assert(pw_runtime_state(1, older, " \n") == PW_RUNTIME_THIS_BUILD);

    /* The tester's case: Alpha 4's title (its pin), the older runtime, a finished setup, the desktop from a tile. */
    const PwRuntimeState tester = pw_runtime_state(1, older, alpha4);
    assert(tester != PW_RUNTIME_THIS_BUILD);                 /* proton_run_wine refuses: no helper starts */
    PwSetupFacts f = facts(tester);
    assert(pw_setup_start(&f) == PW_SETUP_INSTALLING);       /* the launcher opens on the install, from its own folder */
    assert(pw_setup_next(PW_SETUP_INSTALLING, &f) == PW_SETUP_NONE);   /* then the library: the setup was done */
    /* Once this build's runtime is in place, nothing is asked again. */
    f = facts(pw_runtime_state(1, alpha4, alpha4));
    assert(pw_setup_start(&f) == PW_SETUP_NONE);
    /* A missing runtime goes the same way as before. */
    f = facts(pw_runtime_state(0, NULL, alpha4));
    assert(pw_setup_start(&f) == PW_SETUP_INSTALLING);

    printf("runtime check passed: this build, another build, missing, unpinned; the tester's case lands on the install\n");
    return 0;
}
