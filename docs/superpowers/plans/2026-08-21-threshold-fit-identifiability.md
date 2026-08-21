# Threshold Accumulation Fit Identifiability Implementation Plan

> **For agentic workers:** Execute inline in the current session. The user explicitly prohibited TDD and test execution for this diagnostic fix.

**Goal:** Reject unidentifiable `threshold_accumulation` exponential fits while retaining useful acquisition diagnostics and removing the temporary memory-replay probe.

**Architecture:** Reuse the existing native `fit_is_identifiable` predicate at the fixed-frequency fit boundary so invalid fits fail before `estimate.b` can replace the raw amplitude. Keep passive query diagnostics unchanged. Remove the active replay command from Half.Sample and its conditional caller from KDM3000.

**Tech Stack:** C++17, Half.Sample command protocol, Python 3 KDM3000 controller.

---

### Task 1: Remove the temporary active replay probe

**Files:**
- Modify: `src/commander/measure.cpp`
- Modify: `src/commander/measure.hpp`
- Modify: `src/commander/commander.cpp`
- Modify: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py`

- [ ] Remove `to_diagnose_current_result` from the Half.Sample implementation, declaration, and command mapper.
- [ ] Remove `_PHASE2_MEMORY_REPLAY_TRIGGER_VOLTAGE` and the `online_measure_memory_replay` conditional command block from KDM3000.
- [ ] Keep `emit_query_result` acquisition diagnostics and `_PHASE2_RESULT_DIAGNOSTIC_FIELDS` unchanged.

### Task 2: Reject unidentifiable threshold fits

**Files:**
- Modify: `src/processor/processor.cpp:447`

- [ ] Change the fixed-frequency guard from:

```cpp
if (!config.is_instant() && config.waveform_processing_mode == "independent_cycle" &&
    !fit_is_identifiable(wave, result.estimate)) {
```

to:

```cpp
if (!config.is_instant() && !fit_is_identifiable(wave, result.estimate)) {
```

- [ ] Confirm the failure path resets only the invalid estimate, sets `FIT_NOT_IDENTIFIABLE`, and returns before `record_voltage(result, result.estimate.b)`, preserving the raw amplitude recorded by `align`.

### Task 3: Verify and commit

**Files:**
- Review: `src/commander/measure.cpp`
- Review: `src/commander/measure.hpp`
- Review: `src/commander/commander.cpp`
- Review: `src/processor/processor.cpp`
- Review: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py`

- [ ] Search both repositories to ensure `to_diagnose_current_result`, `_PHASE2_MEMORY_REPLAY_TRIGGER_VOLTAGE`, and `online_measure_memory_replay` have no remaining references.
- [ ] Build Half.Sample with `scons -Q sample.exe`; expected result is a successful link of `cpp_build/sample.exe`.
- [ ] Run `python -m py_compile controller/tasks/sampling_controller.py` in KDM3000; expected result is exit code 0 with no output.
- [ ] Review diffs to ensure passive diagnostics remain and no unrelated user changes are included.
- [ ] Commit only Half.Sample source and plan changes on `master`; leave the already mixed KDM3000 working tree uncommitted.
