import ast
from pathlib import Path
import unittest

from .subprocess_compat import daq_demo_unavailable


ROOT = Path(__file__).resolve().parents[2]


class Python36CompatibilityTest(unittest.TestCase):
    def test_missing_daq_runtime_marks_demo_as_unavailable(self):
        self.assertTrue(daq_demo_unavailable("code=RUNTIME_NOT_FOUND stage=runtime_load"))
        self.assertTrue(daq_demo_unavailable('{"code":"RUNTIME_NOT_FOUND"}'))
        self.assertTrue(daq_demo_unavailable("DEVICE_NOT_FOUND"))
        self.assertFalse(daq_demo_unavailable("LAYOUT_VERIFIED_BY_DEMO"))

    def test_subprocess_run_uses_python36_compatible_keywords(self):
        violations = []
        for source_root in (ROOT / "tests", ROOT / "scripts"):
            for path in source_root.rglob("*.py"):
                tree = ast.parse(path.read_text(encoding="utf-8"), filename=str(path))
                for node in ast.walk(tree):
                    if not isinstance(node, ast.Call):
                        continue
                    function = node.func
                    if not (isinstance(function, ast.Attribute) and function.attr == "run"):
                        continue
                    forbidden = sorted(
                        keyword.arg for keyword in node.keywords
                        if keyword.arg in ("capture_output", "text")
                    )
                    if forbidden:
                        violations.append("{}:{}".format(path.relative_to(ROOT), ",".join(forbidden)))
        self.assertFalse(violations, "\n".join(violations))


if __name__ == "__main__":
    unittest.main()
