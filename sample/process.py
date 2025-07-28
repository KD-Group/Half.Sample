import time
import logging
import sys
import threading
import subprocess
import multiprocessing


logger = logging.getLogger('library')


def process(target, args=(), finished=None):
    p = multiprocessing.Process(target=target, args=args)
    p.start()

    def check_finished():
        p.join()
        finished()

    threading.Thread(target=check_finished).start()


class Process:
    def __init__(self, cmd):
        self.cmd = cmd
        self.p = None
        self._max_retries = 1
        self._start_process()

    def set_max_retry(self, max_retries):
        self._max_retries = max_retries

    def get_max_retry(self):
        return self._max_retries

    def _start_process(self):
        """启动或重启子进程"""
        if self.p is not None:
            # 如果旧进程还在运行，先终止它
            try:
                if self.p.poll() is None:  # 进程仍在运行
                    self.p.terminate()
                    self.p.wait(timeout=3)
            except Exception as e:
                logger.warning(f"failed to poll: {e}")
                try:
                    self.p.kill()
                    self.p.wait()
                except Exception as e:
                    logger.warning(f"failed to kill: {e}")
                    pass
        # 启动新进程
        self.p = subprocess.Popen(self.cmd,
                                  shell=False,
                                  creationflags=0x00000008 if sys.platform == "win32" else 0,
                                  stdin=subprocess.PIPE,
                                  stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)

    def _is_process_alive(self):
        """检查进程是否存活"""
        if self.p is None:
            return False
        return self.p.poll() is None

    def _ensure_process_alive(self):
        """确保进程存活，如果崩溃则重启"""
        if not self._is_process_alive():
            logger.warning(f"subprocess [{self.cmd}] crash, restarting...")
            self._start_process()
            logger.warning(f"subprocess [{self.cmd}] restart completed")

    def write_line(self, line: str):
        """写入一行数据，自动处理进程重启"""
        for attempt in range(self._max_retries):
            try:
                self._ensure_process_alive()
                self.p.stdin.write(line.encode())
                self.p.stdin.write('\n'.encode())
                self.p.stdin.flush()
                return  # 成功写入，退出重试循环
            except (BrokenPipeError, OSError) as e:
                if attempt < self._max_retries - 1:
                    logger.warning(f"write line failed (attempted {attempt + 1}/{self._max_retries}): {e}")
                    self._start_process()  # 重启进程
                    time.sleep(0.1)  # 短暂等待新进程启动
                else:
                    raise RuntimeError(f"无法向子进程写入数据，已重试{self._max_retries}次: {e}")

    def read_line(self) -> str:
        """读取一行数据，自动处理进程重启"""
        for attempt in range(self._max_retries):
            try:
                self._ensure_process_alive()
                line = self.p.stdout.readline()
                if not line:  # EOF，进程可能已终止
                    if not self._is_process_alive():
                        raise RuntimeError("子进程已终止")
                    return ""
                return line.decode().replace('\r\n', '').replace('\n', '')
            except (OSError, RuntimeError) as e:
                if attempt < self._max_retries - 1:
                    logger.warning(f"read line failed (attempted {attempt + 1}/{self._max_retries}): {e}")
                    self._start_process()  # 重启进程
                    time.sleep(0.1)  # 短暂等待新进程启动
                else:
                    raise RuntimeError(f"无法从子进程读取数据，已重试{self._max_retries}次: {e}")

    def read_until(self, until):
        """读取直到指定字符串，自动处理进程重启"""
        lines = []

        for attempt in range(self._max_retries):
            try:
                while True:
                    line = self.read_line()
                    if line == until:
                        return '\n'.join(lines)
                    elif not line:
                        break
                    else:
                        lines.append(line)
            except (RuntimeError, OSError) as e:
                if attempt < self._max_retries - 1:
                    logger.warning(f"read_until failed (attempted {attempt + 1}/{self._max_retries}): {e}")
                    self._start_process()  # 重启进程
                    lines = []  # 清空已读取的行
                    time.sleep(0.1)
                else:
                    raise RuntimeError(f"read_until 失败，已重试{self._max_retries}次: {e}")

        return '\n'.join(lines)

    def terminate(self):
        """终止子进程"""
        if self.p is not None and self.p.poll() is None:
            try:
                self.p.terminate()
                self.p.wait(timeout=3)
            except subprocess.TimeoutExpired as e:
                logger.warning(f"Timeout expired while trying to terminate [{self.cmd}]: {e}")
                try:
                    self.p.kill()
                    self.p.wait()
                except Exception as e:
                    logger.warning(f"Failed to kill [{self.cmd}]: {e}")
            except Exception as e:
                logger.warning(f"Failed to terminate [{self.cmd}]: {e}")

    def get_return_code(self):
        """获取进程返回码"""
        if self.p is not None:
            return self.p.poll()
        return None
