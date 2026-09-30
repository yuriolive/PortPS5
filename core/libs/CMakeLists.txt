if(NOT WIN32)
    string(REPLACE "-static " "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ")
    string(STRIP "${CMAKE_EXE_LINKER_FLAGS}" CMAKE_EXE_LINKER_FLAGS)
endif()

add_executable(nid_patcher
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/Sha1.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/NidCompute.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/NidResolver.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/ExportExclusions.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/ElfNidPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/PeNidPatcher.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/BinaryPatcherFactory.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/nid/src/main.cpp
)
target_include_directories(nid_patcher PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/nid/include)

if(MINGW)
    add_compile_options(-fno-asynchronous-unwind-tables)
endif()

function(configure_windows_unwind target)
    if(MINGW)
        if(NOT EXISTS "${CMAKE_OBJCOPY}")
            message(FATAL_ERROR "Windows DWARF unwinding requires objcopy")
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
                COMMAND "${CMAKE_OBJCOPY}" --rename-section .eh_frame=.ehfram "$<TARGET_FILE:${target}>"
                VERBATIM
        )
    endif()
endfunction()

set(LIBC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/prx/libc)
add_library(libc SHARED EXCLUDE_FROM_ALL)
set(LIBS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
# Vendored single-header dependencies (docs/spec/build-toolchain.md). The
# directory itself is on the path so includes read tomlplusplus/toml.hpp.
set(PORTPS5_THIRDPARTY_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../3rdparty)
add_subdirectory(${LIBC_DIR} ${CMAKE_CURRENT_BINARY_DIR}/prx/libc)
target_include_directories(libc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
configure_windows_unwind(libc)

set(UNPATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/libs/unpatched)
set(PATCHED_DIR ${CMAKE_CURRENT_BINARY_DIR}/libs)

set_target_properties(libc PROPERTIES
        PREFIX ""
        SUFFIX .prx
        RUNTIME_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
        LIBRARY_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
)

set(LIBC_UNPATCHED ${UNPATCHED_DIR}/libc.prx)
set(LIBC_PATCHED ${PATCHED_DIR}/libc.prx)

add_custom_command(
        OUTPUT ${LIBC_PATCHED}
        COMMAND ${CMAKE_COMMAND} -E copy ${LIBC_UNPATCHED} ${LIBC_PATCHED}
        COMMAND $<TARGET_FILE:nid_patcher> libc ${LIBC_PATCHED}
        DEPENDS nid_patcher libc ${LIBC_UNPATCHED}
        COMMENT "Patching NIDs for libc"
)

file(GLOB LIBS_DIRS LIST_DIRECTORIES true ${CMAKE_CURRENT_SOURCE_DIR}/prx/*)
list(REMOVE_ITEM LIBS_DIRS ${LIBC_DIR})

set(PATCHED_OUTPUTS ${LIBC_PATCHED})

foreach(LIB_DIR ${LIBS_DIRS})
    if(IS_DIRECTORY ${LIB_DIR})
        get_filename_component(LIB_NAME ${LIB_DIR} NAME)

        if(EXISTS ${LIB_DIR}/CMakeLists.txt)
            set(LIBS_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
            add_subdirectory(${LIB_DIR} ${CMAKE_CURRENT_BINARY_DIR}/prx/${LIB_NAME})
        else()
            add_library(${LIB_NAME} SHARED EXCLUDE_FROM_ALL ${LIB_DIR}/Export.cpp)
            target_include_directories(${LIB_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
            configure_windows_unwind(${LIB_NAME})
        endif()

        if(LIB_NAME STREQUAL "libkernel")
            target_link_libraries(${LIB_NAME} PRIVATE libc)
        else()
            target_link_libraries(${LIB_NAME} PRIVATE libkernel libc)
        endif()

        set_target_properties(${LIB_NAME} PROPERTIES
                PREFIX ""
                SUFFIX .prx
                RUNTIME_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
                LIBRARY_OUTPUT_DIRECTORY ${UNPATCHED_DIR}
        )

        set(UNPATCHED_FILE ${UNPATCHED_DIR}/${LIB_NAME}.prx)
        set(PATCHED_FILE ${PATCHED_DIR}/${LIB_NAME}.prx)

        add_custom_command(
                OUTPUT ${PATCHED_FILE}
                COMMAND ${CMAKE_COMMAND} -E copy ${UNPATCHED_FILE} ${PATCHED_FILE}
                COMMAND $<TARGET_FILE:nid_patcher> ${LIB_NAME} --preserve-exports "${LIBC_UNPATCHED}" "${PATCHED_FILE}"
                DEPENDS nid_patcher ${LIB_NAME} ${UNPATCHED_FILE} ${LIBC_UNPATCHED}
                COMMENT "Patching NIDs for ${LIB_NAME}"
        )

        list(APPEND PATCHED_OUTPUTS ${PATCHED_FILE})
    endif()
endforeach()

add_custom_target(libs ALL DEPENDS ${PATCHED_OUTPUTS})

if(WIN32 AND MINGW)
    get_filename_component(MINGW_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET libs POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
            "${PATCHED_DIR}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MINGW_BIN_DIR}/libgcc_s_seh-1.dll"
            "${MINGW_BIN_DIR}/libstdc++-6.dll"
            "${MINGW_BIN_DIR}/libwinpthread-1.dll"
            "${UNPATCHED_DIR}"
        COMMENT "Copying MinGW runtime DLLs to patched and unpatched libs directories"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    add_executable(guest_rendering_wait_tests tests/GuestRenderingWait.cpp)
    target_include_directories(guest_rendering_wait_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_rendering_wait_tests PRIVATE libSceAgcDriver libc)
    configure_windows_unwind(guest_rendering_wait_tests)
    add_test(NAME guest_rendering_wait COMMAND guest_rendering_wait_tests)
    if(WIN32)
        set_tests_properties(guest_rendering_wait PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_buffer_reuse_tests tests/GuestBufferReuse.cpp)
    target_include_directories(guest_buffer_reuse_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_buffer_reuse_tests PRIVATE libc)
    configure_windows_unwind(guest_buffer_reuse_tests)
    add_test(NAME guest_buffer_reuse COMMAND guest_buffer_reuse_tests)
    if(WIN32)
        set_tests_properties(guest_buffer_reuse PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_shader_link_tests tests/GuestShaderLink.cpp)
    target_include_directories(guest_shader_link_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_shader_link_tests PRIVATE libSceAgc libc)
    configure_windows_unwind(guest_shader_link_tests)
    add_test(NAME guest_shader_link COMMAND guest_shader_link_tests)
    if(WIN32)
        set_tests_properties(guest_shader_link PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_system_exit_tests tests/GuestSystemExit.cpp)
    target_include_directories(guest_system_exit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_system_exit_tests PRIVATE libSceSystemService libc)
    configure_windows_unwind(guest_system_exit_tests)
    add_test(NAME guest_system_exit_parameters COMMAND guest_system_exit_tests)
    add_test(NAME guest_system_exit COMMAND guest_system_exit_tests --exit)
    add_test(NAME guest_immediate_exit COMMAND guest_system_exit_tests --immediate)
    set_tests_properties(guest_system_exit PROPERTIES
        PASS_REGULAR_EXPRESSION "Guest shutdown and atexit completed")
    if(WIN32)
        set_tests_properties(guest_system_exit guest_system_exit_parameters guest_immediate_exit PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_character_tests tests/GuestCharacter.cpp)
    target_include_directories(guest_character_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_character_tests PRIVATE libc)
    configure_windows_unwind(guest_character_tests)
    add_test(NAME guest_character COMMAND guest_character_tests)
    if(WIN32)
        set_tests_properties(guest_character PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_math_tests tests/GuestMath.cpp)
    target_include_directories(guest_math_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_math_tests PRIVATE libc)
    configure_windows_unwind(guest_math_tests)
    add_test(NAME guest_math COMMAND guest_math_tests)
    if(WIN32)
        set_tests_properties(guest_math PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_working_directory_tests tests/GuestWorkingDirectory.cpp)
    target_include_directories(guest_working_directory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_working_directory_tests PRIVATE libc)
    configure_windows_unwind(guest_working_directory_tests)
    add_test(NAME guest_working_directory COMMAND guest_working_directory_tests)
    if(WIN32)
        set_tests_properties(guest_working_directory PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_library(guest_module_fixture SHARED tests/GuestModuleFixture.cpp)
    target_include_directories(guest_module_fixture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_custom_command(TARGET guest_module_fixture POST_BUILD
        COMMAND $<TARGET_FILE:nid_patcher> guest_module_fixture $<TARGET_FILE:guest_module_fixture>
        VERBATIM)
    add_dependencies(guest_module_fixture nid_patcher)
    add_executable(guest_dynamic_loader_tests tests/GuestDynamicLoader.cpp)
    target_include_directories(guest_dynamic_loader_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_dynamic_loader_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_dynamic_loader_tests)
    add_dependencies(guest_dynamic_loader_tests guest_module_fixture)
    add_test(NAME guest_dynamic_loader COMMAND guest_dynamic_loader_tests $<TARGET_FILE_NAME:guest_module_fixture>)
    set_tests_properties(guest_dynamic_loader PROPERTIES
        WORKING_DIRECTORY "$<TARGET_FILE_DIR:guest_module_fixture>")
    if(WIN32)
        set_tests_properties(guest_dynamic_loader PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_filesystem_tests tests/GuestFilesystem.cpp)
    target_include_directories(guest_filesystem_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_filesystem_tests PRIVATE libc)
    configure_windows_unwind(guest_filesystem_tests)
    add_test(NAME guest_filesystem COMMAND guest_filesystem_tests)
    if(WIN32)
        set_tests_properties(guest_filesystem PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_directory_tests tests/GuestDirectory.cpp)
    target_include_directories(guest_directory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_directory_tests PRIVATE libc)
    configure_windows_unwind(guest_directory_tests)
    add_test(NAME guest_directory COMMAND guest_directory_tests)
    if(WIN32)
        set_tests_properties(guest_directory PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_environment_tests tests/GuestEnvironment.cpp)
    target_include_directories(guest_environment_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_environment_tests PRIVATE libc)
    configure_windows_unwind(guest_environment_tests)
    add_test(NAME guest_environment COMMAND guest_environment_tests)
    if(WIN32)
        set_tests_properties(guest_environment PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_standard_stream_tests tests/GuestStandardStreams.cpp)
    target_include_directories(guest_standard_stream_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_standard_stream_tests PRIVATE libc)
    configure_windows_unwind(guest_standard_stream_tests)
    add_test(NAME guest_standard_streams COMMAND guest_standard_stream_tests)
    if(WIN32)
        set_tests_properties(guest_standard_streams PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_once_tests tests/GuestOnce.cpp)
    target_include_directories(guest_once_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_once_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_once_tests)
    add_test(NAME guest_once COMMAND guest_once_tests)
    set_tests_properties(guest_once PROPERTIES TIMEOUT 20)
    if(WIN32)
        set_tests_properties(guest_once PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_signal_tests tests/GuestSignals.cpp)
    target_include_directories(guest_signal_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_signal_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_signal_tests)
    add_test(NAME guest_signals COMMAND guest_signal_tests)
    if(WIN32)
        set_tests_properties(guest_signals PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_system_configuration_tests tests/GuestSystemConfiguration.cpp)
    target_include_directories(guest_system_configuration_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_system_configuration_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_system_configuration_tests)
    add_test(NAME guest_system_configuration COMMAND guest_system_configuration_tests)
    if(WIN32)
        set_tests_properties(guest_system_configuration PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_udp_tests tests/GuestUdp.cpp)
    target_include_directories(guest_udp_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_udp_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_udp_tests)
    add_test(NAME guest_udp COMMAND guest_udp_tests)
    set_tests_properties(guest_udp PROPERTIES TIMEOUT 20)
    if(WIN32)
        set_tests_properties(guest_udp PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_address_tests tests/GuestAddressConversion.cpp)
    target_include_directories(guest_address_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_address_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_address_tests)
    add_test(NAME guest_addresses COMMAND guest_address_tests)
    if(WIN32)
        set_tests_properties(guest_addresses PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_error_tests tests/GuestErrors.cpp)
    target_include_directories(guest_error_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_error_tests PRIVATE libc)
    configure_windows_unwind(guest_error_tests)
    add_test(NAME guest_errors COMMAND guest_error_tests)
    if(WIN32)
        set_tests_properties(guest_errors PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_random_tests tests/GuestRandom.cpp)
    target_include_directories(guest_random_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_random_tests PRIVATE libSceRandom)
    add_test(NAME guest_random COMMAND guest_random_tests)
    if(WIN32)
        set_tests_properties(guest_random PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceRandom>")
    endif()
    add_executable(guest_resolver_tests tests/GuestResolver.cpp)
    target_include_directories(guest_resolver_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_resolver_tests PRIVATE libScePosixForWebKit)
    add_test(NAME guest_resolver COMMAND guest_resolver_tests)
    if(WIN32)
        set_tests_properties(guest_resolver PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libScePosixForWebKit>")
    endif()
    add_executable(guest_rtc_tests tests/GuestRtc.cpp)
    target_include_directories(guest_rtc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_rtc_tests PRIVATE libSceRtc)
    add_test(NAME guest_rtc COMMAND guest_rtc_tests)
    if(WIN32)
        set_tests_properties(guest_rtc PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libSceRtc>")
    endif()
    # Mspace allocator invariants, return codes and randomized stress (GoogleTest).
    portps5_add_gtest(mspace_tests
        SOURCES  tests/Mspace.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
        LIBRARIES libc
        LABELS   unit
    )
    # Lifecycle exports (cxa_atexit/finalize, thread atexit, init_env).
    portps5_add_gtest(guest_lifecycle_tests
        SOURCES  tests/GuestLifecycle.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
        LIBRARIES libc libSceLibcInternal
        LABELS   unit
    )
    add_executable(guest_memory_tests tests/GuestMemory.cpp)
    target_include_directories(guest_memory_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_memory_tests PRIVATE libkernel libc)
    configure_windows_unwind(guest_memory_tests)
    add_test(NAME guest_memory COMMAND guest_memory_tests)
    if(WIN32)
        set_tests_properties(guest_memory PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libkernel>")
    endif()
    add_executable(guest_search_tests tests/GuestSearch.cpp)
    target_include_directories(guest_search_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_search_tests PRIVATE libc)
    configure_windows_unwind(guest_search_tests)
    add_test(NAME guest_search COMMAND guest_search_tests)
    if(WIN32)
        set_tests_properties(guest_search PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    add_executable(guest_string_tests tests/GuestStrings.cpp)
    target_include_directories(guest_string_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(guest_string_tests PRIVATE libc libScePosixForWebKit)
    configure_windows_unwind(guest_string_tests)
    add_test(NAME guest_strings COMMAND guest_string_tests)
    if(WIN32)
        set_tests_properties(guest_strings PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()
    if(NOT WIN32)
        add_executable(exception_runtime_tests tests/ExceptionRuntime.cpp)
        target_include_directories(exception_runtime_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
        target_link_libraries(exception_runtime_tests PRIVATE libc)
        configure_windows_unwind(exception_runtime_tests)
        add_test(NAME exception_runtime COMMAND exception_runtime_tests)
    endif()
    # application_heap_tests tests ApplicationHeap allocator routines in isolation.
    # Note: tests/ApplicationHeap.cpp defines callbacks using APS5_VABI (__attribute__((sysv_abi))).
    # MinGW GCC cannot generate Windows SEH unwind information for sysv_abi functions, requiring
    # -fno-asynchronous-unwind-tables (DWARF unwinding). This in turn references __gxx_personality_v0,
    # which is exported by libc.prx. Hence application_heap_tests links libc and configures Windows unwinding.
    add_executable(config_tests tests/Config.cpp)
    target_include_directories(config_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(config_tests PRIVATE libc)
    configure_windows_unwind(config_tests)
    portps5_add_test(config config_tests)
    # LocaleNids pins the six libc locale/iostream NID data exports (hash,
    # linkage, size, zero-init) plus the verbatim Loader wrappers, including
    # the uninitialized-Get abort as a death test (config_tests cannot abort
    # in-process). Uses real Nid::ComputeNid, so the NID sources compile in.
    portps5_add_gtest(locale_nid_tests
        SOURCES  tests/LocaleNids.cpp
                 nid/src/NidCompute.cpp
                 nid/src/Sha1.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_SOURCE_DIR}/nid/include
        LIBRARIES libc
        LABELS   unit
    )
    # WriteTracker suites are GoogleTest (portps5_add_gtest): the IWriteTracker
    # contract plus WriteWatchTracker/page-state behavior (TEST invariant
    # comments per tools/check_comments.py rule 3).
    portps5_add_gtest(write_tracker_tests
        SOURCES
            tests/WriteTracker.cpp
        LIBRARIES
            libc
        INCLUDES
            ${CMAKE_CURRENT_SOURCE_DIR}
        LABELS
            unit
    )
    add_executable(application_heap_tests tests/ApplicationHeap.cpp prx/libc/src/ApplicationHeap.cpp)
    target_include_directories(application_heap_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(application_heap_tests PROPERTIES CXX_EXTENSIONS OFF)
    target_link_libraries(application_heap_tests PRIVATE libc)
    configure_windows_unwind(application_heap_tests)
    add_test(NAME application_heap COMMAND application_heap_tests)
    if(WIN32)
        set_tests_properties(application_heap PROPERTIES ENVIRONMENT_MODIFICATION
            "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
    endif()

    if(MINGW)
        foreach(runtimeLinkFlag IN ITEMS -static-libgcc -static-libstdc++ -static)
            string(REPLACE "${runtimeLinkFlag}" "" CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
        endforeach()
        add_executable(windows_exception_tests tests/WindowsExceptions.cpp)
        set_target_properties(windows_exception_tests PROPERTIES CXX_EXTENSIONS OFF)
        target_link_libraries(windows_exception_tests PRIVATE libc)
        configure_windows_unwind(windows_exception_tests)
        add_test(NAME windows_exception COMMAND windows_exception_tests)
        if(WIN32)
            set_tests_properties(windows_exception PROPERTIES ENVIRONMENT_MODIFICATION
                "PATH=path_list_prepend:$<TARGET_FILE_DIR:libc>")
        endif()
    endif()

    # --- slice threading: test registration ---
    add_executable(futex_sync_tests tests/FutexSync.cpp)
    target_include_directories(futex_sync_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(futex_sync_tests PRIVATE libkernel libc synchronization)
    configure_windows_unwind(futex_sync_tests)
    portps5_add_test(futex_sync futex_sync_tests)

    # GuestKernelErrors uses GTest cases (TEST / gtest_discover_tests) via the
    # portps5_add_gtest helper: equeue wait/delete and error-check mutex contracts.
    portps5_add_gtest(kernel_errors_tests
        SOURCES  tests/GuestKernelErrors.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
        LIBRARIES libkernel libc
        LABELS   unit
    )

    # GuestCondTimedwait / GuestThreadSelf use GTest cases via the
    # portps5_add_gtest helper: cond timedwait slices and thread identity.
    portps5_add_gtest(cond_timedwait_tests
        SOURCES  tests/GuestCondTimedwait.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
        LIBRARIES libkernel libc
        LABELS   unit
    )
    portps5_add_gtest(thread_self_tests
        SOURCES  tests/GuestThreadSelf.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
        LIBRARIES libkernel libc
        LABELS   unit
    )

    # HostThreadLocal exercises host TLS construction/destruction balance;
    # the Library TU provides the Touch/Destroyed counters in the same binary.
    portps5_add_gtest(host_thread_local_tests
        SOURCES  tests/HostThreadLocal.cpp
                 tests/HostThreadLocalLibrary.cpp
        INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
        LIBRARIES libc
        LABELS   unit
    )

    get_property(ALL_LIB_TESTS DIRECTORY PROPERTY TESTS)
    set_property(TEST ${ALL_LIB_TESTS} PROPERTY LABELS "unit")
# --- slice audio: test registration ---
add_executable(audio_out2_tests tests/AudioOut2.cpp)
target_include_directories(audio_out2_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include)
target_link_libraries(audio_out2_tests PRIVATE libSceAudioOut libc)
configure_windows_unwind(audio_out2_tests)
portps5_add_test(audio_out2 audio_out2_tests)
add_executable(audio_ajm_tests tests/AudioAjm.cpp)
target_include_directories(audio_ajm_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(audio_ajm_tests PRIVATE libSceAjm.native libc)
configure_windows_unwind(audio_ajm_tests)
portps5_add_test(audio_ajm audio_ajm_tests)

# --- slice offline: test registration ---
add_executable(offline_stubs_tests tests/OfflineStubs.cpp)
target_include_directories(offline_stubs_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(offline_stubs_tests PRIVATE libSceNpManager libSceNpAuth libSceNpWebApi2 libSceNpCommerce libSceNpEntitlementAccess libSceNpSessionSignaling libSceNpGameIntent libSceNpTrophy2 libSceNpUniversalDataSystem libSceUserService libSceSystemService libSceMsgDialog libSceSaveDataDialog.native libSceCommonDialog libScePad libkernel libc)
configure_windows_unwind(offline_stubs_tests)
portps5_add_test(offline_stubs offline_stubs_tests)

# --- slice input: test registration ---
# PadHapticsTests uses GTest fixtures (TEST_F / gtest_discover_tests) and the
# portps5_add_gtest helper, which links GTest::gtest_main so no manual main()
# is needed.  SDL2 include is required for PadInputTypes.hpp → SDL_scancode.h.
portps5_add_gtest(pad_haptics_tests
    SOURCES  tests/PadHapticsTests.cpp
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}
             ${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include
    LIBRARIES libScePad libkernel libc
    LABELS   unit
)
endif()
