#!/usr/bin/env python3
"""Portable protocol tests. Hardware acceptance is a separate explicit command."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'amiga/native'

class NativeProtocolTests(unittest.TestCase):
    def test_http_and_agent_loop(self):
        with tempfile.TemporaryDirectory() as directory:
            work = Path(directory)
            flags = ['cc', '-std=c99', '-Wall', '-Wextra', '-Werror',
                     '-Wno-deprecated-declarations',
                     '-fsanitize=address,undefined', '-g', '-I', str(SOURCE)]
            subprocess.run(flags + [str(SOURCE / 'http.c'),
                str(SOURCE / 'tests/test_http.c'), '-o', str(work / 'http')], check=True)
            subprocess.run([str(work / 'http')], check=True)
            subprocess.run(flags + ['-DTERMIC_PROJECT="Project/"',
                '-DTERMIC_CONFIG="Config/"'] + [str(SOURCE / f) for f in
                ['agent.c', 'files.c', 'vendor/cJSON.c', 'tests/agent_harness.c']] +
                ['-o', str(work / 'agent')], check=True)
            (work / 'Project').mkdir()
            (work / 'Config').mkdir()
            (work / 'Config/model').write_text('fixture-model\n')
            (work / 'Project/task.txt').write_text('Compile a synthetic fixture.\n')
            (work / 'Project/build.log').write_text('Fixture compiler succeeded.\n')
            for mode in range(7):
                with self.subTest(mode=mode):
                    (work / 'Config/api-key').write_text('' if mode == 6 else 'test-key\n')
                    original = '/* fixture source */\n'
                    (work / 'Project/main.c').write_text(original)
                    subprocess.run([str(work / 'agent'), str(mode)], cwd=work, check=True)
                    if mode == 0:
                        self.assertEqual((work / 'Project/main.c.bak').read_text(), original)
                        self.assertIn('return 0', (work / 'Project/main.c').read_text())
                    else:
                        self.assertEqual((work / 'Project/main.c').read_text(), original)
                    self.assertNotIn('test-key', (work / 'Project/transcript.txt').read_text())

if __name__ == '__main__':
    unittest.main()
