#!/usr/bin/env python3
"""Opt-in native hardware acceptance over an already-running DevBench MCP.

Uses the installed sample project. Builds it, launches its window, tests
Increment/Reset/Quit, and verifies native client launch/quit. Never calls a
model, opens a credential file, injects mouse input, or takes a full-screen
capture. Run only while nobody is using the Amiga keyboard.
"""
import argparse
import asyncio
import json
from pathlib import Path
import subprocess
import urllib.parse
import urllib.request

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client


async def accept(args):
    args.output.mkdir(parents=True, exist_ok=True)
    evidence = []
    async with streamablehttp_client(args.server + '/mcp') as (read, write, _):
        async with ClientSession(read, write) as session:
            await session.initialize()

            async def call(name, arguments):
                result = await session.call_tool(name, arguments)
                text = '\n'.join(getattr(item, 'text', '') for item in result.content)
                if result.isError or text.startswith(('ERROR:', 'Error ')):
                    raise RuntimeError(text)
                evidence.append({'tool': name, 'result': text})
                return text

            async def key(code, expected):
                windows = await call('amiga_list_screen_windows', {})
                first = windows.split('\n\n', 1)[-1].splitlines()[0].strip()
                if not first.startswith(expected + '  pos='):
                    raise RuntimeError('Expected test window is not frontmost; no key injected')
                await call('amiga_input_key', {'rawkey': code, 'direction': 'down'})
                await call('amiga_input_key', {'rawkey': code, 'direction': 'up'})
                await asyncio.sleep(0.3)

            async def capture(title, filename):
                url = args.server + '/api/screenshot?' + urllib.parse.urlencode({'window': title})
                with urllib.request.urlopen(url, timeout=30) as response:
                    data = json.load(response)
                # This suite runs on the same host as the DevBench server.
                source = Path(data['path'])
                if not source.is_file():
                    raise RuntimeError('Screenshot path is not local to this test runner')
                destination = args.output / filename
                subprocess.run(['sips', '-s', 'format', 'png', str(source),
                                '--out', str(destination)], check=True, capture_output=True)
                return destination

            windows = await call('amiga_list_screen_windows', {})
            if 'Termic Native Amiga  pos=' in windows:
                await key('0x10', 'Termic Native Amiga')
            output = await call('amiga_run_script', {
                'script': 'FailAt 10\nStack 262144\nTermic:TermicAgent --probe', 'timeout': 60})
            if 'Verified HTTPS probe: HTTP 401' not in output:
                raise RuntimeError('Verified HTTPS probe did not pass')
            output = await call('amiga_run_script', {
                'script': 'FailAt 10\nStack 262144\nTermic:TermicAgent --build', 'timeout': 60})
            if 'Native Amiga build succeeded.' not in output:
                raise RuntimeError('Build did not report success; wait for its completion and rerun')
            await call('amiga_run_script', {
                'script': 'Run >NIL: <NIL: Termic:Project/Program', 'timeout': 15})
            await asyncio.sleep(1)
            await capture('Termic Project', 'project-initial.png')
            await key('0x17', 'Termic Project')
            await key('0x17', 'Termic Project')
            await capture('Termic Project', 'project-incremented.png')
            await key('0x13', 'Termic Project')
            await capture('Termic Project', 'project-reset.png')
            await key('0x10', 'Termic Project')
            windows = await call('amiga_list_screen_windows', {})
            if 'Termic Project  pos=' in windows:
                raise RuntimeError('Project Quit did not close its window')
            await call('amiga_run_script', {
                'script': 'Termic:TermicNative --icon Termic:TermicNative\n'
                          'Run >NIL: <NIL: WBLoad Termic:TermicNative', 'timeout': 15})
            await asyncio.sleep(1)
            await capture('Termic Native Amiga', 'native-client.png')
            await key('0x10', 'Termic Native Amiga')
            windows = await call('amiga_list_screen_windows', {})
            if 'Termic Native Amiga  pos=' in windows:
                raise RuntimeError('Native client Quit did not close its window')
            await call('amiga_run_script', {
                'script': 'Run >NIL: <NIL: WBLoad Termic:TermicNative', 'timeout': 15})
            await asyncio.sleep(1)
            # Delete only the binary just built by this suite, then prove that
            # the Workbench button creates it again through an async worker.
            await call('amiga_run_script', {
                'script': 'Delete Termic:Project/Program', 'timeout': 15})
            await key('0x35', 'Termic Native Amiga')  # B
            for _ in range(20):
                await asyncio.sleep(3)
                ready = await call('amiga_run_script', {
                    'script': 'If EXISTS Termic:Project/Program\n'
                              'If NOT EXISTS Termic:Project/.busy\n'
                              'Echo UI_BUILD_READY\nEndIf\nEndIf', 'timeout': 10})
                if 'UI_BUILD_READY' in ready:
                    break
            else:
                raise RuntimeError('Workbench Build did not finish')
            await capture('Termic Native', 'native-build-console.png')
    (args.output / 'acceptance.json').write_text(json.dumps(evidence, indent=2) + '\n')
    print('Native build, project keyboard flow, Workbench launch/quit and Build action passed.')
    print('Inspect screenshots; this does not test a cloud model or mouse interaction.')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', default='http://127.0.0.1:55010')
    parser.add_argument('--output', type=Path, default=Path('.context/amiga-native'))
    args = parser.parse_args()
    asyncio.run(accept(args))

if __name__ == '__main__':
    main()
