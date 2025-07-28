import os
import shutil

from . import Process
from . import Result


class Sampler:
    @property
    def execution_path(self) -> str:
        main_path = os.path.join(os.path.dirname(__file__), '..')

        # build if SConstruct file exists
        if os.path.exists(os.path.join(main_path, 'SConstruct')):
            if os.system('cd {} && scons'.format(main_path)) != 0:
                raise self.Error("Compile C++ Driver Error")

        # try to use cpp_build/sample.exe when developing
        execution_name = 'sample.exe'
        if not os.path.exists(execution_name):
            execution_name = os.path.join(main_path, 'cpp_build', execution_name)
        if os.path.exists(execution_name):
            return os.path.abspath(execution_name)

        # try to find sample.exe in system path when release
        if shutil.which("sample.exe"):
            return shutil.which("sample.exe")

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

    def measure(self, number_of_waveforms: int, emitting_frequency: float, auto_mode: bool = False) -> None:
        self.communicate("to_measure {} {:.2f} {}".format(number_of_waveforms, emitting_frequency, auto_mode))

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
