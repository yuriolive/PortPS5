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

    def test_alphabet_only_nid_is_still_skipped(self):
        # About one NID in ten has no digit or punctuation; it must stay exempt or an
        # unresolved guest NID would fail the build (review finding on #68).
        checker = self._load()
        self.assertTrue(checker.is_nid("AbCdEfGhIjK", set()))
        self.assertTrue(checker.is_nid("hj8XRd3EmJ8", set()))

    def test_declared_cut_name_is_checked_even_if_nid_shaped(self):
        # "Unsupported" is exactly 11 letters. When the source declares it with the
        # _nid_no_patch_cut form, a provider missing it must be reported.
        checker = self._load()
        self.assertFalse(checker.is_nid("Unsupported", {"Unsupported"}))

    def test_cut_names_scans_the_source_tree(self):
        checker = self._load()
        with tempfile.TemporaryDirectory() as root:
            # The bare suffix inside a string literal has no name before it and must not match.
            (Path(root) / "a.cpp").write_text(
                'GLOBAL_ALIAS(Unsupported_nid_no_patch_cut, f);\nconst char* k = "_nid_no_patch_cut";\n',
                encoding="utf-8",
            )
            (Path(root) / "notes.txt").write_text("Ignored_nid_no_patch_cut", encoding="utf-8")
            self.assertEqual(checker.cut_names(root), {"Unsupported"})
