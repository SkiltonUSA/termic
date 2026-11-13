#!/usr/bin/env python3
"""Create a local LHA installation kit from verified SDKs and Hunk binaries.

Never packages a live installation, personal project, credentials or logs.
Use original upstream SDK archives; their licenses remain inside each archive.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'amiga/native'
SCRIPTS = ['Setup', 'BuildNative', 'BuildProject', 'InstallSDK', 'StartAgent',
           'StartBuild', 'StartRun', 'TestHTTPS', 'ReadMe']


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--downloads', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--lha', default='lha')
    parser.add_argument('--download', action='store_true', help='Fetch missing SDK archives')
    args = parser.parse_args()
    manifest = json.loads((SOURCE / 'sdk-lock.json').read_text())
    args.downloads.mkdir(parents=True, exist_ok=True)
    for name, entry in manifest.items():
        path = args.downloads / name
        if not path.exists() and args.download:
            with urllib.request.urlopen(entry['url'], timeout=60) as response:
                path.write_bytes(response.read())
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry['sha256']:
            raise ValueError(f'SDK checksum mismatch: {name}')
    output = args.output.resolve()
    if output.exists():
        raise ValueError('Choose a new output path; existing archives are not modified')
    output.parent.mkdir(parents=True, exist_ok=True)
    lha = shutil.which(args.lha)
    if not lha:
        raise ValueError('Supply an LHA executable with --lha')
    lha = str(Path(lha).resolve())
    with tempfile.TemporaryDirectory() as directory:
        stage = Path(directory) / 'TermicNative'
        stage.mkdir()
        for name in ('TermicNative', 'TermicAgent'):
            binary = args.binaries / name
            if binary.read_bytes()[:4] != b'\x00\x00\x03\xf3':
                raise ValueError(f'Not an Amiga Hunk executable: {name}')
            shutil.copyfile(binary, stage / name)
        icon = args.binaries / 'TermicNative.info'
        if icon.exists():
            shutil.copyfile(icon, stage / icon.name)
        for script in SCRIPTS:
            shutil.copyfile(SOURCE / script, stage / script)
        (stage / 'Source').mkdir()
        for path in sorted(SOURCE.glob('*')):
            if path.suffix in ('.c', '.h'):
                shutil.copyfile(path, stage / 'Source' / path.name)
        shutil.copytree(SOURCE / 'vendor', stage / 'Source/vendor')
        shutil.copytree(SOURCE / 'Project', stage / 'Project')
        (stage / 'Config').mkdir()
        (stage / 'Config/api-key').write_text('')
        (stage / 'Config/model').write_text('gpt-5-mini\n')
        (stage / 'SDK').mkdir()
        for name in manifest:
            shutil.copyfile(args.downloads / name, stage / 'SDK' / name)
        shutil.copyfile(SOURCE / 'sdk-lock.json', stage / 'SDK/sdk-lock.json')
        shutil.copyfile(ROOT / 'LICENSE', stage / 'LICENSE')
        # Amiga lha preserves executable attributes from POSIX permissions.
        for path in stage.iterdir():
            if path.is_file():
                path.chmod(0o755 if path.name in ('TermicNative', 'TermicAgent') else 0o644)
        subprocess.run([lha, 'aq', str(output), 'TermicNative'], cwd=directory, check=True)
        subprocess.run([lha, 'tq', str(output)], check=True, capture_output=True)
    print(f'{output}\nSHA256 {hashlib.sha256(output.read_bytes()).hexdigest()}')


if __name__ == '__main__':
    main()
