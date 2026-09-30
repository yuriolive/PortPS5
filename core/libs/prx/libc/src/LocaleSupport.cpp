// Libc locale, character classification, and Dinkumware/libstdc++ ABI support.
// Implements ctype classification/conversion tables and standard locale facets.
// Subsystem: libc. Exports use System V ABI (APS5_VABI) for guest runtime compatibility.

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <ios>
#include <iostream>
#include <locale>
#include <memory>

#include "prx/libc/include/General.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"
#include "prx/libc/include/LocaleSupport.hpp"

namespace {

void ValidateCharacter(int value) {
    if (value != EOF && (value < 0 || value > UCHAR_MAX)) throw std::invalid_argument("Invalid character value");
}

int ClassifyCharacter(int value, std::ctype_base::mask mask) {
    ValidateCharacter(value);
    if (value == EOF) return 0;
    return std::use_facet<std::ctype<char>>(std::locale::classic()).is(mask, static_cast<char>(value));
}

int ConvertCharacter(int value, bool upper) {
    ValidateCharacter(value);
    if (value == EOF) return EOF;
    const auto& facet = std::use_facet<std::ctype<char>>(std::locale::classic());
    const auto character = static_cast<char>(value);
    return static_cast<unsigned char>(upper ? facet.toupper(character) : facet.tolower(character));
}

std::array<int, UCHAR_MAX + 2> MakeCaseTable(bool upper) {
    std::array<int, UCHAR_MAX + 2> table{};
    table[0] = EOF;
    for (int value = 0; value <= UCHAR_MAX; ++value) table[value + 1] = ConvertCharacter(value, upper);
    return table;
}

}

extern "C" {

/// Checks whether character is uppercase.
int APS5_VABI isupper_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::upper); }
/// Checks whether character is lowercase.
int APS5_VABI islower_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::lower); }
/// Checks whether character is alphabetic.
int APS5_VABI isalpha_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::alpha); }
/// Checks whether character is a decimal digit.
int APS5_VABI isdigit_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::digit); }
/// Checks whether character is alphanumeric.
int APS5_VABI isalnum_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::alnum); }
/// Checks whether character is whitespace.
int APS5_VABI isspace_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::space); }
/// Checks whether character is blank.
int APS5_VABI isblank_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::blank); }
/// Checks whether character is a control character.
int APS5_VABI iscntrl_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::cntrl); }
/// Checks whether character is printable.
int APS5_VABI isprint_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::print); }
/// Checks whether character has a graphical representation.
int APS5_VABI isgraph_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::graph); }
/// Checks whether character is punctuation.
int APS5_VABI ispunct_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::punct); }
/// Checks whether character is a hexadecimal digit.
int APS5_VABI isxdigit_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::xdigit); }
/// Converts character to uppercase.
int APS5_VABI toupper_nid_postfix(int c) { return ConvertCharacter(c, true); }
/// Converts character to lowercase.
int APS5_VABI tolower_nid_postfix(int c) { return ConvertCharacter(c, false); }

std::streamoff _ZSt7_BADOFF_nid_postfix = -1;
std::fpos_t _ZSt4_Fpz_nid_postfix{};

// Why zero-initialized libstdc++ ABI data, and why no APS5_VABI (data, not
// functions): game NIDs hash these facet-id / vtable / counter names, so the
// _nid_postfix spellings below must hash to the title's NIDs. Adapted from
// AnyPS5 core/libs/prx/libc/src/LocaleSupport.cpp:143-154 @75a8668 (first in
// 9ac0798); declarations only, never upstream's GuestLocale shim, because
// PortPS5 uses real std::locale. Linkable everywhere, lethal only if guest
// code virtual-calls through them (documented limitation, identical for
// every title, hence no title-specific branch).
std::uint64_t _ZNSt5ctypeIcE2idE_nid_postfix = 0;  // ctype<char>::id (Cv+zC4EjGMA).
std::uint64_t _ZNSt5ctypeIwE2idE_nid_postfix = 0;  // ctype<wchar_t>::id (VmqsS6auJzo).
std::uint64_t _ZNSt7collateIcE2idE_nid_postfix = 0;  // collate<char>::id (7brRfHVVAlI).
std::uint64_t _ZNSt7collateIwE2idE_nid_postfix = 0;  // collate<wchar_t>::id (irGo1yaJ-vM).
std::uint64_t _ZNSt7codecvtIcc9_MbstatetE2idE_nid_postfix = 0;  // codecvt<char, char, mbstate_t>::id (eVFYZnYNDo0).
std::uintptr_t _ZTVSt7codecvtIcc9_MbstatetE_nid_postfix[16]{};  // codecvt<char, char, mbstate_t> vtable (aK1Ymf-NhAs).
std::uint64_t _ZNSt7codecvtIwc9_MbstatetE2idE_nid_postfix = 0;  // codecvt<wchar_t, char, mbstate_t>::id (FjZCPmK0SbA).
std::uint64_t _ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix =
    0;  // num_put id (E14mW8pVpoE).
std::uintptr_t _ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix[12]{};  // num_put vtable (1kZFcktOm+s).
std::int32_t _ZNSt6locale2id7_Id_cntE_nid_postfix = 0;  // locale::_Id_cnt (H4fcpQOpc08).

const std::locale* _ZSt21_sceLibcClassicLocale_nid_postfix = &std::locale::classic();

alignas(16) unsigned char _ZSt4cout_nid_postfix[0x400]{};
alignas(16) unsigned char _ZSt4cerr_nid_postfix[0x400]{};
alignas(16) unsigned char _ZSt3cin_nid_postfix[0x400]{};
alignas(16) unsigned char _ZSt5wcout_nid_postfix[0x400]{};
alignas(16) unsigned char _ZSt5wcerr_nid_postfix[0x400]{};
alignas(16) unsigned char _ZSt4wcin_nid_postfix[0x400]{};

static_assert(sizeof(std::ostream) <= sizeof(_ZSt4cout_nid_postfix), "std::ostream buffer too small");
static_assert(sizeof(std::ostream) <= sizeof(_ZSt4cerr_nid_postfix), "std::ostream buffer too small");
static_assert(sizeof(std::istream) <= sizeof(_ZSt3cin_nid_postfix), "std::istream buffer too small");
static_assert(sizeof(std::wostream) <= sizeof(_ZSt5wcout_nid_postfix), "std::wostream buffer too small");
static_assert(sizeof(std::wostream) <= sizeof(_ZSt5wcerr_nid_postfix), "std::wostream buffer too small");
static_assert(sizeof(std::wistream) <= sizeof(_ZSt4wcin_nid_postfix), "std::wistream buffer too small");
static_assert(alignof(std::ostream) <= 16, "std::ostream alignment exceeds buffer alignment");

namespace {

struct StandardStreamsInitializer {
    StandardStreamsInitializer() {
        new (_ZSt4cout_nid_postfix) std::ostream(std::cout.rdbuf());
        new (_ZSt4cerr_nid_postfix) std::ostream(std::cerr.rdbuf());
        new (_ZSt3cin_nid_postfix) std::istream(std::cin.rdbuf());
        new (_ZSt5wcout_nid_postfix) std::wostream(std::wcout.rdbuf());
        new (_ZSt5wcerr_nid_postfix) std::wostream(std::wcerr.rdbuf());
        new (_ZSt4wcin_nid_postfix) std::wistream(std::wcin.rdbuf());
    }
};

const StandardStreamsInitializer g_standardStreamsInitializer;

}  // namespace

/// Destructs std::ios_base instance.
void APS5_VABI _ZNSt8ios_baseD2Ev_nid_postfix(std::ios_base* self) {
    if (self == nullptr) throw std::invalid_argument("ios_base destructor: null object");
    self->std::ios_base::~ios_base();
}

/// Initializes and returns the classic locale instance.
const std::locale* APS5_VABI _ZNSt6locale5_InitEv_nid_postfix() {
    return &std::locale::classic();
}

/// Registers a locale facet with the runtime and increments the facet ID counter.
void APS5_VABI _ZNSt6locale5facet9_RegisterEv_nid_postfix(std::locale::facet* facet) {
    if (facet == nullptr) {
        return;
    }
    ++_ZNSt6locale2id7_Id_cntE_nid_postfix;
}

/// Obtains current global locale reference.
const std::locale* APS5_VABI _ZNSt6locale16_GetgloballocaleEv_nid_postfix() {
    thread_local std::locale locale;
    locale = std::locale();
    return &locale;
}

/// Obtains character collation category facet pointer.
void APS5_VABI _ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(const std::locale::facet**, const std::locale*) {
    NotImplemented_nid_no_patch("collate<char>::_Getcat");
}

/// Obtains wide collation category facet pointer.
void APS5_VABI _ZNSt7collateIwE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(const std::locale::facet**, const std::locale*) {
    NotImplemented_nid_no_patch("collate<wchar_t>::_Getcat");
}

/// Constructs locale info descriptor.
void APS5_VABI _ZNSt8_LocinfoC1EPKc_nid_postfix(std::locale* self, const char* localeName) {
    if (self == nullptr || localeName == nullptr) throw std::invalid_argument("Locale constructor: null argument");
    std::construct_at(self, localeName);
}

/// Destructs locale info descriptor.
void APS5_VABI _ZNSt8_LocinfoD1Ev_nid_postfix(std::locale* self) {
    if (self == nullptr) throw std::invalid_argument("Locale destructor: null object");
    std::destroy_at(self);
}

/// Converts multibyte character sequence to wide characters.
std::size_t APS5_VABI _Mbtowcx_nid_postfix(wchar_t* dst, const char* src, std::size_t count, std::mbstate_t* state) {
    if (dst == nullptr || src == nullptr || state == nullptr || count == 0) throw std::invalid_argument("_Mbtowcx: invalid conversion arguments");
    const auto result = std::mbrtowc(dst, src, count, state);
    if (result == static_cast<std::size_t>(-1)) throw std::runtime_error("_Mbtowcx: invalid multibyte character");
    if (result == static_cast<std::size_t>(-2)) throw std::runtime_error("_Mbtowcx: incomplete multibyte character");
    return result;
}

/// Converts wide character to multibyte sequence.
std::size_t APS5_VABI _Wctombx_nid_postfix(char* dst, wchar_t src, std::mbstate_t* state) {
    if (dst == nullptr || state == nullptr) throw std::invalid_argument("_Wctombx: invalid conversion arguments");
    const auto result = std::wcrtomb(dst, src, state);
    if (result == static_cast<std::size_t>(-1)) throw std::runtime_error("_Wctombx: invalid wide character");
    return result;
}

/// Returns pointer to classic character classification mask table.
const std::ctype<char>::mask* APS5_VABI _Getpctype_nid_postfix() {
    return std::ctype<char>::classic_table();
}

/// Returns pointer to lowercase character conversion lookup table.
const int* APS5_VABI _Getptolower_nid_postfix() {
    static const auto table = MakeCaseTable(false);
    return table.data() + 1;
}

/// Returns pointer to uppercase character conversion lookup table.
const int* APS5_VABI _Getptoupper_nid_postfix() {
    static const auto table = MakeCaseTable(true);
    return table.data() + 1;
}

/// Returns thread-local multibyte shift conversion state.
std::mbstate_t* APS5_VABI _Getpmbstate_nid_postfix() {
    thread_local std::mbstate_t state{};
    return &state;
}

/// Returns thread-local wide character conversion state.
std::mbstate_t* APS5_VABI _Getpwcstate_nid_postfix() {
    thread_local std::mbstate_t state{};
    return &state;
}

/// Translates wide character according to character mapping descriptor.
std::wint_t APS5_VABI _Towctrans_nid_postfix(std::wint_t c, std::wctrans_t desc) {
    if (desc == std::wctrans_t{}) throw std::invalid_argument("_Towctrans: invalid transformation");
    return std::towctrans(c, desc);
}

/// Initializes runtime environment and application heap.
void APS5_VABI _init_env_nid_postfix() {
    ApplicationHeapInitialize_nid_no_patch(ApplicationProcessParameters_nid_no_patch());
}

}
