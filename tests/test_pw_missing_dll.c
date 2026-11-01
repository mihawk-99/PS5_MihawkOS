/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Why a program in the title could not start (src/pw_missing_dll.h), from Wine's lines in the form a tester's demo printed them. */
#include "../src/pw_missing_dll.h"
#include <assert.h>
#include <stdio.h>

static int feed(PwMissingDll *state, const char *line) { return pw_missing_dll_line(state, line, strlen(line)); }

int main(void)
{
    PwMissingDll state = {0};
    /* A LoadLibrary that fails, which the program survives: nothing to say. */
    assert(!feed(&state, "175.1:0388:038c:err:module:import_dll Library xinput9.dll (which is needed by L\"C:\\\\windows\\\\x.dll\") not found\n"));
    assert(!state.reason[0]);
    assert(!feed(&state, "175.2:0388:038c:trace:loaddll:build_module Loaded L\"C:\\\\a.dll\" at 0: native\n"));
    /* The program's own import, then the loader's failure: the reason names both. */
    assert(!feed(&state, "175.592:0388:038c:err:module:import_dll Library upscaler_loader_dx12.dll (which is needed by "
                         "L\"C:\\\\Games\\\\Demo\\\\demo.exe\") not found\n"));
    assert(feed(&state, "175.807:0388:038c:err:module:loader_init Importing dlls for L\"C:\\\\Games\\\\"
                        "Demo\\\\demo.exe\" failed, status c0000135\n"));
    assert(!strcmp(state.reason, "demo.exe could not start: upscaler_loader_dx12.dll was not found"));
    /* Another loader failure (not a missing DLL) names nothing new. */
    PwMissingDll other = {0};
    assert(!feed(&other, "1.0:0020:0024:err:module:loader_init Importing dlls for L\"C:\\\\g.exe\" failed, status c0000142\n"));
    assert(!other.reason[0]);
    /* Both lines in one chunk, as one write may carry them. */
    PwMissingDll both = {0};
    assert(feed(&both, "x:err:module:import_dll Library pw-missing-import.dll (which is needed by L\"C:\\\\g\\\\game64.exe\") not found\n"
                       "y:err:module:loader_init Importing dlls for L\"C:\\\\g\\\\game64.exe\" failed, status c0000135\n"));
    assert(!strcmp(both.reason, "game64.exe could not start: pw-missing-import.dll was not found"));
    puts("pw_missing_dll: ok");
    return 0;
}
