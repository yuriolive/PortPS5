/**
 * Verification test suite for libc locale facets, iostream data symbols, and _Id_cnt semantics.
 *
 * Subsystem: libc. Exercises System V ABI exports, std::locale facets, and stream formatting.
 */

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <string>
#include <vector>

#include <nid/NidCompute.hpp>
#include <prx/libc/include/LocaleSupport.hpp>

namespace {

/**
 * Verifies one NID hash by recomputing from the unadorned symbol name.
 *
 * Passes an empty library name and compares the computed NID with the expected hash.
 */
void ExpectNid(const char* stripped, const char* expected) {
    EXPECT_EQ(::Nid::ComputeNid(stripped, ""), std::string(expected));
}

/**
 * Test fixture facet to verify facet registration through the exported runtime symbol.
 *
 * Provides a std::locale::facet instance for the System V ABI registration call.
 */
struct TestFacet : std::locale::facet {
    static inline std::locale::id id;
};

}  // namespace

// Mapping descriptor relating guest C++ export symbol names to expected NID hashes.
struct GuestNidMapping {
    const char* testName;
    const char* symbol;
    const char* expectedNid;

    friend void PrintTo(const GuestNidMapping& mapping, std::ostream* os) {
        *os << mapping.testName;
    }
};

// Parameterized test fixture verifying guest locale and stream NID hashes match relinker expectations.
class GuestLocaleNidTests : public ::testing::TestWithParam<GuestNidMapping> {};

// Verifies that each locale export symbol hashes to the expected NID recorded in title inventory.
TEST_P(GuestLocaleNidTests, SymbolHashesToExpectedNid) {
    const auto& [name, symbol, expectedNid] = GetParam();
    (void)name;
    EXPECT_EQ(::Nid::ComputeNid(symbol, ""), std::string(expectedNid))
        << "Mismatch for symbol: " << symbol;
}

INSTANTIATE_TEST_SUITE_P(
    LocaleSymbols,
    GuestLocaleNidTests,
    ::testing::Values(
        GuestNidMapping{"CtypeCharId", "_ZNSt5ctypeIcE2idE", "Cv+zC4EjGMA"},
        GuestNidMapping{"CtypeWcharId", "_ZNSt5ctypeIwE2idE", "VmqsS6auJzo"},
        GuestNidMapping{"CollateCharId", "_ZNSt7collateIcE2idE", "7brRfHVVAlI"},
        GuestNidMapping{"CollateWcharId", "_ZNSt7collateIwE2idE", "irGo1yaJ-vM"},
        GuestNidMapping{"CodecvtCharId", "_ZNSt7codecvtIcc9_MbstatetE2idE", "eVFYZnYNDo0"},
        GuestNidMapping{"CodecvtCharVtable", "_ZTVSt7codecvtIcc9_MbstatetE", "aK1Ymf-NhAs"},
        GuestNidMapping{"CodecvtWcharId", "_ZNSt7codecvtIwc9_MbstatetE2idE", "FjZCPmK0SbA"},
        GuestNidMapping{"NumPutId", "_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE", "E14mW8pVpoE"},
        GuestNidMapping{"NumPutVtable", "_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE", "1kZFcktOm+s"},
        GuestNidMapping{"LocaleIdCount", "_ZNSt6locale2id7_Id_cntE", "H4fcpQOpc08"},
        GuestNidMapping{"CollateCharGetcat", "_ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1_", "BSVJqITGCyI"}
    ),
    [](const ::testing::TestParamInfo<GuestNidMapping>& info) {
        return std::string(info.param.testName);
    }
);

// Verifies link addresses, ABI sizes, and alignment of exported locale data and stream storage symbols.
TEST(GuestLocaleTests, SymbolLinkageSizesAndAlignment) {
    struct SymbolSpec {
        const void* address;
        std::size_t size;
    };

    const SymbolSpec specs[] = {
        {&_ZNSt5ctypeIcE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZNSt5ctypeIwE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZNSt7collateIcE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZNSt7collateIwE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZNSt7codecvtIcc9_MbstatetE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZTVSt7codecvtIcc9_MbstatetE_nid_postfix, 16 * sizeof(std::uintptr_t)},
        {&_ZNSt7codecvtIwc9_MbstatetE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix, sizeof(std::uint64_t)},
        {&_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix, 12 * sizeof(std::uintptr_t)},
        {&_ZNSt6locale2id7_Id_cntE_nid_postfix, sizeof(std::int32_t)},
    };

    for (const auto& item : specs) {
        ASSERT_NE(item.address, nullptr);
    }

    // Standard stream buffers have non-null address, proper size and 16-byte alignment
    const void* const streamBuffers[] = {
        _ZSt4cout_nid_postfix,
        _ZSt4cerr_nid_postfix,
        _ZSt3cin_nid_postfix,
        _ZSt5wcout_nid_postfix,
        _ZSt5wcerr_nid_postfix,
        _ZSt4wcin_nid_postfix,
    };

    for (const void* buf : streamBuffers) {
        ASSERT_NE(buf, nullptr);
        EXPECT_EQ(reinterpret_cast<std::uintptr_t>(buf) % 16, 0u);
    }

    EXPECT_EQ(sizeof(_ZSt4cout_nid_postfix), 0x400u);
    EXPECT_EQ(sizeof(_ZSt4cerr_nid_postfix), 0x400u);
    EXPECT_EQ(sizeof(_ZSt3cin_nid_postfix), 0x400u);
    EXPECT_EQ(sizeof(_ZSt5wcout_nid_postfix), 0x400u);
    EXPECT_EQ(sizeof(_ZSt5wcerr_nid_postfix), 0x400u);
    EXPECT_EQ(sizeof(_ZSt4wcin_nid_postfix), 0x400u);
}

// Verifies that standard ctype, collate, and num_put facets resolve cleanly via std::use_facet.
TEST(GuestLocaleTests, StandardFacetsResolveCleanly) {
    const std::locale& classicLoc = std::locale::classic();

    ASSERT_TRUE(std::has_facet<std::ctype<char>>(classicLoc));
    ASSERT_TRUE(std::has_facet<std::collate<char>>(classicLoc));
    ASSERT_TRUE(std::has_facet<std::num_put<char>>(classicLoc));

    // Verify ctype operations
    const auto& ctypeFacet = std::use_facet<std::ctype<char>>(classicLoc);
    EXPECT_EQ(ctypeFacet.toupper('a'), 'A');
    EXPECT_EQ(ctypeFacet.tolower('Z'), 'z');
    EXPECT_TRUE(ctypeFacet.is(std::ctype_base::digit, '7'));
    EXPECT_FALSE(ctypeFacet.is(std::ctype_base::digit, 'x'));
    EXPECT_TRUE(ctypeFacet.is(std::ctype_base::space, ' '));

    // Verify collate operations
    const auto& collateFacet = std::use_facet<std::collate<char>>(classicLoc);
    const std::string s1 = "apple";
    const std::string s2 = "banana";
    EXPECT_LT(collateFacet.compare(s1.data(), s1.data() + s1.size(),
                                   s2.data(), s2.data() + s2.size()), 0);
    EXPECT_EQ(collateFacet.compare(s1.data(), s1.data() + s1.size(),
                                   s1.data(), s1.data() + s1.size()), 0);

    // Verify num_put operations into a string stream
    std::ostringstream oss;
    oss.imbue(classicLoc);
    const auto& numPutFacet = std::use_facet<std::num_put<char>>(classicLoc);
    std::ostreambuf_iterator<char> it(oss);
    numPutFacet.put(it, oss, ' ', 12345L);
    EXPECT_EQ(oss.str(), "12345");
}

// Verifies that basic stream formatting and parsing works without throwing exceptions.
TEST(GuestLocaleTests, BasicStreamFormattingWithoutExceptions) {
    std::ostringstream oss;
    oss.imbue(std::locale::classic());
    oss << "Value: " << 42 << ", Float: " << std::fixed << std::setprecision(2) << 3.14;
    oss << ", Bool: " << std::boolalpha << true;
    EXPECT_EQ(oss.str(), "Value: 42, Float: 3.14, Bool: true");

    std::istringstream iss("100 200 300");
    iss.imbue(std::locale::classic());
    int a = 0;
    int b = 0;
    int c = 0;
    iss >> a >> b >> c;
    EXPECT_EQ(a, 100);
    EXPECT_EQ(b, 200);
    EXPECT_EQ(c, 300);
}

// Verifies that standard stream buffers are placement-constructed as usable iostream objects.
TEST(GuestLocaleTests, StandardStreamsArePlacementConstructedAndUsable) {
    auto* coutStream = reinterpret_cast<std::ostream*>(_ZSt4cout_nid_postfix);
    ASSERT_NE(coutStream, nullptr);
    EXPECT_NE(coutStream->rdbuf(), nullptr);

    auto* cerrStream = reinterpret_cast<std::ostream*>(_ZSt4cerr_nid_postfix);
    ASSERT_NE(cerrStream, nullptr);
    EXPECT_NE(cerrStream->rdbuf(), nullptr);

    auto* cinStream = reinterpret_cast<std::istream*>(_ZSt3cin_nid_postfix);
    ASSERT_NE(cinStream, nullptr);
    EXPECT_NE(cinStream->rdbuf(), nullptr);

    auto* wcoutStream = reinterpret_cast<std::wostream*>(_ZSt5wcout_nid_postfix);
    ASSERT_NE(wcoutStream, nullptr);
    EXPECT_NE(wcoutStream->rdbuf(), nullptr);

    auto* wcerrStream = reinterpret_cast<std::wostream*>(_ZSt5wcerr_nid_postfix);
    ASSERT_NE(wcerrStream, nullptr);
    EXPECT_NE(wcerrStream->rdbuf(), nullptr);

    auto* wcinStream = reinterpret_cast<std::wistream*>(_ZSt4wcin_nid_postfix);
    ASSERT_NE(wcinStream, nullptr);
    EXPECT_NE(wcinStream->rdbuf(), nullptr);

    // Verify standard stream ties and flushing settings
    EXPECT_EQ(cinStream->tie(), coutStream);
    EXPECT_EQ(cerrStream->tie(), coutStream);
    EXPECT_NE(cerrStream->flags() & std::ios_base::unitbuf, 0);

    EXPECT_EQ(wcinStream->tie(), wcoutStream);
    EXPECT_EQ(wcerrStream->tie(), wcoutStream);
    EXPECT_NE(wcerrStream->flags() & std::ios_base::unitbuf, 0);

    // Test that writing to a standard stream object executes through vptr and rdbuf without faulting
    std::stringbuf testBuf;
    std::streambuf* origBuf = cerrStream->rdbuf(&testBuf);
    *cerrStream << "PortPS5 stream ok " << 42;
    EXPECT_EQ(testBuf.str(), "PortPS5 stream ok 42");
    cerrStream->rdbuf(origBuf);
}

// Verifies that locale facet registration drives the runtime _Id_cnt increment.
TEST(GuestLocaleTests, FacetRegistrationDrivesIdCounter) {
    const std::int32_t initialCount = _ZNSt6locale2id7_Id_cntE_nid_postfix;

    TestFacet testFacet;

    _ZNSt6locale5facet9_RegisterEv_nid_postfix(&testFacet);
    EXPECT_EQ(_ZNSt6locale2id7_Id_cntE_nid_postfix, initialCount + 1);

    // Register with null pointer must safely no-op without altering the counter
    _ZNSt6locale5facet9_RegisterEv_nid_postfix(nullptr);
    EXPECT_EQ(_ZNSt6locale2id7_Id_cntE_nid_postfix, initialCount + 1);

    // Clean up
    _ZNSt6locale2id7_Id_cntE_nid_postfix = initialCount;
}

// Verifies that facet ID symbols and _Id_cnt are isolated in memory and writable without corruption.
TEST(GuestLocaleTests, FacetIdStorageAndSymbolIsolation) {
    const std::int32_t initialCount = _ZNSt6locale2id7_Id_cntE_nid_postfix;
    const std::uint64_t originalCtypeId = _ZNSt5ctypeIcE2idE_nid_postfix;

    // Simulate inlined guest code incrementing _Id_cnt and writing to facet id
    const std::uint64_t assignedId = static_cast<std::uint64_t>(++_ZNSt6locale2id7_Id_cntE_nid_postfix);
    _ZNSt5ctypeIcE2idE_nid_postfix = assignedId;

    EXPECT_EQ(_ZNSt5ctypeIcE2idE_nid_postfix, assignedId);
    EXPECT_EQ(_ZNSt6locale2id7_Id_cntE_nid_postfix, initialCount + 1);

    // Ensure adjacent symbols are unaffected (no out-of-bounds corruption)
    EXPECT_EQ(_ZNSt7collateIcE2idE_nid_postfix, 0u);
    EXPECT_EQ(_ZNSt7collateIwE2idE_nid_postfix, 0u);
    EXPECT_EQ(_ZNSt7codecvtIcc9_MbstatetE2idE_nid_postfix, 0u);

    // Restore state
    _ZNSt5ctypeIcE2idE_nid_postfix = originalCtypeId;
    _ZNSt6locale2id7_Id_cntE_nid_postfix = initialCount;
}

// Verifies that classic locale initialization and global locale getters return valid instances.
TEST(GuestLocaleTests, ClassicAndGlobalLocaleGetters) {
    const std::locale* initLoc = _ZNSt6locale5_InitEv_nid_postfix();
    ASSERT_NE(initLoc, nullptr);
    ASSERT_NE(_ZSt21_sceLibcClassicLocale_nid_postfix, nullptr);
    EXPECT_EQ(initLoc, _ZSt21_sceLibcClassicLocale_nid_postfix);
    EXPECT_EQ(*initLoc, std::locale::classic());

    const std::locale* globalLoc = _ZNSt6locale16_GetgloballocaleEv_nid_postfix();
    ASSERT_NE(globalLoc, nullptr);
    EXPECT_EQ(*globalLoc, std::locale::classic());
}

// Verifies that character collate _Getcat terminates cleanly through NotImplemented_nid_no_patch.
TEST(GuestLocaleTests, CollateCharGetcatTerminatesAsNotImplemented) {
    const std::locale::facet* facet = nullptr;
    const std::locale classicLoc = std::locale::classic();
    EXPECT_DEATH(_ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(&facet, &classicLoc), "collate<char>::_Getcat");
}

