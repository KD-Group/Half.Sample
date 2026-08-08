import time
import tempfile
import unittest
from pathlib import Path
from sample import Sampler, sampler


class MyTestCase(unittest.TestCase):
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
        sampler.measure(
            number_of_waveforms=3,
            emitting_frequency=0.1,
            instant_ai_frequency_threshold=0.1,
            instant_ai_target_points_per_waveform=100,
        )
        while sampler.is_measuring:
            time.sleep(0.01)
        result = sampler.query()
        self.assertTrue(result.success, result.message)
        self.assertEqual(result.acquisition_mode, "instant_ai")
        self.assertEqual(result.instant_ai_late_reads, 0)
        self.assertEqual(result.instant_ai_interpolated_bins, 0)
        self.assertEqual(len(result.wave), 50)
        self.assertAlmostEqual(result.wave_interval, 100000.0)

    def test_instant_ai_dump_can_be_replayed(self):
        sampler.set_sampler(sampler_name="mock_sampler")
        sampler.set_sampler_value("mock_noise", 0)
        with tempfile.TemporaryDirectory(dir=Path(__file__).resolve().parents[1] / "cpp_build") as directory:
            dump_path = Path(directory) / "instant.csv"
            sampler.dump(str(dump_path), 3, 0.1, instant_ai_frequency_threshold=0.1)
            while sampler.is_measuring:
                time.sleep(0.01)
            self.assertTrue(dump_path.read_text(encoding="utf-8").startswith("#HALF_SAMPLE_INSTANT_AI_V1\n"))
            sampler.process(str(dump_path))
            replay = sampler.query()
            self.assertTrue(replay.success)


if __name__ == '__main__':
    unittest.main()
