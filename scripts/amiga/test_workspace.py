import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('workspace', Path(__file__).with_name('workspace.py'))
workspace = importlib.util.module_from_spec(spec)
spec.loader.exec_module(workspace)


class WorkspaceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name).resolve() / 'Amiga workspace'
        for name in ('Makefile', 'scripts/amiga.py', 'amiga/target.json',
                     'docs/amiga-session-memory.md'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.touch()
        self.config = Path(self.temp.name) / 'local/config.json'

    def test_configure_roundtrip_and_build_exit_code(self):
        with patch.object(workspace, 'CONFIG', self.config), patch.dict('os.environ', {}, clear=True):
            self.assertEqual(workspace.main(['configure', str(self.root)]), 0)
            self.assertEqual(workspace.resolve_workspace(), self.root)
            with patch.object(workspace.subprocess, 'run') as run:
                run.return_value.returncode = 2
                self.assertEqual(workspace.main(['build']), 2)
                run.assert_called_once_with(['make', '-C', str(self.root), 'amiga-build'], check=False)

    def test_invalid_workspace_does_not_replace_config(self):
        self.config.parent.mkdir()
        self.config.write_text('original')
        with patch.object(workspace, 'CONFIG', self.config):
            self.assertEqual(workspace.main(['configure', str(self.root / 'missing')]), 1)
        self.assertEqual(self.config.read_text(), 'original')

    def test_env_override_and_missing_config(self):
        with patch.object(workspace, 'CONFIG', self.config), patch.dict('os.environ', {}, clear=True):
            with self.assertRaisesRegex(ValueError, 'Configure first'):
                workspace.resolve_workspace()
            with patch.dict('os.environ', {'TERMIC_AMIGA_WORKSPACE': str(self.root)}):
                self.assertEqual(workspace.resolve_workspace(), self.root)

    def test_corrupt_config_fails_without_running_make(self):
        self.config.parent.mkdir()
        self.config.write_text('{')
        with patch.object(workspace, 'CONFIG', self.config), patch.dict('os.environ', {}, clear=True), patch.object(workspace.subprocess, 'run') as run:
            self.assertEqual(workspace.main(['build']), 1)
            run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
