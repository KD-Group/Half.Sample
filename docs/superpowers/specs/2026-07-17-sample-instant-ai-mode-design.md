# sample.exe Instant AI 自动选择设计

## 背景

`sample.exe` 当前仅使用 legacy `BufferedAiCtrl`。采集配置根据发射频率选择 Buffered AI
的内部采样率：

- 发射频率大于等于 10 Hz 时使用 20 MHz；
- 发射频率小于 10 Hz 时使用 1 MHz。

这两个内部采样率属于原有 Buffered AI 数据路径，不能与新增的 Instant AI 模式阈值混为
一谈，也不能因新增模式而改变。

硬件验证确认：

- `PCI-1714,BID#0` 支持 legacy `InstantAiCtrl`；
- 连续全速调用 `ReadAny` 虽然可达到约 46 k reads/s，但 30 秒数据可能只覆盖波形的局部
  电压范围；
- 按约 10 Hz 调用 `ReadAny` 时，30 秒数据能够观察到约 3 个完整的
  `-1.5 V ~ 1.5 V` 波形。

因此新增功能的目标不是追求最高 `ReadAny` 调用率，而是在低频周期信号中权衡采集耗时、
相位覆盖和波形平均精度。

## 目标

1. 仅使用 legacy `bdaqctrl.h`，为 `sample.exe` 增加 Instant AI 采集路径。
2. 根据用户传入的发射频率阈值自动选择 Buffered AI 或 Instant AI。
3. 默认每个 Instant AI 等效波形重建为 100 点。
4. 平均次数严格采用现有的 `number_of_waveforms` 参数。
5. 保证旧调用默认仍使用 Buffered AI，旧 Buffered AI 的采样、模拟和 τ 计算路径保持不变。
6. Instant AI 发生读取失败或数据质量不足时直接返回错误，不静默回退到 Buffered AI。

## 非目标

- 不支持 XNavi 版本。
- 不改变 PCI-1714 的设备描述、通道 0 或 `-5 V ~ 5 V` 量程。
- 不改变原有 Buffered AI 的 10 Hz 内部采样率分界。
- 不承诺 Instant AI 与 Buffered AI 具有相同的拟合点密度。
- 不在本次功能中重新设计 τ 拟合算法。

## 外部接口

Python 的 `measure()` 和 `dump()` 追加两个可选参数：

```python
sampler.measure(
    number_of_waveforms=3,
    emitting_frequency=0.1,
    auto_mode=False,
    instant_ai_frequency_threshold=0.5,
    instant_ai_target_points_per_waveform=100,
)
```

默认值：

```text
instant_ai_frequency_threshold = 0
instant_ai_target_points_per_waveform = 100
```

模式选择规则：

```text
threshold <= 0                   -> Buffered AI
emitting_frequency > threshold  -> Buffered AI
emitting_frequency <= threshold -> Instant AI
```

阈值等于 0 表示禁用 Instant AI。这样所有旧 Python 调用和旧底层命令仍进入 Buffered AI。

底层命令在现有必需参数后追加两个可选字段。C++ 读取完必需字段后，只解析当前命令行的
剩余文本，不能使用跨行的 `operator>>` 探测可选字段，否则可能把下一条命令当成当前命令
的参数。`to_measure`、`to_dump` 和 `to_config` 使用同一套解析和默认规则。

## 配置模型

`SamplingConfig` 增加明确的采集模式：

```cpp
enum class AcquisitionMode {
    Buffered,
    Instant,
};
```

同时保存：

- `instant_ai_frequency_threshold`；
- `instant_ai_target_points_per_waveform`；
- Instant AI 计划读取间隔；
- Instant AI 目标波形数量。

### Buffered AI配置

Buffered 分支继续执行现有 `SamplingConfig::update()` 的所有计算：

```text
emitting_frequency >= 10 Hz -> sampling_frequency = 20 MHz
emitting_frequency < 10 Hz  -> sampling_frequency = 1 MHz
```

`sampling_interval`、`waveform_length`、`sampling_length_per_sample`、`sampling_time` 和
`valid_length` 的计算保持原样。现有 `Processor::summation()` 的固定 300 点基线窗口也只在
该分支使用。

现有自动估计中的 10 Hz 频率搜索范围判断保留。它不是新采集模式阈值，不能简单替换为
`AcquisitionMode`，否则默认阈值为 0 时也会改变旧的低频 Buffered AI 结果。

### Instant AI配置

Instant 分支使用：

```text
waveform_length   = instant_ai_target_points_per_waveform
valid_length      = waveform_length / 2
sampling_interval = 1e6 / (emitting_frequency * waveform_length)
```

这里的 `sampling_interval` 是重建后等效波形相邻点的微秒间隔，不代表 `ReadAny` 调用
间隔。`ReadAny` 的最小计划间隔为 100 ms。

## Instant AI采集

### 控制器生命周期

Instant 分支创建 `AdxInstantAiCtrlCreate()`，选择 `PCI-1714,BID#0`，将通道 0 配置为
`V_Neg5To5`，并循环调用 legacy `InstantAiCtrl::ReadAny()`。

控制器在所有成功和失败路径上都必须 `Dispose()`。任意 DAQ 错误码立即保存到
`SamplingResult::error_code` 并终止本次测量，不回退模式。

### 时间戳

legacy `ReadAny` 不返回采样时间戳，因此每次调用使用 `std::chrono::steady_clock` 记录：

- 计划调用时间；
- 实际调用前时间；
- `ReadAny` 完成时间；
- 返回电压。

相位映射使用单调时钟，不能使用可能发生校时跳变的系统墙上时钟。

### 相位覆盖调度

直接每 100 ms 调用一次时，某些发射频率会反复落在相同相位。例如 1 Hz 信号只有约
10 个固定相位。因此调度器按目标相位区间选择下一次调用时间：

1. 每个等效波形建立 `target_points_per_waveform` 个相位区间；
2. 下一次计划时间不得早于上一次计划时间加 100 ms；
3. 在满足最小间隔的候选时间中，优先选择最早能够落入尚未覆盖区间的时间；
4. 实际返回点按实际单调时间重新计算相位，而不是强行归入计划区间；
5. 若因系统调度抖动落入已覆盖区间，保留该点用于区间平均，并继续补缺失区间；
6. 不采用连续快速补读。

每个等效波形单独完成一次相位覆盖，完成后再开始下一个等效波形。这样
`number_of_waveforms` 仍表示实际参与平均的独立等效波形数量。

### 预计耗时与截止时间

设：

```text
P = target_points_per_waveform
R = 10 Hz
f = emitting_frequency
N = number_of_waveforms
```

默认 `P=100`。每个等效波形至少需要同时满足读取 P 个点和覆盖一个周期，因此理论下界为：

```text
max(P / R, 1 / f)
```

对于相位与 100 ms 调度形成重复关系的频率，保守计划时间为：

```text
phase_groups = ceil(P * f / R)
planned_per_waveform = phase_groups / f
planned_total = N * planned_per_waveform
```

示例：

| 发射频率 | 平均次数 | 默认目标点数 | 计划主体时间 |
|---:|---:|---:|---:|
| 0.1 Hz | 3 | 100 | 约 30 秒 |
| 0.5 Hz | 3 | 100 | 约 30 秒 |
| 1 Hz | 3 | 100 | 约 30 秒 |
| 0.05 Hz | 3 | 100 | 至少 60 秒 |

每个等效波形的硬截止时间为其计划时间加
`max(1 秒, 计划时间的 10%)`。达到截止时间仍无法满足覆盖条件时直接失败，不无限延长
采集。

### 调度异常

每次读取都比较实际开始时间与计划时间。实际迟到超过以下值时记录为错过计划点：

```text
max(20 ms, 当前计划间隔的 50%)
```

计划中的相位偏移不属于异常。错过计划点后不突发补读，而是重新计算下一个绝对计划
时间。单次迟到本身不必立即终止；若最终造成连续超过两个目标相位区间缺失，或在截止
时间内不能完成覆盖，则返回数据不完整错误。

## 重建和处理

### 数据表示

`SamplingResult` 为 Instant AI 保存原始电压、实际单调时间和计划时间。Buffered AI 不
要求时间戳，其原有数据布局保持不变。

### 相位折叠和线性重采样

对每个等效波形，将实际时间映射到 `[0, 1)` 周期相位：

```text
phase = fractional_part((timestamp - reference_time) * emitting_frequency)
```

同一目标区间有多个点时先按实际时间和电压求区间代表值。目标点缺失时：

- 缺失不超过连续两个目标区间：使用左右真实点按相位时间做线性插值；
- 连续缺失超过两个区间：该等效波形失败；
- 周期首尾的插值按周期边界处理。

线性插值只修正调度抖动和小缺口，不能用来制造大段没有采样证据的波形。

### 波形对齐和平均

每个重建波形先根据整体最小值、最大值及现有上下阈值规则定位上升沿，然后循环旋转，使
上升沿成为统一起点。无法找到明确上升沿的波形判为失败。

Instant AI 不进入 Buffered AI 的固定 300 点基线逻辑。每个 Instant 波形使用上升沿前
最多 10 个重采样点计算基线，且至少需要 5 个有效基线点。去基线后，将用户要求的
`number_of_waveforms` 个波形逐点平均。

平均后的前半周期进入现有 τ 拟合。默认完整波形 100 点，因此通常约有 50 个拟合点；
这是默认约 10 秒/等效波形与 Buffered AI 高密度数据之间的明确取舍。

Rapid Decline 检查继续按结果长度的百分比计算，但必须保证检查点数量至少为 1，避免
低密度 Instant 波形产生 0 点窗口。Buffered AI 在正常长度下结果不变。

## dump和process

Buffered AI 的旧 dump 格式保持可读。Instant AI dump 必须带有可识别的版本/模式标记，
并保存：

- 发射频率；
- 目标点数；
- 用户要求的平均次数；
- 每次读取的单调相对时间；
- 电压。

`process` 根据格式标记选择加载方式。读取 Instant dump 时恢复时间戳并执行相同的相位
重建；读取无标记的旧文件时继续按 Buffered AI 原格式处理。不能仅凭列数猜测模式。

## Mock行为

Mock 使用与真实采样相同的 `AcquisitionMode` 选择结果：

- Buffered 分支保持现有高密度波形生成和处理，不改变旧测试期望；
- Instant 分支生成带实际时间、计划时间、调度抖动和相位偏移的低密度周期数据；
- Instant Mock 覆盖完整数据、小缺口可插值、大缺口失败、读取失败和波形不足场景。

Mock 不应绕过相位重建直接提供最终平均波形，否则无法验证最关键的数据处理路径。

## 错误处理

新增应用级错误至少区分：

- Instant AI 参数无效；
- Instant AI 读取失败（DAQ 原始错误码仍原样传播）；
- Instant AI 相位覆盖不足；
- Instant AI 读取调度超时；
- Instant AI 波形对齐失败；
- Instant AI 完整波形数量不足。

Python 保持现有行为：测量结果失败时由包装层抛出 `Sampler.Error`。任何 Instant AI 错误
都不自动切换为 Buffered AI。

## 验证

### 无硬件自动验证

1. 旧三参数命令仍解析成功且选择 Buffered AI。
2. 阈值为 0 时所有正发射频率均选择 Buffered AI。
3. 频率等于阈值时选择 Instant AI，高于阈值时选择 Buffered AI。
4. Buffered AI 的 1 MHz/20 MHz 分界及现有 Mock τ 结果保持不变。
5. `number_of_waveforms` 为 1、3、5 时，Instant AI 分别重建并平均对应数量的波形。
6. 默认目标点数为 100；显式传入 500 或 1000 时配置和预计时间相应变化。
7. 相位重复场景（例如 1 Hz）通过计划偏移覆盖目标相位，且任意计划间隔不小于 100 ms。
8. 小于等于两个区间的缺口可插值，大于两个区间的缺口失败。
9. 任意模拟 `ReadAny` 失败立即终止且不回退。
10. Instant dump 可重放，旧 Buffered dump 仍可读取。

### 后续硬件验证

硬件恢复后至少执行：

1. 阈值禁用时对比修改前后的 Buffered AI 波形和 τ。
2. `0.1 Hz`、100 点、3 次平均，确认约 30 秒完成并覆盖 3 个完整波形。
3. 在阈值上下各选一个频率，确认实际创建的控制器模式正确。
4. 检查 Instant 原始时间戳、计划间隔、最大迟到、相位覆盖率和插值数量。
5. 对同一稳定信号重复测量，比较 Instant AI τ 的重复性。
6. 人为占用设备或断开设备，确认直接返回 DAQ 错误且不回退。

## 兼容性结论

新参数默认禁用 Instant AI，所以现有调用继续使用原 Buffered AI。Buffered AI 内部的
10 Hz 分界、1 MHz/20 MHz 采样、原始波形平均和 τ 计算均不改变。只有用户显式传入大于
0 的 Instant AI 模式阈值，并且发射频率小于等于该阈值时，才进入新增路径。
