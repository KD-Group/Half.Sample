import time
import unittest
from sample import sampler


class MyTestCase(unittest.TestCase):
    def test_is_measuring(self):
        result = sampler.communicate("is_measuring")
        self.assertEqual(result.measuring, False)

    def test_simple_measure(self):
        sampler.set_sampler(sampler_name="mock_sampler")

        mock_tau = 100  # us
        sampler.communicate("set_mock_tau {}".format(mock_tau))

        sampler.measure(number_of_waveforms=2, emitting_frequency=200)

        while sampler.is_measuring:
            time.sleep(0.1)

        result = sampler.query()
        self.assertEqual(result.success, True)

        self.assertTrue(0 < len(result.wave) <= 1000)
        self.assertTrue(2.3 < result.wave[0] < 2.7)


if __name__ == '__main__':
    unittest.main()
