import time
import unittest
from sample import sampler


class MyTestCase(unittest.TestCase):
    def test_estimate(self):
        sampler.set_sampler(sampler_name="mock_sampler")

        for mock_tau in [50, 100, 200]:  # us
            sampler.communicate("set_mock_tau {}".format(mock_tau))

            sampler.measure(number_of_waveforms=2, emitting_frequency=200)

            while sampler.is_measuring:
                time.sleep(0.1)

            result = sampler.query()
            self.assertEqual(result.success, True)

            tau = result.tau
            self.assertTrue(abs(tau - mock_tau) < 2.0)

    def test_auto_mode(self):
        sampler.set_sampler(sampler_name="mock_sampler")

        mock_taus = [1, 10, 100, 1000, 10000]  # us
        for mock_tau in mock_taus:
            sampler.communicate('set_mock_tau {}'.format(mock_tau))
            sampler.measure(number_of_waveforms=4, emitting_frequency=10, auto_mode=True)
            while sampler.is_measuring:
                time.sleep(0.1)
            result = sampler.query()
            diff_ratio = abs(result.tau - mock_tau) / mock_tau

            self.assertTrue(diff_ratio <= 0.1)


if __name__ == '__main__':
    unittest.main()
