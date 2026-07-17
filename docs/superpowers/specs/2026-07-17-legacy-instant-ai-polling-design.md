# Legacy Instant AI 轮询验证设计

## 目标

在现有 `daq_capability_test_legacy.exe` 中增加独立的
`instant-ai-polling` 命令，使用 Legacy `InstantAiCtrl::ReadAny()` 连续轮询
30 秒，保存每次读取的时间和通道值，并报告实际软件轮询间隔分布。

该验证只回答 Instant AI 在当前 Windows、驱动和负载下能否持续成功轮询。
它不把软件轮询结果解释为硬件时钟等间隔采样，也不宣称不存在未观测到的输入变化。

## 范围

- 只支持 Legacy 构建。
- 不改变现有 Buffered AI adapter、suite、矩阵或正式 `sample.exe`。
- 不为 Mock 或 XNavi 实现 Instant AI 硬件采集。
- 本次不实现采集过程中的实时 Python 消费。
- 增加一个采集完成后读取 TSV 并逐点打印的 Python 脚本。

## 命令接口

```powershell
cpp_build\daq_capability_test_legacy.exe instant-ai-polling `
  --device 'PCI-1714,BID#0' `
  --channels 0 `
  --range '-10V~10V' `
  --duration 30 `
  --output-dir daq_capability_results\legacy_instant
```

参数规则：

- `--device` 沿用现有设备描述规则和默认设备。
- `--channels` 沿用现有连续通道列表格式，至少一个通道。
- `--range` 必须精确映射到设备报告的受支持量程。
- `--duration` 为正数秒，默认 `30`。
- `--max-gap-ms` 可选且必须为正数；未提供时只报告间隔，不据此判定失败。
- `--output-dir` 沿用现有结果目录规则。

Mock 或 XNavi 构建收到该命令时返回明确的 `SKIP`/unsupported 结果，不尝试加载
Legacy Instant AI 入口。

## 采集流程

Legacy Instant AI 逻辑与现有 `BufferedAiCtrl` 分离：

1. 加载现场 `biodaq.dll` 并检查 `AdxInstantAiCtrlCreate`。
2. 创建 `InstantAiCtrl`，选择设备。
3. 查询通道集合并为所有请求通道设置量程。
4. 使用单调高精度时钟记录测试起点。
5. 在持续时间到达前反复调用一次 `ReadAny(channelStart, channelCount, ...)`。
6. 每次成功后记录：
   - 从测试起点算起的完成时间；
   - 本次 `ReadAny` 调用耗时；
   - 与上一次成功读取完成时间的间隔；
   - 每个请求通道的缩放电压值。
7. 到达持续时间后释放控制器并写出汇总。

通道必须连续，因为 `ReadAny` 使用 `channelStart + channelCount`。非连续列表在调用
驱动前返回参数错误。

任何 `ReadAny` 驱动错误立即停止测试并返回 FAIL，证据保留失败阶段、驱动错误、
已成功读取次数和已运行时间。NaN 或无穷值返回数据验证失败。

## 结果和判定

基础成功条件：

- 在整个请求时长内没有 `ReadAny` 调用失败；
- 至少成功读取一次；
- 所有返回值均为有限数；
- 原始数据和汇总完整写入。

成功 code 为 `INSTANT_AI_POLLING_STABLE`。

提供 `--max-gap-ms` 时，相邻成功读取完成时间的最大间隔不得超过阈值；超过时返回
exit `2`、`FAIL / INSTANT_AI_GAP_EXCEEDED`。未提供阈值时，间隔分布只作为证据，
不参与 PASS/FAIL。

汇总 JSON evidence 至少包括：

- `requested_duration_seconds`
- `wall_duration_seconds`
- `successful_reads`
- `failed_reads`
- `reads_per_second`
- `mean_interval_us`
- `p95_interval_us`
- `p99_interval_us`
- `max_interval_us`
- `max_gap_threshold_ms`（未提供时为空）
- 每通道的最小值、最大值和峰峰跨度
- `run_directory`

只有一个成功读数时，所有相邻间隔统计量为 `0`，而不是空值或 NaN。

## 输出文件

沿用统一运行目录和完成标记契约：

```text
<output-root>/<timestamp>/
  environment.tsv
  summary.tsv
  test_log.txt
  raw/
    instant_ai_polling.tsv
  capture.complete
```

`raw/instant_ai_polling.tsv` 使用制表符分隔，表头为：

```text
read_index	elapsed_seconds	call_duration_us	interval_us	channel_0...
```

通道列使用实际通道编号，例如请求 `--channels 1,2` 时列名为
`channel_1`、`channel_2`。第一行样本的 `interval_us` 为 `0`。

只有整个命令 PASS 且所有文件完整落盘时生成 `capture.complete`。失败结果保留已写出的
诊断文件，但不得生成完成标记。

## Python 离线打印

新增脚本：

```powershell
python scripts\print_instant_ai_samples.py `
  daq_capability_results\legacy_instant\<timestamp>\raw\instant_ai_polling.tsv
```

脚本使用 Python 标准库读取 TSV，验证固定列和至少一个 `channel_<n>` 列，然后按文件
顺序逐点打印：

```text
read_index=0 elapsed_seconds=0.000084 call_duration_us=78.2 interval_us=0 channel_0=1.234
```

空文件、缺少表头、缺少固定列、没有通道列或非数值字段都写入 stderr 并返回非零。
正常打印完成返回 `0`。脚本不修改采样文件，也不把打印速度用于评价采集连续性。

## 测试

自动测试覆盖：

- CLI 接受 Legacy `instant-ai-polling` 及默认30秒。
- 非法 duration、max gap、空通道和非连续通道在驱动调用前失败。
- 间隔统计的空边界、单样本和多样本百分位计算。
- 无阈值时只报告最大间隔；有阈值且超限时返回专用 code。
- `ReadAny` 失败和非有限数据保留专用失败证据。
- 原始 TSV 的表头、通道编号、首行间隔及完成标记契约。
- Python 脚本正确打印有效 TSV，并拒绝损坏输入。
- 现有 Buffered AI、suite、Mock 和 XNavi 测试保持通过。

真实 `InstantAiCtrl` 创建、驱动入口、轮询间隔和硬件通道值必须在 PCI-1714U 目标机上
执行30秒命令验证；自动测试不能替代该硬件结论。
