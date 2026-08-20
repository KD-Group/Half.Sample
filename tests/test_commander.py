import unittest
from sample import protocol, sampler


class MyTestCase(unittest.TestCase):
    def test_protocol_parser_accepts_cpp_escape_contract(self):
        response = 'message = "plain \\"quote\\" \\\\ slash\\rline\\nnext\\ttab"\n'
        expected = 'plain "quote" \\ slash\rline\nnext\ttab'
        self.assertEqual(protocol.parse_assignments(response), {'message': expected})

    def test_commander_frame(self):
        result = sampler.communicate("simple_test")
        self.assertTrue(result.success)

        try:
            result = sampler.communicate("unknown_command")
        except sampler.Error as e:
            self.assertTrue(str(e).startswith("command_not_found"))


if __name__ == '__main__':
    unittest.main()
