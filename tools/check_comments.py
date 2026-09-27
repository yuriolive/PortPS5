#!/usr/bin/env python3
"""
PortPS5 automated code comment and best-practice linter.

Enforces three documentation rules on C++ source files:

  Rule 1 - File-level purpose header:
      Every .hpp/.cpp file in core/, tests/, and tools/ must begin (before
      includes or guards) with a 2+ line comment block explaining subsystem
      purpose and ownership/ABI invariants.

  Rule 2 - Documentation on public guest exports (APS5_VABI):
      Every function declaration or definition that uses the APS5_VABI
      calling convention must be preceded by a doc comment (/** ... */,
      ///, or //) explaining the function's role, parameters, and
      return/error codes.

  Rule 3 - Test invariant documentation (GoogleTest):
      Every TEST(suite, case) and TEST_F(fixture, case) in tests/ must be
      preceded by a comment describing the behavioral invariant or edge case
      being verified.

Usage:
    python tools/check_comments.py [--base <git-ref>] [--all] [--paths <dir>...]

Exit codes:
    0  - All checked files pass.
    1  - One or more documentation violations found.
    2  - Tool error (e.g., git failure with no fallback).
"""

import argparse
import os
import re
import subprocess
import sys
from dataclasses import dataclass

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

EXIT_OK = 0
EXIT_VIOLATION = 1
EXIT_ERROR = 2

#: File extensions checked by the linter.
SOURCE_EXTENSIONS = ('.cpp', '.hpp', '.cc', '.cxx', '.h', '.hxx')

#: Directories that are exempt from all checks (vendored / generated code).
EXEMPT_DIRS = frozenset({'3rdparty', 'build', '.codebase-memory'})

#: Top-level directories where Rule 1 (file headers) applies.
RULE1_DIRS = frozenset({'core', 'tests', 'tools'})

#: Top-level directory where Rule 3 (TEST docs) applies.
TESTS_DIR = 'tests'

#: Regex matching an APS5_VABI *function* declaration/definition.
#: The lookbehind (?<![\w*]) prevents matching inside identifiers that end
#: with APS5_VABI (e.g. a variable named myAPS5_VABI) and the requirement
#: for \w+ after APS5_VABI + whitespace ensures we match a function name,
#: not a pointer-type alias like  (APS5_VABI *).
VABI_FUNC_RE = re.compile(r'(?<![\w*])APS5_VABI\s+\w+\s*\(')

#: Regex matching TEST( or TEST_F( at the start of a line (ignoring indentation).
TEST_RE = re.compile(r'^\s*TEST(_F)?\s*\(')

# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------


@dataclass
class Violation:
    """A single documentation policy violation."""

    file: str       # Repository-relative path with forward slashes
    line: int       # 1-based line number
    rule: str       # 'file-header', 'vabi-doc', or 'test-doc'
    message: str    # Human-readable description


# ---------------------------------------------------------------------------
# Helper functions (all pure -- easy to unit-test)
# ---------------------------------------------------------------------------


def normalize_path(path):
    """Normalize path separators to forward slashes for consistent reporting."""
    return path.replace(os.sep, '/').replace('\\', '/')


def is_comment_line(line):
    """Return True if the stripped line is a comment line."""
    stripped = line.strip()
    if not stripped:
        return False
    return (
        stripped.startswith('//')
        or stripped.startswith('/*')
        or stripped.startswith('*')
    )


def is_attribute_line(line):
    """Return True if the stripped line is a C++11 attribute like [[noreturn]]."""
    stripped = line.strip()
    return stripped.startswith('[[') and ']]' in stripped


def read_source_lines(path):
    """Read a source file and return its lines (keeping line endings)."""
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        return f.readlines()


def find_first_non_empty(lines):
    """Return the 0-based index of the first non-empty line, or None if the file is blank."""
    for i, line in enumerate(lines):
        if line.strip():
            return i
    return None


def has_doc_comment_before(lines, idx, skip_attrs=True):
    """Check whether a doc comment immediately precedes the line at *idx*.

    Walks backwards from idx-1, skipping blank lines and (optionally) C++11
    attributes.  Returns True as soon as a comment line is encountered.
    Returns False if a non-comment, non-blank, non-attribute line is hit first,
    or if the beginning of the file is reached.
    """
    j = idx - 1
    while j >= 0:
        prev = lines[j].strip()
        if prev == '':
            j -= 1
            continue
        if skip_attrs and is_attribute_line(prev):
            j -= 1
            continue
        if is_comment_line(prev):
            return True
        break
    return False


def extract_func_name(line):
    """Extract the function name from an APS5_VABI function signature line."""
    m = VABI_FUNC_RE.search(line)
    if not m:
        return 'unknown'
    # The match looks like "APS5_VABI funcname("
    after = m.group(0)
    return after.split('APS5_VABI')[-1].strip().split('(')[0].strip()


def is_statically_false_condition(cond):
    """Return True if a preprocessor condition expression is statically disabled (e.g. 0)."""
    cond = re.sub(r'//.*$', '', cond)
    cond = re.sub(r'/\*.*?\*/', '', cond)
    cond = cond.strip()
    if cond in ('0', '(0)', 'false', 'FALSE', '!1', '(!1)', '!true', '(!true)'):
        return True
    if re.match(r'^(?:0|\(0\)|false|FALSE)\b', cond):
        return True
    return False


@dataclass
class SanitizedLine:
    """Sanitized line representation for comment policy checking."""
    original: str
    cleaned: str
    is_active: bool
    is_pure_comment: bool


def sanitize_source_file(lines):
    """Tokenize and sanitize C++ source lines.

    Replaces string literals, character literals, raw string literals, and comments
    with spaces (preserving line length and original indices) while tracking active
    preprocessor conditional blocks (such as #if 0 ... #endif).

    Returns a list of SanitizedLine objects.
    """
    sanitized = []
    pp_stack = []

    in_block_comment = False
    in_raw_string = False
    raw_delimiter = ''

    def is_currently_active():
        return all(frame['branch_active'] for frame in pp_stack)

    raw_string_start_re = re.compile(r'^R"([a-zA-Z0-9_{}\[\]#<>%:;?*+\-/\^&|~!=,\.]*)\(')

    for line in lines:
        stripped = line.strip()

        # Handle preprocessor directives when outside block comments and raw strings
        if not in_block_comment and not in_raw_string and stripped.startswith('#'):
            m = re.match(r'^#\s*([a-zA-Z_]\w*)(.*)$', stripped)
            if m:
                directive = m.group(1)
                remainder = m.group(2).strip()

                if directive == 'if':
                    parent = is_currently_active()
                    if not parent:
                        pp_stack.append({'parent_active': False, 'branch_active': False, 'any_branch_taken': True})
                    else:
                        disabled = is_statically_false_condition(remainder)
                        active = not disabled
                        pp_stack.append({'parent_active': True, 'branch_active': active, 'any_branch_taken': active})
                elif directive in ('ifdef', 'ifndef'):
                    parent = is_currently_active()
                    if not parent:
                        pp_stack.append({'parent_active': False, 'branch_active': False, 'any_branch_taken': True})
                    else:
                        pp_stack.append({'parent_active': True, 'branch_active': True, 'any_branch_taken': True})
                elif directive == 'elif':
                    if pp_stack:
                        frame = pp_stack[-1]
                        if not frame['parent_active'] or frame['any_branch_taken']:
                            frame['branch_active'] = False
                        else:
                            disabled = is_statically_false_condition(remainder)
                            frame['branch_active'] = not disabled
                            if frame['branch_active']:
                                frame['any_branch_taken'] = True
                elif directive == 'else':
                    if pp_stack:
                        frame = pp_stack[-1]
                        if not frame['parent_active'] or frame['any_branch_taken']:
                            frame['branch_active'] = False
                        else:
                            frame['branch_active'] = True
                            frame['any_branch_taken'] = True
                elif directive == 'endif':
                    if pp_stack:
                        pp_stack.pop()

            sanitized.append(SanitizedLine(
                original=line,
                cleaned=' ' * len(line),
                is_active=is_currently_active(),
                is_pure_comment=False,
            ))
            continue

        active = is_currently_active()
        chars = list(line)
        cleaned_chars = []
        n = len(chars)
        i = 0
        in_string = False
        in_char = False
        string_escaped = False
        char_escaped = False
        has_non_comment_code = False

        while i < n:
            ch = chars[i]

            if in_block_comment:
                cleaned_chars.append(' ')
                if ch == '*' and i + 1 < n and chars[i + 1] == '/':
                    cleaned_chars.append(' ')
                    in_block_comment = False
                    i += 2
                    continue
                i += 1
                continue

            if in_raw_string:
                cleaned_chars.append(' ')
                target = ')' + raw_delimiter + '"'
                if line[i:i + len(target)] == target:
                    for _ in range(len(target) - 1):
                        cleaned_chars.append(' ')
                    in_raw_string = False
                    raw_delimiter = ''
                    i += len(target)
                    continue
                i += 1
                continue

            if in_string:
                cleaned_chars.append(' ')
                if string_escaped:
                    string_escaped = False
                elif ch == '\\':
                    string_escaped = True
                elif ch == '"':
                    in_string = False
                i += 1
                continue

            if in_char:
                cleaned_chars.append(' ')
                if char_escaped:
                    char_escaped = False
                elif ch == '\\':
                    char_escaped = True
                elif ch == "'":
                    in_char = False
                i += 1
                continue

            # In active normal code:
            # Check for line comment //
            if ch == '/' and i + 1 < n and chars[i + 1] == '/':
                cleaned_chars.extend([' '] * (n - i))
                break

            # Check for block comment start /*
            if ch == '/' and i + 1 < n and chars[i + 1] == '*':
                in_block_comment = True
                cleaned_chars.append(' ')
                cleaned_chars.append(' ')
                i += 2
                continue

            # Check for raw string R"..."
            if ch == 'R' and i + 1 < n and chars[i + 1] == '"':
                rem = line[i:]
                m_raw = raw_string_start_re.match(rem)
                if m_raw:
                    in_raw_string = True
                    raw_delimiter = m_raw.group(1)
                    matched_len = len(m_raw.group(0))
                    cleaned_chars.extend([' '] * matched_len)
                    has_non_comment_code = True
                    i += matched_len
                    continue

            # Check for normal string "
            if ch == '"':
                in_string = True
                string_escaped = False
                cleaned_chars.append(' ')
                has_non_comment_code = True
                i += 1
                continue

            # Check for character literal '
            if ch == "'":
                in_char = True
                char_escaped = False
                cleaned_chars.append(' ')
                has_non_comment_code = True
                i += 1
                continue

            if not ch.isspace():
                has_non_comment_code = True
            cleaned_chars.append(ch)
            i += 1

        cleaned_str = ''.join(cleaned_chars)
        is_pure_comment = not has_non_comment_code and is_comment_line(stripped)

        sanitized.append(SanitizedLine(
            original=line,
            cleaned=cleaned_str,
            is_active=active,
            is_pure_comment=is_pure_comment,
        ))

    return sanitized


# ---------------------------------------------------------------------------
# Rule implementations
# ---------------------------------------------------------------------------


def check_file_header(rel_path, lines):
    """Rule 1 - Verify a 2+ line comment header before includes/guards/imports."""
    violations = []
    first_idx = find_first_non_empty(lines)
    if first_idx is None:
        return violations  # Empty file — skip silently.

    first_stripped = lines[first_idx].strip()

    # The first non-empty line must be a comment.
    if not is_comment_line(first_stripped):
        violations.append(Violation(
            rel_path, first_idx + 1, 'file-header',
            "File must begin with a 2+ line comment block (/* ... */ or "
            "consecutive //) explaining subsystem purpose and ownership/ABI "
            "invariants, before includes or guards.",
        ))
        return violations

    # Count comment lines in the header block (blank lines inside are allowed).
    comment_count = 0
    idx = first_idx
    while idx < len(lines):
        stripped = lines[idx].strip()
        if is_comment_line(stripped):
            comment_count += 1
            idx += 1
        elif stripped == '':
            idx += 1  # Blank line inside / between header comment lines.
        else:
            break  # Non-comment, non-blank — header block ended.

    if comment_count < 2:
        violations.append(Violation(
            rel_path, first_idx + 1, 'file-header',
            f"File header must be at least 2 lines (found {comment_count}). "
            "Explain subsystem purpose and ownership/ABI invariants.",
        ))

    return violations


def check_vabi_docs(rel_path, lines):
    """Rule 2 - Verify doc comments on APS5_VABI function declarations/defs.

    Uses tokenize/sanitize pass to ignore inactive preprocessor blocks (e.g. #if 0)
    and string/character literals that mention APS5_VABI.
    """
    violations = []
    sanitized = sanitize_source_file(lines)

    for i, sline in enumerate(sanitized):
        if not sline.is_active:
            continue

        cleaned = sline.cleaned.strip()
        if not cleaned:
            continue

        if cleaned.startswith('#'):
            continue

        match = VABI_FUNC_RE.search(cleaned)
        if not match:
            continue

        if not has_doc_comment_before(lines, i, skip_attrs=True):
            func_name = extract_func_name(cleaned)
            violations.append(Violation(
                rel_path, i + 1, 'vabi-doc',
                f"Export '{func_name}' with APS5_VABI lacks a preceding doc "
                f"comment explaining role, parameters, and return/error codes.",
            ))

    return violations


def check_test_docs(rel_path, lines):
    """Rule 3 - Verify doc comments on GoogleTest TEST()/TEST_F() cases.

    Uses tokenize/sanitize pass to ignore inactive preprocessor blocks (e.g. #if 0)
    and string/character literals that mention TEST(...).
    """
    violations = []
    sanitized = sanitize_source_file(lines)

    for i, sline in enumerate(sanitized):
        if not sline.is_active:
            continue

        cleaned = sline.cleaned
        match = TEST_RE.match(cleaned)
        if not match:
            continue

        if not has_doc_comment_before(lines, i, skip_attrs=False):
            stripped_orig = lines[i].strip()
            violations.append(Violation(
                rel_path, i + 1, 'test-doc',
                f"{stripped_orig} is missing a doc comment explaining the "
                f"invariant or edge case being tested.",
            ))

    return violations


# ---------------------------------------------------------------------------
# File-level dispatch
# ---------------------------------------------------------------------------

#: Directories whose files should also have file headers (Rule 1 scope).
_RULE1_PREFIXES = tuple(RULE1_DIRS)


def _in_dir(rel_path, dir_name):
    """Return True if *rel_path* lives inside the top-level *dir_name* directory."""
    norm = normalize_path(rel_path)
    return norm == dir_name or norm.startswith(dir_name + '/')


def _is_exempt(rel_path):
    """Return True if the file path passes through an exempt directory."""
    parts = normalize_path(rel_path).split('/')
    return any(part in EXEMPT_DIRS for part in parts)


def check_file(rel_path, abs_path):
    """Run all applicable rules on a single source file.

    Parameters
    ----------
    rel_path : str
        Repository-relative path (forward-slash normalised).
    abs_path : str
        Absolute path for reading the file.

    Returns
    -------
    list[Violation]
    """
    violations = []
    try:
        lines = read_source_lines(abs_path)
    except (IOError, OSError) as exc:
        print(f"WARNING: could not read {rel_path}: {exc}", file=sys.stderr)
        return violations

    # Rule 1 — file-level header (core/, tests/, tools/ only, exempt dirs excluded).
    if any(_in_dir(rel_path, d) for d in RULE1_DIRS) and not _is_exempt(rel_path):
        violations.extend(check_file_header(rel_path, lines))

    # Rule 2 — APS5_VABI function docs (any non-exempt file in the tree).
    if not _is_exempt(rel_path):
        violations.extend(check_vabi_docs(rel_path, lines))

    # Rule 3 — TEST() / TEST_F() docs (tests/ only).
    if _in_dir(rel_path, TESTS_DIR):
        violations.extend(check_test_docs(rel_path, lines))

    return violations


# ---------------------------------------------------------------------------
# File discovery
# ---------------------------------------------------------------------------


def collect_source_files(root):
    """Walk *root* and return all source files (.cpp/.hpp), excluding exempt dirs."""
    results = []
    for dirpath, dirnames, filenames in os.walk(root):
        # Prune exempt directories in-place.
        dirnames[:] = [d for d in dirnames if d not in EXEMPT_DIRS]
        for fname in sorted(filenames):
            if fname.endswith(SOURCE_EXTENSIONS):
                results.append(os.path.join(dirpath, fname))
    return results


def get_changed_files(base_ref, repo_root):
    """Return the list of .cpp/.hpp files changed since *base_ref*.

    Checks commits on this branch relative to base (triple-dot diff),
    and also includes any uncommitted staged/working tree modifications.
    Returns None if git diff fails completely, or a list of existing source file paths.
    """
    files = set()

    # 1. Branch commits diff: base_ref...HEAD
    res = subprocess.run(
        ['git', '-C', repo_root, 'diff', '--name-only', f'{base_ref}...HEAD'],
        capture_output=True, text=True,
    )
    if res.returncode == 0:
        files.update(f.strip() for f in res.stdout.splitlines() if f.strip())
    else:
        # Fallback to direct diff against base_ref
        res = subprocess.run(
            ['git', '-C', repo_root, 'diff', '--name-only', base_ref],
            capture_output=True, text=True,
        )
        if res.returncode == 0:
            files.update(f.strip() for f in res.stdout.splitlines() if f.strip())
        else:
            print(f"ERROR: git diff against '{base_ref}' failed: {res.stderr.strip()}", file=sys.stderr)
            return None

    # 2. Also check any uncommitted changes (staged or in working directory)
    res_staged = subprocess.run(
        ['git', '-C', repo_root, 'diff', '--cached', '--name-only'],
        capture_output=True, text=True,
    )
    if res_staged.returncode == 0:
        files.update(f.strip() for f in res_staged.stdout.splitlines() if f.strip())

    res_wc = subprocess.run(
        ['git', '-C', repo_root, 'diff', '--name-only', 'HEAD'],
        capture_output=True, text=True,
    )
    if res_wc.returncode == 0:
        files.update(f.strip() for f in res_wc.stdout.splitlines() if f.strip())

    out = []
    for f in sorted(files):
        if f.endswith(SOURCE_EXTENSIONS) and not _is_exempt(f):
            full = os.path.join(repo_root, f)
            if os.path.isfile(full):
                out.append(full)
    return out


def get_default_base(repo_root):
    """Return the default git ref to diff against (origin/main, else main)."""
    for candidate in ('origin/main', 'main', 'HEAD~1'):
        try:
            result = subprocess.run(
                ['git', '-C', repo_root, 'rev-parse', '--verify', candidate],
                capture_output=True, text=True,
            )
            if result.returncode == 0:
                return candidate
        except (subprocess.CalledProcessError, FileNotFoundError):
            pass
    return 'origin/main'  # Best-guess fallback.


def get_files_to_check(args, repo_root):
    """Resolve the list of absolute file paths to check based on CLI args."""
    if args.paths:
        files = []
        for p in args.paths:
            abs_p = os.path.abspath(p)
            if os.path.isfile(abs_p):
                files.append(abs_p)
            elif os.path.isdir(abs_p):
                files.extend(collect_source_files(abs_p))
        # De-duplicate while preserving order.
        seen = set()
        unique = []
        for f in files:
            rp = normalize_path(os.path.relpath(f, repo_root))
            if rp not in seen and not _is_exempt(rp):
                seen.add(rp)
                unique.append(f)
        return unique

    if args.all:
        return collect_source_files(repo_root)

    # Default: PR scope via git diff.
    base = args.base or get_default_base(repo_root)
    changed = get_changed_files(base, repo_root)
    return changed


# ---------------------------------------------------------------------------
# Output helpers
# ---------------------------------------------------------------------------


def format_violation(v, github_format=False):
    """Format a single violation as an output line."""
    if github_format:
        # GitHub Actions annotation: ::error file=<path>,line=<line>::<message>
        msg = v.message.replace('\n', ' ').replace('\r', '')
        return f"::error file={v.file},line={v.line}::{msg}"
    return f"ERROR: {v.file}:{v.line}: {v.message}"


# ---------------------------------------------------------------------------
# CLI entry point
# ---------------------------------------------------------------------------


def main(argv=None):
    parser = argparse.ArgumentParser(
        description='PortPS5 code comment and best-practice linter.',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog='Exit 0 = all pass; 1 = violations found; 2 = tool error.',
    )
    parser.add_argument(
        '--base',
        default=None,
        help='Git ref to diff against for PR scope (default: origin/main or main).',
    )
    parser.add_argument(
        '--all',
        action='store_true',
        help='Audit all .cpp/.hpp files in the repository.',
    )
    parser.add_argument(
        '--paths',
        nargs='*',
        default=None,
        metavar='DIR',
        help='Only check specific files or directories.',
    )
    args = parser.parse_args(argv)

    # Determine repository root (parent of the tools/ directory).
    repo_root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

    files = get_files_to_check(args, repo_root)

    if files is None:
        return EXIT_ERROR

    if not files:
        if args.paths:
            print("No source files found in specified paths.")
        elif args.all:
            print("No source files found.")
        else:
            base = args.base or get_default_base(repo_root)
            print(f"OK: No changed C++ source files to check against base '{base}'.")
        return EXIT_OK

    all_violations = []
    for f in sorted(files):
        rel = normalize_path(os.path.relpath(f, repo_root))
        all_violations.extend(check_file(rel, f))

    # Sort by file path then line number for stable output.
    all_violations.sort(key=lambda v: (v.file, v.line))

    github_format = os.environ.get('GITHUB_ACTIONS', '') == 'true'

    if all_violations:
        for v in all_violations:
            print(format_violation(v, github_format=github_format))
        print(
            f"\nFAIL: {len(all_violations)} documentation violations found. "
            f"Please document public interfaces, tests, and file headers."
        )
        return EXIT_VIOLATION

    print(f"OK: All {len(files)} file(s) pass documentation checks.")
    return EXIT_OK


if __name__ == '__main__':
    sys.exit(main())
