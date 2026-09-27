# cmake/PortPS5GTest.cmake
# GoogleTest (GTest + GMock) integration via FetchContent

include(FetchContent)

set(BUILD_GMOCK ON CACHE BOOL "Build GMock" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "Install GTest" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build GTest static" FORCE)

# WinLibs MinGW toolchain does not package root CA bundles for GnuTLS curl.
# Cryptographic integrity is verified by the pinned SHA-256 URL_HASH below.
set(CMAKE_TLS_VERIFY OFF CACHE BOOL "Disable TLS verification when toolchain lacks CA bundle" FORCE)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    URL_HASH SHA256=7b42b4d6ed48810c5362c265a17faebe90dc2373c885e5216439d37927f02926
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    TLS_VERIFY OFF
)

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# Helper function for registering GoogleTest executables into CTest
function(portps5_add_gtest target)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES;INCLUDES;LABELS;ARGS" ${ARGN})

    if(NOT BUILD_TESTING)
        return()
    endif()

    add_executable(${target} ${ARG_SOURCES})
    target_include_directories(${target} PRIVATE
        ${ARG_INCLUDES}
        ${CMAKE_SOURCE_DIR}/tests
        ${CMAKE_SOURCE_DIR}/core/libs
    )

    target_link_libraries(${target} PRIVATE
        ${ARG_LIBRARIES}
        GTest::gtest
        GTest::gmock
        GTest::gtest_main
    )

    if(MINGW)
        target_link_options(${target} PRIVATE "-Wl,--allow-multiple-definition")
    endif()

    if(COMMAND configure_windows_unwind)
        configure_windows_unwind(${target})
    endif()

    set_target_properties(${target} PROPERTIES EXCLUDE_FROM_ALL OFF)
    if(WIN32 AND TARGET libc)
        set_target_properties(${target} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:libc>"
        )
    endif()

    if(ARG_LABELS)
        set(TEST_LABELS "${ARG_LABELS}")
    else()
        set(TEST_LABELS "unit")
    endif()

    gtest_discover_tests(${target}
        PROPERTIES
            LABELS "${TEST_LABELS}"
        DISCOVERY_MODE PRE_TEST
    )
endfunction()
