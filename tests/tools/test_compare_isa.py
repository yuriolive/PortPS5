#!/usr/bin/env python3
"""Unit tests for tools/compare_isa.py (RDNA decode coverage versus references).

Synthetic reference trees only: a fake KytyPS5 decoder header and a fake
SharpEmu translator file. No code from those projects is used.
"""

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path

_TOOLS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools")
sys.path.insert(0, _TOOLS_DIR)

import compare_isa  # noqa: E402

_ISA = {"V_ADD_F32": "VOP2", "V_CMPX_EQ_I16": "VOPC", "S_CMPK_EQ_I32": "SOPK", "V_NOP": "VOP1"}


def _kyty(root):
    path = Path(root) / compare_isa.KYTY_DECODER
    path.parent.mkdir(parents=True)
    # V_ADD_F32 and V_CMPX_EQ_I16 are ISA names; NOT_AN_OP must be ignored.
    path.write_text("enum class Op { V_ADD_F32, V_CMPX_EQ_I16, NOT_AN_OP };\n")
    return Path(root)


def _sharpemu(root):
    path = Path(root) / compare_isa.SHARPEMU_DECODER
    path.parent.mkdir(parents=True)
    # PascalCase strings map back to ISA names; "Unrelated" is not an opcode.
    path.write_text('0x01 => "VCmpxEqI16", 0x02 => "SCmpkEqI32", x => "Unrelated",\n')
    return Path(root)


class ParserTests(unittest.TestCase):
    """Each reference parser returns only names present in the ISA list."""

    def test_kyty_reads_upper_snake_names(self):
        with tempfile.TemporaryDirectory() as tmp:
            names = compare_isa.decoded_by_kyty(_kyty(tmp), _ISA)
        self.assertEqual(names, {"V_ADD_F32", "V_CMPX_EQ_I16"})

    def test_sharpemu_maps_pascal_strings_back_to_isa(self):
        with tempfile.TemporaryDirectory() as tmp:
            names = compare_isa.decoded_by_sharpemu(_sharpemu(tmp), _ISA)
        self.assertEqual(names, {"V_CMPX_EQ_I16", "S_CMPK_EQ_I32"})


class CompareTests(unittest.TestCase):
    """The gap is what references decode minus ours, ranked by agreement."""

    def test_gap_excludes_ours_and_counts_agreement(self):
        # Invariant: an instruction PortPS5 decodes never appears in the gap,
        # and agreement counts how many references decode each missing name.
        # Dropping the "- ours" subtraction or the holder list fails this.
        report = compare_isa.compare(
            {"V_ADD_F32"},
            {
                "KytyPS5": {"V_ADD_F32", "V_CMPX_EQ_I16"},
                "SharpEmu": {"V_CMPX_EQ_I16", "S_CMPK_EQ_I32"},
            },
            _ISA,
        )
        self.assertEqual(report["gap_total"], 2)
        self.assertEqual(report["agreed_by_two_or_more"], 1)
        self.assertEqual(
            report["gaps"]["VOPC"],
            [{"name": "V_CMPX_EQ_I16", "decoded_by": ["KytyPS5", "SharpEmu"]}],
        )
        self.assertEqual(report["references"]["KytyPS5"], {"decoded": 2, "not_in_ours": 1})
        self.assertIn("| VOPC | 1 | 1 |", compare_isa.markdown(report))

    def test_main_requires_a_reference_and_writes_json(self):
        # The CLI refuses to run with no reference, and --json writes the report.
        with self.assertRaises(SystemExit):
            compare_isa.main([])
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "report.json"
            self.assertEqual(
                compare_isa.main(["--kyty", str(_kyty(Path(tmp) / "k")), "--json", str(out)]), 0
            )
            data = json.loads(out.read_text())
        self.assertIn("KytyPS5", data["references"])
        self.assertEqual(data["isa_total"], len(compare_isa.load_isa()))


if __name__ == "__main__":
    unittest.main()
