#!/usr/bin/env python3
"""Cross-prx import/export consistency check for the patched libs directory.

Purpose: every prx that imports a symbol *by name* from another PortPS5 prx
(for example libScePad importing from libc.prx) must find that name in the
provider's export table. `nid_patcher` rewrites undecorated C++ exports to
NIDs, so a dependent that imports a mangled C++ name from libc.prx fails to
load with GetLastError 127 even though the code compiles and links. This
broke the Dreaming Sarah boot twice (Config::Loader, then Unsupported); see
docs/spec/build-toolchain.md "cross-prx host APIs".

Imports bound by NID (11-character base64 names) are skipped: those resolve
through the guest NID tables, not through this contract, and a provider may
legitimately lack one (the relinker reports unresolved NIDs separately).

Usage: check_prx_imports.py <patched-libs-dir>
Needs `objdump` (MinGW binutils) on PATH. Exit 0 = consistent, 1 = missing
exports (each printed as `importer -> provider symbol`), 2 = usage/tool error.
No game data is read.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

# A NID is 11 characters of base64url-ish text; a cross-prx import of that shape is
# assumed to be a NID and skipped. Roughly one NID in ten is letters only, so shape alone
# cannot tell it from a verbatim 11-letter name. Verbatim names come only from the
# `_nid_no_patch_cut` export form, which the source declares, so those declared names
# (see cut_names) are always checked. The hint field in objdump output is hex, so only the
# member name is tested.
NID_RE = re.compile(r"^[A-Za-z0-9+\-]{11}$")
CUT_RE = re.compile(r"\b([A-Za-z0-9_]+)_nid_no_patch_cut\b")


def cut_names(source_root):
    """Names declared with the `_nid_no_patch_cut` suffix under *source_root* (cut form removed)."""
    names = set()
    for path in Path(source_root).rglob("*"):
        if path.suffix in (".cpp", ".hpp", ".h"):
            names.update(CUT_RE.findall(path.read_text(encoding="utf-8", errors="replace")))
    return names


def is_nid(name, verbatim):
    """True when *name* is skipped as a NID: NID-shaped and not a declared verbatim cut name."""
    return bool(NID_RE.match(name)) and name not in verbatim


def objdump(path):
    """Return `objdump -p` text for one module, or exit 2 if the tool fails."""
    try:
        return subprocess.run(
            ["objdump", "-p", path], capture_output=True, text=True, check=True
        ).stdout
    except (OSError, subprocess.CalledProcessError) as error:
        print(f"objdump failed on {path}: {error}", file=sys.stderr)
        sys.exit(2)


def parse_exports(text):
    """Names in the export name-pointer table. objdump wraps long names onto
    the following line, so an entry with an empty name takes the next line."""
    names = set()
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = re.match(r"\s*\[\s*\d+\]\s+\+base\[\s*\d+\]\s+[0-9a-f]+\s*(\S*)$", line)
        if not match:
            continue
        if match.group(1):
            names.add(match.group(1))
        elif index + 1 < len(lines):
            names.add(lines[index + 1].strip())
    return names


def parse_imports(text):
    """Yield (provider dll, member name) for every named import."""
    provider = None
    for line in text.splitlines():
        dll = re.match(r"\s*DLL Name: (\S+)", line)
        if dll:
            provider = dll.group(1)
            continue
        member = re.match(r"\s*[0-9a-f]+\s+<none>\s+[0-9a-f]+\s+(\S+)", line)
        # An all-zero hint/name is the table terminator objdump prints.
        if member and provider and member.group(1) != "00000000":
            yield provider, member.group(1)


def main(argv):
    """Check every prx under argv[1]; return the process exit code (0 ok, 1 missing exports, 2 usage)."""
    if len(argv) != 2 or not os.path.isdir(argv[1]):
        print(__doc__, file=sys.stderr)
        return 2
    libs = argv[1]
    modules = sorted(f for f in os.listdir(libs) if f.endswith(".prx"))
    if not modules:
        print(f"No .prx modules found in {libs}", file=sys.stderr)
        return 2
    texts = {m: objdump(os.path.join(libs, m)) for m in modules}
    exports = {m: parse_exports(t) for m, t in texts.items()}
    verbatim = cut_names(Path(__file__).resolve().parents[1] / "core")
    missing = []
    for importer, text in texts.items():
        for provider, name in parse_imports(text):
            if provider in exports and not is_nid(name, verbatim) and name not in exports[provider]:
                missing.append(f"{importer} -> {provider} {name}")
    for line in missing:
        print(line)
    print(f"checked {len(modules)} modules, {len(missing)} missing named exports")
    return 1 if missing else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
