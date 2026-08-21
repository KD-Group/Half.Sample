# Half.Sample Online Result Diagnostic Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变异常发生前连续采样时序和业务结果的前提下，用新的结果对象重算当前内存原始缓冲区，并记录在线结果与重算结果的差异。

**Architecture:** Half.Sample 新增只读命令 `to_diagnose_current_result`，它复制当前采集输入到局部 `SamplingResult` 并执行现有处理流水线，然后以普通查询协议返回局部结果。KDM3000 仍先取得并保留在线业务结果，只在 no-dump 在线电压达到临时阈值 `0.2 V` 后调用诊断命令并写结构化日志。

**Tech Stack:** C++11、Half.Sample stdin/stdout 命令协议、Python `sample` 包、KDM3000 Python/PySide6 日志系统。

---

### Task 1: Half.Sample 只读内存重算命令

**Files:**
- Modify: `src/commander/measure.hpp`
- Modify: `src/commander/measure.cpp`
- Modify: `src/commander/commander.cpp`

- [ ] **Step 1: 提取可复用的结果输出函数**

在 `src/commander/measure.cpp` 匿名命名空间中加入 `emit_query_result(const Config::SamplingConfig&, const Result::SamplingResult&, bool)`，把当前 `to_query()` 从 `success` 到 `wave` 的输出逻辑迁入该函数。`to_query()` 只保留 acquire 读取 `measuring`，然后调用：

```cpp
emit_query_result(Global::config, Global::result, measuring);
```

输出字段名和顺序必须保持不变，避免改变现有 Python 协议。

- [ ] **Step 2: 实现当前缓冲区快照和局部处理**

在 `src/commander/measure.cpp` 新增 `to_diagnose_current_result()`：

```cpp
void to_diagnose_current_result() {
    if (Global::result.measuring.load(std::memory_order_acquire)) {
        Base::error(Error::NOW_IN_MEASURING);
        return;
    }

    const Config::SamplingConfig config = Global::config;
    Result::SamplingResult replay(false);
    replay.totalSamplingBuffer = Global::result.totalSamplingBuffer;
    replay.instant_ai_waveforms = Global::result.instant_ai_waveforms;
    replay.instant_ai_readings = Global::result.instant_ai_readings;
    replay.instant_ai_format_version = Global::result.instant_ai_format_version;
    replay.instant_ai_actual_duration_seconds = Global::result.instant_ai_actual_duration_seconds;
    replay.resultWave.assign(static_cast<std::size_t>(std::max(0, config.valid_length)), 0.0);

    bool success = Processor::align(config, replay);
    if (success) success = Processor::summation(config, replay);
    if (success) success = Processor::estimate(config, replay);
    if (success) {
        replay.success = true;
        success = Processor::validate_finite_result(config, replay);
    }
    replay.success = success;
    emit_query_result(config, replay, false);
}
```

该函数不得修改 `Global::config`、`Global::result` 或采样器状态。

- [ ] **Step 3: 注册命令声明和分发**

在 `src/commander/measure.hpp` 声明：

```cpp
void to_diagnose_current_result();
```

在 `src/commander/commander.cpp` 的 mapper 中注册：

```cpp
add_func_into_mapper(to_diagnose_current_result, mapper);
```

- [ ] **Step 4: 构建 Half.Sample**

Run: `scons`

Expected: 构建退出码为 0，生成 `cpp_build/sample.exe`。

### Task 2: 诊断协议冒烟

**Files:**
- Verify: `cpp_build/sample.exe`

- [x] **Step 1: 保持 Python 包装层兼容**

不新增 Python 包装层公开方法。KDM3000 使用现有 `sampler.communicate()` 发送 `to_diagnose_current_result`，因此现场只需更新支持新命令的 `sample.exe`。

- [x] **Step 2: 用异常原始文件验证普通结果与重算结果**

启动 `Sampler`，先发送 `to_config 1 50 False`，对 `20260821-1_phase2_motion_risk_1_50.00_1_1787317913666273500.txt` 执行 `process/query`，然后发送 `to_diagnose_current_result`。

Observed: 普通结果和内存重算结果均为 `success=True`、`v_inf=0.090841`、`maximum=-0.36377`、`minimum=-0.493164`。

### Task 3: KDM3000 异常后条件诊断

**Files:**
- Modify: `D:/kunde/code/KDM3000/controller/tasks/sampling_controller.py`

- [ ] **Step 1: 定义临时诊断阈值**

在 `SamplingController` 类常量中加入：

```python
_PHASE2_MEMORY_REPLAY_TRIGGER_VOLTAGE = 0.2
```

- [ ] **Step 2: no-dump 查询后触发只读重算**

在 `_motion_risk_detect_v_inf.finish_attempt()` 取得并记录 `result` 后，仅当 `filename is None`、`result.v_inf_valid is True` 且有限电压不小于阈值时调用 `sampler.diagnose_current_result()`。

成功时用现有 `_log_phase2_result_diagnostic()` 记录 stage `online_measure_memory_replay`；异常时记录 warning。无论诊断成功或失败，都返回原来的 `result`。

- [ ] **Step 3: 保持 dump 链路不变**

确认 `filename is not None` 时仍只产生 `online_before_process`、`process_returned` 和 `offline_after_process`，不调用内存重算命令。

### Task 4: 直接验证和提交

**Files:**
- Verify only: Half.Sample 与 KDM3000 上述修改文件

- [ ] **Step 1: 运行 Half.Sample 现有构建、测试与风格检查**

Run: `scons -j $env:NUMBER_OF_PROCESSORS`

Run: `cpp_build/sample_instant_ai_unit_tests.exe`

Run: `python -m pytest`

Run: `pycodestyle . --max-line-length=120 --exclude=src/3rdparty`

Expected: 构建及所有现有测试和风格检查退出码均为 0；本任务按用户要求不新增 TDD 测试。

- [ ] **Step 2: 运行 KDM3000 现有 sampling controller 测试和风格检查**

Run: `pytest tests/controller/test_sampling_controller.py -q`

Run: `pycodestyle controller/tasks/sampling_controller.py --max-line-length=120`

Expected: 测试和风格检查退出码均为 0。

- [ ] **Step 3: 检查差异范围**

Run: `git diff --check`，并分别检查两个仓库的 `git status --short` 和目标文件 diff。

Expected: 无空白错误，不包含用户现有无关修改。

- [ ] **Step 4: 提交 Half.Sample 修改**

只暂存本任务修改的 Half.Sample 文件与本计划/设计修订，提交消息：

```text
feat: diagnose current sampling result
```

KDM3000 当前已有用户和前序诊断修改，不擅自提交；交付时明确列出未提交文件。
