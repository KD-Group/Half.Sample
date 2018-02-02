import sample
import unittest


class MyTestCase(unittest.TestCase):
    def test_commander_frame(self):
        sampler = sample.Sample()

        result = sampler.communicate("simple_test")
        self.assertTrue(result.success)

        try:
            result = sampler.communicate("unknown_command")
        except sampler.Error as e:
            self.assertEqual(str(e), "command_not_found")


if __name__ == '__main__':
    unittest.main()