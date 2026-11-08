#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bring the console to ready for MihawkOS, after a factory reset or on any boot. Every step reads the
console's own state first, so running the command again resumes, and on a reset console it redoes everything.

  tools/console-setup.py                  apply: exit 0 ready, 1 stopped; evidence/console-setup-<date>-NN.json
  tools/console-setup.py --check          read only: exit 0 every step done, 2 something to do, 1 blocked
  tools/console-setup.py --check --deep   also read every deployed file back (minutes)

By hand, first:
  after a factory reset   reinstall the same 10.01 image; Power Saving: never enter rest mode; automatic system
                          updates off. If the console's address changed (DHCP), fix PS5_HOST in PS5_Vulkan/.env.
  every boot              load kstuff and elfldr with websrv (8080), ftpsrv (2121) and klogsrv (3232); never OnionHEN.
                          Only after a real restart: python3 tools/console_guard.py new-boot, before ps5vkctl is loaded.

Steps: done, did, todo (--check), or stopped (blocked under --check, which goes on unless S0, S1 or a running title
blocks it):
  S0 host       dist/ and the staged runtime. Whether dist/ was built from this tree is recorded, not required
                (tools/gate.py requires it).
  S1 reach      ftpsrv and websrv answer, and a klog stream opens that lasts the whole command: saved redacted to
                klog/<run>.log and scanned line by line. A storage-damage line (console_guard.STORAGE_DAMAGE) records
                the guard's storage block and stops the run at the next step, as does OnionHEN or the stream ending.
  S2 guard      the console guard refuses nothing: no drop, no storage block.
  S3 control    ps5vkctl answers. Only when its port refuses (nothing listens) is the repository's pinned build
                (tools/build-console-control.py) POSTed to websrv's elfldr, after the klog replay was scanned. A port
                that accepts or times out without a ping reply, or a ps5vkctl that answered this boot and is gone,
                stops: a second copy is never started. No title may be running; this tool never force-closes one.
                A firmware other than 10010000 in the Lapy daemon's log (once the title has run) stops it here,
                before the first write.
  S4 klog       klogsrv's replay: no damage, no OnionHEN. It starts minutes after boot (146 s measured), so boot-time
                fsck lines are not in it.
  S5 salvage    once per boot, before anything launches: the console's .log and .txt files, read only, redacted into
                klog/salvage-<boot>/. After a hang this keeps that session's logs (plan section 4).
  S6 dev.conf   /data/homebrew/dev.conf, the loopback setting the Lapy daemon needs; made when missing, and another
                content stops (it may point at tools/ps5log-sink.py on purpose).
  S7 title      the title read back by sha256 with SELF off (tools/verify-title.py, which the guard records); only the
                files that differ are sent, with PS5_Vulkan's deploy-title-folder.py upload() (a hidden name, then
                renamed; eboot.bin and sce_sys/param.json last). Unchanged signed files such as libc.prx stay.
  S8 runtime    the console's runtime deploy manifest against .deps/staged/manifest.json, the probe prefix, and the
                guard's runtime record; tools/deploy-wine.py runs only when one of them is off.
  S9 prefix     the mihawk prefix's .update-timestamp ends in ' wow64' and the Lapy log reports firmware=10010000.
                Otherwise one Mihawk OS session of its own (run-console --profile mihawk-os --seconds 150) builds it:
                it must pass, its logs (klog, title, wine, every helper) must hold 'elevation: ok (filesystem access)'
                and 'PS5 prefix baseline ready' and no storage damage or OnionHEN. A new prefix's first session gets
                no DualSense, so no game ever runs in it.
  S10 read-back every file the runtime deploy manifest lists, read with SELF off and compared by
                sha256 (always on apply, with --deep under --check). Reads only; the guard records the runtime.

It never launches a game, never force-closes a title, never deletes anything on the console and never runs
new-boot. Every write goes through console_guard.begin_write/end_write (ps5vkctl's sync), and S9's launch waits the
guard's 60 s after the last write, which also covers ShadowMountPlus's ~45 s to register a new title.

"""
import argparse
import contextlib
import ftplib
import hashlib
import importlib.util
import io
import json
import os
import re
import socket
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'PS5_Vulkan/tools'))
import ps5_console  # noqa: E402
sys.path.insert(0, str(ROOT / 'tools'))
import console_guard  # noqa: E402
import gate  # noqa: E402
from console_log import LineLog, redact  # noqa: E402


def load(path, name):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


deploy_folder = load(ROOT / 'tools/deploy-folder.py', 'deploy_folder')
verify = load(ROOT / 'tools/verify-title.py', 'verify_title')
title_folder = load(ROOT.parent / 'PS5_Vulkan/tools/deploy-title-folder.py', 'deploy_title_folder')  # read only

TITLE = 'PPSA99170'
DATA = '/data/ps5proton'
DEV_CONF_PATH = '/data/homebrew/dev.conf'
DEV_CONF = b'DEV_SERVER=127.0.0.1\nDEV_PORT=8080\n'
FIRMWARE = '10010000'   # the pinned image: the Pro's 10.01
FIRMWARES = (FIRMWARE, '13600007')   # also the owner's base PS5, firmware 13.60 (2026-11-06)
WEBSRV = 8080
DIST = ROOT / 'dist' / TITLE
STAGED = ROOT / '.deps/staged/manifest.json'
EVIDENCE = ROOT / 'evidence'
KLOG = ROOT / 'klog'
SALVAGE = ((DATA + '/logs', True), (f'/data/homebrew/{TITLE}', False))   # (folder, with its subfolders)
PREFIX_SECONDS = 150
# 'PS5 prefix baseline ready' is a helper's line (patch 0956), so it is in a -wine-child-*.log, not in the klog.
GOOD_LINES = ('elevation: ok (filesystem access)', 'PS5 prefix baseline ready')
RUNTIME_DONE = re.compile(r'Uploaded and verified \d+ files.*; prefix probe: (created|preserved existing state)')

class Stop(Exception):
    """A step cannot go on. final: --check stops there too (nothing more may be read)."""

    def __init__(self, why, final=False):
        super().__init__(why)
        self.final = final


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def next_name(prefix):
    day, number = time.strftime('%Y-%m-%d'), 1
    while (EVIDENCE / f'{prefix}-{day}-{number:02d}.json').exists():
        number += 1
    return f'{prefix}-{day}-{number:02d}'


def port_state(host, port):
    """'open', 'refused' (nothing listens) or 'silent' (no answer within 5 s)."""
    try:
        with socket.create_connection((host, port), timeout=5):
            return 'open'
    except ConnectionRefusedError:
        return 'refused'
    except OSError:
        return 'silent'


def ping(settings):
    """ps5vkctl's pid, or None (the error's text holds the address and is dropped)."""
    try:
        match = console_guard.PING.search(ps5_console.ps5vkctl_command(settings, 'ping', timeout=5))
    except (SystemExit, OSError):
        return None
    return int(match.group(1)) if match else None


def procs(settings):
    return ps5_console.ps5vkctl_command(settings, 'procs', timeout=5)


def build_control():
    """The repository's pinned ps5vkctl (tools/build-console-control.py), checked against the digest it recorded."""
    built = subprocess.run([sys.executable, str(ROOT / 'tools/build-console-control.py')], cwd=ROOT,
                           capture_output=True, text=True)
    if built.returncode:
        raise Stop('tools/build-console-control.py failed: ' + (built.stderr or built.stdout).strip()[-300:])
    out = Path(built.stdout.strip().splitlines()[-1])
    elf = (out / 'ps5vkctl.elf').read_bytes()
    if sha256(elf) != json.loads((out / 'manifest.json').read_text())['elf_sha256']:
        raise Stop(f'{out}/ps5vkctl.elf is not the build its manifest records')
    return elf


def post_elf(settings, elf):
    """websrv's elfldr starts it, without pipe: ps5vkctl stays resident (as loaded by hand on 2026-10-14)."""
    boundary = 'ConsoleSetup'
    body = b''
    for name, data in (('args', b'ps5vkctl'), ('elf', elf)):
        header = f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"'
        if name == 'elf':
            header += '; filename="ps5vkctl.elf"'
        body += (header + '\r\n\r\n').encode() + data + b'\r\n'
    body += f'--{boundary}--\r\n'.encode()
    request = urllib.request.Request(f'http://{settings["host"]}:{WEBSRV}/elfldr', data=body,
                                     headers={'Content-Type': 'multipart/form-data; boundary=' + boundary})
    with urllib.request.urlopen(request, timeout=30) as response:
        response.read(4096)


class KlogStream:
    """klogsrv for as long as the command runs: its replay on connect, then every new line, saved redacted (path None:
    nowhere) and scanned. klogsrv may serve one client at a time (unmeasured), so it is closed around S9's run."""

    def __init__(self, settings, path):
        self.lines, self.damage, self.onion = 0, [], 0
        self.quiet, self.ended, self.closing = threading.Event(), threading.Event(), False
        self.socket = socket.create_connection((settings['host'], settings['klog_port']), timeout=5)
        self.socket.settimeout(console_guard.BOOT_QUIET)
        self.output = path.open('a') if path else open(os.devnull, 'w')
        self.output.write('--- console-setup: klog connected; klogsrv replays its buffer first ---\n')
        self.thread = threading.Thread(target=self.read, daemon=True)
        self.thread.start()

    def line(self, text):
        self.lines += 1
        if console_guard.STORAGE_DAMAGE.search(text):
            self.damage.append(text)
        self.onion += 'OnionHEN' in text

    def read(self):
        log = LineLog(self.output, self.line)
        try:
            while True:
                try:
                    chunk = self.socket.recv(65536)
                except socket.timeout:
                    self.quiet.set()
                    continue
                if not chunk:
                    break
                log.write(chunk)
        except (OSError, ValueError):
            pass
        finally:
            self.ended.set()
            self.quiet.set()

    def replay(self):
        """The replay klogsrv sends on connect is in: 2 s without a line, or 30 s in all."""
        self.quiet.wait(console_guard.BOOT_LIMIT)

    def close(self):
        self.closing = True
        with contextlib.suppress(OSError):
            self.socket.shutdown(socket.SHUT_RDWR)
        self.socket.close()
        self.thread.join(5)
        self.output.close()


class Run:
    def __init__(self, check, deep):
        self.check, self.deep = check, deep or not check
        self.name = None if check else next_name('console-setup')
        self.settings = self.ftp = self.klog = None
        self.klogs, self.facts, self.steps = [], {}, {}

    def mask(self, text):
        text = str(text)
        if self.settings:
            text = text.replace(self.settings['host'], '<console>')
        return re.sub(r'\b\d{1,3}(\.\d{1,3}){3}\b', '<console>', text)

    def connect(self):
        if self.ftp:
            with contextlib.suppress(OSError, EOFError, ftplib.Error):
                self.ftp.close()
        self.ftp = deploy_folder.connect(self.settings)   # SELF off
        return self.ftp

    def retr(self, path):
        return deploy_folder.read_remote(self.ftp, path)

    def open_klog(self):
        self.klog = KlogStream(self.settings, None if self.check else KLOG / f'{self.name}.log')
        self.klogs.append(self.klog)

    def tool(self, *argv):
        """A repository tool, its output shown masked: (exit code, masked lines). The FTP connection is renewed after
        it, since the tool may have run for minutes."""
        process = subprocess.Popen([sys.executable, *argv], cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                   text=True, errors='replace')
        lines = []
        for line in process.stdout:
            lines.append(self.mask(line.rstrip('\n')))
            print('  ' + lines[-1], flush=True)
        code = process.wait()
        self.connect()
        return code, lines

    @contextlib.contextmanager
    def writing(self, what):
        console_guard.begin_write(what, self.settings)
        try:
            yield
        finally:
            console_guard.end_write(self.settings)

    def scan(self):
        """What the klog stream saw so far: storage damage (recorded on apply), OnionHEN, or the stream gone."""
        if self.klog.damage:
            if not self.check:
                console_guard.record_storage(self.name, self.klog.damage)
            raise Stop(f'{len(self.klog.damage)} storage-damage lines in the klog: stop all console work for the day '
                       '(plan section 4); a person must look first')
        if self.klog.onion:
            raise Stop(f'OnionHEN is loaded ({self.klog.onion} klog lines): restart without it (plan section 4)')
        if self.klog.ended.is_set() and not self.klog.closing:
            raise Stop('the klog stream ended (klogsrv gone?), so storage damage cannot be watched: load klogsrv, '
                       'then run again')


def build_state():
    """Whether dist/ was built from this tree (tools/gate.py's fingerprint): recorded here, required by the gate."""
    try:
        built = json.loads((gate.STATE / 'build.json').read_text())
    except (FileNotFoundError, ValueError):
        return {'matches_tree': False, 'head': None, 'time': None}
    return {'matches_tree': gate.fingerprint(gate.working_tree()) == built['fingerprint'],
            'head': built['head'][:12], 'time': built['time']}


def s0_host(run):
    run.settings = ps5_console.load_settings()
    if not (DIST / 'eboot.bin').is_file() or not STAGED.is_file():
        raise Stop(f'dist/{TITLE} or the staged runtime is missing: run tools/build-production.sh')
    if not run.check:
        run.facts['build'] = build_state()
    return 'done', 'dist and the staged runtime are there'


def s1_reach(run):
    host = run.settings['host']
    shut = [port for port in (run.settings['ftp_port'], WEBSRV) if port_state(host, port) != 'open']
    try:
        if shut:
            raise OSError(f'port {shut[0]} does not answer')
        run.connect()
        if not run.check:
            KLOG.mkdir(exist_ok=True)
        run.open_klog()
    except (OSError, EOFError, ftplib.Error) as error:
        raise Stop(f'{run.mask(error)}: is PS5_HOST in PS5_Vulkan/.env still the console after the reset (DHCP)? '
                   'Load kstuff and elfldr with websrv (8080), ftpsrv (2121) and klogsrv (3232)', final=True) from None
    return 'done', 'ftpsrv and websrv answer; klog stream open'


def s2_guard(run):
    why = console_guard.refusal(console_guard.load(), deploys=False)
    if why:
        raise Stop(why)
    return 'done', 'no drop, no storage block'


def s3_control(run):
    known = console_guard.load().get('ps5vkctl_pid')
    pid, did = ping(run.settings), False
    if pid is None:
        port = port_state(run.settings['host'], run.settings['ctl_port'])
        if port != 'refused':
            raise Stop(f"ps5vkctl's port {'accepts' if port == 'open' else 'does not answer'} but gives no ping reply: "
                       'it is loaded and hung; restart the console by hand, then tools/console_guard.py new-boot '
                       '(a second copy is never started)')
        if known is not None:
            raise Stop(f'ps5vkctl answered this boot as pid {known} and is not loaded now: if the console was '
                       'restarted, run tools/console_guard.py new-boot first; otherwise it died (rest mode?): restart '
                       'by hand, then new-boot')
        if run.check:
            return 'todo', 'ps5vkctl is not loaded'
        s4_klog(run)   # this boot's klog is scanned before anything is loaded
        elf = build_control()
        try:
            post_elf(run.settings, elf)
        except OSError as error:   # a timed-out POST may still have started it: poll, never send twice
            print('  elfldr: ' + run.mask(error), flush=True)
        for _ in range(10):
            time.sleep(1)
            pid = ping(run.settings)
            if pid is not None:
                break
        else:
            raise Stop('ps5vkctl did not answer within 10 s of the POST: restart by hand before another try')
        did = True
    if not run.check:
        console_guard.control_payload(run.settings)   # records this boot's pid; another pid than recorded is a drop
    elif known is not None and pid != known:
        raise Stop(f'ps5vkctl answers as pid {pid} but was pid {known} this boot: it was gone and loaded again (a '
                   'drop): restart by hand, then new-boot')
    status = procs(run.settings)
    if ' count=0 ' not in status + ' ':
        raise Stop(f'a title is running ({status}): close it on the console; this tool never force-closes',
                   final=True)
    # Before the first write (S6): the firmware the Lapy daemon last logged; S9 reads it again after a session.
    firmware = lapy_firmware(run)
    return ('did' if did else 'done'), (f'ps5vkctl pid {pid}, no title running'
                                        + (f', firmware {firmware}' if firmware else ''))


def s4_klog(run):
    run.klog.replay()
    if not run.klog.lines:
        # An idle console can be quiet for 30 s (26 s to the first line, 2026-10-14); the open stream still scans
        # every later line. Only a stream the server closed leaves the damage unwatched.
        if run.klog.ended.is_set():
            raise Stop('klogsrv closed the stream (another client holding it?), so storage damage cannot be watched')
        return 'done', 'stream open, quiet so far (an idle console); every later line is scanned'
    run.scan()
    return 'done', f'{run.klog.lines} lines, no storage damage, no OnionHEN'


def logs(ftp, directory, below):
    lines = []
    try:
        ftp.retrlines('LIST ' + directory, lines.append)
    except ftplib.error_perm:
        return []
    found = []
    for line in lines:
        fields = line.split(None, 8)
        if len(fields) < 9 or fields[8] in ('.', '..'):
            continue
        path = f'{directory}/{fields[8]}'
        if line.startswith('d'):
            found += logs(ftp, path, below) if below else []
        elif path.endswith(('.log', '.txt')):
            found.append(path)
    return found


def s5_salvage(run):
    boot = console_guard.load().get('boot_started', 0)
    folder = KLOG / f"salvage-{time.strftime('%Y%m%d-%H%M%S', time.localtime(boot))}"
    if folder.is_dir():
        return 'done', 'salvaged once this boot'
    found = [path for directory, below in SALVAGE for path in logs(run.ftp, directory, below)]
    if run.check:
        return ('todo' if found else 'done'), f'{len(found)} log files'
    folder.mkdir(parents=True)
    for path in found:
        data = run.retr(path) or b''
        (folder / path.strip('/').replace('/', '_')).write_text(run.mask(redact(data.decode('utf-8', 'replace'))))
    return ('did' if found else 'done'), f'{len(found)} log files'


def s6_dev_conf(run):
    raw = run.retr(DEV_CONF_PATH)
    if raw == DEV_CONF:
        return 'done', 'loopback 8080'
    if raw is not None:
        raise Stop(f'{DEV_CONF_PATH} holds {raw[:80]!r}, not the loopback setting: it may point at '
                   'tools/ps5log-sink.py on purpose; change it by hand')
    if run.check:
        return 'todo', 'missing'
    with run.writing('dev.conf'):
        deploy_folder.make_directories(run.ftp, {'/data/homebrew'})
        run.ftp.storbinary('STOR ' + DEV_CONF_PATH, io.BytesIO(DEV_CONF))
    if run.retr(DEV_CONF_PATH) != DEV_CONF:
        raise Stop(f'{DEV_CONF_PATH} reads back otherwise')
    return 'did', 'written and read back'


def s7_title(run):
    files = sum(1 for path in DIST.rglob('*') if path.is_file())
    differ, digest = verify.title_mismatches(run.ftp, DIST)
    if differ and run.check:
        return 'todo', f'{len(differ)} of {files} files differ'
    if differ:
        ordered = [f for f in differ if f not in title_folder.LAST] + [f for f in title_folder.LAST if f in differ]
        with run.writing('title'):
            made = set()
            for name in ordered:
                title_folder.upload(run.ftp, DIST / name, f'/data/homebrew/{TITLE}/{name}', made)
        left, digest = verify.title_mismatches(run.ftp, DIST)
        if left:
            raise Stop(f'{len(left)} title files still differ after the upload')
    if not run.check:
        console_guard.record_verified('title', digest, 'console-setup: every file read back by sha256 with SELF off')
    run.facts['title_files'] = files
    return ('did', f'{len(differ)} of {files} files sent') if differ else ('done', f'{files} files equal')


def runtime_problems(run):
    problems = verify.runtime_problems(run.ftp, STAGED)
    if deploy_folder.listing(run.ftp, DATA + '/prefixes/probe') is None:
        problems.append('no probe prefix (the gate runs in it)')
    if not console_guard.load().get('deploys', {}).get('runtime', {}).get('verified', True):
        problems.append('the guard records the runtime as not verified')
    return problems


def s8_runtime(run):
    problems = runtime_problems(run)
    if not problems:
        return 'done', 'the deploy manifest is the staged runtime'
    if run.check:
        return 'todo', f'{len(problems)} differences, first: {problems[0]}'
    code, lines = run.tool('tools/deploy-wine.py')
    last = lines[-1] if lines else ''
    if code or not RUNTIME_DONE.fullmatch(last):
        raise Stop(f'tools/deploy-wine.py exited {code}: {last[-200:]}')
    left = runtime_problems(run)
    if left:
        raise Stop(f'after tools/deploy-wine.py, {len(left)} differences, first: {left[0]}')
    run.facts['runtime'] = last
    return 'did', last


def lapy_firmware(run):
    """The firmware the Lapy daemon last logged (None before the title first ran on this install); another one stops."""
    found = re.findall(rb'\bfirmware=(\w+)', run.retr(DATA + '/logs/lapy-service.log') or b'')
    firmware = found[-1].decode() if found else None
    if firmware not in (None, *FIRMWARES):
        raise Stop(f'the console runs firmware {firmware}; this setup is pinned to {FIRMWARE} (the 10.01 image): '
                   'reinstall that image, or re-pin FIRMWARE deliberately', final=True)
    return firmware


def prefix_state(run):
    """The mihawk prefix finished the update that ends its first session (patch 0952: ' wow64') on the pinned firmware
    (the Lapy daemon's last report; another one stops)."""
    stamp = run.retr(DATA + '/prefixes/mihawk/.update-timestamp') or b''
    firmware = lapy_firmware(run)
    run.facts['firmware'] = firmware
    return stamp.strip().endswith(b' wow64') and firmware in FIRMWARES


def wait_idle(run):
    """deploy-folder and begin_write check idle once, without waiting for a title that is still closing."""
    deadline = time.monotonic() + 60
    while ' count=0 ' not in procs(run.settings) + ' ':
        if time.monotonic() > deadline:
            raise Stop('the title still runs 60 s after the prefix session: close it on the console (never forced)')
        time.sleep(1)


def s9_prefix(run):
    if prefix_state(run):
        return 'done', f'prefix ready, firmware {run.facts.get("firmware") or FIRMWARE}'
    if run.check:
        return 'todo', 'the mihawk prefix is not set up'
    name = next_name('console-setup-prefix')
    run.klog.close()   # klogsrv may serve one client; the run opens its own
    run.scan()         # the last lines before the launch
    try:
        code, _ = run.tool('tools/run-console.py', '--name', name, '--profile', 'mihawk-os',
                           '--seconds', str(PREFIX_SECONDS))
    finally:
        run.open_klog()
    text = [line for path in [KLOG / f'{name}.log', *sorted(KLOG.glob(f'{name}-*.log'))] if path.is_file()
            for line in path.read_text(errors='replace').splitlines()]
    damage = console_guard.storage_damage(text)
    if damage:
        console_guard.record_storage(name, damage)
        raise Stop(f'{len(damage)} storage-damage lines in the logs of {name}: stop all console work for the day')
    onion = sum('OnionHEN' in line for line in text)
    if onion:
        raise Stop(f'OnionHEN was loaded during {name} ({onion} lines): restart without it')
    evidence = EVIDENCE / f'{name}.json'
    result = json.loads(evidence.read_text()) if evidence.is_file() else {}
    if code or not result.get('passed'):
        raise Stop(f'the prefix session {name} did not pass (exit {code}, {result.get("result")})')
    missing = [good for good in GOOD_LINES if not any(good in line for line in text)]
    if missing:
        raise Stop(f'{name} passed without {missing[0]!r} in its logs')
    if not prefix_state(run):
        raise Stop(f'the prefix is unfinished after {name}: run console-setup again (one more prefix-only session)')
    run.facts['prefix_session'] = name
    wait_idle(run)
    return 'did', name


def remote_sha(ftp, path):
    digest = hashlib.sha256()
    try:
        ftp.retrbinary('RETR ' + path, digest.update, deploy_folder.BLOCK)
    except ftplib.error_perm:
        return None
    return digest.hexdigest()


def s10_read_back(run):
    run.klog.replay()
    run.scan()
    if not run.deep:
        return 'skipped', 'the read-back runs with --deep'
    expected, bad = {}, 0   # path -> (sha256, runtime?)
    runtime = run.retr(verify.RUNTIME_MANIFEST)
    staged = json.loads(STAGED.read_text())
    expected.update({f'{DATA}/{key}': (staged.get(key), True) for key in json.loads(runtime or b'{}')})
    runtime_bad = runtime is None
    for count, (path, (digest, in_runtime)) in enumerate(expected.items(), 1):
        if remote_sha(run.ftp, path) != digest:
            bad += 1
            runtime_bad |= in_runtime
        if count % 500 == 0:
            print(f'  read back {count}/{len(expected)}', flush=True)
    bad += runtime is None
    run.facts['read_back'] = {'files': len(expected), 'mismatches': bad}
    if not run.check:
        if runtime_bad:
            state = console_guard.load()
            state.setdefault('deploys', {})['runtime'] = {'verified': False, 'why': f'{run.name}: read-back differs'}
            console_guard.save(state)
        else:
            console_guard.record_verified('runtime', sha256(runtime),
                                          'console-setup: every file read back by sha256 with SELF off')
    if bad:
        raise Stop(f'{bad} of {len(expected)} files do not match their manifests by sha256 (SELF off): the storage '
                   'may be failing; a person must look before any launch')
    return 'done', f'{len(expected)} files match'


STEPS = (('S0', 'host', s0_host), ('S1', 'reach', s1_reach), ('S2', 'guard', s2_guard),
         ('S3', 'control', s3_control), ('S4', 'klog', s4_klog), ('S5', 'salvage', s5_salvage),
         ('S6', 'dev.conf', s6_dev_conf), ('S7', 'title', s7_title), ('S8', 'runtime', s8_runtime),
         ('S9', 'prefix', s9_prefix), ('S10', 'read-back', s10_read_back))


def evidence(run, stopped):
    guard = console_guard.load()
    return {'run': run.name, 'result': 'stopped' if stopped else 'ready', 'stopped_at': stopped,
            'steps': run.steps,
            'klog': run.klogs and {'connections': len(run.klogs), 'lines': sum(k.lines for k in run.klogs),
                                   'storage_damage': sum(len(k.damage) for k in run.klogs),
                                   'onionhen': sum(k.onion for k in run.klogs)},
            'build': run.facts.get('build'), 'title_files': run.facts.get('title_files'),
            'runtime': run.facts.get('runtime'), 'prefix_session': run.facts.get('prefix_session'),
            'firmware': run.facts.get('firmware'),
            'read_back': run.facts.get('read_back'), 'last_console_write': guard.get('last_write'),
            'sync': guard.get('sync')}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--check', action='store_true', help='read only; exit 0 all done, 2 something to do, 1 blocked')
    parser.add_argument('--deep', action='store_true', help='with --check: read every deployed file back too')
    parser.add_argument('--until', choices=[number for number, _, _ in STEPS],
                        help='stop after this step (e.g. S8: deploy, but run no session before a measurement)')
    args = parser.parse_args(argv)
    if args.deep and not args.check:
        parser.error('--deep goes with --check: an apply run always reads back')
    run, stopped = Run(args.check, args.deep), None
    try:
        for index, (number, label, step) in enumerate(STEPS):
            started, final = time.monotonic(), False
            try:
                if run.klog and not run.check:
                    run.scan()   # the next step boundary after a klog hit
                state, detail = step(run)
            except Stop as stop:
                state, detail, final = 'blocked' if run.check else 'stopped', str(stop), stop.final
            except (SystemExit, RuntimeError, OSError, EOFError, ValueError, ftplib.Error) as error:
                if isinstance(error, OSError) and error.filename:   # the evidence never holds a PC path or file name
                    print('  ' + run.mask(error), flush=True)
                    error = f'{type(error).__name__}: {error.strerror} (errno {error.errno})'
                state, detail = 'blocked' if run.check else 'stopped', str(error)
            run.steps[number] = {'step': label, 'state': state, 'seconds': round(time.monotonic() - started, 1),
                                 'detail': run.mask(detail)}
            print(f'{number} {label}: {state}: {run.mask(detail)}', flush=True)
            if state == 'stopped' or (state == 'blocked' and (final or index < 2)):
                stopped = number
                break
            if number == args.until:
                stopped = f'{number} (--until)'
                break
    finally:
        if run.klog:
            run.klog.close()
        if run.ftp:
            with contextlib.suppress(OSError, EOFError, ftplib.Error):
                run.ftp.close()
    states = [step['state'] for step in run.steps.values()]
    if run.check:
        return 1 if 'blocked' in states else 2 if 'todo' in states else 0
    EVIDENCE.mkdir(exist_ok=True)
    (EVIDENCE / f'{run.name}.json').write_text(json.dumps(evidence(run, stopped), indent=1) + '\n')
    if stopped:
        print(f'stopped at {stopped}: evidence/{run.name}.json')
        return 1
    print(f'ready: evidence/{run.name}.json\n'
          'before tools/gate.py run: export PROTON_TLS_TRUSTED_PORT=1749 PROTON_TLS_UNTRUSTED_PORT=1750')
    return 0


if __name__ == '__main__':
    sys.exit(main())
