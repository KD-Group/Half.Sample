# Phase2 Compact Diagnostics Implementation Plan

> **For agentic workers:** Execute inline in the current session. The user explicitly prohibited TDD and test execution.

**Goal:** Replace repeated full phase2 diagnostics with one compact line per result while retaining complete evidence for suspicious samples.

**Architecture:** Keep Half.Sample query fields unchanged. In KDM3000, construct a flat compact payload for every phase2 result, evaluate a deterministic suspicious-result predicate, and emit the existing full payload only when that predicate is true.

**Tech Stack:** Python 3, KDM3000 logging, Half.Sample result protocol.

---

### Task 1: Add compact and suspicious-result helpers

**Files:**
- Modify: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py:49-158`

- [ ] Add constants for the `0.2V` detail trigger and the `198000` tau identifiability boundary.
- [ ] Add a compact payload builder containing stage, generation, operation, mode, frequency, average count, success, error category, terminal stage, `v_inf`, raw span, tau, b, buffer hash, buffer equality flags, and the first 12 SHA256 characters.
- [ ] Add a suspicious-result predicate covering hardware acquisition failure, repeated buffers, unexpected terminal stages, boundary tau, high `v_inf`, and missing terminal stage.
- [ ] Change `_log_phase2_result_diagnostic` to always emit `phase2 sample` with the compact payload and emit the existing full result as `phase2 sampler diagnostic detail` only when suspicious.

### Task 2: Remove redundant phase2 lines

**Files:**
- Modify: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py:158-198`
- Modify: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py:848-912`

- [ ] Delete `_log_phase2_attempt_diagnostic` and both of its `start_attempt` callers.
- [ ] Pass frequency and average count into each result logger call.
- [ ] Delete the standalone `process_returned` log while keeping raw-file, online, and offline result logging for dump mode.
- [ ] Keep `_PHASE2_RESULT_DIAGNOSTIC_FIELDS`, `_log_phase2_raw_file_diagnostic`, sampling behavior, and motion safety behavior unchanged.

### Task 3: Verify delivery

**Files:**
- Review: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py`

- [ ] Confirm `stage=start` and `process_returned` logging references are gone.
- [ ] Confirm compact and conditional detail log calls are present.
- [ ] Run `python -m py_compile controller/tasks/sampling_controller.py`; expected result is exit code 0 with no output.
- [ ] Review the focused diff and leave the mixed KDM3000 working tree uncommitted.
