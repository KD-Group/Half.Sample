import os
import shutil
import sys
from pathlib import Path

from . import Process
from . import Result


def _existing_file(*parts):
    try:
        candidate = Path(parts[0]).joinpath(*parts[1:])
        if candidate.is_file():
            return str(candidate.resolve())
    except (OSError, TypeError, ValueError):
        return None
    return None


class Sampler:
    @property
    def execution_path(self) -> str:
        main_path = os.path.join(os.path.dirname(__file__), '..')

        # keep the historical current-working-directory override first
        execution_path = _existing_file('sample.exe')
        if execution_path:
            return execution_path

        # build if SConstruct file exists
        if os.path.exists(os.path.join(main_path, 'SConstruct')):
            if os.system('cd {} && scons'.format(main_path)) != 0:
                raise self.Error("Compile C++ Driver Error")

        # try to use cpp_build/sample.exe when developing
        execution_path = _existing_file(main_path, 'cpp_build', 'sample.exe')
        if execution_path:
            return execution_path

        # try the Python/frozen executable directory
        try:
            executable_directory = Path(sys.executable).parent
        except (OSError, TypeError, ValueError):
            executable_directory = None
        execution_path = _existing_file(executable_directory, 'sample.exe')
        if execution_path:
            return execution_path

        # try the environment root used by venv/uv data-file installs
        execution_path = _existing_file(sys.prefix, 'sample.exe')
        if execution_path:
            return execution_path

        # try to find sample.exe in system path when release
        execution_path = _existing_file(shutil.which("sample.exe"))
        if execution_path:
            return execution_path

        raise self.Error('Sample Driver Not Found')

    @property
    def p(self) -> Process:
        p = getattr(self, 'p_', None)
        if p is not None:
            return p

        p = Process(self.execution_path)
        setattr(self, 'p_', p)
        return p

    def communicate(self, command: str, executor: Process = None) -> Result:
        result = Result()
        try:
            executor = executor or self.p
            executor.write_line(command)
            lines = executor.read_until('EOF')
            if lines:
                exec(lines, result.__dict__)
        except Exception as e:
            result.error = True
            result.message = str(e)
            raise self.Error("{}: {}".format(result.message, result.chinese_message))

        if result.error:
            raise self.Error("{}: {}".format(result.message, result.chinese_message))

        return result

    @property
    def is_measuring(self) -> bool:
        return self.communicate('is_measuring').measuring

    def measure(self, number_of_waveforms: int, emitting_frequency: float, auto_mode: bool = False,
                instant_ai_frequency_threshold: float = 0.0,
                instant_ai_target_points_per_waveform: int = 100,
                instant_ai_max_reliable_polling_hz: float = 10.0) -> Result:
        command = "to_measure {} {:.12g} {}".format(number_of_waveforms, emitting_frequency, auto_mode)
        if (instant_ai_frequency_threshold != 0.0 or instant_ai_target_points_per_waveform != 100 or
                instant_ai_max_reliable_polling_hz != 10.0):
            command += " {:.12g} {} {:.12g}".format(
                instant_ai_frequency_threshold, instant_ai_target_points_per_waveform,
                instant_ai_max_reliable_polling_hz)
        return self.communicate(command)

    def dump(self, filename: str, number_of_waveforms: int, emitting_frequency: float,
             auto_mode: bool = False, instant_ai_frequency_threshold: float = 0.0,
             instant_ai_target_points_per_waveform: int = 100,
             instant_ai_max_reliable_polling_hz: float = 10.0) -> Result:
        command = "to_dump {} {} {:.12g} {}".format(filename, number_of_waveforms, emitting_frequency, auto_mode)
        if (instant_ai_frequency_threshold != 0.0 or instant_ai_target_points_per_waveform != 100 or
                instant_ai_max_reliable_polling_hz != 10.0):
            command += " {:.12g} {} {:.12g}".format(
                instant_ai_frequency_threshold, instant_ai_target_points_per_waveform,
                instant_ai_max_reliable_polling_hz)
        return self.communicate(command)

    def process(self, filename: str) -> Result:
        return self.communicate("to_process {}".format(filename))

    def sampling_progress(self) -> Result:
        return self.communicate("to_sampling_progress")

    def cancel_sampling(self) -> Result:
        return self.communicate("to_cancel_sampling")

    def set_sampler(self, sampler_name: str) -> Result:
        return self.communicate("set_sampler {}".format(sampler_name))

    def get_sampler(self) -> Result:
        return self.communicate("get_sampler")

    def set_sampler_value(self, key: str, value: float) -> Result:
        return self.communicate("set_sampler_value {} {}".format(key, value))

    def get_sampler_value(self, key: str) -> Result:
        return self.communicate("get_sampler_value {}".format(key))

    def query(self) -> Result:
        result = self.communicate("to_query")
        result.process()
        if not result.success and result.message == "success":
            result.message = "error_undefined"
        return result

    class Error(RuntimeError):
        pass


sampler = Sampler()
