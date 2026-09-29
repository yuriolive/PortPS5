// NID-presence tests for the six libc locale/iostream data exports plus the
// verbatim cross-prx Loader wrappers (docs/spec/build-toolchain.md NID rule).
//
// Why this suite: the Dreaming Sarah import inventory (484 unique NIDs) showed
// six libc `#T#T` libstdc++ ABI data NIDs with no provider, and a prx-load
// failure (GetLastError 127) where libSceVideoOut imports the mangled
// Loader::IsInitialized name that nid_patcher hashes. Each TEST below pins
// one invariant:
//
//   - Nid::ComputeNid of the stripped export name equals the title's NID hash,
//     so the patched libc.prx exports the needed NID string.
//   - The data symbols link (non-null address), have ABI sizes, and are
//     zero-initialized placeholders (lethal only on guest virtual calls).
//   - The _nid_no_patch Loader wrappers delegate to the same Loader instance
//     (address equality), report initialized/uninitialized paths, and abort on
//     uninitialized Get exactly like Loader::Get (death test).
//
// No GPU and no game data needed: only NID hashes, symbol names and counts.

#include <gtest/gtest.h>

#include <nid/NidCompute.hpp>
#include <prx/libc/include/config/Config.hpp>

#include <cstdint>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Extern declarations of the symbols under test (definitions live in libc).
// ---------------------------------------------------------------------------

extern "C" {
// Six locale/iostream ABI data exports (core/libs/prx/libc/src/LocaleSupport.cpp).
extern std::uint64_t _ZNSt5ctypeIcE2idE_nid_postfix;
extern std::uint64_t _ZNSt5ctypeIwE2idE_nid_postfix;
extern std::uint64_t _ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix;
extern std::uintptr_t _ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix[12];
extern std::uint64_t _ZNSt7collateIwE2idE_nid_postfix;
extern std::int32_t _ZNSt6locale2id7_Id_cntE_nid_postfix;
}

namespace {

// Verifies one NID hash: the booked title hash must equal a fresh recompute
// from the stripped export name, so the test fails if either drifts.
void ExpectNid(const char* stripped, const char* expected) {
    EXPECT_EQ(::Nid::ComputeNid(stripped, ""), std::string(expected));
}

// Initializes the Loader with the minimal schema-only TOML for wrapper tests.
void InitMinimalLoader() {
    using namespace ::PortPS5::Config;
    Loader::ResetForTesting();
    std::string error;
    std::vector<std::string> warnings;
    ASSERT_TRUE(Loader::InitializeForTesting("schema = 1\n", "", "", "C:/game", "", error,
                                             warnings))
        << error;
}

}  // namespace

// ctype<char>::id hashes to the title's Cv+zC4EjGMA NID.
TEST(LocaleNidHash, CtypeCharId) {
    ExpectNid("_ZNSt5ctypeIcE2idE", "Cv+zC4EjGMA");
}

// ctype<wchar_t>::id hashes to the title's VmqsS6auJzo NID.
TEST(LocaleNidHash, CtypeWcharId) {
    ExpectNid("_ZNSt5ctypeIwE2idE", "VmqsS6auJzo");
}

// num_put facet id hashes to the title's E14mW8pVpoE NID.
TEST(LocaleNidHash, NumPutId) {
    ExpectNid("_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE",
              "E14mW8pVpoE");
}

// num_put vtable hashes to the title's 1kZFcktOm+s NID.
TEST(LocaleNidHash, NumPutVtable) {
    ExpectNid("_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE",
              "1kZFcktOm+s");
}

// collate<wchar_t>::id hashes to the title's irGo1yaJ-vM NID.
TEST(LocaleNidHash, CollateWcharId) {
    ExpectNid("_ZNSt7collateIwE2idE", "irGo1yaJ-vM");
}

// locale::_Id_cnt hashes to the title's H4fcpQOpc08 NID.
TEST(LocaleNidHash, LocaleIdCount) {
    ExpectNid("_ZNSt6locale2id7_Id_cntE", "H4fcpQOpc08");
}

// All six link with ABI sizes and zero-initialized placeholder values.
TEST(LocaleNidLinkage, AddressSizeAndZeroInit) {
    EXPECT_NE(&_ZNSt5ctypeIcE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt5ctypeIwE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix,
              nullptr);
    EXPECT_NE(&_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix[0],
              nullptr);
    EXPECT_NE(&_ZNSt7collateIwE2idE_nid_postfix, nullptr);
    EXPECT_NE(&_ZNSt6locale2id7_Id_cntE_nid_postfix, nullptr);

    EXPECT_EQ(sizeof(_ZNSt5ctypeIcE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt5ctypeIwE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix),
              8u);
    EXPECT_EQ(sizeof(_ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix),
              12 * sizeof(std::uintptr_t));
    EXPECT_EQ(sizeof(_ZNSt7collateIwE2idE_nid_postfix), 8u);
    EXPECT_EQ(sizeof(_ZNSt6locale2id7_Id_cntE_nid_postfix), 4u);

    EXPECT_EQ(_ZNSt5ctypeIcE2idE_nid_postfix, 0u);
    EXPECT_EQ(_ZNSt5ctypeIwE2idE_nid_postfix, 0u);
    EXPECT_EQ(_ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix, 0u);
    EXPECT_EQ(_ZNSt7collateIwE2idE_nid_postfix, 0u);
    EXPECT_EQ(_ZNSt6locale2id7_Id_cntE_nid_postfix, 0);
}

// The wrappers delegate to the same Loader instance and track its state.
TEST(LoaderVerbatimExport, DelegatesToLoaderInstance) {
    using namespace ::PortPS5::Config;
    Loader::ResetForTesting();
    EXPECT_FALSE(Loader::IsInitialized());
    EXPECT_FALSE(PortPS5_Config_Loader_IsInitialized_nid_no_patch());

    InitMinimalLoader();
    EXPECT_TRUE(Loader::IsInitialized());
    EXPECT_TRUE(PortPS5_Config_Loader_IsInitialized_nid_no_patch());
    EXPECT_EQ(&PortPS5_Config_Loader_Get_nid_no_patch(), &Loader::Get());
    EXPECT_EQ(PortPS5_Config_Loader_Get_nid_no_patch().installDir, "C:/game");

    Loader::ResetForTesting();
    EXPECT_FALSE(PortPS5_Config_Loader_IsInitialized_nid_no_patch());
}

// Uninitialized Get aborts exactly like Loader::Get (pass-through, no new logic).
TEST(LoaderVerbatimExport, UninitializedGetAborts) {
    ::PortPS5::Config::Loader::ResetForTesting();
    EXPECT_DEATH({ PortPS5_Config_Loader_Get_nid_no_patch(); }, "");
}
