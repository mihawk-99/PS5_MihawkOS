#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Relink ntdll with the payload process adapter; read all base objects unchanged.

The link is the ntdll recipe from build-wine-ps5.sh. No Wine vendor source,
base object, or shared dependency is changed. Every reused input is hashed.
"""
import argparse,hashlib,json,re,shlex,shutil,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def run(args,**kw):return subprocess.check_output(list(map(str,args)),text=True,**kw)
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--base',type=Path,default=ROOT/'.deps/wine-gpu-broker')
 p.add_argument('--output',type=Path,default=ROOT/'build/payload-runtime')
 a=p.parse_args();base=a.base.resolve();out=a.output.resolve();prx=out/'prx';prx.mkdir(parents=True,exist_ok=True);(prx/'sce_module').mkdir(exist_ok=True)
 sdk=(ROOT/'.deps/native/ps5-payload-sdk').resolve();foundation=(ROOT/'.deps/prx-foundation').resolve();tool=foundation/'build/host/ps5-native-tool';pie=foundation/'tooling/native/ps5-pie.ld'
 if run(['git','-C',foundation,'rev-parse','HEAD']).strip()!='30597512539e7edfde079cbcaf4a626bc0a948c5':raise RuntimeError('PRX converter pin differs')
 if (sdk/'.ps5-sdk-revision').read_text().strip()!='e105cf6f25801d9e9981812a89a3b74021d2d2a2':raise RuntimeError('Payload SDK pin differs')
 report=json.loads((base/'report.json').read_text())
 if report['wine_commit']!='1c78596625336a73dbfc035dbbcf0dbdd95b06eb':raise RuntimeError('Wine pin differs')
 for name in ('ntdll','wineserver','win32u'):
  entry=report['prx']['modules'][name]
  if any(entry.get(k) for k in ('unresolved','errors','raw_syscalls','title_unbound')) or digest(base/'prx/sce_module'/f'{name}.prx')!=entry['sha256']:raise RuntimeError('Base runtime differs from its report')
  for suffix in ('.elf','.shared.elf'):shutil.copyfile(base/'prx'/f'{name}{suffix}',prx/f'{name}{suffix}')
  shutil.copyfile(base/'prx/sce_module'/f'{name}.prx',prx/'sce_module'/f'{name}.prx')
 obj=prx/'pw_wine_process.o'
 run([sdk/'bin/prospero-clang','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fPIC','-c',ROOT/'wine/ps5/pw_wine_process.c','-o',obj])
 dmem_obj=prx/'pw_wine_dmem_ps5.o'
 run([sdk/'bin/prospero-clang','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fPIC','-c',ROOT/'wine/ps5/pw_wine_dmem_ps5.c','-o',dmem_obj])
 exports=re.findall(r'extern char ([a-zA-Z0-9_]+)\[\];',(base/'prx/obj/ntdll_desc.c').read_text())
 for name in ('__wine_ps5_set_payload_memory','__wine_ps5_set_payload_low_image','__wine_ps5_set_payload_module','__wine_ps5_set_payload_sigaction','__wine_ps5_exec_stats','__wine_ps5_code_share_stats','__wine_ps5_code_share_mapped_kib','__wine_ps5_foreign_stats','__wine_ps5_exec_convert','__wine_ps5_exec_convert_image','__wine_ps5_host_allows','__wine_ps5_exec_refused'):
  if name not in exports:exports.append(name)
 dl_obj=prx/'pw_wine_dl_libc.o'
 run([sdk/'bin/prospero-clang','-std=gnu11','-O2','-Wall','-Wextra','-Werror','-fPIC','-c',ROOT/'wine/ps5/pw_wine_dl_libc.c','-o',dl_obj])
 desc=prx/'ntdll_desc.c';desc_obj=prx/'ntdll_desc.o'
 run(['python3',ROOT/'tools/gen_prx_descriptor.py',desc,*exports])
 run([sdk/'bin/prospero-clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-fPIC','-I'+str(ROOT/'wine/ps5'),'-c',desc,'-o',desc_obj])
 text=(base/'make.log').read_text().replace(chr(92)+chr(10),' ')
 lines=[l for l in text.splitlines() if '-o dlls/ntdll/ntdll.so ' in l]
 objects=[base/'build'/v for v in shlex.split(lines[-1]) if v.endswith('.o') and not v.startswith('/')]
 objects += [dmem_obj if n=='pw_wine_dmem_ps5' else base/'heap'/f'{n}.o' for n in ('pw_wine_heap','pw_wine_heap_libc','pw_wine_dmem','pw_wine_dmem_ps5','pw_wine_section','pw_wine_section_ps5','pw_wine_alias','pw_wine_codeshare','pw_wine_mmu4k','pw_wine_mmu4k_slot','pw_wine_mmu4k_ps5','pw_ia32_isa','pw_ia32_decode')]
 names=('pw_ntsync','pw_wine_ntsync','pw_wine_process','pw_wine_process_io','pw_wine_threads','pw_wine_prx','pw_wine_dl','pw_wine_dl_libc','pw_wine_compat','pw_wine_compat_libc','pw_wine_sink','pw_audio_ring','pw_memory_helper','pw_memory_service','pw_wine_perf','pw_wine_cwd','pw_wine_cwd_libc','pw_wine_fdv','pw_wine_fdv_libc','ntdll_desc')
 objects += [obj if n=='pw_wine_process' else dl_obj if n=='pw_wine_dl_libc' else desc_obj if n=='ntdll_desc' else base/'prx/obj'/f'{n}.o' for n in names]
 wraps=sorted(set(re.findall('__wrap_([a-z_0-9]+)',(ROOT/'wine/ps5/pw_wine_cwd_libc.c').read_text()+(ROOT/'wine/ps5/pw_wine_fdv_libc.c').read_text())))
 stubs=sorted((base/'title-stubs').glob('*.so'))
 command=[sdk/'bin/prospero-lld','--shared','-Bsymbolic','-T',pie,'-T',ROOT/'wine/ps5/prx_eh_frame.ld','--eh-frame-hdr','-soname','ntdll.prx','-z','defs','--warn-unresolved-symbols','-L'+str(base/'ps5lib'),'-o',prx/'ntdll.shared.elf',*objects,*['--wrap='+n for n in wraps],sdk/'target/lib/libps5platform.a',sdk/'target/lib/libunwind.a','--as-needed',*stubs]
 log=run(command,stderr=subprocess.STDOUT)
 if 'undefined symbol:' in log:raise RuntimeError(log)
 log+=run([tool,'link','--module','--in',prx/'ntdll.shared.elf','--out',prx/'ntdll.elf','--stub-dir',base/'title-stubs','--module-sdk','0x02000009','--companion-sdk','0x08050001','--file-name','ntdll.prx'],stderr=subprocess.STDOUT)
 log+=run([tool,'self','--sign','--in',prx/'ntdll.elf','--out',prx/'sce_module/ntdll.prx'],stderr=subprocess.STDOUT)
 (prx/'ntdll.link.log').write_text(log)
 # Reuse the canonical import/syscall audit against the freshly linked module.
 recipe=(ROOT/'tools/build-wine-ps5.sh').read_text();audit=recipe.split("import hashlib, json, re, shutil, subprocess, sys\n",1)[1].split("\nPY",1)[0]
 # Audit needs the untouched base modules' converter logs beside their copies.
 for name in ('wineserver','win32u'):shutil.copyfile(base/'prx'/f'{name}.link.log',prx/f'{name}.link.log')
 audit="import hashlib, json, re, shutil, subprocess, sys\n"+audit
 run(['python3','-c',audit,base/'build',base/'make.log',out/'report.json',sdk,report['wine_commit'],prx,'0',*report['patches']])
 measured=json.loads((out/'report.json').read_text());entry=measured['prx']['modules']['ntdll']
 if any(entry.get(k) for k in ('unresolved','errors','raw_syscalls','title_unbound')):raise RuntimeError('New runtime import audit failed: '+str(entry))
 inputs=[*objects,*stubs,pie,tool,sdk/'target/lib/libps5platform.a',sdk/'target/lib/libunwind.a',base/'report.json',base/'make.log',ROOT/'tools/build-wine-ps5.sh',ROOT/'wine/ps5/pw_wine_process.c',ROOT/'wine/ps5/pw_payload_spawn.h',ROOT/'wine/ps5/pw_payload_rendezvous.h',ROOT/'wine/ps5/pw_wine_dmem_ps5.c',ROOT/'wine/ps5/pw_wine_dl_libc.c',ROOT/'tools/gen_prx_descriptor.py',base/'prx/obj/ntdll_desc.c',Path(__file__)]
 manifest={'base_report_sha256':digest(base/'report.json'),'sdk_revision':(sdk/'.ps5-sdk-revision').read_text().strip(),'inputs':{str(n):digest(n) for n in inputs},'ntdll_sha256':entry['sha256']}
 (out/'payload-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');print(out)
if __name__=='__main__':main()
