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

# Cross-prx named-import check: fails when a prx imports a verbatim/mangled
# name that the patched provider (usually libc.prx) does not export, which
# surfaces at runtime as GetLastError 127 on load. Regression for the
# libScePad -> libc `Unsupported` break that stopped the Dreaming Sarah boot.
# Reads only the built patched libs; needs objdump on PATH (MinGW binutils).
portps5_add_test(prx_cross_import_check
    ${Python3_EXECUTABLE}
    LABELS
        unit
    ARGS
        ${CMAKE_SOURCE_DIR}/tools/check_prx_imports.py $<TARGET_FILE_DIR:libc>/..
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
