"""Compare RDNA instruction decode coverage of PortPS5 with reference projects.

Maps each project's decoder opcode names onto tools/rdna_isa.txt (the same
1166-entry list progress.py uses) and reports the instructions a reference
decodes that PortPS5 does not, grouped by encoding. Only opcode names are
read from the reference trees: no code is copied, so their licences do not
apply to the output (the names are AMD's public ISA mnemonics).

Supported layouts (checked out locally, paths passed on the command line):
- AnyPS5 and PortPS5: the RdnaOpcode enum, through progress.collect_shaders.
- KytyPS5: UPPER_SNAKE opcode names in
  src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h.
- SharpEmu: PascalCase opcode strings in
  src/SharpEmu.ShaderCompiler/Gen5ShaderTranslator.cs.

The match is by name, so it is an estimate: a name present in a decoder file
is taken as decoded. Usage:

    python tools/compare_isa.py --anyps5 PATH --kyty PATH --sharpemu PATH [--json OUT]
"""

import argparse
import json
import re
from pathlib import Path

import progress

KYTY_DECODER = Path("src/graphics/shader/recompiler/frontend/decode/ShaderDecoder.h")
SHARPEMU_DECODER = Path("src/SharpEmu.ShaderCompiler/Gen5ShaderTranslator.cs")


def load_isa(isa_path=progress.ISA):
    """Return {name: encoding} from the ISA list."""
    isa = {}
    for line in isa_path.read_text().splitlines():
        if line and not line.startswith("#"):
            name, encoding = line.split()
            isa[name] = encoding
    return isa


def decoded_by_enum(root, isa_path=progress.ISA):
    """ISA names an AnyPS5-layout tree decodes (PortPS5 or AnyPS5)."""
    data = progress.collect_shaders(root / progress.OPCODES.relative_to(progress.ROOT), isa_path)
    return {n for g in data["groups"] for n in g["done_names"]}


def decoded_by_kyty(root, isa):
    """ISA names KytyPS5's decoder header lists (UPPER_SNAKE identifiers)."""
    text = (root / KYTY_DECODER).read_text(errors="ignore")
    return set(re.findall(r"\b([A-Z][A-Z0-9_]+)\b", text)) & set(isa)


def decoded_by_sharpemu(root, isa):
    """ISA names SharpEmu's decoder maps to (PascalCase string literals)."""
    by_pascal = {progress.camel(name): name for name in isa}
    text = (root / SHARPEMU_DECODER).read_text(errors="ignore")
    return {by_pascal[s] for s in re.findall(r'"([A-Z][A-Za-z0-9]+)"', text) if s in by_pascal}


def compare(ours, references, isa):
    """Build the gap report: per-encoding lists of names some reference decodes and we don't.

    Each entry records which references decode it, so the report can rank
    instructions that several independent decoders agree on.
    """
    gaps = {}
    for name in sorted(set().union(*references.values()) - ours):
        holders = sorted(ref for ref, names in references.items() if name in names)
        gaps.setdefault(isa[name], []).append({"name": name, "decoded_by": holders})
    return {
        "isa_total": len(isa),
        "ours": len(ours),
        "references": {
            ref: {"decoded": len(names), "not_in_ours": len(names - ours)}
            for ref, names in references.items()
        },
        "gap_total": sum(len(v) for v in gaps.values()),
        "agreed_by_two_or_more": sum(
            1 for v in gaps.values() for e in v if len(e["decoded_by"]) >= 2
        ),
        "gaps": dict(sorted(gaps.items(), key=lambda kv: (-len(kv[1]), kv[0]))),
    }


def markdown(report):
    """Render the report as the markdown table used in shader-recompiler.md."""
    refs = ", ".join(
        f"{ref} {v['decoded']} ({v['not_in_ours']} not in PortPS5)"
        for ref, v in report["references"].items()
    )
    lines = [
        f"PortPS5 decodes {report['ours']}/{report['isa_total']}. References: {refs}.",
        (
            f"Decoded by at least one reference but not PortPS5: {report['gap_total']} "
            f"({report['agreed_by_two_or_more']} by two or more)."
        ),
        "",
        "| Encoding | Missing | Agreed by 2+ | Examples |",
        "|---|---|---|---|",
    ]
    for encoding, entries in report["gaps"].items():
        agreed = [e["name"] for e in entries if len(e["decoded_by"]) >= 2]
        examples = ", ".join(f"`{e['name']}`" for e in entries[:4])
        lines.append(f"| {encoding} | {len(entries)} | {len(agreed)} | {examples} |")
    return "\n".join(lines) + "\n"


def main(argv=None):
    """Entry point: print the markdown report; optionally write the JSON report."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--anyps5", type=Path, help="AnyPS5 checkout")
    parser.add_argument("--kyty", type=Path, help="KytyPS5 checkout")
    parser.add_argument("--sharpemu", type=Path, help="SharpEmu checkout")
    parser.add_argument("--json", type=Path, help="write the full report here")
    args = parser.parse_args(argv)
    isa = load_isa()
    references = {}
    if args.anyps5:
        references["AnyPS5"] = decoded_by_enum(args.anyps5)
    if args.kyty:
        references["KytyPS5"] = decoded_by_kyty(args.kyty, isa)
    if args.sharpemu:
        references["SharpEmu"] = decoded_by_sharpemu(args.sharpemu, isa)
    if not references:
        parser.error("pass at least one of --anyps5, --kyty, --sharpemu")
    report = compare(decoded_by_enum(progress.ROOT), references, isa)
    if args.json:
        args.json.write_text(json.dumps(report, indent=2))
    print(markdown(report), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
