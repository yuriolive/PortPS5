// GoogleTest suite for wide-character formatting/output and the scanf family:
// vswprintf, wprintf, fputwc, fputws (core/libs/prx/libc/src/FormattingWide.cpp) and fscanf / sscanf
// (Formatting.cpp, include/ScanfArguments.hpp).
//
// Ported from AnyPS5 940f28d1, 77194e45 and 85a0ec2e and converted to GoogleTest. The upstream fscanf test was
// disabled on Windows because the host vfscanf can not consume a System V va_list; here fscanf is
// exercised on every platform, including the LP64 `long` (8 bytes) behaviour that the Windows host (4-byte
// long) would otherwise get wrong.
//
// Variadic calls into System V exports are made through small APS5_VABI helpers that build the System V
// va_list exactly like guest code would.
#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <gtest/gtest.h>

#include <cstdarg>
#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <algorithm>
#include <cstring>
#include <iterator>
#include <limits>
#include <string>

extern "C" {
// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
int* APS5_VABI __error_nid_postfix();
// Declaration of `vswprintf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI vswprintf_nid_postfix(char16_t*, std::size_t, const char16_t*, void*);
// Declaration of `wprintf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI wprintf_nid_postfix(const char16_t*, ...);
// Declaration of `fputwc_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fputwc_nid_postfix(char16_t, FileStream*);
// Declaration of `fputws_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fputws_nid_postfix(const char16_t*, FileStream*);
// Declaration of `fscanf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fscanf_nid_postfix(FileStream*, const char*, ...);
// Declaration of `sscanf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sscanf_nid_postfix(const char*, const char*, ...);
// Declaration of `fprintf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fprintf_nid_postfix(FileStream*, const char*, ...);
}

namespace {

constexpr int Einval = 22;

// Builds a System V va_list from the variadic arguments and forwards to vswprintf.
int APS5_VABI FormatWide(char16_t* buffer, std::size_t size, const char16_t* format, ...) {
#ifdef _WIN32
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
#else
    std::va_list args;
    va_start(args, format);
#endif
    const int result = vswprintf_nid_postfix(buffer, size, format, args);
#ifdef _WIN32
    __builtin_sysv_va_end(args);
#else
    va_end(args);
#endif
    return result;
}

// Rewinds a tmpfile-backed stream and reads back everything written so far.
std::string ReadAll(FileStream& stream) {
    std::fflush(stream.GetHandle());
    std::rewind(stream.GetHandle());
    std::string bytes;
    char chunk[64];
    std::size_t count;
    while ((count = std::fread(chunk, 1, sizeof(chunk), stream.GetHandle())) > 0) bytes.append(chunk, count);
    return bytes;
}

}  // namespace

// Invariant: vswprintf handles integer/float/char/string conversions with POSIX semantics (%s narrow UTF-8,
// %ls wide), flags, width, precision and star arguments, and returns the UTF-16 length.
TEST(WideFormatting, ConversionsProduceExpectedText) {
    char16_t out[128]{};
    const char16_t* wide = u"wide";
    const int count = FormatWide(out, 128, u"%d|%s|%ls|%6.2f|%c|%x|%-4d|%*d|%%", -42, "\xC3\xA9t\xC3\xA9", wide, 3.14159, 'Z', 255u, 7, 5, 9);
    const std::u16string expected = u"-42|été|wide|  3.14|Z|ff|7   |    9|%";
    EXPECT_EQ(out, expected);
    EXPECT_EQ(static_cast<std::size_t>(count), expected.size());
}

// Invariant: 64-bit conversions (%lld/%llx/%ld/%zu) read eight-byte arguments; %hhd/%hd truncate like C.
TEST(WideFormatting, IntegerLengthModifiers) {
    char16_t out[128]{};
    EXPECT_EQ(FormatWide(out, 128, u"%lld %llx %ld %zu %hhd %hd", std::int64_t{-5000000000}, std::uint64_t{0x1ffffffffULL}, std::int64_t{4294967297}, std::size_t{8}, 300, 70000), 42);
    EXPECT_EQ(out, std::u16string(u"-5000000000 1ffffffff 4294967297 8 44 4464"));
}

// Invariant: a truncated result returns -1 but the buffer is still terminated and holds the prefix; a null
// or zero-sized buffer is rejected with EINVAL.
TEST(WideFormatting, TruncationAndBadBuffers) {
    char16_t small[5]{};
    EXPECT_EQ(FormatWide(small, 5, u"%s", "abcdefgh"), -1);
    EXPECT_EQ(std::u16string(small), u"abcd");
    *__error_nid_postfix() = 0;
    EXPECT_EQ(FormatWide(nullptr, 5, u"x"), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(FormatWide(small, 0, u"x"), -1);
}

// Invariant: malformed or unsupported conversions (%n would write through a guest pointer; incomplete
// specs; bad lengths) fail with -1/EINVAL and leave an empty string, never an exception or a write.
TEST(WideFormatting, RejectsUnsupportedConversions) {
    char16_t out[16] = u"junk";
    int sink = 7;
    *__error_nid_postfix() = 0;
    EXPECT_EQ(FormatWide(out, 16, u"abc%n", &sink), -1);
    EXPECT_EQ(sink, 7);
    EXPECT_EQ(out[0], 0);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(FormatWide(out, 16, u"%", 0), -1);
    EXPECT_EQ(FormatWide(out, 16, u"%Ld", 1), -1);   // L is not an integer length
    EXPECT_EQ(FormatWide(out, 16, u"%q", 1), -1);
}

// Invariant (overflow regression): absurd star/literal widths are rejected with -1 instead of overflowing int (negating
// INT_MIN is undefined behaviour in the upstream version) or blowing the host snprintf stack (crashed MinGW).
TEST(WideFormatting, HugeStarWidthDoesNotOverflow) {
    char16_t out[8]{};
    EXPECT_EQ(FormatWide(out, 8, u"%*d", std::numeric_limits<int>::min(), 1), -1);
    EXPECT_EQ(FormatWide(out, 8, u"%*d", std::numeric_limits<int>::max(), 1), -1);
    EXPECT_EQ(FormatWide(out, 8, u"%99999999999999999999d", 1), -1);
    EXPECT_EQ(out[7], 0);  // terminated
}

// Invariant: wprintf returns the number of UTF-16 units formatted (output goes to stdout as UTF-8).
TEST(WideFormatting, WprintfReturnsLength) {
    EXPECT_EQ(wprintf_nid_postfix(u"wprintf:%d\n", 12), 11);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(wprintf_nid_postfix(u"%", 1), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
}

// Invariant: fputwc/fputws encode UTF-16 as UTF-8 (BMP, surrogate pairs, lone surrogate -> U+FFFD), return
// the unit / unit count, and fail with -1/EINVAL on null arguments.
TEST(WideStreams, FputwcFputwsEncodeUtf8) {
    FileStream stream(std::tmpfile());
    EXPECT_EQ(fputwc_nid_postfix(u'A', &stream), u'A');
    EXPECT_EQ(fputws_nid_postfix(u"B\x00E9", &stream), 2);
    EXPECT_EQ(fputws_nid_postfix(u"\xD83D\xDE00", &stream), 2);  // U+1F600 as a surrogate pair
    EXPECT_EQ(fputwc_nid_postfix(static_cast<char16_t>(0xD800), &stream), 0xD800);  // lone surrogate
    EXPECT_EQ(ReadAll(stream), std::string("AB\xC3\xA9\xF0\x9F\x98\x80\xEF\xBF\xBD"));
    *__error_nid_postfix() = 0;
    EXPECT_EQ(fputwc_nid_postfix(u'x', nullptr), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(fputws_nid_postfix(nullptr, &stream), -1);
    EXPECT_EQ(fputws_nid_postfix(u"x", nullptr), -1);
    stream.Close();
    // A closed stream must fail with an error code rather than throw into the caller.
    EXPECT_EQ(fputwc_nid_postfix(u'x', &stream), -1);
    EXPECT_EQ(fputws_nid_postfix(u"x", &stream), -1);
}

// Invariant: fscanf assigns through guest pointers with guest (LP64) sizes: %d/%s, %ld into 8 bytes (must not
// leave the upper half stale), %lf, %hhd, scansets, %c, suppression (%*d), and returns EOF at end of input.
TEST(Fscanf, ParsesAllGuestConversions) {
    FileStream stream(std::tmpfile());
    ASSERT_EQ(fprintf_nid_postfix(&stream, "%d %s -5000000000 2.5 7 abcxyz Q 99 tail", 42, "answer"), 44);
    std::rewind(stream.GetHandle());
    int number = 0;
    char word[16]{};
    std::int64_t big = 0x7777777777777777LL;  // upper half must be overwritten by a correct %ld
    double real = 0;
    signed char tiny = 0;
    char set[8]{};
    char single = 0;
    char last[8]{};
    EXPECT_EQ(fscanf_nid_postfix(&stream, "%d %15s %ld %lf %hhd %[a-c]", &number, word, &big, &real, &tiny, set), 6);
    EXPECT_EQ(number, 42);
    EXPECT_STREQ(word, "answer");
    EXPECT_EQ(big, -5000000000LL);
    EXPECT_DOUBLE_EQ(real, 2.5);
    EXPECT_EQ(tiny, 7);
    EXPECT_STREQ(set, "abc");
    EXPECT_EQ(fscanf_nid_postfix(&stream, "%*3s %c %*d %7s", &single, last), 2);  // %*: no pointer consumed
    EXPECT_EQ(single, 'Q');
    EXPECT_STREQ(last, "tail");
    EXPECT_EQ(fscanf_nid_postfix(&stream, "%d", &number), EOF);
    stream.Close();
}

// Invariant: fscanf rejects a null stream, null format and an over-long conversion list (more than 16
// pointers) with EOF/EINVAL instead of reading garbage arguments.
TEST(Fscanf, RejectsBadArguments) {
    *__error_nid_postfix() = 0;
    int value = 0;
    EXPECT_EQ(fscanf_nid_postfix(nullptr, "%d", &value), EOF);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    FileStream stream(std::tmpfile());
    *__error_nid_postfix() = 0;
    EXPECT_EQ(fscanf_nid_postfix(&stream, nullptr), EOF);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
#ifdef _WIN32
    // The 16-pointer cap only exists on the marshalling (Windows) path; native vfscanf has no such limit.
    *__error_nid_postfix() = 0;
    EXPECT_EQ(fscanf_nid_postfix(&stream, "%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d%d", &value, &value, &value, &value, &value,
                                 &value, &value, &value, &value, &value, &value, &value, &value, &value, &value, &value, &value),
              EOF);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
#endif
    stream.Close();
}

// Invariant (regression): sscanf also assigns correctly with 8-byte %ld and several conversions. On Windows it
// previously handed the System V register layout to the host vsscanf as if it were a host va_list.
TEST(Sscanf, AssignsThroughGuestPointers) {
    int number = 0;
    std::int64_t big = 0x7777777777777777LL;
    char word[8]{};
    EXPECT_EQ(sscanf_nid_postfix("12 -9000000000 hello", "%d %ld %7s", &number, &big, word), 3);
    EXPECT_EQ(number, 12);
    EXPECT_EQ(big, -9000000000LL);
    EXPECT_STREQ(word, "hello");
    EXPECT_EQ(sscanf_nid_postfix("zzz", "%d", &number), 0);
}

// Invariant (regression, behaviour oracle musl libc-test sscanf.c): sscanf with more than four conversions
// must store through every pointer. The old register-parameter implementation took its va_list overflow
// area from the fifth argument's value, so conversions 5+ wrote through garbage.
TEST(Sscanf, ManyConversions) {
    int a = 0, b = 0, c = 0, d = 0, e = 0, f = 0, g = 0;
    EXPECT_EQ(sscanf_nid_postfix("011 0x100 11 0x100 100 7 8", "%i %i %o %x %x %d %d", &a, &b, &c, &d, &e, &f, &g), 7);
    EXPECT_EQ(a, 9);
    EXPECT_EQ(b, 256);
    EXPECT_EQ(c, 9);
    EXPECT_EQ(d, 256);
    EXPECT_EQ(e, 256);
    EXPECT_EQ(f, 7);
    EXPECT_EQ(g, 8);
}

// Invariant (oracle: musl sscanf.c): return values distinguish partial match, no match and input failure;
// %8c does not skip or terminate, %2d limits digits, and %* suppresses assignment.
TEST(Sscanf, ReturnValueSemantics) {
    int x = 0, y = 0, z = 0;
    EXPECT_EQ(sscanf_nid_postfix("20 xyz", "%d %d\n", &x, &y), 1);
    EXPECT_EQ(sscanf_nid_postfix("xyz", "%d %d\n", &x, &y), 0);
    EXPECT_EQ(sscanf_nid_postfix("", "%d %d\n", &x, &y), EOF);
    EXPECT_EQ(sscanf_nid_postfix(" 12345 6", "%2d%d%d", &x, &y, &z), 3);
    EXPECT_EQ(x, 12);
    EXPECT_EQ(y, 345);
    EXPECT_EQ(z, 6);
    char a[16]{}, b[16]{};
#ifndef _WIN32
    // The Windows host scanf accepts a short %8c field (returns 2); C and FreeBSD require exactly 8 chars.
    // Known divergence, see docs/spec/libc.md Open questions.
    EXPECT_EQ(sscanf_nid_postfix("hello, world\n", "%8c%8c", a, b), 1);
#endif
    EXPECT_EQ(sscanf_nid_postfix("hello, world\n", "%8c", a), 1);
    EXPECT_EQ(std::memcmp(a, "hello, w", 8), 0);
    EXPECT_EQ(sscanf_nid_postfix("56789 0123 56a72", "%2d%d%*d %[0123456789]\n", &x, &y, a), 3);
    EXPECT_EQ(x, 56);
    EXPECT_EQ(y, 789);
    EXPECT_STREQ(a, "56");
}

// Invariant (oracle: musl swprintf.c): swprintf truncation returns -1 with a terminated prefix and no
// overrun; %lc and %s (UTF-8) decode to single UTF-16 units; a null/zero buffer fails.
TEST(WideFormatting, MuslSwprintfEdges) {
    char16_t b[8];
    std::fill(std::begin(b), std::end(b), u'x');
    EXPECT_EQ(FormatWide(b, 4, u"%d", 123456), -1);
    EXPECT_EQ(std::u16string(b), u"123");
    EXPECT_EQ(b[5], u'x');
    EXPECT_EQ(FormatWide(b, 2, u"%lc", 0xc0), 1);
    EXPECT_EQ(b[0], 0xc0);
    EXPECT_EQ(FormatWide(b, 2, u"%lc", 0x20ac), 1);
    EXPECT_EQ(b[0], 0x20ac);
    EXPECT_EQ(FormatWide(b, 3, u"%s", "\xc3\x80!"), 2);
    EXPECT_EQ(b[0], 0xc0);
    EXPECT_EQ(FormatWide(b, 2, u"%.1s", "\xc3\x80!"), 1);
    EXPECT_EQ(b[0], 0xc0);
    EXPECT_EQ(FormatWide(nullptr, 0, u"%d", 123456), -1);
}

// Invariant (oracle: musl snprintf.c integer table): flag/precision combinations, notably that precision 0
// with value 0 prints nothing (except "%#.0o"), and width/flags are still honoured.
TEST(WideFormatting, MuslIntegerFlagTable) {
    char16_t b[32];
    struct Case { const char16_t* fmt; int v; const char16_t* want; };
    const Case cases[] = {
        {u"%04d", 12, u"0012"}, {u"%.3d", 12, u"012"}, {u"%-3d", 12, u"12 "}, {u"%+- 5d", 12, u"+12  "},
        {u"%0-5d", 12, u"12   "}, {u"%.0d", 0, u""}, {u"%#.0o", 0, u"0"}, {u"%#.0x", 0, u""},
        {u"%02.0d", 0, u"  "}, {u"% .0d", 0, u" "}, {u"%+.0d", 0, u"+"}, {u"%#x", 63, u"0x3f"}, {u"%X", 63, u"3F"},
    };
    for (const auto& c : cases) {
        const int n = FormatWide(b, 32, c.fmt, c.v);
        EXPECT_EQ(std::u16string(b), c.want);
        EXPECT_EQ(static_cast<std::size_t>(n), std::u16string(c.want).size());
    }
}

// Invariant (oracle: FreeBSD lib/libc/tests/stdio/printbasic_test.c, BSD-2): the integer length modifiers
// j, t, z, l, ll, h and hh print full-width -1/max values, and INT_MIN/INTMAX_MIN print exactly.
TEST(WideFormatting, FreeBsdIntegerLengthTable) {
    char16_t b[64];
    FormatWide(b, 64, u"%jd %ju", std::intmax_t{-1}, std::uintmax_t{UINT64_MAX});
    EXPECT_EQ(std::u16string(b), u"-1 18446744073709551615");
    FormatWide(b, 64, u"%td %tu %zd %zu", std::ptrdiff_t{-1}, std::size_t{SIZE_MAX}, std::ptrdiff_t{-1}, std::size_t{SIZE_MAX});
    EXPECT_EQ(std::u16string(b), u"-1 18446744073709551615 -1 18446744073709551615");
    FormatWide(b, 64, u"%ld %lu %lld", std::int64_t{-1}, std::uint64_t{UINT64_MAX}, std::int64_t{-1});
    EXPECT_EQ(std::u16string(b), u"-1 18446744073709551615 -1");
    FormatWide(b, 64, u"%hd %hu %hhd %hhu", -1, 65535, -1, 255);
    EXPECT_EQ(std::u16string(b), u"-1 65535 -1 255");
    FormatWide(b, 64, u"%d %jd", std::numeric_limits<int>::min(), std::numeric_limits<std::intmax_t>::min());
    EXPECT_EQ(std::u16string(b), u"-2147483648 -9223372036854775808");
}

// Invariant (oracle: FreeBSD sscanf_test.c): %n reports characters consumed, and 64-bit length modifiers
// (j, z, t, ll) store eight bytes into guest integers.
TEST(Sscanf, FreeBsdCountAndWideLengths) {
    int value = 0, consumed = -1;
    EXPECT_EQ(sscanf_nid_postfix("0x1f rest", "%i%n", &value, &consumed), 1);
    EXPECT_EQ(value, 31);
    EXPECT_EQ(consumed, 4);
    std::intmax_t j = 0x7777777777777777LL;
    std::size_t z = 0x7777777777777777ULL;
    std::ptrdiff_t t = 0x7777777777777777LL;
    long long ll = 0x7777777777777777LL;
    EXPECT_EQ(sscanf_nid_postfix("-5000000000 5000000001 -5000000002 5000000003", "%jd %zu %td %lld", &j, &z, &t, &ll), 4);
    EXPECT_EQ(j, -5000000000LL);
    EXPECT_EQ(z, 5000000001ULL);
    EXPECT_EQ(t, -5000000002LL);
    EXPECT_EQ(ll, 5000000003LL);
    EXPECT_EQ(sscanf_nid_postfix("08", "%i", &value), 1);  // octal prefix stops at the 8
    EXPECT_EQ(value, 0);
}
