# tests/CMakeLists.txt
# GoogleTest-based verification test suites for PortPS5

if(NOT BUILD_TESTING)
    return()
endif()

# Kernel event queue lifetime and race condition verification (ported from KytyPS5)
portps5_add_gtest(kernel_equeue_lifetime_tests
    SOURCES
        kernel/EventQueueLifetimeTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Kernel sync-on-address verification (wait-on-address, timeouts, concurrent wakeups, isolation)
portps5_add_gtest(kernel_sync_on_address_tests
    SOURCES
        kernel/SyncOnAddressTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Kernel pthread synchronization verification (mutex attributes, condvar signals, semaphores)
portps5_add_gtest(kernel_pthread_tests
    SOURCES
        kernel/PthreadMutexTests.cpp
        kernel/PthreadCondTests.cpp
        kernel/PthreadSemTests.cpp
        kernel/PthreadSemHandleTests.cpp
        kernel/PthreadBarrierOnceTests.cpp
        kernel/KernelSleepSignalTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Virtual and direct memory allocation verification (ported from KytyPS5)
portps5_add_gtest(memory_virtual_allocation_tests
    SOURCES
        memory/VirtualMemoryAllocationTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Guest-memory write tracking (GuestMemoryTracking::Watch), ported in behaviour from KytyPS5 MemoryTrackerTests
portps5_add_gtest(memory_tracker_tests
    SOURCES
        memory/MemoryTrackerTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Guest-pointer range validation API (GuestMemoryValidation.hpp)
portps5_add_gtest(guest_memory_validation_tests
    SOURCES
        memory/GuestMemoryValidationTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Literal SCE error values exposed by DirectMemory.hpp (header-only, runs on any host)
portps5_add_gtest(kernel_error_value_tests
    SOURCES
        kernel/KernelErrorValuesTests.cpp
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# GuestAllocations::Mutation::Unmap: ranges spanning several allocations (FreeBSD munmap semantics)
portps5_add_gtest(guest_allocations_unmap_tests
    SOURCES
        memory/GuestAllocationsUnmapTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Guest-visible direct-memory exports: query, available size, argument hardening
portps5_add_gtest(direct_memory_export_tests
    SOURCES
        memory/DirectMemoryExportTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Mapping placement (hint, NO_OVERWRITE) and munmap spanning, end to end through the exports
portps5_add_gtest(memory_mapping_tests
    SOURCES
        memory/MemoryMappingTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Direct memory physical pool and SceKernelMemoryPool API verification
portps5_add_gtest(direct_memory_pool_tests
    SOURCES
        memory/DirectMemoryPoolTests.cpp
    LIBRARIES
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)


# Arena extent-tree allocator differential verification (first-fit order contract)
portps5_add_gtest(guest_arena_extent_tests
    SOURCES
        memory/GuestArenaExtentTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Full uncapped 10^6-op differential run (spec guest-memory.md). DISABLED_ in
# the gtest binary keeps it out of the hosted `unit` run; this entry forces it
# on under the `slow` label, which only the scheduled workflow selects.
portps5_add_test(GuestArenaExtent.FuzzUncapped guest_arena_extent_tests
    ARGS --gtest_also_run_disabled_tests
         --gtest_filter=GuestArenaExtent.DISABLED_MatchesLinearScanFuzzUncapped
    LABELS slow
)
set_tests_properties(GuestArenaExtent.FuzzUncapped PROPERTIES TIMEOUT 900)

# Filesystem sandbox containment & path traversal prevention (ported from SharpEMU)
portps5_add_gtest(filesystem_sandbox_path_tests
    SOURCES
        filesystem/SandboxPathTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Offline network stack verification (sockets, options, epoll, DNS resolver stubs)
portps5_add_gtest(offline_net_stack_tests
    SOURCES
        net/OfflineNetStackTests.cpp
    LIBRARIES
        libSceNet
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# AvPlayer playback state machine verification (Ready/Play/Pause/Stop lifecycle, blank stream info)
portps5_add_gtest(avplayer_state_machine_tests
    SOURCES
        avplayer/AvPlayerStateMachineTests.cpp
    LIBRARIES
        libSceAvPlayer.native
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Exercise the same C/System V exports in the standalone non-native PRX.
portps5_add_gtest(avplayer_exports_tests
    SOURCES
        avplayer/AvPlayerStateMachineTests.cpp
    LIBRARIES
        libSceAvPlayer
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Headless message dialog state machine (transitions, error codes, 44-byte result, auto-answer policy)
portps5_add_gtest(msgdialog_tests
    SOURCES
        dialogs/MsgDialogTests.cpp
    LIBRARIES
        libSceMsgDialog
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# H.264 decoding for libSceVideodec2: the FFmpeg wrapper on synthetic I_PCM/P_Skip streams, the guest ABI
# (layouts, error codes, NV12 layout), and the FFmpeg licence gate script on fixture configure outputs.
portps5_add_gtest(h264_decoder_tests
    SOURCES
        video/H264DecoderTests.cpp
    LIBRARIES
        portps5_h264_decoder
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}/video
    LABELS
        unit
)

portps5_add_gtest(videodec2_tests
    SOURCES
        video/Videodec2Tests.cpp
    LIBRARIES
        libSceVideodec2
        portps5_h264_decoder
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/video
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

foreach(case
        "lgpl_accepted;lgpl;lgpl;PASS"
        "gpl_rejected;gpl;lgpl;FAIL;GPL"
        "lgplv3_rejected;lgplv3;lgpl;FAIL;LGPLV3"
        "gpl_header_rejected;lgpl;gpl;FAIL;GPL"
        "nonfree_rejected;lgpl;nonfree;FAIL;NONFREE")
    list(GET case 0 name)
    list(GET case 1 mak)
    list(GET case 2 hdr)
    list(GET case 3 expect)
    add_test(NAME ffmpeg_license_gate_${name}
        COMMAND ${CMAKE_COMMAND}
            -DCONFIG_MAK=${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/fixtures/${mak}.mak
            -DCONFIG_H=${CMAKE_CURRENT_SOURCE_DIR}/ffmpeg/fixtures/${hdr}.h
            -P ${CMAKE_SOURCE_DIR}/cmake/PortPS5FFmpegLicenseCheck.cmake)
    set_tests_properties(ffmpeg_license_gate_${name} PROPERTIES LABELS unit)
    if(expect STREQUAL "FAIL")
        # Match the specific rejection so a missing fixture or unrelated CMake error cannot pass.
        list(GET case 4 reject_flag)
        set_tests_properties(ffmpeg_license_gate_${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "FFmpeg configure enabled CONFIG_${reject_flag};")
    endif()
endforeach()

# Save data subsystem fidelity tests (mount modes, snapshots, atomic swaps, quotas, patterns, dialogs)
portps5_add_gtest(savedata_fidelity_tests
    SOURCES
        savedata/SaveDataFidelityTests.cpp
        savedata/SaveDataDialogScanTests.cpp
    LIBRARIES
        libSceSaveData.native
        libSceSaveDataDialog.native
        libSceCommonDialog
        libkernel
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Cross-PRX verbatim symbol resolution and Config::Loader export stability
portps5_add_gtest(config_cross_prx_tests
    SOURCES
        config/ConfigCrossPrxTests.cpp
        ${CMAKE_SOURCE_DIR}/core/libs/nid/src/Sha1.cpp
        ${CMAKE_SOURCE_DIR}/core/libs/nid/src/NidCompute.cpp
        ${CMAKE_SOURCE_DIR}/core/libs/nid/src/NidResolver.cpp
        ${CMAKE_SOURCE_DIR}/core/libs/nid/src/ExportExclusions.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
        ${CMAKE_SOURCE_DIR}/core/libs/nid/include
    LABELS
        unit
)

# Guest locale and C++ iostream facet ABI verification (ctype, collate, num_put, _Id_cnt)
portps5_add_gtest(guest_locale_tests
    SOURCES
        libc/GuestLocaleTests.cpp
        ${CMAKE_SOURCE_DIR}/core/libs/nid/src/Sha1.cpp
        ${CMAKE_SOURCE_DIR}/core/libs/nid/src/NidCompute.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
        ${CMAKE_SOURCE_DIR}/core/libs/nid/include
    LABELS
        unit
)
# Python unit tests for tools/check_comments.py — verifies the comment
# policy linter logic (Rules 1-3) in isolation with no game data required.
portps5_add_test(check_comments_unit_tests
    ${Python3_EXECUTABLE}
    LABELS
        unit
    ARGS
        -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools -p "test_check_comments.py" -v
)

# Python unit tests for tools/progress.py — verifies --root revision
# attribution (libraries, opcode enum and ISA list all follow the selected
# root) with synthetic trees and no game data.
portps5_add_test(progress_unit_tests
    ${Python3_EXECUTABLE}
    LABELS
        unit
    ARGS
        -m unittest discover -s ${CMAKE_CURRENT_SOURCE_DIR}/tools -p "test_progress.py" -v
)

# Compile shared dialog regressions separately to avoid duplicate native exports.
portps5_add_gtest(savedata_dialog_non_native_tests
    SOURCES savedata/SaveDataDialogScanTests.cpp
    LIBRARIES libSceSaveDataDialog libkernel libc
    INCLUDES ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/core/libs
    LABELS unit
)
target_compile_definitions(savedata_dialog_non_native_tests PRIVATE SAVE_DATA_DIALOG_NON_NATIVE)

# SDL game controller -> scePadRead translation (synthetic samples, no device)
portps5_add_gtest(pad_controller_input_tests
    SOURCES
        input/ControllerInputTests.cpp
    LIBRARIES
        libScePad libkernel libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
        ${CMAKE_SOURCE_DIR}/3rdparty/SDL2/include
    LABELS
        unit
)

# Telemetry core: record schema, stall/softlock definitions, watchdog latch and
# audio/A-V sampler on a synthetic clock (header-only, no game data).
portps5_add_gtest(telemetry_core_tests
    SOURCES
        telemetry/TelemetryTests.cpp
    INCLUDES
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

# Telemetry runtime exports in libc: Start/Shutdown lifecycle and the run.end
# end-marker guarantee, writing only to a temp directory.
portps5_add_gtest(telemetry_runtime_tests
    SOURCES
        telemetry/TelemetryRuntimeTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)
