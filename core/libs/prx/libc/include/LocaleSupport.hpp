// core/libs/prx/libc/include/LocaleSupport.hpp
// Declarations of libc locale, character classification, and libstdc++ ABI fixtures.
// Subsystem: libc. Exports use System V ABI (APS5_VABI) for guest runtime compatibility.

#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_LOCALESUPPORT_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_LOCALESUPPORT_HPP

#include <cstddef>
#include <cstdint>
#include <cwchar>
#include <cwctype>
#include <ios>
#include <locale>

#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/general/ExportMacros.hpp"

extern "C" {

// Standard C++ runtime facet IDs and vtables (data exports)
extern std::uint64_t _ZNSt5ctypeIcE2idE_nid_postfix;
extern std::uint64_t _ZNSt5ctypeIwE2idE_nid_postfix;
extern std::uint64_t _ZNSt7collateIcE2idE_nid_postfix;
extern std::uint64_t _ZNSt7collateIwE2idE_nid_postfix;
extern std::uint64_t _ZNSt7codecvtIcc9_MbstatetE2idE_nid_postfix;
extern std::uintptr_t _ZTVSt7codecvtIcc9_MbstatetE_nid_postfix[16];
extern std::uint64_t _ZNSt7codecvtIwc9_MbstatetE2idE_nid_postfix;
extern std::uint64_t _ZNSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE2idE_nid_postfix;
extern std::uintptr_t _ZTVSt7num_putIcSt19ostreambuf_iteratorIcSt11char_traitsIcEEE_nid_postfix[12];
extern std::int32_t _ZNSt6locale2id7_Id_cntE_nid_postfix;

// Classic locale pointer and standard stream buffers
extern const std::locale* _ZSt21_sceLibcClassicLocale_nid_postfix;
alignas(16) extern unsigned char _ZSt4cout_nid_postfix[0x400];
alignas(16) extern unsigned char _ZSt4cerr_nid_postfix[0x400];
alignas(16) extern unsigned char _ZSt3cin_nid_postfix[0x400];
alignas(16) extern unsigned char _ZSt5wcout_nid_postfix[0x400];
alignas(16) extern unsigned char _ZSt5wcerr_nid_postfix[0x400];
alignas(16) extern unsigned char _ZSt4wcin_nid_postfix[0x400];

// Stream positioning sentinels
extern std::streamoff _ZSt7_BADOFF_nid_postfix;
extern std::fpos_t _ZSt4_Fpz_nid_postfix;

/**
 * @brief Checks whether character is uppercase.
 * @param c Character code to check.
 * @return Non-zero if character is uppercase, zero otherwise.
 */
int APS5_VABI isupper_nid_postfix(int c);

/**
 * @brief Checks whether character is lowercase.
 * @param c Character code to check.
 * @return Non-zero if character is lowercase, zero otherwise.
 */
int APS5_VABI islower_nid_postfix(int c);

/**
 * @brief Checks whether character is alphabetic.
 * @param c Character code to check.
 * @return Non-zero if character is alphabetic, zero otherwise.
 */
int APS5_VABI isalpha_nid_postfix(int c);

/**
 * @brief Checks whether character is a decimal digit.
 * @param c Character code to check.
 * @return Non-zero if character is a digit, zero otherwise.
 */
int APS5_VABI isdigit_nid_postfix(int c);

/**
 * @brief Checks whether character is alphanumeric.
 * @param c Character code to check.
 * @return Non-zero if character is alphanumeric, zero otherwise.
 */
int APS5_VABI isalnum_nid_postfix(int c);

/**
 * @brief Checks whether character is whitespace.
 * @param c Character code to check.
 * @return Non-zero if character is whitespace, zero otherwise.
 */
int APS5_VABI isspace_nid_postfix(int c);

/**
 * @brief Checks whether character is blank.
 * @param c Character code to check.
 * @return Non-zero if character is blank, zero otherwise.
 */
int APS5_VABI isblank_nid_postfix(int c);

/**
 * @brief Checks whether character is a control character.
 * @param c Character code to check.
 * @return Non-zero if character is a control character, zero otherwise.
 */
int APS5_VABI iscntrl_nid_postfix(int c);

/**
 * @brief Checks whether character is printable.
 * @param c Character code to check.
 * @return Non-zero if character is printable, zero otherwise.
 */
int APS5_VABI isprint_nid_postfix(int c);

/**
 * @brief Checks whether character has a graphical representation.
 * @param c Character code to check.
 * @return Non-zero if character has graphical representation, zero otherwise.
 */
int APS5_VABI isgraph_nid_postfix(int c);

/**
 * @brief Checks whether character is punctuation.
 * @param c Character code to check.
 * @return Non-zero if character is punctuation, zero otherwise.
 */
int APS5_VABI ispunct_nid_postfix(int c);

/**
 * @brief Checks whether character is a hexadecimal digit.
 * @param c Character code to check.
 * @return Non-zero if character is hexadecimal digit, zero otherwise.
 */
int APS5_VABI isxdigit_nid_postfix(int c);

/**
 * @brief Converts character to uppercase.
 * @param c Character code to convert.
 * @return Converted uppercase character or EOF.
 */
int APS5_VABI toupper_nid_postfix(int c);

/**
 * @brief Converts character to lowercase.
 * @param c Character code to convert.
 * @return Converted lowercase character or EOF.
 */
int APS5_VABI tolower_nid_postfix(int c);

/**
 * @brief Destructs std::ios_base instance.
 * @param self Pointer to std::ios_base object to destruct.
 */
void APS5_VABI _ZNSt8ios_baseD2Ev_nid_postfix(std::ios_base* self);

/**
 * @brief Initializes and returns the classic locale instance.
 * @return Pointer to classic std::locale singleton.
 */
const std::locale* APS5_VABI _ZNSt6locale5_InitEv_nid_postfix();

/**
 * @brief Registers a locale facet with the runtime and increments the facet ID counter.
 * @param facet Locale facet to register.
 */
void APS5_VABI _ZNSt6locale5facet9_RegisterEv_nid_postfix(std::locale::facet* facet);

/**
 * @brief Obtains current thread-local global locale reference.
 * @return Pointer to current std::locale instance.
 */
const std::locale* APS5_VABI _ZNSt6locale16_GetgloballocaleEv_nid_postfix();

/**
 * @brief Obtains character collation category facet pointer.
 * @param facet Output facet pointer.
 * @param loc Locale to retrieve facet from.
 * @return Locale collation category constant (_X_COLLATE) or (size_t)-1 on failure.
 */
std::size_t APS5_VABI _ZNSt7collateIcE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(const std::locale::facet** facet, const std::locale* loc);

/**
 * @brief Obtains wide character collation category facet pointer.
 * @param facet Output facet pointer.
 * @param loc Locale to retrieve facet from.
 * @return Locale collation category constant (_X_COLLATE) or (size_t)-1 on failure.
 */
std::size_t APS5_VABI _ZNSt7collateIwE7_GetcatEPPKNSt6locale5facetEPKS1__nid_postfix(const std::locale::facet** facet, const std::locale* loc);

/**
 * @brief Constructs locale info descriptor.
 * @param self Locale descriptor destination storage.
 * @param localeName Name of locale to construct.
 */
void APS5_VABI _ZNSt8_LocinfoC1EPKc_nid_postfix(std::locale* self, const char* localeName);

/**
 * @brief Destructs locale info descriptor.
 * @param self Pointer to locale descriptor to destroy.
 */
void APS5_VABI _ZNSt8_LocinfoD1Ev_nid_postfix(std::locale* self);

/**
 * @brief Converts multibyte character sequence to wide characters.
 * @param dst Destination buffer for wide characters.
 * @param src Source multibyte string.
 * @param count Maximum number of bytes to convert.
 * @param state Multibyte conversion state.
 * @return Number of bytes processed or -1 on error.
 */
std::size_t APS5_VABI _Mbtowcx_nid_postfix(wchar_t* dst, const char* src, std::size_t count, std::mbstate_t* state);

/**
 * @brief Converts wide character to multibyte sequence.
 * @param dst Destination multibyte buffer.
 * @param src Wide character to convert.
 * @param state Multibyte conversion state.
 * @return Number of bytes produced.
 */
std::size_t APS5_VABI _Wctombx_nid_postfix(char* dst, wchar_t src, std::mbstate_t* state);

/**
 * @brief Returns pointer to classic character classification mask table.
 * @return Pointer to classification mask table.
 */
const std::ctype<char>::mask* APS5_VABI _Getpctype_nid_postfix();

/**
 * @brief Returns pointer to lowercase character conversion lookup table.
 * @return Pointer to lowercase lookup table.
 */
const int* APS5_VABI _Getptolower_nid_postfix();

/**
 * @brief Returns pointer to uppercase character conversion lookup table.
 * @return Pointer to uppercase lookup table.
 */
const int* APS5_VABI _Getptoupper_nid_postfix();

/**
 * @brief Returns thread-local multibyte shift conversion state.
 * @return Pointer to thread-local mbstate_t.
 */
std::mbstate_t* APS5_VABI _Getpmbstate_nid_postfix();

/**
 * @brief Returns thread-local wide character conversion state.
 * @return Pointer to thread-local mbstate_t for wide characters.
 */
std::mbstate_t* APS5_VABI _Getpwcstate_nid_postfix();

/**
 * @brief Translates wide character according to character mapping descriptor.
 * @param c Wide character to translate.
 * @param desc Character mapping descriptor.
 * @return Translated wide character.
 */
std::wint_t APS5_VABI _Towctrans_nid_postfix(std::wint_t c, std::wctrans_t desc);

/**
 * @brief Initializes runtime environment and application heap.
 */
void APS5_VABI _init_env_nid_postfix();

} // extern "C"

#endif // CORE_LIBS_PRX_LIBC_INCLUDE_LOCALESUPPORT_HPP
