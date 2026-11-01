#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run one PE probe or one installed game on the console and summarise it.

  tools/run-console.py --exe wine-probe.exe [--marker PS5_PROTON_PE64_PROBE]
  tools/run-console.py --profile 1 --seconds 60
  ... [--env NAME=VALUE]... [--name RUN] [--exit-stall 15]

The run goes through the title's own paths only: a one-shot wine-run.txt
selects the game (and test-only env= overrides, which never outlive the
launch), test-run.txt makes the launcher draw ten frames and print its
"ends" line once the game has returned, and stop-game asks a running game to
go back to the launcher. It ends at the first of: that "ends" line, a crash
record, the title no longer running, or the watchdog. A probe that printed
its verdict but has not returned to the launcher within --exit-stall seconds
is an exit stall: the title is asked to return (stop-game) and the run is
reported as failing.

Every run gets PROTON_STOP_TERMINATE=0 unless --env names it. That only turns
off the server fallback: a game in helpers that has not closed 10 s after
stop-game is still ended by SIGKILL to its helpers (native/proton_wine.cpp).
A --profile the console's library does not hold is refused before anything is
written. A run also fails when its klog shows storage damage (which blocks
every later launch, tools/console_guard.py), when the title did not report its
session-log rotation with status 0, when a helper it started left no log in
this session's folder, when the memory helper stopped answering, a helper
asserted, the kernel placed a mapping over the section arena, the payload SDK
was asked for execute, or a helper's threads did not get their scheduling.

--expect-ended: the probe holds on after its verdict and must be ended by the
title's SIGKILL (src/pw_hang_breaker.h); with PROTON_TEST_BROKER_HOLD_S, by the
flight recorder's broker-wedge rule, the held call named in its log, and its
helper may have ended itself just before, its GPU client having given the
broker up.
--sequence FILE: with --exe gate-sequence.exe, the probes one launch runs, each
its own process (tools/run-battery.py --grouped judges each from its window).
--expect-direct-section: a section in direct memory, mapped a chunk at a time
and given back whole. A run that injects a fault (PROTON_MEMORY_TEST_STALL,
WINE_PS5_ASSERT_PROBE=1, WINETEST_DOOM_AFTER_MS, PROTON_GPU_TEST_FAIL_SUBMIT)
passes when the fault took its bounded path instead of on the probe's verdict.
Any other run, PROTON_GPU_TEST_FAIL_SUBMIT's too (an answered failure is no lost
broker), fails when a helper's backstop had to end it (src/pw_control_watch.h).

Outputs: klog/<RUN>.log and klog/<RUN>-wine.log (ignored), and a small
evidence/<RUN>.json with the verdict and the runtime counters' intervals
(wine/ps5/pw_wine_perf.h), which is committed.
"""
import argparse
import ftplib
import io
import json
import re
import socket
import statistics
import subprocess
import sys
import threading
import time
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT.parent / 'PS5_Vulkan/tools'
sys.path.insert(0, str(TOOLS))
import ps5_console  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools'))
import console_guard  # noqa: E402
import helper_sched  # noqa: E402
import start_stages  # noqa: E402

spec = importlib.util.spec_from_file_location('run_title', TOOLS / 'run-title.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class TitleCrash:
    """run-title's crash lines, for the title's own process only. A Windows program runs in a payload helper of its own,
    and a test that crashes one (i386 quartz avisplit, 2026-10-25: 'mDBG: Sending signal(pid: <helper>') is that
    program's result, which Wine reports; it ended the watch of a title that went on running 900 files. The kernel's
    banner names its process a few lines later ('# proc ID: N'), so the banner waits for it."""

    def __init__(self, crash):
        self.crash, self.title, self.banner = crash, None, False

    def search(self, line):
        found = re.match(r'<(\d+)> EXEC /app0/', line)
        if found:
            self.title = found.group(1)
        if self.title is None:
            return self.crash.search(line)
        if 'A user thread receives a fatal signal' in line:
            self.banner = True
            return None
        found = re.match(r'# proc ID: (\d+)', line) if self.banner else None
        if found:
            self.banner = False
            return found if found.group(1) == self.title else None
        found = re.search(r'mDBG: Sending signal\(pid: (\d+)', line)
        if found:
            return found if found.group(1) == self.title else None
        return self.crash.search(line)


runner.CRASH = TitleCrash(runner.CRASH)

TITLE = 'PPSA99170'
BASE = f'/data/homebrew/{TITLE}'
DATA = '/data/ps5proton'
# Equals PW_LOG_SESSIONS_ROOT (src/pw_log_sessions.h): <SESSIONS>/0 holds this run's helper logs.
SESSIONS = f'{DATA}/logs/sessions'
# tests/gate-sequence-probe.c reads its list here, in the probe prefix every --exe runs in (C:\\).
SEQUENCE = f'{DATA}/prefixes/probe/drive_c/gate-sequence.txt'
SESSION_STATUS = re.compile(r'Wine: session logs status=(-?\d+)')
MARKERS = {'wine-probe.exe': 'PS5_PROTON_PE64_PROBE', 'gate-sequence.exe': 'PS5_PROTON_GATE_SEQUENCE', 'pe32-probe.exe': 'PS5_PROTON_PE32_PROBE',
           'cross32-probe.exe': 'PS5_PROTON_CROSS_PROBE', 'cross64-probe.exe': 'PS5_PROTON_CROSS_PROBE',
           'wow64-probe.exe': 'PS5_PROTON_WOW64_PROBE', 'd3d11-probe.exe': 'PS5_PROTON_D3D11_PROBE', 'gpu-host-memory-probe.exe': 'PS5_PROTON_GPU_HOST_MEMORY', 'section-memory-probe.exe': 'PS5_PROTON_SECTION_MEMORY',
           'xinput-probe.exe': 'PS5_PROTON_XINPUT_PROBE', 'hid-probe.exe': 'PS5_PROTON_HID_PROBE', 'hid32-probe.exe': 'PS5_PROTON_HID_PROBE',
           'vk32-probe.exe': 'PS5_PROTON_VK_PROBE', 'vk64-probe.exe': 'PS5_PROTON_VK_PROBE',
           'gl-vertex32-probe.exe': 'PS5_PROTON_GL_VERTEX_PROBE', 'd3d11-32-probe.exe': 'PS5_PROTON_D3D11_PROBE',
           'd3d12-levels-probe.exe': 'PS5_PROTON_D3D12_LEVELS', 'd3d12-sparse-probe.exe': 'PS5_PROTON_D3D12_SPARSE', 'many-files-probe.exe': 'PS5_PROTON_MANY_FILES', 'log-flood-probe.exe': 'PS5_PROTON_LOG_FLOOD', 'd3d12-shared-probe.exe': 'PS5_PROTON_D3D12_SHARED', 'd3d12-existing-heap-probe.exe': 'PS5_PROTON_D3D12_EXISTING_HEAP',
           'd3d9ex-d3d11-share-probe.exe': 'PS5_PROTON_D3D9_D3D11_SHARE', 'osk-typed-probe.exe': 'PS5_PROTON_OSK_TYPED',
           'sync-probe.exe': 'PS5_PROTON_SYNC_PROBE', 'exit-probe.exe': 'PS5_PROTON_EXIT_PROBE',
           'priority-probe.exe': 'PS5_PROTON_PRIORITY_PROBE', 'hang-probe.exe': 'PS5_PROTON_HANG_PROBE', 'window-probe.exe': 'PS5_PROTON_WINDOW_PROBE', 'fs-probe.exe': 'PS5_PROTON_FS_PROBE', 'stack-probe.exe': 'PS5_PROTON_STACK_PROBE', 'gl-exit-probe.exe': 'PS5_PROTON_GL_EXIT_PROBE', 'mem-probe.exe': 'PS5_PROTON_MEM_PROBE', 'gl-vertex-probe.exe': 'PS5_PROTON_GL_VERTEX_PROBE', 'gl-present-probe.exe': 'PS5_PROTON_GL_PRESENT_PROBE', 'gl-sprite-probe.exe': 'PS5_PROTON_GL_SPRITE_PROBE', 'vm-protect-probe.exe': 'PS5_PROTON_VM_PROTECT_PROBE', 'vm-protect32-probe.exe': 'PS5_PROTON_VM_PROTECT_PROBE', 'remote-memory-probe.exe': 'PS5_PROTON_REMOTE_MEMORY_PROBE', 'stdout-probe.exe': 'PS5_PROTON_STDOUT_PROBE', 'crash-probe.exe': 'PS5_PROTON_CRASH_PROBE', 'nt-file-probe.exe': 'PS5_PROTON_NT_FILE_PROBE', 'registry-flush64-probe.exe': 'PS5_PROTON_REGISTRY_FLUSH', 'file-write32-probe.exe': 'PS5_PROTON_FILE_WRITE', 'file-write64-probe.exe': 'PS5_PROTON_FILE_WRITE', 'relative-dir32-probe.exe': 'PS5_PROTON_NT_RELDIR_PROBE', 'relative-dir64-probe.exe': 'PS5_PROTON_NT_RELDIR_PROBE', 'file-cost32-probe.exe': 'PS5_PROTON_FILE_COST', 'file-cost64-probe.exe': 'PS5_PROTON_FILE_COST', 'handles-paths32-probe.exe': 'PS5_PROTON_HANDLES_PATHS', 'handles-paths64-probe.exe': 'PS5_PROTON_HANDLES_PATHS', 'pipe-cancel32-probe.exe': 'PS5_PROTON_PIPE_CANCEL', 'pipe-cancel64-probe.exe': 'PS5_PROTON_PIPE_CANCEL', 'lock32-probe.exe': 'PS5_PROTON_LOCK', 'lock64-probe.exe': 'PS5_PROTON_LOCK', 'continue64-probe.exe': 'PS5_PROTON_CONTINUE', 'stack-limit64-probe.exe': 'PS5_PROTON_STACK_LIMIT', 'syscall-cost32-probe.exe': 'PS5_PROTON_SYSCALL_COST', 'syscall-cost64-probe.exe': 'PS5_PROTON_SYSCALL_COST', 'net-throughput32-probe.exe': 'PS5_PROTON_NET_THROUGHPUT', 'net-throughput64-probe.exe': 'PS5_PROTON_NET_THROUGHPUT', 'thread-leak-probe.exe': 'PS5_PROTON_THREAD_LEAK_PROBE'}
PERF = {
    'frames': re.compile(r'perf: (\w+) frames=(\d+) fps=([\d.]+) p50=([\d.]+) p95=([\d.]+) p99=([\d.]+) '
                         r'max=([\d.]+) ms present_avg=([\d.]+) present_max_all=([\d.]+)'),
    'server': re.compile(r'perf: server calls=(\d+) per_s=(\d+) avg_us=([\d.]+) selects=(\d+) blocked=(\d+) '
                         r'block_avg_us=([\d.]+) waits=(\d+) handles_avg=([\d.]+) all=(\d+) polled=(\d+) '
                         r'delays=(\d+) yields=(\d+)'),
    'top': re.compile(r'perf: top (.*)'),
    'first': re.compile(r'perf: first (\w+) frame (\d+) ms after Wine started'),
}


# Where a run's seconds go: each stage's first line, in host seconds since the launch request, as klog delivered it.
STAGES = {'title_started': '[MihawkOS] starts', 'elevated': '[MihawkOS] elevation: ok',
          'wine_loaded': 'Wine: load status=0', 'wineboot_started': 'Wine: process wineboot.exe command line',
          'wine_ended': 'Wine: process completed; returning to launcher', 'launcher_started': 'launch: mode=launcher',
          'launcher_ended': '[MihawkOS] ends: status'}
READY = re.compile(r'child ready native=(\d+)')
COMPLETED = re.compile(r'child native=(\d+) completion=')
END_MS = re.compile(r' end_ms=(\d+)')
WINE_TIME = re.compile(r'(\d+\.\d{3}):[\da-f]{4}:')


def note_stage(marks, line, now):
    """One klog line seen at host time now: each stage's first time; the program's helper (the first one ready before
    wineboot starts) and wineboot's, and when each completed; and the console clock's offset from the host's, from the
    ms stamps of payload close lines (klog only delays a line, so the smallest difference is the closest)."""
    stages, pids = marks['stages'], marks['pids']
    stages.setdefault('launched', now)
    for stage, text in STAGES.items():
        if text in line:
            stages.setdefault(stage, now)
    if (m := READY.search(line)):
        pids.setdefault('wineboot' if 'wineboot_started' in stages else 'program', m.group(1))
    elif (m := COMPLETED.search(line)):
        for who in ('program', 'wineboot'):
            if pids.get(who) == m.group(1):
                stages.setdefault(who + '_ended', now)
    if (m := END_MS.search(line)):
        offset = now - int(m.group(1)) / 1000
        marks['offset'] = min(marks.get('offset', offset), offset)


def console_stage(marks, line):
    """A helper log line's Wine timestamp (+timestamp, the console's clock) in host seconds, or None."""
    m = WINE_TIME.match(line)
    return round(float(m.group(1)) + marks['offset'], 2) if m and 'offset' in marks else None


def connect(settings, timeout=20):
    ftp = ftplib.FTP()
    ftp.connect(settings['host'], settings['ftp_port'], timeout=timeout)
    ftp.login(settings['ftp_user'], settings['ftp_password'])
    return ftp


def remove(ftp, path):
    try:
        ftp.delete(path)
    except ftplib.error_perm as error:
        if not str(error).startswith('550'):
            raise
    except ftplib.error_reply as error:
        # The console's FTP server answers a delete with "226 File deleted", not the 250 ftplib expects.
        if not str(error).startswith('226'):
            raise


def idle(settings):
    return ' count=0 ' in ps5_console.ps5vkctl_command(settings, 'procs') + ' '


# A probe that measures something can end on a named result that is neither PASS nor FAIL: the line says what it saw.
# Without the name the runner read "no verdict" (and never started the exit-stall clock). Such a result is recorded as
# the probe's verdict and does not pass; it is not a failure of the run to finish.
NAMED_VERDICTS = {
    'PS5_PROTON_D3D9_D3D11_SHARE': ('NOSHARE',),   # tests/d3d9ex-d3d11-share-probe.c: a shared texture the other device cannot open
}


def probe_verdict(marker, line):
    """The verdict one probe line carries, or None: PASS or FAIL after the marker, or a name NAMED_VERDICTS gives it."""
    if f'{marker} PASS' in line:
        return 'PASS'
    if f'{marker} FAIL' in line:
        return 'FAIL'
    for name in NAMED_VERDICTS.get(marker, ()):
        if f'{marker} {name}' in line:
            return name
    return None


def probe_completed(verdict, stop_requested):
    # A watchdog stop can change the workload (e.g. disconnecting input unblocks a stuck message loop).
    # A PASS printed afterward is not an autonomous probe completion.
    return verdict == 'PASS' and not stop_requested


def touchpad_touched_at_start(lines):
    """The physical pad's touchpad count at the title's first pad line ("Wine: pad connected=1 buttons=... touch=N"), or
    0. A contact there drives the title's touchpad-as-mouse, so every probe's parked cursor moves (17 cases of
    alpha5-pro-2026-10-29-02, touch=1 in each): such a run says so and fails, instead of failing as a probe."""
    for line in lines:
        match = re.search(r'Wine: pad connected=(\d+)(?: \([^)]*\))? buttons=\S+ touch=(\d+)', line)
        if match:
            return int(match.group(2)) if match.group(1) == '1' else 0
    return 0


def native_close_failures(text):
    """An owner completion marker cannot erase a native payload close timeout."""
    failures = set()
    for pid, status in re.findall(r'child native=(\d+) service=\S+ closed=(0[xX][\da-fA-F]+|-?\d+)(?=\s|$)', text):
        value = int(status, 16 if status.lower().startswith('0x') else 10)
        if value:
            failures.add((int(pid), value))
    return [{'pid': pid, 'status': status} for pid, status in sorted(failures)]


EXEC_KEYS = ('refused', 'conversions', 'converted_kib', 'convert_failed', 'exec_refused', 'grant_retries', 'modules',
             'sdk_refused')
EXEC_TALLY = re.compile('wine-child: executable protection ' + ' '.join(key + r'=(\d+)' for key in EXEC_KEYS))
EXEC_ADOPTED = 'payload SDK memory adopted; execute through JIT memory'


def exec_protection(texts):
    """Each helper's last tally (native/proton_wine_child.c), summed; None if none printed one."""
    record = None
    for text in texts:
        found = EXEC_TALLY.findall(text)
        if found:
            record = record or dict.fromkeys(EXEC_KEYS, 0)
            for key, value in zip(record, found[-1]):
                record[key] += int(value)
    return record


HELPER_IMAGE = re.compile(r'trace:loaddll:build_module Loaded L"([^"]*\\)?([^"\\]+\.exe)" at \S+: native')
HELPER_RESIDENT = re.compile(r'wine-child: memory resident=(\d+) MiB .*? images=(\d+) holding (\d+) MiB')
HELPER_VM = re.compile(r'wine-child: vm resident=(\d+) MiB direct_resident=(\d+)(?: reserved_resident=\d+)? flexible=(\d+)')


def helper_rows(texts):
    """One row per helper log, not summed (docs/MIHAWK_BROWSER.md C0: what one CEF process costs): its program, whether
    it loaded libcef.dll, its census's peak and last flexible resident memory, its last vm line, and its last
    executable-protection tally."""
    rows = []
    for text in texts:
        image = HELPER_IMAGE.search(text)
        resident = [int(m.group(1)) for m in HELPER_RESIDENT.finditer(text)]
        held = HELPER_RESIDENT.findall(text)
        vm = HELPER_VM.findall(text)
        tally = EXEC_TALLY.findall(text)
        rows.append({'program': image.group(2) if image else None, 'libcef': 'libcef.dll' in text,
                     'resident_peak_mib': max(resident, default=None), 'resident_last_mib': resident[-1] if resident else None,
                     'images_held_mib': int(held[-1][2]) if held else None,
                     'vm_last': dict(zip(('resident_mib', 'direct_resident_mib', 'flexible_mib'), map(int, vm[-1]))) if vm else None,
                     'exec': dict(zip(EXEC_KEYS, map(int, tally[-1]))) if tally else None})
    return rows


CODE_SHARE_KEYS = ('shared', 'shared_kib', 'offered', 'mismatched', 'failed', 'private_writes', 'private_kib')
CODE_SHARE_TALLY = re.compile('wine-child: shared code copies ' + ' '.join(key + r'=(\d+)' for key in CODE_SHARE_KEYS))


def code_share(texts):
    """Shared code copies (wine/ps5/pw_wine_codeshare.h): each helper's last tally, summed; None if none printed one. A
    recorded fact, never a failure: a failed ask turns sharing off in that helper and it copies privately, as before."""
    record = None
    for text in texts:
        found = CODE_SHARE_TALLY.findall(text)
        if found:
            record = record or dict.fromkeys(CODE_SHARE_KEYS, 0)
            for key, value in zip(CODE_SHARE_KEYS, found[-1]):
                record[key] += int(value)
    return record


FOREIGN_KEYS = ('found', 'skipped_unmaps', 'refused_maps', 'query_failed', 'holes')
FOREIGN_TALLY = re.compile('wine-child: foreign mappings ' + ' '.join(key + r'=(\d+)' for key in FOREIGN_KEYS) + '(.*)')
FOREIGN_HOLE = re.compile(r' at=(0x[\da-f]+|0)\+(0x[\da-f]+|0)/([\da-f]+) skips=(\d+) refusals=(\d+) kind=(\S+)')


def foreign_mappings(texts):
    """Kernel mappings found inside Wine's own space that are not Wine's (wine/ps5/pw_wine_dmem.h): each helper's last
    tally, summed, and the holes it named. A recorded fact, never a failure: a skip or a refusal is the guard working.
    None if no helper printed one."""
    record = None
    for text in texts:
        found = FOREIGN_TALLY.findall(text)
        if not found:
            continue
        record = record or {**dict.fromkeys(FOREIGN_KEYS, 0), 'hole_list': []}
        *values, rest = found[-1]
        for key, value in zip(FOREIGN_KEYS, values):
            record[key] += int(value)
        for address, size, protection, skips, refusals, kind in FOREIGN_HOLE.findall(rest):
            record['hole_list'].append({'address': address, 'bytes': int(size, 16), 'protection': int(protection, 16),
                                        'skips': int(skips), 'refusals': int(refusals), 'kind': kind})
    return record


def exec_clean(record):
    """Execute came from JIT memory only: no conversion failed, no execute fault Wine allowed went unserved, and nothing
    asked the payload SDK, whose grant writes the kernel's map entry without its lock."""
    return not record or not (record['convert_failed'] or record['exec_refused'] or record['sdk_refused'])


def exec_tally_missing(texts):
    """A helper that adopted the payload memory but printed no tally did not end through its loader, unless its backstop
    ended it, which prints none (helper_doom judges that)."""
    return any(EXEC_ADOPTED in text and not EXEC_TALLY.search(text) and not DOOMED.search(text) for text in texts)


DOOM_SEEN = re.compile(r'wine-child: payload doom seen reason=(\w+)')
DOOMED = re.compile(r'wine-child: payload doomed reason=(\w+) stamped_ms_ago=(-?\d+)')
PAYLOAD_ENDING = 'wine-child: payload ending'
# Each reason's grace before the loader ends its helper (src/pw_control_watch.h, pw_control_doom_grace_ms).
DOOM_GRACE_MS = {'gpu': 5000, 'exit': 20000, 'engine': 5000, 'test': 5000, 'stall': 5000}


def helper_doom(texts):
    """The helpers' backstop (src/pw_control_watch.h, native/proton_wine_child.c): for each helper log that saw a doom
    reason, the reasons, the one it was ended for and that stamp's age then, and whether the loader went on to its
    ending. None when no helper saw one."""
    helpers = []
    for text in texts:
        seen = list(DOOM_SEEN.finditer(text))
        if not seen:
            continue
        doomed = DOOMED.search(text)
        helpers.append({'seen': [m.group(1) for m in seen], 'doomed': doomed.group(1) if doomed else None,
                        'stamped_ms_ago': int(doomed.group(2)) if doomed else None,
                        'ended': PAYLOAD_ENDING in text[(doomed or seen[-1]).end():]})
    return helpers or None


def doom_ended(record, reason):
    """Some helper saw reason, and each that did reached its loader's ending, doomed for that reason once its grace was
    up (the loop looks every 100 ms; a second is allowed)."""
    mine = [helper for helper in record or [] if reason in helper['seen']]
    for helper in mine:
        if not helper['ended'] or helper['doomed'] != reason or \
                not 0 <= helper['stamped_ms_ago'] - DOOM_GRACE_MS[reason] <= 1000:
            return False
    return bool(mine)


def backstop_allowed(fault, record):
    """A helper's backstop ends it only for what the run injects: the test stamp, or the broker given up on (the broker
    wedge, whose held submit gets no answer and gives up 15 s into the hold, so the 5 s grace ends near the recorder's
    20th busy sample by design and either may end the helper first), each within its grace; any other run, never. The
    GPU reason is stamped only when the broker client gives the broker up (graphics patch 0017): in gpu-loss the broker
    answers a submission with a failure, which is not that, so it ends no helper there either. Outside the wedge no
    helper may even see the GPU reason: in gate-hang-a-2026-10-15-01 gpu-window32/64 saw it at refusals the broker
    answered and stamped their exit 1.8 and 1.9 s later, inside the grace, where a game slower to rebuild is ended.
    No run may see the stall reason (Wine's virtual mutex or the engine lock held 3 s, src/pw_control_watch.h): a run
    whose helper needed it failed, whatever it injected; helper_doom keeps it in the evidence."""
    doomed = [helper for helper in record or [] if helper['doomed']]
    if any('stall' in helper['seen'] for helper in record or []):
        return False
    if fault == 'broker-wedge':
        return all(helper['doomed'] == 'gpu' and 0 <= helper['stamped_ms_ago'] - DOOM_GRACE_MS['gpu'] <= 1000
                   for helper in doomed)
    if any('gpu' in helper['seen'] for helper in record or []):
        return False
    if fault == 'doom':
        return doom_ended(record, 'test') and all(helper['doomed'] == 'test' for helper in doomed)
    return not doomed


ENDING = re.compile(r'Wine: (.+): ending (\d+) helpers by SIGKILL, not through the server')
KILLED = re.compile(r"Wine: helper (\d+): SIGKILL (sent|refused|found it gone already|not sent: not a helper.s pid) "
                    r"\(errno (-?\d+)\)")
GONE = re.compile(r'Wine: helper (\d+) gone (\d+) ms after SIGKILL')
STILL_THERE = re.compile(r'Wine: process (\d+) is still there')
AFTER_KILL = re.compile(r'Wine: after the kill: census (-?\d+) ms old, frames \+(\d+) in (\d+) ms')
STALLED = re.compile(r'Wine: hang breaker: stalled: no frame for (\d+) ms and the system memory census (\d+) ms old')
RECORDER_ROWS = re.compile(r'flight recorder: (process rows .*)')
WEDGE = re.compile(r'flight recorder: wedge broker_busy_s=(\d+)')
WEDGE_S = 20   # PW_HANG_WEDGE_S (src/pw_hang_breaker.h)


def helpers_ended(lines):
    """The title's SIGKILL ending of a game's helpers (native/proton_wine.cpp end_helpers), the hang breaker's verdict,
    the flight recorder's broker wedge and its one line on process rows; None when the title ended nothing and neither
    the breaker nor the recorder saw a stall."""
    record = {'why': None, 'count': 0, 'signals': {}, 'gone_ms': {}, 'still_there': [], 'after_kill': None,
              'stalled': None, 'wedge_s': None, 'recorder_rows': None}
    for line in lines:
        if (m := ENDING.search(line)):
            record['why'], record['count'] = m.group(1), int(m.group(2))
        elif (m := KILLED.search(line)):
            record['signals'][int(m.group(1))] = {'outcome': m.group(2), 'errno': int(m.group(3))}
        elif (m := GONE.search(line)):
            record['gone_ms'][int(m.group(1))] = int(m.group(2))
        elif (m := STILL_THERE.search(line)):
            record['still_there'].append(int(m.group(1)))
        elif (m := AFTER_KILL.search(line)):
            record['after_kill'] = dict(zip(('census_ms', 'frames', 'ms'), map(int, m.groups())))
        elif (m := STALLED.search(line)):
            record['stalled'] = record['stalled'] or {'frame_ms': int(m.group(1)), 'census_ms': int(m.group(2))}
        elif (m := WEDGE.search(line)):
            record['wedge_s'] = int(m.group(1))
        elif (m := RECORDER_ROWS.search(line)):
            record['recorder_rows'] = m.group(1)
    if not record['why'] and not record['stalled'] and not record['wedge_s'] and not record['recorder_rows']:
        return None
    return record


def broker_wedge_ended(record):
    """The flight recorder ended the game once the GPU broker's client lock had been held WEDGE_S samples in a row, and
    every helper it sent SIGKILL is gone (ended_cleanly)."""
    return bool(record and (record['wedge_s'] or 0) >= WEDGE_S and
                (record['why'] or '').startswith("the GPU broker's client lock held") and ended_cleanly(record))


PHASE = re.compile(r' phase=([\w-]+):(\d+) tid=-?\d+')


def broker_phases(lines):
    """The calls the broker's client-lock holder waited in (graphics patch 0017), from the flight recorder's samples
    while that lock was held: each one's longest wait in ms. None when no sample named one."""
    phases = {}
    for line in lines:
        if (m := PHASE.search(line)):
            phases[m.group(1)] = max(phases.get(m.group(1), 0), int(m.group(2)))
    return phases or None


def ended_cleanly(record):
    """Every helper the title sent SIGKILL is gone (only ESRCH proves it, src/pw_hang_breaker.h), none stayed, and the
    console's payload processes answered right after: the memory helper's census was fresh."""
    if not record or record['count'] < 1:
        return False
    signalled = [pid for pid, signal in record['signals'].items() if not signal['outcome'].startswith('not sent')]
    after = record['after_kill']
    return (bool(signalled) and all(pid in record['gone_ms'] for pid in signalled) and not record['still_there'] and
            after is not None and 0 <= after['census_ms'] < 1000)


MEMORY_DEAD = re.compile(r'wine-ps5: memory helper did not answer (\w+) \S+ \S+: errno (-?\d+), waited (\d+) ms')
MEMORY_SLOWEST = re.compile(r'wine-ps5: memory helper slowest answers this session \(ms\):((?: \w+=\d+)+)')
STALL_TEST = 'wine-ps5: memory helper: test stall before direct-memory chunk'
DIRECT_SECTION = re.compile(r'wine-ps5: section of (\d+) MiB in direct memory')
GAVE_BACK = re.compile(r'wine-ps5: direct-memory sections gave back (\d+) chunks \((\d+) lost')


def memory_helper(lines):
    """The memory helper as the title saw it (wine/ps5/pw_wine_process.c), from the title's klog: the request that marked
    it dead (none while it answers), its slowest answers this session, the stall tests injected, the large sections
    made in direct memory and the chunks the collector gave back or lost."""
    record = {'dead': [], 'slowest': None, 'stall_tests': 0, 'sections_mib': [], 'given_back': 0, 'lost': 0,
              'gave_back_lines': 0}
    for line in lines:
        if (m := MEMORY_DEAD.search(line)):
            record['dead'].append({'request': m.group(1), 'errno': int(m.group(2)), 'waited_ms': int(m.group(3))})
        elif (m := MEMORY_SLOWEST.search(line)):
            record['slowest'] = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)', m.group(1))}
        elif STALL_TEST in line:
            record['stall_tests'] += 1
        elif (m := DIRECT_SECTION.search(line)):
            record['sections_mib'].append(int(m.group(1)))
        elif (m := GAVE_BACK.search(line)):
            record['given_back'] += int(m.group(1))
            record['lost'] += int(m.group(2))
            record['gave_back_lines'] += 1
    return record


DIRECT_SECTIONS = re.compile(r'wine-child: direct sections chunk_maps=(\d+) mapped_mib=(\d+) retries=(\d+) asks=(\d+) '
                             r'remembered=(\d+) ask_failures=(\d+) misplaced=(\d+)')
ASSERTION = re.compile(r'wine-ps5: child native=\d+ completion=0xc0000420')
REP_STEP = re.compile(r'PS5_PROTON_VM_PROTECT_PROBE stepped: rep movsb of 4096 bytes beside read-only pages took (\d+) us')
EXEC_REWRITES = re.compile(r'PS5_PROTON_VM_PROTECT_PROBE exec: E1-E7 passed; 1000 rewrites of a code page .* took (\d+) us')


def wine_robustness(klog, runtime):
    """Patches 1122-1126 in a run: whether a helper asserted (it is then ended with STATUS_ASSERTION_FAILURE, never
    left holding Wine's virtual mutex), the helpers' direct-section census (each value its highest, a helper's
    counters only grow), the vm-protect probe's stepped rep string and its 1000 code-page rewrites (E1)."""
    census = [tuple(map(int, m.groups())) for line in runtime if (m := DIRECT_SECTIONS.search(line))]
    top = [max(values) for values in zip(*census)] if census else None
    steps = [int(m.group(1)) for line in klog + runtime if (m := REP_STEP.search(line))]
    rewrites = [int(m.group(1)) for line in klog + runtime if (m := EXEC_REWRITES.search(line))]
    return {'assertion': any(ASSERTION.search(line) for line in klog),
            'chunk_maps': top[0] if top else None, 'mapped_mib': top[1] if top else None,
            'ask_failures_max': top[5] if top else None, 'misplaced_max': top[6] if top else None,
            'rep_step_us': max(steps) if steps else None, 'exec_rewrites_us': max(rewrites) if rewrites else None}


def telemetry_hold_passed(pid, text, native_states, expected_pids, elapsed):
    """Conservative deadline: time from before launch through the absence reply.

    The hold starts after launch, so an entire run shorter than the hold proves
    absence before expiry without comparing clocks on different machines.
    """
    return bool(pid and 0 < elapsed < 60 and
                f'telemetry hold begin pid={pid} duration_ms=60000' in text and
                'telemetry hold expired' not in text and
                'PS5_PROTON_WINDOW_PROBE: WM_CLOSE received' in text and
                'PS5_PROTON_WINDOW_PROBE: leaving after the window closed' in text and
                'payload terminal reason=control-ended' in text and
                pid in expected_pids and
                sorted(item['pid'] for item in native_states) == sorted(expected_pids) and
                all(item['absent'] == 1 for item in native_states))


def leftover_helpers():
    """The helper processes alive while no title runs: an earlier run's, which outlived their title. Two from
    a run of 2026-10-06 still held the previous build's code, and the next runs failed in ways that pointed at
    the new build (remote-memory-probe-2026-10-06-02 to -04). A helper is a process websrv's payload launcher
    started under the name SceSpZeroConf; the console's own SceSpZeroConf has another parent. Returns their
    listing lines, or None when the listing could not be made."""
    return console_guard.leftover_helpers()


def capture_launch_state(settings, log_path, mask):
    """Preserve mount-service diagnostics before a reboot rotates them away. Read-only on the console."""
    log_path = log_path.resolve()
    snapshot = log_path.with_name(log_path.stem + '-launch-state.log')
    with snapshot.open('w') as output:
        for command in ('ping', 'health', 'procs'):
            try:
                output.write(command + ': ' + mask(ps5_console.ps5vkctl_command(settings, command, timeout=5)) + '\n')
            except (Exception, SystemExit) as error:
                output.write(command + ': ' + mask(error) + '\n')
        try:
            with connect(settings, 5) as ftp:
                for folder in (f'/system_ex/app/{TITLE}', '/mnt/sandbox'):
                    output.write(folder + '\n')
                    try:
                        ftp.retrlines('LIST ' + folder, lambda line: output.write(mask(line) + '\n'))
                    except ftplib.all_errors as error:
                        output.write(mask(error) + '\n')
                for name in ('debug.log', 'debug.log.1'):
                    data = io.BytesIO()
                    try:
                        ftp.retrbinary('RETR /data/shadowmount/' + name, data.write)
                        path = log_path.with_name(log_path.stem + '-shadowmount-' + name)
                        path.write_text(mask(data.getvalue().decode(errors='replace')))
                        output.write('saved ' + str(path.relative_to(ROOT)) + '\n')
                    except ftplib.all_errors as error:
                        output.write(name + ': ' + mask(error) + '\n')
        except ftplib.all_errors as error:
            output.write('FTP: ' + mask(error) + '\n')
    print(f'launch state: {snapshot}', flush=True)


def captures_on_console(ftp):
    """The numbered pictures a pad script's captures left (game-frame-NN.ppm)."""
    names = []
    ftp.retrlines(f'LIST {DATA}/logs', names.append)
    return [f'{DATA}/logs/{line.split()[-1]}' for line in names
            if re.fullmatch(r'game-frame-\d+\.ppm', line.split()[-1])]


def record_after(settings, log_path, mask, seconds=90):
    """What the console did after a run whose control payload stopped answering. The klog of the next seconds, and
    every process with its wait event (tools/console-processes.py, which needs no control payload). After
    a run of 2026-10-06 the klog had ended at "Terminating pid=220", and only a later listing showed the
    title held in its own exit with SceSysCore stopped (docs/WINDOWS_PROCESSES.md)."""
    after = log_path.with_name(log_path.stem + '-after.log')
    try:
        with socket.create_connection((settings['host'], settings['klog_port']), timeout=5) as connection, \
                after.open('w') as output:
            connection.settimeout(1.0)
            end = time.monotonic() + seconds
            while time.monotonic() < end:
                try:
                    chunk = connection.recv(65536)
                except socket.timeout:
                    continue
                if not chunk:
                    break
                output.write(mask(chunk.decode(errors='replace')))
        print(f'klog after the run: {after}', flush=True)
    except OSError as error:
        print('klog after the run: ' + mask(error), flush=True)
    processes = log_path.with_name(log_path.stem + '-processes.txt')
    listing = subprocess.run([sys.executable, str(ROOT / 'tools/console-processes.py')], capture_output=True,
                             text=True, timeout=120)
    processes.write_text(mask(listing.stdout + listing.stderr))
    print(f'processes after the run: {processes}', flush=True)


def fetch_captures(settings, log_path):
    """Fetch, convert to PNG next to the run's log, and remove them from the console."""
    pictures = []
    with connect(settings) as ftp:
        for path in sorted(captures_on_console(ftp)):
            raw = io.BytesIO()
            ftp.retrbinary(f'RETR {path}', raw.write)
            number = re.search(r'-(\d+)\.ppm$', path).group(1)
            target = log_path.with_name(f'{log_path.stem}-capture-{number}.png')
            from PIL import Image
            Image.open(io.BytesIO(raw.getvalue())).save(target)
            remove(ftp, path)
            pictures.append(str(target))
    return pictures


def fetch_broker_log(settings, log_path, mask):
    """The GPU broker's own record (PS5_Vulkan tooling/gpu-broker/server.cpp): its limits, every allocation it
    refused and at which stage, and each program's frames. None when the run had no broker."""
    path = log_path.with_name(log_path.stem + '-gpu-broker.log')
    try:
        with connect(settings) as ftp:
            raw = io.BytesIO()
            ftp.retrbinary(f'RETR {BASE}/gpu-broker-results.txt', raw.write)
    except ftplib.error_perm:
        return None
    text = mask(raw.getvalue().decode(errors='replace'))
    path.write_text(text)
    return text


PACING = re.compile(r'gpu-pacing cookie=(\d+) pid=(-?\d+) route=(\w+) final=(\d) (.*)')


def pacing(lines):
    """When each GPU client's frames reached the screen (src/pw_frame_pacing.h), from the owner's klog: every 10 s
    window, and per client the totals, frames by route (direct: the launcher's flip; composed: the desktop;
    independent: game mode on the desktop, src/pw_window_direct.h), why composed frames could not go directly, the
    median window p50, the worst window p95/p99/max and the time its presents took."""
    windows = []
    for line in lines:
        if (m := PACING.search(line)):
            cookie, pid, route, final, rest = m.groups()
            values = {key: float(value) for key, value in re.findall(r'(\w+)=(-?[\d.]+)', rest)}
            refused = re.search(r'refused=([\w-]+)', rest)
            windows.append({'cookie': int(cookie), 'pid': int(pid), 'route': route, 'final': final == '1',
                            'refused': refused.group(1) if refused else None, **values})
    clients = {}
    for w in windows:
        clients.setdefault(str(w['cookie']), []).append(w)
    summary = {}
    for key, ws in clients.items():
        shown = [w for w in ws if w['frames'] > 1]
        seconds = sum(w['seconds'] for w in ws)
        frames = sum(w['frames'] for w in ws)
        routes = {}
        for w in ws:
            routes[w['route']] = routes.get(w['route'], 0) + int(w['frames'])
        summary[key] = {
            'pid': ws[0]['pid'], 'route': max(routes, key=routes.get), 'routes': routes, 'windows': len(ws),
            'seconds': round(seconds, 1), 'frames': int(frames), 'hidden': int(sum(w['hidden'] for w in ws)),
            'direct': int(sum(w.get('direct', 0) for w in ws)), 'composed': int(sum(w.get('composed', 0) for w in ws)),
            'refused': sorted({w['refused'] for w in ws if w['refused'] not in (None, 'none')}),
            'fps': round(frames / seconds, 1) if seconds else 0.0,
            'p50_median_ms': statistics.median(w['p50'] for w in shown) if shown else None,
            'p95_worst_ms': max((w['p95'] for w in shown), default=None),
            'p99_worst_ms': max((w['p99'] for w in shown), default=None),
            'max_ms': max((w['max'] for w in ws), default=None),
            'over20': int(sum(w['over20'] for w in ws)), 'over34': int(sum(w['over34'] for w in ws)),
            'present_avg_ms': round(sum(w['present_avg'] * w['frames'] for w in ws) / frames, 2) if frames else None,
            'present_max_ms': max((w['present_max'] for w in ws), default=None)}
    return {'windows': windows, 'clients': summary} if windows else None


def broker(text):
    """What the broker granted and refused, from its record."""
    if text is None:
        return None
    limit = re.search(r'gpu-wine broker local_memory=\d+ local_offer=(\d+) local_floor=\d+ host_limit=(\d+)', text)
    refused = [{'pid': int(pid), 'stage': stage, 'bytes': int(size)} for pid, stage, size in
               re.findall(r'gpu-wine allocation_failed=1 cookie=-?\d+ pid=(-?\d+) stage=(\w+) bytes=(\d+)', text)]
    closed = [{'pid': int(pid), 'frames': int(frames), 'submissions': int(submissions)} for pid, frames, submissions in
              re.findall(r'gpu-wine cookie=-?\d+ pid=(-?\d+) closed=1 absent=1 frames=(\d+) submissions=(\d+)', text)]
    stopped = re.search(r'gpu-wine stopped=1 .*host_peak=(\d+) local_peak=(\d+)', text)
    windows = re.search(r'gpu-windows stopped frames=(\d+) .*effect_bytes=(\d+) mica_builds=(\d+) thumbnail_builds=(\d+)'
                        r' compose_count=(\d+) compose_avg_us=(\d+) compose_max_us=(\d+)', text)
    return {'local_offer': int(limit.group(1)) if limit else None, 'host_limit': int(limit.group(2)) if limit else None,
            'windows': dict(zip(('presents', 'effect_bytes', 'mica_builds', 'thumbnail_builds', 'compositions',
                                 'compose_avg_us', 'compose_max_us'), map(int, windows.groups()))) if windows else None,
            'refused': refused[:32], 'refused_count': len(refused), 'clients': closed,
            'wedged': 'wedged=1' in text,
            'failure_reasons': re.findall(r'(?:wedged|unsafe_to_close)=1 reason=(\w+)', text),
            'host_peak': int(stopped.group(1)) if stopped else None,
            'local_peak': int(stopped.group(2)) if stopped else None}


def fetch_child_logs(settings, owner_text, log_path):
    """Only processes named by this owner's launch records, including pre-READY failures: (fetched paths, pids
    without a log). Only this session's folder: pids restart every boot, so an older session can hold a log of the
    same name."""
    pids = sorted({int(pid) for pid in re.findall(
        r'(?:payload rendezvous native=|child ready native=)(\d+)', owner_text)})
    paths, missing = [], []
    if not pids:
        return paths, missing
    with connect(settings) as ftp:
        for pid in pids:
            path = log_path.with_name(f'{log_path.stem}-wine-child-{pid}.log')
            try:
                with path.open('wb') as output:
                    ftp.retrbinary(f'RETR {SESSIONS}/0/wine-child-{pid}.log', output.write)
            except ftplib.error_perm as error:
                path.unlink(missing_ok=True)
                if not str(error).startswith('550'):
                    raise
                missing.append(pid)
                continue
            paths.append(path)
    return paths, missing


def installed_profiles(lines):
    """The library entry ids in an FTP LIST of a games folder: its folders, as proton_read_games reads them."""
    return {fields[8] for fields in (line.split(None, 8) for line in lines)
            if len(fields) == 9 and fields[0].startswith('d') and not fields[8].startswith('.')}


def log_ends(ftp, name):
    """A log's end files after its head (src/pw_capped_log.h): the older one, then the newest; nothing when the log never
    filled its head."""
    data = b''
    for part in (f'{name}-end.1.log', f'{name}-end.log'):
        raw = io.BytesIO()
        try:
            ftp.retrbinary(f'RETR {DATA}/logs/{part}', raw.write)
        except ftplib.error_perm:
            continue
        data += f'=== {part} ===\n'.encode() + raw.getvalue()
    return data


def log_ends_kept(wine, title_lines):
    """tests/log-flood-probe.c's 200,000 lines through the in-title Wine's log (src/pw_capped_log.h): its head holds the
    first, an end file the last, the set at most 8 + 2 x 4 MiB and some lines over; the title's log ends with its end line."""
    first, last = b'log-flood line 0000000 ', b'log-flood line 0199999 '
    head = wine.split(b'=== wine-end', 1)[0]
    checks = {'first line in the head': first in head, 'end files': b'=== wine-end.log ===' in wine,
              'last line kept': last in wine[len(head):], 'bounded': len(wine) <= (16 << 20) + (1 << 20),
              'flood not in the title log': not any('log-flood line' in line for line in title_lines),
              'title end line': any('Session: ended' in line for line in title_lines)}
    print(f'log ends: wine log {len(wine)} bytes, ' + ', '.join(f'{name} {"yes" if ok else "NO"}' for name, ok in checks.items()))
    return all(checks.values())


def captures_lit(pictures, share):
    """Every capture the run fetched shows a frame: at least SHARE of its pixels are not black. The broker's fast copy
    (PROTON_GPU_FAST_COPY, src/pw_speed_defaults.h) once gave black captures, the pointer alone, while the TV showed the
    game (w3-ab-b-pro-2026-11-01); a capture now reads what was scanned out (PS5_Vulkan gpu-broker display.cpp)."""
    from PIL import Image
    shares = []
    for picture in pictures:
        image = Image.open(picture).convert('RGB')
        lit = sum(1 for pixel in image.getdata() if pixel != (0, 0, 0))
        shares.append(lit / (image.width * image.height))
    print('captures lit: ' + (', '.join(f'{Path(p).name} {s:.1%}' for p, s in zip(pictures, shares)) or 'none fetched'))
    return bool(shares) and min(shares) >= share


def session_logs_status(lines):
    """The title's last report of its session-log rotation (native/proton_wine.cpp); None when it gave none."""
    statuses = [int(match.group(1)) for line in lines if (match := SESSION_STATUS.search(line))]
    return statuses[-1] if statuses else None


def parse_perf(lines):
    """The counter intervals in the order the title printed them."""
    frames, server, top = [], [], []
    for line in lines:
        if (m := PERF['frames'].search(line)):
            api, *values = m.groups()
            keys = ('frames', 'fps', 'p50_ms', 'p95_ms', 'p99_ms', 'max_ms', 'present_avg_ms', 'present_max_all_ms')
            frames.append({'api': api, **{k: float(v) for k, v in zip(keys, values)}})
        elif (m := PERF['server'].search(line)):
            keys = ('calls', 'per_s', 'avg_us', 'selects', 'blocked', 'block_avg_us', 'waits', 'handles_avg',
                    'wait_all', 'polled', 'delays', 'yields')
            server.append({k: float(v) for k, v in zip(keys, m.groups())})
        elif (m := PERF['first'].search(line)):
            frames.append({'api': m.group(1), 'first_frame_ms': float(m.group(2))})
        elif (m := PERF['top'].search(line)):
            # Item by item: a line another writer's output ran into (a program in the title prints to the title's
            # standard error, fmem-bd-title-2026-10-26-01) keeps the items before the join and drops the rest.
            entries = {name: {'count': int(count), 'avg_us': float(avg)}
                       for name, count, avg in re.findall(r'(\w+)=(\d+)/([\d.]+)us(?=\s|$)', m.group(1))}
            top.append(entries)
    return frames, server, top


def parse_bench(lines):
    """A probe's measurements: every key=value after "<MARKER> bench"."""
    bench = {}
    for line in lines:
        m = re.search(r'PS5_PROTON_\w+ bench (.*)', line)
        if not m:
            continue
        for key, value in re.findall(r'(\w+)=(-?[\d.]+)', m.group(1)):
            bench[key] = float(value)
    return bench


SHOWN = re.compile(r'Wine: frames shown (\d+) in 10 s, longest gap (\d+) ms')


def shown(lines):
    """The frames the display showed in each 10 s report, their total and the longest gap between two of them."""
    reports = [(int(m.group(1)), int(m.group(2))) for line in lines if (m := SHOWN.search(line))]
    return {'frames': sum(f for f, _ in reports), 'per_10s': [f for f, _ in reports],
            'longest_gap_ms': max((g for _, g in reports), default=0)}


def aggregate(frames, server, skip):
    """Median of each counter over the intervals after the first `skip`
    (startup and shader compilation are reported, not averaged in)."""
    summary = {}
    for api in sorted({f['api'] for f in frames}):
        first = [f['first_frame_ms'] for f in frames if f['api'] == api and 'first_frame_ms' in f]
        if first:
            summary[f'{api}_first_frame_ms'] = first[0]
        steady = [f for f in frames if f['api'] == api and 'first_frame_ms' not in f][skip:]
        if steady:
            summary[api] = {k: statistics.median(f[k] for f in steady) for k in steady[0] if k != 'api'}
            summary[api]['intervals'] = len(steady)
            summary[api]['worst_p99_ms'] = max(f['p99_ms'] for f in steady)
            summary[api]['worst_max_ms'] = max(f['max_ms'] for f in steady)
    steady = server[skip:]
    if steady:
        summary['server'] = {k: statistics.median(s[k] for s in steady) for k in steady[0]}
        summary['server']['intervals'] = len(steady)
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    what = parser.add_mutually_exclusive_group(required=True)
    what.add_argument('--exe', help='a probe in the probe prefix (C:\\NAME)')
    what.add_argument('--profile', help='an installed game: /data/ps5proton/games/<id>')
    what.add_argument('--path', help='a Windows path to run as a game (Z:\\data\\...), with --env for its environment; '
                                     'the game.json files are left as they are')
    parser.add_argument('--program', help="with --profile: a Windows path run instead of the game's executable, in "
                                          "its prefix and with its settings (an installer step, a game's tool)")
    parser.add_argument('--marker', help='probe verdict prefix; PASS/FAIL follows it')
    parser.add_argument('--telemetry-hold', action='store_true',
                        help='diagnostic: close window-probe after its telemetry worker enters a bounded hold')
    parser.add_argument('--seconds', type=int, default=60, help='game budget before stop-game')
    parser.add_argument('--env', action='append', default=[], help='NAME=VALUE for this launch only')
    parser.add_argument('--no-stdout', action='store_true', help="do not redirect the program's standard output to a file (WINE_PS5_STDOUT)")
    parser.add_argument('--launch-diagnostics', action='store_true',
                        help='save control status, title mount and ShadowMount logs before and after the run (read-only)')
    parser.add_argument('--args', help='command line arguments of the program for this launch only (a Wine test takes its test name)')
    parser.add_argument('--name', help='run name (default: the exe or profile and a time)')
    parser.add_argument('--sequence', help="with --exe gate-sequence.exe: the probes it runs, one '<case><TAB><command "
                                           "line>' a line (tools/run-battery.py --grouped), for this launch only")
    parser.add_argument('--pad-script', help='a pad script (src/pw_pad_script.h) for this launch only; its captures are fetched')
    parser.add_argument('--exit-stall', type=float, default=15.0)
    parser.add_argument('--watchdog', type=float, default=0, help='hang watchdog (default: budget + 90 s)')
    parser.add_argument('--skip', type=int, default=1, help='counter intervals left out of the medians')
    parser.add_argument('--expect-ended', action='store_true',
                        help="the probe holds on after its verdict and must be ended by the title's SIGKILL to its helpers")
    parser.add_argument('--expect-title-game', metavar='EXE',
                        help='Alpha 7\'s default: this executable, and only it, ran in the title (docs/TITLE_CLIENT.md)')
    parser.add_argument('--expect-log-ends', action='store_true',
                        help="tests/log-flood-probe.c: the Wine log kept its start, its end and the last line, within its bound, "
                             "and the title's log its end line (src/pw_capped_log.h)")
    parser.add_argument('--expect-captures-lit', type=float, metavar='PERCENT',
                        help='every capture the run fetched has at least PERCENT of its pixels not black (a frame shown, '
                             'not an empty copy)')
    parser.add_argument('--expect-direct-section', action='store_true',
                        help='a large section in direct memory, mapped by chunks, all given back with none lost')
    parser.add_argument('--reparse', action='store_true',
                        help='rebuild evidence/<name>.json from the logs of a run already made (needs --name)')
    args = parser.parse_args()
    if args.program and not args.profile:
        parser.error('--program needs --profile')
    if args.telemetry_hold:
        if args.path != r'C:\window-probe.exe' or args.marker or args.reparse:
            parser.error('--telemetry-hold requires --path C:\\window-probe.exe without --marker or --reparse')
        args.env += ['PROTON_LAYOUT=launcher', 'WINETEST_TELEMETRY_HOLD=1']
    if args.reparse:
        log_path = ROOT / 'klog' / f'{args.name}.log'
        lines = log_path.read_text(errors='replace').splitlines()
        wine_log = log_path.with_name(log_path.stem + '-wine.log')
        if wine_log.is_file():
            lines += wine_log.read_text(errors='replace').splitlines()
        for child_log in sorted(log_path.parent.glob(log_path.stem + '-wine-child-*.log')):
            lines += child_log.read_text(errors='replace').splitlines()
        evidence = ROOT / 'evidence' / f'{args.name}.json'
        summary = json.loads(evidence.read_text()) if evidence.is_file() else {'run': args.name}
        frames, server, top = parse_perf(lines)
        summary['bench'] = parse_bench(lines)
        summary['steady'] = aggregate(frames, server, args.skip)
        summary['intervals'] = {'frames': frames, 'server': server, 'top': top}
        evidence.write_text(json.dumps(summary, indent=1) + '\n')
        print(json.dumps(summary['bench'], indent=1))
        return 0
    settings = ps5_console.load_settings()
    console_guard.check_launch_allowed()
    # The shared runner closes a title that is still running when its watchdog
    # expires, with the control payload's kill. Force-closing a title preceded
    # console power-offs, so this tool refuses that command: a title that does
    # not leave is reported, and killed with tools/kill-hung-title.py.
    original_command = ps5_console.ps5vkctl_command

    def guarded_command(settings, command, *rest, **keywords):
        if command.split()[:1] == ['kill']:
            print('the title is still running; this tool never force-closes: a hung title is killed with tools/kill-hung-title.py', flush=True)
            return 'refused: this tool never force-closes a title'
        return original_command(settings, command, *rest, **keywords)

    ps5_console.ps5vkctl_command = guarded_command
    # Never print the console's address, whichever layer formatted the message.
    mask = lambda text: re.sub(r'\b\d{1,3}(\.\d{1,3}){3}\b', '<console>', str(text).replace(settings['host'], '<console>'))
    # Ending a game through the server after stop-game preceded console power-offs (native/proton_wine.cpp:1491-1493;
    # dmem-01's pad-script stop): a run only asks the game to close unless --env PROTON_STOP_TERMINATE=1 says otherwise.
    if not any(item.startswith('PROTON_STOP_TERMINATE=') for item in args.env):
        args.env.append('PROTON_STOP_TERMINATE=0')
    stop_terminate = next(item for item in args.env if item.startswith('PROTON_STOP_TERMINATE=')).split('=', 1)[1]
    if args.profile:
        # Refused before anything is written: a run of 2026-10-14 ran an unknown profile in the wrong prefix.
        # The title adds mihawk-os itself when no folder of that name exists (ps5/src/proton_launch.cpp).
        known = {'mihawk-os'}
        try:
            with connect(settings) as ftp:
                for games in (f'{DATA}/games', f'{BASE}/games'):
                    listing = []
                    try:
                        ftp.retrlines(f'LIST {games}', listing.append)
                    except ftplib.error_perm as error:
                        if not str(error).startswith('550'):
                            raise
                    known |= installed_profiles(listing)
        except (OSError, ftplib.Error) as error:
            raise SystemExit('library listing: ' + mask(error)) from None
        if args.profile not in known:
            raise SystemExit(mask(f'unknown --profile {args.profile}: the console has {sorted(known)}'))
    name = args.name or ((args.exe or args.path or f'profile{args.profile}').replace('.exe', '').replace('\\', '-').replace(':', '')[:40]
                         + time.strftime('-%H%M%S'))
    marker = args.marker or (MARKERS.get(args.exe) if args.exe else None)
    log_path = ROOT / 'klog' / f'{name}.log'
    evidence = ROOT / 'evidence' / f'{name}.json'
    state = {'verdict': None, 'verdict_at': None, 'returned': False, 'stop_sent': None, 'exit_stall': False,
             'loaded': False}
    marks = {'stages': {}, 'pids': {}}
    lock = threading.Lock()
    clean_finish = False
    launched = False
    launch_readiness = None
    native_pids = set()
    held_pid = None
    storage_lines = []

    def request_stop(reason):
        with lock:
            if state['stop_sent']:
                return
            state['stop_sent'] = reason
        try:
            with connect(settings, 15) as ftp:
                ftp.storbinary(f'STOR {DATA}/stop-game', io.BytesIO((reason + '\n').encode()))
            print(f'asked the title to return to the launcher ({reason})', flush=True)
        except Exception as error:  # the watchdog still ends the run
            print('stop request failed: ' + mask(error), flush=True)

    def on_line(line):
        with lock:
            native_pids.update(map(int, re.findall(r'(?:payload rendezvous native=|child ready native=)(\d+)', line)))
        note_stage(marks, line, round(time.monotonic() - started, 2))
        if '[MihawkOS] Wine: load status=0' in line:
            state['loaded'] = True
        if marker and state['verdict'] is None and marker in line:
            state['verdict'] = probe_verdict(marker, line)
            if state['verdict']:
                state['verdict_at'] = time.monotonic()
                marks['stages'].setdefault('marker', round(state['verdict_at'] - started, 2))
        if 'returning to launcher' in line or 'restart: to=launcher' in line:
            state['returned'] = True
        if console_guard.STORAGE_DAMAGE.search(line):
            storage_lines.append(line)   # acted on by watch(): this is the klog loop, which must not block

    def watch():
        nonlocal held_pid
        started = time.monotonic()
        next_hold_poll = started
        storage_seen = False
        while not state['returned']:
            time.sleep(0.25)
            now = time.monotonic()
            if storage_lines and not storage_seen:
                storage_seen = True
                console_guard.record_storage(name, storage_lines[:5])
                request_stop('storage error in klog')
            if args.telemetry_hold and not state['stop_sent'] and now >= next_hold_poll:
                next_hold_poll = now + 1
                with lock:
                    candidates = sorted(native_pids)
                try:
                    with connect(settings, 3) as ftp:
                        for pid in candidates:
                            contents = io.BytesIO()
                            try:
                                ftp.retrbinary(f'RETR {SESSIONS}/0/wine-child-{pid}.log', contents.write)
                            except ftplib.error_perm as error:
                                if str(error).startswith('550'):
                                    continue
                                raise
                            text = contents.getvalue().decode(errors='replace')
                            if (f'telemetry hold begin pid={pid} duration_ms=60000' in text and
                                'PS5_PROTON_WINDOW_PROBE PASS: window created' in text and
                                'telemetry hold expired' not in text):
                                held_pid = pid
                                break
                    if held_pid:
                        request_stop('telemetry worker hold observed')
                except (OSError, ftplib.Error) as error:
                    # A missed sample never qualifies entry; the ordinary
                    # watchdog still requests normal cleanup and fails the run.
                    print('telemetry hold observation: ' + mask(error), flush=True)
            if (args.profile or args.path) and now - started > args.seconds + 20 and not state['stop_sent']:
                request_stop('run budget reached')
            if state['verdict_at'] and now - state['verdict_at'] > args.exit_stall and not state['stop_sent']:
                state['exit_stall'] = True
                request_stop('probe exit stalled')
            # A run still out 25 s before the watchdog is asked to return through the title's own stop: a probe
            # hung before its verdict otherwise held the console after the run (m3-pe32-2026-10-07-05).
            limit = args.watchdog or args.seconds + 90
            if now - started > limit - 25 and not state['stop_sent']:
                request_stop('no return before the watchdog')
            if now - started > limit:
                return

    try:
        launch_readiness = console_guard.before_launch(readiness=lambda: console_guard.wait_ready(settings))
        if args.launch_diagnostics:
            capture_launch_state(settings, log_path.with_name(log_path.stem + '-before.log'), mask)
        with connect(settings) as ftp:
            remove(ftp, f'{DATA}/stop-game')
            if 'WINETEST_LIFECYCLE_TRACE=1' in args.env:
                remove(ftp, f'{BASE}/payload-lifecycle.log')
            remove(ftp, f'{DATA}/logs/game-frame.ppm.broker-request')
            remove(ftp, f'{DATA}/logs/wine-stdout.txt')  # never a previous run's output
            # The GPU broker truncates its record only when it starts: viewfix-01 fetched the previous game's numbers.
            remove(ftp, f'{BASE}/gpu-broker-results.txt')
            words = ([f'profile={args.profile}'] + ([f'path={args.program}'] if args.program else []) if args.profile else [f'path={args.path}'] if args.path
                     else [f'path=C:\\{args.exe}'])
            words += [f'env={item}' for item in ([] if args.no_stdout else ['WINE_PS5_STDOUT=' + DATA + '/logs/wine-stdout.txt']) + args.env if '=' in item]
            if args.args is not None:
                words.append(f'args={args.args}')
            for listed in captures_on_console(ftp):   # never a previous run's pictures
                remove(ftp, listed)
            if args.sequence:
                ftp.storbinary(f'STOR {SEQUENCE}', io.BytesIO(Path(args.sequence).read_bytes()))
            else:
                remove(ftp, SEQUENCE)
            if args.pad_script:
                ftp.storbinary(f'STOR {DATA}/pad-script.txt', io.BytesIO(Path(args.pad_script).read_bytes()))
            else:
                remove(ftp, f'{DATA}/pad-script.txt')
            ftp.storbinary(f'STOR {BASE}/wine-run.txt', io.BytesIO(('\n'.join(words) + '\n').encode()))
            ftp.storbinary(f'STOR {BASE}/test-run.txt', io.BytesIO(b'frames 10\nsamples all\n'))
        watcher = threading.Thread(target=watch, daemon=True)
        watcher.start()
        started = time.monotonic()
        console_guard.after_launch()
        launched = True
        result, log, _ = runner.run_title(TITLE, r'\[MihawkOS\].*ends: status', (args.watchdog or args.seconds + 90),
                                          str(log_path), on_line=on_line, exit_grace=30,
                                          echo_pattern=r'\[MihawkOS\] (perf:|Wine: (load|alive|process completed|test override))|'
                                                       r'PS5_PROTON_|fatal signal|returning to launcher')
        elapsed = time.monotonic() - started
        state['returned'] = True
        wine_log = log_path.with_name(log_path.stem + '-wine.log')
        with connect(settings) as ftp, wine_log.open('wb') as output:
            ftp.retrbinary(f'RETR {DATA}/logs/wine-last.log', output.write)
            output.write(log_ends(ftp, 'wine'))
        # The title's say() lines, kept on the console's disk (ps5/src/platform.c): what survives when klog does not.
        title_log = log_path.with_name(log_path.stem + '-title.log')
        title_lines = []
        try:
            raw = io.BytesIO()
            with connect(settings) as ftp:
                ftp.retrbinary(f'RETR {DATA}/logs/title-last.log', raw.write)
                raw.write(log_ends(ftp, 'title'))
            title_log.write_text(mask(raw.getvalue().decode(errors='replace')))
            title_lines = title_log.read_text().splitlines()
        except ftplib.error_perm:
            pass
        # The title's flight recorder (native/proton_flight_recorder.h): one block a second, kept on the console's disk.
        flight_lines = []
        try:
            raw = io.BytesIO()
            with connect(settings) as ftp:
                ftp.retrbinary(f'RETR {SESSIONS}/0/flight-recorder.log', raw.write)
            flight_log = log_path.with_name(log_path.stem + '-flight.log')
            flight_log.write_text(mask(raw.getvalue().decode(errors='replace')))
            flight_lines = flight_log.read_text().splitlines()
        except ftplib.error_perm:
            pass
        if 'WINETEST_LIFECYCLE_TRACE=1' in args.env:
            with connect(settings) as ftp, log_path.with_name(log_path.stem + '-lifecycle.log').open('wb') as output:
                ftp.retrbinary(f'RETR {BASE}/payload-lifecycle.log', output.write)
        # A Windows program's standard output (patch 0900) is a file of its own; the conformance tests print there.
        stdout_file = log_path.with_name(log_path.stem + '-stdout.txt')
        try:
            with connect(settings) as ftp, stdout_file.open('wb') as output:
                ftp.retrbinary(f'RETR {DATA}/logs/wine-stdout.txt', output.write)
        except ftplib.error_perm:
            stdout_file.unlink(missing_ok=True)
        pictures = fetch_captures(settings, log_path)
        broker_log = fetch_broker_log(settings, log_path, mask)
        lines = log.read_text(errors='replace').splitlines()
        child_logs, child_logs_missing = fetch_child_logs(settings, '\n'.join(lines), log_path)
        runtime_lines = wine_log.read_text(errors='replace').splitlines()
        for child_log in child_logs:
            runtime_lines += child_log.read_text(errors='replace').splitlines()
        # klog can lose its last lines across LoadExec; the Wine log is
        # written synchronously, so a verdict missing from klog is read there.
        if marker and state['verdict'] is None:
            for line in runtime_lines:
                if probe_verdict(marker, line):
                    state['verdict'] = probe_verdict(marker, line)
                    if (at := console_stage(marks, line)) is not None:
                        marks['stages'].setdefault('marker', at)
                    break
        frames, server, top = parse_perf(lines + runtime_lines)
        bench = parse_bench(lines + runtime_lines)
        # LoadExec can remove the owner before the framework's final line is
        # captured. Its explicit completion verdict plus the restart line prove
        # the same clean endpoint; a crash/timeout is never accepted here.
        clean_owner_restart = any('PS5_PROTON_OWNER_BOOTSTRAP PASS initial_status=0' in line for line in lines) and \
                              any('Wine: process completed; returning to launcher' in line for line in lines)
        # The control service can observe the launcher closing before its final
        # klog line arrives in the drain. Use that captured status too; a crash
        # or timeout still cannot pass, nor can a nonzero launcher status.
        launcher_statuses = [int(match.group(1)) for line in lines
                             if (match := re.search(r'\[MihawkOS\].*ends: status(?:=|\s+)(-?\d+)', line))]
        launcher_end_status = launcher_statuses[-1] if launcher_statuses else None
        # A run that injects a fault, or whose probe must be ended from outside, is judged on how it ended, not on the
        # probe's verdict or exit status.
        fault = ('memory-stall' if any(item.startswith('PROTON_MEMORY_TEST_STALL=') for item in args.env) else
                 'assertion' if 'WINE_PS5_ASSERT_PROBE=1' in args.env else
                 'doom' if any(item.startswith('WINETEST_DOOM_AFTER_MS=') for item in args.env) else
                 'gpu-loss' if any(item.startswith('PROTON_GPU_TEST_FAIL_SUBMIT=') for item in args.env) else
                 'broker-wedge' if any(item.startswith('PROTON_TEST_BROKER_HOLD_S=') for item in args.env) else None)
        verdict_run = not fault and not args.expect_ended
        passed = (result == 'finished' or (result == 'exited' and
                  (clean_owner_restart or launcher_end_status == 0))) and \
                 (not state['exit_stall'] or args.expect_ended) and state['loaded']
        passed = passed and launcher_end_status in (None, 0)
        touched = touchpad_touched_at_start(lines)
        if touched:
            print(f'touchpad touched at start: {touched} contact(s) on the pad\'s touchpad move the pointer; leave the pad '
                  'face-up and untouched, or switch it off, and run again')
        passed = passed and not touched
        # The owner's verdict line (native/proton_wine.cpp) says FAIL when the initial program's exit status is not 0
        # or the broker did not stop. A probe's status is its verdict. A game the title asked to close (the run's
        # budget, a pad script's stop) and that closed by itself ends with a status of its own choosing, which is no
        # verdict: a .NET (MonoGame) game's is 1 (a run of 2026-10-06). A game that had to be ended did not close.
        owners = [match for match in (re.search(r'PS5_PROTON_OWNER_BOOTSTRAP (PASS|FAIL) initial_status=(-?\d+) '
                                                r'graphics_claimed=\d+ broker_stopped=(\d+)', line) for line in lines) if match]
        closed_on_request = not marker and any('Wine: stop requested; asking the game to close' in line for line in lines) \
            and not any(re.search(r'the game did not close in \d+ s', line) for line in lines)
        passed = passed and (not verdict_run or all(owner.group(1) == 'PASS' or (closed_on_request and owner.group(3) == '1')
                                                    for owner in owners))
        passed = passed and not any('PS5_PROTON_OWNER_BOOTSTRAP FAIL' in line and not re.search(r'initial_status=', line)
                                    for line in lines)
        if marker and verdict_run:
            passed = passed and probe_completed(state['verdict'], state['stop_sent'])
        elif marker and args.expect_ended:
            passed = passed and state['verdict'] == 'PASS'   # printed before the hold the title ends
        broker_result = broker(broker_log)
        # Composition statistics are also on the owner's stderr (klog): keep
        # them even when the broker's results file was not readable.
        if broker_result is not None and broker_result.get('windows') is None:
            broker_result['windows'] = broker('\n'.join(lines)).get('windows')
        elif broker_result is None:
            windows = broker('\n'.join(lines)).get('windows')
            if windows:
                broker_result = {'windows': windows}
        # A completed Windows process and a closed display are not enough:
        # the broker may retain mappings because a native helper is still live.
        passed = passed and (fault == 'gpu-loss' or not (broker_result and broker_result.get('wedged')))
        failed_closes = native_close_failures('\n'.join(lines + runtime_lines))
        passed = passed and not failed_closes
        # Evidence that survives a hang: the title rotated its session folders (status 0), every helper it named
        # left its log there, and the klog showed no storage damage.
        logs_status = session_logs_status(lines + title_lines)
        storage = console_guard.storage_damage(lines)
        passed = passed and logs_status == 0 and not child_logs_missing and not storage
        # Executable memory in a helper is JIT memory only (wine/ps5/pw_wine_dmem_ps5.c).
        child_texts = [path.read_text(errors='replace') for path in child_logs]
        # Each helper start, step by step (tools/start_stages.py): the title's spawn line, the helper's stamps, Wine's lines.
        child_by_pid = {int(m.group(1)): text for path, text in zip(child_logs, child_texts)
                        if (m := re.search(r'-(\d+)\.log$', path.name))}
        initial = next((text for text in child_texts if 'wine-child: initial Windows client' in text), '')
        image = re.search(r'^.*trace:loaddll:build_module Loaded L"[^"]*\.exe"', initial, re.M)
        if image and (at := console_stage(marks, image.group(0))) is not None:
            marks['stages'].setdefault('program_started', at)
        exec_record = exec_protection(child_texts)
        foreign_record = foreign_mappings(child_texts)
        passed = passed and exec_clean(exec_record) and \
            not (marker and exec_tally_missing(child_texts) and not args.expect_ended)
        # Helper scheduling (src/pw_helper_sched.h): each helper's threads got what they asked for; a census run's C2 row.
        sched = helper_sched.summary(runtime_lines)
        passed = passed and (sched is None or sched['scheduled']['ok'])
        ended_record = helpers_ended(lines + title_lines)
        memory = memory_helper(lines)
        robustness = wine_robustness(lines, runtime_lines)
        returned = any('returning to launcher' in line for line in lines + title_lines + wine_log.read_text(errors='replace').splitlines())
        # The memory helper never stops answering, no helper asserts, and the kernel placed nothing over the section
        # arena, except where the run injects exactly that fault.
        passed = passed and (not memory['dead'] or fault == 'memory-stall') and \
            (not robustness['assertion'] or fault == 'assertion') and not robustness['misplaced_max']
        if fault == 'memory-stall':
            # Marked dead once, after the 5 s deadline and not much later, and the chunks it could not take back counted.
            passed = passed and memory['stall_tests'] == 1 and len(memory['dead']) == 1 and \
                memory['dead'][0]['request'] == 'stall' and 5000 <= memory['dead'][0]['waited_ms'] <= 5600 and \
                memory['lost'] > 0 and returned
        if fault == 'assertion':
            passed = passed and robustness['assertion'] and returned and \
                any('Assertion failed: !assert_probe' in line for line in runtime_lines)
        # A helper's backstop (src/pw_control_watch.h) ends it only in a run that injects its reason.
        doom = helper_doom(child_texts)
        passed = passed and backstop_allowed(fault, doom)
        if fault == 'doom':
            # The run's program ended by its loader 5 s after the test stamp, nothing asked of it; the title came back.
            passed = passed and returned and not state['stop_sent']
        elif fault == 'gpu-loss':
            # The broker answered a submission with a failure and stayed wedged; the program saw its device lost and
            # ended without the backstop (backstop_allowed: no GPU stamp seen, no doom), and the title came back.
            passed = passed and bool(broker_result) and broker_result.get('failure_reasons') == ['test_submit_failure'] \
                and returned
        phases = broker_phases(flight_lines)
        if args.expect_ended:
            passed = passed and ended_cleanly(ended_record) and returned
            if any(item.startswith('PROTON_HANG_BREAKER_TEST_CENSUS_FREEZE_S=') for item in args.env):
                passed = passed and ended_record['why'] == 'hang breaker' and ended_record['stalled'] is not None
            if fault == 'broker-wedge':
                passed = passed and broker_wedge_ended(ended_record) and 'test-hold' in (phases or {})
        if args.expect_title_game:
            in_title = [line.split('Wine: ', 1)[-1] for line in lines + title_lines
                        if line.endswith(' runs in the title beside the broker')]
            in_title = sorted(set(in_title))
            passed = passed and len(in_title) == 1 and \
                in_title[0].lower().endswith('\\' + args.expect_title_game.lower() + ' runs in the title beside the broker')
            print(f'in the title: {in_title or "nothing"}')
        if args.expect_log_ends:
            passed = passed and log_ends_kept(wine_log.read_bytes(), title_lines)
        if args.expect_captures_lit is not None:
            passed = passed and captures_lit(pictures, args.expect_captures_lit / 100)
        if args.expect_direct_section:
            passed = passed and bool(memory['sections_mib']) and memory['gave_back_lines'] > 0 and memory['given_back'] > 0 \
                and not memory['lost'] and bool(robustness['chunk_maps']) and robustness['ask_failures_max'] == 0
        telemetry_record = None
        if args.telemetry_hold:
            # The existing read-only payload probe checks the actual native
            # PIDs, including helpers that never reached READY.
            process_spec = importlib.util.spec_from_file_location('process_probe', ROOT / 'tools/run-process-probe.py')
            process_probe = importlib.util.module_from_spec(process_spec)
            process_spec.loader.exec_module(process_probe)
            expected_pids = sorted({int(pid) for pid in re.findall(
                r'(?:payload rendezvous native=|child ready native=)(\d+)', '\n'.join(lines))})
            native_states = process_probe.payload_absence(settings, expected_pids, ROOT / 'build/payload-lifecycle-probe.elf')
            absence_elapsed = time.monotonic() - started
            held_log = log_path.with_name(f'{log_path.stem}-wine-child-{held_pid}.log')
            held_text = held_log.read_text(errors='replace') if held_log.exists() else ''
            hold_passed = state['stop_sent'] == 'telemetry worker hold observed' and telemetry_hold_passed(
                held_pid, held_text, native_states, expected_pids, absence_elapsed)
            telemetry_record = {'pid': held_pid, 'native_states': native_states,
                                'absence_elapsed_seconds': round(absence_elapsed, 3), 'passed': hold_passed}
            passed = passed and hold_passed
        summary = {
            'telemetry_hold': telemetry_record,
            'launch_readiness': launch_readiness,
            'native_close_failures': failed_closes,
            'run': name, 'what': args.exe or args.path or f'profile {args.profile}' + (f' program {args.program}' if args.program else ''), 'env': args.env,
            'result': result, 'seconds': round(elapsed, 1), 'probe_verdict': state['verdict'],
            # program_started and marker are helper log times moved onto the host's clock (within klog's delay).
            'stages': dict(sorted({**marks['stages'], 'closed': round(elapsed, 2)}.items(), key=lambda item: item[1])),
            'clean_owner_restart': clean_owner_restart,
            'launcher_end_status': launcher_end_status,
            'initial_status': int(owners[0].group(2)) if owners else None, 'closed_on_request': closed_on_request,
            'child_logs': [str(path.relative_to(ROOT)) for path in child_logs],
            'child_logs_missing': child_logs_missing, 'session_logs_status': logs_status,
            'storage_errors': len(storage), 'stop_terminate': stop_terminate,
            'injected_fault': fault, 'exec_protection': exec_record, 'code_share': code_share(child_texts), 'helpers': helper_rows(child_texts),
            'start_stages': start_stages.summary(lines, child_by_pid),
            'foreign_mappings': foreign_record, 'helper_sched': sched, 'helpers_ended': ended_record,
            'helper_doom': doom, 'broker_phases': phases,
            'memory_helper_slowest': memory.pop('slowest'), 'memory_helper': memory, **robustness,
            'exit_stall': state['exit_stall'], 'stop_requested': state['stop_sent'],
            # klog can lose a process's last lines when the title replaces itself: the Wine log has
            # them, and the launcher starting again after the game's own start is the return itself.
            'returned_to_launcher': any('returning to launcher' in line
                                        for line in lines + wine_log.read_text(errors='replace').splitlines())
                                    or any('launch: mode=launcher' in line
                                           for line in lines[next((i for i, line in enumerate(lines)
                                                                   if 'launch: mode=game' in line), len(lines)):]),
            'touchpad_touched_at_start': touched,
            'passed': passed, 'bench': bench, 'steady': aggregate(frames, server, args.skip),
            'intervals': {'frames': frames, 'server': server, 'top': top},
            # What the display showed of the foreground process, from the title's 10 s reports
            # (native/proton_wine.cpp): a game in a helper process has no ntdll counters in the title.
            'shown': shown(lines),
            'broker': broker_result,
            'pacing': pacing(lines),
        }
        # Diagnostics can request broker captures without a timed pad script.
        # Record every image we downloaded, including an explicit empty list.
        summary['captures'] = [str(Path(picture).relative_to(ROOT)) for picture in pictures]
        if args.pad_script:
            summary['pad_script'] = Path(args.pad_script).read_text().splitlines()
        clean_finish = passed and (clean_owner_restart or launcher_end_status == 0) and \
                       not re.search(r'A user thread receives a fatal signal|GPU_FAULT|gpu fault', '\n'.join(lines), re.I)
        evidence.parent.mkdir(exist_ok=True)
        evidence.write_text(json.dumps(summary, indent=1) + '\n')
        print(json.dumps({k: summary[k] for k in ('run', 'result', 'probe_verdict', 'exit_stall', 'passed', 'stages', 'steady')},
                         indent=1))
        print(f'klog: {log}  wine: {wine_log}  evidence: {evidence}')
        return 0 if passed else 1
    except BaseException as error:
        if isinstance(error, SystemExit) and error.code in (0, None):
            raise
        # Only a launched run can have dropped the console; a run refused before it leaves no drop behind.
        if launched and ('did not answer' in str(error) or 'No route to host' in str(error)):
            console_guard.record_drop_unless_answering(
                mask(error), lambda: ps5_console.ps5vkctl_command(settings, 'ping', timeout=5))
            if 'did not answer' in str(error):
                try:
                    record_after(settings, log_path, mask)
                except Exception as after_error:  # diagnostics only: the run's result stands
                    print('after the run: ' + mask(after_error), flush=True)
        # A service failure is still a run result, even if the normal summary
        # could not be collected. Keep the reason redacted and never call it a
        # console shutdown without evidence of that event.
        evidence.parent.mkdir(exist_ok=True)
        evidence.write_text(json.dumps({'run': name, 'what': args.exe or args.path or f'profile {args.profile}' + (f' program {args.program}' if args.program else ''),
            'env': args.env, 'passed': False, 'result': 'runner_error', 'error': mask(error),
            'loaded': state['loaded'], 'probe_verdict': state['verdict'],
            'stop_requested': state['stop_sent'], 'cleanup_verified': False}, indent=1) + '\n')
        raise SystemExit(mask(error)) from None
    finally:
        if args.launch_diagnostics:
            try:
                capture_launch_state(settings, log_path, mask)
            except (Exception, SystemExit) as error:
                print('launch diagnostics: ' + mask(error), flush=True)
        # One-shot requests never outlive the run, pass or fail.
        hooks_removed = False
        try:
            if idle(settings):
                with connect(settings, 15) as ftp:
                    for path in (f'{BASE}/test-run.txt', f'{BASE}/wine-run.txt', f'{DATA}/stop-game',
                                 f'{DATA}/pad-script.txt', SEQUENCE):
                        remove(ftp, path)
                hooks_removed = True
            else:
                clean_finish = False
        except (Exception, SystemExit) as error:
            clean_finish = False
            print('request cleanup: ' + mask(error), flush=True)
            if launched and 'did not answer' in str(error):
                console_guard.record_drop_unless_answering(
                    mask(error), lambda: ps5_console.ps5vkctl_command(settings, 'ping', timeout=5))
        # Every way a run ends, the runner's error and the klog after it included.
        found = [line for path in (log_path, log_path.with_name(log_path.stem + '-after.log')) if launched and path.is_file()
                 for line in console_guard.storage_damage(path.read_text(errors='replace').splitlines())]
        if found:
            console_guard.record_storage(name, found)
            clean_finish = False
        console_guard.finished(clean_finish)
        if evidence.exists():
            record = json.loads(evidence.read_text())
            record.update(cleanup_verified=clean_finish, hooks_removed=hooks_removed)
            if found:
                record.update(passed=False, storage_errors=len(found))
            if not hooks_removed:
                was_passed = record.get('passed')
                record['passed'] = False
                evidence.write_text(json.dumps(record, indent=1) + '\n')
                if was_passed:
                    raise SystemExit('Run hooks were not removed; qualification refused')
            else:
                evidence.write_text(json.dumps(record, indent=1) + '\n')


if __name__ == '__main__':
    sys.exit(main())
