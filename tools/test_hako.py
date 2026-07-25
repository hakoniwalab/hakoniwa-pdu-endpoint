import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path

MODULE_PATH = Path(__file__).with_name("hako.py")
spec = importlib.util.spec_from_file_location("hako_build_tool", MODULE_PATH)
assert spec and spec.loader
hako = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = hako
spec.loader.exec_module(hako)


class ConfigTests(unittest.TestCase):
    def parse(self, text: str):
        with tempfile.TemporaryDirectory() as temp_dir:
            path = Path(temp_dir) / "build.yaml"
            path.write_text(text, encoding="utf-8")
            return hako.resolve_config(hako.load_simple_yaml(path))

    def test_default_python_implies_shared(self):
        cfg = self.parse("version: 1\n")
        self.assertTrue(cfg["bindings"]["python"])
        self.assertTrue(cfg["build"]["shared_resolved"])
        self.assertFalse(cfg["features"]["hakoniwa_core"])

    def test_default_keeps_optional_targets_off(self):
        cfg = self.parse("version: 1\n")
        self.assertFalse(cfg["validation"]["tests"])
        self.assertFalse(cfg["validation"]["examples"])
        self.assertFalse(cfg["validation"]["tools"])
        self.assertFalse(cfg["validation"]["benchmarks"])
        self.assertTrue(cfg["validation"]["python_import"])

    def test_cpp_only_can_be_static(self):
        cfg = self.parse("version: 1\nbindings:\n  python: false\n")
        self.assertFalse(cfg["build"]["shared_resolved"])
        self.assertFalse(cfg["validation"]["python_import"])

    def test_python_rejects_forced_static(self):
        with self.assertRaises(hako.ConfigError):
            self.parse("version: 1\nbuild:\n  shared: false\nbindings:\n  python: true\n")

    def test_features_are_independent(self):
        cfg = self.parse(
            "version: 1\nfeatures:\n  hakoniwa_core: false\n  zenoh: true\n  mqtt: false\n"
        )
        self.assertTrue(cfg["features"]["zenoh"])
        self.assertFalse(cfg["features"]["hakoniwa_core"])

    def test_unknown_key_is_rejected(self):
        with self.assertRaises(hako.ConfigError):
            self.parse("version: 1\nfeatures:\n  zenoooh: true\n")

    def test_comments_and_quoted_hash_are_supported(self):
        cfg = self.parse(
            'version: 1 # comment\nbuild:\n  dir: "build#local" # another comment\n'
        )
        self.assertEqual(cfg["build"]["dir"], "build#local")


if __name__ == "__main__":
    unittest.main()
