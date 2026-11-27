/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Game resolution setting (src/pw_resolution.h): parsing, names, dimensions, atomic persistence. */
#include "../src/pw_resolution.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    assert(pw_resolution_kind("1080p") == PW_RESOLUTION_1080P);
    assert(pw_resolution_kind("1920x1080") == PW_RESOLUTION_1080P);
    assert(pw_resolution_kind("1440p") == PW_RESOLUTION_1440P);
    assert(pw_resolution_kind("2560x1440") == PW_RESOLUTION_1440P);
    assert(pw_resolution_kind("4k") == PW_RESOLUTION_4K);
    assert(pw_resolution_kind("4K") == PW_RESOLUTION_4K);
    assert(pw_resolution_kind("2160p") == PW_RESOLUTION_4K);
    assert(pw_resolution_kind("3840x2160") == PW_RESOLUTION_4K);
    assert(pw_resolution_kind(NULL) == -1 && pw_resolution_kind("") == -1 && pw_resolution_kind("720p") == -1);

    unsigned w = 0, h = 0;
    pw_resolution_dimensions(PW_RESOLUTION_1080P, &w, &h);
    assert(w == 1920 && h == 1080 && !strcmp(pw_resolution_desktop(PW_RESOLUTION_1080P), "1920x1080"));
    pw_resolution_dimensions(PW_RESOLUTION_1440P, &w, &h);
    assert(w == 2560 && h == 1440 && !strcmp(pw_resolution_desktop(PW_RESOLUTION_1440P), "2560x1440"));
    pw_resolution_dimensions(PW_RESOLUTION_4K, &w, &h);
    assert(w == 3840 && h == 2160 && !strcmp(pw_resolution_desktop(PW_RESOLUTION_4K), "3840x2160"));
    assert(!strcmp(pw_resolution_badge(PW_RESOLUTION_4K), "4K"));
    assert(!strcmp(pw_resolution_word(99), "1080p"));

    char dir[] = "/tmp/pw-resolution-XXXXXX";
    assert(mkdtemp(dir));
    char path[256];
    snprintf(path, sizeof(path), "%s/resolution.txt", dir);

    int kind = PW_RESOLUTION_4K;
    assert(pw_resolution_load(&kind, path) == ENOENT && kind == PW_RESOLUTION_1080P);
    assert(pw_resolution_save(-1, path) == EINVAL);
    assert(pw_resolution_save(PW_RESOLUTION_KINDS, path) == EINVAL);

    for (int k = 0; k < PW_RESOLUTION_KINDS; ++k) {
        assert(pw_resolution_save(k, path) == 0);
        kind = -1;
        assert(pw_resolution_load(&kind, path) == 0 && kind == k);
    }

    FILE *f = fopen(path, "w");
    assert(f);
    fputs("resolution 8k\n", f);
    fclose(f);
    kind = PW_RESOLUTION_4K;
    assert(pw_resolution_load(&kind, path) == EINVAL && kind == PW_RESOLUTION_1080P);

    f = fopen(path, "w");
    assert(f);
    fputs("resolution 1440p extra\n", f);
    fclose(f);
    assert(pw_resolution_load(&kind, path) == EINVAL && kind == PW_RESOLUTION_1080P);

    unlink(path);
    rmdir(dir);
    puts("test_pw_resolution: ok");
    return 0;
}
