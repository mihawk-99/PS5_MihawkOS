/* SPDX-License-Identifier: GPL-3.0-or-later */
/* src/pw_umip.h: what each UMIP-refused instruction would have done, per encoding, in both modes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "../src/pw_ia32_decode.h"
#include "../src/pw_umip.h"

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9 };

typedef struct Case {
    unsigned mode;
    const char *hex;
    int result;
    const char *mnemonic;
    unsigned length;
    int to_register;
    unsigned reg;          /* register: which, and its value after */
    uint64_t value;
    uint64_t address;      /* memory: where, and the bytes */
    const char *data;
} Case;

static const uint8_t GDT64[] = { 0, 0, 0x00, 0x00, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff };

static const Case cases[] = {
    /* Long mode. The registers: rax 0x1000, rcx 3, rsp 0x7fff0000, rbx 0x1234, r8 0x5000, r9 0x1111111111111111, rest ~0. */
    { 64, "0f010424", PW_OK, "sgdt", 4, 0, 0, 0, 0x7fff0000, "0000" "0000feffffffffff" },
    { 64, "480f010c24", PW_OK, "sidt", 5, 0, 0, 0, 0x7fff0000, "0000" "0000ffffffffffff" },
    { 64, "0f010510000000", PW_OK, "sgdt", 7, 0, 0, 0, 0x400000 + 7 + 0x10, "0000" "0000feffffffffff" },
    { 64, "410f01448810", PW_OK, "sgdt", 6, 0, 0, 0, 0x5000 + 3 * 4 + 0x10, "0000" "0000feffffffffff" },
    { 64, "670f010424", PW_OK, "sgdt", 5, 0, 0, 0, 0x7fff0000, "0000" "0000feffffffffff" },
    { 64, "0f01e0", PW_OK, "smsw", 3, 1, RAX, 0x80050033, 0, NULL },
    { 64, "660f01e0", PW_OK, "smsw", 4, 1, RAX, 0x0033, 0, NULL },   /* a 16-bit write keeps rax's upper bits (0) */
    { 64, "480f01e0", PW_OK, "smsw", 4, 1, RAX, 0x80050033, 0, NULL },
    { 64, "0f0123", PW_OK, "smsw", 3, 0, 0, 0, 0x1234, "3300" },
    { 64, "4c0f00c0", PW_OK, "sldt", 4, 1, RAX, 0, 0, NULL },
    { 64, "0f00c8", PW_OK, "str", 3, 1, RAX, 0x40, 0, NULL },
    { 64, "66410f00c9", PW_OK, "str", 5, 1, R9, 0x1111111111110040ull, 0, NULL },
    { 64, "0f0008", PW_OK, "str", 3, 0, 0, 0, 0x1000, "4000" },
    { 64, "0f0000", PW_OK, "sldt", 3, 0, 0, 0, 0x1000, "0000" },
    /* Not emulated: the fault goes on as before. */
    { 64, "640f010424", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },   /* FS override */
    { 64, "650f0100", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },     /* GS override */
    { 64, "f00f010424", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },   /* LOCK */
    { 64, "0f01d0", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },       /* XGETBV */
    { 64, "0f0110", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },       /* LGDT */
    { 64, "0f01c1", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },       /* VMCALL: /0 with a register is not SGDT */
    { 64, "0f0020", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },       /* VERR */
    { 64, "0f01", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },         /* truncated */
    { 64, "90", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },
    /* 32-bit (WoW64) code. The same registers, read at 32 bits where the CPU would. */
    { 32, "0f010424", PW_OK, "sgdtd", 4, 0, 0, 0, 0x7fff0000, "0000" "0000feff" },
    { 32, "0f010d78563412", PW_OK, "sidtd", 7, 0, 0, 0, 0x12345678, "0000" "0000ffff" },
    { 32, "660f0101", PW_OK, "sgdtw", 4, 0, 0, 0, 3, "0000" "0000feff" },
    { 32, "670f0107", PW_OK, "sgdtd", 4, 0, 0, 0, 0x1234, "0000" "0000feff" },
    { 32, "0f01e0", PW_OK, "smsw", 3, 1, RAX, 0x80050033, 0, NULL },
    { 32, "0f00c8", PW_OK, "str", 3, 1, RAX, 0x40, 0, NULL },
    { 32, "640f0100", PW_ERR_UNSUPPORTED, NULL, 0, 0, 0, 0, 0, NULL },
};

static size_t hex_bytes(const char *hex, uint8_t *out, size_t capacity)
{
    size_t n = 0;
    for (; hex && hex[0] && hex[1] && n < capacity; hex += 2) sscanf(hex, "%2hhx", &out[n++]);
    return n;
}

static PwUmipCpu cpu_for(unsigned mode, uint64_t rip)
{
    PwUmipCpu cpu = { .rip = rip, .mode = mode };
    for (unsigned i = 0; i < 16; i++) cpu.gpr[i] = ~0ull;
    cpu.gpr[RAX] = 0x1000;
    cpu.gpr[RCX] = 3;
    cpu.gpr[RSP] = mode == 64 ? 0x7fff0000 : 0xffffffff7fff0000ull;   /* the upper half is not an address in 32-bit code */
    cpu.gpr[RBX] = 0x1234;
    cpu.gpr[R8] = 0x5000;
    cpu.gpr[R9] = 0x1111111111111111ull;
    return cpu;
}

static void test_cases(void)
{
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        const Case *c = &cases[i];
        uint8_t code[16];
        const size_t n = hex_bytes(c->hex, code, sizeof(code));
        PwUmipCpu cpu = cpu_for(c->mode, 0x400000);
        PwUmipEffect effect;
        const int result = pw_umip_emulate(code, n, &cpu, &effect);
        if (result != c->result) {
            fprintf(stderr, "%u %s: result %d, want %d\n", c->mode, c->hex, result, c->result);
            assert(0);
        }
        if (result != PW_OK) continue;
        assert(!strcmp(effect.mnemonic, c->mnemonic));
        assert(effect.length == c->length && effect.rip == 0x400000 + c->length);
        assert(effect.to_register == c->to_register);
        if (c->to_register) {
            const uint64_t want = c->value;
            if (effect.reg != c->reg || effect.value != want) {
                fprintf(stderr, "%u %s: r%u = %#llx, want r%u = %#llx\n", c->mode, c->hex, effect.reg,
                        (unsigned long long)effect.value, c->reg, (unsigned long long)want);
                assert(0);
            }
        } else {
            uint8_t want[10];
            const size_t size = hex_bytes(c->data, want, sizeof(want));
            if (effect.address != c->address || effect.size != size || memcmp(effect.data, want, size)) {
                fprintf(stderr, "%u %s: %u bytes at %#llx, want %zu at %#llx\n", c->mode, c->hex, effect.size,
                        (unsigned long long)effect.address, size, (unsigned long long)c->address);
                assert(0);
            }
        }
    }
}

/* The next instruction wraps at 4 GiB in 32-bit code; a register write there zero-extends. */
static void test_32_bit_wrap(void)
{
    const uint8_t smsw[] = { 0x0f, 0x01, 0xe0 };
    PwUmipCpu cpu = cpu_for(32, 0xfffffffe);
    PwUmipEffect effect;
    assert(pw_umip_emulate(smsw, sizeof(smsw), &cpu, &effect) == PW_OK);
    assert(effect.rip == 1 && effect.value == 0x80050033);
}

static void test_bounds(void)
{
    const uint8_t sgdt[] = { 0x0f, 0x01, 0x04, 0x24 };
    PwUmipCpu cpu = cpu_for(64, 0x400000);
    PwUmipEffect effect;
    assert(pw_umip_emulate(NULL, 4, &cpu, &effect) == PW_ERR_PRECONDITION);
    assert(pw_umip_emulate(sgdt, 4, NULL, &effect) == PW_ERR_PRECONDITION);
    assert(pw_umip_emulate(sgdt, 4, &cpu, NULL) == PW_ERR_PRECONDITION);
    cpu.mode = 16;
    assert(pw_umip_emulate(sgdt, 4, &cpu, &effect) == PW_ERR_PRECONDITION);
    cpu.mode = 64;
    for (size_t n = 0; n < sizeof(sgdt); n++) assert(pw_umip_emulate(sgdt, n, &cpu, &effect) == PW_ERR_UNSUPPORTED);
    assert(pw_umip_emulate(sgdt, sizeof(sgdt), &cpu, &effect) == PW_OK && !memcmp(effect.data, GDT64, 10));
}

static void test_nested_task_flag(void)
{
    uint64_t eflags = 0x246;                         /* ZF PF IF and the always-set bit 1: nothing to clear */
    assert(!pw_umip_clear_nested_task(&eflags) && eflags == 0x246);
    eflags = 0x246 | PW_EFLAGS_NESTED_TASK;          /* popf with NT set */
    assert(pw_umip_clear_nested_task(&eflags) && eflags == 0x246);
    assert(!pw_umip_clear_nested_task(&eflags) && eflags == 0x246);   /* the retry finds it clear: no loop */
    eflags = 0xffffffffffffffffull;                  /* every other bit stays */
    assert(pw_umip_clear_nested_task(&eflags) && eflags == (0xffffffffffffffffull & ~0x4000ull));
    eflags = 0x100000000ull | PW_EFLAGS_NESTED_TASK; /* bits above 32 are not touched */
    assert(pw_umip_clear_nested_task(&eflags) && eflags == 0x100000000ull);
}

int main(void)
{
    test_nested_task_flag();
    test_cases();
    test_32_bit_wrap();
    test_bounds();
    printf("pw_umip: %zu encodings, all checks passed\n", sizeof(cases) / sizeof(cases[0]));
    return 0;
}
