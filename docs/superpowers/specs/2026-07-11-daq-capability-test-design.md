# 采集卡能力测试设计

## 问题

当前 PCI-1714 采样路径采用固定的单通道采集流程，没有暴露选择可靠高阻测量方案所需的硬件和驱动边界。团队尤其需要获得低采样率、长时间采集、双通道同步采样、外部触发和触发延迟的实测证据。

现有采样程序包含一份旧版 `bdaqctrl.h`，当前 XNavi 安装提供了变化较大的新版头文件和 `biodaq.dll` 运行库。两版的上层 C++ 概念基本相似，但头文件与运行库的绑定 ABI 已经改变：旧版头文件逐个解析入口函数，新版头文件通过 `AdxDaqNaviLibInitialize` 获取分组函数表。因此，两版头文件不能混入同一个可执行文件。

## 目标

- 增加一个独立的命令行硬件能力测试工具，不改变正式采样行为。
- 使用 SCons，从共享测试核心分别构建 legacy 和 XNavi 两个版本。
- 查询采集卡报告的能力，并执行单通道、双通道、外部触发和延迟触发采集。
- 在线采集或离线读取分段数据，实际重建 N 个互不复用原始片段的独立完整周期。
- 保存原始数据、环境信息、详细日志和机器可读的汇总结果，便于后续复核。
- 在 README 中提供验证操作手册，明确每项测试的目的、准确命令、自动成功返回、自动失败原因，以及结果对各候选测量方案的影响。

## 非目标

- 除移动旧版头文件并调整 `src/sampler/real_sampler.hpp` 的 include 路径外，不修改正式采样源码或已有可执行程序的行为。
- 不把该工具集成到 KDM3000 或其正式 UI 中。
- 不通过该工具控制脉冲发生器。
- 不修改高阻拟合算法，也不把重建波形接入正式测量流程。
- 不重新分发研华 DLL、设备驱动或安装包。
- 不根据 DemoDevice 的结果推断 PCI-1714U 的实际硬件行为。

## 约束

- 源码仓库：`D:\kunde\code\Half.Sample`。
- 构建系统：使用仓库现有的 SCons 构建。
- 输出程序：
  - `cpp_build/daq_capability_test_legacy.exe`
  - `cpp_build/daq_capability_test_xnavi.exe`
- legacy 程序使用 `src/daq_headers/legacy/bdaqctrl.h`。
- XNavi 程序使用 `src/daq_headers/xnavi/bdaqctrl.h`。
- 现有旧版头文件移入 legacy 目录；`real_sampler.hpp` 只调整 include 路径，不改变采样行为。
- 两个版本只依赖头文件即可编译，不需要导入库。运行时需要正确安装 DAQNavi 运行库、设备专用 DLL、内核驱动和设备注册信息。
- 设计评审期间观察到的 XNavi 运行库为 `biodaq.dll 4.1.22.0`；程序必须报告实际运行库版本，不能假定固定版本。
- 当前开发机没有实际 PCI-1714U，最终硬件验收必须在目标工控机上完成。

## 架构

新增独立的 `src/daq_capability_test/` 目录：

```text
src/daq_capability_test/
  main.cpp
  cli.cpp
  cli.hpp
  test_runner.cpp
  test_runner.hpp
  result_writer.cpp
  result_writer.hpp
  daq_adapter.hpp
  legacy_adapter.cpp
  xnavi_adapter.cpp
  phase_stitcher.cpp
  phase_stitcher.hpp
  default_test_matrix.tsv
src/daq_headers/
  legacy/
    bdaqctrl.h
  xnavi/
    bdaqctrl.h
```

共享核心负责 CLI 解析、测试矩阵解析、重复执行、结果分类、日志和文件输出。共享核心只依赖 `daq_adapter.hpp` 中定义的窄接口。

适配器接口只暴露本诊断工具所需的操作：

- 加载运行库并报告版本；
- 选择并识别设备；
- 查询通道、采样率限制、最大扫描点数、缓冲区容量、触发源、触发动作和触发延迟范围；
- 配置通道、量程、采样、触发和延迟；
- 执行一次有限采集，并报告实际配置、返回点数、耗时及驱动或事件错误；
- 安全停止并释放控制器。

`legacy_adapter.cpp` 和 `xnavi_adapter.cpp` 分别编入不同的 SCons target。任何 target 都不能同时包含两版 DAQNavi 头文件或两个适配器。

所有源码使用带目录限定的头文件路径，禁止使用无法区分版本的 `#include "bdaqctrl.h"`。SCons 为两个 target 分别设置对应 include path，避免头文件冲突。

`phase_stitcher` 不依赖具体驱动，只处理带时间、通道和参考边沿信息的采样片段。每个原始片段最多归属于一个重建周期，N 个重建周期之间不得复用原始数据。

相位重建具有显式参考校准前提：至少一个 `reference_calibration` 片段必须包含
`rising -> falling -> next rising` 完整参考周期，用于测量周期、占空比和边沿抖动。校准片段消耗一次
`max_attempts` 采集尝试，但不参与波形 bin 分配，也不出现在重建波形的 segment IDs 中。普通短片段仅用
校准结果与其局部边沿索引映射相位，下降沿相位等于已校准占空比。

## 命令

两个可执行程序提供相同的命令接口：

```text
daq_capability_test_*.exe capability [options]
daq_capability_test_*.exe acquire [options]
daq_capability_test_*.exe trigger [options]
daq_capability_test_*.exe phase-stitch capture [options]
daq_capability_test_*.exe phase-stitch reconstruct --input-dir <path> [options]
daq_capability_test_*.exe suite --config <matrix.tsv> [options]
```

默认设备描述为 `PCI-1714,BID#0`，允许通过 CLI 覆盖。

### capability

连接指定设备，报告运行库版本、设备身份、通道、支持的采样率信息、最大扫描点数、缓冲区容量、触发数量、触发源、触发动作和触发延迟范围。该命令不启动采集。

### acquire

接收通道列表、量程、采样率、每通道点数、重复次数、超时和输出目录。支持单通道和双通道。记录请求值、从驱动回读的值、返回点数、耗时和所有检测到的错误状态。

### trigger

除普通采集参数外，还接收触发源、边沿、动作、delay count 和可选的抖动阈值。记录等待触发的时间并保存采集信号。存在参考通道时，计算参考边沿的时序统计数据。

### phase-stitch capture

同时采集样品通道和激励参考通道，解析每个有效片段的周期相位，持续采集并分配片段，直到重建出 N 个独立完整周期或达到最大尝试次数。无法确定相位的片段不得用于重建，但必须记录失败原因。

### phase-stitch reconstruct

读取已保存的原始片段，使用与在线模式相同的算法和阈值重新验证，不操作硬件，也不修改原始文件。该模式用于调整阈值、复核现场结果和重现失败。

### suite

读取 TSV 矩阵并按顺序执行测试。随程序提供的默认矩阵覆盖能力查询、500/200/100 kHz 采集、1600 万点边界、双通道参考、N 个独立周期重建、外部触发和延迟触发。每项测试默认重复 3 次。某项失败或不支持时，不阻止后续相互独立的测试继续执行。

`suite` 支持三种执行范围：

```text
--all           按依赖顺序执行全部验证
--case <name>   执行指定验证及其最小前置检查
--from <name>   从指定验证开始继续执行后续验证
```

一键执行顺序固定为：

```text
preflight
device_capability
single_channel_boundary
low_sample_rate
dual_channel_reference
segment_phase
phase_stitch
external_trigger
delay_trigger
strategy_decision
```

前置条件失败时，依赖该条件的验证返回 `SKIP / PREREQUISITE_FAILED`，不相关的后续验证继续执行。`--from` 创建新的运行记录，不覆盖之前的输出。

## 配置与输出

与硬件相关的值通过 CLI 或测试矩阵输入，不硬编码为程序策略。矩阵至少包含测试名称、模式、设备、通道、量程、采样率、每通道点数、重复次数、超时、触发源、触发边沿、触发动作、delay count 和可选的抖动阈值。

### 示例测试矩阵

`default_test_matrix.tsv` 是可复制修改的完整示例，不是空模板。文件顶部必须使用以 `#` 开头的注释逐项说明：

- 参数名称和用途；
- 单位；
- 允许值或格式；
- 默认值的含义；
- 哪些参数只适用于指定 case；
- 电压、量程、触发接线等硬件安全注意事项。

解析器必须忽略空行和第一个非空字符为 `#` 的注释行。注释区之后保留一行正式 TSV 表头和覆盖全部 case 的默认数据行。

示例配置至少包含以下字段：

```text
case_name
enabled
device
mode
sample_channel
reference_channel
value_range
sample_rate_hz
points_per_channel
repeat_count
timeout_seconds
signal_frequency_hz
min_complete_cycles
min_signal_span_v
max_edge_jitter_us
reference_duty_cycle_percent
max_duty_cycle_error_percent
target_waveforms
phase_bin_count
min_samples_per_bin
min_overlap_percent
max_overlap_error_v
max_response_drift_v
max_boundary_jump_v
max_attempts
trigger_source
trigger_edge
trigger_action
trigger_delay_counts
max_delay_position_error_us
```

默认数据行覆盖 `preflight`、`device_capability`、`single_channel_boundary`、`low_sample_rate_500k`、`low_sample_rate_200k`、`low_sample_rate_100k`、`dual_channel_reference`、`segment_phase`、`phase_stitch`、`external_trigger` 和 `delay_trigger`。

默认值必须满足以下原则：

- 设备默认为 `PCI-1714,BID#0`，样品通道为 AI0，参考通道为 AI1；
- 基础稳定性测试默认重复 3 次；
- 单通道边界默认使用 1 MHz 和每通道 1600 万点；
- 低采样率 case 分别使用 500、200 和 100 kHz；
- 不确定且可能影响业务结论或硬件安全的阈值使用显式的 `REQUIRED`，不得伪造通用安全值；
- `preflight` 遇到任何 `REQUIRED` 时返回 `REQUIRED_THRESHOLD_MISSING`，提示参数名和所在 case；
- 不适用于某个 case 的字段留空，不能用 `0` 混淆“不适用”和真实零值；
- 默认禁用需要现场接线的外部触发和 delay-trigger case，用户确认接线和参数后再设置 `enabled=true`。

README 步骤 0 提供复制命令：

```powershell
Copy-Item src\daq_capability_test\default_test_matrix.tsv src\daq_capability_test\field_test_matrix.tsv
```

`field_test_matrix.tsv` 是现场可修改配置，默认加入 `.gitignore`，避免现场参数和设备信息被意外提交。程序运行时把实际使用的配置原样复制到本次结果目录，保证结果可审计。

每次调用创建一个带时间戳的输出目录：

```text
daq_results/<timestamp>/
  environment.tsv
  capability.tsv
  summary.tsv
  test_log.txt
  raw/<test_name>_<repeat>.tsv
```

`environment.tsv` 记录可执行程序版本类型、程序版本或构建标识、操作系统架构、进程架构、DAQNavi 运行库路径和版本、设备描述及调用参数。

`summary.tsv` 至少记录测试名称、重复序号、请求和实际采样率、通道数量、请求和实际每通道点数、理论和实测时长、触发配置、等待时长、溢出标志、驱动错误码和错误阶段、`PASS/FAIL/SKIP` 及诊断备注。

原始 TSV 文件首列为样本序号，后续每列对应一个通道。原始文件先写入临时文件，只有预期数据全部成功写入后才重命名为最终文件。

## 命令返回契约

每个命令都必须由程序自动完成判定。正式结果由进程退出码和 stdout 最后一行的单行 JSON 共同表达，不要求用户读取 TSV 后人工判断。

统一退出码：

```text
0  PASS
2  验证结论失败
3  硬件或驱动明确不支持，SKIP
4  命令参数或矩阵无效
5  运行环境错误，例如 DLL、设备或入口点缺失
6  DAQNavi 调用或采集错误
7  输出文件写入错误
```

成功结果示例：

```json
{"result":"PASS","code":"ACQUISITION_STABLE","message":"3/3 acquisitions passed","evidence":{"actual_points":16000000,"duration_error_percent":0.12}}
```

失败结果示例：

```json
{"result":"FAIL","code":"CACHE_OVERFLOW","message":"cache overflow on repetition 2","stage":"acquire","driver_error":"0xE0000001"}
```

`suite` 中只要存在一个 `FAIL`，进程就返回非零。最终 JSON 必须列出全部失败项和原因；`SKIP` 单独列出，不能掩盖失败。

## 结果分类

默认稳定性标准为连续 3 次成功。基础采集测试只有在所有重复测试均满足以下条件时才通过：

- 每个 DAQNavi 调用均成功；
- 没有超时、overrun 或 cache overflow；
- 每个通道的实际点数等于请求点数；
- 双通道结果中每个通道的点数相同；
- 实测时长与理论时长的偏差不超过 1%；
- 从驱动回读的配置值与请求且受支持的配置值一致。

触发和同步测试还必须应用配置的抖动阈值。未提供阈值时，工具仍报告实测抖动，但不会自行设定业务验收阈值。

涉及真实信号和波形重建的测试从 CLI 或矩阵接收 `signal_frequency_hz`、`min_complete_cycles`、`min_signal_span_v`、`max_edge_jitter_us`、phase bin 数量、每 bin 最少样本数、最小重叠比例、最大重叠误差、最大响应漂移和最大尝试次数。缺少判定所必需的阈值时拒绝执行，不能降级为人工判断。

只有当指定设备或驱动明确报告某项能力不受支持时，才使用 `SKIP`。无效输入、运行库错误和采集失败均记为 `FAIL`，不能记为 `SKIP`。

## 错误处理与硬件安全

- 接触设备前，先验证所有 CLI 和矩阵参数。
- 配置前先查询驱动报告的能力范围；超出范围时拒绝执行，不静默截断参数。
- 检查每个 DAQNavi `ErrorCode`，记录十六进制错误码、操作阶段和相关输入值。
- 采集前从驱动回读配置值。
- 检测 `biodaq.dll` 缺失、必要入口点缺失、设备不可用、运行库与头文件不兼容、超时、overrun、cache overflow、短读和输出写入失败。
- 使用 RAII，确保所有退出路径都能在必要时停止采集，并调用适当的 cleanup/dispose 操作。
- 不修改正式配置文件，也不向脉冲发生器发送命令。
- 某个独立测试失败后继续执行 suite，同时保留该失败结果。

## 待验证问题与方案关系

README 和默认 suite 必须按以下问题组织。每个问题均提供验证目的、前置接线、准确命令、自动成功返回、自动失败返回，以及对候选方案的结论。TSV 和原始数据只用于审计。

本节用 `<variant>` 简写 legacy 和 xnavi 两种程序。README 中不得保留该简写，必须为 `cpp_build\daq_capability_test_legacy.exe` 和 `cpp_build\daq_capability_test_xnavi.exe` 分别给出可直接执行的完整命令。

### 问题 1：设备和驱动是否可用

命令：

```powershell
daq_capability_test_<variant>.exe capability --device "PCI-1714,BID#0"
```

成功返回 `PASS / DEVICE_CAPABILITY_READY`。失败返回 `RUNTIME_NOT_FOUND`、`DEVICE_NOT_FOUND`、`ENTRY_POINT_MISSING`、`HEADER_RUNTIME_INCOMPATIBLE` 或 `CAPABILITY_QUERY_FAILED`。这是所有候选方案的共同前置条件。

### 问题 2：现有单通道 1600 万点边界是否稳定

命令：

```powershell
daq_capability_test_<variant>.exe acquire --channels 0 --rate 1000000 --points 16000000 --repeat 3
```

成功返回 `PASS / ACQUISITION_STABLE`。失败返回 `POINT_COUNT_MISMATCH`、`DURATION_OUT_OF_TOLERANCE`、`TIMEOUT`、`OVERRUN` 或 `CACHE_OVERFLOW`。该结果建立现有采集能力基准。

### 问题 3：降低采样率能否覆盖完整低频周期

分别在 500、200 和 100 kHz 下运行 `acquire`，传入信号频率、最少完整周期数和最低信号幅值。成功返回 `PASS / LOW_RATE_WINDOW_VALID`。失败返回 `WINDOW_TOO_SHORT`、`SIGNAL_SPAN_TOO_LOW` 或采集类错误。只有成功时，“降低采样率采完整周期”方案才具备硬件可行性。

### 问题 4：是否具备参考相位采集条件

该方案要求在样品之前取得激励参考信号：激励一路经过样品和测量链路接入 AI0，另一路经过必要的分压、保护、缓冲或隔离接入 AI1。仅有样品之后的一路信号时，双通道参考方案不成立。

双通道 `acquire` 成功返回 `PASS / DUAL_CHANNEL_REFERENCE_READY`。失败返回 `REFERENCE_PATH_REQUIRED`、`DUAL_CHANNEL_UNSUPPORTED`、`REFERENCE_SIGNAL_INVALID` 或 `CHANNEL_POINT_COUNT_MISMATCH`。该问题只验证软件相位拼接的硬件前置条件，不单独证明拼接成功。

### 问题 5：参考信号能否确定每个片段的周期相位

程序从 AI1 自动检测上升沿和下降沿，将 AI0 样本映射到统一周期坐标。成功返回 `PASS / SEGMENT_PHASE_RESOLVED`。失败返回 `EDGE_NOT_FOUND`、`SEGMENT_PHASE_UNKNOWN`、`REFERENCE_FREQUENCY_MISMATCH` 或 `EDGE_JITTER_EXCEEDED`。

方波平台内没有任何参考边沿且采集起点未知时，程序不得猜测相位，必须返回 `SEGMENT_PHASE_UNKNOWN`。外部触发、已知 delay count 或包含边沿的片段可以提供已知相位。

### 问题 6：能否重建 N 个独立完整周期

在线采集命令使用 `phase-stitch capture`，离线复核使用 `phase-stitch reconstruct --input-dir <path>`。成功必须返回 `PASS / N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED`，并自动证明：

- 重建数量等于 N；
- N 个周期不共享原始片段；
- 每个周期相位覆盖率为 100%；
- 没有空 phase bin；
- 片段重叠比例和重叠区域误差达标；
- 周期边界连续；
- 同相位响应、基线、幅值和形状没有超过阈值的漂移。

失败返回 `INSUFFICIENT_PHASE_COVERAGE`、`EMPTY_PHASE_BINS`、`INSUFFICIENT_SEGMENT_OVERLAP`、`OVERLAP_MISMATCH`、`NON_STATIONARY_RESPONSE`、`WAVEFORM_BOUNDARY_DISCONTINUITY`、`INSUFFICIENT_COMPLETE_WAVEFORMS` 或 `MAX_ATTEMPTS_EXCEEDED`。只有本问题成功，才能确认“多次分段采样 + 相位拼接”方案走通。

### 问题 7：硬件触发和延迟采样是否可行

`trigger` 命令分别验证外部触发和多个 delay count。成功返回 `EXTERNAL_TRIGGER_STABLE` 和 `DELAY_TRIGGER_WINDOW_COVERED`。失败返回 `TRIGGER_UNSUPPORTED`、`TRIGGER_TIMEOUT`、`TRIGGER_START_JITTER_EXCEEDED`、`TRIGGER_DELAY_UNSUPPORTED`、`DELAY_POSITION_MISMATCH` 或 `DELAY_WINDOW_INCOMPLETE`。

外部触发可以统一采集起点，但不是软件相位拼接的必要条件。外部触发和 delay 均成功时，可以主动选择采样片段的周期位置，减少随机等待参考边沿的低效率。

### 问题 8：最终支持哪些方案

`suite` 自动汇总前述问题，末行 JSON 输出 `supported_strategies` 和带原因的 `rejected_strategies`。至少区分 `LOW_SAMPLE_RATE`、`PHASE_STITCHING` 和 `TRIGGER_DELAY`。程序直接给出方案结论，不要求人工解释 TSV。

## 开发期与现场验证边界

- 无实际硬件时，构建两个新增 SCons target，确认现有 target 仍可构建，并使用 fake adapter 自动测试参数、矩阵、退出码、JSON、失败后继续、TSV 和重建算法。
- 使用 XNavi DemoDevice 时，只验证新版头文件、运行库和代表性 API 路径，不据此判断 PCI-1714U 硬件能力。
- 在目标 PCI-1714U 系统上，使用实际激励分路、样品、测量链路和保护电路执行 README 中的准确命令。退出码和末行 JSON 是正式判定，完整输出目录用于审计。

## 现场验证操作手册 Check-list

README 必须完整包含本操作手册。执行正式验证前，应一次性完成 AI0 样品路径、AI1 参考支路和外部触发线的安全接线；无法提供的接线能力应在配置中明确禁用，不能让程序猜测。

### [ ] 步骤 0：填写并检查现场验收参数

**验证目的**

把所有业务阈值和硬件参数固定在可审计配置中，使后续命令能够自动判定，不依赖人工查看波形。

**配置文件**

先执行以下命令复制带注释和默认值的示例，再填写现场使用的 `field_test_matrix.tsv`：

```powershell
Copy-Item src\daq_capability_test\default_test_matrix.tsv src\daq_capability_test\field_test_matrix.tsv
```

至少明确：设备描述、AI0/AI1 通道、量程、500/200/100 kHz 测试点数、信号频率、目标周期数 N、最低信号幅值、最大边沿抖动、phase bin 数量、每 bin 最少样本数、最小重叠比例、最大重叠误差、最大响应漂移、最大边界跳变、最大尝试次数、触发源和 delay count 列表。所有 `REQUIRED` 均替换为现场确认值后，才能通过 preflight。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case preflight
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case preflight
```

**成功返回**

退出码 `0`，末行 JSON 为 `PASS / PREFLIGHT_READY`。

**失败返回**

`CONFIG_NOT_FOUND`、`CONFIG_PARSE_ERROR`、`REQUIRED_THRESHOLD_MISSING`、`INVALID_CHANNEL`、`INVALID_RANGE`、`INVALID_TEST_MATRIX` 或 `UNSAFE_REFERENCE_VOLTAGE`。失败时先修正配置或接线，不得继续一键验证。

### [ ] 步骤 1：确认驱动和采集卡可用

**验证目的**

确认程序能够加载对应 `biodaq.dll`、找到 `PCI-1714,BID#0` 并读取能力。该步骤失败时，所有采集方案均不能继续。

**前置条件**

PCI-1714U 和对应驱动已安装，Windows 设备管理器无设备错误。本步骤不要求启动脉冲输出。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case device_capability
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case device_capability
```

**成功返回**

退出码 `0`，`PASS / DEVICE_CAPABILITY_READY`。`evidence` 包含设备、运行库版本、通道数、buffer capacity 和触发能力。

**失败返回**

`RUNTIME_NOT_FOUND`、`ENTRY_POINT_MISSING`、`HEADER_RUNTIME_INCOMPATIBLE`、`DEVICE_NOT_FOUND` 或 `CAPABILITY_QUERY_FAILED`。失败时停止后续硬件验证，先修复驱动或设备环境。

### [ ] 步骤 2：确认单通道 1600 万点边界

**验证目的**

验证现有 1 MHz、1600 万点单通道基准能否连续 3 次稳定完成，排除测试工具本身破坏既有能力。

**前置条件**

步骤 1 通过，AI0 已连接安全范围内的样品测量信号。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case single_channel_boundary
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case single_channel_boundary
```

**成功返回**

退出码 `0`，`PASS / ACQUISITION_STABLE`，3/3 次点数相等、时长误差不超过 1%，无 timeout、overrun 或 cache overflow。

**失败返回**

`POINT_COUNT_MISMATCH`、`DURATION_OUT_OF_TOLERANCE`、`TIMEOUT`、`OVERRUN`、`CACHE_OVERFLOW` 或 DAQNavi 错误。失败说明现有能力基准未成立，应先处理采集稳定性，不能据此评价新方案。

### [ ] 步骤 3：验证降低采样率方案

**验证目的**

判断 500、200 和 100 kHz 是否能在固定点数内覆盖配置要求的完整低频周期，同时保持最低有效信号幅值。

**前置条件**

步骤 2 通过，脉冲发生器和代表性样品按正式测量路径连接并稳定输出。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case low_sample_rate
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case low_sample_rate
```

**成功返回**

退出码 `0`，`PASS / LOW_RATE_WINDOW_VALID`。`evidence` 分别列出每个采样率的完整周期数、信号幅值、实际点数和时长误差。

**失败返回**

`WINDOW_TOO_SHORT`、`SIGNAL_SPAN_TOO_LOW`、`POINT_COUNT_MISMATCH` 或采集类错误。全部目标采样率失败时拒绝 `LOW_SAMPLE_RATE`；部分成功时只报告通过的采样率，不得笼统判定整个方案通过。

### [ ] 步骤 4：确认双通道参考路径可用

**验证目的**

确认 AI0 能采集样品响应、AI1 能同步采集样品之前分出的激励参考，且两个通道的点数和时间轴一致。

**前置条件**

激励一路经过样品和测量链路接 AI0，另一路经过分压、保护、缓冲或隔离接 AI1。AI1 电压位于配置量程内，共地或隔离方式已确认安全。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case dual_channel_reference
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case dual_channel_reference
```

**成功返回**

退出码 `0`，`PASS / DUAL_CHANNEL_REFERENCE_READY`。程序自动确认两个通道点数相等、参考信号幅值有效且三次采集稳定。

**失败返回**

`REFERENCE_PATH_REQUIRED`、`DUAL_CHANNEL_UNSUPPORTED`、`REFERENCE_SIGNAL_INVALID`、`CHANNEL_POINT_COUNT_MISMATCH` 或 `UNSAFE_REFERENCE_VOLTAGE`。失败时相位拼接方案不能继续。

### [ ] 步骤 5：确认每个采样片段可以定位相位

**验证目的**

从 AI1 方波上升沿或下降沿确定 AI0 每个样本在激励周期中的位置，拒绝相位未知的片段。

**前置条件**

步骤 4 通过，配置中的信号频率、边沿阈值和最大抖动已确定。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case segment_phase
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case segment_phase
```

**成功返回**

退出码 `0`，`PASS / SEGMENT_PHASE_RESOLVED`。`evidence` 包含检测边沿数、实测周期、周期误差和最大边沿抖动。

**失败返回**

`EDGE_NOT_FOUND`、`SEGMENT_PHASE_UNKNOWN`、`REFERENCE_FREQUENCY_MISMATCH` 或 `EDGE_JITTER_EXCEEDED`。方波平台片段中没有边沿且采集起点未知时必须返回 `SEGMENT_PHASE_UNKNOWN`，不能猜测相位。

### [ ] 步骤 6：重建 N 个独立完整周期

**验证目的**

实际证明多次分段采样可以重建配置要求的 N 个完整周期，并自动排除相位缺口、片段不一致和样品漂移。

**前置条件**

步骤 4 通过；步骤 5 能产生有效相位片段；样品响应在重复激励下应稳定。所有重建阈值已在配置中确定。

**Legacy 在线命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case phase_stitch
```

**XNavi 在线命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case phase_stitch
```

**离线复核命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe phase-stitch reconstruct --input-dir <现场输出目录> --config src\daq_capability_test\field_test_matrix.tsv
```

**成功返回**

退出码 `0`，`PASS / N_INDEPENDENT_WAVEFORMS_RECONSTRUCTED`。`evidence` 必须给出请求和实际重建数量、每周期片段 ID、相位覆盖率、空 bin 数、最小重叠比例、最大重叠误差、最大响应漂移和最大边界跳变。

**失败返回**

`INSUFFICIENT_PHASE_COVERAGE`、`EMPTY_PHASE_BINS`、`INSUFFICIENT_SEGMENT_OVERLAP`、`OVERLAP_MISMATCH`、`NON_STATIONARY_RESPONSE`、`WAVEFORM_BOUNDARY_DISCONTINUITY`、`INSUFFICIENT_COMPLETE_WAVEFORMS` 或 `MAX_ATTEMPTS_EXCEEDED`。失败时不得生成标记为有效的重建波形。

### [ ] 步骤 7：验证外部触发

**验证目的**

判断采集卡能否由脉冲边沿稳定触发，使各次采集从一致的相位起点开始。

**前置条件**

脉冲发生器同步或参考输出已安全连接到 PCI-1714U 外部触发接口，触发源和边沿已写入配置。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case external_trigger
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case external_trigger
```

**成功返回**

退出码 `0`，`PASS / EXTERNAL_TRIGGER_STABLE`，三次均成功触发且起点抖动不超过配置阈值。

**失败返回**

`TRIGGER_UNSUPPORTED`、`TRIGGER_TIMEOUT`、`TRIGGER_START_JITTER_EXCEEDED` 或 DAQNavi 触发配置错误。失败不自动否定软件相位拼接，但否定基于硬件触发统一起点的方案。

### [ ] 步骤 8：验证触发后延迟覆盖

**验证目的**

判断多个 delay count 是否能主动采集周期中的不同位置，并覆盖配置要求的完整低频周期。

**前置条件**

步骤 7 通过，delay count 列表、目标相位位置和最大位置误差已写入配置。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case delay_trigger
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --case delay_trigger
```

**成功返回**

退出码 `0`，`PASS / DELAY_TRIGGER_WINDOW_COVERED`。每个延迟点的实测位置误差均达标，整体相位覆盖率为 100%。

**失败返回**

`TRIGGER_DELAY_UNSUPPORTED`、`DELAY_OUT_OF_RANGE`、`DELAY_POSITION_MISMATCH` 或 `DELAY_WINDOW_INCOMPLETE`。失败时拒绝 `TRIGGER_DELAY` 方案。

### [ ] 步骤 9：一键执行全部验证并取得方案结论

**验证目的**

按依赖顺序执行全部检查，并自动给出可采用和被排除的方案及原因。

**前置条件**

步骤 0 已通过，全部所需接线已经完成。

**Legacy 命令**

```powershell
cpp_build\daq_capability_test_legacy.exe suite --config src\daq_capability_test\field_test_matrix.tsv --all
```

**XNavi 命令**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --all
```

**成功返回**

退出码 `0` 表示所有启用且受支持的验证均通过。末行 JSON 包含 `supported_strategies`、`rejected_strategies`、`skipped_cases` 和每项证据摘要。

**失败返回**

只要存在一个 `FAIL`，退出码即为非零；末行 JSON 必须列出全部失败 case、错误 code、原因和受影响方案。依赖条件不足的项目返回 `SKIP / PREREQUISITE_FAILED`，不能掩盖其他失败。

**断点续跑**

```powershell
cpp_build\daq_capability_test_xnavi.exe suite --config src\daq_capability_test\field_test_matrix.tsv --from dual_channel_reference
```

### 每步操作结果记录

README 在每一步末尾保留以下现场记录模板：

```text
执行版本：Legacy / XNavi
执行时间：
进程退出码：
最终 result/code：
输出目录：
现场备注：
```

## 验收标准

- 两个相互隔离的 SCons target 构建成功，且现有 target 不受影响。
- 两个可执行程序提供相同的 CLI 命令和输出结构。
- legacy 和 XNavi 适配器只使用各自纳入源码管理的头文件编译。
- 工具在采集前报告环境和能力信息。
- 单通道、双通道、外部触发、延迟触发和 suite 路径均能生成可审计的结果。
- `phase-stitch capture` 和 `phase-stitch reconstruct` 能自动判定是否重建出 N 个互不复用原始片段的独立完整周期。
- 默认 suite 对每项测试重复 3 次，并应用已定义的分类规则。
- 每个命令均遵守统一退出码和末行 JSON 契约，失败结果包含明确原因和证据。
- 错误信息包含上下文且机器可读；失败时不会遗留仍在运行的控制器，也不会把不完整原始文件呈现为有效结果。
- README 按待验证问题组织，为每项验证提供准确命令、自动成功返回、自动失败原因，并把结果映射到方案决策。
- 自动化和 DemoDevice 检查与实际 PCI-1714U 验收有明确区分。
