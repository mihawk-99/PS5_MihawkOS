#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The first-install rehearsal with nobody at the console (docs/SELF_INSTALL.md): the guided setup walked by scripted
presses with the real install engine, its first Mihawk OS session, Done, and one Mihawk OS session that logs off.

  tools/run-setup-unattended.py --name RUN
  tools/run-setup-unattended.py --name RUN --read --package DIR    afterwards: downloads only, nothing launched or
                                                                   written
  tools/run-setup-unattended.py --name RUN --pieces DIR
                         first copies DIR's files (dist/runtime, or a package's PPSA99170/runtime-pieces) into the
                         title folder's runtime-pieces/ over FTP, as a release carries them (one already there at its
                         size is left alone): for a title deployed from a development build, which has none.

Before it: the title folder holds the build under test with its runtime pieces (a release package's PPSA99170, or
--pieces), and the tour has not finished (no setup.txt with done=1). The harness writes only its own hooks (/data/ps5proton/setup-unattended, the title
folder's test-run.txt, /data/ps5proton/stop-game) and removes them when the console is idle, failure or not; the title
writes what a first install writes. With the marker, a scripted run gives the tour the real engine and lets it restart
into its sessions (proton_setup_unattended, ps5/src/proton_launch.h); without it nothing of this happens.

No person can close a title, so every launcher is a scripted one that ends itself:
  0. prime, only when /data/ps5proton is missing (after a first attempt was renamed aside): a 300-frame launcher with
     no presses is the title's real first run (dev.conf, the folder and logs/ before root, the grant); the harness
     checks the folder is there and keeps that run's title-early.log, which the next launch writes over.
  1. install: Welcome, the first language; the install then starts by itself from the title folder, in a launcher of
     18000 frames (about 5 min, so it outlives the install). It waits for "install: done" (or a failure) and for the launcher's end. Skipped when
     the runtime's stamp is already on the console (a rerun after an install that finished).
  2. prepare: until setup.txt says done=1 the tour starts at Welcome, and with the runtime in place the language goes
     on to Preparing: Welcome, language, Start. The title restarts into the first Mihawk OS session, which ends itself once the prefix
     is made. The launcher after it (its test-run.txt planted during the session) is at Before you play, where cross
     is held 1,500 frames (12.5 s at 120 fps; the page wants 10 s), then Done → Go to the game library, then Mihawk OS,
     which the library focuses after Done. The session after that is logged off through stop-game once
     its desktop has been up 45 s, and the launcher after it runs its frames and ends.
It stops at the first stop line (a crash, maxpipekva, TTY(STDIN), errno=5, a storage error) and changes nothing more.
klog goes to klog/RUN/capture.log; each milestone is printed as it arrives; evidence/RUN.json records them.
--read, with the console idle, downloads what the install left (dev.conf, title-early.log, setup.txt, the logs and
sessions/, the runtime's runtime-pack.txt and runtime-stamp) into klog/RUN/results/, compares the manifest and stamp
with the package's title folder, and reads back every installed runtime file, hashing it against that manifest. It
turns the FTP server's SELF mode off first, or a signed file would read back decrypted.
"""
import argparse
import hashlib
import io
import json
import re
import socket
import sys
import time
import uuid
from ftplib import FTP, all_errors as ftplib_errors, error_perm, error_reply
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'PS5_Vulkan/tools'))
sys.path.insert(0, str(ROOT / 'tools'))
import console_guard   # noqa: E402
import ps5_console     # noqa: E402
from console_log import LineLog, redact   # noqa: E402

TITLE = '/data/homebrew/PPSA99170'
MARKER = '/data/ps5proton/setup-unattended'
STOP = re.compile(r'fatal signal|maxpipekva|TTY\(STDIN\)|errno=5|\[IOD\] process_completion: error|nsid1_read')
MILESTONE = re.compile(r'\[MihawkOS\] (setup: (?!sky)|install: (from|done|failed|stopped)|restart: |'
                       r'Wine: (process completed|'
                       r'stop requested)|Mihawk: desktop session|ends: status|elevation: |first run: )')


def script(identity, frames, presses):
    """A test run: presses are (frame, action); holds are (first frame, last frame, action)."""
    return (f'identity {identity}\nframes {frames}\nscreenshot\nsamples ps5proton\n' +
            ''.join(f'press ps5proton {p[0]} {p[1]}\n' if len(p) == 2 else f'hold ps5proton {p[0]} {p[1]} {p[2]}\n'
                    for p in presses)).encode()


def read_results(args):
    """Downloads only: what the install left, and every runtime file hashed against the package's manifest."""
    settings = ps5_console.load_settings()
    if ' count=0 ' not in ps5_console.ps5vkctl_command(settings, 'procs', timeout=15) + ' ':
        raise SystemExit('the console is running an application; read it when idle')
    out = ROOT / 'klog' / args.name / 'results'
    out.mkdir(parents=True, exist_ok=True)
    ftp = FTP()
    ftp.connect(settings['host'], settings['ftp_port'], timeout=60)
    ftp.login(settings['ftp_user'], settings['ftp_password'] or 'codex')
    if 'disabled' not in ftp.sendcmd('SELF').lower() and 'disabled' not in ftp.sendcmd('SELF').lower():
        raise SystemExit("the FTP server's SELF mode did not turn off; nothing was read")

    def fetch(remote):
        sink = io.BytesIO()
        try:
            ftp.retrbinary('RETR ' + remote, sink.write)
        except error_perm:
            return None
        return sink.getvalue()

    def listing(remote):
        rows = []
        try:
            ftp.retrlines('LIST ' + remote, rows.append)
        except error_perm:
            pass
        return rows

    record = {'name': args.name, 'read': {}}
    sessions = [f'logs/sessions/{n}/' + row.split()[-1] for n in '012'
                for row in listing(f'/data/ps5proton/logs/sessions/{n}')
                if row.split() and row.split()[-1] not in ('.', '..') and not row.startswith('d')]
    for remote in ['/data/homebrew/dev.conf', TITLE + '/title-early.log'] + [
            '/data/ps5proton/' + r for r in ('setup.txt', 'logs/title-last.log', 'logs/wine-last.log',
                                             'logs/lapy-service.log', 'runtime/ge11-7/runtime-pack.txt',
                                             'runtime/ge11-7/runtime-stamp', *sessions)]:
        data = fetch(remote)
        record['read'][remote] = None if data is None else len(data)
        if data is not None:
            (out / remote.strip('/').replace('/', '_')).write_text(redact(data.decode('utf-8', 'replace')))
    folders = listing('/data/ps5proton') + listing('/data/ps5proton/runtime/ge11-7')
    (out / 'data_ps5proton.list').write_text('\n'.join(folders))
    manifest = (args.package / 'runtime-pack.txt').read_text()
    record['runtime_pack_matches'] = fetch('/data/ps5proton/runtime/ge11-7/runtime-pack.txt') == manifest.encode()
    record['runtime_stamp_matches'] = fetch('/data/ps5proton/runtime/ge11-7/runtime-stamp') == \
        (args.package / 'runtime-stamp').read_bytes()
    files = [line.split(' ', 4)[1:] for line in manifest.splitlines() if line.startswith('file ')]
    passed, failed = 0, []
    for digest, _, _, relative in files:
        check = hashlib.sha256()
        try:
            ftp.retrbinary('RETR /data/ps5proton/' + relative, check.update, blocksize=1 << 20)
        except error_perm:
            failed.append(relative)
            continue
        if check.hexdigest() == digest:
            passed += 1
        else:
            failed.append(relative)
    ftp.quit()
    record.update(hashes_passed=passed, hashes_total=len(files), hash_failures=failed[:50])
    record['passed'] = record['runtime_pack_matches'] and record['runtime_stamp_matches'] and passed == len(files)
    (ROOT / 'evidence' / (args.name + '-read.json')).write_text(json.dumps(record, indent=2) + '\n')
    print(json.dumps({k: v for k, v in record.items() if k != 'read'}, indent=2))
    return 0 if record['passed'] else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--name', required=True)
    parser.add_argument('--read', action='store_true', help='downloads only: nothing is launched or written')
    parser.add_argument('--package', type=Path, help="with --read: the package's title folder (its runtime-pack.txt)")
    parser.add_argument('--pieces', type=Path, help="runtime pieces to copy into the title folder's runtime-pieces/")
    args = parser.parse_args()
    if args.pieces and not args.pieces.is_dir():
        parser.error('--pieces needs a folder')
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.name):
        parser.error('use a plain run name')
    if args.read:
        if not args.package:
            parser.error('--read needs --package')
        return read_results(args)
    run, evidence_path = ROOT / 'klog' / args.name, ROOT / 'evidence' / (args.name + '.json')
    if evidence_path.exists():
        raise SystemExit('evidence already exists; use a new run name')
    console_guard.check_launch_allowed()
    settings = ps5_console.load_settings()
    readiness = console_guard.before_launch(readiness=lambda: console_guard.wait_ready(settings))
    idle = lambda: ' count=0 ' in ps5_console.ps5vkctl_command(settings, 'procs', timeout=15) + ' '
    if not idle():
        raise SystemExit('the console is running an application; nothing was launched')
    run.mkdir(parents=True)

    def ftp():
        connection = FTP()
        connection.connect(settings['host'], settings['ftp_port'], timeout=30)
        connection.login(settings['ftp_user'], settings['ftp_password'] or 'codex')
        return connection

    def put(path, data):
        try:
            with ftp() as connection:
                connection.storbinary('STOR ' + path, io.BytesIO(data))
        except ftplib_errors as error:
            raise SystemExit(f'could not write {path} on the console ({redact(str(error))}); stopped here') from None

    def exists(path):
        with ftp() as connection:
            try:
                connection.cwd(path)
                return True
            except error_perm:
                return False

    def fetch(path):
        sink = io.BytesIO()
        with ftp() as connection:
            try:
                connection.retrbinary('RETR ' + path, sink.write)
            except error_perm:
                return ''
        return redact(sink.getvalue().decode('utf-8', 'replace'))

    def remove(path):
        with ftp() as connection:
            try:
                connection.delete(path)
            except (error_perm, error_reply) as error:
                if not str(error).startswith(('550', '226')):
                    raise

    record = {'name': args.name, 'launch_readiness': readiness, 'milestones': [], 'stop_lines': [],
              'launches': []}
    lines = []
    clean = False
    sock = socket.create_connection((settings['host'], settings['klog_port']), timeout=10)
    try:
        sock.settimeout(0.5)
        end = time.monotonic() + 20   # klogsrv replays its buffer first; this run's lines come after it
        while time.monotonic() < end:
            try:
                if not sock.recv(65536):
                    raise SystemExit('klog closed before launch')
            except socket.timeout:
                break
        log = (run / 'capture.log').open('w')

        def on_line(line):
            lines.append(line)
            if STOP.search(line):
                record['stop_lines'].append(line)
                print('STOP LINE:', line, flush=True)
            elif MILESTONE.search(line):
                record['milestones'].append(line)
                print(line, flush=True)

        stream = LineLog(log, on_line)

        def wait(until, limit, idle_ends=False):
            """Read klog until `until(lines)` holds, a stop line comes, or `limit` s pass; with idle_ends, the console
            idle for 3 s also ends it. True when `until` held."""
            started, quiet, poll = time.monotonic(), None, time.monotonic() + 2
            while time.monotonic() - started < limit:
                try:
                    chunk = sock.recv(65536)
                    if not chunk:
                        raise SystemExit('klog disconnected during the run')
                    stream.write(chunk)
                except socket.timeout:
                    pass
                if record['stop_lines']:
                    return False
                if until(lines):
                    return True
                if idle_ends and time.monotonic() >= poll:
                    poll = time.monotonic() + 2
                    quiet = (quiet or time.monotonic()) if idle() else None
                    if quiet and time.monotonic() - quiet >= 3:
                        return False
            return False

        seen = lambda text: (lambda all_lines: any(text in line for line in all_lines[mark:]))
        # 0. Priming, when /data/ps5proton is missing: the title's own first run makes it, never FTP (its owner and mode
        # are what the title's pre-root mkdir of logs/ expects). A short scripted launcher with no presses shows no tour
        # without the marker: it writes dev.conf, makes the folder and logs/ before root, is granted, and ends itself.
        if not exists('/data/ps5proton'):
            put(TITLE + '/test-run.txt', script(uuid.uuid4().hex, 300, []))
            console_guard.after_launch()
            mark = len(lines)
            ps5_console.ps5vkctl_command(settings, 'launch PPSA99170', timeout=120)
            record['launches'].append('prime')
            wait(lambda all_lines: False, 180, idle_ends=True)
            early = fetch(TITLE + '/title-early.log')   # the next launch writes over it
            (run / 'prime-title-early.log').write_text(early)
            record['primed'] = {'folder_made': exists('/data/ps5proton'), 'logs_made': exists('/data/ps5proton/logs'),
                                'early_lines': [l for l in early.splitlines()
                                                if l.startswith(('first run:', 'elevation:'))]}
            if record['stop_lines'] or not record['primed']['folder_made'] or not idle():
                raise SystemExit('the priming launch did not end with /data/ps5proton made and the console idle; '
                                 'stopped here')
        put(MARKER, b'')
        remove('/data/ps5proton/setup-first-run')   # the simulated first run would win over the real one
        if args.pieces:
            # The pieces into the title folder's runtime-pieces/, where a release carries them (one already there at its
            # size is left alone)
            with ftp() as connection:
                try:
                    connection.mkd(TITLE + '/runtime-pieces')
                except error_perm:
                    pass
                there = {}
                connection.retrlines(f'LIST {TITLE}/runtime-pieces',
                                     lambda line: there.__setitem__(line.split()[-1], int(line.split()[4])))
                pieces = sorted(p for p in args.pieces.iterdir() if p.is_file() and p.suffix == '.zst')
                copied = 0
                for piece in pieces:
                    if there.get(piece.name) == piece.stat().st_size:
                        continue
                    with piece.open('rb') as data:
                        connection.storbinary(f'STOR {TITLE}/runtime-pieces/{piece.name}', data)
                    copied += 1
            record['pieces'] = {'count': len(pieces), 'copied': copied}
            print(f'pieces: {len(pieces)} in {TITLE}/runtime-pieces ({copied} copied)', flush=True)
        # 1. The install, unless an earlier run finished it: the stamp is the last file an install writes.
        stamp = fetch('/data/ps5proton/runtime/ge11-7/runtime-stamp')
        record['install_skipped'] = bool(stamp)
        if not stamp:
            # 1. The install. Welcome; the language focused (en-US); the install starts by itself. Nothing is pressed
            # after it: during the install CROSS is Stop.
            identity = uuid.uuid4().hex
            # 18000 frames, about 5 min at 60 fps: the scripted launcher must outlive the install, which ends with it.
            presses = [(30, 'cross'), (70, 'cross')]
            put(TITLE + '/test-run.txt', script(identity, 18000, presses))
            console_guard.after_launch()
            mark = len(lines)
            ps5_console.ps5vkctl_command(settings, 'launch PPSA99170', timeout=120)
            record['launches'].append('install')
            installed = wait(lambda all_lines: any('install: done' in l or 'install: failed' in l
                                                   for l in all_lines[mark:]), 600)
            wait(lambda all_lines: False, 420, idle_ends=True)   # the launcher's frames end it
            record['install_done'] = any('install: done' in l for l in lines[mark:])
            if record['stop_lines'] or not record['install_done']:
                raise SystemExit('the install did not end in "install: done"; stopped here')
            if not idle():
                raise SystemExit('the install launcher is still running after its frames; stopped here')
        # 2. Preparing → Start; the first session; Before you play (cross held); Done → the library → Mihawk OS; a session
        # logged off; the launcher's end.
        # Until setup.txt says done=1 the tour starts at Welcome (src/pw_setup.c pw_setup_start); with the runtime in
        # place the language goes on to Preparing: Welcome, language, Start.
        if not idle():
            raise SystemExit('the console is not idle before the Preparing launch; stopped here')
        put(TITLE + '/test-run.txt', script(uuid.uuid4().hex, 1200, [(30, 'cross'), (70, 'cross'), (100, 'cross')]))
        console_guard.after_launch()
        mark = len(lines)
        ps5_console.ps5vkctl_command(settings, 'launch PPSA99170', timeout=120)
        record['launches'].append('prepare')
        if not wait(seen('setup: the first Mihawk OS session starts'), 120):
            raise SystemExit('Preparing did not start the first session; stopped here')
        # for the launcher after it: cross held through Before you play, then Done's library tile (right of Install
        # Steam), then Mihawk OS, which the library focuses
        put(TITLE + '/test-run.txt', script(uuid.uuid4().hex, 2400, [(90, 1590, 'cross'), (1620, 'right'), (1640, 'cross'),
                                                                     (1700, 'cross')]))
        if not wait(seen('setup: the prefix is made'), 900) or not wait(seen('setup: held'), 300) \
                or not wait(seen('setup: ends (the library)'), 300):
            raise SystemExit('the first session or Done did not finish; stopped here')
        put(TITLE + '/test-run.txt', script(uuid.uuid4().hex, 600, []))   # for the launcher after the next session
        mark = len(lines)
        if not wait(seen('Mihawk: desktop session'), 300):
            raise SystemExit('the Mihawk OS session did not reach its desktop; stopped here')
        wait(lambda all_lines: False, 45)
        if record['stop_lines']:
            raise SystemExit('stopped on a stop line')
        put('/data/ps5proton/stop-game', b'unattended rehearsal\n')
        record['session_logged_off'] = wait(seen('Wine: process completed; returning to launcher'), 120)
        wait(lambda all_lines: False, 180, idle_ends=True)
        clean = record['session_logged_off'] and not record['stop_lines']
        return 0 if clean else 1
    finally:
        try:
            stream.finish()
            log.close()
        except NameError:
            pass
        sock.close()
        storage = console_guard.storage_damage(lines)
        if storage:
            console_guard.record_storage(args.name, storage)
        record['storage_errors'] = len(storage)
        try:
            if idle():
                for path in (MARKER, TITLE + '/test-run.txt', TITLE + '/wine-run.txt', '/data/ps5proton/stop-game'):
                    remove(path)
                record['hooks_removed'] = True
            else:
                record['hooks_removed'] = False   # a title still runs: nothing is touched under it
                clean = False
            console_guard.finished(clean and not storage)
        except BaseException as error:
            console_guard.finished(False)
            record['cleanup_error'] = redact(str(error))
        record['passed'] = clean and not storage and record.get('hooks_removed', False)
        record['capture'] = str(run.relative_to(ROOT))
        evidence_path.write_text(json.dumps(record, indent=2) + '\n')


if __name__ == '__main__':
    sys.exit(main())
