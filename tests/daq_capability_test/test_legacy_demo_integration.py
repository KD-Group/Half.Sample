import subprocess
import json
import tempfile
import unittest
from pathlib import Path

from .subprocess_compat import daq_demo_unavailable, run_captured


ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "cpp_build/legacy_adapter_smoke.exe"


class LegacyDemoIntegrationTest(unittest.TestCase):
    def test_normative_range_runs_single_channel_case_and_rejects_ambiguous_range(self):
        if not EXE.is_file():
            self.skipTest("Legacy executable has not been built")
        cli = ROOT / "cpp_build/daq_capability_test_legacy.exe"
        if not cli.is_file():
            self.skipTest("Legacy CLI has not been built")
        matrix_lines = (
            ROOT / "src/daq_capability_test/default_test_matrix.tsv"
        ).read_text(encoding="utf-8").splitlines()
        header_index = next(i for i, line in enumerate(matrix_lines) if line.startswith("enabled\t"))
        rows = matrix_lines[:header_index + 1]
        for source in matrix_lines[header_index + 1:header_index + 4]:
            fields = source.split("\t")
            fields[1] = "DemoDevice,BID#0"
            if fields[28] == "single_channel_boundary":
                fields[6:10] = ["100000", "1024", "1", "5"]
            rows.append("\t".join(fields))
        with tempfile.TemporaryDirectory(dir=ROOT / "cpp_build") as directory:
            matrix = Path(directory) / "demo.tsv"
            matrix.write_text("\n".join(rows) + "\n", encoding="utf-8")
            completed = run_captured([str(cli), "suite", "--config", str(matrix), "--all",
                                      "--output-dir", directory], cwd=ROOT, timeout=30)
            if daq_demo_unavailable(completed.stdout + completed.stderr):
                self.skipTest("DAQ runtime or DemoDevice is unavailable")
            self.assertEqual(0, completed.returncode, completed.stdout + completed.stderr)
            result = json.loads(completed.stdout.splitlines()[-1])
            case = result["cases"]["single_channel_boundary"]
            self.assertEqual("PASS", case["result"])
            self.assertEqual("1", case["evidence"]["passed_repetitions"])
            rejected = run_captured(
                [str(cli), "acquire", "--device", "DemoDevice,BID#0", "--channels", "0", "--range", "5V",
                 "--rate", "100000", "--points", "1024", "--repeat", "1", "--timeout", "5",
                 "--output-dir", directory], cwd=ROOT, timeout=30)
            self.assertNotEqual(0, rejected.returncode)
            self.assertEqual("VALUE_RANGE_UNSUPPORTED", json.loads(rejected.stdout.splitlines()[-1])["code"])

    def test_demo_layout_oracle_is_repeatable_and_restores_ranges(self):
        if not EXE.is_file():
            self.skipTest("legacy executable has not been built")
        for repetition in range(2):
            completed = run_captured(
                [str(EXE), "DemoDevice,BID#0", "validate-layout"],
                cwd=ROOT, timeout=30,
            )
            output = completed.stdout + completed.stderr
            if daq_demo_unavailable(output):
                self.skipTest("DAQ runtime or DemoDevice is unavailable")
            self.assertEqual(0, completed.returncode, f"repetition {repetition + 1}: {output}")
            self.assertIn("LAYOUT_VERIFIED_BY_DEMO", output)
            self.assertIn("layout=scan_major_interleaved", output)
            self.assertIn("original_ranges=", output)
            self.assertIn("restored_ranges=", output)
            self.assertIn("BioSimulator=", output)


if __name__ == "__main__":
    unittest.main()
