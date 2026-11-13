#!/usr/bin/env python3
"""Run the existing Amiga development workspace from Termic."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / '.context/amiga-workspace.json'
TARGETS = {
    'doctor': 'amiga-doctor',
    'build': 'amiga-build',
    'test': 'test-amiga',
    'simulator': 'amiga-sim',
    'hardware': 'amiga-hardware',
    'emulator': 'amiga-emulator',
}


def validate_workspace(value):
    path = Path(value).expanduser().resolve()
    for name in ('Makefile', 'scripts/amiga.py', 'amiga/target.json',
                 'docs/amiga-session-memory.md'):
        if not (path / name).is_file():
            raise ValueError(f'Missing {name} in Amiga workspace: {path}')
    return path


def resolve_workspace():
    value = os.environ.get('TERMIC_AMIGA_WORKSPACE')
    if not value:
        if not CONFIG.exists():
            raise ValueError('Configure first: python3 scripts/amiga/workspace.py '
                             'configure /path/to/amiga-workspace')
        value = json.loads(CONFIG.read_text())['workspace']
    return validate_workspace(value)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action', choices=['configure', 'demo-build', *TARGETS])
    parser.add_argument('workspace', nargs='?')
    args = parser.parse_args(argv)
    try:
        if args.action == 'configure':
            if not args.workspace:
                parser.error('configure requires the existing Amiga workspace path')
            path = validate_workspace(args.workspace)
            CONFIG.parent.mkdir(parents=True, exist_ok=True)
            CONFIG.write_text(json.dumps({'workspace': str(path)}, indent=2) + '\n')
            print(f'Amiga workspace configured: {path}')
            return 0
        if args.workspace:
            parser.error('workspace path is only accepted with configure')
        path = resolve_workspace()
        print(f'Amiga {args.action}: {path}', flush=True)
        if args.action == 'demo-build':
            settings = json.loads((path / '.context/amiga/settings.json').read_text())
            pin = json.loads((path / 'amiga/upstream.json').read_text())
            command = settings['container_command']
            if not isinstance(command, list) or not command or not all(
                    isinstance(part, str) and part for part in command):
                raise ValueError('container_command must be a nonempty array of strings')
            result = subprocess.run(command + [
                'run', '--rm', '--platform', 'linux/amd64',
                '-v', f'{ROOT}:/work', '-w', '/work/amiga/workbench-demo',
                pin['image'], 'make'], check=False)
            if result.returncode:
                return result.returncode
            binary = ROOT / '.context/amiga-demo/TermicDemo'
            if binary.read_bytes()[:4] != b'\x00\x00\x03\xf3':
                raise ValueError('Build did not produce an Amiga Hunk executable')
            print(f'Workbench application: {binary}')
            return 0
        return subprocess.run(['make', '-C', str(path), TARGETS[args.action]],
                              check=False).returncode
    except (OSError, ValueError, KeyError) as error:
        print(f'Amiga environment: {error}', file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
