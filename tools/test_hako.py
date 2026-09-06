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


class VcpkgDiscoveryTests(unittest.TestCase):
    def make_vcpkg(self, root: Path) -> Path:
        toolchain = root / "scripts" / "buildsystems" / "vcpkg.cmake"
        toolchain.parent.mkdir(parents=True)
        toolchain.write_text("", encoding="utf-8")
        return root.resolve()

    def test_non_windows_ignores_ambient_vcpkg(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            repo_root = root / "repo"
            repo_root.mkdir()
            ambient = self.make_vcpkg(root / "ambient-vcpkg")
            self.make_vcpkg(root / "vcpkg")
            cfg = hako.resolve_config({"version": 1})

            with patch.dict(
                hako.os.environ,
                {
                    "VCPKG_ROOT": str(ambient),
                    "VCPKG_INSTALLATION_ROOT": str(ambient),
                },
                clear=False,
            ):
                self.assertIsNone(hako._find_vcpkg_root(cfg, repo_root, "macos"))
                self.assertIsNone(hako._find_vcpkg_root(cfg, repo_root, "linux"))

    def test_non_windows_honors_explicit_vcpkg(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            repo_root = root / "repo"
            repo_root.mkdir()
            explicit = self.make_vcpkg(root / "explicit-vcpkg")
            ambient = self.make_vcpkg(root / "ambient-vcpkg")
            cfg = hako.resolve_config(
                {
                    "version": 1,
                    "paths": {"vcpkg_root": str(explicit)},
                }
            )

            with patch.dict(
                hako.os.environ,
                {"VCPKG_INSTALLATION_ROOT": str(ambient)},
                clear=False,
            ):
                self.assertEqual(
                    hako._find_vcpkg_root(cfg, repo_root, "macos"),
                    explicit,
                )

    def test_windows_keeps_ambient_vcpkg_discovery(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            repo_root = root / "repo"
            repo_root.mkdir()
            ambient = self.make_vcpkg(root / "ambient-vcpkg")
            cfg = hako.resolve_config({"version": 1})

            with patch.dict(
                hako.os.environ,
                {
                    "VCPKG_ROOT": str(ambient),
                    "VCPKG_INSTALLATION_ROOT": str(ambient),
                },
                clear=False,
            ):
                self.assertEqual(
                    hako._find_vcpkg_root(cfg, repo_root, "windows"),
                    ambient,
                )


class PrepareTests(unittest.TestCase):
    def make_context(
        self,
        repo_root: Path,
        *,
        python_binding: bool = True,
        platform_name: str = "linux",
    ):
        cfg = hako.resolve_config(
            {
                "version": 1,
                "bindings": {"python": python_binding},
            }
        )
        return hako.BuildContext(
            repo_root=repo_root,
            manifest_path=repo_root / "hakoniwa-build.yaml",
            cfg=cfg,
            platform_name=platform_name,
            arch="arm64",
            build_dir=repo_root / "build",
            vcpkg_root=None,
            core_root=None,
            vcpkg_triplet="",
            child_env={},
        )

    def write_pyproject(self, repo_root: Path) -> None:
        (repo_root / "pyproject.toml").write_text(
            '[build-system]\nrequires = ["setuptools>=68", "wheel", "cffi>=1.16"]\n'
            'build-backend = "setuptools.build_meta"\n',
            encoding="utf-8",
        )

    def workspace(self, root: Path, *, platform_name: str = "linux"):
        home = root / "work" / "foundation" / "install"
        venv = home / "python"
        interpreter = hako._venv_python(venv, platform_name)
        interpreter.parent.mkdir(parents=True)
        interpreter.write_text("", encoding="utf-8")
        env = {
            "HAKONIWA_WORKSPACE_ACTIVE": "1",
            "HAKONIWA_WORKSPACE_ROOT": str(root),
            "HAKONIWA_HOME": str(home),
            "VIRTUAL_ENV": str(venv),
        }
        return venv, interpreter, env

    def test_prepare_refuses_outside_active_workspace(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            repo = root / "endpoint"
            repo.mkdir()
            self.write_pyproject(repo)
            venv = root / "python"
            interpreter = hako._venv_python(venv, "linux")
            interpreter.parent.mkdir(parents=True)
            interpreter.write_text("", encoding="utf-8")
            ctx = self.make_context(repo)

            with patch.dict(hako.os.environ, {}, clear=True), patch.object(
                hako, "_run"
            ) as run:
                with self.assertRaisesRegex(
                    hako.ConfigError, "only inside an active Hakoniwa"
                ):
                    hako.prepare(ctx, venv)

            run.assert_not_called()

    def test_prepare_refuses_non_workspace_python_target(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir) / "business-pack"
            repo = Path(temp_dir) / "endpoint"
            root.mkdir()
            repo.mkdir()
            self.write_pyproject(repo)
            _venv, _interpreter, env = self.workspace(root)
            other = Path(temp_dir) / "user-venv"
            ctx = self.make_context(repo)

            with patch.dict(hako.os.environ, env, clear=True), patch.object(
                hako, "_run"
            ) as run:
                with self.assertRaisesRegex(
                    hako.ConfigError, "refuses to modify a Python environment"
                ):
                    hako.prepare(ctx, other)

            run.assert_not_called()

    def test_prepare_installs_declared_requirements_into_foundation_python(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir) / "business-pack"
            repo = Path(temp_dir) / "endpoint"
            root.mkdir()
            repo.mkdir()
            self.write_pyproject(repo)
            venv, interpreter, env = self.workspace(root)
            ctx = self.make_context(repo)

            with patch.dict(hako.os.environ, env, clear=True), patch.object(
                hako, "_run"
            ) as run:
                hako.prepare(ctx, venv)

            run.assert_called_once_with(
                [
                    str(interpreter.resolve()),
                    "-m",
                    "pip",
                    "install",
                    "setuptools>=68",
                    "wheel",
                    "cffi>=1.16",
                ],
                cwd=repo,
            )

    def test_prepare_is_noop_for_cpp_only_profile(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            repo = Path(temp_dir) / "endpoint"
            repo.mkdir()
            ctx = self.make_context(repo, python_binding=False)

            with patch.dict(hako.os.environ, {}, clear=True), patch.object(
                hako, "_run"
            ) as run:
                hako.prepare(ctx, None)

            run.assert_not_called()


class DoctorTests(unittest.TestCase):
    def make_context(
        self,
        *,
        zenoh: bool,
        platform_name: str = "linux",
        vcpkg_root: Path | None = None,
        child_env: dict[str, str] | None = None,
    ):
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
            platform_name=platform_name,
            arch="arm64",
            build_dir=Path("build"),
            vcpkg_root=vcpkg_root,
            core_root=None,
            vcpkg_triplet="x64-windows" if platform_name == "windows" else "",
            child_env=child_env or {},
        )

    def test_common_probe_uses_cxx20_toolchain_and_native_search_environment(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            vcpkg = root / "vcpkg"
            toolchain = vcpkg / "scripts" / "buildsystems" / "vcpkg.cmake"
            toolchain.parent.mkdir(parents=True)
            toolchain.write_text("", encoding="utf-8")
            ctx = self.make_context(
                zenoh=False,
                platform_name="macos",
                vcpkg_root=vcpkg,
                child_env={"CMAKE_PREFIX_PATH": "/opt/homebrew/opt/boost"},
            )
            captured: dict[str, object] = {}

            def run(command, **kwargs):
                source = Path(command[command.index("-S") + 1])
                captured["project"] = (source / "CMakeLists.txt").read_text(
                    encoding="utf-8"
                )
                captured["command"] = command
                captured["env"] = kwargs["env"]
                return hako.subprocess.CompletedProcess(command, 0, "", "")

            with patch.object(hako.subprocess, "run", side_effect=run):
                result = hako._cmake_boost_headers_probe(
                    ctx, "/usr/bin/cmake", "/usr/bin/clang++"
                )

        self.assertTrue(result.available)
        self.assertIn("set(CMAKE_CXX_STANDARD 20)", captured["project"])
        self.assertIn("set(CMAKE_CXX_STANDARD_REQUIRED ON)", captured["project"])
        self.assertIn(
            f"-DCMAKE_TOOLCHAIN_FILE={toolchain}", captured["command"]
        )
        self.assertEqual(
            captured["env"]["CMAKE_PREFIX_PATH"], "/opt/homebrew/opt/boost"
        )
        self.assertEqual(captured["env"]["CXX"], "/usr/bin/clang++")

    def test_probe_distinguishes_discovery_from_cxx20_compile_failure(self):
        ctx = self.make_context(zenoh=False)
        failed = hako.subprocess.CompletedProcess(
            ["cmake"], 1, "", "HAKO_BOOST_HEADERS_COMPILE_FAILED"
        )
        with patch.object(hako.subprocess, "run", return_value=failed):
            result = hako._cmake_boost_headers_probe(
                ctx, "/usr/bin/cmake", "/usr/bin/c++"
            )

        self.assertFalse(result.available)
        self.assertIn("did not compile with C++20", result.detail)

    def test_windows_uses_the_common_cmake_probe_without_vcpkg(self):
        ctx = self.make_context(zenoh=False, platform_name="windows")
        available = hako.BoostProbeResult(True, "available")
        with patch.object(hako.shutil, "which", return_value="C:/cmake.exe"):
            with patch.object(
                hako, "_cmake_boost_headers_probe", return_value=available
            ) as probe:
                errors, warnings = hako.doctor(ctx)

        self.assertEqual(errors, [])
        self.assertEqual(warnings, [])
        probe.assert_called_once_with(ctx, "C:/cmake.exe", None)

    def test_windows_probe_failure_adds_selected_vcpkg_package_hint(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            ctx = self.make_context(
                zenoh=False,
                platform_name="windows",
                vcpkg_root=Path(temp_dir) / "vcpkg",
            )
            missing = hako.BoostProbeResult(
                False, "Boost headers were not discovered by CMake"
            )
            with patch.object(hako.shutil, "which", return_value="C:/cmake.exe"):
                with patch.object(
                    hako, "_cmake_boost_headers_probe", return_value=missing
                ):
                    errors, warnings = hako.doctor(ctx)

        self.assertEqual(warnings, [])
        self.assertEqual(len(errors), 1)
        self.assertIn("boost-asio:x64-windows", errors[0])
        self.assertIn("boost-beast:x64-windows", errors[0])

    def test_zenoh_requires_rust_toolchain(self):
        ctx = self.make_context(zenoh=True)

        def fake_which(command: str):
            if command in {"cargo", "rustc"}:
                return None
            return f"/usr/bin/{command}"

        with patch.object(hako.shutil, "which", side_effect=fake_which), patch.object(
            hako,
            "_cmake_boost_headers_probe",
            return_value=hako.BoostProbeResult(True, "available"),
        ):
            errors, _warnings = hako.doctor(ctx)

        self.assertTrue(any("missing: cargo, rustc" in error for error in errors))

    def test_rust_toolchain_not_required_without_zenoh(self):
        ctx = self.make_context(zenoh=False)
        with patch.object(hako.shutil, "which", return_value="/usr/bin/tool"), patch.object(
            hako,
            "_cmake_boost_headers_probe",
            return_value=hako.BoostProbeResult(True, "available"),
        ):
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
        ) as available, patch.object(
            hako,
            "_cmake_boost_headers_probe",
            return_value=hako.BoostProbeResult(True, "available"),
        ):
            errors, _warnings = hako.doctor(ctx, selected)

        self.assertEqual(errors, [])
        self.assertEqual(
            available.call_args_list,
            [
                unittest.mock.call(selected, "cffi"),
                unittest.mock.call(selected, "setuptools"),
            ],
        )

    def test_linux_missing_boost_headers_are_actionable(self):
        ctx = self.make_context(zenoh=False)
        with patch.object(hako.shutil, "which", return_value="/usr/bin/tool"), patch.object(
            hako,
            "_cmake_boost_headers_probe",
            return_value=hako.BoostProbeResult(
                False, "Boost headers were not discovered by CMake"
            ),
        ):
            errors, _warnings = hako.doctor(ctx)

        self.assertEqual(len(errors), 1)
        self.assertIn("hakoniwa-pdu-endpoint", errors[0])
        self.assertIn("boost/asio.hpp", errors[0])
        self.assertIn("boost/beast.hpp", errors[0])
        self.assertIn("platform: linux arm64", errors[0])
        self.assertNotIn("apt install", errors[0])

    def test_linux_discoverable_boost_headers_pass(self):
        ctx = self.make_context(zenoh=False)
        with patch.object(hako.shutil, "which", return_value="/usr/bin/tool"), patch.object(
            hako,
            "_cmake_boost_headers_probe",
            return_value=hako.BoostProbeResult(True, "available"),
        ):
            errors, _warnings = hako.doctor(ctx)

        self.assertEqual(errors, [])

    def test_linux_missing_compiler_is_reported_without_boost_probe(self):
        ctx = self.make_context(zenoh=False)

        def fake_which(command: str):
            return "/usr/bin/cmake" if command == "cmake" else None

        with patch.object(hako.shutil, "which", side_effect=fake_which), patch.object(
            hako, "_cmake_boost_headers_probe"
        ) as probe:
            errors, _warnings = hako.doctor(ctx)

        self.assertTrue(any("C++ compiler" in error for error in errors))
        probe.assert_not_called()


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
