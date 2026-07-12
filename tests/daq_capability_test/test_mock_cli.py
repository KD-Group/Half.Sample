import json
import csv
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "cpp_build" / "daq_capability_test_mock.exe"
SUCCESS = ROOT / "src" / "daq_capability_test" / "mock_success.tsv"
FAILURES = ROOT / "src" / "daq_capability_test" / "mock_failures.tsv"


class MockCliTest(unittest.TestCase):
    def run_validation_entrypoint(self, *arguments):
        script = ROOT / "scripts" / "daq_validation.ps1"
        return subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", str(script), *arguments],
            cwd=ROOT, text=True, encoding="utf-8", capture_output=True, check=False)

    def test_validation_script_direct_entrypoint_runs_mock_suite(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            completed = self.run_validation_entrypoint(
                "-Variant", "mock", "-Config", str(SUCCESS), "-OutputDir", directory, "-All")

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("[DAQ][RUN]", completed.stdout)
        self.assertIn("[DAQ][PASS] result=PASS code=SUITE_PASSED", completed.stdout)
        self.assertNotIn("已加载 Invoke-DaqValidation", completed.stdout)

    def test_validation_script_creates_a_missing_nested_output_directory(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as parent:
            directory = Path(parent) / "missing-level-1" / "missing-level-2"
            self.assertFalse(directory.exists())
            completed = self.run_validation_entrypoint(
                "-Variant", "mock", "-Config", str(SUCCESS), "-OutputDir", str(directory), "-All")
            self.assertTrue(directory.is_dir())

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("[DAQ][PASS] result=PASS code=SUITE_PASSED", completed.stdout)

    def test_validation_script_direct_entrypoint_forwards_case_and_from(self):
        for scope, value in (("-Case", "preflight"), ("-From", "preflight")):
            with self.subTest(scope=scope), tempfile.TemporaryDirectory(
                    prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
                completed = self.run_validation_entrypoint(
                    "-Variant", "mock", "-Config", str(SUCCESS), "-OutputDir", directory, scope, value)
                self.assertEqual(completed.returncode, 0, completed.stderr)
                self.assertIn(scope.lower().lstrip("-") + " " + value, completed.stdout.lower())

    def test_validation_script_direct_entrypoint_rejects_multiple_scopes(self):
        completed = self.run_validation_entrypoint(
            "-Variant", "mock", "-Config", str(SUCCESS), "-All", "-Case", "preflight")
        self.assertNotEqual(completed.returncode, 0)
        self.assertTrue("恰好指定一个" in completed.stderr + completed.stdout or
                        "parameter set" in (completed.stderr + completed.stdout).lower())

    def test_validation_script_direct_entrypoint_without_parameters_shows_help(self):
        completed = self.run_validation_entrypoint()
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("-Variant mock|legacy|xnavi", completed.stdout)
        self.assertIn("-All", completed.stdout)
        self.assertIn("示例", completed.stdout)

    def test_validation_script_direct_entrypoint_quiet_returns_only_payload(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            completed = self.run_validation_entrypoint(
                "-Variant", "mock", "-Config", str(SUCCESS), "-OutputDir", directory, "-All", "-Quiet")
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertNotIn("[DAQ][RUN]", completed.stdout)
        self.assertNotIn("[DAQ][PASS]", completed.stdout)
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
        self.assertEqual(payload["code"], "SUITE_PASSED")

    def run_powershell_validation(self, invocation):
        script = ROOT / "scripts" / "daq_validation.ps1"
        command = "$ErrorActionPreference='Stop'; . '{}'; {}; 'SCRIPT_ASSERTION_PASSED'".format(script, invocation)
        return subprocess.run(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-Command", command],
            cwd=ROOT, text=True, encoding="utf-8", capture_output=True, check=False)

    def test_validation_script_is_the_single_function_source_and_keeps_stderr_separate(self):
        script = (ROOT / "scripts" / "daq_validation.ps1").read_text(encoding="utf-8")
        readme = (ROOT / "README.md").read_text(encoding="utf-8")

        self.assertIn("function Invoke-DaqValidation", script)
        self.assertIn("[CmdletBinding(DefaultParameterSetName = 'Help')]", script)
        self.assertNotIn("for ($index = 0; $index -lt $args.Count", script)
        self.assertTrue((ROOT / "scripts" / "daq_validation.ps1").read_bytes().startswith(b"\xef\xbb\xbf"))
        self.assertNotIn("2>&1", script)
        self.assertNotIn("function Invoke-DaqValidation", readme)
        self.assertIn(". .\\scripts\\daq_validation.ps1", readme)

    def test_validation_script_exports_function_and_accepts_mock_success(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            invocation = (
                "Invoke-DaqValidation -Executable '{}' -Arguments "
                "@('suite','--config','{}','--all','--output-dir','{}') "
                "-ExpectedCode 'SUITE_PASSED' -ExpectedSupportedStrategies "
                "@('LOW_SAMPLE_RATE','PHASE_STITCHING','TRIGGER_DELAY') -StrategyMatch Exact | Out-Null"
            ).format(EXE, SUCCESS, directory)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("SCRIPT_ASSERTION_PASSED", completed.stdout)
        self.assertNotIn("SCRIPT_ASSERTION_PASSED", completed.stderr)

    def test_validation_script_does_not_parse_stderr_as_json(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            helper = Path(directory) / "stdout_stderr.cmd"
            helper.write_text(
                '@echo stderr-marker 1>&2\n'
                '@echo {"result":"PASS","code":"OK","message":"ok","evidence":{}}\n'
                '@exit /b 0\n',
                encoding="ascii")
            invocation = (
                "Invoke-DaqValidation -Executable $env:ComSpec -Arguments @('/d','/c','{}') "
                "-ExpectedCode OK | Out-Null"
            ).format(helper)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("SCRIPT_ASSERTION_PASSED", completed.stdout)
        self.assertIn("stderr-marker", completed.stderr)
        self.assertNotIn("stderr-marker", completed.stdout)

    def test_validation_script_accepts_expected_non_stationary_failure(self):
        matrix = ROOT / "src" / "daq_capability_test" / "mock_non_stationary.tsv"
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            invocation = (
                "Invoke-DaqValidation -Executable '{}' -Arguments "
                "@('suite','--config','{}','--case','phase_stitch','--output-dir','{}') "
                "-ExpectedExit 2 -ExpectedResult FAIL -ExpectedCode 'SUITE_FAILED' "
                "-ExpectedFailedCase phase_stitch -ExpectedFailedCode 'NON_STATIONARY_RESPONSE' | Out-Null"
            ).format(EXE, matrix, directory)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("SCRIPT_ASSERTION_PASSED", completed.stdout)

    def test_validation_script_reports_load_and_success_progress_without_polluting_pipeline(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            helper = Path(directory) / "success.cmd"
            helper.write_text(
                '@echo {"result":"PASS","code":"OK","message":"completed","evidence":{"samples":8}}\n'
                '@exit /b 0\n', encoding="ascii")
            invocation = (
                "$result = @(Invoke-DaqValidation -Executable $env:ComSpec "
                "-Arguments @('/d','/c','{}') -ExpectedCode OK); "
                "Write-Output ('PIPELINE_COUNT=' + $result.Count); "
                "Write-Output ('PIPELINE_CODE=' + $result[0].code)"
            ).format(helper)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        for marker in ("[DAQ] 已加载 Invoke-DaqValidation", "[DAQ][RUN]", "[DAQ][EXIT] exit=0",
                       "[DAQ][PASS] result=PASS code=OK", "[DAQ][MESSAGE] completed",
                       '[DAQ][EVIDENCE] {"samples":8}', "PIPELINE_COUNT=1", "PIPELINE_CODE=OK"):
            self.assertIn(marker, completed.stdout)

    def test_validation_script_quiet_suppresses_host_progress_but_returns_payload(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            helper = Path(directory) / "quiet.cmd"
            helper.write_text(
                '@echo {"result":"PASS","code":"OK","message":"quiet","evidence":{}}\n'
                '@exit /b 0\n', encoding="ascii")
            invocation = (
                "$result = Invoke-DaqValidation -Executable $env:ComSpec "
                "-Arguments @('/d','/c','{}') -ExpectedCode OK -Quiet; "
                "Write-Output ('PIPELINE_CODE=' + $result.code)"
            ).format(helper)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertIn("[DAQ] 已加载 Invoke-DaqValidation", completed.stdout)
        self.assertIn("PIPELINE_CODE=OK", completed.stdout)
        self.assertNotIn("[DAQ][RUN]", completed.stdout)
        self.assertNotIn("[DAQ][PASS]", completed.stdout)

    def test_validation_script_reports_assertions_and_throws_complete_failure(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            helper = Path(directory) / "failure.cmd"
            helper.write_text(
                '@echo {"result":"FAIL","code":"ACTUAL","message":"bad data","evidence":{"reason":"noise"}}\n'
                '@exit /b 2\n', encoding="ascii")
            invocation = (
                "try {{ Invoke-DaqValidation -Executable $env:ComSpec -Arguments @('/d','/c','{}') "
                "-ExpectedExit 0 -ExpectedResult PASS -ExpectedCode EXPECTED | Out-Null; exit 9 }} "
                "catch {{ Write-Output ('CAUGHT=' + $_.Exception.Message) }}"
            ).format(helper)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        for marker in ("[DAQ][EXIT] exit=2", "[DAQ][FAIL] result=FAIL code=ACTUAL",
                       "[DAQ][MESSAGE] bad data", '[DAQ][EVIDENCE] {"reason":"noise"}',
                       "[DAQ][ASSERT] expected exit=0", "[DAQ][ASSERT] expected result=PASS",
                       "[DAQ][ASSERT] expected code=EXPECTED", "CAUGHT=exit=2 code=ACTUAL",
                       "message=bad data", 'evidence={"reason":"noise"}', "validation=expected exit=0"):
            self.assertIn(marker, completed.stdout)

    def test_validation_script_reports_json_parse_failure_with_complete_fields(self):
        with tempfile.TemporaryDirectory(prefix="daq_ps_", dir=ROOT / "cpp_build") as directory:
            helper = Path(directory) / "invalid_json.cmd"
            helper.write_text('@echo not-json\n@exit /b 6\n', encoding="ascii")
            invocation = (
                "try {{ Invoke-DaqValidation -Executable $env:ComSpec -Arguments @('/d','/c','{}') "
                "| Out-Null; exit 9 }} catch {{ Write-Output ('CAUGHT=' + $_.Exception.Message) }}"
            ).format(helper)
            completed = self.run_powershell_validation(invocation)

        self.assertEqual(completed.returncode, 0, completed.stderr)
        for marker in ("[DAQ][FAIL] result=FAIL code=JSON_PARSE_FAILED",
                       "[DAQ][ASSERT] 最后一个非空 stdout 行不是 JSON",
                       "CAUGHT=exit=6 code=JSON_PARSE_FAILED",
                       "message=最后一个非空 stdout 行不是 JSON", "evidence={}",
                       "validation=最后一个非空 stdout 行不是 JSON"):
            self.assertIn(marker, completed.stdout)

    def test_help_lists_supported_commands_and_suite_scopes(self):
        completed = subprocess.run([str(EXE), "--help"], cwd=ROOT, text=True, encoding="utf-8",
                                   capture_output=True, check=False)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        for text in ("capability", "acquire", "trigger", "phase-stitch capture",
                     "phase-stitch reconstruct", "suite", "--all", "--case", "--from"):
            self.assertIn(text, completed.stdout)
        for text in ("DAQ 能力验证工具", "命令：", "选项：", "显示此帮助并退出"):
            self.assertIn(text, completed.stdout)
        self.assertNotIn("Commands:", completed.stdout)
        self.assertEqual(completed.stderr, "")

    def test_mock_target_has_no_vendor_adapter_or_header_dependency(self):
        build = (ROOT / "SConstruct").read_text(encoding="utf-8")
        block = build.split("mock_sources = [", 1)[1].split("]", 1)[0]
        self.assertNotIn("legacy_adapter", block)
        self.assertNotIn("xnavi_adapter", block)
        self.assertNotIn("daq_headers", block)

    def run_suite(self, matrix, case=None):
        output = Path(tempfile.mkdtemp(prefix="daq_mock_", dir=ROOT / "cpp_build"))
        self.addCleanup(shutil.rmtree, output, True)
        command = [str(EXE), "suite", "--config", str(matrix), "--output-dir", str(output)]
        command += ["--case", case] if case else ["--all"]
        completed = subprocess.run(command, cwd=ROOT, text=True, capture_output=True, check=False)
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
        return completed, payload, output

    def failure_manifest(self):
        lines = [line for line in FAILURES.read_text(encoding="utf-8").splitlines()
                 if line and not line.startswith("#")]
        return list(csv.DictReader(lines, delimiter="\t"))

    def test_phase_faults_are_generated_as_data_not_adapter_error_codes(self):
        source = (ROOT / "src" / "daq_capability_test" / "fake_daq_adapter.cpp").read_text(encoding="utf-8")
        for code in ("EDGE_NOT_FOUND", "EDGE_JITTER_EXCEEDED", "MAX_ATTEMPTS_EXCEEDED",
                     "OVERLAP_MISMATCH", "NON_STATIONARY_RESPONSE", "WAVEFORM_BOUNDARY_DISCONTINUITY"):
            self.assertNotIn(code, source)

    def test_fake_uses_explicit_acquisition_role(self):
        source = (ROOT / "src" / "daq_capability_test" / "fake_daq_adapter.cpp").read_text(encoding="utf-8")
        self.assertIn('request.role=="calibration"', source)
        self.assertNotIn("points_per_channel>300", source)

    def test_failure_matrix_catalogs_every_scenario(self):
        catalog = {row["scenario"] for row in self.failure_manifest()}
        self.assertTrue({"short_read", "timeout", "overrun", "cache_overflow", "reference_missing",
                         "edge_missing", "edge_jitter", "insufficient_coverage", "overlap_mismatch",
                         "non_stationary", "boundary_jump", "trigger_timeout",
                         "delay_position_mismatch"}.issubset(catalog))

    def test_success_suite_supports_all_strategies_and_writes_artifacts(self):
        completed, payload, output = self.run_suite(SUCCESS)
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertEqual(payload["result"], "PASS")
        self.assertEqual(set(payload["supported_strategies"]),
                         {"LOW_SAMPLE_RATE", "PHASE_STITCHING", "TRIGGER_DELAY"})
        phase = payload["cases"]["phase_stitch"]["evidence"]
        reconstructed = int(phase["reconstructed_waveforms"])
        self.assertGreaterEqual(reconstructed, 2)
        used = set()
        for index in range(1, reconstructed + 1):
            segment_ids = set(phase[f"waveform_{index}_segment_ids"].split(","))
            self.assertGreaterEqual(len(segment_ids), 2)
            self.assertTrue(used.isdisjoint(segment_ids))
            used.update(segment_ids)
        run_dirs = list(output.iterdir())
        self.assertEqual(len(run_dirs), 1)
        self.assertTrue((run_dirs[0] / "capture.complete").exists())
        self.assertTrue((run_dirs[0] / "summary.tsv").exists())
        raw = run_dirs[0] / "raw"
        self.assertEqual(len(list(raw.glob("delay_trigger_calibration_*.tsv"))), 1)
        self.assertEqual(len(list(raw.glob("delay_trigger_probe_*.tsv"))), 8)
        self.assertEqual(len(list(raw.glob("delay_trigger_short_*.tsv"))), 8)
        summary_rows = list(csv.DictReader(
            (run_dirs[0] / "summary.tsv").read_text(encoding="utf-8").splitlines(),
            delimiter="\t",
        ))
        self.assertEqual(sum(row["test_name"] == "delay_trigger_probe" for row in summary_rows), 8)
        self.assertEqual(sum(row["test_name"] == "delay_trigger_short" for row in summary_rows), 8)

    def test_non_stationary_matrix_is_a_hard_failure(self):
        matrix = ROOT / "src" / "daq_capability_test" / "mock_non_stationary.tsv"
        completed, payload, _ = self.run_suite(matrix, "phase_stitch")
        self.assertEqual(completed.returncode, 2)
        self.assertEqual(payload["failed_cases"]["phase_stitch"]["code"], "NON_STATIONARY_RESPONSE")

    def test_each_mock_failure_preserves_its_exact_code(self):
        for row in self.failure_manifest():
            scenario, case, code = row["scenario"], row["case"], row["expected_code"]
            with self.subTest(scenario=scenario):
                matrix = ROOT / row["matrix_path"]
                self.assertTrue(matrix.is_file())
                completed, payload, _ = self.run_suite(matrix, case)
                expected_exit = int(row["expected_exit"])
                self.assertEqual(completed.returncode, expected_exit)
                json.loads(completed.stdout.strip().splitlines()[-1])
                self.assertEqual(payload["failed_cases"][case]["code"], code)
                run_directory = Path(payload["evidence"]["run_directory"])
                summary = ROOT / run_directory / "summary.tsv"
                self.assertTrue(summary.exists())
                if scenario == "edge_jitter":
                    rows = list(csv.DictReader(summary.read_text(encoding="utf-8").splitlines(), delimiter="\t"))
                    calibration = next(item for item in rows if item["test_name"] == "segment_phase" and
                                       item["repetition"] == "0")
                    self.assertEqual(calibration["requested_points_per_channel"], "151")
                self.assertFalse((ROOT / run_directory / "capture.complete").exists())


if __name__ == "__main__":
    unittest.main()
