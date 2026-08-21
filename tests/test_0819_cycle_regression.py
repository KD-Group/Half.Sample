import math
import os
import subprocess
import time
from pathlib import Path

import pytest

from sample.protocol import parse_assignments


SOURCE_ROOT = Path(__file__).resolve().parents[1]
SAMPLE_EXE = SOURCE_ROOT / "cpp_build" / "sample.exe"
DEFAULT_REGRESSION_DIR = Path(r"D:\kunde\code\KDM3000\Sample采集数据-20280819")
REGRESSION_CASES = (
    (1, 1, 65),
    (2, 16, 65),
    (3, 32, 65),
    (4, 1, 62),
    (5, 16, 62),
    (6, 32, 62),
    (7, 32, 72),
    (8, 16, 72),
    (9, 1, 72),
    (10, 32, 72),
    (11, 32, 60),
    (12, 16, 60),
    (13, 1, 60),
)
FINITE_SCALARS = (
    "maximum",
    "minimum",
    "sampling_interval",
    "wave_interval",
    "tau",
    "w",
    "b",
    "loss",
    "v_inf",
    "cycle_maximum",
    "cycle_minimum",
    "cycle_vmax",
    "cycle_vmin",
    "cycle_vpp",
    "cycle_vtop",
    "cycle_vbase",
)


@pytest.fixture(scope="module")
def regression_dir():
    configured = os.environ.get("HALF_SAMPLE_0819_REGRESSION_DIR")
    directory = Path(configured) if configured else DEFAULT_REGRESSION_DIR
    if not directory.is_dir():
        pytest.skip("0819 regression data directory not found: {}".format(directory))
    return directory


@pytest.fixture(scope="module")
def sample_executable(regression_dir):
    del regression_dir
    assert SAMPLE_EXE.is_file(), "build the current source with: scons -Q sample.exe"
    return SAMPLE_EXE


def _split_protocol_responses(stdout):
    responses = []
    current = []
    for line in stdout.splitlines():
        if line.strip() == "EOF":
            responses.append("\n".join(current))
            current = []
        else:
            current.append(line)
    assert not current, "sample.exe output did not end with EOF: {!r}".format(current)
    assert len(responses) == 3, "expected three protocol responses, got {}: {!r}".format(
        len(responses), stdout
    )
    return responses


def _run_protocol_case(executable, directory, case_number, waveforms):
    filename = "0819-{}.txt".format(case_number)
    assert (directory / filename).is_file(), "missing regression input: {}".format(directory / filename)
    commands = "\n".join((
        "to_config {} 50 False 0 100 10 independent_cycle".format(waveforms),
        "to_process {} independent_cycle".format(filename),
        "to_query",
        "",
    ))
    started = time.perf_counter()
    completed = subprocess.run(
        [str(executable)],
        input=commands,
        text=True,
        cwd=str(directory),
        capture_output=True,
        timeout=180,
        check=False,
    )
    elapsed = time.perf_counter() - started
    assert completed.returncode == 0, (
        "sample.exe exited {} for case {}\nstdout:\n{}\nstderr:\n{}".format(
            completed.returncode, case_number, completed.stdout, completed.stderr
        )
    )
    responses = _split_protocol_responses(completed.stdout)
    parsed = [parse_assignments(response) if response.strip() else {} for response in responses]
    for command, response in zip(("to_config", "to_process", "to_query"), parsed):
        assert response.get("error") is not True, "{} failed for case {}: {}".format(
            command, case_number, response
        )
    return parsed[-1], elapsed


@pytest.mark.parametrize(
    "case_number,waveforms,nominal_mv",
    REGRESSION_CASES,
    ids=["0819-{}-n{}-{}mv".format(*case) for case in REGRESSION_CASES],
)
def test_0819_independent_cycle_regression(
        regression_dir, sample_executable, case_number, waveforms, nominal_mv):
    result, elapsed = _run_protocol_case(sample_executable, regression_dir, case_number, waveforms)

    assert result["success"] is True
    assert result["waveform_processing_mode"] == "independent_cycle"
    assert result["complete_waveforms"] == waveforms
    assert result["v_inf_valid"] is True
    for field in FINITE_SCALARS:
        assert math.isfinite(result[field]), "{} is not finite: {!r}".format(field, result[field])
    assert result["cycle_vpp"] == pytest.approx(
        result["cycle_vmax"] - result["cycle_vmin"], abs=2e-6
    )
    assert result["v_inf"] == pytest.approx(
        result["cycle_vtop"] - result["cycle_vbase"], abs=2e-6
    )

    nominal_voltage = nominal_mv / 1000.0
    assert abs(result["v_inf"] - nominal_voltage) <= max(0.015, nominal_voltage * 0.25)
    assert 50.0 <= result["tau"] <= 500.0
    assert result["loss"] >= 0.0
    assert math.sqrt(result["loss"]) / result["v_inf"] < 0.15
    assert len(result["wave"]) >= 3
    assert all(math.isfinite(value) for value in result["wave"])

    print(
        "0819-{} PASS n={} v_inf={:.6f} tau={:.6f} normalized_rmse={:.6f} elapsed={:.3f}s".format(
            case_number,
            waveforms,
            result["v_inf"],
            result["tau"],
            math.sqrt(result["loss"]) / result["v_inf"],
            elapsed,
        )
    )
