r"""Check of `curvy.exe --run`: scripts started from the command line, their output, exit codes, arguments and the open/save they do.

    python D:\code\curvy\tests\gui\test_curvy_cli.py --exe D:\code\curvy\out\build\x64-release\curvy.exe

Each test starts the real exe (its window shows briefly) but sends no input. A run ends by itself (--exit-after-run); the timeout only
guards against a hang.
"""
import argparse
import os
import subprocess
import sys
import tempfile
import unittest

EXE = r"D:\code\curvy\out\build\x64-release\curvy.exe"
SAMPLE = r"D:\code\curvy\Resources\samples\layersAndGroups.curvy"


class CommandLineRun(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.settings = os.path.join(self.folder.name, "settings.json5")

    def script(self, text, name="job.py"):
        path = os.path.join(self.folder.name, name)
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        return path

    def curvy(self, *args, timeout=60):
        return subprocess.run([EXE, "--settings", self.settings, *args], capture_output=True, text=True, encoding="utf-8", timeout=timeout)

    def test_a_script_prints_to_stdout_and_exits_zero(self):
        result = self.curvy("--run", self.script("print('hello from a script')"), "--exit-after-run")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("hello from a script", result.stdout)

    def test_an_error_goes_to_stderr_and_exits_one(self):
        result = self.curvy("--run", self.script("print('before')\n1 / 0"), "--exit-after-run")
        self.assertEqual(result.returncode, 1)
        self.assertIn("before", result.stdout)
        self.assertIn("ZeroDivisionError", result.stderr)

    def test_args_holds_what_follows_the_double_dash(self):
        result = self.curvy("--run", self.script("print(args)"), "--exit-after-run", "--", "a.curvy", "two words")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("['a.curvy', 'two words']", result.stdout)

    def test_a_missing_script_exits_three(self):
        result = self.curvy("--run", os.path.join(self.folder.name, "nope.py"), "--exit-after-run")
        self.assertEqual(result.returncode, 3)
        self.assertIn("cannot read the script", result.stderr)

    def test_a_script_past_the_time_limit_exits_two(self):
        with open(self.settings, "w") as f:
            f.write("{ scriptEditor: { scriptTimeout: true, scriptTimeoutSeconds: 1 } }")
        result = self.curvy("--run", self.script("import time\nwhile True:\n    time.sleep(0.05)"), "--exit-after-run")
        self.assertEqual(result.returncode, 2)
        self.assertIn("ran longer than 1 seconds", result.stderr)

    def test_open_then_change_then_save_is_a_batch_job(self):
        target = os.path.join(self.folder.name, "out.curvy").replace("\\", "/")
        script = self.script("doc.add_layer('Added by the batch job')\nprint(doc.save(args[0]))\nprint([l.name for l in doc.layers])")
        result = self.curvy("--open", SAMPLE, "--run", script, "--exit-after-run", "--", target)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(target, result.stdout)
        self.assertIn("Added by the batch job", result.stdout)
        self.assertTrue(os.path.exists(target))
        # The saved file really holds the change: a second run opens it and lists the layers.
        again = self.curvy("--run", self.script("doc.open(args[0])\nprint([l.name for l in doc.layers])", "check.py"), "--exit-after-run", "--", target)
        self.assertEqual(again.returncode, 0, again.stderr)
        self.assertIn("Added by the batch job", again.stdout)


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", default=EXE)
    known, rest = parser.parse_known_args()
    EXE = known.exe
    unittest.main(argv=[sys.argv[0]] + rest)
