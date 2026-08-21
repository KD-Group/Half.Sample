# Half.Sample 在线结果状态诊断设计

## 背景

KDM3000 二阶段下降连续采样时，`to_measure -> to_query` 偶发返回与原始波形不一致的电压；启用原始数据保存后，`to_dump -> to_process -> to_query` 未复现。源码确认 `to_measure` 与 `to_dump` 使用同一采集和在线处理流程，但 dump 链路会先写文件，并由 KDM3000 再调用 `to_process`，使用新的 `SamplingResult` 离线重算并覆盖在线结果。

现有异常波形在当前 Half.Sample 中连续离线重放五次均得到 `v_inf=0.090841`，不能支持现场报告的 `0.625382`。需要在不落盘、不覆盖现场结果的情况下，判断异常来自复用的在线结果对象，还是来自写文件所改变的采集/处理时序。

## 方案比较

### 方案 A：同一内存缓冲区只读重算（采用）

新增显式诊断命令。采样结束后复制当前原始缓冲区到新的 `SamplingResult`，使用当前配置重新执行 `align -> summation -> estimate -> validate`，返回诊断结果，但不修改 `Global::result`。

优点是在线结果和重算结果使用完全相同的原始数据，且不引入采样前写文件延迟，可以直接隔离结果对象状态问题。代价是诊断调用会增加一次采样后的 CPU 处理时间。

### 方案 B：生产在线链路直接改用新结果对象

每次测量都在局部结果对象上处理，最后发布到全局结果。该方案可能成为最终修复，但当前直接采用会清除现场症状，无法证明根因。

### 方案 C：无条件落盘并离线处理

复用目前工作正常的 dump 链路。该方案同时改变磁盘 I/O、采样间隔和结果对象，无法区分哪个因素消除了异常，也会显著影响二阶段下降时序。

## 设计

### Half.Sample

新增 `to_diagnose_current_result` 命令，仅允许在 `measuring=false` 时调用。

命令执行以下操作：

1. 通过 acquire 读取 `Global::result.measuring`，采样未结束时返回 `NOW_IN_MEASURING`。
2. 快照 `Global::config`。
3. 新建不预分配大缓冲区的 `SamplingResult replay(false)`。
4. 复制当前采集输入，包括 buffered 原始缓冲区或 Instant AI 原始读数，并按配置初始化 `resultWave`。
5. 在 `replay` 上执行与在线链路相同的 `align -> summation -> estimate -> validate_finite_result`。
6. 输出带 `diagnostic_` 前缀的结果字段，包括成功状态、错误、`v_inf`、有效性、最大最小值以及拟合参数。
7. 不写入 `Global::config` 或 `Global::result`。

不修改 Python 包装层公开 API。KDM3000 通过现有 `sampler.communicate()` 发送诊断命令，并使用普通 `Result` 解析返回字段，减少现场只更新 `sample.exe` 时的版本耦合。

### KDM3000

仅在二阶段 motion-risk 的 no-dump 路径中，并且只在在线结果出现明显异常时：

1. 保留原来的 `sampler.query()` 结果作为业务结果。
2. 正常结果不增加额外调用，保持连续采样时序不变。
3. 当有效在线电压达到临时诊断阈值 `0.2 V` 时，立即通过 `sampler.communicate('to_diagnose_current_result')` 重算；该阈值覆盖已观察到的 `0.625382 V`，同时不会影响前序约 `0.1 V` 的样本。
4. 在同一条结构化日志中记录在线结果与内存重算结果。
5. 诊断失败只记录 warning，不改变业务结果、重试逻辑或运动控制。

dump 路径继续使用现有 `online_before_process` 与 `offline_after_process` 日志，不重复调用内存诊断。

## 判定规则

- 在线值异常、内存重算正常：问题位于复用的 `Global::result`、在线处理状态或未定义行为。
- 在线值和内存重算同时异常：问题来自本次内存原始数据、配置或确定性处理算法。
- 诊断只在异常在线结果已经产生后运行，因而不会改变异常发生前的连续采样节奏。
- dump 在线值正常、no-dump 在线值异常，且 no-dump 内存重算正常：写文件读取或延迟掩盖了在线状态问题。

## 约束与验证

- 这是临时诊断，不作为自动纠错或业务兜底。
- 不新增 TDD 用例；按用户要求采用直接开发。
- 完成后构建 `sample.exe`，运行现有相关测试/静态检查，并用现有异常原始文件验证诊断重算结果仍为 `0.090841`。
- 日志收集完成并确认根因后，单独评审最终修复和诊断代码清理。
