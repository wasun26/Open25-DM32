#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Build the shared C core and its target compiler stack/call-graph evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--cc', default='cc')
    p.add_argument('--ar', default='ar')
    p.add_argument('--target', action='store_true')
    p.add_argument('--sanitize', action='store_true')
    a = p.parse_args()
    if a.target and a.sanitize:
        p.error('Sanitizers require a host build')
    out = a.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    options = json.loads((ROOT/'build-options.json').read_text())
    flags = list(options['common_flags'])
    if a.target:
        flags += options['target_flags'] + ['-fcallgraph-info=su', '-fstack-usage']
    elif a.sanitize:
        flags += ['-g', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-omit-frame-pointer']
    inputs = [ROOT/'build-options.json', Path(__file__), *sorted((ROOT/'src').iterdir()),
              ROOT/'compat/open25/dm32_imbe.h']
    before = {str(path.relative_to(ROOT)): sha(path) for path in inputs if path.is_file()}
    commands = []

    def run(command):
        command = list(map(str, command))
        commands.append(command)
        subprocess.run(command, check=True)

    objects = []
    graphs = []
    for stem in options['sources']:
        source = ROOT/'src'/(stem+'.c')
        obj = out/(stem+'.o')
        run([a.cc, *flags, '-c', source, '-o', obj])
        objects.append(obj)
        if a.target:
            graph = obj.with_suffix('.ci')
            if not graph.is_file() or ' bytes (' not in graph.read_text():
                raise SystemExit('Missing target compiler stack evidence: '+str(graph))
            graphs.append(graph)
    archive = out/'libdm32_imbe.a'
    # Re-create the archive so retired objects cannot survive a source change.
    archive.unlink(missing_ok=True)
    # GNU target ar defaults to real timestamps and uid/gid on this toolchain.
    # Zero that metadata so identical source builds have identical archives.
    run([a.ar, 'rcsD' if a.target else 'rcs', archive, *objects])
    if a.target:
        (out/'c-codec.ci').write_text(''.join(path.read_text()+'\n' for path in graphs))
        (out/'c-codec.json').write_text(json.dumps({
            'format': 'GCC -fcallgraph-info=su',
            'objects': {path.name: sha(path) for path in objects},
            'graphs': {path.name: sha(path) for path in graphs},
            'merged_sha256': sha(out/'c-codec.ci')}, indent=2)+'\n')
    assert before == {name: sha(ROOT/name) for name in before}, 'Source changed while building'
    (out/'codec-build.json').write_text(json.dumps({
        'language': 'C11 fixed point', 'target': 'CK803S' if a.target else 'native host',
        'source_sha256': before, 'commands': commands,
        'compiler_version': subprocess.check_output([a.cc, '--version'], text=True).splitlines()[0],
        'archive_sha256': sha(archive), 'sanitizers': bool(a.sanitize)}, indent=2)+'\n')
    print(archive)


if __name__ == '__main__':
    main()
