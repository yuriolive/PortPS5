# tools/progress.py
# PortPS5 implementation-progress reporter, ported from AnyPS5 upstream/main.
# Scans core/libs/prx for APS5_VABI definitions (done = no NotImplemented_nid_no_patch
# in body) and the RDNA decoder opcode enum vs tools/rdna_isa.txt, then renders
# progress.svg/.json, shields-style badges and an HTML summary. Outputs drive the
# GitHub Pages site (workflows/progress.yml) and per-PR deltas (progress-report.yml
# + progress-comment.yml). No game data needed; static source scan only.
import argparse
import json
import os
import re
from html import escape
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PRX = ROOT / "core" / "libs" / "prx"
OPCODES = (
    ROOT
    / "core"
    / "shader"
    / "recompiler"
    / "RdnaDecoder"
    / "include"
    / "RdnaDecoder"
    / "RdnaOpcode.hpp"
)
ISA = ROOT / "tools" / "rdna_isa.txt"
SOURCE = f"https://github.com/{os.environ.get('GITHUB_REPOSITORY', 'yuriolive/PortPS5')}/blob/main"
DEFINITION = re.compile(r"\bAPS5_VABI\s+(\w+)\s*\([^;{]*\)\s*(?:noexcept\s*)?\{")
STUB = "NotImplemented_nid_no_patch"
FLAT_SEGMENTS = ("GLOBAL_", "SCRATCH_")
OPCODE_SENTINELS = {"Invalid", "Count", "Unknown", "Unsupported"}
OPCODE_ALIASES = {
    "TBufferLoadFormatX": "TBUFFER_LOAD_FORMAT_X",
    "TBufferLoadFormatXyzw": "TBUFFER_LOAD_FORMAT_XYZW",
    "ExpMrt": "EXP",
    "ExpPos": "EXP",
    "ExpParam": "EXP",
    "VAddcU32": "V_ADD_CO_CI_U32",
    "VMadMixloF16": "V_FMA_MIXLO_F16",
    "VMadMixhiF16": "V_FMA_MIXHI_F16",
}
REPORT_ROWS = 100
PANEL_WIDTH, GAP, MAP_HEIGHT, HEADER = 495, 10, 280, 30
DONE_COLOR, TODO_COLOR, BORDER, TEXT = "#2ea043", "#1f6feb", "#0d1117", "#ffffff"


def body_end(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "{":
            depth += 1
        elif text[i] == "}":
            depth -= 1
            if depth == 0:
                return i
    return len(text)


def scan_library(path):
    done, todo = set(), set()
    for source in path.rglob("*.cpp"):
        text = source.read_text(errors="ignore")
        for match in DEFINITION.finditer(text):
            name = match.group(1)
            if name.endswith("_nid_no_patch"):
                continue
            body = text[match.end() - 1 : body_end(text, match.end() - 1)]
            (todo if STUB in body else done).add(name)
    todo -= done
    return {
        "name": path.name,
        "label": path.name.removeprefix("libSce"),
        "done": len(done),
        "todo": len(todo),
        "done_names": sorted(done),
        "todo_names": sorted(todo),
    }


def summarize(groups):
    done = sum(g["done"] for g in groups)
    total = done + sum(g["todo"] for g in groups)
    return {
        "done": done,
        "total": total,
        "percent": round(100 * done / total, 2) if total else 0,
        "groups": groups,
    }


def collect_libraries(prx_dir=PRX):
    return summarize([scan_library(p) for p in sorted(prx_dir.iterdir()) if p.is_dir()])


def camel(name):
    return "".join(part.capitalize() for part in name.split("_"))


def collect_shaders(opcodes_path=OPCODES, isa_path=ISA):
    isa = {}
    for line in isa_path.read_text().splitlines():
        if line and not line.startswith("#"):
            name, encoding = line.split()
            isa[name] = encoding
    by_camel = {camel(name): name for name in isa}
    enum = re.search(
        r"enum class RdnaOpcode[^{]*\{(.*?)\};", opcodes_path.read_text(), re.DOTALL
    ).group(1)
    opcodes = [
        o
        for o in re.findall(r"^\s*([A-Z]\w*)\s*[,=]", enum, re.MULTILINE)
        if o not in OPCODE_SENTINELS
    ]
    supported, extra = set(), []
    for opcode in opcodes:
        name = OPCODE_ALIASES.get(opcode) or by_camel.get(opcode)
        if name in isa:
            supported.add(name)
            if name.startswith("FLAT_"):
                supported.update(
                    n for n in (s + name.removeprefix("FLAT_") for s in FLAT_SEGMENTS) if n in isa
                )
        else:
            extra.append(opcode)
    groups = {}
    for name, encoding in isa.items():
        group = groups.setdefault(
            encoding,
            {
                "name": encoding,
                "label": encoding,
                "done": 0,
                "todo": 0,
                "done_names": [],
                "todo_names": [],
            },
        )
        state = "done" if name in supported else "todo"
        group[state] += 1
        group[f"{state}_names"].append(name)
    result = summarize(sorted(groups.values(), key=lambda g: g["name"]))
    result["extra"] = extra
    return result


def worst_ratio(row, side):
    area = sum(row)
    return max(max(side * side * r / area**2, area**2 / (side * side * r)) for r in row)


def place(row, x, y, w, h, rects):
    thickness = sum(row) / min(w, h)
    offset = 0
    for area in row:
        length = area / thickness
        if w >= h:
            rects.append((x, y + offset, thickness, length))
        else:
            rects.append((x + offset, y, length, thickness))
        offset += length
    return (x + thickness, y, w - thickness, h) if w >= h else (x, y + thickness, w, h - thickness)


def squarify(values, x, y, w, h):
    total = sum(values)
    areas = [v * w * h / total for v in values]
    rects, row = [], []
    while areas:
        side = min(w, h)
        if not row or worst_ratio([*row, areas[0]], side) <= worst_ratio(row, side):
            row.append(areas.pop(0))
            continue
        x, y, w, h = place(row, x, y, w, h, rects)
        row = []
    if row:
        place(row, x, y, w, h, rects)
    return rects


def cells(count, x, y, w, h):
    if not count:
        return []
    rows = max(1, min(count, round((count * h / w) ** 0.5)))
    result = []
    for r in range(rows):
        in_row = count // rows + (r < count % rows)
        cw = w / in_row
        result += [(x + c * cw, y + r * h / rows, cw, h / rows) for c in range(in_row)]
    return result


def rect(x, y, w, h, color, stroke=0.5):
    return (
        f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" '
        f'fill="{color}" stroke="{BORDER}" stroke-width="{stroke}"/>'
    )


def text(x, y, value, size):
    return (
        f'<text x="{x:.2f}" y="{y:.2f}" font-family="sans-serif" font-size="{size}" fill="{TEXT}" '
        f'stroke="{BORDER}" stroke-width="3" paint-order="stroke">{escape(value)}</text>'
    )


def treemap(title, data, left):
    groups = sorted(
        (g for g in data["groups"] if g["done"] + g["todo"]), key=lambda g: -(g["done"] + g["todo"])
    )
    parts = [
        text(left + 4, 21, f"{title}: {data['percent']}% ({data['done']}/{data['total']})", 16)
    ]
    for group, (x, y, w, h) in zip(
        groups,
        squarify([g["done"] + g["todo"] for g in groups], left, HEADER, PANEL_WIDTH, MAP_HEIGHT),
        strict=True,
    ):
        total = group["done"] + group["todo"]
        parts.append(
            f"<g><title>{escape(group['name'])}: {group['done']}/{total} ({100 * group['done'] / total:.0f}%)</title>"
        )
        for i, cell in enumerate(cells(total, x + 1, y + 1, w - 2, h - 2)):
            parts.append(rect(*cell, DONE_COLOR if i < group["done"] else TODO_COLOR))
        parts.append(
            f'<rect x="{x:.2f}" y="{y:.2f}" width="{w:.2f}" height="{h:.2f}" fill="none" stroke="{BORDER}" stroke-width="2"/>'
        )
        label = group["label"][: int((w - 10) / 7)]
        if (label == group["label"] or len(label) >= 3) and h > 22:
            parts.append(text(x + 5, y + 16, label, 12))
        parts.append("</g>")
    return parts


def render(libraries, shaders):
    width, height = 2 * PANEL_WIDTH + GAP, HEADER + MAP_HEIGHT
    parts = [
        f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {width} {height}" width="{width}" height="{height}">',
        rect(0, 0, width, height, BORDER, 0),
    ]
    parts += treemap("System libraries*", libraries, 0)
    parts += treemap("GPU shader instructions", shaders, PANEL_WIDTH + GAP)
    parts.append("</svg>")
    return "\n".join(parts)


def badge(label, data):
    percent = data["percent"]
    color = (
        "#4c1"
        if percent >= 90
        else "#97ca00"
        if percent >= 60
        else "#dfb317"
        if percent >= 30
        else "#fe7d37"
    )
    value = f"{percent}%"
    left, right = 10 + 7 * len(label), 10 + 7 * len(value)
    width = left + right
    return (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="20" role="img" aria-label="{label}: {value}">'
        f'<rect width="{left}" height="20" fill="#555"/><rect x="{left}" width="{right}" height="20" fill="{color}"/>'
        f'<g fill="#fff" font-family="Verdana,DejaVu Sans,sans-serif" font-size="11" text-anchor="middle">'
        f'<text x="{left / 2}" y="14">{label}</text><text x="{left + right / 2}" y="14">{value}</text></g></svg>'
    )


def table(heading, column, data):
    rows = [
        f"<h2>{heading}</h2>",
        "<table>",
        f"<tr><th>{column}</th><th>Implemented</th><th>Total</th><th>%</th></tr>",
    ]
    for group in sorted(data["groups"], key=lambda g: g["name"].lower()):
        total = group["done"] + group["todo"]
        percent = f"{100 * group['done'] / total:.0f}%" if total else "-"
        rows.append(
            f"<tr><td>{escape(group['name'])}</td><td>{group['done']}</td><td>{total}</td><td>{percent}</td></tr>"
        )
    rows.append(
        f"<tr><th>Total</th><th>{data['done']}</th><th>{data['total']}</th><th>{data['percent']}%</th></tr>"
    )
    rows.append("</table>")
    return "\n".join(rows)


def summary(libraries, shaders):
    extra = ", ".join(f"<code>{escape(o)}</code>" for o in shaders["extra"])
    return (
        "\n".join(
            [
                "<!DOCTYPE html>",
                '<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">',
                "<title>PortPS5 progress</title>",
                (
                    "<style>body{font-family:sans-serif;max-width:1000px;margin:auto;padding:16px}img{max-width:100%}"
                    "table{border-collapse:collapse}th,td{border:1px solid #ccc;padding:2px 8px}td+td,th+th{text-align:right}</style>"
                ),
                "</head><body>",
                "<h1>Progress</h1>",
                '<img src="progress.svg" alt="progress">',
                (
                    f'<p>Generated by <a href="{SOURCE}/tools/progress.py">tools/progress.py</a> on every push to <code>main</code>. '
                    'Raw numbers: <a href="progress.json">progress.json</a>.</p>'
                ),
                table("System libraries", "Library", libraries),
                (
                    "<p>A function is implemented when it no longer calls <code>NotImplemented_nid_no_patch</code>. "
                    f'The total only includes functions already declared in <a href="{SOURCE}/core/libs/prx">core/libs/prx</a>, '
                    "not every function exported by the PS5 firmware.</p>"
                ),
                table("GPU shader instructions", "Encoding", shaders),
                (
                    f'<p>The total is the AMD RDNA 1 + RDNA 2 instruction list (<a href="{SOURCE}/tools/rdna_isa.txt">tools/rdna_isa.txt</a>). '
                    f'An instruction is implemented when the <a href="{SOURCE}/core/shader/recompiler/RdnaDecoder">decoder</a> recognizes it. '
                    f"Decoded opcodes not present in AMD's public list are not counted: {extra}.</p>"
                ),
                "</body></html>",
            ]
        )
        + "\n"
    )


def names(data, state):
    return {(g["name"], n) for g in data["groups"] for n in g.get(f"{state}_names", [])}


def details(icon, title, column, items):
    if not items:
        return []
    rows = [
        f"<details>\n<summary>{icon} {len(items)} {title}</summary>\n",
        f"| {column} | Name |",
        "| - | - |",
    ]
    rows += [f"| {group} | `{name}` |" for group, name in sorted(items)[:REPORT_ROWS]]
    if len(items) > REPORT_ROWS:
        rows.append(f"| ... | {len(items) - REPORT_ROWS} more |")
    return [*rows, "\n</details>"]


def compare(title, column, unit, base, head):
    base_done, head_done = names(base, "done"), names(head, "done")
    base_all, head_all = base_done | names(base, "todo"), head_done | names(head, "todo")
    implemented, declared = head_done - base_done, head_all - base_all - head_done
    regressed, removed = base_done & (head_all - head_done), base_all - head_all
    if not (implemented or declared or regressed or removed):
        return []
    delta = round(head["percent"] - base["percent"], 2)
    icon = "📈" if delta > 0 else "📉" if delta < 0 else "➖"  # noqa: RUF001 — icon glyphs are user-facing PR-comment markers
    counts = [
        f"{n:+} {label}"
        for n, label in (
            (len(implemented), "implemented"),
            (len(declared), "declared"),
            (-len(removed), "removed"),
        )
        if n
    ]
    lines = [f"{icon} **{title}**: {head['percent']}% ({delta:+}%, {', '.join(counts)} {unit})", ""]
    lines += details("✅", "implemented", column, implemented)
    lines += details("🆕", "declared as stubs", column, declared)
    lines += details("⚠️", "went back to stubs", column, regressed)
    lines += details("🗑️", "removed", column, removed)
    return [*lines, ""]


def report(base, head):
    lines = compare(
        "System libraries", "Library", "functions", base["libraries"], head["libraries"]
    )
    lines += compare(
        "GPU shader instructions", "Encoding", "instructions", base["shaders"], head["shaders"]
    )
    return "\n".join(lines)


def main(argv=None):
    """Entry point: render progress.json/svg/badges/html into *output*.

    Returns the process exit code (0 always; usage errors exit via argparse).
    Paths stay local so repeated calls (tests) never leak one root into the next.
    """
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path, nargs="?")
    parser.add_argument(
        "--root", type=Path, help="source tree to measure instead of the one containing this script"
    )
    parser.add_argument(
        "--compare",
        type=Path,
        nargs=2,
        metavar=("BASE", "HEAD"),
        help="print a markdown report of the changes between two progress.json files",
    )
    args = parser.parse_args(argv)
    if args.compare:
        base, head = (json.loads(path.read_text()) for path in args.compare)
        print(report(base, head), end="")
        return 0
    if not args.output:
        parser.error("the output directory is required")
    if args.root:
        root = args.root
        prx = root / "core" / "libs" / "prx"
        opcodes = root / OPCODES.relative_to(ROOT)
        isa = root / ISA.relative_to(ROOT)
    else:
        prx, opcodes, isa = PRX, OPCODES, ISA
    output = args.output
    output.mkdir(parents=True, exist_ok=True)
    libraries, shaders = collect_libraries(prx), collect_shaders(opcodes, isa)
    (output / "progress.json").write_text(
        json.dumps({"libraries": libraries, "shaders": shaders}, indent=2)
    )
    (output / "badge-libraries.svg").write_text(badge("libraries*", libraries))
    (output / "badge-shaders.svg").write_text(badge("shaders", shaders))
    (output / "progress.svg").write_text(render(libraries, shaders))
    (output / "index.html").write_text(summary(libraries, shaders))
    print(f"libraries {libraries['done']}/{libraries['total']} ({libraries['percent']}%)")
    print(f"shaders {shaders['done']}/{shaders['total']} ({shaders['percent']}%)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
