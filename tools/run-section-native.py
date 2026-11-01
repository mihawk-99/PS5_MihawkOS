#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run the bounded shared-section paging/lease probe on an idle PS5.

Builds only repository-owned native sources using the pinned payload SDK.
Does not replace the title or read/write kernel memory. The native probe owns
its virtual guards and temporary files and prints one cleanup verdict.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT.parent / 'PS5_Vulkan/tools'))
import ps5_console
sys.path.insert(0, str(ROOT / 'tools'))
import console_guard


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--name', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[a-zA-Z0-9_-]+', args.name):
        parser.error('use a plain run name')
    evidence = ROOT / 'evidence' / (args.name + '.json')
    if evidence.exists():
        parser.error('use a new run name')
    sdk = ROOT / '.deps/native/ps5-payload-sdk'
    revision = (sdk / '.ps5-sdk-revision').read_text().strip()
    if revision != 'e105cf6f25801d9e9981812a89a3b74021d2d2a2':
        raise SystemExit('Payload SDK pin differs')
    sources = ['native/proton_section_probe.c', 'wine/ps5/pw_wine_section.c', 'src/pw_memory_file.c']
    inputs = sources + ['wine/ps5/pw_wine_section.h', 'src/pw_memory_file.h', 'tools/run-section-native.py']
    elf = ROOT / 'build/section-native-probe.elf'
    subprocess.run([str(sdk / 'bin/prospero-clang'), '-std=gnu11', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-pthread', *[str(ROOT / s) for s in sources], '-lkernel_sys',
                    '-o', str(elf)], check=True)
    settings = ps5_console.load_settings()
    mask = lambda text: re.sub(r'\b(?:\d{1,3}\.){3}\d{1,3}\b', '<console>', str(text))
    state = mask(ps5_console.ps5vkctl_command(settings, 'procs', timeout=5))
    if ' count=0 ' not in state + ' ':
        raise SystemExit('Console occupied; probe refused')
    console_guard.before_launch('bounded shared-section paging and descriptor leases')
    boundary = 'SectionNativeProbe'
    body = b''
    for name, data in [('args', b'section-probe'), ('pipe', b'1'), ('elf', elf.read_bytes())]:
        header = f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"'
        if name == 'elf':
            header += '; filename="section-native-probe.elf"'
        body += (header + '\r\n\r\n').encode() + data + b'\r\n'
    body += f'--{boundary}--\r\n'.encode()
    request = urllib.request.Request(f'http://{settings["host"]}:8080/elfldr', data=body,
        headers={'Content-Type': 'multipart/form-data; boundary=' + boundary})
    console_guard.after_launch()
    error = None
    try:
        with urllib.request.urlopen(request, timeout=45) as response:
            text = mask(response.read().decode(errors='replace'))
    except BaseException as exc:
        text, error = '', mask(type(exc).__name__ + ': ' + str(exc))
    passed = 'SECTION_NATIVE PASS' in text and 'backing_lease=PASS' in text
    record = {'run': args.name, 'payload': True, 'kernel_reads': False, 'kernel_writes': False,
        'no_overwrite': True, 'sdk_revision': revision,
        'inputs': {s: hashlib.sha256((ROOT / s).read_bytes()).hexdigest() for s in inputs},
        'elf_sha256': hashlib.sha256(elf.read_bytes()).hexdigest(),
        'passed': passed, 'completed': 'SECTION_NATIVE PASS' in text,
        'wine_integration_qualified': False, 'error': error, 'lines': text.splitlines()}
    (ROOT / 'klog' / (args.name + '.log')).write_text(text + (error or ''))
    evidence.write_text(json.dumps(record, indent=1) + '\n')
    print(text or error, end='\n')
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except SystemExit as exc:
        if isinstance(exc.code, str):
            raise SystemExit(re.sub(r'\b(?:\d{1,3}\.){3}\d{1,3}\b', '<console>', exc.code)) from None
        raise
    except (OSError, RuntimeError) as exc:
        raise SystemExit(re.sub(r'\b(?:\d{1,3}\.){3}\d{1,3}\b', '<console>', str(exc))) from None
