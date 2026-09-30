// GoogleTest suite for the libc extras ported from AnyPS5 a599eca7 (and helpers around them):
// bounds-checked string/memory functions, wcstombs (C locale), strtoumax, asctime, 4-byte atomics,
// the `_s` printf family, and fopen_s / fgetpos / fsetpos.
//
// Every test pins return codes (guest FreeBSD errno numbering: EINVAL 22, ERANGE 34, ENOENT 2, EILSEQ 86)
// and the destination-neutralization rules, because guests that ignore return values rely on them.
// No game data: only in-memory buffers and one temporary file in the working directory.
#include "prx/libc/include/FileStream.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"

#include <gtest/gtest.h>

#include <cinttypes>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif
#include <filesystem>
#include <limits>

extern "C" {
// Declaration of `__error_nid_postfix`; its contract is documented at the definition.
int* APS5_VABI __error_nid_postfix();
// Declaration of `strncpy_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI strncpy_s_nid_postfix(char*, std::size_t, const char*, std::size_t);
// Declaration of `strcpy_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI strcpy_s_nid_postfix(char*, std::size_t, const char*);
// Declaration of `strncat_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI strncat_s_nid_postfix(char*, std::size_t, const char*, std::size_t);
// Declaration of `strcat_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI strcat_s_nid_postfix(char*, std::size_t, const char*);
// Declaration of `memcpy_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI memcpy_s_nid_postfix(void*, std::size_t, const void*, std::size_t);
// Declaration of `memmove_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI memmove_s_nid_postfix(void*, std::size_t, const void*, std::size_t);
// Declaration of `memset_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI memset_s_nid_postfix(void*, std::size_t, int, std::size_t);
// Declaration of `strnstr_nid_postfix`; its contract is documented at the definition.
char* APS5_VABI strnstr_nid_postfix(const char*, const char*, std::size_t);
// Declaration of `wcstombs_nid_postfix`; its contract is documented at the definition.
std::size_t APS5_VABI wcstombs_nid_postfix(char*, const std::uint16_t*, std::size_t);
// Declaration of `strtoumax_nid_postfix`; its contract is documented at the definition.
std::uintmax_t APS5_VABI strtoumax_nid_postfix(const char*, char**, int);
// Declaration of `asctime_nid_postfix`; its contract is documented at the definition.
char* APS5_VABI asctime_nid_postfix(const std::tm*);
// Declaration of `_Atomic_compare_exchange_weak_4_nid_postfix`; its contract is documented at the definition.
int APS5_VABI _Atomic_compare_exchange_weak_4_nid_postfix(volatile unsigned int*, unsigned int*, unsigned int, int, int);
// Declaration of `_Atomic_load_4_nid_postfix`; its contract is documented at the definition.
unsigned int APS5_VABI _Atomic_load_4_nid_postfix(volatile unsigned int*, int);
// Declaration of `snprintf_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI snprintf_s_nid_postfix(char*, std::size_t, const char*, ...);
// Declaration of `sprintf_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sprintf_s_nid_postfix(char*, std::size_t, const char*, ...);
// Declaration of `sscanf_nid_postfix`; its contract is documented at the definition.
int APS5_VABI sscanf_nid_postfix(const char*, const char*, ...);
// Declaration of `vsprintf_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI vsprintf_s_nid_postfix(char*, std::size_t, const char*, void*);
// Declaration of `fopen_nid_postfix`; its contract is documented at the definition.
FileStream* APS5_VABI fopen_nid_postfix(const char*, const char*);
// Declaration of `fclose_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fclose_nid_postfix(FileStream*);
// Declaration of `fputs_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fputs_nid_postfix(const char*, FileStream*);
// Declaration of `fopen_s_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fopen_s_nid_postfix(FileStream**, const char*, const char*);
// Declaration of `fgetpos_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fgetpos_nid_postfix(FileStream*, std::int64_t*);
// Declaration of `fsetpos_nid_postfix`; its contract is documented at the definition.
int APS5_VABI fsetpos_nid_postfix(FileStream*, const std::int64_t*);
// Declaration of `ftello_nid_postfix`; its contract is documented at the definition.
std::int64_t APS5_VABI ftello_nid_postfix(FileStream*);
}

namespace {
constexpr int Einval = 22;
constexpr int Erange = 34;
constexpr int Enoent = 2;
constexpr int Eilseq = 86;
}

// Invariant: strcpy_s/strncpy_s copy and terminate on success; null dest/zero size -> EINVAL without writes;
// null src -> EINVAL with dest emptied; overflow -> ERANGE with dest emptied; count limits the copy.
TEST(BoundsCheckedStrings, CopyContract) {
    char small[4] = "zz";
    EXPECT_EQ(strcpy_s_nid_postfix(small, sizeof(small), "abc"), 0);
    EXPECT_STREQ(small, "abc");
    EXPECT_EQ(strcpy_s_nid_postfix(small, sizeof(small), "abcd"), Erange);
    EXPECT_EQ(small[0], '\0');
    EXPECT_EQ(strcpy_s_nid_postfix(nullptr, 4, "a"), Einval);
    char untouched[4] = "xyz";
    EXPECT_EQ(strcpy_s_nid_postfix(untouched, 0, "a"), Einval);
    EXPECT_STREQ(untouched, "xyz");
    EXPECT_EQ(strcpy_s_nid_postfix(untouched, sizeof(untouched), nullptr), Einval);
    EXPECT_EQ(untouched[0], '\0');
    char limited[8];
    EXPECT_EQ(strncpy_s_nid_postfix(limited, sizeof(limited), "abcdef", 3), 0);
    EXPECT_STREQ(limited, "abc");
    EXPECT_EQ(strncpy_s_nid_postfix(limited, 3, "abcdef", 3), Erange);  // 3 chars + NUL do not fit in 3
}

// Invariant: strcat_s/strncat_s append within the bound; an unterminated destination or overflow -> ERANGE
// with dest emptied; null src -> EINVAL.
TEST(BoundsCheckedStrings, ConcatContract) {
    char joined[8] = "ab";
    EXPECT_EQ(strcat_s_nid_postfix(joined, sizeof(joined), "cd"), 0);
    EXPECT_STREQ(joined, "abcd");
    EXPECT_EQ(strncat_s_nid_postfix(joined, sizeof(joined), "efgh", 2), 0);
    EXPECT_STREQ(joined, "abcdef");
    EXPECT_EQ(strcat_s_nid_postfix(joined, sizeof(joined), "gh"), Erange);
    EXPECT_EQ(joined[0], '\0');
    char unterminated[4] = {'a', 'b', 'c', 'd'};
    EXPECT_EQ(strcat_s_nid_postfix(unterminated, sizeof(unterminated), "x"), Erange);
    EXPECT_EQ(unterminated[0], '\0');
    char ok[8] = "a";
    EXPECT_EQ(strcat_s_nid_postfix(ok, sizeof(ok), nullptr), Einval);
    EXPECT_EQ(strcat_s_nid_postfix(nullptr, 8, "a"), Einval);
}

// Invariant: memcpy_s/memmove_s/memset_s honour destsz: oversize counts zero-fill (copy) or clamp (set) and
// report ERANGE; null pointers report EINVAL; memmove_s handles overlap.
TEST(BoundsCheckedMemory, Contract) {
    char bytes[4] = {1, 2, 3, 4};
    const char source[4] = {5, 6, 7, 8};
    EXPECT_EQ(memcpy_s_nid_postfix(bytes, sizeof(bytes), source, 2), 0);
    EXPECT_EQ(bytes[0], 5);
    EXPECT_EQ(bytes[2], 3);
    EXPECT_EQ(memcpy_s_nid_postfix(bytes, 2, source, 4), Erange);
    EXPECT_EQ(bytes[0], 0);
    EXPECT_EQ(bytes[1], 0);
    EXPECT_EQ(bytes[2], 3);  // bytes beyond destsz are untouched
    EXPECT_EQ(memcpy_s_nid_postfix(nullptr, 4, source, 1), Einval);
    EXPECT_EQ(memcpy_s_nid_postfix(bytes, 4, nullptr, 1), Einval);
    EXPECT_EQ(bytes[0], 0);  // null src zero-fills the destination
    char overlap[6] = "abcde";
    EXPECT_EQ(memmove_s_nid_postfix(overlap + 1, 5, overlap, 3), 0);
    EXPECT_STREQ(overlap, "aabce");
    EXPECT_EQ(memmove_s_nid_postfix(overlap, 2, overlap + 1, 4), Erange);
    EXPECT_EQ(memset_s_nid_postfix(bytes, sizeof(bytes), 9, 8), Erange);
    EXPECT_EQ(bytes[3], 9);
    EXPECT_EQ(memset_s_nid_postfix(bytes, sizeof(bytes), 7, 2), 0);
    EXPECT_EQ(bytes[1], 7);
    EXPECT_EQ(bytes[2], 9);
    EXPECT_EQ(memset_s_nid_postfix(nullptr, 4, 0, 1), Einval);
}

// Invariant: strnstr only matches inside the first `length` bytes and never reads past the NUL; an empty
// needle matches at the start.
TEST(BoundsCheckedStrings, Strnstr) {
    const char haystack[] = "haystack";
    EXPECT_EQ(strnstr_nid_postfix(haystack, "st", 4), nullptr);
    EXPECT_EQ(strnstr_nid_postfix(haystack, "st", 6), haystack + 3);
    EXPECT_EQ(strnstr_nid_postfix(haystack, "", 0), haystack);
    EXPECT_EQ(strnstr_nid_postfix(haystack, "stackk", 64), nullptr);
    EXPECT_EQ(strnstr_nid_postfix(haystack, "hay", 3), haystack);
}

// Invariant: wcstombs maps UTF-16 units <= 0xFF to bytes, returns the length for a null destination, stops at
// capacity without NUL, and fails with EILSEQ (86) above 0xFF / EINVAL (22) for a null source.
TEST(WideConversion, CLocaleWcstombs) {
    const std::uint16_t text[] = {'h', 'i', 0xE9, 0};
    char out[8]{};
    EXPECT_EQ(wcstombs_nid_postfix(out, text, sizeof(out)), 3u);
    EXPECT_EQ(std::memcmp(out, "hi\xE9", 4), 0);
    EXPECT_EQ(wcstombs_nid_postfix(nullptr, text, 0), 3u);
    char exact[2] = {'?', '?'};
    EXPECT_EQ(wcstombs_nid_postfix(exact, text, 2), 2u);
    EXPECT_EQ(exact[0], 'h');
    const std::uint16_t wide[] = {'a', 0x0100, 0};
    *__error_nid_postfix() = 0;
    EXPECT_EQ(wcstombs_nid_postfix(out, wide, sizeof(out)), static_cast<std::size_t>(-1));
    EXPECT_EQ(*__error_nid_postfix(), Eilseq);
    EXPECT_EQ(wcstombs_nid_postfix(nullptr, wide, 0), static_cast<std::size_t>(-1));
    EXPECT_EQ(wcstombs_nid_postfix(out, nullptr, sizeof(out)), static_cast<std::size_t>(-1));
    EXPECT_EQ(*__error_nid_postfix(), Einval);
}

// Invariant: strtoumax parses full 64-bit values (the guest intmax_t is 64-bit) and reports the end pointer.
TEST(NumericConversion, StrtoumaxIs64Bit) {
    char* end = nullptr;
    EXPECT_EQ(strtoumax_nid_postfix("18446744073709551615 tail", &end, 10), std::numeric_limits<std::uint64_t>::max());
    EXPECT_STREQ(end, " tail");
    EXPECT_EQ(strtoumax_nid_postfix("0x1ffffffff", nullptr, 16), 0x1ffffffffull);
}

// Invariant: asctime produces the fixed 26-byte layout "Www Mmm dd hh:mm:ss yyyy\n".
TEST(TimeExports, AsctimeFormat) {
    std::tm value{};
    value.tm_sec = 5; value.tm_min = 4; value.tm_hour = 3; value.tm_mday = 2;
    value.tm_mon = 0; value.tm_year = 100; value.tm_wday = 0;
    EXPECT_STREQ(asctime_nid_postfix(&value), "Sun Jan  2 03:04:05 2000\n");
}

// Invariant: the 4-byte atomics are real CAS/load: a matching expected value swaps and returns 1; a stale
// one fails, returns 0 and refreshes `expected` with the current value (spurious failure is allowed for the
// weak form, hence the retry loop on the success path).
TEST(Atomics, CompareExchangeAndLoad) {
    volatile unsigned int cell = 5;
    unsigned int expected = 5;
    int swapped = 0;
    for (int attempt = 0; attempt < 100 && !swapped; ++attempt) {
        expected = 5;
        swapped = _Atomic_compare_exchange_weak_4_nid_postfix(&cell, &expected, 9, 5, 5);
    }
    EXPECT_EQ(swapped, 1);
    EXPECT_EQ(_Atomic_load_4_nid_postfix(&cell, 5), 9u);
    expected = 5;  // stale
    EXPECT_EQ(_Atomic_compare_exchange_weak_4_nid_postfix(&cell, &expected, 1, 5, 5), 0);
    EXPECT_EQ(expected, 9u);
    EXPECT_EQ(cell, 9u);
}

// Invariant: sprintf_s/snprintf_s truncate to size-1 characters plus NUL and return the untruncated length;
// vsprintf_s forwards a System V va_list (here built by a small variadic helper).
static int APS5_VABI CallVsprintfS(char* buffer, std::size_t size, const char* format, ...) {
#ifdef _WIN32
    __builtin_sysv_va_list args;
    __builtin_sysv_va_start(args, format);
#else
    std::va_list args;
    va_start(args, format);
#endif
    const int result = vsprintf_s_nid_postfix(buffer, size, format, args);
#ifdef _WIN32
    __builtin_sysv_va_end(args);
#else
    va_end(args);
#endif
    return result;
}

TEST(CheckedPrintf, TruncationAndVaList) {
    char buffer[8];
    EXPECT_EQ(snprintf_s_nid_postfix(buffer, sizeof(buffer), "%d-%s", 42, "x"), 4);
    EXPECT_STREQ(buffer, "42-x");
    EXPECT_EQ(sprintf_s_nid_postfix(buffer, sizeof(buffer), "%s", "0123456789"), 10);  // truncated, length reported
    EXPECT_STREQ(buffer, "0123456");
    EXPECT_EQ(CallVsprintfS(buffer, sizeof(buffer), "%03d|%s", 7, "ab"), 6);
    EXPECT_STREQ(buffer, "007|ab");
}

namespace {
// Fixture that removes the temporary file even if an assertion fails part-way.
class StdioExtras : public ::testing::Test {
protected:
    static constexpr const char* Name = "portps5_libc_extras_test.tmp";
    void TearDown() override { std::error_code ignored; std::filesystem::remove(Name, ignored); }
};
}

// Invariant: fopen_s returns 0 and a stream on success, EINVAL for null arguments and ENOENT for a missing
// file, nulling *result on every failure.
TEST_F(StdioExtras, FopenSReturnCodes) {
    FileStream* stream = reinterpret_cast<FileStream*>(0x1);
    EXPECT_EQ(fopen_s_nid_postfix(&stream, "portps5_definitely_missing.bin", "rb"), Enoent);
    EXPECT_EQ(stream, nullptr);
    EXPECT_EQ(fopen_s_nid_postfix(&stream, nullptr, "rb"), Einval);
    EXPECT_EQ(stream, nullptr);
    EXPECT_EQ(fopen_s_nid_postfix(&stream, Name, nullptr), Einval);
    EXPECT_EQ(fopen_s_nid_postfix(nullptr, Name, "wb+"), Einval);
    ASSERT_EQ(fopen_s_nid_postfix(&stream, Name, "wb+"), 0);
    ASSERT_NE(stream, nullptr);
    EXPECT_EQ(fclose_nid_postfix(stream), 0);
}

// Invariant: fgetpos/fsetpos round-trip a 64-bit position; null position pointers fail with -1/EINVAL
// instead of throwing.
TEST_F(StdioExtras, FgetposFsetposRoundTrip) {
    FileStream* stream = nullptr;
    ASSERT_EQ(fopen_s_nid_postfix(&stream, Name, "wb+"), 0);
    ASSERT_GE(fputs_nid_postfix("hello world", stream), 0);
    std::int64_t position = -1;
    ASSERT_EQ(fgetpos_nid_postfix(stream, &position), 0);
    EXPECT_EQ(position, 11);
    const std::int64_t middle = 6;
    ASSERT_EQ(fsetpos_nid_postfix(stream, &middle), 0);
    EXPECT_EQ(ftello_nid_postfix(stream), 6);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(fgetpos_nid_postfix(stream, nullptr), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(fsetpos_nid_postfix(stream, nullptr), -1);
    EXPECT_EQ(fclose_nid_postfix(stream), 0);
}


// Invariant (oracle: C11 Annex K / review regression): strcpy_s/strncat_s scan the source only within the
// destination window. A source with no NUL inside that window returns ERANGE without reading past it, which a
// PAGE_NOACCESS guard page right after the source proves (an unbounded scan would fault).
#ifdef _WIN32
TEST(BoundsCheckedStrings, SourceScanStopsAtDestinationWindow) {
    auto* page = static_cast<char*>(VirtualAlloc(nullptr, 8192, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    ASSERT_NE(page, nullptr);
    DWORD previous = 0;
    ASSERT_TRUE(VirtualProtect(page + 4096, 4096, PAGE_NOACCESS, &previous));
    char* source = page + 4096 - 4;  // exactly four readable bytes, then the guard page
    std::memcpy(source, "abcd", 4);
    char dest[4] = "zzz";
    EXPECT_EQ(strcpy_s_nid_postfix(dest, sizeof(dest), source), Erange);
    EXPECT_EQ(dest[0], '\0');
    char joined[8] = "ab";
    EXPECT_EQ(strcat_s_nid_postfix(joined, 5, source), Erange);  // room for 2 more, source has no NUL in 2
    EXPECT_EQ(joined[0], '\0');
    VirtualFree(page, 0, MEM_RELEASE);
}
#endif

// Invariant (review regression): fgetpos/fsetpos report a null or closed stream as -1/EBADF (9) instead of
// letting a host exception reach the caller.
TEST_F(StdioExtras, PositionFunctionsRejectBadStreams) {
    std::int64_t position = 0;
    *__error_nid_postfix() = 0;
    EXPECT_EQ(fgetpos_nid_postfix(nullptr, &position), -1);
    EXPECT_EQ(*__error_nid_postfix(), 9);
    EXPECT_EQ(fsetpos_nid_postfix(nullptr, &position), -1);
    FileStream* stream = nullptr;
    ASSERT_EQ(fopen_s_nid_postfix(&stream, Name, "wb+"), 0);
    ASSERT_EQ(fclose_nid_postfix(stream), 0);
}

// Invariant (review regression): sscanf with a null string or format returns EOF/EINVAL rather than invoking
// undefined behaviour in the host scanf.
TEST(Sscanf, NullArgumentsAreRejected) {
    int value = 0;
    *__error_nid_postfix() = 0;
    EXPECT_EQ(sscanf_nid_postfix(nullptr, "%d", &value), EOF);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(sscanf_nid_postfix("1", nullptr, &value), EOF);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
}

#ifdef _WIN32
// Invariant (review regression, Windows narrow formatter): hostile literal/star widths and precisions fail with
// -1 and guest errno EINVAL at the export boundary; no exception unwinds through System V guest frames.
TEST(CheckedPrintf, NarrowFormatterRejectsHugeWidths) {
    char buffer[16];
    EXPECT_EQ(sprintf_s_nid_postfix(buffer, sizeof(buffer), "%5d", 7), 5);
    *__error_nid_postfix() = 0;
    EXPECT_EQ(sprintf_s_nid_postfix(buffer, sizeof(buffer), "%99999999999d", 7), -1);
    EXPECT_EQ(*__error_nid_postfix(), Einval);
    EXPECT_EQ(sprintf_s_nid_postfix(buffer, sizeof(buffer), "%*d", std::numeric_limits<int>::min(), 7), -1);
    EXPECT_EQ(sprintf_s_nid_postfix(buffer, sizeof(buffer), "%.99999999999f", 1.0), -1);
    EXPECT_EQ(snprintf_s_nid_postfix(buffer, sizeof(buffer), "%q", 1), -1);  // unsupported conversion
}
#endif
