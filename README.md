# Half.Sample

[![Build status](https://ci.appveyor.com/api/projects/status/04ox7guybqotu67d?svg=true)](https://ci.appveyor.com/project/Wingsgo/half-sample)

Half.Sample 是 Half 应用中的 C++ 采样程序和 Python 模块。本仓库同时提供独立的 PCI-1714U DAQ 能力验证程序；验证程序不改变原有 `sample.exe` 的行为。

## 构建环境

需要 Windows、支持 C++14 的 MSVC、Python 和 SCons。在仓库根目录执行：

```powershell
scons -Q cpp_build/sample.exe
scons -Q cpp_build/daq_capability_test_mock.exe
scons -Q cpp_build/daq_capability_test_legacy.exe
scons -Q cpp_build/daq_capability_test_xnavi.exe
```

三个验证 target 分别是无硬件 mock、旧版 BDaq/Legacy 驱动和新版 DAQNavi/XNavi 驱动。Legacy 与 XNavi 使用平级且隔离的头文件目录，不能混用。编译验证程序只需要对应版本的头文件，不需要导入库；程序在运行时动态加载已安装驱动的 `biodaq.dll`。仓库不复制、提交或分发 DLL。现场机必须安装与目标程序架构匹配的对应驱动，不能把另一版本 DLL 放到程序目录“凑版本”。

帮助命令不加载设备，退出码应为 `0`：

```powershell
cpp_build\daq_capability_test_legacy.exe --help
cpp_build\daq_capability_test_xnavi.exe --help
```

## sample.exe自动选择 Instant AI

应用层应把下列三项作为全局采集卡设置，并在每次 `measure` 或 `dump` 时一起传入。
推荐的出厂默认值为：

- `instant_ai_frequency_threshold=0.1` Hz：Instant/Buffered 切换阈值。
- `instant_ai_target_points_per_waveform=100`：每个完整波形的目标重建点数。
- `instant_ai_max_reliable_polling_hz=10`：驱动可靠软件轮询频率上限。

```python
from sample import sampler

sampler.measure(
    number_of_waveforms=3,
    emitting_frequency=0.05,
    instant_ai_frequency_threshold=0.1,
    instant_ai_target_points_per_waveform=100,
    instant_ai_max_reliable_polling_hz=10,
)
```

当阈值大于 0 且发射频率小于等于阈值时选择 legacy Instant AI；高于阈值时
选择原有 Buffered AI。Python API 的阈值参数仍默认为 0，以保证未传新参数的旧程序继续
使用 Buffered AI；新集成应显式传入上述三个推荐值。Buffered AI 原有规则保持不变：发射
频率低于 10 Hz 使用 1 MHz，否则使用 20 MHz。

Instant AI 只启动一次连续采集会话。若要得到 `N` 个完整波形，因开始相位未知，计划采集
窗口固定为 `(N+1)/f` 秒，不把 `N` 个周期拆成独立任务。例如 `f=0.05 Hz`、`N=1`
时计划采集 `(1+1)/0.05 = 40` 秒。实际轮询率为
`min(f * target_points, max_reliable_polling_hz)`。

采集启动后可轮询进度或请求取消：

```python
import time

while sampler.is_measuring:
    progress = sampler.sampling_progress()
    print(progress.elapsed_seconds, progress.planned_seconds,
          progress.completed_cycles, progress.target_cycles,
          progress.successful_reads, progress.late_reads)
    # 用户取消时：
    # sampler.cancel_sampling()
    time.sleep(0.2)

result = sampler.query()
```

`cancel_sampling()` 是取消请求，不是强制终止线程。真实采集卡的同步 `ReadAny()` 调用期间
无法立即中断；取消会在该调用返回后生效。最终应继续轮询 `is_measuring` 并通过
`query()` 确认 `cancelled=True`。

`query()` 稳定公开 `error_category`、`retryable` 和 `cancelled`。Instant AI 采集失败的可重试类别
为 `read`、`schedule`、`alignment` 和 `waveform_count`。此外，`state` 表示生命周期或并发冲突，
`state` 当前也是 `retryable=True`；调用方应等待当前测量结束后再试，不能同时启动另一项采集。
`config`、`coverage` 和 `cancelled` 必须立即停止。应用层可采用“每次测量最多尝试 3 次”的策略，
但应以返回的 `retryable` 和 `error_category` 为准，不应匹配错误文本。每次采集失败后的重试都必须
重新采集完整的 `(N+1)/f` 窗口。

Instant AI 错误不会自动回退到 Buffered AI。原始数据回放显式兼容 Instant AI V2、
Instant AI V1 和无标记 Buffered 三种格式；V2 保留计划/实际时间、读取成功状态和原生
错误码。Instant AI 采集仅支持 legacy 驱动。

## Legacy Instant AI 轮询验证

`instant-ai-polling` 只在 Legacy 版本中实现。它连续调用
`InstantAiCtrl::ReadAny()`，记录每次读取完成时间、驱动调用耗时、相邻读取间隔和通道值。
默认轮询 30 秒：

```powershell
scons -Q cpp_build/daq_capability_test_legacy.exe
cpp_build\daq_capability_test_legacy.exe instant-ai-polling --device "PCI-1714,BID#0" --channels 0 --range "-5V~5V" --duration 30 --output-dir daq_capability_results\legacy_instant
```

不指定 `--poll-rate` 时程序无等待地连续调用 `ReadAny()`。要匹配官方界面的
`sampling rate per-channel = 10 Hz`，使用软件定时轮询：

```powershell
cpp_build\daq_capability_test_legacy.exe instant-ai-polling --device "PCI-1714,BID#0" --channels 0 --range "-5V~5V" --poll-rate 10 --duration 30 --output-dir daq_capability_results\legacy_instant_10hz
```

`--poll-rate 10` 使用单调时钟和绝对 deadline，每 100 ms 调用一次 `ReadAny()`；它是每通道的软件
轮询频率，不是 Buffered AI 硬件采样时钟。多通道在同一次 `ReadAny()` 中读取，因此每个通道均为
10 点/秒。

成功时 exit `0`，末行 JSON 为 `PASS / INSTANT_AI_POLLING_STABLE`。证据包括成功/失败
读取次数、平均读取率、平均/P95/P99/最大读取间隔，以及每个通道的最小值、最大值和跨度。
原始数据位于：

```text
<evidence.run_directory>\raw\instant_ai_polling.tsv
```

第一次硬件测试不要设置间隔阈值，先记录实际分布。明确业务门槛后可增加
`--max-gap-ms <毫秒>`；任一相邻读取完成间隔超过阈值时返回 exit `2` 和
`INSTANT_AI_GAP_EXCEEDED`。

该 PASS 只证明本次 Windows/驱动环境下软件轮询持续成功，不证明硬件时钟等间隔采样，
也不能证明两次 `ReadAny()` 之间没有遗漏输入变化。

安装 Python 绘图依赖并生成采样点图：

```powershell
python -m pip install -r requirements.txt
python scripts\plot_instant_ai_samples.py "daq_capability_results\legacy_instant\<timestamp>\raw\instant_ai_polling.tsv"
```

默认在 TSV 旁生成 `instant_ai_polling.png`。X 轴为读取完成时间，Y 轴为电压，每个通道
独立着色。TSV 始终保留全部数据；图中默认在完整时间范围内均匀抽取最多 100,000 个
共用索引，并保留首尾点。可选参数：

```powershell
python scripts\plot_instant_ai_samples.py "daq_capability_results\legacy_instant\<timestamp>\raw\instant_ai_polling.tsv" --output instant.png
python scripts\plot_instant_ai_samples.py "daq_capability_results\legacy_instant\<timestamp>\raw\instant_ai_polling.tsv" --max-points 50000
python scripts\plot_instant_ai_samples.py "daq_capability_results\legacy_instant\<timestamp>\raw\instant_ai_polling.tsv" --max-points 0 --show
```

`--max-points 0` 绘制全部点；`--show` 在保存 PNG 后显示窗口。

## CLion 与代码格式

`src\CMakeLists.txt` 声明 `daq_capability_test_mock`、`daq_capability_test_legacy` 和
`daq_capability_test_xnavi`，供 CLion 正确索引验证程序的源文件、符号和 include
路径。正式构建和验收仍使用本 README 中的 SCons 命令。

仓库根目录 `.clang-format` 用于项目自有 C++。批量格式化时排除
`src\3rdparty\` 和厂商文件 `src\daq_headers\**\bdaqctrl.h`。

## 自动判定契约

执行命令的 stdout 最后一行是单行 JSON，正式结论由进程退出码和该 JSON 共同给出，不要求人工看波形或 TSV。日志写 stderr；失败原因在末行 JSON 的 `message`、`evidence`，suite 还在 `failed_cases` 中保留每个失败 case 的 `code` 和证据。

| 退出码 | 含义 |
| --- | --- |
| `0` | 验证通过 |
| `2` | 能力或数据验证失败 |
| `3` | 当前硬件或驱动不支持，JSON `result=SKIP` |
| `4` | 命令参数或配置无效 |
| `5` | 运行环境缺失或不匹配 |
| `6` | 驱动、运行库、设备调用或采集超时 |
| `7` | 结果或输出文件写入失败 |

首选方式是直接执行统一验证脚本。脚本根据 `-Variant` 自动选择构建目录中的 Mock、Legacy 或 XNavi 程序，构造 `suite` 命令，并自动断言 exit `0`、`result=PASS`、`code=SUITE_PASSED` 以及 suite 顶层结果字段：

```powershell
# 无硬件完整验证
.\scripts\daq_validation.ps1 -Variant mock -Config src\daq_capability_test\mock_success.tsv -OutputDir daq_capability_results\mock -All

# Legacy/XNavi 现场完整验证
.\scripts\daq_validation.ps1 -Variant legacy -Config src\daq_capability_test\field_test_matrix.tsv -OutputDir daq_capability_results\legacy -All
.\scripts\daq_validation.ps1 -Variant xnavi -Config src\daq_capability_test\field_test_matrix.tsv -OutputDir daq_capability_results\xnavi -All

# 单项验证、从断点继续、静默自动化
.\scripts\daq_validation.ps1 -Variant xnavi -Config src\daq_capability_test\field_test_matrix.tsv -Case phase_stitch
.\scripts\daq_validation.ps1 -Variant xnavi -Config src\daq_capability_test\field_test_matrix.tsv -From dual_channel_reference
.\scripts\daq_validation.ps1 -Variant mock -Config src\daq_capability_test\mock_success.tsv -All -Quiet
```

`-All`、`-Case`、`-From` 必须恰好指定一个。无参数执行脚本会显示中文帮助和示例并返回 `0`；未知参数、缺少配置/程序或 scope 冲突会显示明确错误并返回非零。正常执行依次显示 `[RUN]`、`[EXIT]` 和 `[PASS]`；验证不一致时显示 `[FAIL]` 及每条 `[ASSERT]`，异常包含 exit、code、message、evidence 和具体断言原因。`-Quiet` 关闭过程提示，但 stdout 最后一行仍是完整 JSON。

`-OutputDir` 可以是尚不存在的相对或绝对多层目录，验证程序会逐层创建目录；任何中间路径无法创建时以 exit `7` 返回，并在 `evidence.stage=create_root`、`evidence.path` 和 `evidence.os_error` 中给出失败位置和原因。程序不会覆盖或删除已有文件。

高级用法需要自定义预期失败、case code 或 evidence 断言时，可 dot-source 脚本后调用底层函数。dot-source 只注册函数，不启动验证：

```powershell
. .\scripts\daq_validation.ps1
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_mock.exe' -Arguments @('suite','--config','src\daq_capability_test\mock_non_stationary.tsv','--all') -ExpectedExit 2 -ExpectedResult FAIL -ExpectedCode SUITE_FAILED
```

函数只捕获 stdout，不把 stderr 合并进 JSON；进程结束后立即保存退出码并解析最后一个非空 stdout 行。过程使用 `Write-Host`，所以赋值接收函数返回值时 pipeline 中仍只有 payload。

后续所有验证命令都通过该函数逐个门控。不要连续裸跑可执行程序，也不要只搜索 stdout 中是否出现 `PASS`。`RequiredTopLevelFields` 和 `RequiredEvidenceProperties` 只要求属性存在，允许合法的空数组/空字符串；`RequiredNonEmptyEvidenceFields` 要求属性存在且不是 null、空白字符串或空集合，布尔值 `false` 和数值 `0` 都是有效值。策略可选择至少包含或精确匹配。验证预期失败场景时显式传入预期 exit/result/code 和 failed case/code。

## 接线与安全

脉冲发生器输出必须在样品前分路：一路经过样品和测量链路接 PCI-1714U `AI0`，另一路作为未经样品的同步参考，经合适的分压、限流、保护、缓冲或隔离接 `AI1`。这样 `AI1` 的方波边沿给出同一次采集中 `AI0` 样品响应的周期位置；它不是从样品后“恢复”原始信号。两路必须共享可靠时间基准，并按设备接线要求共地或隔离。

上电前用万用表/示波器确认两路峰值、偏置和地电位差都在配置量程内。默认 `-10V~10V` 只是示例，不是安全承诺。外部触发验证还需把同步输出安全连接到板卡外部触发端。未完成某种接线时，把相应 case 的 `enabled` 设为 `false`，不能悬空运行。先关闭脉冲输出完成接线，再从小幅度开始；程序不会控制脉冲发生器，也不能代替硬件保护。

## 配置示例

`default_test_matrix.tsv` 是可修改示例，文件开头的注释逐项解释 30 个参数，数据行给出默认值或 `REQUIRED`。复制后只修改现场文件：

```powershell
Copy-Item src\daq_capability_test\default_test_matrix.tsv src\daq_capability_test\field_test_matrix.tsv
```

保持制表符分隔和表头不变；空值表示“不适用”，不能用 `0` 代替。把所有启用行中的 `REQUIRED` 替换为现场确认值，尤其是设备名、AI0/AI1、量程、采样率/点数、信号频率、目标周期数 N、幅值、边沿抖动、占空比误差、phase bin、重叠/漂移/边界阈值、最大尝试次数、触发源和 delay。外部触发默认禁用，确认接线后才启用。每次运行会把实际配置快照写入结果目录。

## 现场验证 Check-list

所有命令从仓库根目录执行。每一步都以退出码和末行 JSON 自动判定；下列 FAIL code 均为代表性业务原因，驱动异常、写盘失败等仍按统一退出码返回明确原因。

### [ ] 步骤 0：现场参数能否自动判定

**问题与方案关系：** 将业务阈值、硬件参数固定到可审计配置；它是三个候选方案的共同前置，避免人工看波形下结论。

**前置：** 已复制并填写 `field_test_matrix.tsv`，无法接线的 case 已禁用。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','preflight') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'preflight' -ExpectedCaseCode 'PREFLIGHT_OK'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','preflight') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'preflight' -ExpectedCaseCode 'PREFLIGHT_OK'
```

**PASS：** exit `0`，末行 JSON `result=PASS`，且 `cases.preflight.code=PREFLIGHT_OK`。**FAIL：** exit `4`；打不开文件为 `CONFIG_OPEN_FAILED`，格式错误为 `MISSING_HEADER`、`MISSING_COLUMN`、`UNKNOWN_COLUMN`、`DUPLICATE_COLUMN`、`COLUMN_COUNT_MISMATCH`、`MISSING_VALUE`、`INVALID_BOOLEAN`、`INVALID_INTEGER`、`INVALID_NUMBER`、`INVALID_LIST` 或 `INVALID_VALUE`，规则错误为 `REQUIRED_THRESHOLD_MISSING`、`MISSING_REQUIRED_FIELD`、`INVALID_FIELD`、`UNKNOWN_CASE`、`DUPLICATE_CASE_NAME` 或 `CASE_MODE_MISMATCH`。`message/evidence` 给出行号、case 和字段。修正后重跑，不得跳过。

**方案含义与下一步：** 通过只证明参数完整且满足静态安全规则，不证明硬件可用；继续步骤 1。失败则按 `message/evidence` 修正具体字段并重复本步。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 1：设备和对应驱动是否可用

**问题与方案关系：** 验证对应 `biodaq.dll` 可加载、设备可找到、能力可读取；失败时所有方案都不能继续。

**前置：** PCI-1714U 和所选版本驱动已安装，设备管理器无错误；脉冲输出可保持关闭。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('capability','--device','PCI-1714,BID#0','--output-dir','daq_capability_results\legacy_capability') -ExpectedCode 'DEVICE_CAPABILITY_CONFIRMED' -RequiredEvidenceProperties @('trigger_sources','trigger_actions') -RequiredNonEmptyEvidenceFields @('device_description','runtime_path','runtime_version','channel_count','buffer_capacity','trigger_supported')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('capability','--device','PCI-1714,BID#0','--output-dir','daq_capability_results\xnavi_capability') -ExpectedCode 'DEVICE_CAPABILITY_CONFIRMED' -RequiredEvidenceProperties @('trigger_sources','trigger_actions') -RequiredNonEmptyEvidenceFields @('device_description','runtime_path','runtime_version','channel_count','buffer_capacity','trigger_supported')
```

**PASS：** exit `0`，末行 JSON 为 `PASS / DEVICE_CAPABILITY_CONFIRMED`；`evidence` 必须含 `runtime_path`、`runtime_version`、`channel_count`、`buffer_capacity`、`trigger_supported`、`trigger_sources` 和 `trigger_actions`。**FAIL：** exit `5` 表示环境缺失；当前适配器报告的 `RUNTIME_NOT_FOUND`、`ENTRY_POINT_MISSING`、`HEADER_RUNTIME_INCOMPATIBLE`、`DEVICE_NOT_FOUND`、`FEATURE_QUERY_FAILED` 等驱动/运行库调用错误为 exit `6`；明确不支持时为 exit `3 / SKIP`。先按 JSON 的 `code/message` 修复环境。

**方案含义与下一步：** 通过建立所有候选方案的设备前置，继续步骤 2；失败则停止全部上机采集，依据机器原因修复 DLL、驱动或设备。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 2：现有单通道边界是否稳定

**问题与方案关系：** 连续三次验证 1 MHz、1600 万点单通道基准，证明测试程序没有破坏既有采集能力；基准失败时不能评价新方案。

**前置：** 步骤 1 通过，AI0 接安全范围内的代表性样品信号，内存和磁盘空间充足。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','single_channel_boundary') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'single_channel_boundary' -ExpectedCaseCode 'SINGLE_CHANNEL_BOUNDARY_PASSED'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','single_channel_boundary') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'single_channel_boundary' -ExpectedCaseCode 'SINGLE_CHANNEL_BOUNDARY_PASSED'
```

**PASS：** exit `0`，`cases.single_channel_boundary` 为 `PASS / SINGLE_CHANNEL_BOUNDARY_PASSED`，3/3 点数正确、时长误差达标且无溢出。**FAIL：** exit `2`（`POINT_COUNT_MISMATCH`、`DURATION_OUT_OF_TOLERANCE` 等数据验证失败），exit `6`（`OVERRUN`、`CACHE_OVERFLOW`、`TIMEOUT` 或其他驱动调用错误），exit `3 / SKIP` 表示不支持；原因和重复序号在 JSON 证据中。

**方案含义与下一步：** 通过说明既有基准成立，继续步骤 3；失败先解决基础采集稳定性，不能用后续结果评价新方案。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 3：降低采样率能否覆盖完整低频周期

**问题与方案关系：** 检查 500/200/100 kHz 在固定点数内是否覆盖要求周期且信号幅值有效；成功的采样率才支持 `LOW_SAMPLE_RATE` 方案。

**前置：** 步骤 2 通过，脉冲发生器经正式样品路径稳定输出，配置频率与实际一致。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','low_sample_rate') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'low_sample_rate' -ExpectedCaseCode 'LOW_SAMPLE_RATE_PASSED'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','low_sample_rate') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'low_sample_rate' -ExpectedCaseCode 'LOW_SAMPLE_RATE_PASSED'
```

**PASS：** exit `0`，`cases.low_sample_rate` 为 `PASS / LOW_SAMPLE_RATE_PASSED`，`evidence.passed_sample_rates_hz` 列出通过采样率；至少一个配置采样率通过即接受该方案。**FAIL：** exit `2`，如 `WINDOW_TOO_SHORT`、`SIGNAL_SPAN_TOO_LOW`、`POINT_COUNT_MISMATCH`；全部失败则拒绝 `LOW_SAMPLE_RATE`，部分失败记录在 `evidence.failed_sample_rates_hz`。

**方案含义与下一步：** 仅采用 `passed_sample_rates_hz` 中的档位；无通过档位则排除降采样率方案。无论结论如何，继续步骤 4 验证独立的相位拼接路径。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 4：双通道原始参考是否有效

**问题与方案关系：** 确认 AI0 样品响应和 AI1 样品前分路参考同步、等点数、同时间轴；这是软件相位定位和拼接的硬件前提。

**前置：** 按“接线与安全”完成 AI0/AI1 分路，AI1 幅值、偏置、接地/隔离均安全。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','dual_channel_reference') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'dual_channel_reference' -ExpectedCaseCode 'REFERENCE_SIGNAL_VALID'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','dual_channel_reference') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'dual_channel_reference' -ExpectedCaseCode 'REFERENCE_SIGNAL_VALID'
```

**PASS：** exit `0`，`cases.dual_channel_reference` 为 `PASS / REFERENCE_SIGNAL_VALID`，两通道点数相等、参考幅值有效且重复稳定。**FAIL：** exit `2`（如 `SIGNAL_SPAN_TOO_LOW`、`CHANNEL_POINT_COUNT_MISMATCH`），exit `3 / SKIP`（如 `CHANNEL_COUNT_UNSUPPORTED`、`CHANNEL_INDEX_UNSUPPORTED`），或 exit `6` 的驱动错误。失败即停止步骤 5/6。

**方案含义与下一步：** 通过证明相位参考硬件路径成立，继续步骤 5；失败则排除软件相位拼接，先修复分路、量程或通道能力。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 5：每个短片段能否定位相位

**问题与方案关系：** 用 AI1 方波边沿标定 AI0 样本在激励周期中的位置；拒绝无边沿且起点未知的片段，防止错误拼接。

**前置：** 步骤 4 通过，频率、参考占空比、占空比误差和最大边沿抖动已填写。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','segment_phase') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'segment_phase' -ExpectedCaseCode 'SEGMENT_PHASE_RESOLVED'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','segment_phase') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'segment_phase' -ExpectedCaseCode 'SEGMENT_PHASE_RESOLVED'
```

**PASS：** exit `0`，`PASS / SEGMENT_PHASE_RESOLVED`，证据含边沿数、实测周期/占空比及抖动。**FAIL：** exit `2`，如 `EDGE_NOT_FOUND`、`SEGMENT_PHASE_UNKNOWN`、`REFERENCE_FREQUENCY_MISMATCH`、`REFERENCE_DUTY_CYCLE_MISMATCH`、`EDGE_JITTER_EXCEEDED`。程序不得猜测相位。

**方案含义与下一步：** 通过说明短片段可放入统一相位坐标，继续步骤 6；失败则排除当前参数下的拼接，调整参考质量或采集窗口后重测。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 6：能否重建 N 个独立完整周期

**问题与方案关系：** 多次分段采样只有在相位覆盖完整、片段重叠一致、样品稳定、周期边界连续，并且每个重建周期使用互不复用的片段时，才支持 `PHASE_STITCHING`。

**前置：** 步骤 4/5 通过；N、phase bin、每 bin 最少样本、重叠/漂移/边界阈值和最大尝试次数已确认。

```powershell
# Legacy：在线采集通过后，自动使用机器返回的实际目录离线复核。
$capture = Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('phase-stitch','capture','--config','src\daq_capability_test\field_test_matrix.tsv','--output-dir','daq_capability_results\legacy_phase') -ExpectedCode 'N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED' -RequiredNonEmptyEvidenceFields @('run_directory','requested_waveforms','reconstructed_waveforms')
$runDirectory = $capture.evidence.run_directory
if (-not $runDirectory) { throw 'exit=0 code=RUN_DIRECTORY_MISSING message=capture PASS 但未返回 evidence.run_directory evidence={}' }
$reconstruct = Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('phase-stitch','reconstruct','--input-dir',$runDirectory,'--config','src\daq_capability_test\field_test_matrix.tsv','--output-dir','daq_capability_results\legacy_reconstruct') -ExpectedCode 'N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED' -RequiredNonEmptyEvidenceFields @('run_directory','requested_waveforms','reconstructed_waveforms')
"PASS code=$($reconstruct.code) input=$runDirectory output=$($reconstruct.evidence.run_directory)"
```

```powershell
# XNavi：在线采集通过后，自动使用机器返回的实际目录离线复核。
$capture = Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('phase-stitch','capture','--config','src\daq_capability_test\field_test_matrix.tsv','--output-dir','daq_capability_results\xnavi_phase') -ExpectedCode 'N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED' -RequiredNonEmptyEvidenceFields @('run_directory','requested_waveforms','reconstructed_waveforms')
$runDirectory = $capture.evidence.run_directory
if (-not $runDirectory) { throw 'exit=0 code=RUN_DIRECTORY_MISSING message=capture PASS 但未返回 evidence.run_directory evidence={}' }
$reconstruct = Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('phase-stitch','reconstruct','--input-dir',$runDirectory,'--config','src\daq_capability_test\field_test_matrix.tsv','--output-dir','daq_capability_results\xnavi_reconstruct') -ExpectedCode 'N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED' -RequiredNonEmptyEvidenceFields @('run_directory','requested_waveforms','reconstructed_waveforms')
"PASS code=$($reconstruct.code) input=$runDirectory output=$($reconstruct.evidence.run_directory)"
```

**PASS：** exit `0`，`PASS / N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED`；证据给出请求/实际 N、每周期互不复用的片段 ID、覆盖率、空 bin、重叠误差、响应漂移和边界跳变。**FAIL：** exit `2`，如 `INSUFFICIENT_PHASE_COVERAGE`、`EMPTY_PHASE_BINS`、`INSUFFICIENT_SEGMENT_OVERLAP`、`OVERLAP_MISMATCH`、`NON_STATIONARY_RESPONSE`、`WAVEFORM_BOUNDARY_DISCONTINUITY`、`INSUFFICIENT_COMPLETE_WAVEFORMS`、`MAX_ATTEMPTS_EXCEEDED`；失败结果不会带有效完成标记。

**方案含义与下一步：** 只有该 PASS 才接受 `PHASE_STITCHING`，且只接受证据声明的 N；失败按专用 code 定位覆盖、重叠、漂移或边界问题。随后可继续步骤 7 验证独立的硬件触发方案。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 7：外部触发能否统一起点

**问题与方案关系：** 验证脉冲边沿能否稳定启动采集；失败只否定硬件触发统一起点，不自动否定 AI1 软件相位拼接。

**前置：** 同步输出已安全接外部触发端，触发源/边沿已填写，并把 `external_trigger` 设为 `true`。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','external_trigger') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'external_trigger' -ExpectedCaseCode 'EXTERNAL_TRIGGER_STABLE'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','external_trigger') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'external_trigger' -ExpectedCaseCode 'EXTERNAL_TRIGGER_STABLE'
```

**PASS：** exit `0`，`PASS / EXTERNAL_TRIGGER_STABLE`，三次触发且起点抖动达标。**FAIL：** exit `3 / SKIP`（`TRIGGER_UNSUPPORTED`），exit `6`（`TRIGGER_TIMEOUT` 或 `TRIGGER_CONFIG_FAILED`），或 exit `2`（`TRIGGER_START_JITTER_EXCEEDED`）。

**方案含义与下一步：** 通过后继续步骤 8；失败则不能使用硬件触发统一起点或延迟方案，但步骤 6 已通过的 AI1 软件拼接结论不受影响。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 8：触发延迟能否覆盖不同周期位置

**问题与方案关系：** 验证多个 delay count 的实测位置和整体覆盖；仅步骤 7/8 都通过才支持 `TRIGGER_DELAY`。

**前置：** 步骤 7 通过，delay count 和最大位置误差已填写，`delay_trigger` 已启用。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','delay_trigger') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'delay_trigger' -ExpectedCaseCode 'DELAY_TRIGGER_WINDOW_COVERED'
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','delay_trigger') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'delay_trigger' -ExpectedCaseCode 'DELAY_TRIGGER_WINDOW_COVERED'
```

**PASS：** exit `0`，`PASS / DELAY_TRIGGER_WINDOW_COVERED`。程序先用普通触发校准参考周期和占空比，再针对每个 `trigger_delay_counts` 的每次 repeat 成对执行同一 delay 的长窗口 phase probe 和短窗口采集；probe 实测起始相位必须匹配等长的 `trigger_delay_target_phase_percent`，probe 周期必须匹配校准周期和配置频率，短窗口完整参考波形必须匹配 probe 相位。所有短窗口按精确圆周区间合并后必须无缝覆盖 100% 相位。preflight 强制每个短窗口不足一周期且至少配置两个 delay，完整覆盖必须来自多个 delay；程序不使用 `count / sample_rate` 推断 delay 时间。calibration、probe、short 分别写入唯一 raw 文件和 summary 记录。**FAIL：** exit `3 / SKIP` 表示驱动不支持触发延迟；exit `2` 为 `DELAY_POSITION_MISMATCH`、`DELAY_SHORT_PHASE_INCONSISTENT`、`TRIGGER_REFERENCE_PERIOD_DRIFT`、`TRIGGER_REFERENCE_FREQUENCY_MISMATCH`、`TRIGGER_REFERENCE_PERIOD_MISSING` 或 `DELAY_WINDOW_INCOMPLETE`；exit `6` 为驱动调用/超时错误，均拒绝 `TRIGGER_DELAY`。

`measured_duration_seconds` 只表示返回采样点构成的采样窗口，`trigger_wait_seconds` 表示触发等待，墙钟总耗时单独记录；相位覆盖只使用“实际返回点数 / 实际采样率”，触发等待再长也不会扩大覆盖区间。

单独检查一个 delay 时可运行 `trigger ... --action delay_to_start --delay <count> --target-phase-percent <0..100> --delay-tolerance <us>`。未提供 `--target-phase-percent` 时命令只报告参考边沿和抖动，不宣称 delay 位置已验证；`--delay-tolerance` 仅在提供独立目标相位后参与判定。

**方案含义与下一步：** 步骤 7/8 都通过才接受 `TRIGGER_DELAY`；否则按机器原因修改 delay 或排除方案。继续步骤 9 生成统一结论。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

### [ ] 步骤 9：全部启用问题能否一次通过

**问题与方案关系：** 按依赖顺序汇总所有启用检查，机器给出可采用和被排除的方案；单项失败仍继续收集其他独立问题。

**前置：** 步骤 0 通过，所有已启用 case 的接线已完成。

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--all') -ExpectedCode 'SUITE_PASSED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--all') -ExpectedCode 'SUITE_PASSED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
```

**PASS：** exit `0`，末行 JSON `result=PASS`，并列出 `supported_strategies`、`rejected_strategies`、`skipped_cases` 和证据。**FAIL：** 任一 FAIL 导致非零退出；`failed_cases` 列出所有错误 code/原因/证据，依赖不足为 `SKIP / PREREQUISITE_FAILED`。结论必须按 JSON，不得用“有些 case 通过”替代总失败。

**方案含义与下一步：** 以 `supported_strategies` 作为可采用方案清单，以 `rejected_strategies` 和 `failed_cases` 安排整改；整改后用 `--case` 或 `--from` 重跑，最终保存完整输出目录作为验收记录。

**现场记录：** 执行版本：____；执行时间：____；进程退出码：____；最终 result/code：____；输出目录：____；现场备注：____。

## 一键、单项与断点执行

一键执行使用 `--all`；单独定位一个问题使用 `--case`；修正现场问题后从指定 case 到末尾使用 `--from`。三者互斥。Legacy 和 XNavi 均支持：

```powershell
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--all') -ExpectedCode 'SUITE_PASSED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','phase_stitch') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'phase_stitch' -ExpectedCaseCode 'N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_legacy.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--from','dual_channel_reference') -ExpectedCode 'SUITE_PASSED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--all') -ExpectedCode 'SUITE_PASSED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--case','phase_stitch') -ExpectedCode 'SUITE_PASSED' -ExpectedCase 'phase_stitch' -ExpectedCaseCode 'N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_xnavi.exe' -Arguments @('suite','--config','src\daq_capability_test\field_test_matrix.tsv','--from','dual_channel_reference') -ExpectedCode 'SUITE_PASSED' -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
```

`--case` 仍会自动检查该 case 的依赖，不能绕过安全前置；`--from` 是重新采集，不会把上次结果当成本次通过证据。

## 上机前本地 Mock

Mock 使用与现场程序相同的 CLI、矩阵解析、采集调度、相位重建、自动判定和结果写入，但不加载厂商头文件、DLL 或设备。先执行：

```powershell
scons -Q cpp_build/daq_capability_test_mock.exe
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_mock.exe' -Arguments @('suite','--config','src\daq_capability_test\mock_success.tsv','--all') -ExpectedCode 'SUITE_PASSED' -ExpectedSupportedStrategies @('LOW_SAMPLE_RATE','PHASE_STITCHING','TRIGGER_DELAY') -StrategyMatch Exact -RequiredTopLevelFields @('supported_strategies','rejected_strategies','skipped_cases','cases','failed_cases')
Invoke-DaqValidation -Executable 'cpp_build\daq_capability_test_mock.exe' -Arguments @('suite','--config','src\daq_capability_test\mock_non_stationary.tsv','--all') -ExpectedExit 2 -ExpectedResult 'FAIL' -ExpectedCode 'SUITE_FAILED' -ExpectedFailedCase 'phase_stitch' -ExpectedFailedCode 'NON_STATIONARY_RESPONSE'
python -m unittest tests.daq_capability_test.test_mock_cli -v
```

第一条 suite 应 exit `0` 并支持 `LOW_SAMPLE_RATE`、`PHASE_STITCHING`、`TRIGGER_DELAY`；non-stationary 场景应故意 exit `2` 并报告 `NON_STATIONARY_RESPONSE`；Python 测试还逐项确认短读、超时、溢出、参考缺失、边沿缺失/抖动、覆盖不足、重叠不一致、边界跳变和触发延迟错误都保留专用 code。

Mock 能在上机前发现配置解析、依赖顺序、返回码、JSON、错误传播、N 个周期的片段独立性、写盘和失败兜底问题。它不能证明 PCI-1714U、实际 DLL/API、量程、接线、电气安全、真实吞吐、触发或样品稳定性；这些必须按步骤 0-9 在目标机实测。

结果写入 `daq_capability_results/<时间戳>/`（或命令指定的输出根目录）。产物按命令和执行结果生成，不能假定每个目录都有全部文件：

| 条件 | 应有产物 |
| --- | --- |
| 所有已开始的运行 | `environment.tsv`、`summary.tsv`、`test_log.txt` |
| `suite` | 上述文件及本次实际配置快照 `matrix.tsv` |
| 采集 case、phase capture | 上述适用文件及 `raw/*.tsv` |
| `capability` | 上述通用文件及 `capability.tsv` |
| 整个命令成功 | `capture.complete` |
| 命令失败、SKIP 或输出未完整落盘 | 不得存在 `capture.complete` |

正式验收保存整个结果目录。`capture.complete` 只证明该命令要求的数据已经完整写入且命令判定成功；其余文件是否存在必须按上表和实际命令判断。
