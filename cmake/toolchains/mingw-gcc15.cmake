# MinGW-w64 GCC 15.2.0 Toolchain File for PortPS5
# Target: x86_64-w64-mingw32 (ucrt-posix-seh)

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

# Compilers
if(NOT CMAKE_C_COMPILER)
    find_program(PORTPS5_C_COMPILER NAMES gcc x86_64-w64-mingw32-gcc)
    if(PORTPS5_C_COMPILER)
        set(CMAKE_C_COMPILER "${PORTPS5_C_COMPILER}" CACHE FILEPATH "C compiler")
    endif()
endif()

if(NOT CMAKE_CXX_COMPILER)
    find_program(PORTPS5_CXX_COMPILER NAMES g++ x86_64-w64-mingw32-g++)
    if(PORTPS5_CXX_COMPILER)
        set(CMAKE_CXX_COMPILER "${PORTPS5_CXX_COMPILER}" CACHE FILEPATH "C++ compiler")
    endif()
endif()

# GNU objcopy (required for .eh_frame -> .ehfram renaming)
find_program(CMAKE_OBJCOPY NAMES objcopy x86_64-w64-mingw32-objcopy)
if(CMAKE_OBJCOPY)
    set(CMAKE_OBJCOPY "${CMAKE_OBJCOPY}" CACHE FILEPATH "Path to objcopy")
endif()

# Deferred verification of compiler version and target architecture
cmake_language(DEFER CALL portps5_verify_toolchain)

function(portps5_verify_toolchain)
    if(NOT MINGW)
        message(FATAL_ERROR "PortPS5 requires MinGW toolchain on Windows. MSVC and clang-cl are unsupported due to sysv_abi requirements.")
    endif()

    if(CMAKE_CXX_COMPILER_VERSION)
        if(NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL "15.2.0")
            message(FATAL_ERROR "PortPS5 requires MinGW-w64 GCC 15.2.0, but found ${CMAKE_CXX_COMPILER_VERSION}. See docs/spec/build-toolchain.md.")
        endif()
    endif()

    if(CMAKE_CXX_COMPILER_TARGET AND NOT CMAKE_CXX_COMPILER_TARGET MATCHES "x86_64.*mingw")
        message(FATAL_ERROR "PortPS5 requires target triple x86_64-w64-mingw32, but found ${CMAKE_CXX_COMPILER_TARGET}.")
    endif()

    if(NOT CMAKE_OBJCOPY OR NOT EXISTS "${CMAKE_OBJCOPY}")
        message(FATAL_ERROR "Windows DWARF unwinding requires objcopy. Please ensure objcopy is in PATH or CMAKE_OBJCOPY is set.")
    endif()
endfunction()
