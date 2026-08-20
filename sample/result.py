import math


class Result:
    ErrorMessageMapper = {
        # 基础状态
        "success": "成功",

        # 用户操作错误
        "command_not_found": "命令不存在",
        "sampler_not_found": "采样器不存在",
        "now_in_measuring": "正在测量中",
        "file_not_found": "文件未找到",

        # 采样错误
        "voltage_not_enough": "电压不足",
        "wave_not_found": "未找到波形",
        "appropriate_wave_not_found": "未找到合适波形",
        "invalid_instant_ai_config": "Instant AI配置无效",
        "instant_ai_coverage_insufficient": "Instant AI相位覆盖不足",
        "instant_ai_schedule_timeout": "Instant AI采集超时",
        "instant_ai_alignment_failed": "Instant AI波形对齐失败",
        "instant_ai_waveform_count_insufficient": "Instant AI完整波形数量不足",
        "fit_not_identifiable": "拟合窗口不足以辨识tau",
        "sampling_result_not_finite": "采样结果包含无效数值",
        "waveform_processing_mode_mismatch": "波形处理模式不一致",
        "user_cancelled": "用户已取消",

        # 警告类错误
        "warning_intr_not_available": "中断资源不可用",
        "warning_param_out_of_range": "参数超出范围",
        "warning_prop_value_out_of_range": "属性值超出范围",
        "warning_prop_value_not_spted": "属性值不支持",
        "warning_prop_value_conflict": "属性值状态冲突",
        "warning_vrg_of_group_not_same": "通道组量程不一致",

        # 系统级错误
        "error_handle_not_valid": "句柄无效",
        "error_param_out_of_range": "参数超出范围",
        "error_param_not_spted": "参数不支持",
        "error_param_fmt_unexpted": "参数格式异常",
        "error_memory_not_enough": "内存不足",
        "error_buffer_is_null": "缓冲区为空",
        "error_buffer_too_small": "缓冲区过小",
        "error_data_len_exceed_limit": "数据长度超限",
        "error_func_not_spted": "功能不支持",
        "error_event_not_spted": "事件不支持",
        "error_prop_not_spted": "属性不支持",
        "error_prop_read_only": "属性只读",
        "error_prop_value_conflict": "属性值冲突",
        "error_prop_value_out_of_range": "属性值超限",
        "error_prop_value_not_spted": "属性值不支持",
        "error_privilege_not_held": "权限未持有",
        "error_privilege_not_available": "权限不可用",
        "error_driver_not_found": "驱动未找到",
        "error_driver_ver_mismatch": "驱动版本不匹配",
        "error_driver_count_exceed_limit": "驱动数量超限",
        "error_device_not_opened": "设备未打开",
        "error_device_not_exist": "设备不存在",
        "error_device_unrecognized": "设备未识别",
        "error_config_data_lost": "配置数据丢失",
        "error_func_not_inited": "功能未初始化",
        "error_func_busy": "功能忙",
        "error_intr_not_available": "中断不可用",
        "error_dma_not_available": "DMA通道不可用",
        "error_device_io_time_out": "设备IO超时",
        "error_signature_not_match": "签名不匹配",
        "error_func_conflict_with_bfd_ai": "功能与缓冲AI冲突",
        "error_vrg_not_available_in_se_mode": "单端模式量程不可用",
        "error_undefined": "未定义错误"
    }

    def __init__(self):
        self.error = False
        self.message = ''

        self.sampler_name = ''
        self.measuring = False
        self.success = False
        self.with_magnetic = False

        self.sampling_interval = 0.0  # us
        self.wave_interval = 0.0  # us
        self.waveforms_per_sample = 0.0  # 采集卡每次采样时能得到的最大波形数量，可能采集到部分波形，因此为double类型，例如0.5
        self.sampling_time = 1  # 采样次数,当要求的波形数量大于采集卡单次最大采样点数的时，进行多次采样直到能采集到要求的波形
        self.sampling_length_per_sample = 0  # 采集卡单次采样点数
        self.waveform_length = 0  # 一个完整波形的点数(包括上升沿和下降沿)
        self.valid_length = 0  # 有效的波形点数(仅包含上升沿部分)
        self.number_of_waveforms = 0  # 波形平均次数
        self.acquisition_mode = ""
        self.waveform_processing_mode = ""
        self.error_category = ""
        self.retryable = False
        self.cancelled = False
        self.planned_seconds = 0.0
        self.elapsed_seconds = 0.0
        self.completed_cycles = 0
        self.target_cycles = 0
        self.successful_reads = 0
        self.late_reads = 0
        self.instant_ai_complete_waveforms = 0
        self.instant_ai_planned_duration_seconds = 0.0
        self.instant_ai_actual_duration_seconds = 0.0
        self.instant_ai_late_reads = 0
        self.instant_ai_interpolated_bins = 0
        self.wave = []
        self.time_line = []
        self.estimate = []

        self.tau = 0.0
        self.tau_b = 0.0
        self.w = 0.0
        self.b = 0.0
        self.loss = 0.0
        self.maximum = 0.0
        self.minimum = 0.0
        self.invalid_field = ''

        self.v0 = 0.0
        self.v_inf = 0.0
        self.v_inf_valid = False
        self._v_inf_reported = False
        self.cycle_maximum = 0.0
        self.cycle_minimum = 0.0

        self.mock_tau = 0.0
        self.mock_v0 = 0.0
        self.mock_v_inf = 0.0
        self.mock_noise = 0.0

        self.rho = 0.0
        self.miu = 0.0
        self.carrier = 0.0
        self.corrected_rho = 0.0
        self.corrected_miu = 0.0
        self.corrected_carrier = 0.0

    def process(self):
        self.time_line = [self.wave_interval * i for i in range(len(self.wave))]

        if self.success:
            tau, w, b = self.tau, self.w, self.b
            self.estimate = [w * math.exp(t / -tau) + b for t in self.time_line]

            self.v0 = b + w
            if not self._v_inf_reported:
                self.v_inf = b

        if self._v_inf_reported:
            voltage_valid = self.v_inf_valid is True and math.isfinite(self.v_inf) and self.v_inf > 0.0
        else:
            voltage_valid = self.success and math.isfinite(self.v_inf) and self.v_inf > 0.0
        if not voltage_valid:
            self.v_inf = 0.0
        self.v_inf_valid = voltage_valid

    @property
    def chinese_message(self) -> str:
        try:
            return self.ErrorMessageMapper[self.message]
        except Exception as e:
            return self.message
