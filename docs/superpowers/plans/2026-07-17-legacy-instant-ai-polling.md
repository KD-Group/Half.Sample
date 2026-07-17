# Legacy Instant AI Polling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a Legacy-only `instant-ai-polling` command that polls `InstantAiCtrl::ReadAny()` for 30 seconds by default, saves timestamped samples, reports interval statistics, and provides a Python offline sample plotter.

**Architecture:** Extend `DaqAdapter` with a default unsupported Instant AI operation so Mock and XNavi remain isolated, then override it only in `LegacyDaqAdapter`. Keep interval validation and percentile calculation in a hardware-independent module, let the existing CLI own result-directory lifecycle and JSON, and add a dedicated ResultWriter method for the timestamped TSV.

**Tech Stack:** C++14, Legacy BDaq header/runtime API, SCons/MSVC, Python 3 standard library `csv`, `unittest`.

---

## File structure

- Create `src/daq_capability_test/instant_ai_polling.hpp`: request/read/result/statistics types and public validation/statistics API.
- Create `src/daq_capability_test/instant_ai_polling.cpp`: hardware-independent interval statistics and PASS/FAIL evaluation.
- Modify `src/daq_capability_test/daq_adapter.hpp`: default unsupported `poll_instant_ai` virtual operation.
- Modify `src/daq_capability_test/legacy_adapter.hpp`: Legacy override declaration and Instant runtime-export helpers.
- Modify `src/daq_capability_test/legacy_adapter.cpp`: real `InstantAiCtrl` selection, range configuration, timed `ReadAny` loop, cleanup.
- Modify `src/daq_capability_test/cli.hpp` and `cli.cpp`: new command/options, validation, orchestration and JSON evidence.
- Modify `src/daq_capability_test/result_writer.hpp` and `.cpp`: atomic `raw/instant_ai_polling.tsv` writer.
- Create `scripts/plot_instant_ai_samples.py`: offline TSV validator and uniformly sampled PNG plotter.
- Create `tests/daq_capability_test/test_instant_ai_polling.cpp`: statistics, threshold, CLI and writer tests.
- Create `tests/daq_capability_test/test_plot_instant_ai_samples.py`: Python PNG/sampling/corrupt-input tests.
- Modify `tests/daq_capability_test/test_main.cpp`, `tests/daq_capability_test/test_legacy_adapter.cpp`, `SConstruct`, and `README.md`: register tests/build source and document field use.

### Task 1: Hardware-independent request, statistics and decision contract

**Files:**
- Create: `src/daq_capability_test/instant_ai_polling.hpp`
- Create: `src/daq_capability_test/instant_ai_polling.cpp`
- Create: `tests/daq_capability_test/test_instant_ai_polling.cpp`
- Modify: `tests/daq_capability_test/test_main.cpp`
- Modify: `SConstruct`

- [ ] **Step 1: Write failing tests for adjacent-interval statistics**

Add `test_instant_ai_polling()` to `test_main.cpp` and create tests that express the intended API:

```cpp
#include "daq_capability_test/instant_ai_polling.hpp"
#include <cassert>
#include <cmath>

void test_instant_ai_polling()
{
    using namespace daq_capability_test;
    InstantAiPollingData one;
    one.reads.push_back(InstantAiRead{0, 0.001, 80.0, 0.0, {1.0}});
    const InstantAiStatistics single = instant_ai_statistics(one);
    assert(single.successful_reads == 1);
    assert(single.mean_interval_us == 0.0);
    assert(single.p95_interval_us == 0.0);
    assert(single.p99_interval_us == 0.0);
    assert(single.max_interval_us == 0.0);

    InstantAiPollingData many;
    many.reads.push_back(InstantAiRead{0, 0.0001, 50.0, 0.0, {1.0}});
    many.reads.push_back(InstantAiRead{1, 0.0002, 40.0, 100.0, {2.0}});
    many.reads.push_back(InstantAiRead{2, 0.0005, 45.0, 300.0, {3.0}});
    many.wall_duration_seconds = 0.0005;
    const InstantAiStatistics stats = instant_ai_statistics(many);
    assert(stats.successful_reads == 3);
    assert(std::fabs(stats.reads_per_second - 6000.0) < 1e-9);
    assert(stats.mean_interval_us == 200.0);
    assert(stats.p95_interval_us == 300.0);
    assert(stats.p99_interval_us == 300.0);
    assert(stats.max_interval_us == 300.0);
    assert(stats.channel_min == std::vector<double>{1.0});
    assert(stats.channel_max == std::vector<double>{3.0});
    assert(stats.channel_span == std::vector<double>{2.0});
}
```

Use nearest-rank percentiles over intervals after the first read; the first synthetic `interval_us=0` is not part of the distribution.

- [ ] **Step 2: Run the unit build and verify RED**

Run:

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
```

Expected: compilation fails because `instant_ai_polling.hpp` and the declared types/functions do not exist.

- [ ] **Step 3: Add minimal types and statistics implementation**

Define:

```cpp
struct InstantAiPollingRequest {
    std::string device;
    std::vector<int> channels;
    std::string value_range;
    double duration_seconds = 30.0;
    bool max_gap_provided = false;
    double max_gap_ms = 0.0;
};

struct InstantAiRead {
    unsigned long long read_index = 0;
    double elapsed_seconds = 0.0;
    double call_duration_us = 0.0;
    double interval_us = 0.0;
    std::vector<double> values;
};

struct InstantAiPollingData {
    std::vector<int> channels;
    std::vector<InstantAiRead> reads;
    double wall_duration_seconds = 0.0;
    unsigned long long failed_reads = 0;
    std::string runtime_path;
    std::string runtime_version;
};

struct InstantAiStatistics {
    unsigned long long successful_reads = 0;
    unsigned long long failed_reads = 0;
    double reads_per_second = 0.0;
    double mean_interval_us = 0.0;
    double p95_interval_us = 0.0;
    double p99_interval_us = 0.0;
    double max_interval_us = 0.0;
    std::vector<double> channel_min, channel_max, channel_span;
};

InstantAiStatistics instant_ai_statistics(const InstantAiPollingData&);
CommandResult validate_instant_ai_polling(
    const InstantAiPollingRequest&, const InstantAiPollingData&, InstantAiStatistics&);
```

Validate at least one read, equal channel widths, finite timestamps/intervals/values, and optional max gap. Return `INSTANT_AI_POLLING_STABLE` on success and `INSTANT_AI_GAP_EXCEEDED` with `ExitCategory::ValidationFailed` on threshold breach.

- [ ] **Step 4: Run the unit executable and verify GREEN**

Run:

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: build and process both exit `0`.

- [ ] **Step 5: Add RED tests for validation failures, then implement them**

Add separate assertions for:

```cpp
assert(validate_instant_ai_polling(request, InstantAiPollingData{}, stats).code
       == "INSTANT_AI_NO_SAMPLES");

request.max_gap_provided = true;
request.max_gap_ms = 0.2;
assert(validate_instant_ai_polling(request, many, stats).code
       == "INSTANT_AI_GAP_EXCEEDED");

many.reads[1].values[0] = std::numeric_limits<double>::quiet_NaN();
assert(validate_instant_ai_polling(request, many, stats).code
       == "INVALID_INSTANT_AI_DATA");
```

Run once before implementation to see the assertions fail, implement the minimal validation, then rerun until exit `0`.

- [ ] **Step 6: Commit Task 1**

```powershell
git add SConstruct tests/daq_capability_test/test_main.cpp `
  tests/daq_capability_test/test_instant_ai_polling.cpp `
  src/daq_capability_test/instant_ai_polling.hpp `
  src/daq_capability_test/instant_ai_polling.cpp
git commit -m "test: define instant AI polling result contract"
```

### Task 2: Adapter contract and Legacy `ReadAny` implementation

**Files:**
- Modify: `src/daq_capability_test/daq_adapter.hpp`
- Modify: `src/daq_capability_test/legacy_adapter.hpp`
- Modify: `src/daq_capability_test/legacy_adapter.cpp`
- Modify: `tests/daq_capability_test/test_legacy_adapter.cpp`

- [ ] **Step 1: Write failing tests for Legacy runtime exports and channel validation**

Add assertions:

```cpp
const std::vector<std::string> instant_exports =
    daq_capability_test::legacy_instant_ai_required_runtime_exports();
assert(instant_exports.size() == 1);
assert(instant_exports[0] == "AdxInstantAiCtrlCreate");
assert(daq_capability_test::instant_ai_channels_are_contiguous({0}));
assert(daq_capability_test::instant_ai_channels_are_contiguous({0, 1}));
assert(!daq_capability_test::instant_ai_channels_are_contiguous({0, 2}));
assert(!daq_capability_test::instant_ai_channels_are_contiguous({}));
```

- [ ] **Step 2: Run and verify RED**

Run:

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
```

Expected: compilation fails because both helper functions are missing.

- [ ] **Step 3: Add the default adapter operation and Legacy declaration**

Include `instant_ai_polling.hpp` in `daq_adapter.hpp` and add:

```cpp
virtual AdapterResult<InstantAiPollingData> poll_instant_ai(
    const InstantAiPollingRequest&)
{
    AdapterResult<InstantAiPollingData> result;
    result.unsupported = true;
    result.code = "INSTANT_AI_UNSUPPORTED";
    result.message = "Instant AI polling is only available in the Legacy build";
    result.stage = "variant";
    return result;
}
```

Declare the Legacy override and test helpers in `legacy_adapter.hpp`.

- [ ] **Step 4: Run the helper tests and verify GREEN**

Implement only the two pure helpers, rerun the unit build and executable, and expect exit `0`.

- [ ] **Step 5: Implement Legacy polling**

In `LegacyDaqAdapter::poll_instant_ai`:

```cpp
InstantAiCtrl* controller = AdxInstantAiCtrlCreate();
if (!controller)
    return failure<InstantAiPollingData>(
        "CONTROLLER_CREATE_FAILED", "Instant AI controller creation returned null", "create_instant");
```

Then:

1. Select the requested device.
2. Require a nonempty contiguous channel list.
3. Resolve the exact requested voltage range with the existing normalized-range helper.
4. Set the range on every requested channel.
5. Loop with `std::chrono::steady_clock` until `duration_seconds`.
6. Call `ReadAny(start, count, NULL, values.data())`.
7. Append `InstantAiRead` with completion timestamp, call duration and completion-to-completion interval.
8. On failure, set `failed_reads=1`, return the driver code/stage plus partial `value`.
9. Always call `Dispose()` using an RAII guard.

Do not call `query_capabilities()` from this path because it creates `BufferedAiCtrl` and would incorrectly make Instant AI depend on Buffered AI.

- [ ] **Step 6: Build the real Legacy target**

Run:

```powershell
scons -Q cpp_build/daq_capability_test_legacy.exe
```

Expected: exit `0`, confirming the Legacy header/API calls compile and link through runtime-loaded exports.

- [ ] **Step 7: Commit Task 2**

```powershell
git add src/daq_capability_test/daq_adapter.hpp `
  src/daq_capability_test/legacy_adapter.hpp `
  src/daq_capability_test/legacy_adapter.cpp `
  tests/daq_capability_test/test_legacy_adapter.cpp
git commit -m "feat: poll Legacy Instant AI values"
```

### Task 3: CLI, JSON evidence and timestamped raw output

**Files:**
- Modify: `src/daq_capability_test/cli.hpp`
- Modify: `src/daq_capability_test/cli.cpp`
- Modify: `src/daq_capability_test/result_writer.hpp`
- Modify: `src/daq_capability_test/result_writer.cpp`
- Modify: `tests/daq_capability_test/test_instant_ai_polling.cpp`
- Modify: `tests/daq_capability_test/test_writer.cpp`

- [ ] **Step 1: Write failing CLI parse tests**

Use `parse_cli` with mutable argument arrays and assert:

```cpp
// instant-ai-polling --channels 0 --range -10V~10V --output-dir out
assert(parsed.ok());
assert(parsed.options.command == CliCommand::InstantAiPolling);
assert(parsed.options.duration_seconds == 30.0);
assert(!parsed.options.max_gap_provided);

// --duration 0
assert(!invalid_duration.ok());
assert(invalid_duration.result.code == "INVALID_OPTION_VALUE");

// --max-gap-ms 0
assert(!invalid_gap.ok());

// --channels 0,2
assert(!non_contiguous.ok());
assert(non_contiguous.result.code == "NON_CONTIGUOUS_CHANNELS");
```

- [ ] **Step 2: Run and verify RED**

Run unit build; expect missing enum/options and parse failures.

- [ ] **Step 3: Implement parser/help contract**

Add:

```cpp
enum class CliCommand {
    None, Help, Capability, Acquire, Trigger, InstantAiPolling,
    PhaseCapture, PhaseReconstruct, Suite
};
```

Add `duration_seconds=30.0`, `max_gap_ms=0.0`, and `max_gap_provided=false` to `CliOptions`. Parse positive `--duration` and `--max-gap-ms`. Require channels, range and output directory for this command and reject noncontiguous channels before runtime access. Add the exact command to `--help`.

- [ ] **Step 4: Write failing ResultWriter TSV tests**

Create two records for channels `{1,2}`, call:

```cpp
writer.write_instant_ai_raw({1, 2}, reads);
```

Assert the published file is `raw/instant_ai_polling.tsv` and begins:

```text
read_index	elapsed_seconds	call_duration_us	interval_us	channel_1	channel_2
0	0.001	80	0	1.25	2.5
```

Also assert a mismatched value width returns `INVALID_INSTANT_AI_DATA` without publishing the final file.

- [ ] **Step 5: Run RED, then implement atomic raw writer**

Reuse `ensure_directory_path` and `atomic_write`; serialize with classic locale and sufficient double precision. Validate finite numeric fields and row width before constructing output.

- [ ] **Step 6: Implement CLI orchestration**

Add `run_instant_ai_polling` which:

1. Creates the result directory/environment without first calling `safe_query`.
2. Calls `adapter.poll_instant_ai(request)`.
3. Writes any valid returned reads to `raw/instant_ai_polling.tsv`.
4. Runs `validate_instant_ai_polling` after a successful adapter call.
5. Adds all required evidence keys and per-channel `channel_<n>_{min,max,span}_v`.
6. Writes summary/log on both success and failure.
7. Creates `capture.complete` only for PASS.

Copy `runtime_path` and `runtime_version` from `InstantAiPollingData` into evidence; environment fields may remain `unavailable` only when controller initialization fails before runtime metadata is known.

- [ ] **Step 7: Add a scripted adapter CLI test**

Create a small test adapter overriding `poll_instant_ai` with deterministic records. Redirect `stdout`, invoke `run_cli`, parse/check the final string, and assert:

```cpp
assert(exit == 0);
assert(output.find("\"code\":\"INSTANT_AI_POLLING_STABLE\"") != std::string::npos);
assert(output.find("\"successful_reads\":\"3\"") != std::string::npos);
assert(output.find("\"max_interval_us\":\"300") != std::string::npos);
```

Add a second result with a 300 µs gap and `--max-gap-ms 0.2`; expect exit `2` and `INSTANT_AI_GAP_EXCEEDED`.

- [ ] **Step 8: Run unit tests and all three builds**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
scons -Q cpp_build/daq_capability_test_mock.exe
scons -Q cpp_build/daq_capability_test_legacy.exe
scons -Q cpp_build/daq_capability_test_xnavi.exe
```

Expected: all commands exit `0`; default adapter methods keep Mock/XNavi link-compatible.

- [ ] **Step 9: Commit Task 3**

```powershell
git add src/daq_capability_test/cli.hpp src/daq_capability_test/cli.cpp `
  src/daq_capability_test/result_writer.hpp src/daq_capability_test/result_writer.cpp `
  tests/daq_capability_test/test_instant_ai_polling.cpp `
  tests/daq_capability_test/test_writer.cpp
git commit -m "feat: expose instant AI polling command"
```

### Task 4: Python offline sample plotter

**Files:**
- Create: `scripts/plot_instant_ai_samples.py`
- Create: `tests/daq_capability_test/test_plot_instant_ai_samples.py`

- [ ] **Step 1: Write failing Python tests**

Create a temporary valid TSV and assert:

```python
completed = subprocess.run(
    [sys.executable, str(SCRIPT), str(path)],
    text=True, encoding="utf-8", capture_output=True, check=False)
self.assertEqual(completed.returncode, 0, completed.stderr)
self.assertEqual(
    completed.stdout.splitlines()[0],
    "read_index=0 elapsed_seconds=0.001 call_duration_us=80 "
    "interval_us=0 channel_0=1.25")
```

Add subtests for missing file, empty file, missing fixed column, no `channel_<n>` column and nonnumeric data; each must return nonzero and write a concise reason to stderr.

- [ ] **Step 2: Run and verify RED**

```powershell
python -m unittest tests.daq_capability_test.test_plot_instant_ai_samples -v
```

Expected: import/process failure because the script does not exist.

- [ ] **Step 3: Implement the standard-library reader**

Use `csv.DictReader(..., delimiter="\t")`. Require exactly the four fixed columns plus at least one column matching `^channel_\d+$`. Parse `read_index` as a nonnegative integer and every other printed value with `float()`, rejecting non-finite values. Print fields in file-header order as `name=value`.

Return `0` on success and `1` after writing a single `error: ...` line to stderr on invalid input.

- [ ] **Step 4: Run Python tests and verify GREEN**

Run the unittest command again and expect all cases `ok`.

- [ ] **Step 5: Commit Task 4**

```powershell
git add scripts/plot_instant_ai_samples.py requirements.txt `
  tests/daq_capability_test/test_plot_instant_ai_samples.py
git commit -m "feat: print saved instant AI samples"
```

### Task 5: Documentation, regression and hardware handoff

**Files:**
- Modify: `README.md`
- Test: all DAQ unit and Python regression suites

- [ ] **Step 1: Add README usage and interpretation**

Document:

```powershell
cpp_build\daq_capability_test_legacy.exe instant-ai-polling `
  --device 'PCI-1714,BID#0' --channels 0 --range '-10V~10V' `
  --duration 30 --output-dir daq_capability_results\legacy_instant

python scripts\plot_instant_ai_samples.py `
  daq_capability_results\legacy_instant\<timestamp>\raw\instant_ai_polling.tsv
```

State explicitly that PASS proves stable software polling for this run, not hardware-clocked gapless acquisition. Explain optional `--max-gap-ms`.

- [ ] **Step 2: Run fresh full verification**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
scons -Q cpp_build/daq_capability_test_mock.exe
scons -Q cpp_build/daq_capability_test_legacy.exe
scons -Q cpp_build/daq_capability_test_xnavi.exe
python -m unittest tests.daq_capability_test.test_plot_instant_ai_samples -v
python -m unittest tests.daq_capability_test.test_mock_cli -v
```

Expected: every command exits `0`; Python reports no failures/errors.

- [ ] **Step 3: Inspect CLI help without loading hardware**

```powershell
cpp_build\daq_capability_test_legacy.exe --help
```

Expected: exit `0` and help contains `instant-ai-polling`, `--duration`, and `--max-gap-ms`.

- [ ] **Step 4: Commit documentation**

```powershell
git add README.md
git commit -m "docs: add Legacy Instant AI polling procedure"
```

- [ ] **Step 5: Run the hardware command on the PCI-1714U machine**

After confirming AI0 voltage, offset and grounding are within the configured range:

```powershell
cpp_build\daq_capability_test_legacy.exe instant-ai-polling `
  --device 'PCI-1714,BID#0' --channels 0 --range '-10V~10V' `
  --duration 30 --output-dir daq_capability_results\legacy_instant
```

Accept only exit `0`, final JSON `PASS / INSTANT_AI_POLLING_STABLE`, a `capture.complete` marker, and a readable raw TSV. Record `reads_per_second`, P95/P99/max interval and channel span before choosing a future `--max-gap-ms`.
