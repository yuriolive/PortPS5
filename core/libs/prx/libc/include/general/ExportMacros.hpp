#ifndef CORE_LIBS_PRX_LIBC_INCLUDE_GENERAL_EXPORTMACROS_HPP
#define CORE_LIBS_PRX_LIBC_INCLUDE_GENERAL_EXPORTMACROS_HPP

#if defined(__GNUC__) || defined(__clang__)
    #define APS5_EXPORT(exportName, funcName) \
    __asm__(".globl \"" exportName "_nid_no_patch_cut\"\n\t" \
    ".set \"" exportName "_nid_no_patch_cut\", " #funcName)
#elif defined(_MSC_VER)
    #define APS5_EXPORT(exportName, funcName) \
    __pragma(comment(linker, "/export:" exportName "_nid_no_patch_cut=" #funcName))
#else
    #define APS5_EXPORT(exportName, funcName)
#endif

#include "VabiMacros.hpp"

// Declares a guest-reachable export with the System V ABI and noexcept, so no
// throw can cross the guest boundary (docs/spec/threading.md Error policy).
// Params must be parenthesized, e.g. APS5_EXPORT_FN(int, Foo, (void*, int)).
// The NID alias keeps flowing through the existing APS5_EXPORT mechanism, so
// export names stay byte-identical; this macro only guards the declaration.
// Definitions must repeat noexcept (it is part of the function type).
#if (defined(__GNUC__) || defined(__clang__)) && defined(_WIN32)
    #define APS5_EXPORT_FN(Ret, Name, Params) \
    extern "C" Ret APS5_VABI Name Params noexcept; \
    static_assert(!__is_same(decltype(&Name), Ret(*) Params noexcept), \
        #Name " must carry APS5_VABI (sysv_abi is part of the function type)")
#elif defined(__GNUC__) || defined(__clang__)
    #define APS5_EXPORT_FN(Ret, Name, Params) \
    extern "C" Ret APS5_VABI Name Params noexcept;
#elif defined(_MSC_VER)
    #define APS5_EXPORT_FN(Ret, Name, Params) \
    extern "C" Ret Name Params noexcept;
#else
    #define APS5_EXPORT_FN(Ret, Name, Params) \
    extern "C" Ret Name Params noexcept;
#endif

#endif
