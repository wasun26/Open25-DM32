#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Content-checked cache for the pinned MIT target codec build."""
import hashlib
import importlib.util
import json
from pathlib import Path
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def key(root,speed):
    path=root/'experiments/codec/build.py'
    spec=importlib.util.spec_from_file_location('codec_cache_source',path)
    m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);m.prepare()
    files=[path,Path(__file__),root/'sources.lock.json',root/'src/runtime/memory.c',root/'toolchain/bin/csky-elf-gcc',root/'toolchain/bin/csky-elf-ar']
    for folder in (root/'src/codec',root/'third_party/imbe'):
        files += [p for p in folder.rglob('*') if p.is_file() and '__pycache__' not in p.parts]
    record={'speed':bool(speed),'inputs':{str(p.relative_to(root)):sha(p) for p in sorted(set(files))},
            'format':'fixedpoint-C-v1',
            'gcc_version':m.subprocess.check_output([str(root/'toolchain/bin/csky-elf-gcc'),'--version'],text=True)}
    return hashlib.sha256(json.dumps(record,sort_keys=True).encode()).hexdigest()
def reusable(folder,want):
    p=folder/'checked-cache.json'
    if not p.exists():return False
    try:r=json.loads(p.read_text());return r['key']==want and bool(r['outputs']) and all((folder/name).is_file() and sha(folder/name)==digest for name,digest in r['outputs'].items())
    except (KeyError,ValueError,OSError):return False
def remember(folder,want):
    files=[folder/name for name in ('libdm32_imbe.a','c-codec.ci','c-codec.json','codec-build.json','stack-probe.o','memory.o','stack-probe.elf','build.json')]
    r={'key':want,'outputs':{p.name:sha(p) for p in sorted(files)}}
    (folder/'checked-cache.json').write_text(json.dumps(r,indent=2)+'\n')
