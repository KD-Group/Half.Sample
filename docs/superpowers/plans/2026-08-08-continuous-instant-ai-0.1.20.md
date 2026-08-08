# Half.Sample 0.1.20 Continuous Instant AI Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 发布 Half.Sample 0.1.20，使低频 Instant AI 在单次硬件会话中连续采集 `(N+1)/f`，稳定提取并平均 N 个完整波形，同时提供进度、取消、稳定错误分类和可独立重放的 V2 原始文件。

**Architecture:** 保留 Buffered AI 分支不变。Instant AI 分为配置/调度、连续读取、纯逻辑重建、命令协议四层；在线采集和 V2 离线重放共同调用连续波形重建，旧 V1 文件继续调用 legacy 重建入口。

**Tech Stack:** C++11/17、Advantech legacy BDaq API、SCons、Python 3.6+、pytest/unittest、PyPI/AppVeyor。

---

## 文件结构

| 文件 | 职责 |
|---|---|
| `src/config/sampling_config.{hpp,cpp}` | 模式判定、三个 Instant 参数校验、窗口/轮询元数据 |
| `src/sampler/instant_ai.{hpp,cpp}` | 连续调度、滞回边沿、周期选择、相位重采样 |
| `src/sampler/real_sampler.{hpp,cpp}` | Buffered 原实现和真实 Instant 连续读取 |
| `src/sampler/mock_sampler.cpp` | 无等待的连续 Instant 测试数据 |
| `src/result/sampling_result.hpp` | 原始读数、线程安全进度和取消状态 |
| `src/sampler/sampler.hpp` | V2/V1/Buffered dump 与 load |
| `src/processor/processor.cpp` | 按文件版本选择连续或 legacy 重建 |
| `src/commander/{measure,commander}.{hpp,cpp}` | 新参数、进度、取消命令 |
| `src/error/error.{hpp,cpp}` | 稳定错误码、类别和 retryable 契约 |
| `sample/{sample,result}.py` | Python API 和返回对象 |

## Task 1: 固化配置、窗口和命令参数契约

**Files:**

- Modify: `src/config/sampling_config.hpp`
- Modify: `src/config/sampling_config.cpp`
- Modify: `src/commander/measure.cpp`
- Modify: `sample/sample.py`
- Modify: `tests/sample_instant_ai/test_sampling_config.cpp`
- Modify: `tests/test_measure.py`

- [ ] **Step 1: 写失败的配置和协议测试**

```cpp
assert(config.update(3, 0.05, 0.1, 100, 10.0));
assert(config.is_instant());
assert(std::abs(config.instant_ai_planned_duration_seconds - 80.0) < 1e-9);
assert(std::abs(config.instant_ai_polling_frequency - 5.0) < 1e-9);
assert(config.instant_ai_planned_readings == 401);
assert(!config.update(1, 0.05, 0.1, 100, 9.9));
assert(!config.update(1, 0.05, 0.1, 19, 10.0));
assert(config.update(1, 0.5, 0.0, 100, 10.0));
assert(!config.is_instant());
```

```python
self.assertEqual(
    client.measure(3, 0.05, instant_ai_frequency_threshold=0.1,
                   instant_ai_target_points_per_waveform=100,
                   instant_ai_max_reliable_polling_hz=10.0),
    "to_measure 3 0.05 False 0.1 100 10",
)
```

- [ ] **Step 2: 运行测试确认失败**

Run:

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py -q
```

Expected: C++ 因五参数 `update()` 和新字段不存在而编译失败；Python 因新关键字不存在而失败。

- [ ] **Step 3: 实现配置字段和校验**

```cpp
double instant_ai_max_reliable_polling_hz = 10.0;
double instant_ai_polling_frequency = 0.0;
double instant_ai_planned_duration_seconds = 0.0;
std::size_t instant_ai_planned_readings = 0;

bool update(int waveforms, double frequency, double instant_threshold = 0.0,
            int instant_target_points = 100,
            double instant_max_reliable_polling_hz = 10.0);
```

当阈值大于 0 时，先校验整组 Instant 参数，即使本次频率最终落入 Buffered；阈值为 0 只表示兼容模式下禁用 Instant。随后 Instant 分支计算窗口：

```cpp
if (instant_threshold > 0 &&
    (instant_target_points < 20 || !std::isfinite(instant_max_reliable_polling_hz) ||
    instant_max_reliable_polling_hz <= 0.0 ||
    instant_threshold * instant_target_points > instant_max_reliable_polling_hz + 1e-12)) {
    return false;
}
instant_ai_polling_frequency =
    std::min(frequency * instant_target_points, instant_max_reliable_polling_hz);
instant_ai_planned_duration_seconds = (number_of_waveforms + 1.0) / frequency;
instant_ai_planned_readings = static_cast<std::size_t>(
    std::llround(instant_ai_planned_duration_seconds * instant_ai_polling_frequency)) + 1;
sampling_interval = 1e6 / instant_ai_polling_frequency;
waveform_length = instant_target_points;
valid_length = instant_target_points / 2;
sampling_length_per_sample = static_cast<int>(instant_ai_planned_readings);
sampling_time = 1;
```

阈值为 0 时保留旧 Buffered 默认行为。

- [ ] **Step 4: 扩展 C++ 命令和 Python API**

`async_measure()` 与 `to_config()` 依次解析 threshold、target points、max polling，并拒绝多余 token：

```cpp
double instant_max_polling_hz = 10.0;
if (!Global::config.update(number_of_waveforms, emitting_frequency,
                           instant_threshold, instant_target_points,
                           instant_max_polling_hz)) {
    Base::error(Error::INVALID_INSTANT_AI_CONFIG);
    return;
}
```

`measure()` 和 `dump()` 增加 `instant_ai_max_reliable_polling_hz: float = 10.0`。只要三个 Instant 参数任一不是旧 API 默认值，就一次性追加全部三个参数：

```python
command += " {:.12g} {} {:.12g}".format(
    instant_ai_frequency_threshold,
    instant_ai_target_points_per_waveform,
    instant_ai_max_reliable_polling_hz,
)
```

- [ ] **Step 5: 验证并提交**

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py -q
git diff --check
git add src/config/sampling_config.hpp src/config/sampling_config.cpp src/commander/measure.cpp sample/sample.py tests/sample_instant_ai/test_sampling_config.cpp tests/test_measure.py
git commit -m "feat: define continuous Instant AI sampling config"
```

Expected: 聚焦测试通过，`git diff --check` 无输出。

## Task 2: 实现连续调度和纯逻辑波形重建

**Files:**

- Modify: `src/sampler/instant_ai.hpp`
- Modify: `src/sampler/instant_ai.cpp`
- Modify: `tests/sample_instant_ai/test_phase_schedule.cpp`
- Modify: `tests/sample_instant_ai/test_reconstruction.cpp`

- [ ] **Step 1: 写连续调度失败测试**

```cpp
const auto schedule = Sampler::InstantAi::build_continuous_schedule(0.05, 1, 100, 10.0);
assert(std::abs(schedule.duration_seconds - 40.0) < 1e-9);
assert(std::abs(schedule.polling_frequency_hz - 5.0) < 1e-9);
assert(schedule.planned_seconds.size() == 201);
assert(std::abs(schedule.planned_seconds.front()) < 1e-12);
assert(std::abs(schedule.planned_seconds.back() - 40.0) < 1e-9);

const auto three = Sampler::InstantAi::build_continuous_schedule(0.01, 3, 100, 10.0);
assert(std::abs(three.duration_seconds - 400.0) < 1e-9);
assert(three.planned_seconds.size() == 401);
```

- [ ] **Step 2: 写任意起相和覆盖失败测试**

```cpp
for (double start_phase : {0.0, 0.13, 0.49, 0.91}) {
    const auto readings = make_continuous_readings(0.05, 3, 100, start_phase);
    const auto result = reconstruct_continuous(readings, 3, 0.05, 100);
    assert(result.status == ReconstructionStatus::Success);
    assert(result.complete_waveforms == 3);
    assert(result.averaged_half_wave.size() == 50);
}
assert(reconstruct_continuous(make_missing_bins(2), 1, 0.05, 100).status ==
       ReconstructionStatus::Success);
assert(reconstruct_continuous(make_missing_bins(3), 1, 0.05, 100).status ==
       ReconstructionStatus::CoverageInsufficient);
```

另加周期偏差超容差、只有 N 个上升沿和幅度不足用例。

- [ ] **Step 3: 运行编译确认失败**

Run: `scons sample_instant_ai_unit_tests.exe`

Expected: 新调度、重建入口和状态枚举不存在。

- [ ] **Step 4: 定义数据结构并保留 V1 入口**

```cpp
struct TimedReading {
    double planned_seconds = 0.0;
    double actual_seconds = 0.0;
    double voltage = 0.0;
    bool read_success = true;
    int read_error_code = 0;
};
using TimedWaveform = std::vector<TimedReading>;
using TimedReadings = std::vector<TimedReading>;

struct ContinuousSchedule {
    std::vector<double> planned_seconds;
    double duration_seconds = 0.0;
    double polling_frequency_hz = 0.0;
};

enum class ReconstructionStatus {
    Success, AlignmentFailed, WaveformCountInsufficient, CoverageInsufficient,
};
```

保留 `reconstruct_legacy_waveforms(...)`，函数体来自现有 V1 `reconstruct(...)`；新增 `reconstruct_continuous(...)`。

- [ ] **Step 5: 实现调度、滞回边沿和周期选择**

调度覆盖闭区间 `0..(N+1)/f`。按全窗口最小/最大值计算 10%/40% 阈值：

```cpp
if (value <= lower) armed = true;
if (armed && value >= upper) {
    edges.push_back(index);
    armed = false;
}
```

相邻边沿时间差须位于理论周期的 `[0.8, 1.2]` 倍；从可信边沿链中选择首个 N 个连续区间。边沿不足返回 `WaveformCountInsufficient`，无可信边沿返回 `AlignmentFailed`。

- [ ] **Step 6: 实现逐周期局部相位重采样**

```cpp
const double phase = (reading.actual_seconds - edge_start_time) /
                     (edge_end_time - edge_start_time);
const int bin = std::min(target_points - 1,
                         static_cast<int>(phase * target_points));
```

同格求平均；连续空格最多插值 2 格，超过后返回 `CoverageInsufficient`。每周期尾部 `min(10, max(5, P/10))` 点求基线，减基线后累加前 `P/2` 点，严格除以 N。

- [ ] **Step 7: 验证并提交**

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
git diff --check
git add src/sampler/instant_ai.hpp src/sampler/instant_ai.cpp tests/sample_instant_ai/test_phase_schedule.cpp tests/sample_instant_ai/test_reconstruction.cpp
git commit -m "feat: reconstruct continuous Instant AI waveforms"
```

## Task 3: 接入结果状态、Mock 和处理器

**Files:**

- Modify: `src/result/sampling_result.hpp`
- Modify: `src/error/error.hpp`
- Modify: `src/error/error.cpp`
- Modify: `src/sampler/mock_sampler.cpp`
- Modify: `src/processor/processor.cpp`
- Modify: `src/commander/measure.cpp`
- Modify: `sample/result.py`
- Modify: `tests/test_measure.py`

- [ ] **Step 1: 写 Mock 端到端失败测试**

```python
sampler.set_sampler("mock_sampler")
sampler.set_sampler_value("mock_phase_offset", 0.49)
result = wait_for_result(sampler.measure(
    3, 0.05,
    instant_ai_frequency_threshold=0.1,
    instant_ai_target_points_per_waveform=100,
    instant_ai_max_reliable_polling_hz=10.0,
))
assert result.success
assert result.acquisition_mode == "instant_ai"
assert result.instant_ai_complete_waveforms == 3
assert result.instant_ai_planned_duration_seconds == pytest.approx(80.0)
```

设置连续缺 3 格时断言 `error_category == "coverage"` 且 `retryable is False`。

- [ ] **Step 2: 运行测试确认失败**

Run: `python -m pytest tests/test_measure.py -q`

Expected: 新 Mock 控制、结果字段和错误类别不存在。

- [ ] **Step 3: 扩展结果和错误分类**

`SamplingResult` 增加：

```cpp
InstantAi::TimedReadings instant_ai_readings;
int instant_ai_format_version = 0;
int instant_ai_complete_waveforms = 0;
double instant_ai_actual_duration_seconds = 0.0;
bool cancelled = false;
```

增加 `USER_CANCELLED`，以及 `Error::category(Code)`、`Error::retryable(Code)`。固定映射为：配置 `config/false`、覆盖 `coverage/false`、取消 `cancelled/false`、DAQ 读取 `read/true`、调度 `schedule/true`、对齐 `alignment/true`、波形数 `waveform_count/true`。`to_query()` 始终输出类别、retryable 和 cancelled。

- [ ] **Step 4: 生成连续 Mock 数据**

Instant Mock 使用连续调度，通过 `mock_phase_offset` 改变起相，以现有指数模型生成电压并写入单个 `instant_ai_readings`。新增 `mock_missing_bin_start` 和 `mock_missing_bin_count`。Mock 不等待；Buffered 分支不改。

- [ ] **Step 5: 处理器按版本选择重建器**

```cpp
if (config.is_instant() && result.instant_ai_format_version >= 2) {
    reconstructed = InstantAi::reconstruct_continuous(
        result.instant_ai_readings, config.number_of_waveforms,
        config.emitting_frequency, config.instant_ai_target_points_per_waveform);
} else if (config.is_instant()) {
    reconstructed = InstantAi::reconstruct_legacy_waveforms(
        result.instant_ai_waveforms, config.number_of_waveforms,
        config.emitting_frequency, config.instant_ai_target_points_per_waveform);
}
```

将重建状态逐一映射到错误码；成功时写入完整波形数、插值数、迟到数和半波。

- [ ] **Step 6: 验证并提交**

```powershell
scons sample.exe sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py tests/test_estimate.py -q
git add src/result/sampling_result.hpp src/error/error.hpp src/error/error.cpp src/sampler/mock_sampler.cpp src/processor/processor.cpp src/commander/measure.cpp sample/result.py tests/test_measure.py
git commit -m "feat: integrate continuous Instant AI processing"
```

## Task 4: 实现真实连续读取、线程安全进度和取消

**Files:**

- Modify: `src/result/sampling_result.hpp`
- Modify: `src/sampler/real_sampler.hpp`
- Create: `src/sampler/real_sampler.cpp`
- Modify: `src/commander/measure.hpp`
- Modify: `src/commander/measure.cpp`
- Modify: `src/commander/commander.cpp`
- Modify: `SConstruct`
- Modify: `src/CMakeLists.txt`
- Create: `tests/sample_instant_ai/test_progress.cpp`
- Modify: `tests/sample_instant_ai/test_main.cpp`

- [ ] **Step 1: 写进度 reset/cancel 失败测试**

```cpp
Result::SamplingProgress progress;
progress.reset(40.0, 2);
assert(progress.planned_milliseconds.load() == 40000);
assert(progress.target_cycles.load() == 2);
assert(!progress.cancel_requested.load());
progress.cancel_requested.store(true);
assert(progress.cancel_requested.load());
```

- [ ] **Step 2: 运行编译确认失败**

Run: `scons sample_instant_ai_unit_tests.exe`

Expected: `SamplingProgress` 不存在。

- [ ] **Step 3: 添加线程安全进度**

```cpp
struct SamplingProgress {
    std::atomic<long long> planned_milliseconds{0};
    std::atomic<long long> elapsed_milliseconds{0};
    std::atomic<int> completed_cycles{0};
    std::atomic<int> target_cycles{0};
    std::atomic<int> successful_reads{0};
    std::atomic<int> late_reads{0};
    std::atomic<bool> cancel_requested{false};
    void reset(double planned_seconds, int cycles);
};
```

使用整数毫秒避免原子 double 兼容问题。`clear_measure_data()` 调用 `reset()`，不整体赋值原子结构。

- [ ] **Step 4: 移出 RealSampler 实现并加入 RAII**

将头文件内实现移到 `real_sampler.cpp`。使用局部控制器所有者：

```cpp
template <typename Controller>
class ControllerOwner {
  public:
    explicit ControllerOwner(Controller* value) : value_(value) {}
    ~ControllerOwner() { if (value_) value_->Dispose(); }
    Controller* operator->() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }
  private:
    Controller* value_;
};
```

Buffered 函数体原样迁移，只用 RAII 替代手工 `Dispose()`。同步更新 SCons 和 CMake 的 Windows source。

- [ ] **Step 5: 实现真实连续读取**

一次创建 `InstantAiCtrl`，按连续调度逐点 `sleep_until` 和 `ReadAny`。每次读取前检查：

```cpp
if (result.progress.cancel_requested.load()) {
    result.error_code = Error::USER_CANCELLED;
    result.cancelled = true;
    return false;
}
```

成功后追加读数并更新实际时长、成功数、迟到数和已识别周期数。DAQ 失败时追加 `read_success=false` 的记录并保留原生错误码。所有出口由 RAII 释放控制器。

- [ ] **Step 6: 增加命令**

注册 `to_sampling_progress` 和 `to_cancel_sampling`。进度命令从原子字段 `load()` 到局部变量后用 `Base::variable` 输出 planned/elapsed seconds、completed/target cycles、successful/late reads；取消命令只执行 `cancel_requested.store(true)` 并立即返回。

- [ ] **Step 7: 验证并提交**

```powershell
scons sample.exe sample_instant_ai_unit_tests.exe daq_capability_test_legacy.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
git diff --check
git add src/result/sampling_result.hpp src/sampler/real_sampler.hpp src/sampler/real_sampler.cpp src/commander/measure.hpp src/commander/measure.cpp src/commander/commander.cpp SConstruct src/CMakeLists.txt tests/sample_instant_ai/test_progress.cpp tests/sample_instant_ai/test_main.cpp
git commit -m "feat: add cancellable Instant AI acquisition progress"
```

## Task 5: 引入 V2 原始文件并保留 V1/Buffered 兼容

**Files:**

- Modify: `src/sampler/sampler.hpp`
- Modify: `src/sampler/real_sampler.cpp`
- Modify: `src/commander/measure.cpp`
- Create: `tests/sample_instant_ai/test_dump_format.cpp`
- Modify: `tests/sample_instant_ai/test_main.cpp`
- Modify: `SConstruct`
- Modify: `tests/test_measure.py`

- [ ] **Step 1: 写三格式失败测试**

V2 首部精确断言：

```text
#HALF_SAMPLE_INSTANT_AI_V2
emitting_frequency=0.05
target_points=100
number_of_waveforms=1
max_reliable_polling_hz=10
planned_duration_seconds=40
actual_duration_seconds=40
planned_seconds,actual_seconds,voltage,read_success,read_error_code
```

测试 V2 全读数 round-trip；加载前预置垃圾数据，加载后确认已清空。再加载固定 V1 和无标记 Buffered 夹具并处理成功。

- [ ] **Step 2: 运行测试确认失败**

Run: `scons sample_instant_ai_unit_tests.exe; .\cpp_build\sample_instant_ai_unit_tests.exe`

Expected: 当前只输出 V1。

- [ ] **Step 3: 写 V2，并在失败时也保留证据**

`dump_origin_data()` 对新在线 Instant 写 V2 和所有 `TimedReading`。真实采样只要配置了 dump 路径，无论成功、DAQ 失败、超时还是取消，都在返回前写出截至当时的全部读数。

- [ ] **Step 4: 按显式标记加载三格式**

```cpp
if (first_line == "#HALF_SAMPLE_INSTANT_AI_V2") return load_instant_v2(...);
if (first_line == "#HALF_SAMPLE_INSTANT_AI_V1") return load_instant_v1(...);
iss.clear();
iss.seekg(0);
return load_buffered_legacy(...);
```

V2 用五参数 `config.update()` 恢复配置；V1 进入 `instant_ai_waveforms`；Buffered 写 vector 前先按配置 resize，避免越界。

- [ ] **Step 5: 保证 process 只依赖文件**

`to_process()` 先清空两种 Instant 容器、进度、波形和旧错误，再加载。V2 走连续重建，V1 走 legacy 重建，Buffered 走原路径。

- [ ] **Step 6: 验证并提交**

```powershell
scons sample.exe sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
python -m pytest tests/test_measure.py -q
git add src/sampler/sampler.hpp src/sampler/real_sampler.cpp src/commander/measure.cpp tests/sample_instant_ai/test_dump_format.cpp tests/sample_instant_ai/test_main.cpp SConstruct tests/test_measure.py
git commit -m "feat: persist complete Instant AI V2 recordings"
```

## Task 6: 完成 Python 进度、取消和公开文档

**Files:**

- Modify: `sample/sample.py`
- Modify: `sample/result.py`
- Modify: `tests/test_measure.py`
- Modify: `README.md`

- [ ] **Step 1: 写 Python API 失败测试**

```python
assert client.sampling_progress().command == "to_sampling_progress"
assert client.cancel_sampling().command == "to_cancel_sampling"
assert result.cancelled is True
assert result.error_category == "cancelled"
assert result.retryable is False
```

- [ ] **Step 2: 运行测试确认失败**

Run: `python -m pytest tests/test_measure.py -q`

Expected: 两个 Python 方法和结果字段不存在。

- [ ] **Step 3: 实现方法和默认结果字段**

```python
def sampling_progress(self) -> Result:
    return self.communicate("to_sampling_progress")

def cancel_sampling(self) -> Result:
    return self.communicate("to_cancel_sampling")
```

`Result.__init__()` 增加 `error_category`、`retryable`、`cancelled`、`planned_seconds`、`elapsed_seconds`、`completed_cycles`、`target_cycles`、`successful_reads`、`late_reads`、`instant_ai_complete_waveforms`。

- [ ] **Step 4: 更新 README**

文档展示三个参数、`(N+1)/f`、进度轮询、取消、错误分类和三格式兼容。0.05 Hz、N=1 明确为 40 秒；说明错误不自动回退 Buffered。

- [ ] **Step 5: 验证并提交**

```powershell
python -m pytest tests/test_measure.py -q
git diff --check
git add sample/sample.py sample/result.py tests/test_measure.py README.md
git commit -m "feat: expose Instant AI progress and cancellation"
```

## Task 7: 全量验证、硬件验收和发布 0.1.20

**Files:**

- Modify: `setup.py`
- Verify: `appveyor.yml`
- Verify: `scripts/daq_validation.ps1`

- [ ] **Step 1: 运行完整软件回归**

```powershell
scons -j$env:NUMBER_OF_PROCESSORS
.\cpp_build\sample_instant_ai_unit_tests.exe
.\cpp_build\daq_capability_unit_tests.exe
python -m pytest -q
pycodestyle . --max-line-length=120 --exclude=src/3rdparty
```

Expected: 构建成功；C++ 返回 0；Python 全部通过或仅有既有硬件跳过；无新增样式错误。

- [ ] **Step 2: 做 Buffered 回归审计**

用 `git diff master --` 逐项确认 1/20 MHz、`RunOnce/GetData`、缓冲长度、波形寻找和拟合路径未改变，并记录结论。

- [ ] **Step 3: 执行 PCI-1714U 硬件矩阵**

默认 `threshold=0.1`、`P=100`、`R_max=10` 下，对 `0.1、0.05、0.02、0.01 Hz` 分别执行 `N=1、3`。每项保存 V2，确认时长、N 个完整波形、取消后可再次启动、离线 process 一致。任一失败即停止发布。

- [ ] **Step 4: 更新版本并验证包**

将 `setup.py` 改为：

```python
version="0.1.20",
```

Run:

```powershell
python setup.py sdist
python -m pip install --force-reinstall .
python -c "import sample; print(sample.sampler)"
```

Expected: `dist/half_sample-0.1.20.tar.gz` 存在且导入成功。

- [ ] **Step 5: 提交版本**

```powershell
git add setup.py
git commit -m "chore: bump version to 0.1.20"
```

- [ ] **Step 6: 标记、推送并确认 PyPI**

仅在软件与硬件验收通过后：

```powershell
git tag v0.1.20
git push origin HEAD
git push origin v0.1.20
```

等待 AppVeyor 后查询 PyPI JSON，确认 0.1.20 文件存在且可安装；失败时不开始 KDM3000 集成。
