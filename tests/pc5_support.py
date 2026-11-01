# SPDX-License-Identifier: GPL-3.0-or-later
"""Host tests of the Wine patches written after reading PC5 (NOTICE.md, "PC5"): the pinned Wine's own source files, as
Git holds them at the pin, with the patch under test applied, compiled on the host and run. Nothing here builds Wine; a
test that needs a file the PS5 series changes says so and takes only files it leaves alone."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(os.environ.get("PC5_ROOT", Path(__file__).resolve().parents[1]))
WINE = ROOT / '.deps/wine-proton'
PIN = ROOT / '.deps/wine-proton.pin'


def have_pinned_wine():
    return WINE.is_dir() and PIN.is_file()


def patched_files(workdir, paths, patch_glob):
    """The files `paths` at the pin, copied under workdir, with the patch matching patch_glob applied to them. The
    series is not replayed: use this for files no other patch of wine/patches changes."""
    if not have_pinned_wine():
        raise unittest.SkipTest('no pinned Wine tree in .deps (tools/fetch-all.sh)')
    pin = PIN.read_text().strip()
    tar = subprocess.run(['git', '-C', str(WINE), 'archive', pin, *paths], check=True, capture_output=True).stdout
    subprocess.run(['tar', '-x', '-C', str(workdir)], input=tar, check=True)
    patch = next((ROOT / 'wine/patches').glob(patch_glob))
    subprocess.run(['git', 'apply', '--whitespace=nowarn', str(patch)], cwd=workdir, check=True,
                   capture_output=True)
    return patch


def pristine_files(workdir, paths):
    shutil.rmtree(workdir, ignore_errors=True)
    workdir.mkdir(parents=True)
    pin = PIN.read_text().strip()
    tar = subprocess.run(['git', '-C', str(WINE), 'archive', pin, *paths], check=True, capture_output=True).stdout
    subprocess.run(['tar', '-x', '-C', str(workdir)], input=tar, check=True)


def compile_and_run(sources, output, flags=(), timeout=120):
    built = subprocess.run(['cc', '-O2', '-w', *flags, '-o', str(output), *map(str, sources), '-lpthread'],
                           capture_output=True, text=True)
    if built.returncode:
        raise AssertionError('the host compiler refused the test:\n' + built.stderr[-2000:])
    return subprocess.run([str(output)], capture_output=True, text=True, timeout=timeout)


def scratch():
    return tempfile.TemporaryDirectory(prefix='pc5-test-')
