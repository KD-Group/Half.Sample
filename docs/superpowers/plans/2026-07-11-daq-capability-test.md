# 采集卡能力测试实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不改变正式采样行为的前提下，增加 legacy/XNavi 两个独立的 PCI-1714U 能力测试程序，支持单项验证、一键 suite、明确退出码/JSON 结果，以及 N 个独立完整周期的在线和离线重建验证。

**Architecture:** 公共核心负责配置、命令、判定、输出和相位重建，两个薄适配器分别包含 legacy 与 XNavi 头文件并构建为独立 EXE。硬件访问通过 `DaqAdapter` 隔离，无硬件测试使用 `FakeDaqAdapter`；所有正式结论由退出码和 stdout 末行 JSON 给出。

**Tech Stack:** C++11、Windows DAQNavi/BioDAQ、SCons、Python unittest、PowerShell、TSV、单行 JSON。

---

## 执行与提交约束

- 直接在 `D:\kunde\code\Half.Sample` 当前工作区实现，不创建 git worktree。
- 开始实现前记录当前 `HEAD` 为实现基线；不得修改或提交用户已有的 `1.txt`、`mock.csv`、`half_sample.egg-info/` 和 `tests/_trial_temp/`。
- 实现过程中允许按任务创建临时提交，便于回退和审查。
- 全部验证完成后，只把实现基线之后的本任务提交 squash 成一个最终提交，主题使用 `feat: add DAQ capability validation tool`。
- squash 前确认实现基线之后没有用户或其他任务的提交；若存在，停止并报告，不能改写他人提交。

## 文件结构

**移动：**

- `src/sampler/bdaqctrl.h` -> `src/daq_headers/legacy/bdaqctrl.h`：旧版 DAQNavi 头文件。

**复制：**

- `D:\DAQNavi\Inc\bdaqctrl.h` -> `src/daq_headers/xnavi/bdaqctrl.h`：XNavi 4.x 头文件快照。

**修改：**

- `src/sampler/real_sampler.hpp`：仅修改旧头文件 include 路径。
- `SConstruct`：显式列出正式程序、新增两个测试程序和无硬件测试 target 的源文件。
- `.gitignore`：忽略现场配置和结果目录。
- `README.md`：增加构建、配置和完整现场 Check-list。

**创建：**

- `src/daq_capability_test/types.hpp`：配置、采集片段、能力、证据和结果类型。
- `src/daq_capability_test/result_codes.hpp`：稳定的结果 code 与退出码。
- `src/daq_capability_test/json_result.cpp/.hpp`：stdout 末行 JSON 序列化。
- `src/daq_capability_test/matrix.cpp/.hpp`：带注释 TSV 的解析与 preflight。
- `src/daq_capability_test/daq_adapter.hpp`：驱动无关接口。
- `src/daq_capability_test/fake_daq_adapter.cpp/.hpp`：无硬件测试适配器。
- `src/daq_capability_test/adapter_factory.hpp`：各 target 创建唯一适配器的公共工厂声明。
- `src/daq_capability_test/mock_adapter_factory.cpp`：mock EXE 的适配器工厂和场景选择。
- `src/daq_capability_test/acquisition_runner.cpp/.hpp`：重复采集与基础判定。
- `src/daq_capability_test/phase_stitcher.cpp/.hpp`：边沿解析、相位覆盖、片段分配、漂移与重建判定。
- `src/daq_capability_test/result_writer.cpp/.hpp`：TSV、日志、原始片段和配置快照输出。
- `src/daq_capability_test/legacy_adapter.cpp`：旧版 DAQNavi 适配器。
- `src/daq_capability_test/xnavi_adapter.cpp`：新版 DAQNavi 适配器。
- `src/daq_capability_test/suite_runner.cpp/.hpp`：依赖顺序、`--all/--case/--from` 和方案决策。
- `src/daq_capability_test/cli.cpp/.hpp`、`main.cpp`：CLI 入口。
- `src/daq_capability_test/default_test_matrix.tsv`：带注释和默认值的示例矩阵。
- `src/daq_capability_test/mock_success.tsv`：完整成功路径的确定性 mock 配置。
- `src/daq_capability_test/mock_non_stationary.tsv`：必须返回 `NON_STATIONARY_RESPONSE` 的漂移场景。
- `src/daq_capability_test/mock_failures.tsv`：短读、溢出、边沿缺失、触发失败和 delay 错误场景。
- `tests/daq_capability_test/test_main.cpp`：轻量 C++ 测试入口。
- `tests/daq_capability_test/test_matrix.cpp`、`test_results.cpp`、`test_acquisition.cpp`、`test_phase_stitcher.cpp`、`test_suite.cpp`：无硬件回归测试。

## Task 1：确认当前工作区并记录实现基线

**Files:**
- Inspect: `SConstruct`
- Inspect: `src/sampler/real_sampler.hpp`
- Inspect: `src/sampler/bdaqctrl.h`
- Inspect: `tests/test_sampler.py`

- [ ] **Step 1：确认当前工作区并记录基线提交**

Run:

```powershell
Set-Location D:\kunde\code\Half.Sample
git rev-parse --show-toplevel
git rev-parse HEAD | Set-Content -NoNewline .git\daq-capability-implementation-base
Get-Content .git\daq-capability-implementation-base
git status --short
```

Expected: 仓库根目录为 `D:/kunde/code/Half.Sample`；基线文件包含一个完整提交哈希；status 只包含已知用户未跟踪文件。基线文件位于 `.git`，不会进入提交。

- [ ] **Step 2：验证当前构建基线**

Run:

```powershell
scons -Q
python -m unittest tests.test_sampler -v
```

Expected: `cpp_build/sample.exe` 构建成功，`tests.test_sampler` 全部通过。若当前环境找不到 MSVC/SCons，记录准确错误并先修复工具链，不修改功能代码绕过。

- [ ] **Step 3：记录干净差异**

Run:

```powershell
git status --short
git diff --check
```

Expected: 只显示用户原有未跟踪文件，没有实现改动。

## Task 2：隔离新旧 DAQNavi 头文件

**Files:**
- Move: `src/sampler/bdaqctrl.h` -> `src/daq_headers/legacy/bdaqctrl.h`
- Create: `src/daq_headers/xnavi/bdaqctrl.h`
- Modify: `src/sampler/real_sampler.hpp`
- Modify: `SConstruct`

- [ ] **Step 1：写构建回归检查脚本**

Create `tests/daq_capability_test/test_header_layout.py`:

```python
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class HeaderLayoutTest(unittest.TestCase):
    def test_headers_are_versioned_and_unqualified_include_is_gone(self):
        self.assertTrue((ROOT / "src/daq_headers/legacy/bdaqctrl.h").is_file())
        self.assertTrue((ROOT / "src/daq_headers/xnavi/bdaqctrl.h").is_file())
        source = (ROOT / "src/sampler/real_sampler.hpp").read_text(encoding="utf-8")
        self.assertIn('daq_headers/legacy/bdaqctrl.h', source)
        self.assertNotIn('#include "bdaqctrl.h"', source)
```

- [ ] **Step 2：先运行测试并确认失败**

Run:

```powershell
python -m unittest tests.daq_capability_test.test_header_layout -v
```

Expected: FAIL，因为版本化目录尚不存在。

- [ ] **Step 3：移动旧头文件、复制新头文件并调整 include**

`real_sampler.hpp` 使用：

```cpp
#include "../daq_headers/legacy/bdaqctrl.h"
```

使用 `Move-Item` 前确认源和目标均位于 `D:\kunde\code\Half.Sample` 当前仓库。新版头文件必须来自已安装的 `D:\DAQNavi\Inc\bdaqctrl.h`，并记录 SHA-256 到提交说明。

- [ ] **Step 4：运行布局和正式构建回归**

Run:

```powershell
python -m unittest tests.daq_capability_test.test_header_layout -v
scons -Q
```

Expected: 测试 PASS，`sample.exe` 仍构建成功。

- [ ] **Step 5：提交头文件隔离**

```powershell
git add src/daq_headers src/sampler/real_sampler.hpp tests/daq_capability_test/test_header_layout.py
git commit -m "refactor: isolate DAQNavi headers"
```

## Task 3：定义稳定结果契约和 JSON 输出

**Files:**
- Create: `src/daq_capability_test/types.hpp`
- Create: `src/daq_capability_test/result_codes.hpp`
- Create: `src/daq_capability_test/json_result.hpp`
- Create: `src/daq_capability_test/json_result.cpp`
- Create: `tests/daq_capability_test/test_main.cpp`
- Create: `tests/daq_capability_test/test_results.cpp`

- [ ] **Step 1：写失败测试**

`test_results.cpp` 覆盖 JSON 转义、证据数值、PASS/FAIL/SKIP 和退出码：

```cpp
#include "daq_capability_test/json_result.hpp"
#include <cassert>

void test_result_json() {
    daq::CommandResult result;
    result.status = daq::Status::Fail;
    result.code = "CACHE_OVERFLOW";
    result.message = "overflow on repetition 2";
    result.evidence["repetition"] = "2";
    assert(daq::exit_code(result) == 6);
    assert(daq::to_json(result) ==
        "{\"result\":\"FAIL\",\"code\":\"CACHE_OVERFLOW\","
        "\"message\":\"overflow on repetition 2\",\"evidence\":{\"repetition\":\"2\"}}");
}
```

`test_main.cpp` 调用各测试函数并在成功时返回 0。

- [ ] **Step 2：构建测试并确认失败**

临时使用 MSVC 或先在 SConstruct 添加仅包含该测试的 `daq_capability_unit_tests` target。Run:

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
```

Expected: FAIL，缺少结果类型和序列化实现。

- [ ] **Step 3：实现最小结果类型**

定义：

```cpp
namespace daq {
enum class Status { Pass, Fail, Skip };
struct CommandResult {
    Status status = Status::Fail;
    std::string code;
    std::string message;
    std::map<std::string, std::string> evidence;
};
int exit_code(const CommandResult& result);
std::string to_json(const CommandResult& result);
}
```

退出码严格实现设计中的 `0/2/3/4/5/6/7`；为参数、环境、驱动和输出错误在 `CommandResult` 中增加 `ExitCategory`，不要根据字符串猜退出码。

- [ ] **Step 4：运行测试**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: exit code 0。

- [ ] **Step 5：提交**

```powershell
git add src/daq_capability_test tests/daq_capability_test SConstruct
git commit -m "feat: define DAQ result contract"
```

## Task 4：实现带注释的示例矩阵和 preflight

**Files:**
- Create: `src/daq_capability_test/matrix.hpp`
- Create: `src/daq_capability_test/matrix.cpp`
- Create: `src/daq_capability_test/default_test_matrix.tsv`
- Create: `tests/daq_capability_test/test_matrix.cpp`
- Modify: `.gitignore`

- [ ] **Step 1：写解析失败测试**

覆盖 `#` 注释、空行、TSV 表头、空字段、`REQUIRED`、布尔值、列表和精确错误位置：

```cpp
void test_matrix_comments_and_required_values() {
    const std::string text =
        "# sample_rate_hz: samples per second\n\n"
        "case_name\tenabled\tsample_rate_hz\tmin_signal_span_v\n"
        "low_sample_rate_100k\ttrue\t100000\tREQUIRED\n";
    daq::Matrix matrix = daq::parse_matrix(text);
    assert(matrix.cases.size() == 1);
    daq::CommandResult result = daq::preflight(matrix);
    assert(result.code == "REQUIRED_THRESHOLD_MISSING");
    assert(result.evidence.at("field") == "min_signal_span_v");
}
```

- [ ] **Step 2：运行并确认失败**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: 编译失败，解析器尚不存在。

- [ ] **Step 3：实现 `MatrixCase` 和严格解析器**

使用 `std::getline` 和显式 TSV 列索引；不得按空白拆分。首个非空字符为 `#` 的行忽略。未知列、重复 `case_name`、非法数值、非法布尔值和缺少表头均返回参数错误。

- [ ] **Step 4：创建完整示例矩阵**

顶部逐项解释设计列出的全部字段。提供所有 case 默认行；安全未知值写 `REQUIRED`，外部触发和 delay 默认 `enabled=false`。确定默认值包括设备 `PCI-1714,BID#0`、AI0/AI1、重复 3 次、1 MHz/1600 万点和 500/200/100 kHz。

- [ ] **Step 5：忽略现场文件和结果**

`.gitignore` 增加：

```gitignore
src/daq_capability_test/field_test_matrix.tsv
daq_results/
```

- [ ] **Step 6：运行测试并提交**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
git add .gitignore src/daq_capability_test tests/daq_capability_test
git commit -m "feat: add DAQ test matrix"
```

Expected: unit test exit code 0。

## Task 5：实现驱动接口、Fake 适配器和基础采集判定

**Files:**
- Create: `src/daq_capability_test/daq_adapter.hpp`
- Create: `src/daq_capability_test/fake_daq_adapter.hpp`
- Create: `src/daq_capability_test/fake_daq_adapter.cpp`
- Create: `src/daq_capability_test/acquisition_runner.hpp`
- Create: `src/daq_capability_test/acquisition_runner.cpp`
- Create: `tests/daq_capability_test/test_acquisition.cpp`

- [ ] **Step 1：写采集判定测试**

测试 3/3 PASS、短读、时长误差、overrun、双通道点数不等和低采样率窗口不足：

```cpp
void test_three_repetitions_are_required() {
    daq::FakeDaqAdapter adapter;
    adapter.enqueue_success(16000000, 16.0);
    adapter.enqueue_success(16000000, 16.0);
    adapter.enqueue_failure("CACHE_OVERFLOW");
    daq::CommandResult result = daq::run_acquisition_case(adapter, daq::boundary_case());
    assert(result.status == daq::Status::Fail);
    assert(result.code == "CACHE_OVERFLOW");
    assert(result.evidence.at("passed_repetitions") == "2");
}
```

- [ ] **Step 2：运行并确认失败**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: 编译失败，接口和 runner 尚不存在。

- [ ] **Step 3：定义窄接口**

`DaqAdapter` 包含 `query_capabilities`、`configure`、`acquire_once`、`configure_trigger`、`stop`，返回结构化结果；禁止把任一版本的 DAQNavi 类型泄漏到公共头文件。结果模板定义为：

```cpp
template <typename T>
struct AdapterResult {
    bool success = false;
    bool unsupported = false;
    std::string code;
    std::string message;
    std::string stage;
    std::string driver_error;
    T value{};
};
```

- [ ] **Step 4：实现基础判定**

严格按设计检查点数、每通道一致性、1% 时长误差、重复次数、timeout/overrun/cache overflow、完整周期数和最低 span。第一个失败保留为主 code，全部失败放入 evidence 数组编码字段。

- [ ] **Step 5：运行并提交**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
git add src/daq_capability_test tests/daq_capability_test
git commit -m "feat: evaluate DAQ acquisitions"
```

## Task 6：实现相位解析与 N 个独立周期重建

**Files:**
- Create: `src/daq_capability_test/phase_stitcher.hpp`
- Create: `src/daq_capability_test/phase_stitcher.cpp`
- Create: `tests/daq_capability_test/test_phase_stitcher.cpp`

- [ ] **Step 1：写确定性合成波形测试**

生成 0.02 Hz 方波参考与稳定指数响应片段，覆盖以下独立测试：边沿检测、无边沿且无已知起点、100% bin 覆盖、空 bin、片段不复用、重叠误差、单调漂移、边界跳变和最大尝试次数。

```cpp
void test_reconstructs_two_independent_waveforms_without_reuse() {
    std::vector<daq::Segment> segments = make_complete_segment_sets(2, 50.0, 16.0);
    daq::StitchConfig config = stable_stitch_config(2);
    daq::StitchResult result = daq::reconstruct(segments, config);
    assert(result.command.code == "N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED");
    assert(result.waveforms.size() == 2);
    assert(disjoint(result.waveforms[0].segment_ids, result.waveforms[1].segment_ids));
    assert(result.waveforms[0].coverage_percent == 100.0);
    assert(result.waveforms[1].coverage_percent == 100.0);
}
```

- [ ] **Step 2：运行并确认失败**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: 编译失败，重建 API 尚不存在。

- [ ] **Step 3：实现参考边沿和相位映射**

使用阈值穿越并要求方向一致；相位按相邻参考周期归一化。无边沿片段只有在元数据含已知触发相位和 delay 时才可使用，否则返回 `SEGMENT_PHASE_UNKNOWN`。

- [ ] **Step 4：实现互斥片段分配**

按输入顺序确定性分配片段；优先填补空 bin，其次满足重叠。一个 segment ID 一旦分配不得进入其他 waveform。任一 waveform 未达到 100% 覆盖则整体 FAIL。

- [ ] **Step 5：实现稳定性兜底**

计算同相位重叠最大误差、基线/幅值漂移、随采集顺序的线性趋势、周期边界跳变。超阈值分别返回 `OVERLAP_MISMATCH`、`NON_STATIONARY_RESPONSE` 或 `WAVEFORM_BOUNDARY_DISCONTINUITY`，且不写有效重建文件。

- [ ] **Step 6：运行并提交**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
git add src/daq_capability_test tests/daq_capability_test
git commit -m "feat: validate phase reconstruction"
```

## Task 7：实现审计输出

**Files:**
- Create: `src/daq_capability_test/result_writer.hpp`
- Create: `src/daq_capability_test/result_writer.cpp`
- Create: `tests/daq_capability_test/test_writer.cpp`

- [ ] **Step 1：写临时文件和 schema 测试**

测试 timestamp 目录、`environment.tsv`、`capability.tsv`、`summary.tsv`、配置快照、原始双通道列和失败时不保留最终 raw 文件。

- [ ] **Step 2：运行并确认失败**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: 编译失败，writer 尚不存在。

- [ ] **Step 3：实现原子写入**

所有文件先写 `.tmp`，检查 stream 状态并关闭成功后使用 Windows `MoveFileEx` 或标准重命名替换。配置文件逐字节复制到结果目录。写入失败返回退出码 7 和 `OUTPUT_WRITE_FAILED`。

- [ ] **Step 4：运行并提交**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
git add src/daq_capability_test tests/daq_capability_test
git commit -m "feat: write DAQ audit results"
```

## Task 8：实现 legacy DAQNavi 适配器

**Files:**
- Create: `src/daq_capability_test/legacy_adapter.cpp`
- Create: `src/daq_capability_test/legacy_adapter.hpp`
- Test: `tests/daq_capability_test/test_header_layout.py`

- [ ] **Step 1：扩展头文件隔离测试**

断言 legacy adapter 只包含 `daq_headers/legacy/bdaqctrl.h`，不包含 XNavi 路径。

- [ ] **Step 2：运行并确认失败**

```powershell
python -m unittest tests.daq_capability_test.test_header_layout -v
```

Expected: FAIL，adapter 尚不存在。

- [ ] **Step 3：实现能力查询和有限采集**

使用 `BufferedAiCtrl`、`AiFeatures`、`ScanChannel`、`ConvertClock` 和 `Trigger`。每个 DAQNavi 调用立即检查 `BioFailed`；controller 用局部 RAII wrapper 在析构中 `Stop/Cleanup/Dispose`。双通道读取按驱动返回的交织布局拆分为每通道数组，并用已知小规模 DemoDevice 数据验证布局。

- [ ] **Step 4：实现运行库检查**

启动时显式 `LoadLibraryW(L"biodaq.dll")`，检查旧版需要的入口并读取文件版本；缺失分别映射到 `RUNTIME_NOT_FOUND`、`ENTRY_POINT_MISSING` 和 `HEADER_RUNTIME_INCOMPATIBLE`。

- [ ] **Step 5：编译检查并提交**

```powershell
scons -Q cpp_build/daq_capability_test_legacy.exe
python -m unittest tests.daq_capability_test.test_header_layout -v
git add src/daq_capability_test tests/daq_capability_test SConstruct
git commit -m "feat: add legacy DAQ adapter"
```

Expected: legacy EXE 构建成功；无硬件时运行 capability 返回结构化 `DEVICE_NOT_FOUND`，不得崩溃。

## Task 9：实现 XNavi DAQNavi 适配器

**Files:**
- Create: `src/daq_capability_test/xnavi_adapter.cpp`
- Create: `src/daq_capability_test/xnavi_adapter.hpp`
- Test: `tests/daq_capability_test/test_header_layout.py`

- [ ] **Step 1：扩展隔离测试**

断言 XNavi adapter 只包含 `daq_headers/xnavi/bdaqctrl.h`，并断言两个 adapter 从不出现在同一 SCons source list。

- [ ] **Step 2：运行并确认失败**

```powershell
python -m unittest tests.daq_capability_test.test_header_layout -v
```

Expected: FAIL，XNavi adapter 尚不存在。

- [ ] **Step 3：实现新版适配器**

复用公共行为但不得 include legacy adapter。通过新版函数表 API 调用同名上层对象；额外检查 `AdxDaqNaviLibInitialize`。保持与 legacy 完全相同的 `DaqAdapter` 结果语义。

- [ ] **Step 4：构建并使用 DemoDevice 验证**

```powershell
scons -Q cpp_build/daq_capability_test_xnavi.exe
cpp_build\daq_capability_test_xnavi.exe capability --device "DemoDevice,BID#0"
```

Expected: 末行是合法 JSON；若本机 DemoDevice 的实际 BID 不为 0，先通过 Navigator/设备枚举确认准确 description，再使用该值运行并在验证记录中注明。此结果只证明 API 路径。

- [ ] **Step 5：提交**

```powershell
git add src/daq_capability_test tests/daq_capability_test SConstruct
git commit -m "feat: add XNavi DAQ adapter"
```

## Task 10：实现 CLI、suite 依赖和方案决策

**Files:**
- Create: `src/daq_capability_test/cli.hpp`
- Create: `src/daq_capability_test/cli.cpp`
- Create: `src/daq_capability_test/suite_runner.hpp`
- Create: `src/daq_capability_test/suite_runner.cpp`
- Create: `src/daq_capability_test/main.cpp`
- Create: `tests/daq_capability_test/test_suite.cpp`

- [ ] **Step 1：写 CLI 和 suite 测试**

覆盖 `capability/acquire/trigger/phase-stitch capture/reconstruct/suite`，互斥的 `--all/--case/--from`，未知 case，前置失败 SKIP，以及最终策略映射。

```cpp
void test_failed_reference_skips_phase_stitch_but_not_trigger() {
    daq::FakeDaqAdapter adapter;
    adapter.fail_case("dual_channel_reference", "REFERENCE_SIGNAL_INVALID");
    daq::SuiteResult result = daq::run_suite(adapter, complete_matrix(), daq::SuiteScope::all());
    assert(result.case_result("phase_stitch").code == "PREREQUISITE_FAILED");
    assert(result.case_result("external_trigger").executed);
    assert(result.rejected_strategies.at("PHASE_STITCHING") == "REFERENCE_SIGNAL_INVALID");
}
```

- [ ] **Step 2：运行并确认失败**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
```

Expected: 编译失败，suite 尚不存在。

- [ ] **Step 3：实现精确依赖图**

`phase_stitch` 依赖 `device_capability + dual_channel_reference + segment_phase`；`delay_trigger` 依赖 `device_capability + external_trigger`；`low_sample_rate` 只依赖 `device_capability + single_channel_boundary`。前置失败产生 `SKIP/PREREQUISITE_FAILED`，原始失败 code 放 evidence。

- [ ] **Step 4：实现方案决策**

`LOW_SAMPLE_RATE` 只在至少一个配置采样率完整通过时支持；`PHASE_STITCHING` 只在 `N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED` 时支持；`TRIGGER_DELAY` 只在 `EXTERNAL_TRIGGER_STABLE` 与 `DELAY_TRIGGER_WINDOW_COVERED` 均通过时支持。

- [ ] **Step 5：确保 stdout 末行唯一 JSON**

日志写 stderr 和文件；stdout 可输出进度，但最后一行必须且只能是最终 `to_json` 结果，随后按结果类别返回退出码。

- [ ] **Step 6：运行并提交**

```powershell
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
git add src/daq_capability_test tests/daq_capability_test
git commit -m "feat: orchestrate DAQ validation suite"
```

## Task 11：增加端到端 mock 可执行程序和故障场景

**Files:**
- Create: `src/daq_capability_test/adapter_factory.hpp`
- Create: `src/daq_capability_test/mock_adapter_factory.cpp`
- Create: `src/daq_capability_test/mock_success.tsv`
- Create: `src/daq_capability_test/mock_non_stationary.tsv`
- Create: `src/daq_capability_test/mock_failures.tsv`
- Modify: `src/daq_capability_test/fake_daq_adapter.cpp`
- Modify: `src/daq_capability_test/fake_daq_adapter.hpp`
- Create: `tests/daq_capability_test/test_mock_cli.py`
- Modify: `SConstruct`

- [ ] **Step 1：写端到端失败测试**

`test_mock_cli.py` 使用 `subprocess.run` 执行真实 mock EXE，解析 stdout 最后一行 JSON，并检查退出码、输出文件和方案结论：

```python
import json
from pathlib import Path
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
EXE = ROOT / "cpp_build/daq_capability_test_mock.exe"


class MockCliTest(unittest.TestCase):
    def run_suite(self, matrix):
        completed = subprocess.run(
            [str(EXE), "suite", "--config", str(ROOT / matrix), "--all"],
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        payload = json.loads(completed.stdout.strip().splitlines()[-1])
        return completed, payload

    def test_success_suite_supports_all_mocked_strategies(self):
        completed, payload = self.run_suite("src/daq_capability_test/mock_success.tsv")
        self.assertEqual(completed.returncode, 0)
        self.assertEqual(payload["result"], "PASS")
        self.assertEqual(
            set(payload["supported_strategies"]),
            {"LOW_SAMPLE_RATE", "PHASE_STITCHING", "TRIGGER_DELAY"},
        )

    def test_non_stationary_response_is_a_hard_failure(self):
        completed, payload = self.run_suite(
            "src/daq_capability_test/mock_non_stationary.tsv"
        )
        self.assertNotEqual(completed.returncode, 0)
        self.assertEqual(payload["failed_cases"]["phase_stitch"]["code"],
                         "NON_STATIONARY_RESPONSE")
```

- [ ] **Step 2：运行并确认失败**

```powershell
python -m unittest tests.daq_capability_test.test_mock_cli -v
```

Expected: ERROR，因为 mock EXE 和矩阵尚不存在。

- [ ] **Step 3：让 FakeDaqAdapter 从矩阵场景生成确定性数据**

Fake adapter 支持 `mock_scenario` 列，至少实现 `success`、`short_read`、`timeout`、`overrun`、`cache_overflow`、`reference_missing`、`edge_missing`、`edge_jitter`、`insufficient_coverage`、`overlap_mismatch`、`non_stationary`、`boundary_jump`、`trigger_timeout`、`delay_position_mismatch`。合成数据使用固定公式和固定种子，不依赖系统时间或随机设备。

- [ ] **Step 4：创建三组 mock 矩阵**

`mock_success.tsv` 启用所有 case 并使用安全的小点数，最终支持三个方案。`mock_non_stationary.tsv` 只让 phase stitch 的同相位响应随片段顺序漂移，必须返回 `NON_STATIONARY_RESPONSE`。`mock_failures.tsv` 为每种故障提供独立 case，确保错误 code 不被泛化为 `ACQUISITION_FAILED`。

- [ ] **Step 5：构建共享 main 的 mock target**

`daq_capability_test_mock.exe` 使用与两个真实 EXE 相同的 `main.cpp`、CLI、suite、writer 和 stitcher，只链接 `mock_adapter_factory.cpp + fake_daq_adapter.cpp`，不得链接任何 DAQNavi adapter 或头文件。

- [ ] **Step 6：运行端到端测试并提交**

```powershell
scons -Q cpp_build/daq_capability_test_mock.exe
python -m unittest tests.daq_capability_test.test_mock_cli -v
cpp_build\daq_capability_test_mock.exe suite --config src\daq_capability_test\mock_success.tsv --all
git add src/daq_capability_test tests/daq_capability_test SConstruct
git commit -m "test: add DAQ mock validation executable"
```

Expected: Python 测试 PASS；成功矩阵末行返回 `PASS`，三种策略均在 `supported_strategies`。

## Task 12：完成显式 SCons targets

**Files:**
- Modify: `SConstruct`
- Test: `tests/daq_capability_test/test_header_layout.py`

- [ ] **Step 1：写 target 源文件隔离测试**

读取 `SConstruct`，断言存在正式程序、legacy、XNavi、mock 和 unit test 五个 target；legacy 源列表没有 `xnavi_adapter.cpp`，XNavi 源列表没有 `legacy_adapter.cpp`，mock 和 unit test 源列表只使用 fake adapter。

- [ ] **Step 2：运行并确认失败**

```powershell
python -m unittest tests.daq_capability_test.test_header_layout -v
```

Expected: 若仍使用 glob 或源列表混合则 FAIL。

- [ ] **Step 3：重构 SConstruct 为显式源列表**

保留现有 `sample.exe` 行为，避免 `Glob('cpp_build/*/*.cpp')` 把新测试 main 和 adapter 自动链接进正式程序。定义 `sample_sources`、`daq_common_sources`、`legacy_sources`、`xnavi_sources`、`unit_test_sources`，分别创建 target。

- [ ] **Step 4：构建全部 target**

```powershell
scons -Q cpp_build/sample.exe
scons -Q cpp_build/daq_capability_unit_tests.exe
scons -Q cpp_build/daq_capability_test_mock.exe
scons -Q cpp_build/daq_capability_test_legacy.exe
scons -Q cpp_build/daq_capability_test_xnavi.exe
```

Expected: 五个 target 均构建成功，无重复 `main`、无混用头文件。

- [ ] **Step 5：提交**

```powershell
git add SConstruct tests/daq_capability_test/test_header_layout.py
git commit -m "build: add isolated DAQ targets"
```

## Task 13：完善 README 操作手册

**Files:**
- Modify: `README.md`
- Reference: `docs/superpowers/specs/2026-07-11-daq-capability-test-design.md`

- [ ] **Step 1：增加构建和运行环境说明**

写明 `scons -Q`、三个新增 target、legacy/XNavi 运行库匹配、DLL 不随程序分发、PCI-1714U AI0/AI1 参考分路、电压量程和外部触发接线要求。

- [ ] **Step 2：加入步骤 0 至步骤 9 Check-list**

逐项复制规格中的结构，并把规格中用于简写版本的 variant 标记展开为完整 legacy 和 XNavi 命令。每步包含目的、前置条件、命令、PASS code、FAIL code、方案含义、下一步和现场记录模板。

- [ ] **Step 3：加入一键、单项和断点命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --all
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case phase_stitch
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --from dual_channel_reference
```

- [ ] **Step 4：人工对照程序 `--help`**

Run:

```powershell
cpp_build\daq_capability_test_legacy.exe --help
cpp_build\daq_capability_test_xnavi.exe --help
```

Expected: README 中每个选项与实际 help 一致；修正文档或实现中的不一致，不保留失效命令。

- [ ] **Step 5：提交**

```powershell
git add README.md
git commit -m "docs: add DAQ field validation guide"
```

## Task 14：全量验证、单提交整理和完成审计

**Files:**
- Verify: all changed files
- Update only if failures expose defects

- [ ] **Step 1：运行无硬件测试**

```powershell
python -m unittest discover -s tests -v
scons -Q cpp_build/daq_capability_unit_tests.exe
cpp_build\daq_capability_unit_tests.exe
python -m unittest tests.daq_capability_test.test_mock_cli -v
```

Expected: Python 和 C++ 测试全部通过。

- [ ] **Step 2：构建全部程序**

```powershell
scons -Q cpp_build/sample.exe
scons -Q cpp_build/daq_capability_test_mock.exe
scons -Q cpp_build/daq_capability_test_legacy.exe
scons -Q cpp_build/daq_capability_test_xnavi.exe
```

Expected: 四个程序构建成功，现有 `sample.exe` 未产生行为相关差异。

- [ ] **Step 3：运行全部端到端 mock 场景**

```powershell
cpp_build\daq_capability_test_mock.exe suite --config src\daq_capability_test\mock_success.tsv --all
cpp_build\daq_capability_test_mock.exe suite --config src\daq_capability_test\mock_non_stationary.tsv --all
cpp_build\daq_capability_test_mock.exe suite --config src\daq_capability_test\mock_failures.tsv --all
```

Expected: success 返回退出码 0 并支持三个方案；non-stationary 返回非零且包含 `NON_STATIONARY_RESPONSE`；failures 返回非零且每个启用的故障 case 保留其专用错误 code。

- [ ] **Step 4：验证示例配置 preflight**

```powershell
Copy-Item src\daq_capability_test\default_test_matrix.tsv src\daq_capability_test\field_test_matrix.tsv
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case preflight
```

Expected: 若示例保留 `REQUIRED`，返回非零和 `REQUIRED_THRESHOLD_MISSING`，JSON 明确列出字段与 case；填入测试阈值后返回 `PASS/PREFLIGHT_READY`。

- [ ] **Step 5：运行 DemoDevice API 验证**

先从 Navigator 确认 DemoDevice description，然后运行 capability 和小点数 acquire。Expected: 两条命令末行都是合法 JSON；成功只记录为 API 路径通过。

- [ ] **Step 6：运行代码风格和差异检查**

```powershell
git diff --check master...HEAD
git status --short
```

Expected: 无空白错误；只保留已知用户未跟踪文件，不提交生成的 EXE、结果目录或现场配置。

- [ ] **Step 7：请求代码审查**

调用 `requesting-code-review`，重点审查：新旧 ABI 是否隔离、所有 DAQNavi 路径是否 RAII 清理、退出码是否稳定、suite 是否错误跳过失败、片段是否绝不复用、漂移检测是否会阻止伪重建。

- [ ] **Step 8：提交审查修复（如有）**

```powershell
git add -u -- src tests SConstruct README.md .gitignore
git commit -m "fix: address DAQ validation review"
```

若无需修复，不创建空提交。

- [ ] **Step 9：把实现期提交整理为一个最终提交**

先确认基线之后只有本任务提交：

```powershell
$base = Get-Content .git\daq-capability-implementation-base
git log --oneline "$base..HEAD"
git status --short
```

Expected: 日志只包含本计划产生的临时提交；工作区没有本任务未提交改动。若出现其他提交，停止并报告。

确认后执行非交互 squash：

```powershell
$base = Get-Content .git\daq-capability-implementation-base
git reset --soft $base
git commit -m "feat: add DAQ capability validation tool"
```

Expected: `git log --oneline "$base..HEAD"` 只显示一个最终提交。该操作仅在前一步确认提交归属后执行。

- [ ] **Step 10：对最终单提交重新验证**

```powershell
python -m unittest discover -s tests -v
scons -Q cpp_build/daq_capability_unit_tests.exe cpp_build/daq_capability_test_mock.exe cpp_build/daq_capability_test_legacy.exe cpp_build/daq_capability_test_xnavi.exe
cpp_build\daq_capability_unit_tests.exe
python -m unittest tests.daq_capability_test.test_mock_cli -v
git diff --check $base..HEAD
git status --short
```

Expected: 所有测试和构建通过；实现基线之后只有一个提交；status 只显示开始前已存在的用户未跟踪文件。

## 剩余风险与现场边界

- 当前开发机没有实际 PCI-1714U，无法在交付前证明 1600 万点、双通道、真实触发和 delay 行为；这些必须按 README 在目标工控机完成。
- XNavi 安装可能更新同名 `biodaq.dll`，legacy EXE 是否能在新版运行库上工作必须由启动兼容检查明确报告，不能假定兼容。
- PCI-1714U 参考输入的分路、共地、分压和隔离属于现场电气安全条件；程序只能验证配置和测得电压，不能替代硬件审查。
- “N 个独立完整周期”是由互不复用的多个物理周期片段重建，前提是重复激励响应稳定；`NON_STATIONARY_RESPONSE` 必须作为硬失败。
