#!/usr/bin/env python3
"""Regression tests for the PRX import checker using synthetic directories."""

import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

_CHECKER = Path(__file__).resolve().parents[2] / "tools" / "check_prx_imports.py"


class CheckPrxImportsTests(unittest.TestCase):
    """Verify that a directory without PRX modules cannot pass the check."""

    def test_no_prx_modules_is_an_error(self):
        # Both an empty directory and one containing unrelated files must fail
        # before objdump runs, with a diagnostic that identifies the directory.
        for filenames in ((), ("README.txt",)):
            with self.subTest(filenames=filenames), tempfile.TemporaryDirectory() as libs:
                for filename in filenames:
                    (Path(libs) / filename).write_text("synthetic fixture", encoding="utf-8")
                result = subprocess.run(
                    [sys.executable, str(_CHECKER), libs],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                self.assertEqual(result.returncode, 2)
                self.assertEqual(result.stdout, "")
                self.assertIn("No .prx modules found", result.stderr)
                self.assertIn(libs, result.stderr)
