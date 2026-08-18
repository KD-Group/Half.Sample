import os
import shutil
import sys
import threading
from pathlib import Path

from . import Process
from . import Result


_SOURCE_ROOT = Path(os.path.realpath(os.path.abspath(__file__))).parent.parent
_SOURCE_BUILD_LOCK = threading.Lock()


def _existing_file(*parts):
    try:
        candidate = Path(parts[0]).joinpath(*parts[1:])
    except (TypeError, ValueError):
        return None
    try:
        if candidate.is_file():
            return str(candidate.resolve())
    except OSError:
        return None
    return None


def _is_source_checkout():
    return all((
        _existing_file(_SOURCE_ROOT, 'SConstruct'),
        _existing_file(_SOURCE_ROOT, 'setup.py'),
        _existing_file(_SOURCE_ROOT, 'sample', 'sample.py'),
    ))


class Sampler:
    @property
    def execution_path(self) -> str:
        # keep the historical current-working-directory override first
        execution_path = _existing_file('sample.exe')
        if execution_path:
            return execution_path

        # try to use (or build) cpp_build/sample.exe in a source checkout
        if _is_source_checkout():
            execution_path = _existing_file(_SOURCE_ROOT, 'cpp_build', 'sample.exe')
            if execution_path:
                return execution_path
            with _SOURCE_BUILD_LOCK:
                execution_path = _existing_file(_SOURCE_ROOT, 'cpp_build', 'sample.exe')
                if not execution_path:
                    if os.system('cd {} && scons'.format(_SOURCE_ROOT)) != 0:
                        raise self.Error("Compile C++ Driver Error")
                    execution_path = _existing_file(_SOURCE_ROOT, 'cpp_build', 'sample.exe')
            if execution_path:
                return execution_path

        # try the Python/frozen executable directory
        try:
            executable_directory = Path(sys.executable).parent
        except (TypeError, ValueError):
            executable_directory = None
        execution_path = _existing_file(executable_directory, 'sample.exe')
        if execution_path:
            return execution_path

        # try the environment root used by venv/uv data-file installs
        execution_path = _existing_file(sys.prefix, 'sample.exe')
        if execution_path:
            return execution_path

        # try to find sample.exe in system path when release
        try:
            path_driver = shutil.which("sample.exe")
        except (OSError, ValueError):
            path_driver = None
        execution_path = _existing_file(path_driver)
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
                instant_ai_max_reliable_polling_hz: float = 10.0,
                waveform_processing_mode: str = "threshold_accumulation") -> Result:
        command = "to_measure {} {:.12g} {}".format(number_of_waveforms, emitting_frequency, auto_mode)
        if (instant_ai_frequency_threshold != 0.0 or instant_ai_target_points_per_waveform != 100 or
                instant_ai_max_reliable_polling_hz != 10.0 or waveform_processing_mode != "threshold_accumulation"):
            command += " {:.12g} {} {:.12g}".format(
                instant_ai_frequency_threshold, instant_ai_target_points_per_waveform,
                instant_ai_max_reliable_polling_hz)
            if waveform_processing_mode != "threshold_accumulation":
                command += " {}".format(waveform_processing_mode)
        return self.communicate(command)

    def dump(self, filename: str, number_of_waveforms: int, emitting_frequency: float,
             auto_mode: bool = False, instant_ai_frequency_threshold: float = 0.0,
             instant_ai_target_points_per_waveform: int = 100,
             instant_ai_max_reliable_polling_hz: float = 10.0,
             waveform_processing_mode: str = "threshold_accumulation") -> Result:
        command = "to_dump {} {} {:.12g} {}".format(filename, number_of_waveforms, emitting_frequency, auto_mode)
        if (instant_ai_frequency_threshold != 0.0 or instant_ai_target_points_per_waveform != 100 or
                instant_ai_max_reliable_polling_hz != 10.0 or waveform_processing_mode != "threshold_accumulation"):
            command += " {:.12g} {} {:.12g}".format(
                instant_ai_frequency_threshold, instant_ai_target_points_per_waveform,
                instant_ai_max_reliable_polling_hz)
            if waveform_processing_mode != "threshold_accumulation":
                command += " {}".format(waveform_processing_mode)
        return self.communicate(command)

    def process(self, filename: str, waveform_processing_mode: str = None) -> Result:
        command = "to_process {}".format(filename)
        if waveform_processing_mode is not None:
            command += " {}".format(waveform_processing_mode)
        return self.communicate(command)

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
