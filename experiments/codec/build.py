#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the pinned MIT full-rate codec through the existing Open25 C ABI."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
LIB = ROOT/'third_party/imbe'
PINS = {'mbelib': ('https://github.com/szechyjs/mbelib.git', '9a04ed5c78176a9965f3d43f7aa1b1f5330e771f')}

def run(args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def prepare():
    run([sys.executable, ROOT/'tools/sources.py', 'verify-vendored'])
    return ROOT/'research/codec'

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--target', action='store_true')
    p.add_argument('--speed', action='store_true', help='retained firmware build preset; shared optimized C core is always used')
    p.add_argument('--sanitize', action='store_true')
    p.add_argument('--full-ubsan', action='store_true', help='ASan/UBSan on the C core and C/C++ harnesses/reference')
    p.add_argument('--out', type=Path)
    p.add_argument('--compiler', default=os.environ.get('CXX','clang++'))
    p.add_argument('--c-compiler', default=os.environ.get('CC','clang'))
    a = p.parse_args()
    if a.target and (a.sanitize or a.full_ubsan): p.error('Sanitizers require a host build')
    deps = prepare()
    out = (a.out or HERE/'out'/('ck803s-c' if a.target else 'ubsan' if a.full_ubsan else 'sanitize' if a.sanitize else 'release')).resolve()
    out.mkdir(parents=True,exist_ok=True)
    archive = out/'libdm32_imbe.a'
    options = json.loads((LIB/'build-options.json').read_text())
    if a.target:
        prefix=str(ROOT/'toolchain/bin/csky-elf-')
        run([sys.executable,LIB/'tools/build.py','--target','--cc',prefix+'gcc','--ar',prefix+'ar','--out',out])
        probe=out/'stack-probe.c'
        probe.write_text('#include "dm32_imbe.h"\nstatic dm32_imbe_context c; static int16_t p[160]; static uint16_t u[8]; static uint8_t b[11]; static volatile size_t state_size;\n'
                         'void dm32_codec_stack_probe(void) { state_size = dm32_imbe_state_bytes(); dm32_imbe_init(&c); dm32_imbe_encode(&c,p,u); dm32_imbe_decode(&c,u,p); dm32_imbe_pack(u,b); dm32_imbe_unpack(b,u); }\n')
        flags=[*options['common_flags'],*options['target_flags']]
        run([prefix+'gcc',*flags,'-I',ROOT/'src/codec','-c',probe,'-o',out/'stack-probe.o'])
        run([prefix+'gcc',*flags,'-O2','-fno-tree-loop-distribute-patterns','-c',ROOT/'src/runtime/memory.c','-o',out/'memory.o'])
        ld=out/'stack-probe.ld';ld.write_text('ENTRY(dm32_codec_stack_probe)\nMEMORY { ROM (rx) : ORIGIN = 0x08010000, LENGTH = 880K\nRAM (rwx) : ORIGIN = 0x20000000, LENGTH = 206K }\nSECTIONS { .text : { *(.text*) *(.rodata*) } > ROM\n.ramfunc : { *(.ramfunc*) } > RAM AT> ROM\n.data : { *(.data*) *(.ramdata*) } > RAM AT> ROM\n.bss (NOLOAD) : { *(.bss*) *(COMMON) } > RAM\n/DISCARD/ : { *(.eh_frame*) *(.comment) } }\n')
        run([prefix+'gcc',*flags,'-nostdlib','-Wl,--no-warn-rwx-segments','-Wl,--gc-sections','-Wl,-T,'+str(ld),out/'stack-probe.o',out/'memory.o',archive,'-lgcc','-o',out/'stack-probe.elf'])
    else:
        command=[sys.executable,LIB/'tools/build.py','--cc',a.c_compiler,'--out',out]
        if a.sanitize or a.full_ubsan: command+=['--sanitize']
        run(command)
        for name,(url,pin) in PINS.items():
            path=deps/name
            if not path.exists():
                run(['git','clone','--no-checkout',url,path]);run(['git','-C',path,'checkout','--detach',pin])
            actual=subprocess.check_output(['git','-C',str(path),'rev-parse','HEAD'],text=True).strip()
            if actual!=pin or subprocess.check_output(['git','-C',str(path),'status','--porcelain','--untracked-files=no']):
                raise SystemExit('Preserving unexpected reference source: '+str(path))
        reference=out/'reference-source';reference.mkdir(exist_ok=True)
        for path in (deps/'mbelib').iterdir():
            if path.suffix in ('.c','.h'):shutil.copyfile(path,reference/path.name)
        path=reference/'imbe7200x4400.c';text=path.read_text();old='prev_mp->log2Ml[intkl[l] + 1]'
        if text.count(old)!=2:raise SystemExit('mbelib endpoint patch mismatch')
        path.write_text(text.replace(old,'prev_mp->log2Ml[intkl[l] < prev_mp->L ? intkl[l] + 1 : prev_mp->L]'))
        common=['-O2','-g','-Wall','-Wextra','-Werror','-I',ROOT/'src/codec']
        if a.sanitize or a.full_ubsan:common+=['-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-omit-frame-pointer']
        refs=[]
        for stem in ['mbelib','ecc','imbe7200x4400']:
            obj=out/('reference_'+stem+'.o')
            run([a.c_compiler,*common,'-Wno-missing-braces','-Wno-deprecated-declarations','-Wno-unused-variable','-Wno-unused-parameter','-std=c99','-D_DEFAULT_SOURCE','-c',reference/(stem+'.c'),'-o',obj]);refs.append(obj)
        run([a.compiler,*common,'-std=c++11','-I',deps/'mbelib',HERE/'harness.cc',archive,*refs,'-o',out/'codec_harness'])
        objects=[]
        for path in [ROOT/'src/p25/p25.c',*sorted((ROOT/'third_party/p25cai/src').glob('*.c'))]:
            obj=out/('cai-'+path.stem+'.o');objects.append(obj)
            run([a.c_compiler,*common,'-std=c99','-I',ROOT/'src/p25','-I',ROOT/'third_party/p25cai/include','-c',path,'-o',obj])
        run([a.compiler,*common,'-std=c++11','-I',deps/'mbelib','-I',ROOT/'src/p25','-I',ROOT/'third_party/p25cai/include',HERE/'pipeline.cc',archive,*refs,*objects,'-o',out/'codec_pipeline'])
    record={'kind':'CK803S codec archive' if a.target else 'Host-only codec/reference tests',
            'runtime_codec':'MIT full-rate fixed-point C codec, pinned shared snapshot',
            'archive_sha256':sha(archive),'flags':options,
            'C_CXX_sanitizers_enabled':bool(a.sanitize or a.full_ubsan),
            'physical_timing_or_RF_tested':False,'stock_codec_included':False}
    (out/'build.json').write_text(json.dumps(record,indent=2)+'\n')
    print(archive)

if __name__=='__main__':main()
