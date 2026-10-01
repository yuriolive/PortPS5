#!/usr/bin/env python3
"""Regression tests for the PRX import checker using synthetic directories."""

import importlib.util
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


class NidHeuristicTests(unittest.TestCase):
    """Verify which import names the checker treats as NIDs (and so skips)."""

    @staticmethod
    def _load():
        spec = importlib.util.spec_from_file_location("check_prx_imports", _CHECKER)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        return module

    def test_eleven_letter_verbatim_name_is_checked(self):
        # "Unsupported" is exactly 11 letters; it must not be mistaken for a NID,
        # or a provider missing it would pass the check.
        self.assertIsNone(self._load().NID_RE.match("Unsupported"))

    def test_real_nids_are_still_skipped(self):
        # NIDs that contain digits or "+"/"-" are skipped, as before.
        nid_re = self._load().NID_RE
        for nid in ("hj8XRd3EmJ8", "34GvCmMQikQ", "N+rqdWaSl5A", "-vp7IjzBpNY"):
            self.assertIsNotNone(nid_re.match(nid), nid)
