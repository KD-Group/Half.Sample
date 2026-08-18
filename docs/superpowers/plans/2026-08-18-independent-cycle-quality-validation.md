# 独立周期质量校验实现计划

> **给执行代理：** 必须按任务逐项执行本计划，并使用 `subagent-driven-development` 或 `executing-plans` 技能。每个步骤使用复选框跟踪。

**目标：** 防止低幅度信号中的错误边界进入独立周期平均，并在有效周期不足时重新独立采集，直到获得 N 个有效周期或达到重试上限。

**架构：** 保持 `threshold_accumulation` 逻辑不变。为 `independent_cycle` 增加稳健分位数阈值、阈值窗口确认、周期长度校验、幅度/基线/形状校验及明确的拒绝计数。处理器只累计通过校验的归一化周期；不同批次之间不拼接原始缓冲区。

**技术栈：** C++17、SCons、现有断言式 C++ 测试、pytest、`sample.exe` 回放流程。

---

## 文件职责

- 修改 `src/processor/independent_cycle.hpp`：增加校验计数和独立周期提取结果定义。
- 修改 `src/processor/independent_cycle.cpp`：实现所有独立周期校验规则。
- 修改 `src/processor/processor.cpp`：跨独立采集批次累计有效周期，并且只在获得 N 个周期后拟合。
- 修改 `src/result/sampling_result.hpp`：暴露有效、拒绝、批次和重试计数。
- 修改 `src/commander/measure.cpp`：重置并通过现有查询结果输出计数。
- 如果现有错误模型需要，修改 `src/error/error.hpp` 和 `src/error/error.cpp`，增加“有效周期不足”错误。
- 如果新测试文件尚未注册，修改 `SConstruct` 和 `src/CMakeLists.txt`。
- 新建或修改 `tests/sample_instant_ai/test_independent_cycle.cpp`：增加合成数据和真实数据回归测试。
- 修改 `tests/sample_instant_ai/test_main.cpp`：注册测试。
- 不把原始采集数据加入 git，通过 `HALF_SAMPLE_REGRESSION_DATA` 或本地数据目录加载。

### 任务 1：增加失败测试，覆盖边界确认和周期拒绝

**文件：** `tests/sample_instant_ai/test_independent_cycle.cpp`、`tests/sample_instant_ai/test_main.cpp`，以及必要的测试构建文件。

- [ ] 增加合成数据辅助函数，生成 20 MHz/50 Hz 数据，其中 `T=400000`，ADC 量化步长为 `0.00244140625 V`，并支持加入短暂高阈值尖峰。
- [ ] 增加失败测试，验证短于 32 个确认点中 24 个点的尖峰不能生成边界：

```cpp
auto result = extract_independent_cycles(samples, 20'000'000, 50.0, 2);
assert(result.accepted_waveforms == 2);
assert(result.rejected_threshold_candidates > 0);
```

- [ ] 增加 `0.54T` 和 `1.39T` 周期的失败测试。断言这些区间不会出现在 `cycles` 中，而正常的 `T` 周期仍能通过。
- [ ] 执行：

```powershell
scons -Q cpp_build/sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
```

预期：基于当前“单点越阈值”和 `0.5T～1.5T` 允许范围的实现，新测试应失败。

### 任务 2：实现独立周期校验

**文件：** `src/processor/independent_cycle.hpp`、`src/processor/independent_cycle.cpp`，必要时修改 `src/constant.hpp`。

- [ ] 增加明确计数：`rejected_threshold_candidates`、`rejected_short_periods`、`rejected_long_periods`、`rejected_amplitude_cycles`、`rejected_baseline_cycles`、`rejected_shape_cycles`。
- [ ] 使用分位数替代单个极值计算阈值：

```cpp
Vmin = P1(samples);
Vmax = P99(samples);
A = Vmax - Vmin;
L = Vmin + 0.10 * A;
U = Vmin + 0.40 * A;
```

- [ ] 使用以下窗口确认低、高阈值：

```cpp
K = max(32, round(0.00005 * waveform_length));
```

窗口中至少 75% 的点必须满足 `x<=L` 或 `x>=U`。短暂高尖峰只增加拒绝计数，不生成边界。
- [ ] 只有满足 `0.90T <= D <= 1.10T` 的区间才允许作为周期。过短候选不移动上一个有效边界；过长区间不重采样，并以当前确认边界重新建立搜索锚点。
- [ ] 获得 3 个有效周期后，计算 `A_ref=median(P99(cycle)-P1(cycle))`；后续周期必须满足 `0.70A_ref <= A_i <= 1.30A_ref`。
- [ ] 计算 `B_i=median(samples[start-300 ... start-150])`；后续基线必须满足 `abs(B_i-B_ref) <= max(0.005,0.20*A_ref)`。
- [ ] 获得 3 个有效周期后，若归一化 RMSE 大于 `0.15` 或与有效模板的相关系数小于 `0.90`，则拒绝该周期。模板只能使用有效周期更新。
- [ ] 执行 C++ 聚焦测试目标，确认任务 1 的测试全部通过。

### 任务 3：集成独立批次累计和重试

**文件：** `src/processor/processor.cpp`、`src/result/sampling_result.hpp`、`src/commander/measure.cpp`，必要时修改错误定义文件。

- [ ] 在顶层测量开始时重置请求数、有效数、丢弃数、批次数、重试次数和各类拒绝计数。
- [ ] 每次只处理一个原始采集缓冲区。只把已验证的归一化周期加入累计列表；禁止拼接原始缓冲区，也禁止跨批次检测边界。
- [ ] 只有累计到准确的 N 个有效周期后才停止。只有 N 个周期存在且拟合成功时，才能设置 `success=true`。
- [ ] 当前批次不足 N 个周期时，重新进行独立采集。最多执行 3 个批次，仍不足时返回明确的“有效周期不足”错误。
- [ ] 通过现有查询/结果路径输出统计信息，同时保持 legacy 模式和省略模式参数时的命令格式不变。
- [ ] 执行：

```powershell
scons -Q cpp_build/sample.exe cpp_build/sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
uv run --no-project pytest tests/test_measure.py -q
```

### 任务 4：增加用户提供数据的回归测试

**文件：** `tests/sample_instant_ai/test_independent_cycle.cpp`、`tests/sample_instant_ai/test_main.cpp`。

- [ ] 优先读取环境变量 `HALF_SAMPLE_REGRESSION_DATA`；默认读取：

```text
D:\kunde\code\KDM3000\Sample新预处理采集数据-20260818\21.txt
```

文件不存在时明确输出跳过原因，不把原始数据复制到仓库。
- [ ] 对 `21.txt` 断言数据点数为 13,200,000、有效周期为 32、短周期和长周期拒绝数为 0。
- [ ] 对存在的 `22.txt`、`23.txt` 和 `25.txt`，断言异常区间被拒绝，且没有任何已接受周期的源长度超出 `[0.90T,1.10T]`。
- [ ] 增加两个独立采集缓冲区的测试，验证可以累计准确 64 个有效周期，且缓冲区连接处不会被当成额外周期。
- [ ] 执行：

```powershell
$env:HALF_SAMPLE_REGRESSION_DATA = 'D:\kunde\code\KDM3000\Sample新预处理采集数据-20260818\21.txt'
scons -Q cpp_build/sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
```

### 任务 5：完整验证和 sample.exe 回放

**文件：** 检查任务 1～4 修改的所有文件。

- [ ] 构建并运行 C++ 测试：

```powershell
scons -Q cpp_build/sample.exe cpp_build/sample_instant_ai_unit_tests.exe
.\cpp_build\sample_instant_ai_unit_tests.exe
```

- [ ] 执行 Python 测试和代码风格检查：

```powershell
uv run --no-project pytest tests -q
uv run --no-project pycodestyle . --max-line-length=120 --exclude temperature,.venv,.git,__pycache__,dist
```

- [ ] 使用正常数据回放：

```text
set_sampler real_sampler
to_config 32 50 False 0 100 10 independent_cycle
to_process 21.txt independent_cycle
to_query
```

预期：获得 32 个有效周期，拟合结果正常。
- [ ] 使用 `22.txt` 回放。预期：异常区间被统计并拒绝，程序重新采集或明确报告有效周期不足；不能使用被拉伸的异常周期成功拟合。
- [ ] 执行 `git diff --check` 和 `git status --short`，不得加入原始数据、构建输出或 `half_sample.egg-info/`。
- [ ] 将校验逻辑、批次/重试集成和测试分别使用简洁的命令式提交。推送前报告具体提交和验证输出。

## 验收覆盖

- 79 mV 的 16/32 次数据保持正常周期和结果。
- 72 mV 和 65 mV 数据拒绝约 `0.54T`、`1.39T` 的异常区间。
- 短暂阈值尖峰不能生成边界。
- 幅度、基线和形状公式都有测试覆盖。
- 不能使用异常周期凑够 N 次平均。
- 不同独立采集批次之间不共享原始相位。
- 重试耗尽时明确失败。
- legacy 处理和 KDM3000 模式选择保持不变。

