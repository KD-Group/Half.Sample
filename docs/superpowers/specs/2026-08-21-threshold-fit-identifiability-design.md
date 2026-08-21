# Threshold Accumulation 拟合可辨识性修复设计

## 目标

修复 `threshold_accumulation` 在原始电压幅度刚超过 `0.1V` 时，将不可辨识的指数拟合误判为成功，并把巨大外推基线 `b` 当作 `v_inf` 返回的问题。

## 已确认根因

- 每次异常采样均为新的 `to_measure` 请求，采样代次递增、缓冲区指纹不同，不存在上一轮数据复用。
- 同一缓冲区使用新结果对象重算后仍得到相同异常值，不存在结果对象状态残留。
- 原始幅度低于 `0.1V` 时 `align` 返回 `VOLTAGE_NOT_ENOUGH`；刚超过 `0.1V` 后进入指数拟合。
- 异常结果的 `tau` 贴近搜索上限 `400000`，说明采样窗口内的指数变化不足以辨识时间常数。
- 现有 `fit_is_identifiable` 能识别此情况，但只对 `independent_cycle` 生效，`threshold_accumulation` 绕过了保护。

## 修改范围

### Half.Sample

1. 保留普通 `to_query` 输出的采样生命周期诊断：
   - `to_dump` / `to_measure` 链路；
   - 请求代次与物理采样次数；
   - 当前、上一轮及最后一次 dump 的缓冲区指纹和大小；
   - 缓冲区相等关系；
   - 原始最小值、最大值、跨度；
   - 采样结果及终止处理阶段。
2. 删除仅用于排除状态残留的 `to_diagnose_current_result` 内存重算命令。
3. 将固定频率、非 Instant 模式的 `fit_is_identifiable` 校验应用到 `threshold_accumulation`，不再限定为 `independent_cycle`。
4. 拟合不可辨识时返回 `FIT_NOT_IDENTIFIABLE`，不使用拟合得到的 `b` 覆盖 `v_inf`；保留 `align` 已记录的原始电压幅度。

### KDM3000

1. 保留 phase2 开始、查询结果和 Half.Sample 原生诊断字段日志。
2. 删除异常电压触发 `to_diagnose_current_result` 的内存重算分支及其阈值常量。
3. 不改变 phase2 运动判断和错误恢复流程。

## 不采用的方案

- 不直接截断 `v_inf`：无法区分真实高电压与错误拟合。
- 不把 `tau` 强制限制到边界：边界解本身代表拟合不可辨识，继续返回成功会掩盖数据质量问题。
- 不切换默认处理模式到 `independent_cycle`：这只能绕开缺陷，不能修复 `threshold_accumulation`。

## 验证边界

按用户要求不增加或运行测试。只进行 Half.Sample 编译检查和 KDM3000 Python 语法检查，并审核最终差异，确认诊断日志仍保留、内存重算代码已删除、拟合保护覆盖 `threshold_accumulation`。
