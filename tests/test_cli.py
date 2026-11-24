"""End-to-end output checks against the documented synthetic sample."""
import json
from pathlib import Path
import subprocess
import unittest

BIN = str(Path(__file__).resolve().parents[1] / "build" / "slow-explain")


class CLI(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([BIN, *args], text=True, capture_output=True, timeout=5)

    def test_known_answer_json(self):
        run = self.run_cli("--demo", "--json")
        self.assertEqual(run.returncode, 0, run.stderr)
        report = json.loads(run.stdout, parse_constant=lambda s: self.fail(s))
        self.assertEqual(report["source"], "synthetic")
        self.assertEqual(report["seconds"], 2)
        self.assertEqual(report["busy_pct"], 90)
        self.assertEqual(report["swapout_mib"], 1)
        self.assertEqual(report["processes"][0]["cpu_pct"], 300)
        self.assertEqual(report["processes"][2]["write_mib_s"], 32)
        self.assertEqual(report["top_cpu_pids"][0], 100)
        self.assertEqual(report["top_footprint_pids"][0], 102)
        self.assertEqual(report["top_disk_activity_pids"][0], 102)
        self.assertEqual(report["coverage"], {
            "processes": 3, "unmatched": 0, "unavailable": 0, "truncated": False})
        self.assertIn("not latency or disk pressure", report["observations"][-1])

    def test_text_has_provenance_and_limits(self):
        run = self.run_cli("--demo")
        self.assertEqual(run.returncode, 0)
        self.assertIn("SYNTHETIC DEMO", run.stdout)
        self.assertIn("one core", run.stdout)
        self.assertIn("not latency or pressure", run.stdout)
        self.assertIn("may contribute", run.stdout)

    def test_help(self):
        run = self.run_cli("--help")
        self.assertEqual(run.returncode, 0)
        self.assertIn("--seconds", run.stdout)

    def test_invalid_options(self):
        for args in [("--seconds",), ("--unknown",), *[
            ("--demo", "--seconds", s) for s in
            ("", "0", "31", "-1", "+1", "1.5", "nan", " 1", "1x", "999999999999999999999")]]:
            with self.subTest(args=args):
                run = self.run_cli(*args)
                self.assertEqual(run.returncode, 2)
                self.assertEqual(run.stdout, "")
                self.assertTrue(run.stderr)


if __name__ == "__main__":
    unittest.main()
