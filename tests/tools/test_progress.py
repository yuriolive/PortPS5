#!/usr/bin/env python3
"""Unit tests for the PortPS5 progress reporter (tools/progress.py).

Covers the --root revision attribution: with --root, the library scan, the
opcode-enum scan AND the ISA instruction list must all come from the selected
root, so base-vs-head deltas never mix revisions. No game data needed.
"""

import json
import os
import subprocess
import sys
import tempfile
import unittest

_TOOLS_DIR = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), '..', '..', 'tools'
)
_PROGRESS = os.path.join(_TOOLS_DIR, 'progress.py')

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
    prx = os.path.join(root, 'core', 'libs', 'prx', 'libSceTest')
    os.makedirs(prx)
    with open(os.path.join(prx, 'Export.cpp'), 'w') as f:
        f.write(_PRX_EXPORT)
    opc = os.path.join(root, 'core', 'shader', 'recompiler', 'RdnaDecoder',
                       'include', 'RdnaDecoder')
    os.makedirs(opc)
    with open(os.path.join(opc, 'RdnaOpcode.hpp'), 'w') as f:
        f.write(_OPCODES)
    tools = os.path.join(root, 'tools')
    os.makedirs(tools)
    with open(os.path.join(tools, 'rdna_isa.txt'), 'w') as f:
        f.write(isa_text)


def _run(root, out):
    proc = subprocess.run(
        [sys.executable, _PROGRESS, '--root', root, out],
        capture_output=True, text=True)
    assert proc.returncode == 0, proc.stderr
    with open(os.path.join(out, 'progress.json')) as f:
        return json.load(f)


class ProgressRootTests(unittest.TestCase):
    def test_libraries_measured_from_selected_root(self):
        # One implemented + one stub export in the synthetic root.
        with tempfile.TemporaryDirectory() as tmp:
            _write_tree(tmp, _ISA_WITH_TESTADD)
            data = _run(tmp, os.path.join(tmp, 'out'))
            libs = data['libraries']
            self.assertEqual(libs['done'], 1)
            self.assertEqual(libs['total'], 2)

    def test_isa_comes_from_selected_root(self):
        # Same opcode enum, different ISA revisions: the shader denominator
        # and supported set must follow --root, not the script checkout.
        with tempfile.TemporaryDirectory() as with_op, \
                tempfile.TemporaryDirectory() as without_op:
            _write_tree(with_op, _ISA_WITH_TESTADD)
            _write_tree(without_op, _ISA_WITHOUT_TESTADD)
            data_with = _run(with_op, os.path.join(with_op, 'out'))
            data_without = _run(without_op, os.path.join(without_op, 'out'))
            self.assertEqual(data_with['shaders']['total'], 2)
            self.assertEqual(data_without['shaders']['total'], 1)
            # TEST_ADD resolves through the enum in both trees, but only the
            # tree whose ISA lists it counts it as supported.
            with_names = {n for g in data_with['shaders']['groups']
                          for n in g['done_names']}
            without_names = {n for g in data_without['shaders']['groups']
                             for n in g['done_names']}
            self.assertIn('TEST_ADD', with_names)
            self.assertNotIn('TEST_ADD', without_names)


if __name__ == '__main__':
    unittest.main()
