#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2427 (a process's command line, its own or another's). ntdll's process.c is not built on the host, so the
answer's layout (ps5_command_line_answer) is cut out of the patch and run against the few Windows types it uses; the patch's
case is checked as text: own process from its parameters, another through NtReadVirtualMemory along PEB, ProcessParameters,
CommandLine."""
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def patch_text():
    return next((ROOT / 'wine/patches').glob('2427-*.patch')).read_text()


def added():
    return '\n'.join(l[1:] for l in patch_text().split('\n') if l.startswith('+') and not l.startswith('+++'))


def cut():
    text = added()
    start = text.index('static unsigned int ps5_command_line_answer')
    end = text.index('struct ps5_command_line_source')
    return text[start:end]


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned short USHORT, WCHAR;
typedef unsigned int ULONG;
typedef struct { USHORT Length, MaximumLength; WCHAR *Buffer; } UNICODE_STRING;
#define STATUS_SUCCESS 0u
#define STATUS_INFO_LENGTH_MISMATCH 0xc0000004u
#define STATUS_ACCESS_VIOLATION 0xc0000005u
#include "answer.inc"
static const WCHAR text[] = {'"','g','.','e','x','e','"',' ','-','-','x'};
static unsigned int copy(void *dest, void *context) { (void)context; memcpy(dest, text, sizeof(text)); return 0; }
static unsigned int fails(void *dest, void *context) { (void)dest; (void)context; return STATUS_ACCESS_VIOLATION; }
int main(void)
{
    static unsigned char buffer[256];
    ULONG needed = 0;
    /* too small: the needed length, nothing written */
    memset(buffer, 0xcc, sizeof(buffer));
    assert(ps5_command_line_answer(sizeof(text), copy, NULL, buffer, 10, &needed) == STATUS_INFO_LENGTH_MISMATCH);
    assert(needed == sizeof(UNICODE_STRING) + sizeof(text) && buffer[0] == 0xcc);
    /* exactly enough: the string, its Buffer just past it, the text */
    assert(ps5_command_line_answer(sizeof(text), copy, NULL, buffer, needed, &needed) == STATUS_SUCCESS);
    const UNICODE_STRING *str = (const UNICODE_STRING *)buffer;
    assert(str->Length == sizeof(text) && str->MaximumLength == sizeof(text));
    assert((unsigned char *)str->Buffer == buffer + sizeof(UNICODE_STRING) && !memcmp(str->Buffer, text, sizeof(text)));
    /* an empty command line: a valid empty string */
    assert(ps5_command_line_answer(0, fails, NULL, buffer, sizeof(buffer), &needed) == STATUS_SUCCESS && needed == sizeof(UNICODE_STRING));
    /* a read that fails: its status */
    assert(ps5_command_line_answer(sizeof(text), fails, NULL, buffer, sizeof(buffer), &needed) == STATUS_ACCESS_VIOLATION);
    puts("command line answer: ok");
    return 0;
}
'''


class CommandLine(unittest.TestCase):
    def test_the_answer_s_layout(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            (work / 'answer.inc').write_text(cut())
            (work / 'harness.c').write_text(HARNESS)
            subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror', '-o', str(work / 'harness'), str(work / 'harness.c')],
                           check=True, cwd=work)
            result = subprocess.run([str(work / 'harness')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('ok', result.stdout)

    def test_the_case_reads_the_right_process(self):
        text = added()
        case = text[text.index('case ProcessCommandLineInformation:'):]
        self.assertIn('handle == GetCurrentProcess()', case)
        self.assertIn('peb->ProcessParameters->CommandLine', case)
        self.assertIn('NtReadVirtualMemory( handle, &((PEB *)pbi.PebBaseAddress)->ProcessParameters', case)
        self.assertIn('NtReadVirtualMemory( handle, &params->CommandLine', case)
        self.assertIn('ps5_command_line_answer( source.bytes', case)


if __name__ == '__main__':
    unittest.main()
