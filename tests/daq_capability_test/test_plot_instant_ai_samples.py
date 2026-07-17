import importlib.util
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from .subprocess_compat import run_captured


ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "plot_instant_ai_samples.py"


class PlotInstantAiSamplesTest(unittest.TestCase):
    def run_script(self, content=None, extra_arguments=(), missing=False):
        with tempfile.TemporaryDirectory(prefix="instant_ai_plot_") as directory:
            path = Path(directory) / "instant_ai_polling.tsv"
            output = Path(directory) / "result.png"
            if not missing:
                path.write_text(content or "", encoding="utf-8")
            environment = os.environ.copy()
            environment["MPLBACKEND"] = "Agg"
            completed = run_captured(
                [
                    sys.executable,
                    str(SCRIPT),
                    str(path),
                    "--output",
                    str(output),
                    *extra_arguments,
                ],
                cwd=ROOT,
                encoding="utf-8",
                check=False,
                env=environment,
            )
            png = output.read_bytes() if output.exists() else b""
            return completed, png

    def test_uniform_indices_share_the_full_time_range_and_keep_endpoints(self):
        specification = importlib.util.spec_from_file_location("instant_plot", SCRIPT)
        module = importlib.util.module_from_spec(specification)
        specification.loader.exec_module(module)
        self.assertEqual(module.uniform_sample_indices(10, 4), [0, 3, 6, 9])
        self.assertEqual(module.uniform_sample_indices(4, 100000), [0, 1, 2, 3])
        self.assertEqual(module.uniform_sample_indices(4, 0), [0, 1, 2, 3])
        self.assertEqual(module.uniform_sample_indices(1, 1), [0])

    def test_generates_png_for_multiple_channels(self):
        completed, png = self.run_script(
            "read_index\telapsed_seconds\tcall_duration_us\tinterval_us\tchannel_0\tchannel_1\n"
            "0\t0.001\t80\t0\t1.25\t2.5\n"
            "1\t0.0012\t70\t200\t1.5\t2.75\n",
            ("--max-points", "2"),
        )
        self.assertEqual(completed.returncode, 0, completed.stderr)
        self.assertTrue(png.startswith(b"\x89PNG\r\n\x1a\n"))
        self.assertIn("total_points=2", completed.stdout)
        self.assertIn("plotted_points=2", completed.stdout)

    def test_rejects_missing_or_invalid_input(self):
        cases = {
            "missing": (None, (), True),
            "empty": ("", (), False),
            "missing_fixed": (
                "read_index\telapsed_seconds\tinterval_us\tchannel_0\n"
                "0\t0.1\t0\t1\n",
                (),
                False,
            ),
            "no_channel": (
                "read_index\telapsed_seconds\tcall_duration_us\tinterval_us\n"
                "0\t0.1\t1\t0\n",
                (),
                False,
            ),
            "non_numeric": (
                "read_index\telapsed_seconds\tcall_duration_us\tinterval_us\tchannel_0\n"
                "0\tbad\t1\t0\t1\n",
                (),
                False,
            ),
            "non_finite": (
                "read_index\telapsed_seconds\tcall_duration_us\tinterval_us\tchannel_0\n"
                "0\tnan\t1\t0\t1\n",
                (),
                False,
            ),
            "invalid_max": (
                "read_index\telapsed_seconds\tcall_duration_us\tinterval_us\tchannel_0\n"
                "0\t0.1\t1\t0\t1\n",
                ("--max-points", "-1"),
                False,
            ),
        }
        for name, (content, arguments, missing) in cases.items():
            with self.subTest(name=name):
                completed, png = self.run_script(content, arguments, missing)
                self.assertNotEqual(completed.returncode, 0)
                self.assertFalse(png)


if __name__ == "__main__":
    unittest.main()
