from concurrent.futures import ThreadPoolExecutor
import importlib
import inspect
import math
import os
import sys
import threading
import time
import tempfile
import unittest
from pathlib import Path
from unittest import mock
from sample import Result, Sampler, sampler


sample_module = importlib.import_module("sample.sample")


def create_source_checkout(root):
    markers = [root / "SConstruct", root / "setup.py", root / "sample" / "sample.py"]
    for marker in markers:
        marker.parent.mkdir(parents=True, exist_ok=True)
        marker.touch()


class DriverDiscoveryTest(unittest.TestCase):
    def test_installed_package_ignores_unrelated_sconstruct(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            installed_root = root / "venv" / "Lib" / "site-packages"
            package_file = installed_root / "sample" / "sample.py"
            executable_dir = root / "python"
            prefix = root / "venv"
            for candidate_dir in (cwd, package_file.parent, executable_dir, prefix):
                candidate_dir.mkdir(parents=True, exist_ok=True)
            package_file.touch()
            (installed_root / "SConstruct").touch()
            prefix_driver = prefix / "sample.exe"
            prefix_driver.touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(package_file)), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", installed_root, create=True), \
                        mock.patch.object(sys, "executable", str(executable_dir / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.object(sample_module.shutil, "which", return_value=None), \
                        mock.patch.object(sample_module.os, "system", return_value=1) as system:
                    self.assertEqual(Sampler().execution_path, str(prefix_driver.resolve()))
                    system.assert_not_called()
            finally:
                os.chdir(str(original_cwd))

    def test_relative_module_file_keeps_source_root_after_cwd_changes(self):
        source_text = Path(sample_module.__file__).read_text(encoding="utf-8")
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            import_root = root / "import-root"
            source_root = import_root / "relative-source"
            later_cwd = root / "later-cwd"
            relative_file = Path("relative-source") / "sample" / "sample.py"
            create_source_checkout(source_root)
            source_driver = source_root / "cpp_build" / "sample.exe"
            source_driver.parent.mkdir()
            source_driver.touch()
            later_cwd.mkdir()

            namespace = {
                "__file__": str(relative_file),
                "__name__": "sample.relative_discovery_test",
                "__package__": "sample",
            }
            try:
                os.chdir(str(import_root))
                exec(compile(source_text, str(relative_file), "exec"), namespace)
                os.chdir(str(later_cwd))
                with mock.patch.object(sys, "executable", str(root / "python" / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(root / "venv")), \
                        mock.patch.object(namespace["shutil"], "which", return_value=None):
                    discovered = namespace["Sampler"]().execution_path
                    self.assertEqual(discovered, str(source_driver.resolve()))
            finally:
                os.chdir(str(original_cwd))

    def test_path_lookup_errors_keep_stable_not_found_error(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            package_file = root / "installed" / "sample" / "sample.py"
            cwd.mkdir(parents=True)
            package_file.parent.mkdir(parents=True)
            package_file.touch()

            try:
                os.chdir(str(cwd))
                for lookup_error in (OSError("lookup failed"), ValueError("invalid PATH")):
                    with self.subTest(lookup_error=type(lookup_error).__name__), \
                            mock.patch.object(sample_module, "__file__", str(package_file)), \
                            mock.patch.object(sample_module, "_SOURCE_ROOT", package_file.parent.parent, create=True), \
                            mock.patch.object(sys, "executable", str(root / "python" / "python.exe")), \
                            mock.patch.object(sys, "prefix", str(root / "venv")), \
                            mock.patch.object(sample_module.shutil, "which", side_effect=lookup_error):
                        with self.assertRaisesRegex(Sampler.Error, "^Sample Driver Not Found$"):
                            Sampler().execution_path
            finally:
                os.chdir(str(original_cwd))

    def test_concurrent_source_discovery_builds_driver_once(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            create_source_checkout(source_root)
            source_driver = source_root / "cpp_build" / "sample.exe"
            source_driver.parent.mkdir()
            cwd.mkdir()
            real_existing_file = sample_module._existing_file
            state_lock = threading.Lock()
            missing_driver_barrier = threading.Barrier(2)
            state = {"calls": 0, "prechecks": 0}

            def synchronize_missing_driver_checks(*parts):
                result = real_existing_file(*parts)
                driver_parts = (source_root, "cpp_build", "sample.exe")
                if parts == driver_parts and result is None:
                    with state_lock:
                        should_wait = state["prechecks"] < 2
                        if should_wait:
                            state["prechecks"] += 1
                    if should_wait:
                        missing_driver_barrier.wait()
                return result

            def build_driver(_command):
                with state_lock:
                    state["calls"] += 1
                source_driver.touch()
                return 0

            def discover():
                return Sampler().execution_path

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root, create=True), \
                        mock.patch.object(sys, "executable", str(root / "python" / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(root / "venv")), \
                        mock.patch.object(sample_module.shutil, "which", return_value=None), \
                        mock.patch.object(sample_module, "_existing_file",
                                          side_effect=synchronize_missing_driver_checks), \
                        mock.patch.object(sample_module.os, "system", side_effect=build_driver):
                    with ThreadPoolExecutor(max_workers=2) as executor:
                        results = list(executor.map(lambda _index: discover(), range(2)))
                self.assertEqual(results, [str(source_driver.resolve())] * 2)
                self.assertEqual(state["calls"], 1)
                self.assertEqual(state["prechecks"], 2)
            finally:
                os.chdir(str(original_cwd))

    def test_concurrent_source_build_failures_are_consistent(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            create_source_checkout(source_root)
            (source_root / "cpp_build").mkdir()
            cwd.mkdir()
            real_existing_file = sample_module._existing_file
            state_lock = threading.Lock()
            missing_driver_barrier = threading.Barrier(2)
            state = {"calls": 0, "prechecks": 0}

            def synchronize_missing_driver_checks(*parts):
                result = real_existing_file(*parts)
                driver_parts = (source_root, "cpp_build", "sample.exe")
                if parts == driver_parts and result is None:
                    with state_lock:
                        should_wait = state["prechecks"] < 2
                        if should_wait:
                            state["prechecks"] += 1
                    if should_wait:
                        missing_driver_barrier.wait()
                return result

            def fail_build(_command):
                with state_lock:
                    state["calls"] += 1
                return 1

            def discover_error():
                try:
                    Sampler().execution_path
                except Sampler.Error as error:
                    return str(error)
                return "no error"

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sample_module, "_existing_file",
                                          side_effect=synchronize_missing_driver_checks), \
                        mock.patch.object(sample_module.os, "system", side_effect=fail_build):
                    with ThreadPoolExecutor(max_workers=2) as executor:
                        errors = list(executor.map(lambda _index: discover_error(), range(2)))
                self.assertEqual(errors, ["Compile C++ Driver Error"] * 2)
                self.assertEqual(state["calls"], 2)
                self.assertEqual(state["prechecks"], 2)
            finally:
                os.chdir(str(original_cwd))

    def test_existing_file_does_not_swallow_internal_type_error(self):
        with mock.patch.object(sample_module.Path, "is_file", side_effect=TypeError("programming error")):
            with self.assertRaisesRegex(TypeError, "programming error"):
                sample_module._existing_file("sample.exe")

    def test_path_lookup_type_error_propagates(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            cwd.mkdir()
            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "_SOURCE_ROOT", root / "installed"), \
                        mock.patch.object(sys, "executable", str(root / "python" / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(root / "venv")), \
                        mock.patch.object(sample_module.shutil, "which",
                                          side_effect=TypeError("programming error")):
                    with self.assertRaisesRegex(TypeError, "programming error"):
                        Sampler().execution_path
            finally:
                os.chdir(str(original_cwd))

    def test_existing_file_handles_file_directory_and_broken_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            unicode_target = root / "驱动文件.exe"
            unicode_target.touch()
            directory_target = root / "driver-directory"
            directory_target.mkdir()
            file_link = root / "file-link.exe"
            directory_link = root / "directory-link.exe"
            broken_link = root / "broken-link.exe"
            try:
                os.symlink(str(unicode_target), str(file_link))
                os.symlink(str(directory_target), str(directory_link), target_is_directory=True)
                os.symlink(str(root / "missing.exe"), str(broken_link))
            except (NotImplementedError, OSError) as error:
                self.skipTest("symbolic links unavailable: {}".format(error))

            self.assertEqual(sample_module._existing_file(file_link), str(unicode_target.resolve()))
            self.assertIsNone(sample_module._existing_file(directory_link))
            self.assertIsNone(sample_module._existing_file(broken_link))

    def test_source_marker_directories_do_not_trigger_build(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            prefix = root / "venv"
            cwd.mkdir()
            (source_root / "sample").mkdir(parents=True)
            (source_root / "sample" / "sample.py").touch()
            (source_root / "SConstruct").touch()
            (source_root / "setup.py").mkdir()
            prefix.mkdir()
            prefix_driver = prefix / "sample.exe"
            prefix_driver.touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", str(root / "python" / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.object(sample_module.shutil, "which", return_value=None), \
                        mock.patch.object(sample_module.os, "system") as system:
                    self.assertEqual(Sampler().execution_path, str(prefix_driver.resolve()))
                    system.assert_not_called()
            finally:
                os.chdir(str(original_cwd))

    def test_existing_source_driver_does_not_enter_build_lock(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            create_source_checkout(source_root)
            source_driver = source_root / "cpp_build" / "sample.exe"
            source_driver.parent.mkdir()
            source_driver.touch()
            cwd.mkdir()
            build_lock = mock.MagicMock()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sample_module, "_SOURCE_BUILD_LOCK", build_lock):
                    self.assertEqual(Sampler().execution_path, str(source_driver.resolve()))
                    build_lock.__enter__.assert_not_called()
            finally:
                os.chdir(str(original_cwd))

    def test_source_build_failure_and_exception_allow_later_retry(self):
        original_cwd = Path.cwd()
        for first_failure in ("return_code", "exception"):
            with self.subTest(first_failure=first_failure), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                cwd = root / "cwd"
                source_root = root / "source"
                create_source_checkout(source_root)
                source_driver = source_root / "cpp_build" / "sample.exe"
                source_driver.parent.mkdir()
                cwd.mkdir()
                calls = []

                def build_driver(_command):
                    calls.append(len(calls) + 1)
                    if len(calls) == 1:
                        if first_failure == "exception":
                            raise OSError("scons unavailable")
                        return 1
                    source_driver.touch()
                    return 0

                try:
                    os.chdir(str(cwd))
                    with mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                            mock.patch.object(sample_module.os, "system", side_effect=build_driver):
                        expected_error = OSError if first_failure == "exception" else Sampler.Error
                        with self.assertRaises(expected_error):
                            Sampler().execution_path
                        self.assertEqual(Sampler().execution_path, str(source_driver.resolve()))
                        self.assertEqual(calls, [1, 2])
                finally:
                    os.chdir(str(original_cwd))

    def test_execution_path_does_not_build_when_cwd_driver_exists(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            for candidate_dir in (cwd, source_root / "sample"):
                candidate_dir.mkdir(parents=True)
            cwd_driver = cwd / "sample.exe"
            cwd_driver.touch()
            (source_root / "SConstruct").touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sample_module.os, "system", return_value=1) as system:
                    self.assertEqual(Path(Sampler().execution_path).resolve(), cwd_driver.resolve())
                    system.assert_not_called()
            finally:
                os.chdir(str(original_cwd))

    def test_execution_path_preserves_exact_candidate_priority(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            executable_dir = root / "python"
            prefix = root / "venv"
            path_dir = root / "path"
            for candidate_dir in (cwd, source_root / "sample", source_root / "cpp_build",
                                  executable_dir, prefix, path_dir):
                candidate_dir.mkdir(parents=True)
            create_source_checkout(source_root)
            candidates = [
                cwd / "sample.exe",
                source_root / "cpp_build" / "sample.exe",
                executable_dir / "sample.exe",
                prefix / "sample.exe",
                path_dir / "sample.exe",
            ]
            for candidate in candidates:
                candidate.touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", str(executable_dir / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.dict(os.environ, {"PATH": str(path_dir)}, clear=True), \
                        mock.patch.object(sample_module.os, "system") as system:
                    expected_cwd = Path.cwd()
                    expected_path = os.environ["PATH"]
                    for expected in candidates:
                        self.assertEqual(Path(Sampler().execution_path).resolve(), expected.resolve())
                        self.assertEqual(Path.cwd(), expected_cwd)
                        self.assertEqual(os.environ["PATH"], expected_path)
                        expected.unlink()
                        if expected == source_root / "cpp_build" / "sample.exe":
                            (source_root / "SConstruct").unlink()
                    system.assert_not_called()
            finally:
                os.chdir(str(original_cwd))

    def test_execution_path_uses_sys_prefix_before_path(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            executable_dir = root / "python"
            prefix = root / "venv"
            path_dir = root / "path"
            for candidate_dir in (cwd, source_root / "sample", executable_dir, prefix, path_dir):
                candidate_dir.mkdir(parents=True)
            prefix_driver = prefix / "sample.exe"
            prefix_driver.touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", str(executable_dir / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.dict(os.environ, {"PATH": str(path_dir)}, clear=True):
                    self.assertEqual(Sampler().execution_path, str(prefix_driver.resolve()))
            finally:
                os.chdir(str(original_cwd))

    def test_execution_path_uses_executable_sibling_before_prefix_and_path(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            executable_dir = root / "python"
            prefix = root / "venv"
            path_dir = root / "path"
            for candidate_dir in (cwd, source_root / "sample", executable_dir, prefix, path_dir):
                candidate_dir.mkdir(parents=True)
            executable_driver = executable_dir / "sample.exe"
            executable_driver.touch()
            (prefix / "sample.exe").touch()
            (path_dir / "sample.exe").touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", str(executable_dir / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.dict(os.environ, {"PATH": str(path_dir)}, clear=True):
                    self.assertEqual(Sampler().execution_path, str(executable_driver.resolve()))
            finally:
                os.chdir(str(original_cwd))

    def test_execution_path_ignores_directory_candidates(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            executable_dir = root / "python"
            prefix = root / "venv"
            path_dir = root / "path"
            for candidate_dir in (cwd, source_root / "sample", source_root / "cpp_build",
                                  executable_dir, prefix, path_dir):
                candidate_dir.mkdir(parents=True)
            create_source_checkout(source_root)
            (cwd / "sample.exe").mkdir()
            source_driver = source_root / "cpp_build" / "sample.exe"
            source_driver.touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", str(executable_dir / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.dict(os.environ, {"PATH": str(path_dir)}, clear=True):
                    self.assertEqual(Path(Sampler().execution_path).resolve(), source_driver.resolve())
            finally:
                os.chdir(str(original_cwd))

    def test_execution_path_rejects_path_directory_with_stable_error(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            executable_dir = root / "python"
            prefix = root / "venv"
            path_driver = root / "path" / "sample.exe"
            for candidate_dir in (cwd, source_root / "sample", executable_dir, prefix, path_driver):
                candidate_dir.mkdir(parents=True)

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", str(executable_dir / "python.exe")), \
                        mock.patch.object(sys, "prefix", str(prefix)), \
                        mock.patch.object(sample_module.shutil, "which", return_value=str(path_driver)):
                    with self.assertRaisesRegex(Sampler.Error, "^Sample Driver Not Found$"):
                        Sampler().execution_path
            finally:
                os.chdir(str(original_cwd))

    def test_execution_path_skips_invalid_executable_path(self):
        original_cwd = Path.cwd()
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cwd = root / "cwd"
            source_root = root / "source"
            prefix = root / "venv"
            path_dir = root / "path"
            for candidate_dir in (cwd, source_root / "sample", prefix, path_dir):
                candidate_dir.mkdir(parents=True)
            prefix_driver = prefix / "sample.exe"
            prefix_driver.touch()

            try:
                os.chdir(str(cwd))
                with mock.patch.object(sample_module, "__file__", str(source_root / "sample" / "sample.py")), \
                        mock.patch.object(sample_module, "_SOURCE_ROOT", source_root), \
                        mock.patch.object(sys, "executable", None), \
                        mock.patch.object(sys, "prefix", prefix), \
                        mock.patch.dict(os.environ, {"PATH": str(path_dir)}, clear=True):
                    self.assertEqual(Sampler().execution_path, str(prefix_driver.resolve()))
            finally:
                os.chdir(str(original_cwd))


def wait_until_sampling_stops(is_measuring, cancel, timeout_seconds=5.0,
                              monotonic=time.monotonic, sleep=time.sleep):
    deadline = monotonic() + timeout_seconds
    while is_measuring():
        if monotonic() >= deadline:
            try:
                cancel()
                cancel_diagnostic = "best-effort cancellation requested"
            except Exception as error:
                cancel_diagnostic = "best-effort cancellation failed: {!r}".format(error)
            raise AssertionError(
                "sampling remained active after {:.3f} seconds; {}".format(
                    timeout_seconds, cancel_diagnostic
                )
            )
        sleep(0.01)


class ResponseExecutor:
    def __init__(self, response):
        self.response = response
        self.commands = []

    def write_line(self, command):
        self.commands.append(command)

    def read_until(self, marker):
        return self.response


class SamplerProtocolIntegrationTest(unittest.TestCase):
    def test_communicate_parses_result_fields_without_dynamic_execution(self):
        response = "\n".join((
            "success=True",
            'message="success"',
            "maximum=9.500000",
            "minimum=-2.250000",
        ))

        result = Sampler().communicate("to_query", ResponseExecutor(response))

        self.assertTrue(result.success)
        self.assertEqual(result.message, "success")
        self.assertEqual(result.maximum, 9.5)
        self.assertEqual(result.minimum, -2.25)
        source = inspect.getsource(Sampler.communicate)
        self.assertNotIn("exec(", source)
        self.assertNotIn("eval(", source)

    def test_communicate_rejects_wrong_type_for_known_field(self):
        executor = ResponseExecutor('success=1\nmessage="success"')

        with self.assertRaisesRegex(Sampler.Error, "line=1.*field='success'.*response_length="):
            Sampler().communicate("to_query", executor)

    def test_communicate_allows_safe_unknown_fields(self):
        result = Sampler().communicate("to_query", ResponseExecutor("future_field=17"))

        self.assertEqual(result.future_field, 17)

    def test_communicate_sets_voltage_reported_marker_internally(self):
        missing = Sampler().communicate("to_query", ResponseExecutor("success=True"))
        reported = Sampler().communicate("to_query", ResponseExecutor("v_inf=0.0"))

        self.assertFalse(missing._v_inf_reported)
        self.assertTrue(reported._v_inf_reported)

    def test_protocol_error_uses_bounded_response_summary(self):
        response = "success=invalid\nsecret=" + "x" * 2000

        with self.assertRaises(Sampler.Error) as raised:
            Sampler().communicate("to_query", ResponseExecutor(response))

        message = str(raised.exception)
        self.assertIn("line=1", message)
        self.assertIn("field='success'", message)
        self.assertIn("response_length=2023", message)
        self.assertLess(len(message), 800)
        self.assertNotIn("x" * 1000, message)

    def test_query_preserves_normal_result_processing(self):
        result = Result()
        result.success = True
        result.message = "success"
        result.maximum = 3.0
        result.minimum = 1.0
        result.sampling_interval = 1.0
        result.wave_interval = 2.0
        result.tau = 4.0
        result.w = 2.0
        result.b = 3.0
        result.loss = 0.5
        result.wave = [5.0, 4.0]
        client = Sampler()
        client.communicate = lambda command, executor=None: result

        queried = client.query()

        self.assertEqual(queried.time_line, [0.0, 2.0])
        self.assertEqual(len(queried.estimate), 2)
        self.assertEqual(queried.v0, 5.0)

    def test_query_does_not_override_existing_failure_with_non_finite_fields(self):
        result = Result()
        result.success = False
        result.message = "fit_not_identifiable"
        result.tau = float("nan")
        client = Sampler()
        client.communicate = lambda command, executor=None: result

        queried = client.query()

        self.assertEqual(queried.message, "fit_not_identifiable")
        self.assertEqual(queried.invalid_field, "")

    def test_query_rejects_each_non_finite_scalar_without_processing(self):
        for field in ("maximum", "minimum", "sampling_interval", "wave_interval", "tau", "w", "b", "loss",
                      "v_inf", "cycle_maximum", "cycle_minimum"):
            for value in (float("nan"), float("inf"), float("-inf")):
                with self.subTest(field=field, value=value):
                    result = self._valid_result()
                    setattr(result, field, value)
                    queried = self._query(result)
                    self._assert_non_finite_failure(queried, field)

    def test_query_rejects_legacy_non_finite_wave_tokens_without_processing(self):
        for token in ("nan", "1.#IND", "1.#INF", "-1.#INF"):
            with self.subTest(token=token):
                response = self._valid_response("wave=[1.0, {0}]".format(token))
                client = Sampler()
                client.communicate = lambda command, executor=None: Sampler().communicate(
                    command, ResponseExecutor(response)
                )

                queried = client.query()

                self._assert_non_finite_failure(queried, "wave[1]")

    @staticmethod
    def _valid_result():
        result = Result()
        result.success = True
        result.message = "success"
        for field, value in (("maximum", 3.0), ("minimum", 1.0), ("sampling_interval", 1.0),
                             ("wave_interval", 2.0), ("tau", 4.0), ("w", 2.0), ("b", 3.0), ("loss", 0.5)):
            setattr(result, field, value)
        result.wave = [5.0, 4.0]
        result.v0 = 99.0
        result.v_inf = 88.0
        result.v_inf_valid = True
        return result

    def test_query_accepts_explicit_valid_voltage_on_failed_fit(self):
        response = "\n".join((
            "success=False", 'message="fit_not_identifiable"', "v_inf=4.2", "v_inf_valid=True",
        ))
        client = Sampler()
        client.communicate = lambda command, executor=None: Sampler().communicate(
            command, ResponseExecutor(response)
        )

        queried = client.query()

        self.assertEqual(queried.message, "fit_not_identifiable")
        self.assertEqual(queried.v_inf, 4.2)
        self.assertTrue(queried.v_inf_valid)

    def test_query_rejects_unvalidated_voltage_without_rewriting_failure(self):
        for voltage, validity in ((4.2, False), (0.0, True), (float("nan"), True), (float("inf"), True)):
            with self.subTest(voltage=voltage, validity=validity):
                result = Result()
                result.success = False
                result.message = "fit_not_identifiable"
                result.error_category = "calculation"
                result.retryable = False
                result.v_inf = voltage
                result.v_inf_valid = validity
                result._v_inf_reported = True

                queried = self._query(result)

                self.assertFalse(queried.success)
                self.assertEqual(queried.message, "fit_not_identifiable")
                self.assertEqual(queried.error_category, "calculation")
                self.assertFalse(queried.retryable)
                self.assertEqual(queried.v_inf, 0.0)
                self.assertFalse(queried.v_inf_valid)

    @staticmethod
    def _valid_response(wave_line):
        return "\n".join((
            "success=True", 'message="success"', "maximum=3.000000", "minimum=1.000000",
            "sampling_interval=1.000000", "wave_interval=2.000000", "tau=4.000000", "w=2.000000",
            "b=3.000000", "loss=0.500000", wave_line,
        ))

    @staticmethod
    def _query(result):
        client = Sampler()
        client.communicate = lambda command, executor=None: result
        return client.query()

    def _assert_non_finite_failure(self, result, field):
        self.assertFalse(result.success)
        self.assertEqual(result.message, "sampling_result_not_finite")
        self.assertEqual(result.error_category, "state")
        self.assertTrue(result.retryable)
        self.assertEqual(result.invalid_field, field)
        self.assertEqual(result.wave, [])
        self.assertEqual(result.time_line, [])
        self.assertEqual(result.estimate, [])
        self.assertEqual(result.v0, 0.0)
        self.assertEqual(result.v_inf, 0.0)
        self.assertFalse(result.v_inf_valid)


class MyTestCase(unittest.TestCase):
    def setUp(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 0)
        sampler.set_sampler_value("mock_work_iterations", 0)

    def tearDown(self):
        wait_until_sampling_stops(
            is_measuring=lambda: sampler.is_measuring,
            cancel=sampler.cancel_sampling,
        )
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 0)
        sampler.set_sampler_value("mock_work_iterations", 0)

    def test_optional_instant_ai_arguments_are_backward_compatible(self):
        client = Sampler()
        client.communicate = lambda command, executor=None: command
        self.assertEqual(client.measure(3, 0.1), "to_measure 3 0.1 False")
        self.assertEqual(
            client.measure(3, 0.1, instant_ai_frequency_threshold=0.5),
            "to_measure 3 0.1 False 0.5 100 10",
        )
        self.assertEqual(
            client.measure(
                3,
                0.05,
                instant_ai_frequency_threshold=0.1,
                instant_ai_target_points_per_waveform=100,
                instant_ai_max_reliable_polling_hz=10.0,
            ),
            "to_measure 3 0.05 False 0.1 100 10",
        )
        self.assertEqual(
            client.dump("capture.csv", 3, 0.05),
            "to_dump capture.csv 3 0.05 False",
        )
        self.assertEqual(
            client.dump(
                "capture.csv",
                3,
                0.05,
                instant_ai_frequency_threshold=0.1,
                instant_ai_target_points_per_waveform=100,
                instant_ai_max_reliable_polling_hz=9.5,
            ),
            "to_dump capture.csv 3 0.05 False 0.1 100 9.5",
        )

    def test_progress_and_cancel_methods_send_exact_commands(self):
        client = Sampler()
        client.communicate = lambda command, executor=None: command
        self.assertEqual(client.sampling_progress(), "to_sampling_progress")
        self.assertEqual(client.cancel_sampling(), "to_cancel_sampling")

    def test_result_has_backward_compatible_instant_ai_defaults(self):
        result = Result()
        self.assertEqual(result.error_category, "")
        self.assertFalse(result.retryable)
        self.assertFalse(result.cancelled)
        self.assertEqual(result.planned_seconds, 0.0)
        self.assertEqual(result.elapsed_seconds, 0.0)
        self.assertEqual(result.completed_cycles, 0)
        self.assertEqual(result.target_cycles, 0)
        self.assertEqual(result.successful_reads, 0)
        self.assertEqual(result.late_reads, 0)
        self.assertEqual(result.acquisition_mode, "")
        self.assertEqual(result.instant_ai_complete_waveforms, 0)
        self.assertEqual(result.instant_ai_planned_duration_seconds, 0.0)
        self.assertEqual(result.instant_ai_actual_duration_seconds, 0.0)
        self.assertEqual(result.instant_ai_late_reads, 0)
        self.assertEqual(result.instant_ai_interpolated_bins, 0)
        self.assertFalse(result.v_inf_valid)

    def test_result_process_preserves_reported_business_v_inf(self):
        result = Result()
        result.success = True
        result.wave_interval = 1.0
        result.wave = [1.0, 0.5]
        result.tau = 10.0
        result.w = -0.5
        result.b = 9.0
        result.v_inf = 1.3
        result.v_inf_valid = True
        result._v_inf_reported = True

        result.process()

        self.assertEqual(result.v0, 8.5)
        self.assertEqual(result.v_inf, 1.3)
        self.assertTrue(result.v_inf_valid)

    def test_result_process_falls_back_to_b_for_old_server_response(self):
        result = Result()
        result.success = True
        result.wave_interval = 1.0
        result.wave = [1.0, 0.5]
        result.tau = 10.0
        result.w = -0.5
        result.b = 9.0

        result.process()

        self.assertEqual(result.v_inf, 9.0)
        self.assertTrue(result.v_inf_valid)

    def test_result_process_does_not_replace_explicit_zero_voltage(self):
        result = Result()
        result.success = True
        result.wave_interval = 1.0
        result.wave = [1.0, 0.5]
        result.tau = 10.0
        result.w = -0.5
        result.b = 9.0
        result.v_inf = 0.0
        result.v_inf_valid = True
        result._v_inf_reported = True

        result.process()

        self.assertEqual(result.v_inf, 0.0)
        self.assertFalse(result.v_inf_valid)

    def test_readme_documents_retryable_state_separately_from_acquisition_failures(self):
        readme = (Path(__file__).resolve().parents[1] / "README.md").read_text(encoding="utf-8")
        self.assertIn("Instant AI 采集失败的可重试类别", readme)
        self.assertIn("`state` 表示生命周期或并发冲突", readme)
        self.assertIn("当前测量结束后再试", readme)
        self.assertIn("`state` 当前也是 `retryable=True`", readme)

    def test_is_measuring(self):
        result = sampler.communicate("is_measuring")
        self.assertEqual(result.measuring, False)

    def test_measure_rejects_malformed_or_extra_instant_ai_options(self):
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_measure 3 0.05 False invalid 100 10")
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_measure 3 0.05 False 0.1 100 10 extra")

    def test_config_rejects_malformed_or_extra_instant_ai_options(self):
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_config 3 0.05 False 0.1 invalid")
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_config 3 0.05 False 0.1 100 invalid")
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_config 3 0.05 False 0.1 100 10 extra")

    def test_active_measurement_rejects_overlapping_request_without_mutating_config(self):
        sampler.set_sampler_value("mock_work_iterations", 100000000)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
            sampler.measure(1, 0.1, instant_ai_frequency_threshold=0.1)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
                sampler.dump(str(Path(directory) / "busy.csv"), 1, 0.1, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.instant_ai_complete_waveforms, 3)
        self.assertAlmostEqual(result.instant_ai_planned_duration_seconds, 80.0)

    def test_query_during_active_measurement_is_not_completed_success(self):
        sampler.set_sampler_value("mock_work_iterations", 100000000)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        active = sampler.query()
        self.assertTrue(active.measuring)
        self.assertFalse(active.success)
        while sampler.is_measuring:
            time.sleep(0.01)
        self.assertTrue(sampler.query().success)

    def test_sampling_stop_wait_has_poll_delay_deadline_and_timeout_cleanup(self):
        now = [0.0]
        sleep_calls = []
        cancel_calls = []

        def fake_sleep(seconds):
            sleep_calls.append(seconds)
            now[0] += seconds

        with self.assertRaisesRegex(AssertionError, "sampling remained active after 0.030 seconds"):
            wait_until_sampling_stops(
                is_measuring=lambda: True,
                cancel=lambda: cancel_calls.append(True),
                timeout_seconds=0.03,
                monotonic=lambda: now[0],
                sleep=fake_sleep,
            )
        self.assertGreaterEqual(len(sleep_calls), 3)
        self.assertTrue(all(seconds == 0.01 for seconds in sleep_calls))
        self.assertEqual(cancel_calls, [True])

    def test_public_progress_and_cancel_stop_active_mock_with_bounded_wait(self):
        sampler.set_sampler_value("mock_work_iterations", 2000000000)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        progress = sampler.sampling_progress()
        self.assertTrue(progress.measuring)
        self.assertAlmostEqual(progress.planned_seconds, 80.0)
        self.assertGreaterEqual(progress.elapsed_seconds, 0.0)
        self.assertLessEqual(progress.elapsed_seconds, progress.planned_seconds)
        self.assertGreaterEqual(progress.completed_cycles, 0)
        self.assertLessEqual(progress.completed_cycles, progress.target_cycles)
        self.assertEqual(progress.target_cycles, 4)
        self.assertGreaterEqual(progress.successful_reads, 0)
        self.assertGreaterEqual(progress.late_reads, 0)
        with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
            sampler.set_sampler("mock_sampler")
        with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
            sampler.set_sampler_value("mock_tau", 100)
        sampler.cancel_sampling()
        wait_until_sampling_stops(
            is_measuring=lambda: sampler.is_measuring,
            cancel=sampler.cancel_sampling,
        )
        result = sampler.query()
        self.assertFalse(result.success)
        self.assertTrue(result.cancelled)
        self.assertEqual(result.message, "user_cancelled")
        self.assertEqual(result.error_category, "cancelled")
        self.assertFalse(result.retryable)

    def test_mock_missing_bin_controls_reject_invalid_values(self):
        sampler.set_sampler_value("mock_missing_bin_start", 7)
        sampler.set_sampler_value("mock_missing_bin_count", 2)
        sampler.set_sampler_value("mock_missing_bin_start", 1.5)
        sampler.set_sampler_value("mock_missing_bin_count", -1)
        self.assertEqual(sampler.get_sampler_value("mock_missing_bin_start").mock_missing_bin_start, 7)
        self.assertEqual(sampler.get_sampler_value("mock_missing_bin_count").mock_missing_bin_count, 2)

    def test_legacy_two_option_measure_protocol_defaults_max_polling(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.communicate("to_measure 1 0.1 False 0.1 100")
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.acquisition_mode, "instant_ai")

    def test_simple_measure(self):
        sampler.set_sampler(sampler_name="mock_sampler")

        mock_v0, mock_v_inf = 2.5, 5.0  # V
        sampler.set_sampler_value("mock_v0", mock_v0)
        sampler.set_sampler_value("mock_v_inf", mock_v_inf)

        mock_tau = 100  # us
        sampler.set_sampler_value("mock_tau", mock_tau)

        sampler.measure(number_of_waveforms=2, emitting_frequency=200)

        while sampler.is_measuring:
            time.sleep(0.1)

        result = sampler.query()
        self.assertEqual(result.success, True)

        self.assertTrue(0 < len(result.wave) <= 1000)
        # self.assertTrue(2.3 < result.wave[0] < 2.7)

    def test_instant_ai_mock_uses_requested_waveform_count(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0.49)
        sampler.measure(
            number_of_waveforms=3,
            emitting_frequency=0.05,
            instant_ai_frequency_threshold=0.1,
            instant_ai_target_points_per_waveform=100,
            instant_ai_max_reliable_polling_hz=10,
        )
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.acquisition_mode, "instant_ai")
        self.assertEqual(result.instant_ai_complete_waveforms, 3)
        self.assertAlmostEqual(result.instant_ai_planned_duration_seconds, 80.0)
        self.assertAlmostEqual(result.instant_ai_actual_duration_seconds, 80.0)
        self.assertEqual(result.instant_ai_late_reads, 0)
        self.assertEqual(result.instant_ai_interpolated_bins, 0)
        self.assertEqual(len(result.wave), 50)
        self.assertAlmostEqual(result.wave_interval, 200000.0)

    def test_instant_ai_continuous_mock_handles_waveform_counts_and_phase_offsets(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 0)
        for waveform_count, phase_offset in ((1, 0.0), (3, 0.23)):
            with self.subTest(waveform_count=waveform_count, phase_offset=phase_offset):
                sampler.set_sampler_value("mock_phase_offset", phase_offset)
                sampler.measure(waveform_count, 0.05, instant_ai_frequency_threshold=0.1)
                while sampler.is_measuring:
                    time.sleep(0.01)
                result = sampler.query()
                self.assertTrue(result.success, result.message)
                self.assertEqual(result.instant_ai_complete_waveforms, waveform_count)
                self.assertAlmostEqual(
                    result.instant_ai_planned_duration_seconds, (waveform_count + 1) / 0.05
                )
                self.assertEqual(len(result.wave), 50)
                self.assertAlmostEqual(result.wave_interval, 200000.0)

    def test_instant_ai_continuous_mock_interpolates_at_most_two_missing_bins(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0.49)
        sampler.set_sampler_value("mock_missing_bin_start", 20)
        sampler.set_sampler_value("mock_missing_bin_count", 2)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertGreaterEqual(result.instant_ai_interpolated_bins, 2)

    def test_instant_ai_continuous_mock_reports_stable_coverage_error(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0.49)
        sampler.set_sampler_value("mock_missing_bin_start", 20)
        sampler.set_sampler_value("mock_missing_bin_count", 3)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertFalse(result.success)
        self.assertEqual(result.error_category, "coverage")
        self.assertFalse(result.retryable)
        self.assertFalse(result.cancelled)
        self.assertEqual(result.instant_ai_complete_waveforms, 0)
        self.assertEqual(result.wave_interval, 0.0)
        self.assertEqual(result.wave, [])

    def test_instant_ai_continuous_mock_preserves_terminal_edge_when_bin_zero_is_missing(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 2)
        try:
            sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            result = sampler.query()
            self.assertTrue(result.success, result.message)
            self.assertGreaterEqual(result.instant_ai_interpolated_bins, 2)
        finally:
            sampler.set_sampler_value("mock_missing_bin_start", 0)
            sampler.set_sampler_value("mock_missing_bin_count", 0)

    def test_instant_ai_continuous_mock_wraparound_missing_run_reaches_coverage_error(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", -2)
        sampler.set_sampler_value("mock_missing_bin_count", 3)
        try:
            sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            result = sampler.query()
            self.assertFalse(result.success)
            self.assertEqual(result.error_category, "coverage")
            self.assertFalse(result.retryable)
        finally:
            sampler.set_sampler_value("mock_missing_bin_start", 0)
            sampler.set_sampler_value("mock_missing_bin_count", 0)

    def test_legacy_instant_ai_v1_dump_can_be_replayed(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "instant.csv"
            rows = [
                "#HALF_SAMPLE_INSTANT_AI_V1",
                "emitting_frequency=0.1",
                "target_points=100",
                "number_of_waveforms=3",
                "waveform_index,planned_seconds,actual_seconds,voltage",
            ]
            for waveform in range(3):
                for phase_bin in range(100):
                    seconds = (phase_bin + 0.5) / 10.0
                    phase = (phase_bin + 0.5) / 100.0
                    voltage = 5.0 + (2.5 - 5.0) * math.exp(phase / 0.1 * 1e6 / -100.0) if phase < 0.5 else 0.0
                    rows.append(f"{waveform},{seconds},{seconds},{voltage}")
            dump_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success)

    def test_instant_ai_v2_dump_is_self_contained_and_replays_after_config_changes(self):
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "continuous-v2.csv"
            sampler.dump(str(dump_path), 3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            captured = sampler.query()
            self.assertTrue(captured.success, captured.message)

            lines = dump_path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], "#HALF_SAMPLE_INSTANT_AI_V2")
            self.assertEqual(lines[1:8], [
                "emitting_frequency=0.05",
                "target_points=100",
                "number_of_waveforms=3",
                "max_reliable_polling_hz=10",
                "planned_duration_seconds=80",
                "actual_duration_seconds=80",
                "planned_seconds,actual_seconds,voltage,read_success,read_error_code",
            ])
            self.assertEqual(len(lines), 8 + 401)
            self.assertEqual(list(dump_path.parent.glob(dump_path.name + ".half-sample-tmp.*")), [])

            sampler.communicate("to_config 1 20 False")
            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success, replay.message)
            self.assertEqual(replay.acquisition_mode, "instant_ai")
            self.assertEqual(replay.instant_ai_complete_waveforms, 3)
            self.assertAlmostEqual(replay.instant_ai_planned_duration_seconds, 80.0)
            self.assertAlmostEqual(replay.instant_ai_actual_duration_seconds, 80.0)
            self.assertEqual(replay.instant_ai_late_reads, 0)

    @unittest.skipUnless(sys.platform == "win32", "Windows UTF-16 filesystem path contract")
    def test_windows_unicode_path_dump_and_process_are_atomic(self):
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            unicode_directory = Path(directory) / "采集数据"
            unicode_directory.mkdir()
            dump_path = unicode_directory / "测量.csv"
            sampler.dump(str(dump_path), 3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            captured = sampler.query()
            self.assertTrue(captured.success, captured.message)
            self.assertTrue(dump_path.is_file())
            self.assertEqual(list(unicode_directory.glob("*.half-sample-tmp.*")), [])

            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success, replay.message)
            self.assertEqual(replay.instant_ai_complete_waveforms, 3)
            self.assertEqual(list(unicode_directory.glob("*.half-sample-tmp.*")), [])

    def test_failed_process_clears_previous_wave_and_keeps_stable_error(self):
        sampler.set_sampler_value("mock_noise", 0)
        sampler.measure(1, 0.05, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        self.assertTrue(sampler.query().success)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            malformed = Path(directory) / "malformed-v2.csv"
            malformed.write_text("#HALF_SAMPLE_INSTANT_AI_V2\nemitting_frequency=bad\n", encoding="utf-8")
            sampler.process(str(malformed))
            failed = sampler.query()
            self.assertFalse(failed.success)
            self.assertEqual(failed.message, "invalid_instant_ai_config")
            self.assertEqual(failed.wave, [])
            self.assertEqual(failed.wave_interval, 0.0)

    def test_unmarked_buffered_dump_remains_replayable(self):
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "buffered.csv"
            sampler.dump(str(dump_path), 3, 100000.0)
            while sampler.is_measuring:
                time.sleep(0.01)
            captured = sampler.query()
            self.assertTrue(captured.success, captured.message)
            self.assertFalse(dump_path.read_text(encoding="utf-8").startswith("#HALF_SAMPLE"))

            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success, replay.message)
            self.assertEqual(replay.acquisition_mode, "buffered_ai")

    def test_empty_legacy_instant_ai_v1_returns_stable_failure(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "empty-instant.csv"
            dump_path.write_text(
                "#HALF_SAMPLE_INSTANT_AI_V1\n"
                "emitting_frequency=0.1\n"
                "target_points=100\n"
                "number_of_waveforms=3\n"
                "waveform_index,planned_seconds,actual_seconds,voltage\n",
                encoding="utf-8",
            )
            sampler.process(str(dump_path))
            result = sampler.query()
            self.assertFalse(result.success)
            self.assertEqual(result.error_category, "coverage")
            self.assertFalse(result.retryable)

    def test_missing_process_file_has_stable_contract(self):
        missing = Path(__file__).resolve().parents[1] / "cpp_build" / "does-not-exist.csv"
        sampler.process(str(missing))
        result = sampler.query()
        self.assertFalse(result.success)
        self.assertEqual(result.message, "file_not_found")
        self.assertEqual(result.error_category, "file")
        self.assertFalse(result.retryable)


if __name__ == '__main__':
    unittest.main()
