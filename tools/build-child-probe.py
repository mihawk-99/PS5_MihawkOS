#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build an isolated direct-child, service preflight, bounded service helper or self-install probe."""
import argparse
import hashlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
TESTER_TITLE = 'PPSA99171'   # the tester probe's title (native/proton_tester_probe.h); MihawkOS is PPSA99170
OVERLAY = ('ps5/src/main.cpp', 'ps5/CMakeLists.txt')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kind', choices=('direct', 'service-preflight', 'service-native','wine-owner','install-probe'), default='direct')
    parser.add_argument('--elevate-gpu-helper', action='store_true', help='use the existing filesystem elevation client in the isolated native GPU helper')
    parser.add_argument('--vulkan-gpu-helper',action='store_true',help='two offscreen Vulkan clients using the title GPU broker')
    parser.add_argument('--draw-gpu-helper',action='store_true',help='draw a shader-based offscreen triangle in each Vulkan client')
    parser.add_argument('--present-gpu-helper',action='store_true',help='present child shader draws through the owning title display')
    parser.add_argument('--wine-gpu-broker',action='store_true',help='owner-only Wine server with child GPU transports')
    parser.add_argument('--tester', action='store_true', help='with --kind service-native: the tester probe, its own title PPSA99171 '
                        '(native/proton_tester_probe.h), a complete title folder in dist/PPSA99171 with the helper beside it')
    parser.add_argument('--flexible-memory',help='experiment: the flexible memory the title asks the kernel for (an experimental PS5_Vulkan native tool, --native-tool)')
    parser.add_argument('--parameter-pointers-in-file',help='experiment: also store the process-parameter pointers in the file at this base')
    parser.add_argument('--native-tool',type=Path,help='experiment: the native tool build that links the title')
    parser.add_argument('--work-root',type=Path,default=ROOT/'.deps/child-probe')
    args = parser.parse_args()
    subprocess.run(['bash',str(ROOT/'tools/setup-graphics-foundation.sh')],check=True,stdout=subprocess.DEVNULL)
    if args.wine_gpu_broker and args.kind!='wine-owner':parser.error('--wine-gpu-broker requires --kind wine-owner')
    if args.tester and args.kind != 'service-native': parser.error('--tester requires --kind service-native')
    if args.present_gpu_helper:args.draw_gpu_helper=True
    if args.draw_gpu_helper:args.vulkan_gpu_helper=True
    if args.vulkan_gpu_helper:
        args.elevate_gpu_helper=True
    if args.elevate_gpu_helper and args.kind != 'service-native':
        parser.error('--elevate-gpu-helper requires --kind service-native')
    if args.kind=='wine-owner':
        overlay=OVERLAY+('native/proton_wine.cpp','native/proton_gpu_identity.h','wine/ps5/pw_wine_process.h')
    else:
        native = {'direct': 'proton_child_probe', 'install-probe': 'proton_install_probe'}.get(args.kind, 'proton_service_preflight')
        overlay = OVERLAY + ('native/' + native + '.h', 'native/' + native + '.cpp')
        if args.kind == 'install-probe':   # its tls step: the title's own TLS (src/pw_https.h)
            overlay += ('src/pw_https.c', 'src/pw_https.h', 'platform/tls/pw_mbedtls_config.h', 'tools/setup-mbedtls.sh')
    if args.kind == 'service-native' or args.wine_gpu_broker:
        overlay += ('native/proton_service_helper.c','native/proton_gpu_broker_probe.h','native/proton_flex_probe.h','native/proton_gpu_broker_probe.cpp','native/proton_gpu_memory_probe.cpp','native/proton_fd_budget_probe.cpp','native/proton_channel_probe.cpp','native/proton_gpu_server_probe.cpp','native/proton_gpu_display.cpp','native/proton_gpu_display.h','native/proton_gpu_server.h','src/pw_gpu_broker.h','wine/ps5/pw_wine_process_io.c','wine/ps5/pw_wine_process_io.h')
    if args.tester:
        overlay += ('native/proton_tester_probe.h', 'native/proton_tester_probe.cpp', 'native/proton_loading.h', 'native/proton_loading.cpp')
    if args.vulkan_gpu_helper:
        overlay+=('native/proton_gpu_client.c','native/proton_gpu_vulkan_probe.c','native/proton_gpu_vulkan_exports.c','wine/ps5/pw_wine_prx.c','wine/ps5/pw_wine_prx.h','tools/link_gpu_client.sh','tools/gen_prx_descriptor.py','wine/ps5/prx_eh_frame.ld','native/proton_gpu_probe_spv.h','native/shaders/gpu-probe.vert','native/shaders/gpu-probe.frag','tools/generate-gpu-probe-shaders.py')
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    if (args.flexible_memory or args.parameter_pointers_in_file) and not args.native_tool:
        parser.error('--flexible-memory and --parameter-pointers-in-file need --native-tool')
    link_args=(['--flexible-memory',args.flexible_memory] if args.flexible_memory else [])+ \
              (['--parameter-pointers-in-file',args.parameter_pointers_in_file] if args.parameter_pointers_in_file else [])
    overlay+=('ps5/tools/link-title.sh','tools/setup-graphics-foundation.sh','src/pw_frame_page.h')
    sdk = Path(subprocess.check_output(['bash', str(ROOT / 'ps5/tools/setup-sdk.sh')], text=True).strip())
    # The pinned PS5_Vulkan with its RADV archive, as the production build links (tools/setup-pinned-radv.sh).
    vulkan = Path(subprocess.check_output(['bash', str(ROOT / 'tools/setup-pinned-radv.sh')], text=True).strip().splitlines()[-1])
    manifest = {'graphics_foundation_revision':(ROOT/'.deps/graphics-foundation/.revision').read_text().strip(),'revision': revision, 'kind': args.kind, 'elevate_gpu_helper': args.elevate_gpu_helper, 'vulkan_gpu_helper': args.vulkan_gpu_helper,'gpu_draw_helper':args.draw_gpu_helper,'gpu_present_helper':args.present_gpu_helper,'wine_gpu_broker':args.wine_gpu_broker,'tester':args.tester,
                'sdk_revision': (sdk / '.ps5-sdk-revision').read_text().strip(),
                'source_sha256': {name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in overlay},
                'radv_recipe_sha256': hashlib.sha256((vulkan / 'tools/radv-link.sh').read_bytes()).hexdigest()}
    if args.native_tool:
        manifest['native_tool_sha256']=hashlib.sha256(args.native_tool.resolve().read_bytes()).hexdigest()
        manifest['title_link_args']=link_args
    if args.kind == 'service-native':
        mesa_header=ROOT/'.deps/graphics-foundation/tooling/gpu-broker/radv_ps5_platform.h'
        manifest['radv_platform_header_sha256']=hashlib.sha256(mesa_header.read_bytes()).hexdigest()
        manifest['radv_archive_sha256']=hashlib.sha256((vulkan/'.deps/native/radv-release/lib/libvulkan_radeon.ps5.a').read_bytes()).hexdigest()
        helper_tool = Path(subprocess.check_output(['python3', str(ROOT / 'tools/build-helper-converter.py')], text=True).strip())
        manifest['helper_tool_sha256'] = hashlib.sha256(helper_tool.read_bytes()).hexdigest()
        manifest['helper_tool_revision'] = 'ab37acd506305ba3e575435e3ae9a351cfaf7cf4'
        manifest['helper_patch_sha256'] = hashlib.sha256((ROOT / 'tools/patches/native-helper-preload.patch').read_bytes()).hexdigest()
        manifest['helper_layout_sha256'] = hashlib.sha256((vulkan / 'tooling/native/ps5-pie.ld').read_bytes()).hexdigest()
        manifest['helper_symbols_sha256'] = hashlib.sha256((vulkan / 'tooling/native/app-symbols.map').read_bytes()).hexdigest()
        manifest['helper_preload_mask'] = '0x8000000000000002'
        manifest['radv_sdk_revision']=(vulkan/'.deps/native/ps5-payload-sdk/.ps5-sdk-revision').read_text().strip()
        manifest['radv_provenance_sha256']=hashlib.sha256((vulkan/'.deps/native/radv-release/PROVENANCE.txt').read_bytes()).hexdigest()
        if args.vulkan_gpu_helper:
            prx_tree=ROOT/'.deps/prx-foundation'
            if subprocess.check_output(['git','-C',str(prx_tree),'rev-parse','HEAD'],text=True).strip()!='30597512539e7edfde079cbcaf4a626bc0a948c5' or subprocess.check_output(['git','-C',str(prx_tree),'status','--porcelain'],text=True):
                raise RuntimeError('PRX converter checkout differs from its pin')
            prx_tool=prx_tree/'build/host/ps5-native-tool'
            manifest['prx_tool_revision']='30597512539e7edfde079cbcaf4a626bc0a948c5'
            manifest['prx_tool_sha256']=hashlib.sha256(prx_tool.read_bytes()).hexdigest()
        manifest['helper_recipe_sha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    identity = hashlib.sha256(json.dumps(manifest, sort_keys=True).encode()).hexdigest()
    work = args.work_root.resolve() / identity[:16]
    source = work / 'source'
    if not source.exists():
        archive = subprocess.check_output(['git', 'archive', revision], cwd=ROOT)
        source.mkdir(parents=True)
        with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
            tree.extractall(source, filter='data')
        for name in overlay:
            (source / name).parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, source / name)
        for sibling in ('PS5_Vulkan', 'PS5_PayloadSDK', 'PS5_VKHomebrewUI', 'PS5_RetroArch'):
            target = ROOT.parent / sibling
            link = work / sibling
            if target.exists() and not link.exists():
                link.symlink_to(target)
        if args.tester:
            # Its own title, beside MihawkOS: another title ID and content ID, so it can never replace PPSA99170's folder.
            param = json.loads((ROOT / 'ps5/sce_sys/param.json').read_text())
            param['titleId'] = TESTER_TITLE
            param['contentId'] = 'UP9000-' + TESTER_TITLE + '_00-PS5PROTONPROBE0'
            param['localizedParameters']['en-US']['titleName'] = 'MihawkOS memory probe'
            (source / 'ps5/sce_sys/param.json').write_text(json.dumps(param, indent=2) + '\n')
    build = work / 'build'
    environment = dict(os.environ, PS5_VKHOMEBREWUI=str(ROOT.parent / 'PS5_VKHomebrewUI'))
    environment.pop('PS5_NATIVE_TOOL',None);environment.pop('PS5_TITLE_LINK_ARGS',None)
    if args.native_tool:
        environment.update(PS5_NATIVE_TOOL=str(args.native_tool.resolve()),PS5_TITLE_LINK_ARGS=' '.join(link_args))
    subprocess.run(['cmake', '-S', str(source / 'ps5'), '-B', str(build), '-G', 'Ninja',
                    '-DCMAKE_TOOLCHAIN_FILE=' + str(sdk / 'toolchain/prospero.cmake'),
                    '-DPS5_VULKAN_DIR=' + str(vulkan), '-DCMAKE_BUILD_TYPE=Release',
                    '-DPS5_CHILD_PROBE=' + ('ON' if args.kind == 'direct' else 'OFF'),
                    '-DPS5_SERVICE_PREFLIGHT=' + ('ON' if args.kind == 'service-preflight' else 'OFF'),
                    '-DPS5_SERVICE_NATIVE=' + ('ON' if args.kind == 'service-native' else 'OFF'),
                    '-DPS5_INSTALL_PROBE=' + ('ON' if args.kind == 'install-probe' else 'OFF'),
                    '-DPS5_TESTER_PROBE=' + ('ON' if args.tester else 'OFF'),
                    # The install probe draws the production launcher under its notifications; proton_wine.cpp requires the broker.
                    '-DPS5_GPU_BROKER='+('ON' if args.wine_gpu_broker or args.kind in ('install-probe', 'service-native') else 'OFF'),
                    '-DCMAKE_VERBOSE_MAKEFILE=OFF'], check=True, env=environment)
    subprocess.run(['ninja', '-C', str(build), 'title'], check=True, env=environment)
    if args.kind == 'service-native':
        # This helper contains no RADV. The normal variant needs only ordinary
        # libkernel; the explicit elevation variant additionally initializes libc.
        helper = work / 'helper'
        helper.mkdir(exist_ok=True)
        environment['PS5_PAYLOAD_SDK'] = str(sdk)
        subprocess.run(['sh', str(vulkan / 'tooling/prospero-clang18'), '-std=c11',
                        '-O2', '-Wall', '-Wextra', '-Werror', '-fPIC', '-ffreestanding', '-fno-builtin',
                        '-fno-omit-frame-pointer', '-funwind-tables', '-c', str(source / 'native/proton_service_helper.c'),
                        *(['-DPROTON_NATIVE_VULKAN_HELPER'] if args.vulkan_gpu_helper else []),
                        *(['-DPROTON_NATIVE_GPU_ELEVATION'] if args.elevate_gpu_helper else []),
                        '-o', str(helper / 'helper.o')], check=True, env=environment)
        if args.vulkan_gpu_helper:
            subprocess.run([str(sdk/'bin/prospero-clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC','-c',str(source/'wine/ps5/pw_wine_prx.c'),'-o',str(helper/'prx.o')],check=True)
            subprocess.run(['bash',str(source/'tools/link_gpu_client.sh'),str(vulkan),str(work/'gpu-client'),str(prx_tool),str(sdk),'1' if args.draw_gpu_helper else '0','1' if args.present_gpu_helper else '0'],check=True)
            manifest['gpu_client_sha256']=hashlib.sha256((work/'gpu-client/gpu-client.prx').read_bytes()).hexdigest()
            manifest['gpu_client_needed']=json.loads((work/'gpu-client/needed.json').read_text())
        subprocess.run([str(sdk / 'bin/prospero-lld'), '-pie',
                        '--eh-frame-hdr', '-T', str(vulkan / 'tooling/native/ps5-pie.ld'),
                        '--version-script', str(vulkan / 'tooling/native/app-symbols.map'),
                        '-e', '_start', '-o', str(helper / 'llvm-pie.elf'),
                        str(helper / 'helper.o'),
                        *([str(helper/'prx.o')] if args.vulkan_gpu_helper else []),
                        *([str(sdk / 'target/lib/libps5platform.a'), '--as-needed'] if args.elevate_gpu_helper else []),
                        str(sdk / 'target/lib/libkernel.so'),
                        *([str(sdk / 'target/lib/libSceLibcInternal.so')] if args.elevate_gpu_helper else [])], check=True)
        tool = str(helper_tool)
        subprocess.run([tool, 'link', '--in', str(helper / 'llvm-pie.elf'),
                        '--out', str(helper / 'helper.elf'), '--stub-dir', str(sdk / 'target/lib'),
                        '--module-sdk', '0x02000009', '--companion-sdk', '0x08050001',
                        '--file-name', 'service-native-helper.bin',
                        '--preload-prx-flags', manifest['helper_preload_mask']], check=True)
        subprocess.run([tool, 'self', '--sign', '--in', str(helper / 'helper.elf'),
                        '--out', str(helper / 'helper.bin'), '--magic', '0x1D3D154F'], check=True)
        manifest['helper_sha256'] = hashlib.sha256((helper / 'helper.bin').read_bytes()).hexdigest()
        # Refuse an accidental reintroduction of title/graphics dependencies.
        dependencies = subprocess.check_output(['llvm-readelf', '-d',
                                                str(helper / 'llvm-pie.elf')], text=True)
        needed = [line for line in dependencies.splitlines() if '(NEEDED)' in line]
        expected = ['libkernel.sprx'] + (['libSceLibcInternal.sprx'] if args.elevate_gpu_helper else [])
        actual = [line.split('[')[1].split(']')[0] for line in needed]
        if sorted(actual) != sorted(expected):
            raise RuntimeError('Headless helper dependencies differ from the explicit kernel/libc policy')
        manifest['helper_needed'] = actual
    title = TESTER_TITLE if args.tester else 'PPSA99170'
    if args.tester:
        # The helper the probe spawns from its own folder (/app0/service-native-helper.bin).
        shutil.copyfile(work / 'helper/helper.bin', source / 'dist' / title / 'service-native-helper.bin')
        # The two fonts its verdict screen draws with (native/proton_loading.cpp), from the pinned HUI kit, and their licence.
        fonts = source / 'dist' / title / 'assets/hui/fonts'
        fonts.mkdir(parents=True, exist_ok=True)
        for name in ('Inter-Regular.ttf', 'Inter-SemiBold.ttf', 'Inter-LICENSE.txt'):
            shutil.copyfile(ROOT / '.deps/hui/third_party/fonts' / name, fonts / name)
    manifest.update(identity=identity, title=title, eboot_sha256=hashlib.sha256(
        (source / 'dist' / title / 'eboot.bin').read_bytes()).hexdigest())
    (work / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('Child probe build: ' + str(work), flush=True)


if __name__ == '__main__':
    main()
