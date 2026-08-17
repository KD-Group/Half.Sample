# Selectable Waveform Processing Modes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a backward-compatible `threshold_accumulation`/`independent_cycle` selection implemented by Half.Sample and exposed by KDM3000 measurement settings.

**Architecture:** Half.Sample parses one optional mode, keeps the existing processor as the default branch, and adds an independent batch processor that collects phase-aligned complete cycles before fitting. The Python wrapper forwards the mode through `measure`, `dump`, and `process`; KDM3000 stores and forwards the selection without implementing signal processing.

**Tech Stack:** C++17/C++11-compatible Half.Sample driver, SCons, C++ assert-based tests, Python wrapper tests, PySide6/quite6 UI, prett6 project models, pytest/uv.

---

### Task 1: Define and validate the processing-mode contract

**Files:**
- Modify: `src/config/sampling_config.hpp`, `src/config/sampling_config.cpp`
- Modify: `src/commander/measure.cpp`, `src/error/error.hpp`, `src/error/error.cpp`
- Modify: `sample/sample.py`, `sample/result.py`
- Test: `tests/sample_instant_ai/test_sampling_config.cpp`, `tests/test_measure.py`

- [ ] **Step 1: Write failing configuration and wrapper tests.**

Test the exact contract:

```cpp
assert(config.waveform_processing_mode == "threshold_accumulation");
assert(config.update(32, 50.0, 0.0, 100, 10.0, "independent_cycle"));
assert(config.waveform_processing_mode == "independent_cycle");
assert(!config.update(32, 50.0, 0.0, 100, 10.0, "unknown_mode"));
```

Also assert that Python calls with `waveform_processing_mode="independent_cycle"` append the value to `measure` and `dump`, while calls without it preserve the old command shape.

- [ ] **Step 2: Run the focused tests and verify failure.**

```powershell
scons -Q cpp_build/sample_instant_ai_unit_tests.exe
cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py -q
```

Expected: failure because the mode field and argument do not exist.

- [ ] **Step 3: Add the mode field and backward-compatible update argument.**

Add `std::string waveform_processing_mode = "threshold_accumulation"` to `SamplingConfig`. Extend `SamplingConfig::update` with a defaulted final mode argument and accept only:

```cpp
"threshold_accumulation"
"independent_cycle"
```

An omitted argument uses the first value; an explicit unknown value returns false.

- [ ] **Step 4: Parse and forward the optional command value.**

Extend `measure`, `config`, and `process` parsing after the existing instant-AI options. Preserve all old command forms. Update `sample/sample.py` to add the optional keyword to `measure`, `dump`, and `process` and append it only when explicitly selected. Map invalid values to the existing configuration error.

- [ ] **Step 5: Run and commit the contract change.**

```powershell
scons -Q cpp_build/sample_instant_ai_unit_tests.exe
cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py -q
git add src/config src/commander/measure.cpp src/error sample tests
git commit -m "feat: add waveform processing mode selection"
```

Expected: all focused tests pass.

### Task 2: Implement independent-cycle extraction and aggregation

**Files:**
- Create: `src/processor/independent_cycle.hpp`, `src/processor/independent_cycle.cpp`
- Modify: `src/processor/processor.cpp`, `src/result/sampling_result.hpp`, `src/constant.hpp`, `SConstruct`
- Test: `tests/sample_instant_ai/test_independent_cycle.cpp`

- [ ] **Step 1: Write failing synthetic-cycle tests.**

Create deterministic samples with a known period and phase. Verify a batch with 29 candidates and one discarded boundary yields 28 accepted normalized cycles. Create a second independently phase-shifted batch and assert that its normalized edge aligns with the first batch.

The test must also verify that fewer than the requested cycles returns an insufficient result and does not produce an averaged waveform.

Add a real-data regression test that reads `D:\kunde\code\KDM3000\sample-软件下降到阶段二后急停采集数据-20260817\13.txt` by default, with `HALF_SAMPLE_REGRESSION_DATA` as an override. The 186 MB file is not copied into the repository. When the file is unavailable outside the development machine, skip with an explicit message; when present, assert the expected 13.2 million samples, 33 source periods, and exactly 32 accepted cycles in `independent_cycle` mode. Duplicate the in-memory vector with `insert` to simulate two files concatenated head-to-tail, then assert the duplicated input supports exactly 64 accepted cycles without treating the join as an extra malformed cycle.

- [ ] **Step 2: Run the new test and verify failure.**

```powershell
scons -Q cpp_build/sample_instant_ai_unit_tests.exe
cpp_build\sample_instant_ai_unit_tests.exe
```

Expected: compilation failure because the helper is not defined.

- [ ] **Step 3: Implement a batch-only extraction helper.**

Define a result containing normalized cycles, candidate count, accepted count, discarded count, and failure status. Detect threshold edges only within the supplied buffer; pair adjacent valid edges; reject intervals outside the configured period tolerance; align each accepted cycle to the same threshold crossing; and baseline-correct it. The helper must not read or append any previous batch.

- [ ] **Step 4: Implement remaining-count acquisition and bounded retries.**

For target `N`, maintain `accepted_count` and compute `remaining = N - accepted_count`. Each batch plans at least `(remaining + 1) * waveform_length` points. If the batch cannot provide its required valid cycles, discard the entire batch and retry only that batch. Stop on cancellation or `MaxIndependentCycleRetries`; return the waveform-count error without fitting.

- [ ] **Step 5: Average only normalized cycles and dispatch by mode.**

Keep the current `threshold_accumulation` body unchanged. For `independent_cycle`, accumulate only normalized cycle vectors, use exactly the first `N`, and then call the existing average/estimate path. Do not concatenate raw buffers across batches. Add buffered result counters for requested, complete, discarded, batch, and retry counts.

- [ ] **Step 6: Add tests, build sources, and commit.**

Add the new source to the sample and unit-test lists in `SConstruct`, then run:

```powershell
scons -Q cpp_build/sample_instant_ai_unit_tests.exe
cpp_build\sample_instant_ai_unit_tests.exe
git add src/processor src/result src/constant.hpp SConstruct tests/sample_instant_ai/test_independent_cycle.cpp
git commit -m "feat: add independent cycle waveform processing"
```

Expected: the synthetic 28+4 scenario and phase-alignment tests pass.

### Task 3: Integrate dump/replay and result reporting

**Files:**
- Modify: `src/commander/measure.cpp`, `src/sampler/origin_data.cpp`, `src/result/sampling_result.hpp`
- Modify: `sample/result.py`
- Test: `tests/test_measure.py`, `tests/sample_instant_ai/test_dump_format.cpp`

- [ ] **Step 1: Write failing integration tests.**

Test that live measurement and dump/replay report the selected mode and complete-cycle count, and that insufficient data returns the waveform-count error with no usable estimate.

- [ ] **Step 2: Persist and restore the mode in dump metadata.**

Write `waveform_processing_mode=<value>` to new dump files. Old dump files without this key use `threshold_accumulation`; explicit process mode overrides only when supplied by the command.

- [ ] **Step 3: Publish and reset counters.**

Copy the new result fields in `publish_process_result`, reset them in `clear_measure_data`, serialize them in `to_query`, and expose backward-compatible Python result attributes with zero defaults.

- [ ] **Step 4: Run integration tests and commit.**

```powershell
scons -Q cpp_build/sample.exe cpp_build/sample_instant_ai_unit_tests.exe
cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py -q
git add src/commander/measure.cpp src/sampler/origin_data.cpp src/result sample/result.py tests
git commit -m "feat: report independent waveform processing results"
```

### Task 4: Add the selector to KDM3000 measurement settings

**Files:**
- Modify: `D:/kunde/code/KDM3000/model/abstract_project_model.py`
- Modify: `D:/kunde/code/KDM3000/settings/project_setting.py`
- Modify: `D:/kunde/code/KDM3000/controller/main_gui/mobility/mobility_param_widget_controller.py`
- Modify: `D:/kunde/code/KDM3000/controller/main_gui/resistivity/resistivity_param_widget_controller.py`
- Modify: `D:/kunde/code/KDM3000/res/ui/main_gui/mobility_parameters_widget.ui`
- Modify: `D:/kunde/code/KDM3000/res/ui/main_gui/resistivity_parameters_widget.ui`
- Modify: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py`
- Modify: `D:/kunde/code/KDM3000/controller/report_gui/export_database_controller.py`
- Test: `D:/kunde/code/KDM3000/tests/controller/test_sampling_controller.py`, `D:/kunde/code/KDM3000/tests/model/test_export_show_raw.py`

- [ ] **Step 1: Write failing model, UI, and forwarding tests.**

Assert that new projects and old projects default to `threshold_accumulation`, that `independent_cycle` persists, and that normal `measure` and `dump` pass the selected mode. Motion-risk safety measurement remains on the default mode.

- [ ] **Step 2: Add the persisted project field and UI choices.**

Add `waveform_processing_mode` with default `threshold_accumulation`. Add a combo to both mobility and resistivity settings with labels “原始阈值累加” and “独立周期对齐”, mapped to the two protocol strings. Connect it using the existing `average_times` combo pattern, including saved settings and enabled state.

- [ ] **Step 3: Forward the selected mode without processing data in KDM3000.**

Include the model value in `_sampling_options_snapshot` and pass it through `sampler.measure`, `sampler.dump`, and offline `sampler.process`. Do not add cycle detection or averaging to KDM3000.

- [ ] **Step 4: Run focused KDM3000 verification and commit.**

From `D:\kunde\code\KDM3000`:

```powershell
uv run pytest tests/controller/test_sampling_controller.py tests/model/test_export_show_raw.py -q
uv run pycodestyle . --max-line-length=120 --exclude temperature,.venv,.git,__pycache__,dist
git add model settings controller res tests
git commit -m "feat: add waveform processing mode setting"
```

Expected: tests pass and style check exits 0.

### Task 5: Document and verify both repositories

**Files:**
- Modify: `README.md` in Half.Sample
- Modify: `D:/kunde/code/KDM3000` documentation only if the existing project-setting documentation requires the new field

- [ ] **Step 1: Document protocol values and compatibility.**

Document that omitted mode means `threshold_accumulation`, and show live and dump/replay examples using `independent_cycle`.

- [ ] **Step 2: Run full Half.Sample verification.**

```powershell
scons -Q cpp_build/sample.exe cpp_build/sample_instant_ai_unit_tests.exe cpp_build/daq_capability_unit_tests.exe
cpp_build\sample_instant_ai_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
python -m pytest tests -q
```

- [ ] **Step 3: Run full KDM3000 verification.**

```powershell
uv run pytest tests -q
uv run pycodestyle . --max-line-length=120 --exclude temperature,.venv,.git,__pycache__,dist
```

- [ ] **Step 4: Review both worktrees before handoff.**

Run `git status --short` in both repositories and confirm unrelated user changes are untouched. Report exact commits, test commands, and any hardware-only risks.
