import importlib.util
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

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

    def test_aarch64_normalizes_to_arm64(self):
        with patch.object(hako.sys, "platform", "linux"), patch.object(
            hako.platform, "machine", return_value="aarch64"
        ):
            self.assertEqual(hako._host_platform(), ("linux", "arm64"))


class DoctorTests(unittest.TestCase):
    def make_context(self, *, zenoh: bool):
        cfg = hako.resolve_config(
            {
                "version": 1,
                "bindings": {"python": False},
                "features": {"zenoh": zenoh},
            }
        )
        return hako.BuildContext(
            repo_root=Path("."),
            manifest_path=Path("hakoniwa-build.yaml"),
            cfg=cfg,
            platform_name="linux",
            arch="arm64",
            build_dir=Path("build"),
            vcpkg_root=None,
            core_root=None,
            vcpkg_triplet="",
            child_env={},
        )

    def test_zenoh_requires_rust_toolchain(self):
        ctx = self.make_context(zenoh=True)

        def fake_which(command: str):
            if command in {"cargo", "rustc"}:
                return None
            return f"/usr/bin/{command}"

        with patch.object(hako.shutil, "which", side_effect=fake_which):
            errors, _warnings = hako.doctor(ctx)

        self.assertTrue(any("missing: cargo, rustc" in error for error in errors))

    def test_rust_toolchain_not_required_without_zenoh(self):
        ctx = self.make_context(zenoh=False)
        with patch.object(hako.shutil, "which", return_value="/usr/bin/cmake"):
            errors, _warnings = hako.doctor(ctx)
        self.assertEqual(errors, [])

    def test_python_dependencies_are_checked_with_selected_interpreter(self):
        ctx = self.make_context(zenoh=False)
        ctx.cfg["bindings"]["python"] = True
        selected = Path("/foundation/python/bin/python")

        with patch.object(hako.shutil, "which", return_value="/usr/bin/cmake"), patch.object(
            hako.Path, "is_file", return_value=True
        ), patch.object(
            hako, "_python_package_available", return_value=True
        ) as available:
            errors, _warnings = hako.doctor(ctx, selected)

        self.assertEqual(errors, [])
        self.assertEqual(
            available.call_args_list,
            [
                unittest.mock.call(selected, "cffi"),
                unittest.mock.call(selected, "setuptools"),
            ],
        )


class FoundationInstallTests(unittest.TestCase):
    def windows_shared_core_context(self):
        return type(
            "Context",
            (),
            {
                "platform_name": "windows",
                "cfg": {
                    "features": {"hakoniwa_core": True},
                    "build": {"shared_resolved": True},
                },
            },
        )()

    def test_windows_shared_core_variants_require_import_libraries(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            prefix = Path(temp_dir)
            for variant in ("core_callback", "core_polling"):
                dll = (
                    prefix
                    / "bin"
                    / f"hakoniwa_pdu_endpoint_{variant}.dll"
                )
                dll.parent.mkdir(parents=True, exist_ok=True)
                dll.touch()

            with self.assertRaisesRegex(
                hako.ConfigError,
                "core_callback.lib.*core_polling.lib",
            ):
                hako._validate_core_variant_artifacts(
                    self.windows_shared_core_context(),
                    prefix,
                )

    def test_windows_shared_core_variants_accept_complete_link_contract(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            prefix = Path(temp_dir)
            for variant in ("core_callback", "core_polling"):
                name = f"hakoniwa_pdu_endpoint_{variant}"
                for relative in (
                    Path("bin") / f"{name}.dll",
                    Path("lib") / f"{name}.lib",
                ):
                    artifact = prefix / relative
                    artifact.parent.mkdir(parents=True, exist_ok=True)
                    artifact.touch()

            hako._validate_core_variant_artifacts(
                self.windows_shared_core_context(),
                prefix,
            )

    def test_core_free_profile_removes_only_stale_endpoint_variants(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            prefix = Path(temp_dir) / "install"
            library_dir = prefix / "lib"
            package_dir = (
                prefix
                / "python"
                / "lib"
                / "python3.12"
                / "site-packages"
                / "hakoniwa_pdu_endpoint"
            )
            library_dir.mkdir(parents=True)
            package_dir.mkdir(parents=True)
            (package_dir / "c_endpoint.py").write_text("", encoding="utf-8")
            stale_callback = library_dir / "libhakoniwa_pdu_endpoint_core_callback.dylib"
            stale_polling = library_dir / "libhakoniwa_pdu_endpoint_core_polling.dylib"
            active = library_dir / "libhakoniwa_pdu_endpoint.dylib"
            unrelated = library_dir / "libunrelated.dylib"
            for path in (stale_callback, stale_polling, active, unrelated):
                path.write_text("", encoding="utf-8")
            ctx = type(
                "Context",
                (),
                {
                    "cfg": {
                        "features": {"hakoniwa_core": False},
                        "bindings": {"python": False},
                    }
                },
            )()

            hako._remove_stale_profile_artifacts(ctx, prefix)

            self.assertFalse(stale_callback.exists())
            self.assertFalse(stale_polling.exists())
            self.assertFalse(package_dir.exists())
            self.assertTrue(active.exists())
            self.assertTrue(unrelated.exists())

    def test_dependency_receipt_reads_core_contract_fields(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            prefix = Path(temp_dir)
            receipt = (
                prefix
                / "share"
                / "hakoniwa"
                / "receipts"
                / "hakoniwa-core-pro.yaml"
            )
            receipt.parent.mkdir(parents=True)
            receipt.write_text(
                """schema_version: 1
component:
  id: hakoniwa-core-pro
  version: 1.0.0
  source_revision: "abc123"
build_limits:
  asset_num: 16
  pdu_channel_max: 8192
artifacts:
  - path: "bin/hako-cmd"
    kind: executable
""",
                encoding="utf-8",
            )

            dependency = hako._read_dependency_receipt(
                prefix,
                "hakoniwa-core-pro",
            )

            self.assertEqual(dependency["version"], "1.0.0")
            self.assertEqual(dependency["source_revision"], "abc123")
            self.assertEqual(dependency["build_limits"]["asset_num"], 16)

    def test_endpoint_artifacts_do_not_expand_all_headers(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            prefix = Path(temp_dir)
            header_root = prefix / "include" / "hakoniwa" / "pdu"
            header_root.mkdir(parents=True)
            (header_root / "endpoint.hpp").write_text("", encoding="utf-8")
            for index in range(100):
                (header_root / f"generated_{index}.hpp").write_text("", encoding="utf-8")
            cmake_dir = prefix / "lib" / "cmake" / "hakoniwa_pdu_endpoint"
            cmake_dir.mkdir(parents=True)
            (prefix / "lib" / "libhakoniwa_pdu_endpoint.a").write_text(
                "",
                encoding="utf-8",
            )

            artifacts = hako._endpoint_artifacts(prefix)

            self.assertIn(
                (Path("include/hakoniwa/pdu/endpoint.hpp"), "header"),
                artifacts,
            )
            self.assertLess(len(artifacts), 10)

    def test_foundation_venv_python_is_cross_platform(self):
        root = Path("/foundation/python")
        self.assertEqual(
            hako._venv_python(root, "macos"),
            root / "bin" / "python",
        )
        self.assertEqual(
            hako._venv_python(root, "windows"),
            root / "Scripts" / "python.exe",
        )


if __name__ == "__main__":
    unittest.main()
