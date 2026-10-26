"""Regression coverage for IDF release workflows and the Ubuntu gateway."""
import importlib.util
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

GATEWAY = Path(__file__).resolve().parents[1] / 'embedded.py'

class EmbeddedCI(unittest.TestCase):
    def load_gateway(self):
        spec = importlib.util.spec_from_file_location('embedded_gateway_ci', GATEWAY)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_ci_keeps_activated_python(self):
        with patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            module = self.load_gateway()
            self.assertEqual(module.LINUX_PY, sys.executable)

    def test_ci_keeps_checkout_build_and_idf_flags(self):
        module = self.load_gateway()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            sdk = root / 'sdk'
            (sdk / 'tools').mkdir(parents=True)
            (sdk / 'tools/idf.py').touch()
            with patch.object(module, 'ROOT', root), patch.dict(os.environ, {'GITHUB_ACTIONS': 'true', 'IDF_PATH': str(sdk)}), patch.object(module, 'run', return_value=0) as run, patch('builtins.print') as emit:
                module.idf(['where'], module.CONFIG['default'])
                emit.assert_called_once_with(root / 'build')
                module.idf(['-D', 'RADIO_UI_TEST=OFF', 'build'], module.CONFIG['default'])
                self.assertEqual(list(map(str, run.call_args.args[0])), [sys.executable, str(sdk / 'tools/idf.py'), '-B', str(root / 'build'), '-D', 'RADIO_UI_TEST=OFF', 'build'])

if __name__ == '__main__':
    unittest.main()
