#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""tools/run-console.py's readings of the title's and helpers' lines: the executable-protection tally, the SIGKILL
ending, the broker wedge, the helpers' backstop, the memory helper and the direct-section census. Each fixture line
is the format its source prints, and the source is checked to still print it, so the C side and the runner cannot
drift apart unnoticed."""
import importlib.util
import os
import subprocess
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('console_run', ROOT / 'tools/run-console.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


def printed(source, *fragments):
    text = (ROOT / source).read_text()
    for fragment in fragments:
        assert fragment in text, f'{source} no longer prints {fragment!r}'


TALLY = ('wine-child: executable protection refused=3 conversions={} converted_kib={} convert_failed={} exec_refused={} '
         'grant_retries=1 modules=7 sdk_refused={}\n')


class ExecProtection(unittest.TestCase):
    def test_source_prints_the_tally(self):
        printed('native/proton_wine_child.c', '"wine-child: executable protection refused=%llu conversions=%llu converted_kib=%llu "',
                '"convert_failed=%llu exec_refused=%llu grant_retries=%llu modules=%u sdk_refused=%llu\\n"',
                '"wine-child: payload SDK memory adopted; execute through JIT memory\\n"')
        self.assertIn(runner.EXEC_ADOPTED, (ROOT / 'native/proton_wine_child.c').read_text())

    def test_last_tally_of_each_helper_summed(self):
        first = TALLY.format(1, 16, 0, 0, 0) + TALLY.format(40, 2048, 0, 0, 0)
        second = TALLY.format(5, 320, 1, 2, 3)
        record = runner.exec_protection([first, second, 'no tally here'])
        self.assertEqual(record, {'refused': 6, 'conversions': 45, 'converted_kib': 2368, 'convert_failed': 1,
                                  'exec_refused': 2, 'grant_retries': 2, 'modules': 14, 'sdk_refused': 3})
        self.assertIsNone(runner.exec_protection(['wine-child: entry']))

    def test_what_fails_a_run(self):
        self.assertTrue(runner.exec_clean(None))
        self.assertTrue(runner.exec_clean(runner.exec_protection([TALLY.format(9, 144, 0, 0, 0)])))
        for failed, refused, sdk in ((1, 0, 0), (0, 1, 0), (0, 0, 1)):
            with self.subTest(convert_failed=failed, exec_refused=refused, sdk_refused=sdk):
                self.assertFalse(runner.exec_clean(runner.exec_protection([TALLY.format(9, 144, failed, refused, sdk)])))

    def test_one_row_per_helper(self):
        cef = ('0.1:0248:024c:trace:loaddll:build_module Loaded L"C:\\Program Files (x86)\\Launcher\\bin\\cef\\cef.win64\\'
               'launcherwebhelper.exe" at 0000000140000000: native\n'
               'trace:loaddll:build_module Loaded L"C:\\x\\libcef.dll" at 0000000180000000: native\n'
               'wine-child: memory resident=119 MiB mapped=1 MiB (status 0) images=54 holding 285 MiB, files read in 1 MiB\n'
               'wine-child: vm resident=27 MiB direct_resident=559 flexible=142 direct=560 other=1 MiB mappings=1\n'
               'wine-child: memory resident=59 MiB mapped=1 MiB (status 0) images=54 holding 285 MiB, files read in 1 MiB\n'
               + TALLY.format(900, 61440, 0, 0, 0))
        current = cef.replace('direct_resident=559 flexible', 'direct_resident=559 reserved_resident=4 flexible')
        self.assertEqual(runner.helper_rows([current])[0]['vm_last']['flexible_mib'], 142)
        printed('native/proton_wine_child.c', '"wine-child: vm resident=%llu MiB direct_resident=%llu reserved_resident=%llu flexible=%llu',
                '"wine-child: memory resident=%llu MiB mapped=%llu MiB (status %#x) images=%llu "')
        rows = runner.helper_rows([cef, 'wine-child: entry'])
        self.assertEqual(rows[0], {'program': 'launcherwebhelper.exe', 'libcef': True, 'resident_peak_mib': 119,
                                   'resident_last_mib': 59, 'images_held_mib': 285,
                                   'vm_last': {'resident_mib': 27, 'direct_resident_mib': 559, 'flexible_mib': 142},
                                   'exec': {'refused': 3, 'conversions': 900, 'converted_kib': 61440, 'convert_failed': 0,
                                            'exec_refused': 0, 'grant_retries': 1, 'modules': 7, 'sdk_refused': 0}})
        self.assertEqual(rows[1]['program'], None)
        self.assertIsNone(rows[1]['exec'])

    def test_a_helper_that_adopted_must_tally(self):
        adopted = 'wine-child: ' + runner.EXEC_ADOPTED + '\n'
        self.assertTrue(runner.exec_tally_missing([adopted]))
        self.assertFalse(runner.exec_tally_missing([adopted + TALLY.format(1, 16, 0, 0, 0), 'wine-child: entry']))
        old = 'wine-child: executable protection given=3 refused=0 waited_retries=0 modules=7 sdk_refused=0\n'
        self.assertTrue(runner.exec_tally_missing([adopted + old]))   # the old format is not a tally
        self.assertIsNone(runner.exec_protection([old]))


FOREIGN = ('wine-child: foreign mappings found={} skipped_unmaps={} refused_maps={} query_failed=0 holes={}{}\n')
HOLE = ' at={}+0x10000/33 skips=1 refusals={} kind=12:anon:00fc0075420f'


class ForeignMappings(unittest.TestCase):
    def test_source_prints_the_tally(self):
        printed('native/proton_wine_child.c', '"wine-child: foreign mappings found=%llu skipped_unmaps=%llu "',
                '"refused_maps=%llu query_failed=%llu holes=%llu"',
                '" at=%#llx+%#llx/%llx skips=%llu refusals=%llu kind=%s%02x:%s"',
                '"__wine_ps5_foreign_stats"')

    def test_last_tally_of_each_helper_summed_and_holes_listed(self):
        first = FOREIGN.format(0, 0, 0, 0, '') + FOREIGN.format(1, 1, 4, 1, HOLE.format('0xc80000000', 4))
        second = FOREIGN.format(2, 0, 1, 2, HOLE.format('0x900000000', 1) + HOLE.format('0x910000000', 0))
        record = runner.foreign_mappings([first, second, 'wine-child: entry'])
        self.assertEqual({key: record[key] for key in runner.FOREIGN_KEYS},
                         {'found': 3, 'skipped_unmaps': 1, 'refused_maps': 5, 'query_failed': 0, 'holes': 3})
        self.assertEqual([hole['address'] for hole in record['hole_list']], ['0xc80000000', '0x900000000', '0x910000000'])
        self.assertEqual(record['hole_list'][0], {'address': '0xc80000000', 'bytes': 0x10000, 'protection': 0x33,
                                                  'skips': 1, 'refusals': 4, 'kind': '12:anon:00fc0075420f'})
        self.assertIsNone(runner.foreign_mappings(['wine-child: entry']))

    def test_recorded_never_judged(self):
        source = (ROOT / 'tools/run-console.py').read_text()
        self.assertIn("'foreign_mappings': foreign_record", source)
        self.assertNotIn('passed and foreign', source)


SHARE = ('wine-child: shared code copies shared={} shared_kib={} offered={} mismatched=0 failed={} private_writes=1 '
         'private_kib=16\n')


class CodeShare(unittest.TestCase):
    def test_source_prints_the_tally(self):
        printed('native/proton_wine_child.c', '"wine-child: shared code copies shared=%llu shared_kib=%llu offered=%llu mismatched=%llu "',
                '"failed=%llu private_writes=%llu private_kib=%llu mapped_kib=%llu\\n"', '"__wine_ps5_code_share_stats"')
        printed('tools/run-console.py', "'code_share': code_share(child_texts)")

    def test_last_tally_of_each_helper_summed(self):
        first = SHARE.format(0, 0, 4, 0) + SHARE.format(2, 32, 40, 0)
        second = SHARE.format(30, 480, 1, 1)
        record = runner.code_share([first, second, 'wine-child: entry'])
        self.assertEqual(record, {'shared': 32, 'shared_kib': 512, 'offered': 41, 'mismatched': 0, 'failed': 1,
                                  'private_writes': 2, 'private_kib': 32})
        self.assertIsNone(runner.code_share(['wine-child: entry']))


TITLE_ENDING = [
    '[MihawkOS] Wine: stop: ending 2 helpers by SIGKILL, not through the server',
    '[MihawkOS] Wine: helper 901: SIGKILL sent (errno 0)',
    '[MihawkOS] Wine: helper 902: SIGKILL found it gone already (errno 3)',
    '[MihawkOS] Wine: helper 901 gone 300 ms after SIGKILL',
    '[MihawkOS] Wine: helper 902 gone 0 ms after SIGKILL',
    '[MihawkOS] Wine: after the kill: census 420 ms old, frames +12 in 1500 ms',
]


class HelpersEnded(unittest.TestCase):
    def test_source_prints_the_lines(self):
        printed('native/proton_wine.cpp', '"Wine: %s: ending %u helpers by SIGKILL, not through the server"',
                '"Wine: helper %d: SIGKILL %s (errno %d)"', '"not sent: not a helper\'s pid"', '"found it gone already"',
                '"Wine: helper %d gone %llu ms after SIGKILL"', '"Wine: process %d is still there %llu ms after SIGKILL',
                '"Wine: after the kill: census %lld ms old, frames +%llu in %llu ms"',
                '"Wine: hang breaker: stalled: no frame for %llu ms and the system memory census %llu ms old%s"')
        printed('native/proton_flight_recorder.cpp', '"flight recorder: process rows unavailable (kern.proc errno %d)')

    def test_clean_ending(self):
        record = runner.helpers_ended(TITLE_ENDING)
        self.assertEqual(record['why'], 'stop')
        self.assertEqual(record['signals'][902], {'outcome': 'found it gone already', 'errno': 3})
        self.assertTrue(runner.ended_cleanly(record))

    def test_what_is_not_a_clean_ending(self):
        for lines in (TITLE_ENDING[:4] + TITLE_ENDING[5:],                       # 902 never reported gone
                      TITLE_ENDING + ['Wine: process 901 is still there 1000 ms after SIGKILL (kill(pid, 0): errno 0)'],
                      TITLE_ENDING[:-1],                                       # no reading after the kill
                      TITLE_ENDING[:-1] + ['Wine: after the kill: census 1000 ms old, frames +0 in 900 ms'],
                      TITLE_ENDING[:-1] + ['Wine: after the kill: census -1 ms old, frames +0 in 900 ms'],
                      [line.replace('SIGKILL sent', 'SIGKILL refused').replace('errno 0)', 'errno 1)')
                       for line in TITLE_ENDING if 'helper 901 gone' not in line]):
            with self.subTest(lines=lines):
                self.assertFalse(runner.ended_cleanly(runner.helpers_ended(lines)))
        self.assertIsNone(runner.helpers_ended(['Wine: load status=0']))
        self.assertFalse(runner.ended_cleanly(None))

    def test_breaker_and_recorder(self):
        record = runner.helpers_ended([
            'Wine: hang breaker: stalled: no frame for 10012 ms and the system memory census 10500 ms old',
            'flight recorder: process rows unavailable (kern.proc errno 1); not asked again'] + TITLE_ENDING[1:])
        self.assertEqual(record['stalled'], {'frame_ms': 10012, 'census_ms': 10500})
        self.assertTrue(record['recorder_rows'].startswith('process rows unavailable'))
        self.assertFalse(runner.ended_cleanly(record))   # no ending line: nothing was ended


WEDGE_ENDING = [
    "[MihawkOS] flight recorder: wedge broker_busy_s=20: the GPU broker's client lock is held; "
    "ending the game's helpers",
    "[MihawkOS] Wine: the GPU broker's client lock held 20 s: ending 2 helpers by SIGKILL, not through the server",
] + TITLE_ENDING[1:]
FLIGHT = 'fr t={}.0 verdict=fine present=16 census=420 flips=+0 mem_lock=free broker={} display=free fences=900/900{}'


class BrokerWedge(unittest.TestCase):
    def test_source_prints_the_lines(self):
        printed('native/proton_flight_recorder.cpp',
                '"flight recorder: wedge broker_busy_s=%u: the GPU broker\'s client lock is held%s"',
                '"the GPU broker\'s client lock held %u s"', '" phase=%s:%llu tid=%d"', '" phase=none"')
        printed('src/pw_hang_breaker.h', f'#define PW_HANG_WEDGE_S {runner.WEDGE_S}u')
        # PS5_Vulkan d14e738, once PS5_Proton's graphics patch 0017 (tools/setup-graphics-foundation.sh pins the revision)
        fork = os.environ.get('PS5_VULKAN_GIT') or str(ROOT.parent / 'PS5_Vulkan')
        change = subprocess.run(['git', '-C', fork, 'show', 'd14e738dec4f596e1e6d060362e0d42f4a83f608'], check=True,
                                capture_output=True, text=True).stdout
        for fragment in ('Phase hold("test-hold");', 'getenv("PROTON_TEST_BROKER_HOLD_S")'):
            self.assertIn(fragment, change)

    def test_the_recorder_ended_the_game(self):
        record = runner.helpers_ended(WEDGE_ENDING)
        self.assertEqual((record['wedge_s'], record['why']), (20, "the GPU broker's client lock held 20 s"))
        self.assertTrue(runner.broker_wedge_ended(record))

    def test_what_is_not_a_wedge_ending(self):
        for lines in (WEDGE_ENDING[1:],                                         # no wedge line
                      [WEDGE_ENDING[0].replace('busy_s=20', 'busy_s=19')] + WEDGE_ENDING[1:],
                      WEDGE_ENDING[:1] + TITLE_ENDING,                          # ended by the stop, not the wedge
                      WEDGE_ENDING[:-1]):                                       # no reading after the kill
            with self.subTest(lines=lines):
                self.assertFalse(runner.broker_wedge_ended(runner.helpers_ended(lines)))
        report_only = runner.helpers_ended(["flight recorder: wedge broker_busy_s=20: the GPU broker's client lock "
                                            "is held; reporting only"])
        self.assertEqual(report_only['wedge_s'], 20)
        self.assertFalse(runner.broker_wedge_ended(report_only))
        self.assertFalse(runner.broker_wedge_ended(None))

    def test_the_held_call_named(self):
        lines = [FLIGHT.format(40, 'free', ''), FLIGHT.format(41, 'busy:1', ' phase=none'),
                 FLIGHT.format(42, 'busy:2', ' phase=test-hold:1210 tid=101722'),
                 FLIGHT.format(60, 'busy:20', ' phase=test-hold:19214 tid=101722'), 'wedge broker_busy_s=20: x',
                 FLIGHT.format(61, 'busy:21', ' phase=copy:3 tid=101730')]
        self.assertEqual(runner.broker_phases(lines), {'test-hold': 19214, 'copy': 3})
        self.assertIsNone(runner.broker_phases(lines[:2]))


def helper_log(*lines):
    return ''.join(line + '\n' for line in ('wine-child: entry',) + lines)


SEEN = ('wine-child: payload doom seen reason={} stamped_ms_ago=0 due_in_ms={}; ending the process then unless '
        'the title closes it first')
DOOMED = 'wine-child: payload doomed reason={} stamped_ms_ago={}; ending without Wine'
TERMINAL = 'wine-child: payload terminal reason=control-ended bootstrap_error=0; returning to payload main'
ENDING = 'wine-child: payload ending monotonic_ms=4845729'


class HelperDoom(unittest.TestCase):
    def test_source_prints_the_lines(self):
        printed('native/proton_wine_child.c',
                '"wine-child: payload doom seen reason=%s stamped_ms_ago=%lld due_in_ms=%lld; "',
                '"wine-child: payload doomed reason=%s stamped_ms_ago=%lld; ending without Wine\\n"',
                '{"gpu", "exit", "engine", "test", "stall"}',
                # Only the run's program dooms itself: the doom-backstop case must not end wineboot in the probe prefix.
                'if (after && *after && getenv("WINE_PS5_INITIAL_CLIENT")) {')
        printed('native/proton_payload_child.c', '"wine-child: payload ending monotonic_ms=%lld\\n"')
        grace = ', '.join(str(runner.DOOM_GRACE_MS[reason]) for reason in ('gpu', 'exit', 'engine', 'test', 'stall'))
        printed('src/pw_control_watch.h', f'pw_control_doom_grace_ms[PW_CHILD_DOOMS] = {{{grace}}};')

    def test_doomed_for_the_test(self):
        doomed = helper_log(SEEN.format('test', 5000), DOOMED.format('test', 5043), ENDING)
        others = helper_log(SEEN.format('exit', 20000), TERMINAL, ENDING)   # services, ending as the program goes
        record = runner.helper_doom([doomed, others, helper_log(TERMINAL, ENDING)])
        self.assertEqual(record[0], {'seen': ['test'], 'doomed': 'test', 'stamped_ms_ago': 5043, 'ended': True})
        self.assertEqual(len(record), 2)
        self.assertTrue(runner.doom_ended(record, 'test'))
        for text in (helper_log(SEEN.format('test', 5000), DOOMED.format('test', 5043)),             # no ending
                     helper_log(SEEN.format('test', 5000), ENDING, DOOMED.format('test', 5043)),     # ending before it
                     helper_log(SEEN.format('test', 5000), DOOMED.format('test', 6100), ENDING),     # past the grace
                     helper_log(SEEN.format('test', 5000), DOOMED.format('test', 4990), ENDING),     # before it
                     helper_log(SEEN.format('test', 5000), TERMINAL, ENDING),                        # not doomed
                     helper_log(SEEN.format('exit', 20000), DOOMED.format('exit', 20010), ENDING)):  # another reason
            with self.subTest(text=text):
                self.assertFalse(runner.doom_ended(runner.helper_doom([text]), 'test'))
        self.assertIsNone(runner.helper_doom([helper_log(TERMINAL, ENDING)]))
        self.assertFalse(runner.doom_ended(None, 'test'))

    def test_an_answered_failure_dooms_nothing(self):
        # gpu-loss: the broker answers a submission with a failure, so the client does not give it up (graphics patch
        # 0017) and nothing stamps the GPU reason: a helper that saw it fails the run even when it ended by itself
        # inside the grace (gate-hang-a-2026-10-15-01 gpu-window32/64, where win32u stamped it at answered refusals).
        by_doom = helper_log(SEEN.format('gpu', 5000), SEEN.format('exit', 20000), DOOMED.format('gpu', 5100), ENDING)
        by_exit = helper_log(SEEN.format('gpu', 5000), SEEN.format('exit', 20000), TERMINAL, ENDING)
        for texts in ([by_doom], [by_exit, by_doom], [by_exit]):
            with self.subTest(texts=texts):
                self.assertFalse(runner.backstop_allowed('gpu-loss', runner.helper_doom(texts)))
        self.assertTrue(runner.backstop_allowed('gpu-loss', runner.helper_doom(
            [helper_log(SEEN.format('exit', 20000), TERMINAL, ENDING)])))
        self.assertTrue(runner.backstop_allowed('gpu-loss', None))

    def test_which_run_may_doom(self):
        by_gpu = helper_log(SEEN.format('gpu', 5000), DOOMED.format('gpu', 5040), ENDING)
        killed = helper_log(SEEN.format('gpu', 5000))   # the title's SIGKILL came first: no ending of its own
        by_test = helper_log(SEEN.format('test', 5000), DOOMED.format('test', 5043), ENDING)
        # The broker wedge: the held submit's give-up (the client's GPU stamp) and the recorder's 20th sample end the
        # helper at about the same time (tools/run-battery.py broker-wedge64); either may, and no other reason.
        for texts in ([by_gpu], [killed], [helper_log(TERMINAL, ENDING)]):
            with self.subTest(texts=texts):
                self.assertTrue(runner.backstop_allowed('broker-wedge', runner.helper_doom(texts)))
        self.assertTrue(runner.backstop_allowed('broker-wedge', None))
        for texts in ([by_test], [helper_log(SEEN.format('gpu', 5000), DOOMED.format('gpu', 6200), ENDING)]):
            with self.subTest(texts=texts):
                self.assertFalse(runner.backstop_allowed('broker-wedge', runner.helper_doom(texts)))
        # Any other run: no backstop, and no GPU stamp seen at all, even by a helper the title or itself ended first.
        by_itself = helper_log(SEEN.format('gpu', 5000), SEEN.format('exit', 20000), TERMINAL, ENDING)
        for texts in ([by_gpu], [killed], [by_itself]):
            with self.subTest(texts=texts):
                self.assertFalse(runner.backstop_allowed(None, runner.helper_doom(texts)))
        self.assertTrue(runner.backstop_allowed(None, runner.helper_doom([helper_log(SEEN.format('exit', 20000),
                                                                                     TERMINAL, ENDING)])))
        # The doom run: the test stamp ends the program, and no helper is ended for anything else (the 2026-10-15 run's exit
        # stuck 20 s in NtTerminateProcess, ended by its exit backstop, beside it).
        stuck_exit = helper_log(SEEN.format('exit', 20000), DOOMED.format('exit', 20040), ENDING)
        self.assertTrue(runner.backstop_allowed('doom', runner.helper_doom([by_test])))
        for texts in ([by_gpu], [by_test, by_gpu], [by_test, stuck_exit]):
            with self.subTest(texts=texts):
                self.assertFalse(runner.backstop_allowed('doom', runner.helper_doom(texts)))
        self.assertFalse(runner.backstop_allowed('gpu-loss', runner.helper_doom([by_gpu])))

    def test_no_run_may_stall(self):
        # A lock held 3 s (src/pw_control_watch.h): recorded, and the run failed, whatever it injected and even when
        # the title closed the helper before the grace was up.
        by_stall = helper_log(SEEN.format('stall', 5000), DOOMED.format('stall', 5021), ENDING)
        record = runner.helper_doom([by_stall])
        self.assertEqual(record, [{'seen': ['stall'], 'doomed': 'stall', 'stamped_ms_ago': 5021, 'ended': True}])
        self.assertTrue(runner.doom_ended(record, 'stall'))
        closed_first = helper_log(SEEN.format('stall', 5000), TERMINAL, ENDING)
        by_test = helper_log(SEEN.format('test', 5000), DOOMED.format('test', 5043), ENDING)
        by_gpu = helper_log(SEEN.format('gpu', 5000), DOOMED.format('gpu', 5040), ENDING)
        for fault, texts in ((None, [by_stall]), (None, [closed_first]), ('doom', [by_test, by_stall]),
                             ('broker-wedge', [by_gpu, closed_first]), ('gpu-loss', [closed_first])):
            with self.subTest(fault=fault, texts=texts):
                self.assertFalse(runner.backstop_allowed(fault, runner.helper_doom(texts)))

    def test_a_doomed_helper_owes_no_tally(self):
        adopted = 'wine-child: ' + runner.EXEC_ADOPTED
        self.assertFalse(runner.exec_tally_missing([helper_log(adopted, SEEN.format('test', 5000),
                                                               DOOMED.format('test', 5043), ENDING)]))
        self.assertTrue(runner.exec_tally_missing([helper_log(adopted, SEEN.format('test', 5000), ENDING)]))


class MemoryHelper(unittest.TestCase):
    def test_source_prints_the_lines(self):
        printed('wine/ps5/pw_wine_process.c', '"wine-ps5: memory helper did not answer %s %#llx %#llx: errno %d, waited %llu ms; marked dead, "',
                '"wine-ps5: memory helper slowest answers %s (ms):"', '" %s=%llu"', '"this session"',
                '"wine-ps5: memory helper: test stall before direct-memory chunk %ld\\n"',
                '"wine-ps5: section of %llu MiB in direct memory\\n"',
                '"wine-ps5: direct-memory sections gave back %llu chunks (%llu lost: the memory helper did not "')

    def test_a_stalled_helper(self):
        record = runner.memory_helper([
            '[PS5 Proton] wine-ps5: section of 256 MiB in direct memory',
            'wine-ps5: memory helper: test stall before direct-memory chunk 3',
            'wine-ps5: memory helper did not answer stall 0x1b58 0: errno 60, waited 5004 ms; marked dead, nothing more is asked of it',
            'wine-ps5: memory helper slowest answers before it (ms): census=12 alloc=0',
            'wine-ps5: direct-memory sections gave back 0 chunks (2 lost: the memory helper did not take them); 0 released',
            'wine-ps5: memory helper slowest answers this session (ms): census=12 alloc=0 shared=3 touch=1 dmem=9 grant=1 free=2 stall=5004'])
        self.assertEqual(record['dead'], [{'request': 'stall', 'errno': 60, 'waited_ms': 5004}])
        self.assertEqual(record['slowest']['stall'], 5004)
        self.assertEqual((record['stall_tests'], record['sections_mib'], record['given_back'], record['lost']), (1, [256], 0, 2))

    def test_an_answering_helper(self):
        record = runner.memory_helper(['wine-ps5: direct-memory sections gave back 128 chunks (0 lost: x); 0 released'])
        self.assertEqual((record['dead'], record['slowest'], record['given_back'], record['gave_back_lines']), ([], None, 128, 1))


class WineRobustness(unittest.TestCase):
    def test_source_prints_the_lines(self):
        printed('native/proton_wine_child.c', '"wine-child: direct sections chunk_maps=%llu mapped_mib=%llu retries=%llu asks=%llu "',
                '"remembered=%llu ask_failures=%llu misplaced=%llu last=%#llx+%#llx\\n"')
        printed('wine/ps5/pw_wine_process.c', '"wine-ps5: child native=%d completion=%#x service=%#x\\n"')
        printed('wine/ps5/pw_wine_dl_libc.c', 'pw_ps5_child_exit_notice(0xc0000420u)')
        printed('tests/vm-protect-probe.c',
                '"PS5_PROTON_VM_PROTECT_PROBE exec: E1-E7 passed; 1000 rewrites of a code page (read-write, write, execute, run) took "')

    def test_highest_census_values(self):
        census = 'wine-child: direct sections chunk_maps={} mapped_mib={} retries=0 asks={} remembered=0 ask_failures={} misplaced=0 last=0+0'
        record = runner.wine_robustness(
            ['wine-ps5: child native=5 completion=0 service=0x1'],
            [census.format(4, 8, 4, 0), census.format(32, 64, 32, 1), census.format(6, 12, 6, 0)])
        self.assertEqual({k: record[k] for k in ('assertion', 'chunk_maps', 'mapped_mib', 'ask_failures_max', 'misplaced_max')},
                         {'assertion': False, 'chunk_maps': 32, 'mapped_mib': 64, 'ask_failures_max': 1, 'misplaced_max': 0})

    def test_assertion_and_rep_step(self):
        record = runner.wine_robustness(
            ['[PS5 Proton] wine-ps5: child native=5 completion=0xc0000420 service=0x1'],
            ['PS5_PROTON_VM_PROTECT_PROBE stepped: rep movsb of 4096 bytes beside read-only pages took 350211 us',
             'PS5_PROTON_VM_PROTECT_PROBE exec: E1-E7 passed; 1000 rewrites of a code page (read-write, write, execute, '
             'run) took 64012 us'])
        self.assertTrue(record['assertion'])
        self.assertEqual(record['rep_step_us'], 350211)
        self.assertEqual(record['exec_rewrites_us'], 64012)
        self.assertIsNone(record['chunk_maps'])


# A launcher-layout probe's klog, abridged from gate-hang-final-2026-10-16-01-virtual-query64, with the host seconds
# each line arrived at.
STAGE_KLOG = [
    (1.2, '<118>[SceLncService] launchApp(PPSA99170)'),
    (2.1, '[MihawkOS] starts (standard error to klog: yes)'),
    (2.3, '[MihawkOS] elevation: ok (filesystem access)'),
    (3.0, '[MihawkOS] Wine: load status=0 stage=6 module=24803'),
    (3.4, '[MihawkOS] wine-ps5: payload rendezvous native=5302 held=1 errno=0'),
    (3.6, '[MihawkOS] wine-ps5: child ready native=5303 service=0x1'),
    (3.7, '[MihawkOS] Wine: process wineboot.exe command line 1/1: "C:\\windows\\system32\\wineboot.exe" --init'),
    (3.8, '[MihawkOS] wine-ps5: child ready native=5304 service=0x1'),
    (4.0, '[MihawkOS] wine-ps5: child ready native=5305 service=0x1'),
    (6.4, '[MihawkOS] wine-ps5: payload close native=5306 absent=1 polls=3 begin_ms=35775192 end_ms=35775392'),
    (9.6, '[MihawkOS] wine-ps5: child native=5304 completion=0 service=0x1'),
    (9.9, '[MihawkOS] wine-ps5: payload close native=5304 absent=1 polls=4 begin_ms=35778745 end_ms=35779045'),
    (10.0, '[MihawkOS] wine-ps5: child native=5303 completion=0 service=0x1'),
    (11.0, '[MihawkOS] Wine: process completed; returning to launcher'),
    (13.0, '[MihawkOS] starts (standard error to klog: yes)'),
    (13.2, '[MihawkOS] launch: mode=launcher profile= path= cycle=1 argc=2 env=0'),
    (13.9, '[MihawkOS] ends: status 0'),
]


class Stages(unittest.TestCase):
    def test_sources_print_the_stage_lines(self):
        printed('ps5/src/platform.c', 'say("starts (standard error to klog: %s)"')
        printed('ps5/src/main.cpp', 'say("elevation: %s%s", proton_data_status(), data ? " (filesystem access)"',
                'say("ends: status %d", status);')
        printed('native/proton_wine.cpp', 'say("Wine: load status=%d stage=%d module=%d"',
                'say("Wine: process %s command line %zu/%zu: %.*s"', 'say("Wine: process completed; returning to launcher");')
        printed('ps5/src/proton_launch.cpp', 'proton_say("launch: mode=%s profile=%s')
        printed('wine/ps5/pw_wine_process.c', '"wine-ps5: child ready native=%d service=%#x\\n"',
                '"wine-ps5: child native=%d completion=%#x service=%#x\\n"')
        printed('wine/ps5/pw_payload_spawn.h', '"wine-ps5: payload close native=%d absent=1 polls=%u begin_ms=%lld end_ms=%lld\\n"')

    def test_each_stage_at_its_first_line(self):
        marks = {'stages': {}, 'pids': {}}
        for now, line in STAGE_KLOG:
            runner.note_stage(marks, line, now)
        self.assertEqual(marks['stages'], {
            'launched': 1.2, 'title_started': 2.1, 'elevated': 2.3, 'wine_loaded': 3.0, 'wineboot_started': 3.7,
            'wineboot_ended': 9.6, 'program_ended': 10.0, 'wine_ended': 11.0, 'launcher_started': 13.2,
            'launcher_ended': 13.9})
        self.assertEqual(marks['pids'], {'program': '5303', 'wineboot': '5304'})
        # 6.4 - 35775.392 and 9.9 - 35779.045: the smaller difference is the line klog delayed least.
        self.assertAlmostEqual(marks['offset'], 9.9 - 35779.045)
        verdict = '35779.030:0020:0024:warn:debugstr:OutputDebugStringA "PS5_PROTON_VIRTUAL_QUERY PASS: reserved=231'
        self.assertAlmostEqual(runner.console_stage(marks, verdict), 9.885, delta=0.006)
        self.assertIsNone(runner.console_stage(marks, 'wine-child: entry'))
        self.assertIsNone(runner.console_stage({'stages': {}}, verdict))

    def test_title_layout_names_no_program_helper(self):
        marks = {'stages': {}, 'pids': {}}
        for now, line in STAGE_KLOG[:5] + STAGE_KLOG[6:8] + [(9.6, STAGE_KLOG[10][1])]:
            runner.note_stage(marks, line, now)
        self.assertEqual(marks['pids'], {'wineboot': '5304'})
        self.assertNotIn('program_ended', marks['stages'])
        self.assertEqual(marks['stages']['wineboot_ended'], 9.6)


class TitleCrash(unittest.TestCase):
    """A helper's signal is its program's result; only the title's own ends the watch (klog formats of 2026-10-25)."""

    def test_only_the_titles_signal_ends_the_run(self):
        crash = runner.TitleCrash(runner.runner.CRASH.crash)
        self.assertTrue(crash.search('mDBG: Sending signal(pid: 5, tid: 1, signo: 0xb)'))   # no title seen yet: as before
        crash = runner.TitleCrash(crash.crash)
        self.assertFalse(crash.search('<3956> EXEC /app0/eboot.bin [user], vm#1, dmem#1 abi=native category=native_game'))
        self.assertFalse(crash.search('mDBG: Sending signal(pid: 4826, tid: 102007, signo: 0xa0020328)'))
        for line in ('#', '# A user thread receives a fatal signal', '#', '# signal: 11 (SIGSEGV)', '# proc ID: 4826'):
            self.assertFalse(crash.search(line))
        self.assertFalse(crash.search('# A user thread receives a fatal signal'))
        self.assertTrue(crash.search('# proc ID: 3956'))
        self.assertTrue(crash.search('mDBG: Sending signal(pid: 3956, tid: 1, signo: 0xb)'))
        self.assertTrue(crash.search('GPU_FAULT in vm 1'))



class TouchpadAtStart(unittest.TestCase):
    def test_source_prints_the_pad_line(self):
        printed('native/proton_wine.cpp', '"Wine: pad connected=%d%s buttons=%#x touch=%u"')

    def test_a_contact_at_start_is_named(self):
        touched = ['[MihawkOS] Wine: pad connected=1 buttons=0 touch=1\n']
        self.assertEqual(runner.touchpad_touched_at_start(touched), 1)
        # the first pad line counts; a later touch (a pad script's) does not
        later = ['[MihawkOS] Wine: pad connected=1 buttons=0 touch=0\n', '[MihawkOS] Wine: pad connected=1 buttons=0x4 touch=2\n']
        self.assertEqual(runner.touchpad_touched_at_start(later), 0)
        # no pad, or no line at all
        self.assertEqual(runner.touchpad_touched_at_start(['[MihawkOS] Wine: pad connected=0 buttons=0 touch=1\n']), 0)
        # While the system holds the pad (PS menu), the line says so between connected= and buttons= (pw-01)
        self.assertEqual(runner.touchpad_touched_at_start(
            ['[MihawkOS] Wine: pad connected=1 (the system has it: idle to the game) buttons=0 touch=1\n']), 1)
        self.assertEqual(runner.touchpad_touched_at_start([]), 0)


class ProbeVerdicts(unittest.TestCase):
    MARKER = 'PS5_PROTON_D3D9_D3D11_SHARE'

    def test_pass_and_fail_after_the_marker(self):
        self.assertEqual(runner.probe_verdict(self.MARKER, f'{self.MARKER} PASS\n'), 'PASS')
        self.assertEqual(runner.probe_verdict(self.MARKER, f'[x] {self.MARKER} FAIL: no device\n'), 'FAIL')

    def test_a_named_result_is_a_verdict_of_its_own(self):
        line = f'{self.MARKER} NOSHARE: Direct3D 11 could not open the shared handle\n'
        self.assertEqual(runner.probe_verdict(self.MARKER, line), 'NOSHARE')
        self.assertNotEqual(runner.probe_verdict(self.MARKER, line), 'PASS')
        # it does not count as the probe having passed
        self.assertFalse(runner.probe_completed('NOSHARE', None))

    def test_step_lines_carry_no_verdict(self):
        for step in (f'{self.MARKER} d3d11-open-shared hr=80070057\n', f'{self.MARKER} order=d3d11-first\n',
                     f'{self.MARKER} d3d9-shared-handle none\n', 'unrelated line\n'):
            self.assertIsNone(runner.probe_verdict(self.MARKER, step))

    def test_a_name_belongs_to_its_own_marker(self):
        # NOSHARE is not a result of other probes: for them the line carries no verdict, as before
        self.assertIsNone(runner.probe_verdict('PS5_PROTON_D3D12_SHARED', 'PS5_PROTON_D3D12_SHARED NOSHARE\n'))
        self.assertEqual(runner.probe_verdict('PS5_PROTON_D3D12_SHARED', 'PS5_PROTON_D3D12_SHARED PASS sharing=no\n'), 'PASS')

    def test_the_probe_prints_the_names_the_runner_reads(self):
        printed('tests/d3d9ex-d3d11-share-probe.c', '"NOSHARE"', 'MARKER " %s%s%s\\n"')
        for marker, names in runner.NAMED_VERDICTS.items():
            printed('tests/d3d9ex-d3d11-share-probe.c', marker)
            self.assertTrue(all(name in (ROOT / 'tests/d3d9ex-d3d11-share-probe.c').read_text() for name in names))


if __name__ == '__main__':
    unittest.main()
