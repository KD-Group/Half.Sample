import subprocess


def run_captured(arguments, **kwargs):
    kwargs["stdout"] = subprocess.PIPE
    kwargs["stderr"] = subprocess.PIPE
    kwargs["universal_newlines"] = True
    kwargs.setdefault("encoding", "utf-8")
    return subprocess.run(arguments, **kwargs)
