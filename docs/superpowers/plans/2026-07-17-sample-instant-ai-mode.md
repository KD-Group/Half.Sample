# sample.exe Instant AI 自动选择实施计划

> **供执行人员使用：** 必须使用 `superpowers:subagent-driven-development`（推荐）或 `superpowers:executing-plans` 子技能，逐项实施本计划。各步骤使用复选框（`- [ ]`）跟踪进度。

**目标：** 为 `sample.exe` 增加仅支持 legacy 的 Instant AI 路径，通过外部发射频率阈值选择模式，默认按用户要求的次数重建每个100点的等效波形，并保留现有 Buffered AI 行为。

**架构：** 在 `SamplingConfig` 中保存模式选择和等效采样元数据。将 Instant AI 调度与重建放入不依赖硬件的源文件，让真实采样器和 Mock 采样器都生成带时间戳的读数；只在采集和波形叠加环节进行模式分支，并复用现有估算器。尽可能原样保留 Buffered 分支，同时兼容旧命令和旧 dump 格式。

**技术栈：** 兼容 C++11 的 sample 核心（MSVC/SCons 当前使用 C++17）、Advantech legacy `bdaqctrl.h`、SCons、CMake/CLion 工程元数据、Python 包装层和 `unittest`。

**仓库约束：** 在当前 `master` 工作树中实施。用户运行并确认结果前不得提交；下面的检查点步骤有意替代技能通常要求的提交步骤。

---

## 文件结构

新建：

- `src/sampler/instant_ai.hpp`：带时间戳的读数、调度和重建接口。
- `src/sampler/instant_ai.cpp`：纯相位调度、覆盖验证、插值和波形对齐。
- `src/sampler/real_sampler.cpp`：Buffered/Instant控制器实现和legacy控制器清理。
- `tests/sample_instant_ai/test_main.cpp`：小型断言式测试运行器。
- `tests/sample_instant_ai/test_sampling_config.cpp`：模式选择和兼容性测试。
- `tests/sample_instant_ai/test_phase_schedule.cpp`：最小间隔和相位覆盖测试。
- `tests/sample_instant_ai/test_reconstruction.cpp`：插值、对齐和平均测试。

修改：

- `src/config/sampling_config.hpp/.cpp`：采集模式、可选阈值和目标点数配置。
- `src/result/sampling_result.hpp`：按用户要求的波形分组保存带时间戳的Instant AI读数。
- `src/commander/measure.cpp`：不会跨行的可选命令解析和模式相关缓冲区初始化。
- `src/sampler/real_sampler.hpp`：仅保留声明；实现移至 `.cpp`。
- `src/sampler/mock_sampler.cpp`：根据模式生成Mock数据。
- `src/sampler/sampler.hpp`：支持带版本的Instant dump/加载，同时保留legacy CSV加载。
- `src/processor/processor.cpp`：增加Instant重建/平均分支；Buffered分支保持不变。
- `src/error/error.hpp/.cpp`：Instant AI应用级错误。
- `sample/sample.py`：公开的可选参数。
- `SConstruct`：新增源文件和单元测试目标。
- `src/CMakeLists.txt`：向CLion公开新增源文件和测试。
- `tests/test_measure.py`：包装层兼容性和Instant集成测试。
- `README.md`：记录参数、选择规则和耗时示例。

## 任务1：配置和模式选择

**文件：**

- 修改：`src/config/sampling_config.hpp`
- 修改：`src/config/sampling_config.cpp`
- 新建：`tests/sample_instant_ai/test_main.cpp`
- 新建：`tests/sample_instant_ai/test_sampling_config.cpp`
- 修改：`SConstruct`
- 修改：`src/CMakeLists.txt`

- [ ] **步骤1：添加会失败的配置测试**

定义由 `test_main.cpp` 调用的测试函数：

```cpp
void test_sampling_config();

int main() {
    test_sampling_config();
    return 0;
}
```

覆盖以下确切情况：

```cpp
Config::SamplingConfig config;

assert(config.update(3, 0.1, 0.0, 100));
assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
assert(config.sampling_frequency == Constant::MinSamplingFrequency);

assert(config.update(3, 10.0, 0.0, 100));
assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);
assert(config.sampling_frequency == Constant::MaxSamplingFrequency);

assert(config.update(3, 0.5, 0.5, 100));
assert(config.acquisition_mode == Config::AcquisitionMode::Instant);
assert(config.waveform_length == 100);
assert(config.valid_length == 50);
assert(std::abs(config.sampling_interval - 20000.0) < 1e-9);

assert(config.update(3, 0.5001, 0.5, 100));
assert(config.acquisition_mode == Config::AcquisitionMode::Buffered);

assert(!config.update(0, 0.1, 0.5, 100));
assert(!config.update(3, 0.0, 0.5, 100));
assert(!config.update(3, 0.1, 0.5, 19));
```

Instant目标点数的最小允许值为20：包含10个半波拟合点，以及至少5个上升沿前基线点。

- [ ] **步骤2：添加单元测试构建目标并验证失败**

使用测试运行器、配置实现以及后续的Instant纯逻辑源文件，在SCons中添加 `sample_instant_ai_unit_tests.exe`。将相同文件添加到CMake测试可执行程序，使CLion能够解析符号。

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
```

预期：编译失败，因为 `AcquisitionMode` 和四参数 `update()` 尚不存在。

- [ ] **步骤3：实现配置模型**

添加：

```cpp
enum class AcquisitionMode {
    Buffered,
    Instant,
};

struct SamplingConfig {
    AcquisitionMode acquisition_mode = AcquisitionMode::Buffered;
    double instant_ai_frequency_threshold = 0.0;
    int instant_ai_target_points_per_waveform = 100;
    double instant_ai_min_read_interval_seconds = 0.1;

    bool update(int waveforms, double frequency,
                double instant_threshold = 0.0,
                int instant_target_points = 100);
    bool is_instant() const { return acquisition_mode == AcquisitionMode::Instant; }
};
```

验证波形数量和发射频率均为正数。仅在选择Instant模式时验证20点下限。只有满足 `threshold > 0 && frequency <= threshold` 时才选择Instant。

将当前 `SamplingConfig::update()` 函数体原样移入Buffered分支。在Instant分支中设置：

```cpp
sampling_frequency = emitting_frequency * instant_ai_target_points_per_waveform;
sampling_interval = 1e6 / sampling_frequency;
waveform_length = instant_ai_target_points_per_waveform;
valid_length = waveform_length / 2;
sampling_length_per_sample = waveform_length * number_of_waveforms;
waveforms_per_sample = 1.0;
sampling_time = 1;
```

- [ ] **步骤4：运行配置测试**

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
```

预期：退出码为0。

- [ ] **步骤5：建立检查点但不提交**

运行 `git diff --check` 并记录任务1通过。不要提交。

## 任务2：向后兼容的命令和Python API

**文件：**

- 修改：`src/commander/measure.cpp`
- 修改：`sample/sample.py`
- 修改：`tests/test_measure.py`
- 修改：`tests/sample_instant_ai/test_sampling_config.cpp`

- [ ] **步骤1：添加会失败的解析器和包装层测试**

提取具有以下接口的纯辅助函数：

```cpp
struct MeasureOptions {
    double instant_ai_frequency_threshold = 0.0;
    int instant_ai_target_points_per_waveform = 100;
};

bool parse_measure_options(const std::string& line_tail, MeasureOptions& options);
```

测试空文本、仅阈值、两个字段、额外字段和无效数字。空文本必须保留 `0.0/100`；存在额外字段或格式错误的值时必须失败。

在Python中模拟 `communicate()` 并断言：

```python
sampler.measure(3, 0.1)
# "to_measure 3 0.1 False"

sampler.measure(3, 0.1, instant_ai_frequency_threshold=0.5)
# "to_measure 3 0.1 False 0.5 100"
```

对 `dump()` 执行相同测试。

- [ ] **步骤2：运行测试并验证失败**

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
python -m unittest tests.test_measure -v
```

预期：由于缺少解析器符号/签名和Python可选参数而失败。

- [ ] **步骤3：实现不会跨行读取的安全解析**

读取现有必需字段后，只调用一次 `std::getline(std::cin, line_tail)`。仅使用 `std::istringstream` 解析该字符串；禁止直接从 `std::cin` 探测可选字段。

在 `async_measure()` 和 `to_config()` 中使用该辅助函数。`to_dump()` 继续先读取文件名，再委托给同一个测量参数解析器。如果解析或 `config.update()` 失败，应在线程启动前报告新的配置无效错误。

- [ ] **步骤4：扩展Python接口且不改变旧命令文本**

使用以下函数签名：

```python
def measure(self, number_of_waveforms: int, emitting_frequency: float,
            auto_mode: bool = False,
            instant_ai_frequency_threshold: float = 0.0,
            instant_ai_target_points_per_waveform: int = 100) -> Result:

def dump(self, filename: str, number_of_waveforms: int, emitting_frequency: float,
         auto_mode: bool = False,
         instant_ai_frequency_threshold: float = 0.0,
         instant_ai_target_points_per_waveform: int = 100) -> Result:
```

当阈值为0且目标为100时，严格输出原命令；否则同时追加两个可选字段。使用 `"{:.12g}"` 格式化频率，避免亚赫兹数值被舍入到两位小数。

- [ ] **步骤5：运行解析器和Python测试**

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
python -m unittest tests.test_measure -v
```

预期：所有测试通过。

- [ ] **步骤6：建立检查点但不提交**

运行 `git diff --check`。不要提交。

## 任务3：纯相位调度器

**文件：**

- 新建：`src/sampler/instant_ai.hpp`
- 新建：`src/sampler/instant_ai.cpp`
- 新建：`tests/sample_instant_ai/test_phase_schedule.cpp`
- 修改：`tests/sample_instant_ai/test_main.cpp`
- 修改：`SConstruct`
- 修改：`src/CMakeLists.txt`

- [ ] **步骤1：编写会失败的调度测试**

定义：

```cpp
namespace Sampler {
namespace InstantAi {

struct Schedule {
    std::vector<double> planned_seconds;
    double deadline_seconds;
};

Schedule build_schedule(double emitting_frequency,
                        int target_points,
                        double minimum_interval_seconds);

int phase_bin(double seconds, double emitting_frequency, int target_points);

} // namespace InstantAi
}
```

对于100点的 `0.1`、`0.5` 和 `1.0 Hz`，断言：

- 恰好包含100次计划调用；
- 在浮点容差内，每个相邻间隔都至少为100ms；
- 覆盖全部100个相位区间；
- 最后一个计划时间约为10秒或更短；
- 截止时间等于计划时长加 `max(1秒, 10%)`。

增加 `0.05 Hz` 情况，断言完整相位覆盖约需20秒。

- [ ] **步骤2：运行并验证失败**

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
```

预期：由于缺少 `instant_ai.hpp` 或调度函数未定义而失败。

- [ ] **步骤3：实现“最早未覆盖区间”调度**

对每个尚未填充的区间，计算其中心相位在
`last_planned + minimum_interval` 时刻或之后的下一次出现时间。选择最早的候选时间，将其追加到计划中，并将对应区间标记为已覆盖。

使用：

```cpp
const double period = 1.0 / emitting_frequency;
const double bin_phase_time = (bin + 0.5) * period / target_points;
const double earliest = planned.empty() ? 0.0 : planned.back() + minimum_interval_seconds;
const double cycles = std::max(0.0, std::ceil((earliest - bin_phase_time) / period));
const double candidate = bin_phase_time + cycles * period;
```

候选时间相同时选择索引较小的区间。在最后一次计划调用后计算截止时间。频率、点数或间隔不是正数时抛出 `std::invalid_argument`。

- [ ] **步骤4：运行调度测试**

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
```

预期：退出码为0，并且相邻计划间隔均不小于100ms。

- [ ] **步骤5：建立检查点但不提交**

运行 `git diff --check`。不要提交。

## 任务4：Instant波形重建

**文件：**

- 修改：`src/sampler/instant_ai.hpp`
- 修改：`src/sampler/instant_ai.cpp`
- 修改：`src/result/sampling_result.hpp`
- 新建：`tests/sample_instant_ai/test_reconstruction.cpp`
- 修改：`tests/sample_instant_ai/test_main.cpp`

- [ ] **步骤1：添加会失败的重建测试**

定义共享数据结构：

```cpp
struct TimedReading {
    double planned_seconds;
    double actual_seconds;
    double voltage;
};

using TimedWaveform = std::vector<TimedReading>;

struct ReconstructionResult {
    bool success;
    std::vector<double> averaged_half_wave;
    int interpolated_bins;
    int late_reads;
};
```

测试：

1. 三个合成的0.1Hz指数波形重建为50个半波点。
2. 在平均之前，分别去除每个波形不同的直流偏移。
3. 连续缺失一个或两个区间时可以成功插值。
4. 连续缺失三个区间时失败。
5. 没有从下阈值到上阈值跳变的波形失败。
6. 请求三个波形但只传入两个波形时失败。
7. 计划中的相位偏移不计为迟到；实际启动迟到超过阈值时计数。

- [ ] **步骤2：运行并验证失败**

运行单元测试目标，预期因重建函数未定义而失败。

- [ ] **步骤3：实现相位分区和插值**

将实际时间映射到循环相位区间：

```cpp
double cycles = (reading.actual_seconds - reference_seconds) * emitting_frequency;
double phase = cycles - std::floor(cycles);
int bin = std::min(target_points - 1, static_cast<int>(phase * target_points));
```

对同一区间内的多个读数求平均。循环扫描连续缺失区间。根据相位距离，使用两侧真实区间对长度为1或2的缺失段进行插值；缺失段更长时返回失败。

- [ ] **步骤4：实现对齐、基线计算和平均**

对每个重建后的完整波形：

- 计算最小值、最大值及现有的10%/40%阈值；
- 循环查找一个低于下阈值的观测点，以及其后的上阈值穿越点；
- 将上阈值穿越点旋转到索引0；
- 对上升沿前的 `min(10, max(5, target_points / 10))` 个点求平均，作为基线；
- 减去每个波形自己的基线；
- 累加前 `target_points / 2` 个采样点。

严格除以用户要求的波形数量。绝不能静默使用更少的波形进行平均。

- [ ] **步骤5：实现读取迟到证据统计**

满足以下条件时将读数计为迟到：

```cpp
actual_seconds - planned_seconds >
    std::max(0.020, planned_gap_seconds * 0.5)
```

不能只因一次迟到就拒绝波形；由覆盖规则判断是否仍有足够可用数据。

- [ ] **步骤6：运行重建测试**

运行：

```powershell
scons sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
```

预期：所有配置、调度和重建断言通过。

- [ ] **步骤7：建立检查点但不提交**

运行 `git diff --check`。不要提交。

## 任务5：真实legacy Instant AI采集

**文件：**

- 修改：`src/sampler/real_sampler.hpp`
- 新建：`src/sampler/real_sampler.cpp`
- 修改：`src/result/sampling_result.hpp`
- 修改：`src/error/error.hpp`
- 修改：`src/error/error.cpp`
- 修改：`SConstruct`
- 修改：`src/CMakeLists.txt`

- [ ] **步骤1：添加legacy API编译期覆盖测试**

添加仅限Windows的测试，获取以下带类型的函数指针：

```cpp
InstantAiCtrl* (*create_instant)() = &AdxInstantAiCtrlCreate;
ErrorCode (InstantAiCtrl::*read_any)(int32, int32, void*, double*) =
    &InstantAiCtrl::ReadAny;
```

目的是在误用XNavi类型或Instant API签名不正确时让编译失败。

- [ ] **步骤2：重构 `RealSampler` 且不改变Buffered行为**

将实现从 `real_sampler.hpp` 移至 `real_sampler.cpp`。添加以下私有方法：

```cpp
bool sample_buffered(const Config::SamplingConfig&, Result::SamplingResult&);
bool sample_instant(const Config::SamplingConfig&, Result::SamplingResult&);
```

`sample()` 根据 `config.acquisition_mode` 分派。将现有Buffered函数体复制到
`sample_buffered()`，不得改变采样率、采样点数、循环次数或输出布局。

使用一个小型局部RAII所有者，其析构函数对两种控制器调用 `Dispose()`，确保DAQ提前失败时也能释放设备。

仅在SCons现有的 `sys.platform == 'win32'` 分支中将 `real_sampler.cpp` 加入
`sample_sources`。在CMake中使用
`if (WIN32) target_sources(sample PRIVATE sampler/real_sampler.cpp) endif()`
添加该文件。纯逻辑文件 `instant_ai.cpp` 保持平台无关，并且必须能够以C++11编译。

- [ ] **步骤3：实现Instant控制器配置**

在 `sample_instant()` 中：

1. 调用 `AdxInstantAiCtrlCreate()` 创建控制器；
2. 拒绝空控制器；
3. 选择 `DeviceInformation(L"PCI-1714,BID#0")`；
4. 将通道0设置为 `V_Neg5To5`；
5. 为用户要求的每个波形创建一份调度；
6. 使用 `sleep_until` 等待 `steady_clock` 绝对计划时间；
7. 调用 `ReadAny(0, 1, nullptr, &voltage)`；
8. 记录计划相对时间、实际相对时间和电压；
9. `BioFailed(code)` 时立即停止；
10. 截止时间到达但覆盖仍不足时拒绝该波形。

唤醒迟到后不得突发执行补偿读取。

- [ ] **步骤4：添加应用级错误**

为以下错误添加稳定的枚举和字符串映射：

```text
INVALID_INSTANT_AI_CONFIG
INSTANT_AI_COVERAGE_INSUFFICIENT
INSTANT_AI_SCHEDULE_TIMEOUT
INSTANT_AI_ALIGNMENT_FAILED
INSTANT_AI_WAVEFORM_COUNT_INSUFFICIENT
```

控制器或读取失败时，继续转换并原样传播DAQ原生错误码。

- [ ] **步骤5：构建所有Windows目标**

运行：

```powershell
scons sample.exe sample_instant_ai_unit_tests.exe daq_capability_test_legacy.exe
```

预期：三个可执行程序均构建成功。

- [ ] **步骤6：运行纯逻辑测试**

运行：

```powershell
.\cpp_build\sample_instant_ai_unit_tests.exe
.\cpp_build\daq_capability_unit_tests.exe
```

预期：退出码为0。

- [ ] **步骤7：建立检查点但不提交**

运行 `git diff --check`。不要提交。

## 任务6：处理程序与Mock集成

**文件：**

- 修改：`src/sampler/mock_sampler.cpp`
- 修改：`src/processor/processor.cpp`
- 修改：`src/commander/measure.cpp`
- 修改：`tests/test_measure.py`
- 修改：`tests/test_estimate.py`

- [ ] **步骤1：添加会失败的Instant集成测试**

Configure `mock_noise=0` for deterministic assertions. Request:

```python
sampler.measure(
    number_of_waveforms=3,
    emitting_frequency=0.1,
    instant_ai_frequency_threshold=0.5,
    instant_ai_target_points_per_waveform=100,
)
```

等待完成后，断言测量成功、返回约50个拟合波形点，并且间隔接近：

```text
1e6 / (0.1 * 100) = 100000 us
```

增加请求1个和5个波形的情况，断言Mock报告并重建的数量完全一致。增加阈值边界测试，以及通过Mock失败标志强制产生Instant错误且不回退到Buffered的测试。

在Instant测试之前，保留0.5、10和200Hz且不传阈值的代表性Buffered断言。

- [ ] **步骤2：运行集成测试并验证失败**

运行：

```powershell
python -m unittest tests.test_measure tests.test_estimate -v
```

预期：Instant请求失败或错误地进入旧的高密度Mock路径。

- [ ] **步骤3：生成带时间戳的Instant Mock数据**

当 `config.is_instant()` 为真时：

- 调用真实的纯逻辑调度构建器；
- 在每个计划/实际相位，根据现有Mock指数方程生成电压；
- 向 `actual_seconds` 添加确定且可配置的抖动；
- 将读数严格分为 `number_of_waveforms` 组波形；
- 通过现有 `set_sampler_value()` 支持仅用于测试的失败和缺口控制。

Mock模式不得等待。保持现有Buffered数据生成分支不变。

- [ ] **步骤4：为 `Processor::summation()` 增加模式分支**

将当前函数体移入 `summation_buffered()`，不得改变行为。添加
`summation_instant()`，调用纯逻辑重建器，将平均后的半波写入
`result.resultWave`，并把重建失败映射为新的应用级错误。

在 `clear_measure_data()` 中保留现有Buffered内存分配。对于Instant，清空带时间戳的分组，并将 `resultWave` 调整为 `valid_length`。

- [ ] **步骤5：保护低密度波形的快速下降检查**

仅修改检查点数量的计算：

```cpp
const int rapidDeclineCheckPoints =
    std::max(1, static_cast<int>(
        merged_length * Constant::RapidDeclineCheckPointsPercentage));
```

增加一个50点Instant半波测试。确认现有Buffered测试结果仍处于当前容差范围内。

- [ ] **步骤6：运行集成测试和回归测试**

运行：

```powershell
scons sample.exe
python -m unittest discover -s tests -p "test_*.py" -v
```

预期：所有现有测试和新测试通过；Instant Mock测试无需真实等待并立即完成。

- [ ] **步骤7：建立检查点但不提交**

运行 `git diff --check`。不要提交。

## 任务7：带版本标记的Instant dump与重放

**文件：**

- 修改：`src/sampler/sampler.hpp`
- 修改：`src/commander/measure.cpp`
- 修改：`tests/sample_instant_ai/test_reconstruction.cpp`
- 修改：`tests/test_measure.py`

- [ ] **步骤1：添加会失败的往返读写测试**

写入一个临时Instant dump，并断言首行为：

```text
#HALF_SAMPLE_INSTANT_AI_V1
```

要求包含模式、发射频率、目标点数和波形数量的元数据行，随后为：

```text
waveform_index,planned_seconds,actual_seconds,voltage
```

加载文件，并在浮点容差内比较每个时间戳和数值。同时加载现有的无标记逗号分隔Buffered测试夹具，断言其数值保持不变。

- [ ] **步骤2：运行并验证失败**

运行C++单元测试目标和Python dump/process测试。预期当前仅包含电压的dump格式无法满足Instant断言。

- [ ] **步骤3：实现显式格式识别**

修改加载器签名，使其接收可修改的配置：

```cpp
static bool load_origin_data(Config::SamplingConfig& config,
                             Result::SamplingResult& result);
```

如果首行等于Instant标记，则先解析并验证元数据，再读取采样值；否则回到文件开头，使用现有的逗号分隔Buffered加载器。不得根据列数推断模式。

- [ ] **步骤4：通过正常处理流程重放**

对于Instant文件，恢复 `acquisition_mode`、等效采样元数据和带时间戳的读数，再执行 `align → summation → estimate`。对于legacy文件，保留当前行为。

- [ ] **步骤5：运行dump兼容性测试**

运行：

```powershell
.\cpp_build\sample_instant_ai_unit_tests.exe
python -m unittest tests.test_measure -v
```

预期：Instant往返读写测试和legacy Buffered测试夹具均通过。

- [ ] **步骤6：建立检查点但不提交**

运行 `git diff --check`。不要提交。

## 任务8：文档、格式化和完整验证

**文件：**

- 修改：`README.md`
- 修改：`clang-format` 报告的所有自有源文件

- [ ] **步骤1：记录公开行为**

在README中添加以下示例：

```python
sampler.measure(3, 0.1, instant_ai_frequency_threshold=0.5)
sampler.measure(5, 0.1,
                instant_ai_frequency_threshold=0.5,
                instant_ai_target_points_per_waveform=500)
```

说明：

- 频率等于阈值时选择Instant；
- 阈值为0时保持Buffered；
- 目标点数默认为100；
- 平均次数来自 `number_of_waveforms`；
- 0.1Hz、3个波形的预计主体时间约为30秒；
- 更低频率不可能比真实周期更快完成；
- 错误不会触发自动回退；
- 仅支持legacy。

- [ ] **步骤2：仅格式化项目自有代码**

使用仓库配置的clang-format格式化 `src`、新增sample测试及其他项目自有C/C++文件。排除：

```text
src/3rdparty
src/daq_headers
```

运行：

```powershell
git diff --check
```

预期：没有空白字符错误。

- [ ] **步骤3：使用SCons构建**

运行：

```powershell
scons sample.exe sample_instant_ai_unit_tests.exe daq_capability_test_mock.exe daq_capability_test_legacy.exe
```

预期：所有指定目标构建成功。

- [ ] **步骤4：运行C++测试**

运行：

```powershell
.\cpp_build\sample_instant_ai_unit_tests.exe
.\cpp_build\daq_capability_unit_tests.exe
```

预期：两个测试程序都返回退出码0。

- [ ] **步骤5：运行Python测试**

运行：

```powershell
python -m unittest discover -s tests -p "test_*.py" -v
```

预期：所有测试通过。

- [ ] **步骤6：复查Buffered兼容性差异**

检查以下文件中的Buffered分支：

```text
src/config/sampling_config.cpp
src/sampler/real_sampler.cpp
src/sampler/mock_sampler.cpp
src/processor/processor.cpp
```

确认原有1MHz/20MHz选择、缓冲区大小、`RunOnce/GetData`、300点基线和估算频率上限均未改变。

- [ ] **步骤7：保留未提交的交付状态**

运行：

```powershell
git status --short
git diff --stat
```

报告修改文件、构建/测试证据和准确的硬件命令。不要提交。

## 任务9：Python 3.6 CI兼容性和本地等价验证

**文件：**

- 新建：`tests/daq_capability_test/subprocess_compat.py`
- 新建：`tests/daq_capability_test/test_python36_compat.py`
- 修改：`tests/daq_capability_test/test_mock_cli.py`
- 修改：`tests/daq_capability_test/test_legacy_demo_integration.py`
- 修改：`tests/daq_capability_test/test_xnavi_demo_integration.py`
- 修改：`tests/daq_capability_test/test_plot_instant_ai_samples.py`
- 修改：`appveyor.yml`

- [ ] **步骤1：将CI失败固化为兼容性回归测试**

AppVeyor环境固定使用Python 3.6.8。该版本的 `subprocess.run()` 不接受
`capture_output` 或 `text` 别名。添加基于AST的测试，扫描可执行的Python测试和脚本文件；
只要向 `subprocess.run()` 传入任意一个不兼容关键字，测试就必须失败：

```python
import ast
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def test_subprocess_run_uses_python36_compatible_keywords():
    violations = []
    roots = (ROOT / "tests", ROOT / "scripts")
    for source_root in roots:
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
    assert not violations, "\n".join(violations)
```

- [ ] **步骤2：运行聚焦测试并验证当前失败**

运行：

```powershell
python -m pytest tests/daq_capability_test/test_python36_compat.py -q
```

预期：测试失败，并列出当前使用 `capture_output` 和/或 `text` 的四个DAQ测试模块。

- [ ] **步骤3：添加统一的Python 3.6兼容子进程辅助函数**

实现：

```python
import subprocess


def run_captured(arguments, **kwargs):
    kwargs["stdout"] = subprocess.PIPE
    kwargs["stderr"] = subprocess.PIPE
    kwargs["universal_newlines"] = True
    kwargs.setdefault("encoding", "utf-8")
    return subprocess.run(arguments, **kwargs)
```

将所有受影响的测试调用替换为 `run_captured(...)`。严格保留 `cwd`、`timeout`、
`check`、`env` 和命令参数。只删除不兼容的
`capture_output=True` 和 `text=True` 关键字。

- [ ] **步骤4：使用本地Python验证兼容性修复**

运行：

```powershell
python -m pytest tests/daq_capability_test/test_python36_compat.py `
    tests/daq_capability_test/test_mock_cli.py `
    tests/daq_capability_test/test_legacy_demo_integration.py `
    tests/daq_capability_test/test_xnavi_demo_integration.py `
    tests/daq_capability_test/test_plot_instant_ai_samples.py -q
```

预期：不再出现 `TypeError: __init__() got an unexpected keyword argument
'capture_output'`；测试通过，或者按现有逻辑在设备不可用时跳过。

- [ ] **步骤5：明确CI的Python版本约束**

保持AppVeyor使用 `C:\Python36`，并在测试前添加诊断断言：

```yaml
before_test:
  - python --version
  - python -c "import sys; assert sys.version_info[:2] == (3, 6), sys.version"
  - scons --version
```

这样可以防止未来CI镜像变化掩盖Python 3.6兼容性回归。

- [ ] **步骤6：运行与CI完全一致的本地测试命令**

从仓库根目录运行：

```powershell
pycodestyle . --max-line-length=120 --exclude=src/3rdparty
python -m pytest
```

预期：代码风格检查成功，收集到的所有测试通过，仅允许已有说明的设备不可用跳过。

- [ ] **步骤7：运行与CI完全一致的本地构建和打包命令**

运行：

```powershell
scons -j$env:NUMBER_OF_PROCESSORS
python setup.py sdist
```

如果本地 `NUMBER_OF_PROCESSORS` 为空，则使用：

```powershell
scons -j1
```

预期：所有SCons目标构建成功，并在 `dist` 目录生成源码分发包。

- [ ] **步骤8：在具备条件时使用Python 3.6验证**

当前开发机只有Python 3.13，因此AST检查是本地兼容性保障。在AppVeyor或任何具有
`C:\Python36\python.exe` 的机器上运行：

```cmd
C:\Python36\python.exe -m pytest
```

预期：完整测试集通过，不再出现不兼容的子进程关键字参数错误。

- [ ] **步骤9：建立检查点但不提交**

运行 `git diff --check` 和 `git status --short`，并记录完整的本地CI输出。不要提交。

## 任务10：传输二进制文件后的远程硬件验证

**文件：**

- 除非验证发现缺陷，否则不修改源文件。

- [ ] **步骤1：确认设备未被占用**

关闭占用采集卡的Advantech官方工具，以及此前启动的 `sample.exe` 或验证程序。

- [ ] **步骤2：直接启动二进制程序**

运行：

```cmd
D:\test\sample.exe
```

在程序命令提示符中输入：

```text
set_sampler real_sampler
to_measure 3 0.1 False 0.5 100
```

使用以下命令轮询：

```text
is_measuring
```

完成后输入：

```text
to_query
```

预期：选择Instant模式，主体采集约30秒，测量成功，并得到约50个半波拟合点。

- [ ] **步骤3：验证Buffered模式选择**

Enter:

```text
to_measure 3 1 False 0.5 100
```

预期：由于1Hz高于0.5Hz外部模式阈值，选择Buffered AI；同时1Hz仍低于10Hz，因此原有Buffered内部规则选择1MHz。

- [ ] **步骤4：验证频率等于阈值的情况**

Enter:

```text
to_measure 3 0.5 False 0.5 100
```

预期：选择Instant AI，因为等于阈值归入低频模式。

- [ ] **步骤5：验证失败传播**

让官方工具占用设备，然后重复Instant测量。

预期：立即返回原生的设备忙或设备不可用DAQ错误，并且程序不会改用Buffered AI重试。

- [ ] **步骤6：比较重复性**

至少运行三次0.1Hz Instant测量。保存原始dump，并比较τ、电压跨度、迟到读取次数、插值次数和相位覆盖率。硬件验证结果需要用户单独确认；确认前不得提交。
