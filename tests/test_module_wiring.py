#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""The callers of wine/ps5/pw_wine_prx.h's wiring rule, which tests/test_crash_replays.c ("wired PRX module") runs but
cannot compile: the payload loader wires every module segment pw_prx_wired names, text and read-only data (an evicted
page of either is a "pager error" that kills the helper, two runs of 2026-10-06), and ntdll's
stop_unload unwires them before the kernel unloads the module (tearing down wired memory hangs the console, "unwire
timeout").

And the hang backstop's wiring (wine/ps5/pw_wine_process.h, src/pw_control_watch.h), which only the console can run:
graphics patch 0017's broker client stamps the GPU reason once, when it gives the broker up, through the lookup ntdll's
module loader hands it (patch 1135 no longer stamps Vulkan results in win32u), patch 1135 stamps an exit before
NtTerminateProcess's first lock, the loader's doom path ends the process without fsync or a Wine call, the census
prints its lock-free lines before image_stats, which waited 160 s on the virtual mutex in a run of 2026-10-15, and the
stall watch stamps STALL and lists the helper's threads without a lookup, an allocation or a Wine lock."""
from pathlib import Path
import os
import re
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[1]


def body(path, name, kind='int '):
    source = (ROOT / path).read_text()
    start = re.search(r'^static ' + re.escape(kind) + name + r'\(.*?\n\{\n', source, re.M | re.S)
    assert start, f'{name} not found in {path}'
    return source[start.end():source.index('\n}\n', start.end())]


def patch_files(number, directory='wine/patches'):
    """The files patch `number` changes, each as its list of diff lines (' ', '+', '-' prefixed)."""
    [path] = (ROOT / directory).glob(f'{number}-*.patch')
    files = {}
    for part in path.read_text().split('\ndiff --git a/')[1:]:
        files[part.split(' ', 1)[0]] = part.splitlines()
    return files


def foundation_commit_files(commit):
    """The files a PS5_Vulkan commit changes, as patch_files gives them: the graphics patches are commits there now
    (tools/setup-graphics-foundation.sh pins the revision; the checkout is the one it exports from)."""
    fork = os.environ.get('PS5_VULKAN_GIT') or str(ROOT.parent / 'PS5_Vulkan')
    text = subprocess.run(['git', '-C', fork, 'show', '--format=', commit], check=True, capture_output=True, text=True).stdout
    files = {}
    for part in ('\n' + text).split('\ndiff --git a/')[1:]:
        files[part.split(' ', 1)[0]] = part.splitlines()
    return files


class ModuleWiring(unittest.TestCase):
    def test_the_loader_wires_what_pw_prx_wired_names(self):
        text = body('native/proton_payload_child.c', 'payload_prepare_module')
        rule = text.index('if(!pw_prx_wired(segments[i].protection))continue;')
        self.assertLess(rule, text.index('mlock(segments[i].address,segments[i].size)'))
        self.assertEqual(text.count('continue;'), 2)   # the rule, then the text-only log line after mlock

    def test_unload_unwires_first(self):
        text = body('wine/ps5/pw_wine_dl_libc.c', 'stop_unload')
        self.assertLess(text.index('pw_prx_unwire(info,munlock)'), text.index('sceKernelStopUnloadModule('))


class HangBackstop(unittest.TestCase):
    def test_1135_leaves_vulkan_results_alone(self):
        # In gate-hang-a-2026-10-15-01 a present the broker refused and answered returned VK_ERROR_DEVICE_LOST, and
        # win32u's stamp doomed gpu-window32/64, whose programs presented on: the GPU reason is the broker client's.
        self.assertEqual([name for name in patch_files(1135) if name.startswith('dlls/win32u/')], [])

    def test_0017_stamps_the_gpu_reason_once_when_the_client_gives_the_broker_up(self):
        # PS5_Vulkan d14e738, once PS5_Proton's graphics patch 0017
        lines = foundation_commit_files('d14e738dec4f596e1e6d060362e0d42f4a83f608')['tooling/gpu-broker/client.c']
        new = [line[1:] for line in lines if line[:1] in ' +']   # the patched file's lines in its hunks, in order
        # Both places that set broken (rpc's failed call, the free path) now go through broker_lost.
        self.assertEqual(sum('__atomic_store_n(&broken,1' in line for line in lines if line.startswith('-')), 2)
        self.assertEqual([line for line in new if '&broken,1' in line],
                         ['    if(__atomic_exchange_n(&broken,1,__ATOMIC_RELAXED))return;'])
        self.assertIn('    if(!okay)broker_lost(lost,operation,expected);', new)
        self.assertIn('    if(rpc(&message,NULL)) { broker_lost("a free it did not take back",PW_GPU_FREE,message.sequence);'
                      'return; }', new)
        # One line and one stamp, on the first 0->1 transition only.
        lost = new.index('static void broker_lost(const char *why,unsigned operation,unsigned sequence)')
        self.assertEqual(new[lost + 2], '    if(__atomic_exchange_n(&broken,1,__ATOMIC_RELAXED))return;')
        self.assertTrue(new[lost + 3].startswith('    fprintf(stderr,"gpu-client: the broker is given up on: %s'))
        self.assertEqual(new[lost + 4], '    if(doomed)doomed(1 /* PW_CHILD_DOOM_GPU */);')
        self.assertEqual(sum('doomed(' in line for line in new if not line.lstrip().startswith(('/*', '*'))), 1)
        # Looked up once, in bind before its first call (rpc takes the lock), never under the rpc lock.
        lookup = '    if(module_lookup)doomed=(void (*)(unsigned))module_lookup(NULL,"pw_ps5_child_doomed");'
        self.assertEqual([line for line in new if 'doomed=' in line], [lookup])
        bind = new.index('int ps5_gpu_client_bind(int fd, bool reserved)')
        self.assertLess(bind, new.index(lookup))
        self.assertLess(new.index(lookup), new.index('    broker=fd;', bind))

    def test_the_loader_hands_the_client_its_lookup(self):
        # The client's own dlsym is libkernel's, which finds no application module's export.
        loader = (ROOT / 'wine/ps5/pw_wine_dl.c').read_text()
        self.assertIn('pw_prx_lookup(module->descriptor,"pw_module_lookup");', loader)
        self.assertLess(loader.index('if(lookup)lookup(pw_wine_dl_sym);'), loader.index('if(start)(void)start(0,NULL);'))
        recipe = (ROOT / 'tools/link_gpu_client.sh').read_text()
        wsi = recipe.split('"$out/obj/client_desc.c" proton_gpu_vulkan_probe vkGetInstanceProcAddr', 1)[1]
        self.assertIn('pw_module_lookup', wsi.split('\nelse\n', 1)[0].split())

    def test_1135_stamps_the_exit_before_its_flush(self):
        lines = patch_files(1135)['dlls/ntdll/unix/process.c']
        entry = lines.index(' NTSTATUS WINAPI NtTerminateProcess( HANDLE handle, LONG exit_code )')
        stamp = lines.index('+    if (!handle || handle == GetCurrentProcess()) pw_ps5_child_doomed( 2 /* PW_CHILD_DOOM_EXIT */ );')
        self.assertLess(entry, stamp)
        self.assertLess(stamp, lines.index('     wb_flush_all();'))

    def test_the_literals_are_the_reasons(self):
        header = (ROOT / 'wine/ps5/pw_wine_process.h').read_text()
        self.assertIn('enum { PW_CHILD_DOOM_GPU = 1, PW_CHILD_DOOM_EXIT, PW_CHILD_DOOM_ENGINE, PW_CHILD_DOOM_TEST,', header)

    def test_1135_watches_the_virtual_mutex_only(self):
        files = patch_files(1135)
        private = '\n'.join(files['dlls/ntdll/unix/unix_private.h'])
        self.assertEqual(private.count('+    if (!process_exiting && mutex == pw_wine_perf_vlock_mutex)'), 2)
        self.assertIn('+        pw_wine_perf_vlock_lock( mutex, __builtin_return_address( 0 ) );', private)
        self.assertIn('+        pw_wine_perf_vlock_unlock( mutex );', private)
        virtual = files['dlls/ntdll/unix/virtual.c']
        self.assertLess(virtual.index('     pthread_mutex_init( &virtual_mutex, &attr );'),
                        virtual.index('+    pw_wine_perf_vlock_mutex = &virtual_mutex;'))

    def test_what_the_loader_and_win32u_look_up_is_exported(self):
        recipe = (ROOT / 'tools/build-wine-ps5.sh').read_text()
        exported = set(recipe.split('gen_prx_descriptor.py" "$prx/obj/ntdll_desc.c"', 1)[1].split('python3', 1)[0].split())
        for name in ('pw_ps5_child_doomed', 'pw_ps5_child_doom', 'pw_wine_perf_vlock_read', '__wine_ps5_dmem_holder'):
            self.assertIn(name, exported)
        loader = (ROOT / 'native/proton_wine_child.c').read_text()
        for name in ('pw_ps5_child_doomed', 'pw_ps5_child_doom', 'pw_wine_perf_vlock_read', '__wine_ps5_dmem_holder'):
            self.assertIn(f'"{name}"', loader)

    def test_the_doom_names_follow_the_reasons(self):
        header = (ROOT / 'wine/ps5/pw_wine_process.h').read_text()
        order = re.search(r'enum \{ PW_CHILD_DOOM_GPU = 1,(.*?)PW_CHILD_DOOMS = (\d+) \};', header, re.S)
        reasons = ['gpu'] + [name.lower() for name in re.findall(r'PW_CHILD_DOOM_(\w+)', order.group(1))]
        self.assertEqual(len(reasons), int(order.group(2)))
        loader = (ROOT / 'native/proton_wine_child.c').read_text()
        names = re.search(r'doom_names\[PW_CHILD_DOOMS\] = \{(.*?)\};', loader).group(1)
        self.assertEqual(re.findall(r'"(\w+)"', names), reasons)
        grace = re.search(r'pw_control_doom_grace_ms\[PW_CHILD_DOOMS\] = \{(.*?)\};',
                          (ROOT / 'src/pw_control_watch.h').read_text()).group(1)
        self.assertEqual(len(grace.split(',')), len(reasons))
        runner = (ROOT / 'tools/run-console.py').read_text()
        self.assertEqual(re.findall(r"'(\w+)': \d+", re.search(r'DOOM_GRACE_MS = \{(.*?)\}', runner).group(1)), reasons)

    def test_the_stall_watch_stamps_and_lists_without_wine(self):
        # The log thread stamps STALL through ntdll's lock-free export and lists the threads into a static buffer:
        # no lookup, no allocation, no Wine lock there (a stalled helper's malloc may wait on the engine lock).
        watch = body('native/proton_wine_child.c', 'stall_watch')
        # The rule sees EXIT's stamp (read before it) and the line sees STALL's (read again after the stamp).
        rule = watch.index('pw_control_stalled(now.owner, vlock_us, now.doing, stamps[PW_CHILD_DOOM_EXIT - 1],')
        # A hold counts from the app's last return, and nothing is stamped while it is away or just back (src/pw_suspend.h).
        self.assertLess(watch.index('helper_suspend_held(payload_monotonic_ms(), &since_us)'), rule)
        self.assertIn('if (!stall_list_ms && !held && pw_control_stalled(', watch)
        self.assertLess(watch.index('if (doom) (void)doom(stamps'), rule)
        self.assertLess(rule, watch.index('doomed(PW_CHILD_DOOM_STALL);'))
        self.assertLess(watch.index('doomed(PW_CHILD_DOOM_STALL);'), watch.rindex('if (doom) (void)doom(stamps'))
        threads = body('native/proton_wine_child.c', 'stall_threads')
        self.assertIn('KERN_PROC_PID | KERN_PROC_INC_THREAD, (int)getpid()', threads)
        for text in (watch, threads):
            for call in ('malloc(', 'census_list(', 'pw_prx_lookup', 'fsync(', 'mutex'):
                self.assertNotIn(call, text)
        sync = body('native/proton_wine_child.c', 'log_sync', 'void *')
        self.assertLess(sync.index('stall_watch()'), sync.index('stall_threads()'))
        loader = (ROOT / 'native/proton_wine_child.c').read_text()
        self.assertIn('void (*doomed)(unsigned)=(void *)pw_prx_lookup(wine.descriptor,"pw_ps5_child_doomed");', loader)

    def test_the_doom_path_ends_without_fsync_or_wine(self):
        text = body('native/proton_wine_child.c', 'doom_tick')
        self.assertNotIn('fsync(', text)
        self.assertNotIn('pw_prx_lookup', text)
        self.assertNotIn('_tally(', text)
        loop = body('native/proton_wine_child.c', 'wine_child_run', 'void ')
        loop = loop[loop.index('PwControlDoom doom_watch={0};'):]
        self.assertLess(loop.index('if (doom_tick(&doom_watch,now,helper_suspend_tick(now))) return;'), loop.index('POLLHUP'))
        # A due doom waits while the app is away or just back: ending would free memory frozen GPU work may use.
        self.assertLess(text.index('if (held) {'), text.index('payload doomed reason='))

    def test_a_helper_ends_with_the_engines_totals(self):
        loop = body('native/proton_wine_child.c', 'wine_child_run', 'void ')
        loop = loop[loop.index('POLLHUP'):]
        total = loop.index('mmu4k_census((void *)pw_prx_lookup(wine.descriptor,"__wine_ps5_mmu4k_stats"),1);')
        self.assertLess(loop.index('direct_section_tally();'), total)
        self.assertLess(total, loop.index('fsync(output_fd)'))

    def test_the_census_prints_lock_free_lines_before_image_stats(self):
        text = body('native/proton_wine_child.c', 'payload_telemetry', 'void *')
        call = text.index('image_stats(&views, &image_bytes);')
        self.assertLess(text.index('vlock_census();'), call)
        self.assertLess(text.index('mmu4k_census(mmu4k_stats, 0);'), call)
        self.assertEqual(text.count('mmu4k_census('), 1)
        self.assertLess(text.index('if (held.owner && held.held_us >= 1000000) {'), call)


class EngineHandOver(unittest.TestCase):
    """Patch 1136: a 4 KiB MMU run reads the virtual mutex's waiters and hands it over, starts none once the process
    exits (mutex_lock does nothing then) and keeps a real hold if it started before, and a fault inside the engine,
    which leaves the run's lock held, dooms the helper."""

    def test_the_ops_carry_the_waiters_count(self):
        virtual = patch_files(1136)['dlls/ntdll/unix/virtual.c']
        self.assertIn('+extern uint64_t pw_wine_perf_vlock_waiters;', virtual)
        self.assertIn('+                                              &pw_wine_perf_vlock_waiters };', virtual)
        self.assertIn('uint64_t pw_wine_perf_vlock_waiters;', (ROOT / 'wine/ps5/pw_wine_perf.c').read_text())

    def test_no_run_starts_once_the_process_exits(self):
        virtual = patch_files(1136)['dlls/ntdll/unix/virtual.c']
        wanted = virtual.index(' static BOOL ps5_mmu4k_wanted( struct ntdll_thread_data *data, char *page, '
                               'void *addr )')
        check = virtual.index('+    if (process_exiting || !pw_wine_mmu4k_ready() || !data->ps5_fault_own || '
                              'data->ps5_step_page[0] ||')
        self.assertLess(wanted, check)
        self.assertLess(check, virtual.index('         return FALSE;', check))

    def test_a_run_holds_the_mutex_for_real_once_the_process_exits(self):
        # mutex_lock does nothing after process_exiting; a run that asked just before still needs the exclusion
        virtual = patch_files(1136)['dlls/ntdll/unix/virtual.c']
        for op, recorded in (('lock', ', __builtin_return_address( 0 )'), ('unlock', '')):
            start = virtual.index(f' static void ps5_mmu4k_{op}( void *context )')
            end = virtual.index(' }', start)
            self.assertLess(virtual.index(f'+    if (process_exiting) pthread_mutex_{op}( &virtual_mutex );', start), end)
            self.assertLess(virtual.index(f'+    else pw_wine_perf_vlock_{op}( &virtual_mutex{recorded} );', start), end)
            self.assertLess(virtual.index(f'-    mutex_{op}( &virtual_mutex );', start), end)

    def test_a_fault_in_the_engine_stamps_the_engine_reason(self):
        lines = patch_files(1136)['dlls/ntdll/unix/signal_x86_64.c']
        branch = lines.index('     if (action == PW_WINE_MMU4K_NOT_OURS)')
        stamp = lines.index('+        pw_ps5_child_doomed( 3 /* PW_CHILD_DOOM_ENGINE */ );')
        self.assertLess(branch, stamp)
        self.assertLess(stamp, lines.index('         return FALSE;', branch))
        calls = [line for line in lines if line[:1] in '+-' and 'doomed(' in line and 'extern' not in line]
        self.assertEqual(calls, ['+        pw_ps5_child_doomed( 3 /* PW_CHILD_DOOM_ENGINE */ );'])

    def test_the_census_names_nine_exits_in_the_engine_order(self):
        header = (ROOT / 'wine/ps5/pw_wine_mmu4k.h').read_text()
        order = re.search(r'enum \{ (PW_WINE_MMU4K_EXIT_IDLE.*?)PW_WINE_MMU4K_EXITS \};', header, re.S).group(1)
        engine = [name.lower() for name in re.findall(r'PW_WINE_MMU4K_EXIT_(\w+)', order)]
        census = body('native/proton_wine_child.c', 'mmu4k_census', 'void ')
        names = re.search(r'exits\[\] = \{(.*?)\};', census, re.S).group(1)
        self.assertEqual(re.findall(r'"(\w+)"', names), engine)
        self.assertEqual(engine[-1], 'yield')


if __name__ == '__main__':
    unittest.main()
