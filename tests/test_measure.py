import math
import sys
import time
import tempfile
import unittest
from pathlib import Path
from sample import Result, Sampler, sampler


def wait_until_sampling_stops(is_measuring, cancel, timeout_seconds=5.0,
                              monotonic=time.monotonic, sleep=time.sleep):
    deadline = monotonic() + timeout_seconds
    while is_measuring():
        if monotonic() >= deadline:
            try:
                cancel()
                cancel_diagnostic = "best-effort cancellation requested"
            except Exception as error:
                cancel_diagnostic = "best-effort cancellation failed: {!r}".format(error)
            raise AssertionError(
                "sampling remained active after {:.3f} seconds; {}".format(
                    timeout_seconds, cancel_diagnostic
                )
            )
        sleep(0.01)


class MyTestCase(unittest.TestCase):
    def setUp(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 0)
        sampler.set_sampler_value("mock_work_iterations", 0)

    def tearDown(self):
        wait_until_sampling_stops(
            is_measuring=lambda: sampler.is_measuring,
            cancel=sampler.cancel_sampling,
        )
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 0)
        sampler.set_sampler_value("mock_work_iterations", 0)

    def test_optional_instant_ai_arguments_are_backward_compatible(self):
        client = Sampler()
        client.communicate = lambda command, executor=None: command
        self.assertEqual(client.measure(3, 0.1), "to_measure 3 0.1 False")
        self.assertEqual(
            client.measure(3, 0.1, instant_ai_frequency_threshold=0.5),
            "to_measure 3 0.1 False 0.5 100 10",
        )
        self.assertEqual(
            client.measure(
                3,
                0.05,
                instant_ai_frequency_threshold=0.1,
                instant_ai_target_points_per_waveform=100,
                instant_ai_max_reliable_polling_hz=10.0,
            ),
            "to_measure 3 0.05 False 0.1 100 10",
        )
        self.assertEqual(
            client.dump("capture.csv", 3, 0.05),
            "to_dump capture.csv 3 0.05 False",
        )
        self.assertEqual(
            client.dump(
                "capture.csv",
                3,
                0.05,
                instant_ai_frequency_threshold=0.1,
                instant_ai_target_points_per_waveform=100,
                instant_ai_max_reliable_polling_hz=9.5,
            ),
            "to_dump capture.csv 3 0.05 False 0.1 100 9.5",
        )

    def test_progress_and_cancel_methods_send_exact_commands(self):
        client = Sampler()
        client.communicate = lambda command, executor=None: command
        self.assertEqual(client.sampling_progress(), "to_sampling_progress")
        self.assertEqual(client.cancel_sampling(), "to_cancel_sampling")

    def test_result_has_backward_compatible_instant_ai_defaults(self):
        result = Result()
        self.assertEqual(result.error_category, "")
        self.assertFalse(result.retryable)
        self.assertFalse(result.cancelled)
        self.assertEqual(result.planned_seconds, 0.0)
        self.assertEqual(result.elapsed_seconds, 0.0)
        self.assertEqual(result.completed_cycles, 0)
        self.assertEqual(result.target_cycles, 0)
        self.assertEqual(result.successful_reads, 0)
        self.assertEqual(result.late_reads, 0)
        self.assertEqual(result.acquisition_mode, "")
        self.assertEqual(result.instant_ai_complete_waveforms, 0)
        self.assertEqual(result.instant_ai_planned_duration_seconds, 0.0)
        self.assertEqual(result.instant_ai_actual_duration_seconds, 0.0)
        self.assertEqual(result.instant_ai_late_reads, 0)
        self.assertEqual(result.instant_ai_interpolated_bins, 0)

    def test_readme_documents_retryable_state_separately_from_acquisition_failures(self):
        readme = (Path(__file__).resolve().parents[1] / "README.md").read_text(encoding="utf-8")
        self.assertIn("Instant AI 采集失败的可重试类别", readme)
        self.assertIn("`state` 表示生命周期或并发冲突", readme)
        self.assertIn("当前测量结束后再试", readme)
        self.assertIn("`state` 当前也是 `retryable=True`", readme)

    def test_is_measuring(self):
        result = sampler.communicate("is_measuring")
        self.assertEqual(result.measuring, False)

    def test_measure_rejects_malformed_or_extra_instant_ai_options(self):
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_measure 3 0.05 False invalid 100 10")
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_measure 3 0.05 False 0.1 100 10 extra")

    def test_config_rejects_malformed_or_extra_instant_ai_options(self):
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_config 3 0.05 False 0.1 invalid")
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_config 3 0.05 False 0.1 100 invalid")
        with self.assertRaisesRegex(Sampler.Error, "invalid_instant_ai_config"):
            sampler.communicate("to_config 3 0.05 False 0.1 100 10 extra")

    def test_active_measurement_rejects_overlapping_request_without_mutating_config(self):
        sampler.set_sampler_value("mock_work_iterations", 100000000)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
            sampler.measure(1, 0.1, instant_ai_frequency_threshold=0.1)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
                sampler.dump(str(Path(directory) / "busy.csv"), 1, 0.1, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.instant_ai_complete_waveforms, 3)
        self.assertAlmostEqual(result.instant_ai_planned_duration_seconds, 80.0)

    def test_query_during_active_measurement_is_not_completed_success(self):
        sampler.set_sampler_value("mock_work_iterations", 100000000)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        active = sampler.query()
        self.assertTrue(active.measuring)
        self.assertFalse(active.success)
        while sampler.is_measuring:
            time.sleep(0.01)
        self.assertTrue(sampler.query().success)

    def test_sampling_stop_wait_has_poll_delay_deadline_and_timeout_cleanup(self):
        now = [0.0]
        sleep_calls = []
        cancel_calls = []

        def fake_sleep(seconds):
            sleep_calls.append(seconds)
            now[0] += seconds

        with self.assertRaisesRegex(AssertionError, "sampling remained active after 0.030 seconds"):
            wait_until_sampling_stops(
                is_measuring=lambda: True,
                cancel=lambda: cancel_calls.append(True),
                timeout_seconds=0.03,
                monotonic=lambda: now[0],
                sleep=fake_sleep,
            )
        self.assertGreaterEqual(len(sleep_calls), 3)
        self.assertTrue(all(seconds == 0.01 for seconds in sleep_calls))
        self.assertEqual(cancel_calls, [True])

    def test_public_progress_and_cancel_stop_active_mock_with_bounded_wait(self):
        sampler.set_sampler_value("mock_work_iterations", 2000000000)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        progress = sampler.sampling_progress()
        self.assertTrue(progress.measuring)
        self.assertAlmostEqual(progress.planned_seconds, 80.0)
        self.assertGreaterEqual(progress.elapsed_seconds, 0.0)
        self.assertLessEqual(progress.elapsed_seconds, progress.planned_seconds)
        self.assertGreaterEqual(progress.completed_cycles, 0)
        self.assertLessEqual(progress.completed_cycles, progress.target_cycles)
        self.assertEqual(progress.target_cycles, 4)
        self.assertGreaterEqual(progress.successful_reads, 0)
        self.assertGreaterEqual(progress.late_reads, 0)
        with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
            sampler.set_sampler("mock_sampler")
        with self.assertRaisesRegex(Sampler.Error, "now_in_measuring"):
            sampler.set_sampler_value("mock_tau", 100)
        sampler.cancel_sampling()
        wait_until_sampling_stops(
            is_measuring=lambda: sampler.is_measuring,
            cancel=sampler.cancel_sampling,
        )
        result = sampler.query()
        self.assertFalse(result.success)
        self.assertTrue(result.cancelled)
        self.assertEqual(result.message, "user_cancelled")
        self.assertEqual(result.error_category, "cancelled")
        self.assertFalse(result.retryable)

    def test_mock_missing_bin_controls_reject_invalid_values(self):
        sampler.set_sampler_value("mock_missing_bin_start", 7)
        sampler.set_sampler_value("mock_missing_bin_count", 2)
        sampler.set_sampler_value("mock_missing_bin_start", 1.5)
        sampler.set_sampler_value("mock_missing_bin_count", -1)
        self.assertEqual(sampler.get_sampler_value("mock_missing_bin_start").mock_missing_bin_start, 7)
        self.assertEqual(sampler.get_sampler_value("mock_missing_bin_count").mock_missing_bin_count, 2)

    def test_legacy_two_option_measure_protocol_defaults_max_polling(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.communicate("to_measure 1 0.1 False 0.1 100")
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.acquisition_mode, "instant_ai")

    def test_simple_measure(self):
        sampler.set_sampler(sampler_name="mock_sampler")

        mock_v0, mock_v_inf = 2.5, 5.0  # V
        sampler.set_sampler_value("mock_v0", mock_v0)
        sampler.set_sampler_value("mock_v_inf", mock_v_inf)

        mock_tau = 100  # us
        sampler.set_sampler_value("mock_tau", mock_tau)

        sampler.measure(number_of_waveforms=2, emitting_frequency=200)

        while sampler.is_measuring:
            time.sleep(0.1)

        result = sampler.query()
        self.assertEqual(result.success, True)

        self.assertTrue(0 < len(result.wave) <= 1000)
        # self.assertTrue(2.3 < result.wave[0] < 2.7)

    def test_instant_ai_mock_uses_requested_waveform_count(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0.49)
        sampler.measure(
            number_of_waveforms=3,
            emitting_frequency=0.05,
            instant_ai_frequency_threshold=0.1,
            instant_ai_target_points_per_waveform=100,
            instant_ai_max_reliable_polling_hz=10,
        )
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.acquisition_mode, "instant_ai")
        self.assertEqual(result.instant_ai_complete_waveforms, 3)
        self.assertAlmostEqual(result.instant_ai_planned_duration_seconds, 80.0)
        self.assertAlmostEqual(result.instant_ai_actual_duration_seconds, 80.0)
        self.assertEqual(result.instant_ai_late_reads, 0)
        self.assertEqual(result.instant_ai_interpolated_bins, 0)
        self.assertEqual(len(result.wave), 50)
        self.assertAlmostEqual(result.wave_interval, 200000.0)

    def test_instant_ai_continuous_mock_handles_waveform_counts_and_phase_offsets(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 0)
        for waveform_count, phase_offset in ((1, 0.0), (3, 0.23)):
            with self.subTest(waveform_count=waveform_count, phase_offset=phase_offset):
                sampler.set_sampler_value("mock_phase_offset", phase_offset)
                sampler.measure(waveform_count, 0.05, instant_ai_frequency_threshold=0.1)
                while sampler.is_measuring:
                    time.sleep(0.01)
                result = sampler.query()
                self.assertTrue(result.success, result.message)
                self.assertEqual(result.instant_ai_complete_waveforms, waveform_count)
                self.assertAlmostEqual(
                    result.instant_ai_planned_duration_seconds, (waveform_count + 1) / 0.05
                )
                self.assertEqual(len(result.wave), 50)
                self.assertAlmostEqual(result.wave_interval, 200000.0)

    def test_instant_ai_continuous_mock_interpolates_at_most_two_missing_bins(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0.49)
        sampler.set_sampler_value("mock_missing_bin_start", 20)
        sampler.set_sampler_value("mock_missing_bin_count", 2)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertGreaterEqual(result.instant_ai_interpolated_bins, 2)

    def test_instant_ai_continuous_mock_reports_stable_coverage_error(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0.49)
        sampler.set_sampler_value("mock_missing_bin_start", 20)
        sampler.set_sampler_value("mock_missing_bin_count", 3)
        sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertFalse(result.success)
        self.assertEqual(result.error_category, "coverage")
        self.assertFalse(result.retryable)
        self.assertFalse(result.cancelled)
        self.assertEqual(result.instant_ai_complete_waveforms, 0)
        self.assertEqual(result.wave_interval, 0.0)
        self.assertEqual(result.wave, [])

    def test_instant_ai_continuous_mock_preserves_terminal_edge_when_bin_zero_is_missing(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", 0)
        sampler.set_sampler_value("mock_missing_bin_count", 2)
        try:
            sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            result = sampler.query()
            self.assertTrue(result.success, result.message)
            self.assertGreaterEqual(result.instant_ai_interpolated_bins, 2)
        finally:
            sampler.set_sampler_value("mock_missing_bin_start", 0)
            sampler.set_sampler_value("mock_missing_bin_count", 0)

    def test_instant_ai_continuous_mock_wraparound_missing_run_reaches_coverage_error(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        sampler.set_sampler_value("mock_phase_offset", 0)
        sampler.set_sampler_value("mock_missing_bin_start", -2)
        sampler.set_sampler_value("mock_missing_bin_count", 3)
        try:
            sampler.measure(3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            result = sampler.query()
            self.assertFalse(result.success)
            self.assertEqual(result.error_category, "coverage")
            self.assertFalse(result.retryable)
        finally:
            sampler.set_sampler_value("mock_missing_bin_start", 0)
            sampler.set_sampler_value("mock_missing_bin_count", 0)

    def test_legacy_instant_ai_v1_dump_can_be_replayed(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "instant.csv"
            rows = [
                "#HALF_SAMPLE_INSTANT_AI_V1",
                "emitting_frequency=0.1",
                "target_points=100",
                "number_of_waveforms=3",
                "waveform_index,planned_seconds,actual_seconds,voltage",
            ]
            for waveform in range(3):
                for phase_bin in range(100):
                    seconds = (phase_bin + 0.5) / 10.0
                    phase = (phase_bin + 0.5) / 100.0
                    voltage = 5.0 + (2.5 - 5.0) * math.exp(phase / 0.1 * 1e6 / -100.0) if phase < 0.5 else 0.0
                    rows.append(f"{waveform},{seconds},{seconds},{voltage}")
            dump_path.write_text("\n".join(rows) + "\n", encoding="utf-8")
            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success)

    def test_instant_ai_v2_dump_is_self_contained_and_replays_after_config_changes(self):
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "continuous-v2.csv"
            sampler.dump(str(dump_path), 3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            captured = sampler.query()
            self.assertTrue(captured.success, captured.message)

            lines = dump_path.read_text(encoding="utf-8").splitlines()
            self.assertEqual(lines[0], "#HALF_SAMPLE_INSTANT_AI_V2")
            self.assertEqual(lines[1:8], [
                "emitting_frequency=0.05",
                "target_points=100",
                "number_of_waveforms=3",
                "max_reliable_polling_hz=10",
                "planned_duration_seconds=80",
                "actual_duration_seconds=80",
                "planned_seconds,actual_seconds,voltage,read_success,read_error_code",
            ])
            self.assertEqual(len(lines), 8 + 401)
            self.assertEqual(list(dump_path.parent.glob(dump_path.name + ".half-sample-tmp.*")), [])

            sampler.communicate("to_config 1 20 False")
            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success, replay.message)
            self.assertEqual(replay.acquisition_mode, "instant_ai")
            self.assertEqual(replay.instant_ai_complete_waveforms, 3)
            self.assertAlmostEqual(replay.instant_ai_planned_duration_seconds, 80.0)
            self.assertAlmostEqual(replay.instant_ai_actual_duration_seconds, 80.0)
            self.assertEqual(replay.instant_ai_late_reads, 0)

    @unittest.skipUnless(sys.platform == "win32", "Windows UTF-16 filesystem path contract")
    def test_windows_unicode_path_dump_and_process_are_atomic(self):
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            unicode_directory = Path(directory) / "采集数据"
            unicode_directory.mkdir()
            dump_path = unicode_directory / "测量.csv"
            sampler.dump(str(dump_path), 3, 0.05, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            captured = sampler.query()
            self.assertTrue(captured.success, captured.message)
            self.assertTrue(dump_path.is_file())
            self.assertEqual(list(unicode_directory.glob("*.half-sample-tmp.*")), [])

            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success, replay.message)
            self.assertEqual(replay.instant_ai_complete_waveforms, 3)
            self.assertEqual(list(unicode_directory.glob("*.half-sample-tmp.*")), [])

    def test_failed_process_clears_previous_wave_and_keeps_stable_error(self):
        sampler.set_sampler_value("mock_noise", 0)
        sampler.measure(1, 0.05, instant_ai_frequency_threshold=0.1)
        while sampler.is_measuring:
            time.sleep(0.01)
        self.assertTrue(sampler.query().success)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            malformed = Path(directory) / "malformed-v2.csv"
            malformed.write_text("#HALF_SAMPLE_INSTANT_AI_V2\nemitting_frequency=bad\n", encoding="utf-8")
            sampler.process(str(malformed))
            failed = sampler.query()
            self.assertFalse(failed.success)
            self.assertEqual(failed.message, "invalid_instant_ai_config")
            self.assertEqual(failed.wave, [])
            self.assertEqual(failed.wave_interval, 0.0)

    def test_unmarked_buffered_dump_remains_replayable(self):
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "buffered.csv"
            sampler.dump(str(dump_path), 3, 100000.0)
            while sampler.is_measuring:
                time.sleep(0.01)
            captured = sampler.query()
            self.assertTrue(captured.success, captured.message)
            self.assertFalse(dump_path.read_text(encoding="utf-8").startswith("#HALF_SAMPLE"))

            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success, replay.message)
            self.assertEqual(replay.acquisition_mode, "buffered_ai")

    def test_empty_legacy_instant_ai_v1_returns_stable_failure(self):
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "empty-instant.csv"
            dump_path.write_text(
                "#HALF_SAMPLE_INSTANT_AI_V1\n"
                "emitting_frequency=0.1\n"
                "target_points=100\n"
                "number_of_waveforms=3\n"
                "waveform_index,planned_seconds,actual_seconds,voltage\n",
                encoding="utf-8",
            )
            sampler.process(str(dump_path))
            result = sampler.query()
            self.assertFalse(result.success)
            self.assertEqual(result.error_category, "coverage")
            self.assertFalse(result.retryable)

    def test_missing_process_file_has_stable_contract(self):
        missing = Path(__file__).resolve().parents[1] / "cpp_build" / "does-not-exist.csv"
        sampler.process(str(missing))
        result = sampler.query()
        self.assertFalse(result.success)
        self.assertEqual(result.message, "file_not_found")
        self.assertEqual(result.error_category, "file")
        self.assertFalse(result.retryable)


if __name__ == '__main__':
    unittest.main()
