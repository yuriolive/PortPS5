#!/usr/bin/env python3
"""Unit tests for the PortPS5 comment policy linter (tools/check_comments.py).

Tests cover:
  - Rule 1: file-level purpose header.
  - Rule 2: APS5_VABI function documentation.
  - Rule 3: GoogleTest TEST()/TEST_F() doc comments.
  - Helper functions and regex patterns.
  - check_file() dispatch logic.
"""

import os
import sys
import tempfile
import unittest
from unittest import mock

# Add tools/ to the Python path so we can import check_comments.
_TOOLS_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools")
sys.path.insert(0, _TOOLS_DIR)

from check_comments import (  # noqa: E402
    TEST_RE,
    VABI_FUNC_RE,
    check_file,
    check_file_header,
    check_test_docs,
    check_vabi_docs,
    extract_func_name,
    get_changed_files,
    get_default_base,
    has_doc_comment_before,
    is_attribute_line,
    is_comment_line,
    is_statically_false_condition,
    is_statically_true_condition,
    read_source_lines,
)


class TestIsCommentLine(unittest.TestCase):
    """Tests for is_comment_line helper."""

    def test_line_comment(self):
        self.assertTrue(is_comment_line("// hello"))

    def test_doxygen_line_comment(self):
        self.assertTrue(is_comment_line("/// hello"))

    def test_block_comment_start(self):
        self.assertTrue(is_comment_line("/* hello"))

    def test_block_comment_middle(self):
        self.assertTrue(is_comment_line(" * hello"))

    def test_block_comment_end(self):
        self.assertTrue(is_comment_line(" */"))

    def test_not_a_comment(self):
        self.assertFalse(is_comment_line("int x;"))

    def test_pointer_dereference_not_comment(self):
        self.assertFalse(is_comment_line("*ptr = value;"))
        self.assertFalse(is_comment_line("*ptr += 1;"))
        self.assertFalse(is_comment_line("*(ptr++) = 5;"))

    def test_empty_line(self):
        self.assertFalse(is_comment_line(""))
        self.assertFalse(is_comment_line("   "))


class TestIsAttributeLine(unittest.TestCase):
    """Tests for is_attribute_line helper."""

    def test_noreturn(self):
        self.assertTrue(is_attribute_line("[[noreturn]]"))

    def test_nodiscard(self):
        self.assertTrue(is_attribute_line("[[nodiscard]]"))

    def test_deprecated(self):
        self.assertTrue(is_attribute_line('[[deprecated("use new_func")]]'))

    def test_not_attribute(self):
        self.assertFalse(is_attribute_line("int APS5_VABI foo();"))


class TestHasDocCommentBefore(unittest.TestCase):
    """Tests for has_doc_comment_before helper."""

    def test_comment_immediately_before(self):
        lines = ["// doc\n", "int APS5_VABI foo();\n"]
        self.assertTrue(has_doc_comment_before(lines, 1))

    def test_blank_line_between(self):
        lines = ["// doc\n", "\n", "int APS5_VABI foo();\n"]
        self.assertTrue(has_doc_comment_before(lines, 2))

    def test_attribute_between(self):
        lines = ["// doc\n", "[[noreturn]]\n", "void APS5_VABI foo();\n"]
        self.assertTrue(has_doc_comment_before(lines, 2, skip_attrs=True))

    def test_no_doc_comment(self):
        lines = ["int x = 0;\n", "int APS5_VABI foo();\n"]
        self.assertFalse(has_doc_comment_before(lines, 1))

    def test_block_comment_before(self):
        lines = ["/** Doc */\n", "int APS5_VABI foo();\n"]
        self.assertTrue(has_doc_comment_before(lines, 1))

    def test_block_comment_closing_line_without_leading_asterisk(self):
        lines = ["/**\n", "role and parameters */\n", "int APS5_VABI foo();\n"]
        self.assertTrue(has_doc_comment_before(lines, 2))

    def test_at_file_start(self):
        lines = ["int APS5_VABI foo();\n"]
        self.assertFalse(has_doc_comment_before(lines, 0))


class TestRule1FileHeader(unittest.TestCase):
    """Tests for Rule 1: file-level purpose header."""

    def test_valid_two_line_header(self):
        lines = [
            "// PortPS5 libc: Export.cpp\n",
            "// ABI: guest exports use APS5_VABI (System V).\n",
            "#include <cstdint>\n",
        ]
        self.assertEqual(check_file_header("core/foo.cpp", lines), [])

    def test_valid_block_comment_header(self):
        lines = [
            "/*\n",
            " * PortPS5 module file.\n",
            " * ABI: System V for guest exports.\n",
            " */\n",
            "#pragma once\n",
        ]
        self.assertEqual(check_file_header("core/foo.hpp", lines), [])

    def test_missing_header(self):
        lines = ["#include <cstdint>\n", "int foo();\n"]
        violations = check_file_header("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].rule, "file-header")
        self.assertEqual(violations[0].line, 1)

    def test_single_line_header(self):
        lines = ["// Single line header.\n", "#include <cstdint>\n"]
        violations = check_file_header("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertIn("at least 2 lines", violations[0].message)

    def test_empty_file(self):
        self.assertEqual(check_file_header("core/foo.cpp", []), [])

    def test_header_after_blank_lines(self):
        lines = ["\n", "\n", "// Line 1\n", "// Line 2\n", "#include <cstdint>\n"]
        self.assertEqual(check_file_header("core/foo.cpp", lines), [])

    def test_single_line_doxygen_header(self):
        lines = ["/** Brief doc. */\n", "#include <cstdint>\n"]
        violations = check_file_header("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)


class TestRule2VabiDocs(unittest.TestCase):
    """Tests for Rule 2: APS5_VABI function documentation."""

    def test_function_with_line_comment(self):
        lines = ["// Doc for foo.\n", "int APS5_VABI foo(int x);\n"]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_function_with_doxygen_block(self):
        lines = ["/** Doc for foo. */\n", "int APS5_VABI foo(int x);\n"]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_function_with_doxygen_slash(self):
        lines = ["/// Doc for foo.\n", "int APS5_VABI foo(int x);\n"]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_function_without_doc(self):
        lines = ["int APS5_VABI foo(int x);\n"]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].rule, "vabi-doc")
        self.assertEqual(violations[0].line, 1)

    def test_function_with_multi_line_doc(self):
        lines = [
            "// First line of doc.\n",
            "// Second line of doc.\n",
            "int APS5_VABI foo(int x);\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_type_alias_not_checked(self):
        lines = ["using Foo = void (APS5_VABI *)(int);\n"]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_define_not_checked(self):
        lines = ["#define APS5_VABI __attribute__((sysv_abi))\n"]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_attribute_before_function_without_doc(self):
        lines = ["[[noreturn]]\n", "void APS5_VABI foo();\n"]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 2)

    def test_attribute_before_function_with_doc(self):
        lines = [
            "// Doc for foo.\n",
            "[[noreturn]]\n",
            "void APS5_VABI foo();\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_attribute_on_same_line_as_function(self):
        lines = ["[[noreturn]] void APS5_VABI foo();\n"]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 1)

    def test_attribute_on_same_line_with_doc(self):
        lines = [
            "// Doc for foo.\n",
            "[[noreturn]] void APS5_VABI foo();\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_function_inside_extern_c(self):
        lines = [
            'extern "C" {\n',
            "\n",
            "// Doc for foo.\n",
            "int APS5_VABI foo(int x);\n",
            "\n",
            "int APS5_VABI bar(int y);\n",
            "}\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 6)
        self.assertIn("bar", violations[0].message)

    def test_multi_line_signature(self):
        lines = [
            "// Doc for foo.\n",
            "int APS5_VABI foo(\n",
            "    int a,\n",
            "    int b\n",
            ");\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_multi_line_signature_without_doc(self):
        lines = [
            "int APS5_VABI foo(\n",
            "    int a,\n",
            "    int b\n",
            ");\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 1)

    def test_noreturn_prefix(self):
        lines = ["void* APS5_VABI __cxa_allocate_exception(std::size_t);\n"]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 1)

    def test_multiple_functions_documented(self):
        lines = [
            "// Doc for foo.\n",
            "int APS5_VABI foo(int x);\n",
            "// Doc for bar.\n",
            "int APS5_VABI bar(int y);\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_one_documented_one_not(self):
        lines = [
            "// Doc for foo.\n",
            "int APS5_VABI foo(int x);\n",
            "int APS5_VABI bar(int y);\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 3)
        self.assertIn("bar", violations[0].message)

    def test_aps5_vabi_in_comment_not_flagged(self):
        lines = [
            "// Note: APS5_VABI foo() is defined elsewhere.\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 0)

    def test_function_inside_namespace(self):
        lines = [
            "namespace Foo {\n",
            "// Doc for bar.\n",
            "int APS5_VABI bar();\n",
            "}\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])


class TestRule3TestDocs(unittest.TestCase):
    """Tests for Rule 3: GoogleTest TEST()/TEST_F() documentation."""

    def test_test_with_comment(self):
        lines = ["// Verifies foo behavior.\n", "TEST(Foo, Bar) {\n", "}\n"]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_test_without_comment(self):
        lines = ["TEST(Foo, Bar) {\n", "}\n"]
        violations = check_test_docs("tests/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].rule, "test-doc")
        self.assertEqual(violations[0].line, 1)

    def test_test_f_with_comment(self):
        lines = ["// Doc.\n", "TEST_F(Fixture, Bar) {\n", "}\n"]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_test_f_without_comment(self):
        lines = ["TEST_F(Fixture, Bar) {\n", "}\n"]
        violations = check_test_docs("tests/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertIn("TEST_F", violations[0].message)

    def test_test_with_blank_line_before_comment(self):
        lines = ["\n", "// Doc.\n", "TEST(Foo, Bar) {\n", "}\n"]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_indented_test(self):
        lines = ["  TEST(Foo, Bar) {\n"]
        violations = check_test_docs("tests/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 1)

    def test_not_a_test(self):
        lines = ["TESTSUITE(Foo, Bar);\n"]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_doxygen_block_before_test(self):
        lines = ["/** Doc. */\n", "TEST(Foo, Bar) {\n", "}\n"]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_correct_line_number(self):
        lines = [
            "// setup\n",
            "TEST(Foo, Bar) {\n",
            "}\n",
            "TEST(Foo, Baz) {\n",
            "}\n",
        ]
        violations = check_test_docs("tests/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 4)

    def test_all_documented(self):
        lines = [
            "// First test doc.\n",
            "TEST(Foo, Bar) {\n",
            "}\n",
            "// Second test doc.\n",
            "TEST_F(Fixture, Baz) {\n",
            "}\n",
        ]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])


class TestCheckFileDispatch(unittest.TestCase):
    """Tests for check_file() rule dispatch based on path."""

    def test_core_file_applies_all_rules(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", delete=False) as f:
            f.write("int APS5_VABI foo(int x);\n")
            temp_path = f.name
        try:
            violations = check_file("core/sub/foo.cpp", temp_path)
            rules = {v.rule for v in violations}
            self.assertIn("file-header", rules)
            self.assertIn("vabi-doc", rules)
            self.assertNotIn("test-doc", rules)
        finally:
            os.unlink(temp_path)

    def test_tests_file_applies_all_rules(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", delete=False) as f:
            f.write("TEST(Foo, Bar) {\n}\n")
            temp_path = f.name
        try:
            violations = check_file("tests/foo.cpp", temp_path)
            rules = {v.rule for v in violations}
            self.assertIn("file-header", rules)
            self.assertIn("test-doc", rules)
            self.assertNotIn("vabi-doc", rules)
        finally:
            os.unlink(temp_path)

    def test_config_file_skips_rule1_and_rule3(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", delete=False) as f:
            f.write("int APS5_VABI foo(int x);\n")
            temp_path = f.name
        try:
            violations = check_file("config/foo.cpp", temp_path)
            rules = {v.rule for v in violations}
            self.assertNotIn("file-header", rules)
            self.assertIn("vabi-doc", rules)
            self.assertNotIn("test-doc", rules)
        finally:
            os.unlink(temp_path)

    def test_exempt_directory_skips_all(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", delete=False) as f:
            f.write("#include <foo>\nint APS5_VABI bar();\n")
            temp_path = f.name
        try:
            violations = check_file("3rdparty/foo/bar.cpp", temp_path)
            self.assertEqual(violations, [])
        finally:
            os.unlink(temp_path)

    def test_compliant_file_passes(self):
        with tempfile.NamedTemporaryFile(mode="w", suffix=".cpp", delete=False) as f:
            f.write(
                "// PortPS5 module: Foo.cpp\n"
                "// ABI: guest exports use APS5_VABI (System V).\n"
                "// Doc for foo.\n"
                "int APS5_VABI foo(int x);\n"
            )
            temp_path = f.name
        try:
            self.assertEqual(check_file("core/foo.cpp", temp_path), [])
        finally:
            os.unlink(temp_path)


class TestExtractFuncName(unittest.TestCase):
    """Tests for extract_func_name helper."""

    def test_simple(self):
        line = "int APS5_VABI sceFoo(int x);"
        self.assertEqual(extract_func_name(line), "sceFoo")

    def test_with_attribute(self):
        line = "[[noreturn]] void APS5_VABI __cxa_throw(void*);"
        self.assertEqual(extract_func_name(line), "__cxa_throw")

    def test_with_pointer_return(self):
        line = "void* APS5_VABI __cxa_allocate_exception(std::size_t);"
        self.assertEqual(extract_func_name(line), "__cxa_allocate_exception")


class TestRegexPatterns(unittest.TestCase):
    """Tests for the regex patterns used to detect functions and tests."""

    def test_vabi_function_decl(self):
        self.assertIsNotNone(VABI_FUNC_RE.search("int APS5_VABI foo(int x);"))

    def test_vabi_function_def(self):
        self.assertIsNotNone(VABI_FUNC_RE.search("int APS5_VABI foo(int x) {}"))

    def test_vabi_type_alias_not_matched(self):
        self.assertIsNone(VABI_FUNC_RE.search("using Foo = void (APS5_VABI *)(int);"))

    def test_vabi_define_not_matched(self):
        # APS5_VABI as a macro name at end-of-line: no identifier or paren after.
        self.assertIsNone(VABI_FUNC_RE.search("#define FOO APS5_VABI\n"))

    def test_vabi_with_noreturn_prefix(self):
        self.assertIsNotNone(VABI_FUNC_RE.search("[[noreturn]] void APS5_VABI foo();"))

    def test_test_match(self):
        self.assertIsNotNone(TEST_RE.match("TEST(Foo, Bar) {"))

    def test_test_f_match(self):
        self.assertIsNotNone(TEST_RE.match("TEST_F(Fixture, Bar) {"))

    def test_test_indented(self):
        self.assertIsNotNone(TEST_RE.match("  TEST(Foo, Bar) {"))

    def test_test_not_match(self):
        self.assertIsNone(TEST_RE.match("TESTSUITE(Foo, Bar);"))
        self.assertIsNone(TEST_RE.match("// TEST(Foo, Bar)"))


class TestPreprocessorAndLiterals(unittest.TestCase):
    """Tests for preprocessor conditional tracking and string/comment literal sanitization."""

    def test_vabi_in_string_literal_ignored(self):
        lines = [
            'const char* decl = "int APS5_VABI false_func(int x);";\n',
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_test_in_string_literal_ignored(self):
        lines = [
            "void log_event() {\n",
            '    LOG_INFO("TEST(FakeSuite, FakeCase) executed");\n',
            "}\n",
        ]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_vabi_in_multiline_block_comment_ignored(self):
        lines = [
            "/*\n",
            " * Legacy signature: int APS5_VABI deprecated_func();\n",
            " */\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_test_in_multiline_block_comment_ignored(self):
        lines = [
            "/*\n",
            " * TEST(OldSuite, OldCase) {\n",
            " * }\n",
            " */\n",
        ]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_vabi_in_raw_string_literal_ignored(self):
        lines = [
            'const char* code = R"(\n',
            "    int APS5_VABI raw_shader_export();\n",
            ')";\n',
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_vabi_in_if_0_ignored(self):
        lines = [
            "#if 0\n",
            "int APS5_VABI disabled_func();\n",
            "#endif\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_test_in_if_0_ignored(self):
        lines = [
            "#if 0\n",
            "TEST(DisabledSuite, DisabledCase) {\n",
            "}\n",
            "#endif\n",
        ]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_vabi_in_if_0_else_active(self):
        lines = [
            "#if 0\n",
            "int APS5_VABI disabled();\n",
            "#else\n",
            "int APS5_VABI enabled();\n",
            "#endif\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 4)
        self.assertIn("enabled", violations[0].message)

    def test_test_in_if_0_else_active(self):
        lines = [
            "#if 0\n",
            "TEST(Disabled, Case) {}\n",
            "#else\n",
            "TEST(Enabled, Case) {}\n",
            "#endif\n",
        ]
        violations = check_test_docs("tests/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 4)
        self.assertIn("Enabled", violations[0].message)

    def test_nested_if_0_ignored(self):
        lines = [
            "#if 0\n",
            "#if 1\n",
            "int APS5_VABI nested();\n",
            "#endif\n",
            "#endif\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_char_literal_with_quote(self):
        lines = [
            "char q = '\"';\n",
            "int APS5_VABI foo();\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 2)

    def test_preprocessor_or_condition_not_statically_false(self):
        # Conditions with || (e.g. 0 || ENABLE_EXPORTS) are not statically false;
        # declarations inside must be checked.
        lines = [
            "#if 0 || ENABLE_EXPORTS\n",
            "int APS5_VABI export_func();\n",
            "#endif\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 2)
        self.assertIn("export_func", violations[0].message)

    def test_read_source_lines_strips_utf8_bom(self):
        with tempfile.NamedTemporaryFile(mode="wb", suffix=".cpp", delete=False) as f:
            # UTF-8 with BOM: \xef\xbb\xbf
            f.write(
                b"\xef\xbb\xbf// First header line\n// Second header line\n#include <cstdint>\n"
            )
            temp_path = f.name
        try:
            lines = read_source_lines(temp_path)
            self.assertTrue(lines[0].startswith("//"))
            violations = check_file_header("core/foo.cpp", lines)
            self.assertEqual(violations, [])
        finally:
            os.unlink(temp_path)

    def test_multiline_block_header_without_asterisks(self):
        lines = [
            "/*\n",
            " Purpose of subsystem without leading asterisks\n",
            " Second line of subsystem description\n",
            " */\n",
            "#pragma once\n",
        ]
        self.assertEqual(check_file_header("core/foo.hpp", lines), [])

    def test_string_literal_line_continuation_ignored(self):
        lines = [
            'const char* code = "first part \\\n',
            "APS5_VABI fake_export(); \\\n",
            'third part";\n',
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_test_literal_line_continuation_ignored(self):
        lines = [
            'const char* log = "run test: \\\n',
            "TEST(FakeSuite, FakeCase) \\\n",
            'done";\n',
        ]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_preprocessor_line_continuation_if_0_ignored(self):
        lines = [
            "#if 0 \\\n",
            "    && defined(OBSOLETE)\n",
            "int APS5_VABI disabled_export();\n",
            "#endif\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_preprocessor_line_continuation_if_0_alone_ignored(self):
        lines = [
            "#if 0 \\\n",
            "\n",
            "int APS5_VABI disabled_export();\n",
            "#endif\n",
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])

    def test_statically_false_condition_variants(self):
        self.assertTrue(is_statically_false_condition("( 0 )"))
        self.assertTrue(is_statically_false_condition("((0))"))
        self.assertTrue(is_statically_false_condition("0U"))
        self.assertTrue(is_statically_false_condition("(0L)"))
        self.assertTrue(is_statically_false_condition("0UL"))
        self.assertTrue(is_statically_false_condition("( 0u )"))
        self.assertFalse(is_statically_false_condition("1"))
        self.assertFalse(is_statically_false_condition("0 || 1"))

    def test_statically_true_condition_variants(self):
        self.assertTrue(is_statically_true_condition("1"))
        self.assertTrue(is_statically_true_condition("( 1 )"))
        self.assertTrue(is_statically_true_condition("1U"))
        self.assertTrue(is_statically_true_condition("true"))
        self.assertFalse(is_statically_true_condition("0"))
        self.assertFalse(is_statically_true_condition("SOME_MACRO"))

    def test_test_macro_args_on_next_line(self):
        lines = [
            "TEST\n",
            "    (SuiteName, CaseName) {\n",
            "}\n",
        ]
        violations = check_test_docs("tests/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertEqual(violations[0].line, 1)

    def test_test_f_macro_args_on_next_line_with_comment(self):
        lines = [
            "// Verifies feature works across line breaks.\n",
            "TEST_F\n",
            "    (FixtureName, CaseName) {\n",
            "}\n",
        ]
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_non_literal_if_else_both_branches_linted(self):
        lines = [
            "#if FEATURE_X\n",
            "int APS5_VABI branch1();\n",
            "#else\n",
            "int APS5_VABI branch2();\n",
            "#endif\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 2)
        func_names = [v.message for v in violations]
        self.assertTrue(any("branch1" in m for m in func_names))
        self.assertTrue(any("branch2" in m for m in func_names))

    def test_literal_if_1_else_only_true_branch_linted(self):
        lines = [
            "#if 1\n",
            "int APS5_VABI enabled();\n",
            "#else\n",
            "int APS5_VABI disabled();\n",
            "#endif\n",
        ]
        violations = check_vabi_docs("core/foo.cpp", lines)
        self.assertEqual(len(violations), 1)
        self.assertIn("enabled", violations[0].message)

    def test_raw_string_literal_with_at_delimiter(self):
        lines = [
            'const char* code = R"@(\n',
            "int APS5_VABI not_real_func();\n",
            "TEST(NotRealSuite, NotRealCase) {}\n",
            ')@";\n',
        ]
        self.assertEqual(check_vabi_docs("core/foo.cpp", lines), [])
        self.assertEqual(check_test_docs("tests/foo.cpp", lines), [])

    def test_check_file_raises_on_unreadable_file(self):
        with self.assertRaises((IOError, OSError)):
            check_file("core/non_existent_file.cpp", "core/non_existent_file.cpp")


def _completed(returncode=0, stdout="", stderr=""):
    proc = mock.Mock()
    proc.returncode = returncode
    proc.stdout = stdout
    proc.stderr = stderr
    return proc


class TestGetChangedFiles(unittest.TestCase):
    """Tests for get_changed_files git-diff collection and fallbacks."""

    def _run(self, outputs, repo_root="/repo", files=None):
        with (
            mock.patch("check_comments.subprocess.run", side_effect=outputs),
            mock.patch(
                "check_comments.os.path.isfile", side_effect=lambda p: files is None or p in files
            ),
        ):
            return get_changed_files("origin/main", repo_root)

    def test_branch_diff_lists_source_files(self):
        out = self._run(
            [_completed(0, "core/a.cpp\nREADME.md\n"), _completed(0, ""), _completed(0, "")],
            files={os.path.join("/repo", "core/a.cpp")},
        )
        self.assertEqual(out, [os.path.join("/repo", "core/a.cpp")])

    def test_branch_diff_failure_falls_back_to_direct_diff(self):
        out = self._run(
            [
                _completed(1, "", "bad revision"),
                _completed(0, "core/b.hpp\n"),
                _completed(0, ""),
                _completed(0, ""),
            ],
            files={os.path.join("/repo", "core/b.hpp")},
        )
        self.assertEqual(out, [os.path.join("/repo", "core/b.hpp")])

    def test_total_git_failure_returns_none(self):
        with mock.patch("check_comments.subprocess.run", return_value=_completed(1, "", "no git")):
            self.assertIsNone(get_changed_files("origin/main", "/repo"))

    def test_staged_and_working_tree_changes_merge(self):
        out = self._run(
            [
                _completed(0, "core/a.cpp\n"),
                _completed(0, "core/staged.cpp\n"),
                _completed(0, "core/working.hpp\n"),
            ],
            files={
                os.path.join("/repo", "core/a.cpp"),
                os.path.join("/repo", "core/staged.cpp"),
                os.path.join("/repo", "core/working.hpp"),
            },
        )
        self.assertEqual(
            out,
            [
                os.path.join("/repo", f)
                for f in ("core/a.cpp", "core/staged.cpp", "core/working.hpp")
            ],
        )

    def test_nonexistent_paths_are_dropped(self):
        out = self._run(
            [_completed(0, "core/gone.cpp\n"), _completed(0, ""), _completed(0, "")], files=set()
        )
        self.assertEqual(out, [])


class TestGetDefaultBase(unittest.TestCase):
    """Tests for get_default_base ref probing."""

    def test_prefers_origin_main(self):
        with mock.patch(
            "check_comments.subprocess.run", return_value=_completed(0, "abc123\n")
        ) as run:
            self.assertEqual(get_default_base("/repo"), "origin/main")
            self.assertEqual(run.call_count, 1)

    def test_falls_back_to_main(self):
        with mock.patch(
            "check_comments.subprocess.run",
            side_effect=[_completed(1, "", "unknown revision"), _completed(0, "def456\n")],
        ):
            self.assertEqual(get_default_base("/repo"), "main")

    def test_returns_none_without_git(self):
        with mock.patch("check_comments.subprocess.run", side_effect=FileNotFoundError("no git")):
            self.assertIsNone(get_default_base("/repo"))


class TestMain(unittest.TestCase):
    """Tests for main() exit codes and output paths."""

    def _main(self, argv, files=None, violations=None, read_error=None):
        from check_comments import main

        if files is None:
            files = ["core/a.cpp"]

        def fake_get_files(args, root):
            return list(files)

        def fake_check(rel, full):
            if read_error is not None:
                raise read_error
            return violations or []

        with (
            mock.patch("check_comments.get_files_to_check", side_effect=fake_get_files),
            mock.patch("check_comments.check_file", side_effect=fake_check),
        ):
            return main(argv)

    def test_clean_tree_returns_ok(self):
        self.assertEqual(self._main([]), 0)

    def test_violations_return_violation_code(self):
        from check_comments import EXIT_VIOLATION, Violation

        bad = [Violation("core/a.cpp", 3, "file-header", "missing header")]
        self.assertEqual(self._main([], violations=bad), EXIT_VIOLATION)

    def test_git_failure_returns_error(self):
        from check_comments import EXIT_ERROR, main

        with mock.patch("check_comments.get_files_to_check", return_value=None):
            self.assertEqual(main([]), EXIT_ERROR)

    def test_unreadable_file_returns_error(self):
        from check_comments import EXIT_ERROR

        self.assertEqual(self._main([], read_error=OSError("denied")), EXIT_ERROR)

    def test_empty_paths_reports_no_files(self):
        self.assertEqual(self._main(["--paths", "empty-dir"]), 0)


if __name__ == "__main__":
    unittest.main(verbosity=2)
