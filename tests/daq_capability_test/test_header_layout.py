import unittest
import ast
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]


def assigned_list(source, name):
    tree = ast.parse(source)
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == name for t in node.targets):
            return ast.literal_eval(node.value)
    raise AssertionError(f"missing list assignment: {name}")


def vendor_source_violations(source):
    tree = ast.parse(source)
    lists = {}
    for node in tree.body:
        if isinstance(node, ast.Assign) and len(node.targets) == 1 and isinstance(node.targets[0], ast.Name):
            try:
                value = ast.literal_eval(node.value)
            except (ValueError, TypeError):
                continue
            if isinstance(value, list):
                lists[node.targets[0].id] = value
    violations = []
    for name, values in lists.items():
        for vendor in ("legacy", "xnavi"):
            production = f"src/daq_capability_test/{vendor}_adapter.cpp"
            if any(str(value).replace("\\", "/") == production for value in values) and name != f"{vendor}_sources":
                violations.append((name, vendor))
    return violations


def compact_cpp(source):
    return "".join(source.split())


class HeaderLayoutTest(unittest.TestCase):
    def test_standalone_entry_initializes_windows_console_for_utf8_bytes(self):
        source = (REPO_ROOT / "src/daq_capability_test/main.cpp").read_text(encoding="utf-8")

        self.assertIn("#ifdef _WIN32", source)
        self.assertIn("SetConsoleOutputCP(CP_UTF8)", source)
        self.assertNotIn("_O_U8TEXT", source)

    def test_sconstruct_declares_explicit_isolated_target_source_lists(self):
        source = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")
        lists = {
            name: assigned_list(source, name)
            for name in ("sample_sources", "daq_common_sources", "legacy_sources", "xnavi_sources",
                         "mock_sources", "unit_test_sources")
        }

        self.assertNotIn("Glob(", source)
        self.assertIn("src/daq_capability_test/legacy_adapter_factory.cpp", lists["legacy_sources"])
        self.assertIn("src/daq_capability_test/xnavi_adapter_factory.cpp", lists["xnavi_sources"])
        self.assertIn("src/daq_capability_test/mock_adapter_factory.cpp", lists["mock_sources"])
        self.assertNotIn("src/daq_capability_test/legacy_adapter.cpp", lists["xnavi_sources"])
        self.assertNotIn("src/daq_capability_test/xnavi_adapter.cpp", lists["legacy_sources"])
        for name in ("mock_sources", "unit_test_sources"):
            self.assertFalse(any(item.startswith("src/daq_capability_test/legacy_adapter") or
                                 item.startswith("src/daq_capability_test/xnavi_adapter")
                                 for item in lists[name]))
            self.assertFalse(any("daq_headers/" in item for item in lists[name]))

    def test_hardware_programs_use_common_cli_and_dedicated_factories(self):
        source = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")
        for variant in ("legacy", "xnavi"):
            marker = "cpp_build/daq_capability_test_{}.exe".format(variant)
            line = next(line for line in source.splitlines() if marker in line and ".Program(" in line)
            self.assertIn("daq_common_objects", line)
            self.assertIn("{}_objects".format(variant), line)
            self.assertNotIn("smoke", line)

    def test_versioned_daqnavi_headers_exist(self):
        self.assertTrue((REPO_ROOT / "src/daq_headers/legacy/bdaqctrl.h").is_file())
        self.assertTrue((REPO_ROOT / "src/daq_headers/xnavi/bdaqctrl.h").is_file())

    def test_unversioned_legacy_header_path_does_not_exist(self):
        self.assertFalse((REPO_ROOT / "src/sampler/bdaqctrl.h").exists())

    def test_cmake_references_versioned_legacy_header(self):
        cmake = (REPO_ROOT / "src/CMakeLists.txt").read_text(encoding="utf-8")

        self.assertIn("daq_headers/legacy/bdaqctrl.h", cmake)
        self.assertNotIn("sampler/bdaqctrl.h", cmake)

    def test_real_sampler_includes_legacy_header_by_versioned_path(self):
        real_sampler = (REPO_ROOT / "src/sampler/real_sampler.hpp").read_text(encoding="utf-8")

        self.assertIn('#include "../daq_headers/legacy/bdaqctrl.h"', real_sampler)
        self.assertNotIn('#include "bdaqctrl.h"', real_sampler)

    def test_source_files_do_not_include_unversioned_daqnavi_header(self):
        vendor_headers = {
            REPO_ROOT / "src/daq_headers/legacy/bdaqctrl.h",
            REPO_ROOT / "src/daq_headers/xnavi/bdaqctrl.h",
        }
        source_suffixes = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp"}
        offenders = []

        for source_file in (REPO_ROOT / "src").rglob("*"):
            if source_file.is_file() and source_file.suffix in source_suffixes and source_file not in vendor_headers:
                if '#include "bdaqctrl.h"' in source_file.read_text(encoding="utf-8"):
                    offenders.append(str(source_file.relative_to(REPO_ROOT)))

        self.assertEqual([], offenders)

    def test_no_translation_unit_includes_both_vendor_headers(self):
        offenders = []
        for source_file in (REPO_ROOT / "src").rglob("*.cpp"):
            text = source_file.read_text(encoding="utf-8")
            if "daq_headers/legacy/bdaqctrl.h" in text and "daq_headers/xnavi/bdaqctrl.h" in text:
                offenders.append(str(source_file.relative_to(REPO_ROOT)))
        self.assertEqual([], offenders)

    def test_legacy_adapter_is_vendor_isolated(self):
        implementation = REPO_ROOT / "src/daq_capability_test/legacy_adapter.cpp"
        public_header = REPO_ROOT / "src/daq_capability_test/legacy_adapter.hpp"
        self.assertTrue(implementation.is_file())
        self.assertTrue(public_header.is_file())
        source = implementation.read_text(encoding="utf-8")
        self.assertIn('#include "../daq_headers/legacy/bdaqctrl.h"', source)
        self.assertNotIn("daq_headers/xnavi", source)
        self.assertNotIn("bdaqctrl", public_header.read_text(encoding="utf-8"))

    def test_public_daq_headers_do_not_expose_vendor_types(self):
        forbidden = ("Automation::BDaq", "BufferedAiCtrl", "AiFeatures", "ScanChannel", "ConvertClock")
        for header in (REPO_ROOT / "src/daq_capability_test").glob("*.hpp"):
            text = header.read_text(encoding="utf-8")
            for token in forbidden:
                self.assertNotIn(token, text, f"{token} leaked through {header.name}")

    def test_sconstruct_keeps_legacy_source_out_of_common_targets(self):
        sconstruct = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")
        self.assertIn("legacy_adapter.cpp", sconstruct)
        self.assertIn("legacy_adapter_smoke", sconstruct)
        self.assertIn("cpp_build/daq_capability_test_legacy.exe", sconstruct)
        self.assertEqual([
            'src/daq_capability_test/legacy_adapter.cpp',
            'src/daq_capability_test/legacy_adapter_factory.cpp',
        ], assigned_list(sconstruct, "legacy_sources"))

    def test_xnavi_adapter_is_vendor_isolated(self):
        implementation = REPO_ROOT / "src/daq_capability_test/xnavi_adapter.cpp"
        public_header = REPO_ROOT / "src/daq_capability_test/xnavi_adapter.hpp"
        self.assertTrue(implementation.is_file())
        self.assertTrue(public_header.is_file())
        source = implementation.read_text(encoding="utf-8")
        self.assertIn('#include "../daq_headers/xnavi/bdaqctrl.h"', source)
        self.assertNotIn("daq_headers/legacy", source)
        self.assertNotIn("legacy_adapter", source)
        self.assertNotIn("bdaqctrl", public_header.read_text(encoding="utf-8"))

    def test_sconstruct_has_isolated_xnavi_target(self):
        source = (REPO_ROOT / "SConstruct").read_text(encoding="utf-8")
        self.assertEqual([
            'src/daq_capability_test/xnavi_adapter.cpp',
            'src/daq_capability_test/xnavi_adapter_factory.cpp',
        ], assigned_list(source, "xnavi_sources"))
        self.assertIn("cpp_build/daq_capability_test_xnavi.exe", source)
        xnavi_program = source[source.index("daq_env.Program('cpp_build/daq_capability_test_xnavi.exe"):]
        self.assertIn("xnavi_objects", xnavi_program.splitlines()[0])
        self.assertNotIn("legacy_objects", xnavi_program.splitlines()[0])
        self.assertEqual([], vendor_source_violations(source))

    def test_vendor_source_list_validator_rejects_common_or_opposite_lists(self):
        bad = "common_sources = ['src/daq_capability_test/legacy_adapter.cpp']\n" \
              "legacy_sources = ['src/daq_capability_test/xnavi_adapter.cpp']\n"
        self.assertEqual([("common_sources", "legacy"), ("legacy_sources", "xnavi")],
                         vendor_source_violations(bad))

    def test_xnavi_runtime_uses_function_table_and_array_raii(self):
        source = (REPO_ROOT / "src/daq_capability_test/xnavi_adapter.cpp").read_text(encoding="utf-8")
        self.assertIn("AdxDaqNaviLibInitialize", source)
        self.assertIn("ArrayOwner", source)
        self.assertIn("ArrayOwner(const ArrayOwner&) = delete", source)
        self.assertIn("runtime_api=xnavi_function_table", source)
        self.assertNotIn("AdxBufferedAiCtrlCreate\"", source)

    def test_xnavi_event_context_outlives_dispatched_callbacks(self):
        source = compact_cpp(
            (REPO_ROOT / "src/daq_capability_test/xnavi_adapter.cpp").read_text(encoding="utf-8")
        )
        self.assertIn("process_lifetime_event_context", source)
        self.assertIn("EventContext*events", source)
        self.assertIn("EVENT_CONTEXT_CAPACITY", source)
        self.assertIn("newEventRegistry", source)
        self.assertNotIn("vector<std::unique_ptr<EventContext", source)
        self.assertNotIn("while(events.inflight.load())", source)
        self.assertNotIn("&events", source)
        create = source.index("controller=BufferedAiCtrl::Create()")
        allocate = source.index("events=process_lifetime_event_context()", create)
        self.assertLess(create, allocate)
        self.assertIn("compare_exchange_weak", source)
        self.assertNotIn("registry->next.fetch_add", source)

    def test_configure_readback_failure_invalidates_both_adapters(self):
        for name in ("legacy_adapter.cpp", "xnavi_adapter.cpp"):
            source = compact_cpp(
                (REPO_ROOT / "src/daq_capability_test" / name).read_text(encoding="utf-8")
            )
            self.assertIn("if(!validated.success){impl_->stop();returnvalidated;}", source)

    def test_missing_trigger_interface_is_unsupported_in_both_adapters(self):
        for name in ("legacy_adapter.cpp", "xnavi_adapter.cpp"):
            source = compact_cpp(
                (REPO_ROOT / "src/daq_capability_test" / name).read_text(encoding="utf-8")
            )
            interface_path = source.split(
                "Trigger*trigger=impl_->controller->getTrigger();", 1
            )[1].split("CollectionOwner", 1)[0]
            if name == "xnavi_adapter.cpp":
                interface_path = interface_path.split("ArrayOwner", 1)[0]
            self.assertIn("r.unsupported=true", interface_path, name)
            invalid_value_path = source.split('"INVALID_TRIGGER_VALUE"', 1)[1].split("SignalDropsource", 1)[0]
            self.assertNotIn("unsupported=true", invalid_value_path, name)

    def test_legacy_runtime_and_collections_are_raii_managed(self):
        source = (REPO_ROOT / "src/daq_capability_test/legacy_adapter.cpp").read_text(encoding="utf-8")
        self.assertNotIn('"BDAQ_CSCL_ANSI_C_APIs"', source)
        self.assertIn("CollectionOwner", source)
        self.assertNotIn("HMODULE module;", source)
        self.assertIn("FreeLibrary", source)
        self.assertIn("TIMEOUT", source)
        self.assertIn("MB_ERR_INVALID_CHARS", source)
        self.assertIn("WC_ERR_INVALID_CHARS", source)
        self.assertNotIn("value.begin(), value.end()", source)
        self.assertNotIn("output.pop_back()", source)

    def test_timeout_smoke_returns_driver_failure_exit_code(self):
        smoke = compact_cpp(
            (REPO_ROOT / "tests/daq_capability_test/legacy_adapter_smoke.cpp").read_text(encoding="utf-8")
        )
        self.assertIn('timed.code!="TIMEOUT"||!timed.value.timed_out', smoke)
        self.assertIn('mode=="timeout-recover"', smoke)

    def test_layout_smoke_uses_single_acquisition_oracle(self):
        smoke = (REPO_ROOT / "tests/daq_capability_test/legacy_adapter_smoke.cpp").read_text(encoding="utf-8")
        self.assertIn("verify_demo_interleaving", smoke)
        self.assertNotIn("single_ch0", smoke)
        self.assertNotIn("single_ch1", smoke)


if __name__ == "__main__":
    unittest.main()
