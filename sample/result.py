import math


class Result:
    ErrorMessageMapper = {
        "success": "成功",
        "command_not_found": "命令不存在",
        "sampler_not_found": "采样器不存在",
        "now_in_measuring": "正在测量中",
        "voltage_not_enough": "电压不足",
        "real_sampler_error": "采样器出错",
        "wave_not_found": "未找到波形",
        "appropriate_wave_not_found": "未找到合适波形"
    }

    def __init__(self):
        self.error = False
        self.message = ''

        self.sampler_name = ''
        self.measuring = False
        self.success = False

        self.with_magnetic = False

        self.sampling_interval = 0.0  # us
        self.wave_interval = 0.0   # us
        self.wave = []
        self.time_line = []
        self.estimate = []

        self.tau = 0.0
        self.w = 0.0
        self.b = 0.0
        self.loss = 0.0

        self.v0 = 0.0
        self.v_inf = 0.0

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

            self.v0, self.v_inf = b + w, b

    @property
    def chinese_message(self) -> str:
        return self.ErrorMessageMapper[self.message]
