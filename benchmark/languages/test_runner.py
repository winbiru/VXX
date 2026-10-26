import unittest

from run import CASES, reference, verify_output


class ResultValidationTests(unittest.TestCase):
    def output(self, case, iterations=32):
        values = reference(case)
        return "\n".join(map(str, values+[100, sum(values)*2, iterations]))

    def test_every_corpus_has_complete_valid_result(self):
        for case in CASES:
            record = verify_output(self.output(case), case, 32)
            self.assertEqual(record["iterations"], 32)
            self.assertGreater(record["ns_per_operation"], 0)

    def test_wrong_value_is_rejected_even_when_sum_is_unchanged(self):
        lines = self.output("branch").splitlines()
        lines[0] = str(int(lines[0])+1)
        lines[1] = str(int(lines[1])-1)
        with self.assertRaises(ValueError):
            verify_output("\n".join(lines), "branch", 32)

    def test_bad_checksum_and_count_are_rejected(self):
        for field in (-1, -2):
            lines = self.output("fibonacci").splitlines()
            lines[field] = "0"
            with self.assertRaises(ValueError):
                verify_output("\n".join(lines), "fibonacci", 32)

    def test_bad_timing_and_partial_stdout_are_rejected(self):
        for value in ("0", "-1", "nan", "inf"):
            lines = self.output("list").splitlines()
            lines[16] = value
            with self.assertRaises(ValueError):
                verify_output("\n".join(lines), "list", 32)
        with self.assertRaises(ValueError):
            verify_output("42", "arithmetic", 32)


if __name__ == "__main__":
    unittest.main()
