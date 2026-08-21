# Phase2 紧凑诊断日志设计

## 目标

减少二阶段下降中重复且冗长的诊断日志，同时保证一次日志即可判断采样链路、数据是否复用、原始幅度及拟合失败位置；真正异常时仍保留完整结果用于深入分析。

## 日志策略

### 常规采样

每次完成查询后只打印一条紧凑日志，包含：

- 请求代次 `gen`；
- `to_measure` / `to_dump` 链路 `op`；
- 波形处理模式、频率和平均次数；
- 最终成功状态、错误分类和 Half.Sample 终止阶段；
- `v_inf`、原始幅度 `raw_span`、`tau` 和 `b`；
- 当前缓冲区指纹；
- 是否与上一轮或最后一次 dump 缓冲区相同；
- sample.exe SHA256 前 12 位。

删除每次采样开始时重复打印的完整 sampler 身份和 `stage=start` 日志。

### 异常采样

满足以下任意条件时，在紧凑日志之后额外打印现有完整 result 诊断：

- 硬件采样失败；
- 当前缓冲区与上一轮或最后一次 dump 相同；
- 终止阶段不是 `align_failed` 或 `success`；
- 成功结果的 `tau` 接近或达到不可辨识边界；
- `v_inf` 达到二阶段内存诊断阈值 `0.2V`；
- Half.Sample 返回未知或缺失的终止阶段。

### Dump 链路

保留原始文件路径、文件大小、`online_before_process` 和 `offline_after_process` 阶段。`process_returned` 独立日志删除，因为紧接其后的 `offline_after_process` 已能证明处理返回并提供结果。

## 实现边界

- Half.Sample 的普通 `to_query` 诊断字段保持不变，日志压缩只发生在 KDM3000。
- KDM3000 新增紧凑 payload 构造和异常判定；完整 payload 构造保留，仅在异常时调用。
- 不改变采样、运动安全判断、错误恢复或 dump/process 数据流。
- 按用户要求不增加或运行测试，只做 Python 语法检查和差异审核。
