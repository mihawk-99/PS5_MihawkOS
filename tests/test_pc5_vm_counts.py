#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Wine patch 2412 (the process memory counts are kept for a second). ntdll's virtual.c cannot be built on the host, but
the function the patch changes can be run: the text of virtual_fill_vm_counters is cut out of the patch itself, as it
reads before and after, and compiled against stand-ins for the views, the mutex and the clock
(tests/pc5_vm_counts_harness.c). Two checks:
 - values: the six counts, the peaks, and when a walk happens;
 - contention: threads asking every 10 ms while another thread takes the same mutex an allocation takes. The cost of a
   walk here is a model (300 views of 16 ranges, a few hundred microseconds), not the console's, so this shows the shape
   of the cost, which is what the cache removes, and not its size on a console (that is what the console run measures)."""
import re
import subprocess
import unittest
from pathlib import Path

import pc5_support as support

PATCH = next((support.ROOT / 'wine/patches').glob('2412-*.patch'))


def function_text(after, directory=None):
    """The function as patch 0905 added it (after=False), or with patch 2412 applied to that (after=True). The block is
    cut out of the two patches: 0905's added lines give the old text, and `git apply` puts 2412's hunks on it."""
    added = []
    in_file = in_hunk = False
    for line in (support.ROOT / next(iter(sorted((support.ROOT / 'wine/patches').glob('0905-*.patch'))))).read_text().split('\n') \
            if False else next(iter(sorted((support.ROOT / 'wine/patches').glob('0905-*.patch')))).read_text().split('\n'):
        if line.startswith('diff --git '):
            in_file = line.endswith('dlls/ntdll/unix/virtual.c')
            in_hunk = False
        elif in_file and line.startswith('@@'):
            in_hunk = False
            added.append([])
        elif in_file and added and line.startswith('+') and not line.startswith('+++'):
            added[-1].append(line[1:])
            in_hunk = True
    block = next('\n'.join(h) + '\n' for h in added if any('virtual_fill_vm_counters( VM_COUNTERS_EX' in l for l in h))
    block = block[block.index('/****'):]
    if not after:
        return block[:block.rindex('#endif')]
    with support.scratch() as tmp:
        work = Path(tmp)
        (work / 'virtual.c').write_text(block)
        diff = PATCH.read_text()
        diff = diff[diff.index('diff --git'):].replace('dlls/ntdll/unix/virtual.c', 'virtual.c')
        (work / 'p.diff').write_text(diff)
        done = subprocess.run(['git', 'apply', '--whitespace=nowarn', 'p.diff'], cwd=work, capture_output=True, text=True)
        if done.returncode:
            raise AssertionError('patch 2412 does not apply to the function patch 0905 added:\n' + done.stderr)
        text = (work / 'virtual.c').read_text()
        return text[:text.rindex('#endif')]


class VmCounts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = support.scratch()
        cls.work = Path(cls.tmp.name)
        cls.exe = {}
        for name, after in (('old', False), ('new', True)):
            directory = cls.work / name
            directory.mkdir()
            (directory / 'counts.inc').write_text(function_text(after))
            flags = [f'-I{directory}', f'-I{support.ROOT}/tests', '-pthread'] + ([] if after else ['-DOLD'])
            built = subprocess.run(['cc', '-O1', '-w', *flags, '-o', str(directory / 'harness'),
                                    str(support.ROOT / 'tests/pc5_vm_counts_harness.c')], capture_output=True, text=True)
            if built.returncode:
                raise AssertionError(built.stderr[-2000:])
            cls.exe[name] = directory / 'harness'

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def run_harness(self, name, *args):
        result = subprocess.run([str(self.exe[name]), *args], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, name + ': ' + result.stdout + result.stderr)
        return result.stdout

    def test_values_before_and_after(self):
        self.assertIn('passed', self.run_harness('old', 'values'))
        self.assertIn('passed', self.run_harness('new', 'values'))

    def test_the_allocator_waits_less(self):
        def p99(out):
            return float(re.search(r'p99 ([\d.]+) ms', out).group(1))

        old = self.run_harness('old', 'contend', '4', '3')
        new = self.run_harness('new', 'contend', '4', '3')
        print('\n  old:', old.strip(), '\n  new:', new.strip())
        self.assertLess(p99(new), p99(old), 'the cache did not shorten the allocator\'s waits')
        walks_old = int(re.search(r'took the mutex (\d+) times', old).group(1))
        walks_new = int(re.search(r'took the mutex (\d+) times', new).group(1))
        self.assertLess(walks_new * 10, walks_old, 'the cache still walked as often')


if __name__ == '__main__':
    unittest.main()
