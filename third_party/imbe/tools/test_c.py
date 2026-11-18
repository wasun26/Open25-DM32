#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Run standalone C mathematical, parameter and state/sanitizer checks."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--sanitize', action='store_true')
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    names = ['codec', 'encoder', 'decoder', 'quant', 'fft', 'math']
    sources = {name: ROOT/'src'/(name+'.c') for name in names}
    tests = {
        'math': ['math', 'fft'],
        'quant': ['quant', 'math', 'fft'],
        'decoder': ['decoder', 'quant', 'math', 'fft'],
        'codec': names,
    }
    inputs = [*sources.values(), *(ROOT/'src').glob('*.h'),
              ROOT/'src/quant_tables.inc', ROOT/'compat/open25/dm32_imbe.h',
              *(ROOT/'tests').glob('test_*.c')]
    digest = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
    before = {str(p.relative_to(ROOT)): digest(p) for p in inputs}
    flags = ['-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
             '-I'+str(ROOT/'src'), '-I'+str(ROOT/'compat/open25')]
    if args.sanitize:
        flags += ['-fsanitize=address,undefined', '-fno-sanitize-recover=all',
                  '-fno-omit-frame-pointer']
    commands = []
    for name, dependencies in tests.items():
        executable = out/('test_'+name)
        command = [args.cc, *flags, ROOT/'tests'/('test_'+name+'.c'),
                   *(sources[key] for key in dependencies), '-lm', '-o', executable]
        commands.append(list(map(str, command)))
        subprocess.run(command, check=True)
        subprocess.run([executable], check=True)
    assert before == {str(p.relative_to(ROOT)): digest(p) for p in inputs}, 'Source changed during tests'
    (out/'test-results.json').write_text(json.dumps({
        'status': 'passed', 'sanitizers': args.sanitize,
        'source_sha256': before, 'commands': commands,
        'external_audio_or_codec_dependencies': [],
    }, indent=2)+'\n')


if __name__ == '__main__':
    main()
