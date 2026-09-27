#include <array>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <ios>
#include <locale>
#include <memory>

#include "prx/libc/include/General.hpp"
#include "prx/libc/include/ApplicationHeap.hpp"

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

int APS5_VABI isupper_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::upper); }
int APS5_VABI islower_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::lower); }
int APS5_VABI isalpha_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::alpha); }
int APS5_VABI isdigit_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::digit); }
int APS5_VABI isalnum_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::alnum); }
int APS5_VABI isspace_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::space); }
int APS5_VABI isblank_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::blank); }
int APS5_VABI iscntrl_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::cntrl); }
int APS5_VABI isprint_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::print); }
int APS5_VABI isgraph_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::graph); }
int APS5_VABI ispunct_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::punct); }
int APS5_VABI isxdigit_nid_postfix(int c) { return ClassifyCharacter(c, std::ctype_base::xdigit); }
int APS5_VABI toupper_nid_postfix(int c) { return ConvertCharacter(c, true); }
int APS5_VABI tolower_nid_postfix(int c) { return ConvertCharacter(c, false); }

std::streamoff _ZSt7_BADOFF_nid_postfix = -1;
std::fpos_t _ZSt4_Fpz_nid_postfix{};

const std::locale* _ZSt21_sceLibcClassicLocale_nid_postfix = &std::locale::classic();

void APS5_VABI _ZNSt8ios_baseD2Ev_nid_postfix(std::ios_base* self) {
    if (self == nullptr) throw std::invalid_argument("ios_base destructor: null object");
    self->std::ios_base::~ios_base();
}

const std::locale* APS5_VABI _ZNSt6locale5_InitEv_nid_postfix() {
    return &std::locale::classic();
}

void APS5_VABI _ZNSt6locale5facet9_RegisterEv_nid_postfix(std::locale::facet*) {
    NotImplemented_nid_no_patch("locale::facet::_Register");
}

const std::locale* APS5_VABI _ZNSt6locale16_GetgloballocaleEv_nid_postfix() {
    thread_local std::locale locale;
    locale = std::locale();
    return &locale;
}

void APS5_VABI _ZNSt7collateIwE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(const std::locale::facet**, const std::locale*) {
    NotImplemented_nid_no_patch("collate<wchar_t>::_Getcat");
}

void APS5_VABI _ZNSt8_LocinfoC1EPKc_nid_postfix(std::locale* self, const char* localeName) {
    if (self == nullptr || localeName == nullptr) throw std::invalid_argument("Locale constructor: null argument");
    std::construct_at(self, localeName);
}

void APS5_VABI _ZNSt8_LocinfoD1Ev_nid_postfix(std::locale* self) {
    if (self == nullptr) throw std::invalid_argument("Locale destructor: null object");
    std::destroy_at(self);
}

std::size_t APS5_VABI _Mbtowcx_nid_postfix(wchar_t* dst, const char* src, std::size_t count, std::mbstate_t* state) {
    if (dst == nullptr || src == nullptr || state == nullptr || count == 0) throw std::invalid_argument("_Mbtowcx: invalid conversion arguments");
    const auto result = std::mbrtowc(dst, src, count, state);
    if (result == static_cast<std::size_t>(-1)) throw std::runtime_error("_Mbtowcx: invalid multibyte character");
    if (result == static_cast<std::size_t>(-2)) throw std::runtime_error("_Mbtowcx: incomplete multibyte character");
    return result;
}

std::size_t APS5_VABI _Wctombx_nid_postfix(char* dst, wchar_t src, std::mbstate_t* state) {
    if (dst == nullptr || state == nullptr) throw std::invalid_argument("_Wctombx: invalid conversion arguments");
    const auto result = std::wcrtomb(dst, src, state);
    if (result == static_cast<std::size_t>(-1)) throw std::runtime_error("_Wctombx: invalid wide character");
    return result;
}

const std::ctype<char>::mask* APS5_VABI _Getpctype_nid_postfix() {
    return std::ctype<char>::classic_table();
}

const int* APS5_VABI _Getptolower_nid_postfix() {
    static const auto table = MakeCaseTable(false);
    return table.data() + 1;
}

const int* APS5_VABI _Getptoupper_nid_postfix() {
    static const auto table = MakeCaseTable(true);
    return table.data() + 1;
}

std::mbstate_t* APS5_VABI _Getpmbstate_nid_postfix() {
    thread_local std::mbstate_t state{};
    return &state;
}

std::mbstate_t* APS5_VABI _Getpwcstate_nid_postfix() {
    thread_local std::mbstate_t state{};
    return &state;
}

std::wint_t APS5_VABI _Towctrans_nid_postfix(std::wint_t c, std::wctrans_t desc) {
    if (desc == std::wctrans_t{}) throw std::invalid_argument("_Towctrans: invalid transformation");
    return std::towctrans(c, desc);
}

void APS5_VABI _init_env_nid_postfix() {
    ApplicationHeapInitialize_nid_no_patch(ApplicationProcessParameters_nid_no_patch());
}

}
