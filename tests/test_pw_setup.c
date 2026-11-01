/* SPDX-License-Identifier: GPL-3.0-or-later */
/* The guided setup's rules (src/pw_setup.h): when it shows and where it goes, complete text in characters the
 * launcher's fonts carry, the same placeholders in every language, and the record. */
#include "../src/pw_setup.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* What the kit's baker draws with its "european" set (ps5/ui/setup-kit.sh bakes the launcher's faces with it). */
static int baked(uint32_t c)
{
    static const uint32_t more[] = {0x0401, 0x0451, 0x2018, 0x2019, 0x201C, 0x201D, 0x201E, 0x20AC, 0x2122, 0x00B7,
                                    0x00D7, 0x00A9, 0x00B0, 0x2013, 0x2014, 0x2022, 0x2026, 0x2190, 0x2191, 0x2192,
                                    0x2193, 0x2713};
    if ((c >= 0x20 && c < 0x7f) || (c >= 0xA1 && c <= 0x17F) || (c >= 0x410 && c <= 0x44F)) return 1;
    for (size_t i = 0; i < sizeof(more) / sizeof(more[0]); ++i)
        if (c == more[i]) return 1;
    return 0;
}

/* A language with its own face (Chinese, Japanese) has that face baked from its strings at build time; the build fails
 * when the face lacks one (ps5/tools/generate_assets.py). Its text may use Han, kana and full-width forms. */
static int own_face(uint32_t c)
{
    return (c >= 0x3000 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xFF00 && c <= 0xFFEF);
}

static void check_drawable(const char *tag, const char *text, int face)
{
    for (const unsigned char *p = (const unsigned char *)text; *p;) {
        uint32_t c = *p++;
        int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
        c &= extra == 3 ? 0x07 : extra == 2 ? 0x0F : extra == 1 ? 0x1F : 0x7F;
        while (extra--) {
            assert((*p & 0xC0) == 0x80);
            c = (c << 6) | (*p++ & 0x3F);
        }
        if (!baked(c) && !(face && own_face(c))) {
            fprintf(stderr, "%s: U+%04X is not in the baked fonts: %s\n", tag, c, text);
            abort();
        }
    }
}

static unsigned placeholders(const char *text)
{
    unsigned mask = 0;
    for (const char *p = text; *p; ++p)
        if (p[0] == '{' && p[1] >= '1' && p[1] <= '9' && p[2] == '}') mask |= 1u << (p[1] - '1');
    return mask;
}

int main(int argc, char **argv)
{
    assert(argc == 2 || argc == 6); /* the repository, for the flags; then a picture, the output, a stage and a tick */
    /* When it shows, and where it starts. */
    PwSetupFacts facts = {.data = 1};
    assert(pw_setup_start(&facts) == PW_SETUP_WELCOME);
    facts = (PwSetupFacts){.data = 0, .asked = 1};
    assert(pw_setup_start(&facts) == PW_SETUP_NONE);
    facts = (PwSetupFacts){.data = 1, .done = 1, .runtime = 1, .prefix = 1};
    assert(pw_setup_start(&facts) == PW_SETUP_NONE);
    facts.asked = 1;
    assert(pw_setup_start(&facts) == PW_SETUP_WELCOME);
    /* A set-up console whose runtime is not this title's (the title folder was replaced by a newer one): the copy
     * alone, then the library (the owner, 2026-10-31: replace the folder and open MihawkOS) */
    facts = (PwSetupFacts){.data = 1, .done = 1, .prefix = 1};
    assert(pw_setup_start(&facts) == PW_SETUP_INSTALLING && pw_setup_next(PW_SETUP_INSTALLING, &facts) == PW_SETUP_NONE);
    facts = (PwSetupFacts){.data = 1, .done = 1, .runtime = 1};
    assert(pw_setup_start(&facts) == PW_SETUP_PREPARING);
    facts = (PwSetupFacts){.data = 1, .returning = 1, .runtime = 1};
    assert(pw_setup_start(&facts) == PW_SETUP_PREPARING);
    facts.prefix = 1;
    assert(pw_setup_start(&facts) == PW_SETUP_LIMITS);

    /* The way through: a first launch, then a re-run with everything installed. How to install, the USB drive and
     * FTP are gone (the runtime is in the title folder, the owner, 2026-10-31): the install starts after the language,
     * and the steps that stay keep their numbers (tools/run-launcher.py's walk expects 0, 1, 4 and 5). */
    assert(PW_SETUP_LANGUAGE == 1 && PW_SETUP_INSTALLING == 4 && PW_SETUP_PREPARING == 5 && PW_SETUP_DONE == 6);
    facts = (PwSetupFacts){.data = 1};
    const PwSetupStep first[] = {PW_SETUP_WELCOME, PW_SETUP_LANGUAGE, PW_SETUP_INSTALLING, PW_SETUP_PREPARING,
                                 PW_SETUP_LIMITS, PW_SETUP_DONE, PW_SETUP_NONE};
    for (size_t i = 0; i + 1 < sizeof(first) / sizeof(first[0]); ++i) assert(pw_setup_next(first[i], &facts) == first[i + 1]);
    facts = (PwSetupFacts){.data = 1, .runtime = 1, .prefix = 1};
    assert(pw_setup_next(PW_SETUP_LANGUAGE, &facts) == PW_SETUP_LIMITS);   /* a re-run passes the limits page too */
    facts.prefix = 0;
    assert(pw_setup_next(PW_SETUP_LANGUAGE, &facts) == PW_SETUP_PREPARING);
    /* tools/run-setup-unattended.py's second launch: the runtime installed, the tour not finished. It starts at Welcome,
     * and the language goes on to Preparing, where Start is: one press a step, three in all. */
    facts = (PwSetupFacts){.data = 1, .runtime = 1};
    const PwSetupStep resumed[] = {PW_SETUP_WELCOME, PW_SETUP_LANGUAGE, PW_SETUP_PREPARING};
    assert(pw_setup_start(&facts) == resumed[0]);
    for (size_t i = 0; i + 1 < sizeof(resumed) / sizeof(resumed[0]); ++i)
        assert(pw_setup_next(resumed[i], &facts) == resumed[i + 1]);
    assert(pw_setup_back(PW_SETUP_LANGUAGE) == PW_SETUP_WELCOME);
    /* The limits page: numbered 8, no way back from it, and only a held cross goes on (the owner, 2026-10-31).
     * A hold under PW_SETUP_HOLD_SECONDS does not; letting go starts it over; an unbroken one does. */
    assert(PW_SETUP_LIMITS == 8 && pw_setup_back(PW_SETUP_LIMITS) == PW_SETUP_NONE);
    float hold = 0.0f;
    for (int frame = 0; frame < 9 * 120; ++frame) hold = pw_setup_hold(hold, 1, 1.0f / 120.0f);
    assert(hold > 8.9f && hold < PW_SETUP_HOLD_SECONDS);
    hold = pw_setup_hold(hold, 0, 1.0f / 120.0f);
    assert(hold == 0.0f);
    for (int frame = 0; frame < 5 * 120; ++frame) hold = pw_setup_hold(hold, 1, 1.0f / 120.0f);
    assert(hold < PW_SETUP_HOLD_SECONDS);   /* 9 s, let go, 5 s: not 14 */
    for (int frame = 0; frame < 5 * 120 + 2; ++frame) hold = pw_setup_hold(hold, 1, 1.0f / 120.0f);
    assert(hold >= PW_SETUP_HOLD_SECONDS);
    /* A re-run from Settings cannot be left either (the owner, 2026-10-31): asked, with everything installed, it goes
     * Welcome, Language, Before you play, Done, and Back from any of its steps stays in the
     * tour. The launcher clears done as it starts a re-run (examples/ps5proton/ps5proton.cpp), so a title closed on the
     * way starts the tour again at Welcome (the record's half is below). */
    facts = (PwSetupFacts){.data = 1, .done = 1, .runtime = 1, .prefix = 1, .asked = 1};
    const PwSetupStep rerun[] = {PW_SETUP_WELCOME, PW_SETUP_LANGUAGE, PW_SETUP_LIMITS, PW_SETUP_DONE};
    assert(pw_setup_start(&facts) == rerun[0]);
    for (size_t i = 0; i < sizeof(rerun) / sizeof(rerun[0]); ++i) {
        if (i + 1 < sizeof(rerun) / sizeof(rerun[0])) assert(pw_setup_next(rerun[i], &facts) == rerun[i + 1]);
        const PwSetupStep back = pw_setup_back(rerun[i]);
        assert(back == PW_SETUP_NONE || (back >= PW_SETUP_WELCOME && back < PW_SETUP_STEPS));
    }
    facts = (PwSetupFacts){.data = 1, .runtime = 1, .prefix = 1};   /* closed during the re-run: done cleared */
    assert(pw_setup_start(&facts) == PW_SETUP_WELCOME);
    /* The install's page speaks of the title folder alone: no drive, USB stick or FTP folder in any of its words */
    static const PwSetupText install_page[] = {
        PW_TEXT_RETRY, PW_TEXT_ERR_NO_PIECES, PW_TEXT_ERR_PARTIAL, PW_TEXT_ERR_NO_SPACE, PW_TEXT_ERR_READ,
        PW_TEXT_ERR_DAMAGED, PW_TEXT_ERR_FULL, PW_TEXT_ERR_WRITE, PW_TEXT_ERR_CANCELLED, PW_TEXT_ERR_BUILD,
        PW_TEXT_INSTALLING_TITLE, PW_TEXT_INSTALLING_BODY, PW_TEXT_INSTALLING_CHECKING, PW_TEXT_INSTALLING_STATS,
        PW_TEXT_INSTALLED, PW_TEXT_STOP};
    for (size_t i = 0; i < sizeof(install_page) / sizeof(install_page[0]); ++i) {
        const char *words = pw_setup_text(NULL, install_page[i]);
        if (strstr(words, "drive") || strstr(words, "USB") || strstr(words, "plug") || strstr(words, "/install")) {
            fprintf(stderr, "the install page shows key %d: %s\n", (int)install_page[i], words);
            abort();
        }
    }
    assert(pw_setup_back(PW_SETUP_PREPARING) == PW_SETUP_LANGUAGE);
    assert(pw_setup_back(PW_SETUP_WELCOME) == PW_SETUP_NONE && pw_setup_back(PW_SETUP_INSTALLING) == PW_SETUP_NONE &&
           pw_setup_back(PW_SETUP_DONE) == PW_SETUP_NONE);

    /* Languages: listed once their text is complete; every string drawable; the same placeholders as English. */
    size_t count = 0, shown = 0;
    const PwSetupLanguage *all = pw_setup_languages(&count);
    const PwSetupLanguage *english = pw_setup_default_language();
    /* The owner's ten, in the owner's order (2026-10-18; Polish added 2026-10-26) */
    assert(count == 10 && !strcmp(english->tag, "en-US") && english->text && !strcmp(all[9].tag, "pl-PL"));
    for (size_t i = 0; i < count; ++i) {
        assert(all[i].tag && all[i].name && all[i].flag && all[i].locale);
        if (all[i].font) assert(!strcmp(all[i].font, "noto-sans-sc") || !strcmp(all[i].font, "noto-sans-jp"));
        char flag[1024];
        snprintf(flag, sizeof(flag), "%s/ps5/assets/flags/%s.svg", argv[1], all[i].flag);
        if (access(flag, R_OK)) {
            fprintf(stderr, "%s: no %s\n", all[i].tag, flag);
            abort();
        }
        for (size_t j = 0; j < i; ++j) assert(strcmp(all[i].tag, all[j].tag));
        if (!all[i].text) {
            assert(!pw_setup_language(all[i].tag));
            continue;
        }
        ++shown;
        assert(pw_setup_language(all[i].tag) == &all[i]);
        check_drawable(all[i].tag, all[i].name, all[i].font != NULL);
        for (int key = 0; key < PW_TEXT_COUNT; ++key) {
            const char *text = all[i].text[key];
            if (!text || !*text) {
                fprintf(stderr, "%s: no text for key %d\n", all[i].tag, key);
                abort();
            }
            check_drawable(all[i].tag, text, all[i].font != NULL);
            assert(placeholders(text) == placeholders(english->text[key]));
        }
    }
    assert(shown == 10 && pw_setup_language("ja-JP") && !pw_setup_language("en-GB") && !pw_setup_language(NULL));
    assert(!strcmp(pw_setup_text(NULL, PW_TEXT_CONTINUE), "Continue"));
    assert(!strcmp(pw_setup_text(pw_setup_language("fr-FR"), PW_TEXT_CONTINUE), "Continuer"));

    /* Filling placeholders. */
    char out[64];
    const char *args[] = {"41", "52"};
    assert(pw_setup_fill(out, sizeof(out), "has {1} of {2} pieces", args, 2) == strlen("has 41 of 52 pieces"));
    assert(!strcmp(out, "has 41 of 52 pieces"));
    pw_setup_fill(out, sizeof(out), "{1} {2} {3}", args, 2);
    assert(!strcmp(out, "41 52 {3}"));
    assert(pw_setup_fill(out, 6, "has {1} of {2}", args, 2) == 5 && !strcmp(out, "has 4"));
    pw_setup_gigabytes(english, 1189920691ull, out, sizeof(out));
    assert(!strcmp(out, "1.19 GB"));
    pw_setup_gigabytes(pw_setup_language("fr-FR"), 3000000000ull, out, sizeof(out));
    assert(!strcmp(out, "3,00 Go"));

    /* The record. */
    char folder[] = "/tmp/proton-setup-XXXXXX", path[256];
    assert(mkdtemp(folder));
    snprintf(path, sizeof(path), "%s/setup.txt", folder);
    PwSetupRecord record = {.language = "x", .done = 1};
    assert(pw_setup_record_load(&record, path) == ENOENT && !record.done && !record.language[0]);
    record = (PwSetupRecord){.language = "fr-FR", .done = 1};
    assert(!pw_setup_record_save(&record, path));
    PwSetupRecord read = {0};
    assert(!pw_setup_record_load(&read, path) && read.done && !strcmp(read.language, "fr-FR"));
    FILE *file = fopen(path, "w");
    fputs("something else\r\nlanguage=en-GB\r\ndone=0\nlanguage=a-tag-far-too-long-to-keep\n", file);
    fclose(file);
    assert(!pw_setup_record_load(&read, path) && !read.done && !strcmp(read.language, "en-GB"));
    record = (PwSetupRecord){.done = 1};
    assert(!pw_setup_record_save(&record, path) && !pw_setup_record_load(&read, path) && read.done && !read.language[0]);
    /* A re-run clears done and keeps the language: the next start is the tour again */
    record = (PwSetupRecord){.language = "ja-JP", .done = 1};
    assert(!pw_setup_record_save(&record, path) && !pw_setup_record_load(&read, path) && read.done);
    read.done = 0;
    assert(!pw_setup_record_save(&read, path) && !pw_setup_record_load(&record, path) && !record.done &&
           !strcmp(record.language, "ja-JP"));
    char tmp[300];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    assert(access(tmp, F_OK) && !unlink(path) && !rmdir(folder));
    char stamps[][32] = {"1734871457 wow64\r\n", "1734871457 wow64\n", "1734871457 wow64", "1734871457\r\n",
                         "1734871457 wow64x\r\n", "wow64", ""};
    const int made[] = {1, 1, 1, 0, 0, 0, 0};
    for (size_t i = 0; i < sizeof(made) / sizeof(made[0]); ++i) assert(pw_setup_prefix_made(stamps[i]) == made[i]);
    assert(!strcmp(stamps[0], "1734871457 wow64"));
    /* The cover's stages, from first-install-unattended-2026-10-19-04's processes in their order. */
    static const struct { const char *command_line; PwSetupBuild stage; } processes[] = {
        {"\"C:\\windows\\system32\\wineboot.exe\" --init", PW_SETUP_BUILD_SETTINGS},
        {NULL, PW_SETUP_BUILD_SETTINGS},
        {"C:\\windows\\system32\\rundll32.exe setupapi,InstallHinfSection DefaultInstall 128 wine.inf", PW_SETUP_BUILD_SETTINGS},
        {"C:\\windows\\system32\\control.exe appwiz.cpl install_mono", PW_SETUP_BUILD_DOTNET},
        {"msiexec /i C:\\windows\\mono\\mono-2.0\\support\\\\winemono-support.msi", PW_SETUP_BUILD_DOTNET},
        {"\"C:\\windows\\mono\\mono-2.0\\support\\\\installinf-x86_64.exe\" \"dotnetfakedlls.inf\"", PW_SETUP_BUILD_DOTNET},
        {"\"C:\\windows\\system32\\iexplore.exe\" /RegServer", PW_SETUP_BUILD_FINISHING},
        {"C:\\windows\\syswow64\\rundll32.exe setupapi,InstallHinfSection Wow64Install 128 wine.inf", PW_SETUP_BUILD_FINISHING},
        {"C:\\windows\\mono\\mono-2.0\\support\\removeuserinstalls-x86.exe", PW_SETUP_BUILD_FINISHING},
    };
    PwSetupBuild stage = PW_SETUP_BUILD_SETTINGS;
    for (size_t i = 0; i < sizeof(processes) / sizeof(processes[0]); ++i)
        assert((stage = pw_setup_build_next(stage, processes[i].command_line)) == processes[i].stage);
    assert(pw_setup_build_next(PW_SETUP_BUILD_READY, "install_mono") == PW_SETUP_BUILD_READY);

    /* The cover's marks, over a flat card: only their square is written, each stage shows as it should, and drawing again
     * puts back what the last frame drew. */
    const size_t pixels = (size_t)PW_SETUP_COVER_WIDTH * PW_SETUP_COVER_HEIGHT;
    unsigned *picture = malloc(pixels * sizeof(unsigned)), *frame = malloc(pixels * sizeof(unsigned)),
             *before = malloc(pixels * sizeof(unsigned));
    assert(picture && frame && before);
    for (size_t i = 0; i < pixels; ++i) picture[i] = 0xffd4d8dcu;
    memcpy(frame, picture, pixels * sizeof(unsigned));
#define AT(f, x, y) (f)[(size_t)(y) * PW_SETUP_COVER_WIDTH + (size_t)(x)]
    const int mx = PW_SETUP_COVER_MARK_X, row[3] = {PW_SETUP_COVER_ROW_Y, PW_SETUP_COVER_ROW_Y + PW_SETUP_COVER_ROW_STEP,
                                                  PW_SETUP_COVER_ROW_Y + 2 * PW_SETUP_COVER_ROW_STEP};
    pw_setup_cover_draw(frame, picture, PW_SETUP_BUILD_READY, 0);
    pw_setup_cover_draw(frame, picture, PW_SETUP_BUILD_DOTNET, 3);
    size_t outside = 0;
    for (int y = 0; y < PW_SETUP_COVER_HEIGHT; ++y)
        for (int x = 0; x < PW_SETUP_COVER_WIDTH; ++x)
            if ((x < mx - 32 || x >= mx + 32 || y < row[0] - 32 || y >= row[2] + 32) && AT(frame, x, y) != AT(picture, x, y))
                ++outside;
    assert(!outside);
    assert(AT(frame, mx, row[0] - 14) == 0xff32a56du);                      /* done: green */
    assert(AT(frame, mx + 1, row[0] + 4) == 0xffffffffu);                   /* with a white check */
    assert(AT(frame, mx, row[1]) == AT(picture, mx, row[1]));              /* current: a ring of dots, open inside */
    assert(AT(frame, mx + 10, row[1] + 10) != AT(picture, mx, row[1]));    /* the head, at tick 3: lower right */
    assert(AT(frame, mx, row[2]) == AT(picture, mx, row[2]));              /* to come: a grey ring, open inside */
    assert(AT(frame, mx + 20, row[2]) != AT(picture, mx, row[2]) && AT(frame, mx - 20, row[2]) == AT(frame, mx + 20, row[2]));
    memcpy(before, frame, pixels * sizeof(unsigned));
    pw_setup_cover_draw(frame, picture, PW_SETUP_BUILD_DOTNET, 4);
    size_t moved = 0;
    for (int y = 0; y < PW_SETUP_COVER_HEIGHT; ++y)
        for (int x = 0; x < PW_SETUP_COVER_WIDTH; ++x)
            if (AT(frame, x, y) != AT(before, x, y)) {
                assert(y > row[1] - 32 && y < row[1] + 32); /* only the turning mark moves */
                ++moved;
            }
    assert(moved);
    pw_setup_cover_draw(frame, picture, PW_SETUP_BUILD_READY, 0);
    for (int i = 0; i < 3; ++i) assert(AT(frame, mx, row[i] - 14) == 0xff32a56du);
    /* With a picture and a stage, the marks over that picture (P6, 1920x1080) as the console draws them: the PC's
     * picture of the cover (tools/test-setup-ui.py). */
    if (argc == 6) {
        FILE *in = fopen(argv[2], "rb");
        unsigned width = 0, height = 0, depth = 0;
        assert(in && fscanf(in, "P6 %u %u %u", &width, &height, &depth) == 3 && fgetc(in) != EOF);
        assert(width == PW_SETUP_COVER_WIDTH && height == PW_SETUP_COVER_HEIGHT && depth == 255);
        for (size_t i = 0; i < pixels; ++i) {
            unsigned char rgb[3];
            assert(fread(rgb, 1, 3, in) == 3);
            picture[i] = 0xff000000u | (unsigned)rgb[0] << 16 | (unsigned)rgb[1] << 8 | rgb[2];
        }
        fclose(in);
        pw_setup_cover_draw(frame, picture, (PwSetupBuild)atoi(argv[4]), (unsigned)atoi(argv[5]));
        for (int y = 0; y < PW_SETUP_COVER_HEIGHT; ++y)
            for (int x = 0; x < PW_SETUP_COVER_WIDTH; ++x)
                if (x < mx - 32 || x >= mx + 32 || y < row[0] - 32 || y >= row[2] + 32) AT(frame, x, y) = AT(picture, x, y);
        FILE *out = fopen(argv[3], "wb");
        assert(out);
        fprintf(out, "P6\n%d %d\n255\n", PW_SETUP_COVER_WIDTH, PW_SETUP_COVER_HEIGHT);
        for (size_t i = 0; i < pixels; ++i) {
            const unsigned char rgb[3] = {(unsigned char)(frame[i] >> 16), (unsigned char)(frame[i] >> 8), (unsigned char)frame[i]};
            fwrite(rgb, 1, 3, out);
        }
        assert(!fclose(out));
    }
#undef AT
    /* The memory guard's notice says what ran out: the page tables are no full memory (a Chromium game's processes ended
     * one at 1,193 MiB, 2026-10-31), and each case has its own text in every language. */
    assert(pw_setup_guard_notice(0, 0, 1) == PW_TEXT_NOTICE_MEMORY_ASKED && pw_setup_guard_notice(1, 0, 1) == PW_TEXT_NOTICE_TABLES_ASKED);
    assert(pw_setup_guard_notice(0, 1, 1) == PW_TEXT_NOTICE_MEMORY_ENDED && pw_setup_guard_notice(1, 1, 1) == PW_TEXT_NOTICE_TABLES_ENDED);
    assert(pw_setup_guard_notice(0, 1, 0) == PW_TEXT_NOTICE_MEMORY_STUCK && pw_setup_guard_notice(1, 1, 0) == PW_TEXT_NOTICE_TABLES_STUCK);
    {
        const PwSetupLanguage *english = pw_setup_language("en-US");
        assert(english && strstr(pw_setup_text(english, PW_TEXT_NOTICE_TABLES_ENDED), "page tables") &&
               !strstr(pw_setup_text(english, PW_TEXT_NOTICE_TABLES_ENDED), "memory stayed full"));
        assert(strstr(pw_setup_text(english, PW_TEXT_NOTICE_MEMORY_ENDED), "memory stayed full"));
        size_t planned = 0;
        const PwSetupLanguage *every = pw_setup_languages(&planned);
        for (size_t i = 0; i < planned; ++i) {
            if (!pw_setup_language(every[i].tag)) continue;   /* planned, no text yet */
            for (int key = PW_TEXT_NOTICE_MEMORY_ASKED; key <= PW_TEXT_NOTICE_TABLES_STUCK; ++key)
                for (int other = key + 1; other <= PW_TEXT_NOTICE_TABLES_STUCK; ++other)
                    assert(strcmp(pw_setup_text(&every[i], (PwSetupText)key), pw_setup_text(&every[i], (PwSetupText)other)));
        }
    }
    free(picture), free(frame), free(before);
    printf("setup: start and steps, %zu of %zu languages with complete drawable text, placeholders, record, prefix stamp, "
           "cover stages\n", shown, count);
    return 0;
}
