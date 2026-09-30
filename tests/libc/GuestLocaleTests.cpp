// tests/libc/GuestLocaleTests.cpp
// Verification test suite for libc locale facets, iostream data symbols, and _Id_cnt semantics.
// Subsystem: libc. Exercises System V ABI exports, std::locale facets, and stream formatting.

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

// Verifies one NID hash by recomputing from the unadorned symbol name.
void ExpectNid(const char* stripped, const char* expected) {
    EXPECT_EQ(::Nid::ComputeNid(stripped, ""), std::string(expected));
}

// Test fixture facet to verify facet registration through the exported runtime symbol.
struct TestFacet : std::locale::facet {
    static inline std::locale::id id;
};

}  // namespace

// Verifies that standard ctype<char>::id hashes to the expected guest NID.
TEST(GuestLocaleTests, CtypeCharIdNid) {
    ExpectNid("_ZNSt5ctypeIcE2idE", "Cv+zC4EjGMA");
}

// Verifies that wide character ctype<wchar_t>::id hashes to the expected guest NID.
TEST(GuestLocaleTests, CtypeWcharIdNid) {
    ExpectNid("_ZNSt5ctypeIwE2idE", "VmqsS6auJzo");
}

// Verifies that character collate<char>::id hashes to the expected guest NID.
TEST(GuestLocaleTests, CollateCharIdNid) {
    ExpectNid("_ZNSt7collateIcE2idE", "7brRfHVVAlI");
}

// Verifies that wide character collate<wchar_t>::id hashes to the expected guest NID.
TEST(GuestLocaleTests, CollateWcharIdNid) {
    ExpectNid("_ZNSt7collateIwE2idE", "irGo1yaJ-vM");
}

// Verifies that codecvt<char, char, mbstate_t>::id hashes to the expected guest NID.
TEST(GuestLocaleTests, CodecvtCharIdNid) {
    ExpectNid("_ZNSt7codecvtIcc9_MbstatetE2idE", "eVFYZnYNDo0");
}

// Verifies that codecvt<char, char, mbstate_t> vtable hashes to the expected guest NID.
TEST(GuestLocaleTests, CodecvtCharVtableNid) {
    ExpectNid("_ZTVSt7codecvtIcc9_MbstatetE", "aK1Ymf-NhAs");
}

// Verifies that codecvt<wchar_t, char, mbstate_t>::id hashes to the expected guest NID.
TEST(GuestLocaleTests, CodecvtWcharIdNid) {
    ExpectNid("_ZNSt7codecvtIwc9_MbstatetE2idE", "FjZCPmK0SbA");
}

// Verifies that num_put facet id hashes to the expected guest NID.
TEST(GuestLocaleTests, NumPutIdNid) {
    ExpectNid("_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE",
              "E14mW8pVpoE");
}

// Verifies that num_put facet vtable hashes to the expected guest NID.
TEST(GuestLocaleTests, NumPutVtableNid) {
    ExpectNid("_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE",
              "1kZFcktOm+s");
}

// Verifies that locale::_Id_cnt hashes to the expected guest NID.
TEST(GuestLocaleTests, LocaleIdCountNid) {
    ExpectNid("_ZNSt6locale2id7_Id_cntE", "H4fcpQOpc08");
}

// Verifies that collate<char>::_Getcat hashes to the expected guest NID.
TEST(GuestLocaleTests, CollateCharGetcatNid) {
    ExpectNid("_ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1_", "BSVJqITGCyI");
}

// Verifies link addresses, ABI sizes, and alignment of all exported locale data symbols.
TEST(GuestLocaleTests, SymbolLinkageSizesAndAlignment) {
    EXPECT_NE(&_ZNSt5ctypeIcE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt5ctypeIwE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt7collateIcE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt7collateIwE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt7codecvtIcc9_MbstatetE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZTVSt7codecvtIcc9_MbstatetE_nid_postfix[0], nullptr);
    EXPECT_NE(&_ZNSt7codecvtIwc9_MbstatetE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix,
              nullptr);
    EXPECT_NE(&_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix[0],
              nullptr);
    EXPECT_NE(&_ZNSt6locale2id7_Id_cntE_nid_postfix, nullptr);

    EXPECT_EQ(sizeof(_ZNSt5ctypeIcE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt5ctypeIwE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt7collateIcE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt7collateIwE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt7codecvtIcc9_MbstatetE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZTVSt7codecvtIcc9_MbstatetE_nid_postfix), 16 * sizeof(std::uintptr_t));
    EXPECT_EQ(sizeof(_ZNSt7codecvtIwc9_MbstatetE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix),
              8u);
    EXPECT_EQ(sizeof(_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix),
              12 * sizeof(std::uintptr_t));
    EXPECT_EQ(sizeof(_ZNSt6locale2id7_Id_cntE_nid_postfix), 4u);

    // Standard stream buffers have non-null address, proper size and 16-byte alignment
    EXPECT_NE(_ZSt4cout_nid_postfix, nullptr);
    EXPECT_NE(_ZSt4cerr_nid_postfix, nullptr);
    EXPECT_NE(_ZSt3cin_nid_postfix, nullptr);
    EXPECT_NE(_ZSt5wcout_nid_postfix, nullptr);
    EXPECT_NE(_ZSt5wcerr_nid_postfix, nullptr);
    EXPECT_NE(_ZSt4wcin_nid_postfix, nullptr);

    EXPECT_EQ(sizeof(_ZSt4cout_nid_postfix), 0x400u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(_ZSt4cout_nid_postfix) % 16, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(_ZSt4cerr_nid_postfix) % 16, 0u);
    EXPECT_EQ(reinterpret_cast<std::uintptr_t>(_ZSt3cin_nid_postfix) % 16, 0u);
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
