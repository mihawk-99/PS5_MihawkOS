#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the title's frame-budget harness with fresh log identity and no forced kill."""
import argparse
import hashlib
import importlib.util
import io
import json
import re
import socket
import sys
import time
import uuid
from ftplib import FTP, error_perm, error_reply
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ACTIVE_EVIDENCE = None
sys.path.insert(0, str(ROOT.parent / 'PS5_Vulkan/tools'))
import ps5_console
import console_guard


from console_log import LineLog, redact

PROBES = {'exit-probe.exe': 'PS5_PROTON_EXIT_PROBE', 'wine-probe.exe': 'PS5_PROTON_PE64_PROBE',
          'd3d11-probe.exe': 'PS5_PROTON_D3D11_PROBE', 'xinput-probe.exe': 'PS5_PROTON_XINPUT_PROBE'}


def remove(ftp, path):
    try:
        ftp.delete(path)
    except error_perm as error:
        if not str(error).startswith('550'):
            raise
    except error_reply as error:
        if not str(error).startswith('226'):
            raise


def guide_pictures(ftp):
    """The Steam guide's pictures the launcher kept (/data/ps5proton/steam-guide-pictures/*.ppm); LIST, not NLST."""
    lines = []
    try:
        ftp.retrlines('LIST /data/ps5proton/steam-guide-pictures', lines.append)
    except Exception:
        return []
    return ['/data/ps5proton/steam-guide-pictures/' + line.split()[-1] for line in lines
            if line.split() and line.split()[-1].endswith('.ppm')]


def main():
    global ACTIVE_EVIDENCE
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    parser.add_argument('--frames', type=int, default=300)
    parser.add_argument('--themes', action='store_true')
    parser.add_argument('--controls', choices=('section', 'buttons', 'remapped'),
                        help='preview the Controls menu without changing saved preferences')
    parser.add_argument('--probe', choices=PROBES)
    parser.add_argument('--press', action='append', default=[], metavar='FRAME ACTION',
                        help="a pad press in the launcher's script, e.g. '40 down'; repeatable (focus a row to see its panel)")
    # tools/run-launcher-ended.py: a session that fails at once (a profile the library lacks), or one that never came
    # back (its marker left in the title folder, as a crash leaves it); the launcher that follows says which.
    parser.add_argument('--profile', help='start this library entry first, then the launcher')
    parser.add_argument('--path', help='start this Windows path in the probe prefix first, then the launcher')
    parser.add_argument('--plant-marker', metavar='NAME', help="leave a session's marker, as a crash would")
    # The guided setup walked as a first run (docs/GUIDED_SETUP.md): its first-run marker makes the tour show and play the
    # install with its fake engine, so the runtime and the prefix are never touched; the test run saves nothing.
    parser.add_argument('--setup', action='store_true', help='walk the guided setup, in Japanese, to Preparing')
    # The Steam guide's launcher pages (src/pw_steam_guide.h), each kept as a picture: every page in one language or all
    # ten ("1"); 24 frames a page (--frames 1300 covers the fifty)
    parser.add_argument('--steam-guide-pictures', metavar='LANGUAGE', help="a language's tag, or 1 for every language")
    args = parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.name) or not 1 <= args.frames <= 3600:
        parser.error('use a plain run name and 1..3600 frames')
    console_guard.check_launch_allowed()
    settings = ps5_console.load_settings()
    launch_readiness = console_guard.before_launch(readiness=lambda: console_guard.wait_ready(settings))
    clean_finish = False
    def idle():
        return ' count=0 ' in ps5_console.ps5vkctl_command(settings, 'procs', timeout=15) + ' '
    if not idle():
        raise RuntimeError('Console is running an application; no launch performed')
    run = ROOT / 'klog' / args.name
    run.mkdir(parents=True, exist_ok=False)
    evidence_path = ROOT / 'evidence' / (args.name + '.json')
    if evidence_path.exists():
        raise RuntimeError('Evidence already exists; use a new run name')
    ACTIVE_EVIDENCE = evidence_path
    evidence_path.write_text(json.dumps({'passed': False, 'status': 'incomplete',
        'capture': str(run.relative_to(ROOT))}, indent=2) + '\n')
    identity = uuid.uuid4().hex
    marker = '[MihawkOS] test run identity: ' + identity
    script = f'identity {identity}\nframes {args.frames}\nscreenshot\nsamples ps5proton\n'
    if args.themes:
        script += 'press ps5proton 2 square\n'
    for press in args.press:
        frame, action = press.split()
        script += f'press ps5proton {int(frame)} {action}\n'
    if args.setup:
        # Welcome; Language: English, down to Russian, right to Chinese and Japanese; the install starts by itself from
        # the title folder (the fake engine), runs about 8.6 s, then Preparing shows to the end
        for frame, action in ((30, 'cross'), (50, 'down'), (56, 'right'), (62, 'right'), (70, 'cross')):
            script += f'press ps5proton {frame} {action}\n'
    if args.controls:
        # Settings is the main menu's dock tile under Mihawk OS (examples/ps5proton/home_grid.hpp): DOWN, then CROSS
        script += 'press ps5proton 2 down\npress ps5proton 4 cross\npress ps5proton 20 cross\n'
        if args.controls != 'section':
            script += 'press ps5proton 40 cross\n'
        if args.controls == 'remapped':
            for frame in range(50, 80, 5):
                script += f'press ps5proton {frame} down\n'
            script += 'press ps5proton 80 cross\n'
    def ftp_connect():
        ftp = FTP()
        ftp.connect(settings['host'], settings['ftp_port'], timeout=30)
        ftp.login(settings['ftp_user'], settings['ftp_password'] or 'codex')
        return ftp
    try:
        with socket.create_connection((settings['host'], settings['klog_port']), timeout=10) as sock:
            sock.settimeout(0.5)
            # Keep replay for diagnosis, but never use it as the run's verdict.
            backlog = bytearray()
            deadline = time.monotonic() + 20
            while True:
                try:
                    chunk = sock.recv(65536)
                    if not chunk:
                        raise RuntimeError('Klog closed before launch')
                    backlog.extend(chunk)
                except socket.timeout:
                    break
                if time.monotonic() >= deadline:
                    raise RuntimeError('Klog replay never became quiet; no launch performed')
            (run / 'replay.log').write_text(redact(backlog.decode(errors='replace')))
            with ftp_connect() as ftp:
                remove(ftp, '/data/homebrew/PPSA99170/wine-run.txt')
                if args.probe:
                    remove(ftp, '/data/ps5proton/logs/wine-last.log')
                    remove(ftp, '/data/ps5proton/stop-game')
                    if args.probe == 'd3d11-probe.exe':
                        remove(ftp, '/data/ps5proton/logs/d3d-probe.ppm')
                    ftp.storbinary('STOR /data/homebrew/PPSA99170/wine-run.txt',
                                   io.BytesIO(('path=C:\\' + args.probe + '\n').encode()))
                if args.profile or args.path:
                    ftp.storbinary('STOR /data/homebrew/PPSA99170/wine-run.txt', io.BytesIO(
                        (f'profile={args.profile}\n' if args.profile else f'path={args.path}\n').encode()))
                if args.plant_marker:
                    ftp.storbinary('STOR /data/homebrew/PPSA99170/session-running',
                                   io.BytesIO(args.plant_marker.encode()))
                if args.setup:
                    ftp.storbinary('STOR /data/ps5proton/setup-first-run', io.BytesIO(b''))
                if args.steam_guide_pictures:
                    for old in guide_pictures(ftp):
                        remove(ftp, old)
                    ftp.storbinary('STOR /data/ps5proton/steam-guide-showcase', io.BytesIO(args.steam_guide_pictures.encode()))
                ftp.storbinary('STOR /data/homebrew/PPSA99170/test-run.txt', io.BytesIO(script.encode()))
            # Count an attempted launch even if the control connection fails afterwards.
            console_guard.after_launch()
            ps5_console.ps5vkctl_command(settings, 'launch PPSA99170', timeout=120)
            print('Launch request completed; capturing title output.', flush=True)
            started = time.monotonic()
            next_poll = started + 2
            idle_since = None
            captured = bytearray()
            # Klog has no timestamps: note when each startup step reached this host. The
            # kernel's exec line is the base; the title's own first line follows its /data request.
            steps = {'exec': ' EXEC /app0/eboot.bin ', 'starts': '[MihawkOS] starts',
                     'elevation': '[MihawkOS] elevation: ', 'library': '[MihawkOS] launcher: ',
                     'ui': '[MihawkOS] launcher UI: '}
            seen = {}
            def stamp(line):
                for step, marker in steps.items():
                    if step not in seen and marker in line:
                        seen[step] = time.monotonic()
            with (run / 'capture.log').open('w') as log:
                stream = LineLog(log, stamp)
                while time.monotonic() - started < 120:
                    try:
                        chunk = sock.recv(65536)
                        if not chunk:
                            raise RuntimeError('Klog disconnected during run')
                        captured.extend(chunk)
                        stream.write(chunk)
                    except socket.timeout:
                        pass
                    now = time.monotonic()
                    if now >= next_poll:
                        next_poll = now + 2
                        if idle():
                            idle_since = idle_since or now
                        else:
                            idle_since = None
                    if idle_since and now - idle_since >= 3:
                        break
                stream.finish()
            text = redact(captured.decode(errors='replace'))
        storage = console_guard.storage_damage(text.splitlines())   # recorded in finally, however the run ends
        # No replayed success can match this nonce. Keep earlier startup diagnostics separately.
        current = text[text.index(marker):] if marker in text else ''
        (run / 'klog.log').write_text(current)
        spec = importlib.util.spec_from_file_location('check_run', ROOT / 'ps5/tools/check-run.py')
        checker = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(checker)
        finished = bool(idle_since and marker in text and '[MihawkOS] ends: status 0' in current)
        checked = checker.check(run, ['ps5proton']) if finished else 1
        probe_passed = None
        probe_lines = []
        if args.probe and idle_since:
            with ftp_connect() as ftp:
                sink = io.BytesIO()
                ftp.retrbinary('RETR /data/ps5proton/logs/wine-last.log', sink.write)
                wine = redact(sink.getvalue().decode(errors='replace'))
                (run / 'wine.log').write_text(wine)
                probe_marker = PROBES[args.probe]
                probe_lines = [line for line in (text + wine).splitlines() if probe_marker in line]
                probe_passed = (any(probe_marker + ' PASS' in line for line in text.splitlines())
                                and not any(probe_marker + ' FAIL' in line for line in probe_lines)
                                and 'Wine: load status=0' in text
                                and ('returning to launcher' in text or 'restart: to=launcher' in text))
                if args.probe == 'd3d11-probe.exe' and probe_passed:
                    sink = io.BytesIO()
                    ftp.retrbinary('RETR /data/ps5proton/logs/d3d-probe.ppm', sink.write)
                    from PIL import Image, ImageStat
                    picture = Image.open(io.BytesIO(sink.getvalue())).convert('RGB')
                    picture.save(run / 'd3d-probe.png')
                    probe_passed = picture.size == (1280, 720) and min(ImageStat.Stat(picture).stddev) > 2
        marker_left = None
        if args.profile or args.path or args.plant_marker:
            with ftp_connect() as ftp:
                # LIST, not NLST: the console's FTP server answers NLST with 502.
                listing = []
                ftp.retrlines('LIST /data/homebrew/PPSA99170', listing.append)
                marker_left = any(line.split()[-1] == 'session-running' for line in listing if line.split())
        evidence = {
            'launch_readiness': launch_readiness,
            'session_marker_left': marker_left,
            'title': 'PPSA99170', 'identity': identity, 'frames_requested': args.frames,
            'eboot_sha256': hashlib.sha256((ROOT / 'dist/PPSA99170/eboot.bin').read_bytes()).hexdigest(),
            'fresh_identity_seen': marker in text, 'shell_idle_after_run': bool(idle_since),
            'title_finished_successfully': finished, 'picture_and_log_check_passed': checked == 0,
            'post_launch_fault_records': len(checker.CRASH.findall(text)),
            'post_launch_driver_errors': len(checker.DRIVER_ERROR.findall(text)),
            'elevation': re.findall(r'\[MihawkOS\] elevation: (.*)', text),
            'sample': re.findall(r'\[MihawkOS\] sample ps5proton: (.*)', current),
            'library': re.findall(r'\[MihawkOS\] launcher: (.*)', current),
            # The system notifications the title posted, in either process (native/proton_wine.cpp notices_post).
            'notices': re.findall(r'\[MihawkOS\] notice: (.*)', text),
            # Seconds after the kernel's exec line that each step's line arrived here.
            'startup_seconds': {step: round(seen[step] - seen['exec'], 2) for step in seen} if 'exec' in seen else None,
            'probe': args.probe, 'probe_passed': probe_passed, 'probe_records': probe_lines,
            'controls_preview': args.controls,
            'passed': finished and checked == 0 and not checker.CRASH.search(text) and not checker.DRIVER_ERROR.search(text) and (not args.probe or probe_passed) and not storage,
            'capture': str(run.relative_to(ROOT)), 'forced_close': False,
            'storage_errors': len(storage),
        }
        if args.setup:
            steps = [int(n) for n in re.findall(r'\[MihawkOS\] setup: step (\d+)', current)]
            sky = [float(ms) for ms in re.findall(r'setup sky: ([0-9.]+) ms GPU', current)]
            evidence['setup'] = {
                'steps': steps, 'language_ja': '[MihawkOS] setup: language ja-JP' in current,
                'installed': bool(re.search(r'setup: install 4 problem 0', current)),
                'sky_gpu_ms': sky, 'fps': re.findall(r'sample ps5proton: ok, \d+ frames in [0-9.]+ s, ([0-9.]+) fps', current)}
            evidence['passed'] = evidence['passed'] and steps == [0, 1, 4, 5] and evidence['setup']['language_ja'] \
                and evidence['setup']['installed'] and bool(sky)
        if args.steam_guide_pictures:
            kept = []
            with ftp_connect() as ftp:
                from PIL import Image
                for path in guide_pictures(ftp):
                    raw = io.BytesIO()
                    ftp.retrbinary(f'RETR {path}', raw.write)
                    target = run / ('steam-guide-' + path.rsplit('/', 1)[1][:-4] + '.png')
                    Image.open(io.BytesIO(raw.getvalue())).save(target)
                    remove(ftp, path)
                    kept.append(target.name)
            expected = 50 if args.steam_guide_pictures == '1' else 5
            evidence['steam_guide_pictures'] = kept
            evidence['passed'] = evidence['passed'] and len(kept) == expected
        if args.controls == 'remapped':
            evidence['controls_selection_verified'] = 'Controls: preview left=r1 right=none middle=none' in text
            evidence['passed'] = evidence['passed'] and evidence['controls_selection_verified']
        evidence_path.write_text(json.dumps(evidence, indent=2) + '\n')
        clean_finish = evidence['passed'] and evidence['shell_idle_after_run']
        print(json.dumps(evidence, indent=2), flush=True)
        return 0 if evidence['passed'] else 1
    finally:
        # Storage damage is recorded however the run ended: a klog drop or a hung console skips the normal path.
        capture = run / 'capture.log'
        found = console_guard.storage_damage(capture.read_text(errors='replace').splitlines()) if capture.is_file() else []
        if found:
            console_guard.record_storage(args.name, found)
            clean_finish = False
        # Remove only this harness's hooks, and only when the console is idle.
        try:
            if idle():
                with ftp_connect() as ftp:
                    for hook in ['test-run.txt', 'wine-run.txt'] + (['session-running'] if args.plant_marker else []):
                        remove(ftp, '/data/homebrew/PPSA99170/' + hook)
                    if args.setup:
                        remove(ftp, '/data/ps5proton/setup-first-run')
                    if args.steam_guide_pictures:
                        remove(ftp, '/data/ps5proton/steam-guide-showcase')
            else:
                clean_finish = False
            console_guard.finished(clean_finish)
        except BaseException as error:
            console_guard.finished(False)
            message = 'Harness cleanup incomplete: ' + redact(error)
            print(message, file=sys.stderr)
            evidence = json.loads(evidence_path.read_text())
            evidence.update(passed=False, cleanup_error=message)
            evidence_path.write_text(json.dumps(evidence, indent=2) + '\n')
            raise RuntimeError(message) from None


if __name__ == '__main__':
    try:
        sys.exit(main())
    except BaseException as error:
        if isinstance(error, SystemExit) and isinstance(error.code, int):
            raise
        if ACTIVE_EVIDENCE is not None and ACTIVE_EVIDENCE.exists():
            record = json.loads(ACTIVE_EVIDENCE.read_text())
            record.update(passed=False, error=redact(error))
            ACTIVE_EVIDENCE.write_text(json.dumps(record, indent=2) + '\n')
        print('Launcher validation failed: ' + redact(error), file=sys.stderr)
        sys.exit(1)
