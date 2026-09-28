#!/usr/bin/env python3
"""Unit tests for the PortPS5 progress reporter (tools/progress.py).

Covers the --root revision attribution: the library scan, the opcode-enum
scan AND the ISA instruction list must all follow the selected root, so
base-vs-head deltas never mix revisions. Synthetic trees only; no game data.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

# Add tools/ to the Python path so we can import progress.
_TOOLS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools")
sys.path.insert(0, _TOOLS_DIR)

import progress  # noqa: E402

_PRX_EXPORT = """\
int APS5_VABI sceFooBar(void* p) noexcept {
    return 0;
}
int APS5_VABI sceFooStub(void* p) noexcept {
    NotImplemented_nid_no_patch(__func__);
    return 0;
}
"""

_OPCODES = """\
#pragma once
enum class RdnaOpcode {
    Invalid = 0,
    TestAdd,
    Count,
};
"""

_ISA_WITH_TESTADD = "TEST_ADD VOP2\nV_ADD_F64 VOP3\n"
_ISA_WITHOUT_TESTADD = "V_ADD_F64 VOP3\n"


def _write_tree(root, isa_text):
    root = Path(root)
    prx = root / "core" / "libs" / "prx" / "libSceTest"
    prx.mkdir(parents=True)
    (prx / "Export.cpp").write_text(_PRX_EXPORT)
    opc = root / "core" / "shader" / "recompiler" / "RdnaDecoder" / "include" / "RdnaDecoder"
    opc.mkdir(parents=True)
    (opc / "RdnaOpcode.hpp").write_text(_OPCODES)
    tools = root / "tools"
    tools.mkdir()
    (tools / "rdna_isa.txt").write_text(isa_text)
    return root


def _names(data):
    return {n for g in data["groups"] for n in g["done_names"]}


class ProgressRootTests(unittest.TestCase):
    """--root attribution: libraries, opcodes and ISA follow the selected root."""

    def test_libraries_measured_from_selected_root(self):
        # One implemented + one stub export in the synthetic root.
        with tempfile.TemporaryDirectory() as tmp:
            root = _write_tree(tmp, _ISA_WITH_TESTADD)
            libs = progress.collect_libraries(root / "core" / "libs" / "prx")
            self.assertEqual(libs["done"], 1)
            self.assertEqual(libs["total"], 2)

    def test_isa_comes_from_selected_root(self):
        # Same opcode enum, different ISA revisions: the shader denominator
        # and supported set must follow the selected root, not the checkout.
        with tempfile.TemporaryDirectory() as with_op, tempfile.TemporaryDirectory() as without_op:
            root_with = _write_tree(with_op, _ISA_WITH_TESTADD)
            root_without = _write_tree(without_op, _ISA_WITHOUT_TESTADD)

            def collect(root):
                return progress.collect_shaders(
                    root
                    / "core"
                    / "shader"
                    / "recompiler"
                    / "RdnaDecoder"
                    / "include"
                    / "RdnaDecoder"
                    / "RdnaOpcode.hpp",
                    root / "tools" / "rdna_isa.txt",
                )

            data_with = collect(root_with)
            data_without = collect(root_without)
            self.assertEqual(data_with["total"], 2)
            self.assertEqual(data_without["total"], 1)
            # TEST_ADD resolves through the enum in both trees, but only the
            # tree whose ISA lists it counts it as supported.
            self.assertIn("TEST_ADD", _names(data_with))
            self.assertNotIn("TEST_ADD", _names(data_without))

    def test_cli_root_wiring_end_to_end(self):
        # The --root flag must repoint libraries, opcodes AND ISA together:
        # a tree whose ISA lacks TEST_ADD reports total 1 through the CLI.
        with tempfile.TemporaryDirectory() as tmp:
            root = _write_tree(tmp, _ISA_WITHOUT_TESTADD)
            out = os.path.join(tmp, "out")
            proc = subprocess.run(
                [sys.executable, os.path.join(_TOOLS_DIR, "progress.py"), "--root", str(root), out],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(proc.returncode, 0, proc.stderr)
            with open(os.path.join(out, "progress.json")) as f:
                data = json.load(f)
            self.assertEqual(data["shaders"]["total"], 1)
            self.assertEqual(data["libraries"]["done"], 1)


def _sample():
    groups = [
        {
            "name": "libScePad",
            "label": "Pad",
            "done": 3,
            "todo": 1,
            "done_names": ["a", "b", "c"],
            "todo_names": ["d"],
        },
        {
            "name": "libSceEmpty",
            "label": "Empty",
            "done": 0,
            "todo": 0,
            "done_names": [],
            "todo_names": [],
        },
    ]
    return progress.summarize(groups)


def _write_lib_with_edge_cases(lib):
    lib.mkdir(parents=True)
    (lib / "Export.cpp").write_text(
        "int APS5_VABI sceRealDone(void* p) noexcept {\n"
        "    return 0;\n"
        "}\n"
        "int APS5_VABI sceRealStub(void* p) noexcept {\n"
        "    NotImplemented_nid_no_patch(__func__);\n"
        "    return 0;\n"
        "}\n"
        "// sceMentionedOnly counts as done: the STUB token here is a comment.\n"
        "// See NotImplemented_nid_no_patch for the unimplemented path.\n"
        "int APS5_VABI sceMentionedOnly(void* p) noexcept {\n"
        "    return 1;\n"
        "}\n"
    )
    tests = lib / "tests"
    tests.mkdir()
    (tests / "Helper.cpp").write_text(
        "int APS5_VABI testDouble(void* p) noexcept {\n    return 0;\n}\n"
    )


class ProgressScanTests(unittest.TestCase):
    """Stub-heuristic edges: comment-only STUB mentions and tests/ exclusion."""

    def test_comment_stub_mention_counts_as_done(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp) / "libSceEdge"
            _write_lib_with_edge_cases(lib)
            result = progress.scan_library(lib)
            self.assertIn("sceMentionedOnly", result["done_names"])
            self.assertIn("sceRealStub", result["todo_names"])
            self.assertIn("sceRealDone", result["done_names"])

    def test_tests_directory_excluded(self):
        with tempfile.TemporaryDirectory() as tmp:
            lib = Path(tmp) / "libSceEdge"
            _write_lib_with_edge_cases(lib)
            result = progress.scan_library(lib)
            self.assertNotIn("testDouble", result["done_names"])
            self.assertNotIn("testDouble", result["todo_names"])
            self.assertEqual(result["done"] + result["todo"], 3)


class ProgressRenderTests(unittest.TestCase):
    """Pure render/report helpers: badges, tables, treemap, compare output."""

    def test_badge_color_thresholds(self):
        # >=90 green, >=60 light-green, >=30 yellow, else orange; label shown.
        low = {"percent": 12.5, "done": 1, "total": 8}
        svg = progress.badge("shaders", low)
        self.assertIn("12.5%", svg)
        self.assertIn("#fe7d37", svg)
        for percent, color in ((95, "#4c1"), (70, "#97ca00"), (40, "#dfb317")):
            self.assertIn(
                color, progress.badge("libraries*", {"percent": percent, "done": 1, "total": 1})
            )

    def test_table_totals_row(self):
        html = progress.table("System libraries", "Library", _sample())
        self.assertIn("<td>libScePad</td><td>3</td><td>4</td><td>75%</td>", html)
        self.assertIn("<th>Total</th><th>3</th><th>4</th>", html)
        # Empty groups render with a dash, not a percentage.
        self.assertIn("<td>libSceEmpty</td><td>0</td><td>0</td><td>-</td>", html)

    def test_treemap_skips_empty_groups(self):
        svg = "\n".join(progress.treemap("System libraries*", _sample(), 0))
        self.assertIn("System libraries*: 75.0% (3/4)", svg)
        self.assertIn("libScePad", svg)
        self.assertNotIn("libSceEmpty", svg)

    def test_render_two_panels(self):
        data = _sample()
        svg = progress.render(data, data)
        self.assertIn("<svg", svg)
        self.assertIn("System libraries*", svg)
        self.assertIn("GPU shader instructions", svg)

    def test_summary_links_sources(self):
        shaders = {"groups": [], "done": 0, "total": 0, "percent": 0, "extra": ["VAddI32"]}
        html = progress.summary(_sample(), shaders)
        self.assertIn("PortPS5 progress", html)
        self.assertIn("rdna_isa.txt", html)
        self.assertIn("VAddI32", html)

    def test_compare_reports_deltas(self):
        base = {"libraries": _sample(), "shaders": _sample()}
        head_libs = {
            "name": "x",
            "label": "x",
            "done": 4,
            "todo": 0,
            "done_names": ["a", "b", "c", "d"],
            "todo_names": [],
        }
        head = {
            "libraries": progress.summarize(
                [
                    {
                        "name": "libScePad",
                        "label": "Pad",
                        "done": 4,
                        "todo": 0,
                        "done_names": ["a", "b", "c", "d"],
                        "todo_names": [],
                    },
                    {
                        "name": "libSceEmpty",
                        "label": "Empty",
                        "done": 0,
                        "todo": 0,
                        "done_names": [],
                        "todo_names": [],
                    },
                ]
            ),
            "shaders": _sample(),
        }
        _ = head_libs
        report = progress.report(base, head)
        self.assertIn("implemented", report)
        # Identical snapshots produce no report body.
        self.assertEqual(progress.report(base, base).strip(), "")

    def test_names_and_details(self):
        data = _sample()
        self.assertEqual(
            progress.names(data, "done"),
            {("libScePad", "a"), ("libScePad", "b"), ("libScePad", "c")},
        )
        rows = progress.details("✅", "implemented", "Library", [(("libScePad", "a"))])
        self.assertTrue(any("libScePad" in r for r in rows))
        self.assertEqual(progress.details("✅", "implemented", "Library", []), [])

    def test_camel_and_body_end(self):
        self.assertEqual(progress.camel("test_add_nc"), "TestAddNc")
        self.assertEqual(progress.body_end("int f() { if (x) { y(); } }", 8), 26)


class ProgressMainTests(unittest.TestCase):
    """CLI entry: artifact rendering, --compare, and missing-ISA fallback."""

    def test_main_renders_all_artifacts(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _write_tree(tmp, _ISA_WITH_TESTADD)
            out = os.path.join(tmp, "out")
            self.assertEqual(progress.main([out, "--root", str(root)]), 0)
            for name in (
                "progress.json",
                "badge-libraries.svg",
                "badge-shaders.svg",
                "progress.svg",
                "index.html",
            ):
                self.assertTrue(os.path.isfile(os.path.join(out, name)), name)
            with open(os.path.join(out, "progress.json")) as f:
                data = json.load(f)
            self.assertEqual(data["shaders"]["total"], 2)
            self.assertEqual(data["libraries"]["done"], 1)

    def test_main_compare_prints_report(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = _write_tree(tmp, _ISA_WITH_TESTADD)
            first = os.path.join(tmp, "first")
            self.assertEqual(progress.main([first, "--root", str(root)]), 0)
            base = os.path.join(first, "progress.json")
            self.assertEqual(progress.main(["--compare", base, base]), 0)

    def test_main_root_without_isa_falls_back(self):
        # A base revision predating rdna_isa.txt still measures: the ISA
        # falls back to this checkout instead of raising FileNotFoundError.
        with tempfile.TemporaryDirectory() as tmp:
            root = _write_tree(tmp, _ISA_WITH_TESTADD)
            os.remove(root / "tools" / "rdna_isa.txt")
            out = os.path.join(tmp, "out")
            self.assertEqual(progress.main([out, "--root", str(root)]), 0)
            with open(os.path.join(out, "progress.json")) as f:
                data = json.load(f)
            self.assertGreater(data["shaders"]["total"], 1)


if __name__ == "__main__":
    unittest.main()
