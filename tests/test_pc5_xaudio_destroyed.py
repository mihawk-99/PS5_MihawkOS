#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2411 (calls on a destroyed XAudio2 voice do nothing). Wine's xaudio2 implementation is one file that the PS5
series leaves alone, so the pin's copy with the patch applied is what the build compiles. It cannot run on the host (it
needs Windows' COM and FAudio's device), so this holds what the patch defines and runs the pieces that can run alone:
 - every method of the source, submix and mastering voices that reaches the FAudio voice looks at it first and does
   nothing when the voice was destroyed (reading it after DestroyVoice read freed memory);
 - destroy_voice forgets the FAudio voice and stamps the time; a second DestroyVoice is ignored;
 - slot_is_free, which decides when a destroyed voice's object may go to a new voice, runs as written: never before the
   grace has passed since the destroy, at once for a voice never destroyed, whatever the tick counter's size;
 - forget_gone_sends, which takes the sends to destroyed voices out of a new voice's list, runs as written."""
import re
import unittest
from pathlib import Path

import pc5_support as support

FILES = ['dlls/xaudio2_7/xaudio_dll.c', 'dlls/xaudio2_7/xaudio_private.h']


def function_bodies(text):
    """{name: text} of every top-level function whose line starts with `static ... XA2<kind>_<name>(`."""
    found = {}
    for match in re.finditer(r'^static [\w ]+? ?\*?WINAPI (XA2(?:SRC|SUB|M)_\w+)\(', text, re.M):
        end = text.index('\n}\n', match.start())
        found.setdefault(match.group(1), text[match.start():end])
    return found


def definition(text, header):
    start = text.index(header)
    return text[start:text.index('\n}\n', start) + 3]


class Destroyed(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = support.scratch()
        cls.work = Path(cls.tmp.name)
        support.patched_files(cls.work, FILES, '2411-*.patch')
        cls.source = (cls.work / FILES[0]).read_text()
        cls.header = (cls.work / FILES[1]).read_text()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_every_method_that_reaches_the_voice_checks_it_first(self):
        bodies = function_bodies(self.source)
        reaching = {n: b for n, b in bodies.items() if 'faudio_voice' in b and not n.endswith('_DestroyVoice')}
        self.assertGreater(len(reaching), 50, 'the voice methods were not found')
        unguarded = []
        for name, body in reaching.items():
            guard = re.search(r'RETURN_(?:ZEROED_)?IF_GONE(?:_WITH)?\(This\b', body)
            if not guard or guard.start() > body.index('faudio_voice'):
                unguarded.append(name)
        self.assertEqual(unguarded, [], 'methods that read the FAudio voice before checking it')

    def test_destroy_forgets_the_voice_and_stamps_the_time(self):
        body = definition(self.source, 'static inline void destroy_voice(XA2VoiceImpl *This)')
        self.assertIn('RETURN_IF_GONE(This);', body)
        self.assertLess(body.index('DestroyVoiceSafeEXT'), body.index('This->faudio_voice = NULL'))
        self.assertRegex(body, r'gone_at_ms = GetTickCount64\(\) \| 1')
        for create in ('src', 'sub'):
            self.assertIn(f'slot_is_free({create}, now)', self.source)
        self.assertIn('ULONGLONG gone_at_ms;', self.header)
        details = definition(self.source, 'static void get_voice_details(')
        self.assertIn('RETURN_ZEROED_IF_GONE(voice, details);', details)

    def run_c(self, name, program):
        source = self.work / f'{name}.c'
        source.write_text(program)
        result = support.compile_and_run([source], self.work / name)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f'pc5 {name} passed', result.stdout)

    def test_a_destroyed_slot_is_free_only_after_the_grace(self):
        grace = re.search(r'#define LATE_CALL_GRACE_MS (\d+)', self.source).group(1)
        self.assertGreaterEqual(int(grace), 1000, 'a late call comes within moments; the grace must be seconds')
        predicate = definition(self.source, 'static inline BOOL voice_is_gone(')
        free = definition(self.source, 'static inline BOOL slot_is_free(')
        self.run_c('slot_free', f'''
#include <stdio.h>
#include <stdint.h>
typedef int BOOL; typedef uint64_t ULONGLONG; typedef struct FAudioVoice FAudioVoice;
#define LATE_CALL_GRACE_MS {grace}
typedef struct {{ FAudioVoice *faudio_voice; BOOL in_use; ULONGLONG gone_at_ms; }} XA2VoiceImpl;
{predicate}
{free}
static int bad;
static void check(const char *what, int got, int want) {{ if (got != want) {{ printf("%s: %d, wanted %d\\n", what, got, want); bad = 1; }} }}
int main(void)
{{
    XA2VoiceImpl fresh = {{0}}, busy = {{0, 1, 0}}, dead = {{0, 0, 100000}};
    ULONGLONG grace = LATE_CALL_GRACE_MS;
    check("a voice never destroyed is free at once", slot_is_free(&fresh, 5), 1);
    check("a voice in use is not free", slot_is_free(&busy, 1ull << 40), 0);
    check("not at the destroy", slot_is_free(&dead, 100000), 0);
    check("not a moment before the grace", slot_is_free(&dead, 100000 + grace - 1), 0);
    check("free when the grace has passed", slot_is_free(&dead, 100000 + grace), 1);
    check("and long after", slot_is_free(&dead, 100000 + 100 * grace), 1);
    dead.gone_at_ms = 1;                                     /* the smallest stamp: destroyed at the first millisecond */
    check("the smallest stamp is a stamp", slot_is_free(&dead, grace), 0);
    check("...and expires", slot_is_free(&dead, 1 + grace), 1);
    check("a destroyed voice is gone", voice_is_gone(&dead) && voice_is_gone(&fresh), 1);
    if (!bad) puts("pc5 slot_free passed");
    return bad;
}}
''')

    def test_sends_to_destroyed_voices_are_taken_out(self):
        forget = definition(self.source, 'static FAudioVoiceSends *forget_gone_sends(')
        self.run_c('gone_sends', f'''
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
typedef struct FAudioVoice FAudioVoice;
typedef struct {{ uint32_t Flags; FAudioVoice *pOutputVoice; }} FAudioSendDescriptor;
typedef struct {{ uint32_t SendCount; FAudioSendDescriptor *pSends; }} FAudioVoiceSends;
{forget}
static int bad;
static void check(const char *what, long got, long want) {{ if (got != want) {{ printf("%s: %ld, wanted %ld\\n", what, got, want); bad = 1; }} }}
int main(void)
{{
    FAudioVoice *a = (FAudioVoice *)1, *b = (FAudioVoice *)2;
    FAudioSendDescriptor items[4] = {{{{10, a}}, {{11, NULL}}, {{12, b}}, {{13, NULL}}}};
    FAudioVoiceSends sends = {{4, items}};
    check("a NULL list stays NULL", forget_gone_sends(NULL) == NULL, 1);
    check("the list itself is returned", forget_gone_sends(&sends) == &sends, 1);
    check("two sends are left", sends.SendCount, 2);
    check("the first keeps its voice and flags", items[0].pOutputVoice == a && items[0].Flags == 10, 1);
    check("the second is the third, closed up", items[1].pOutputVoice == b && items[1].Flags == 12, 1);
    FAudioSendDescriptor all_gone[2] = {{{{1, NULL}}, {{2, NULL}}}};
    FAudioVoiceSends none = {{2, all_gone}};
    forget_gone_sends(&none);
    check("every send gone leaves none", none.SendCount, 0);
    FAudioSendDescriptor live[2] = {{{{1, a}}, {{2, b}}}};
    FAudioVoiceSends all = {{2, live}};
    forget_gone_sends(&all);
    check("live sends are untouched", all.SendCount == 2 && live[0].Flags == 1 && live[1].Flags == 2, 1);
    if (!bad) puts("pc5 gone_sends passed");
    return bad;
}}
''')


if __name__ == '__main__':
    unittest.main()
