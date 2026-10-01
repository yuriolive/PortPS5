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

# /savedata0 per-title save container mount + file I/O + sandbox containment
portps5_add_gtest(filesystem_savedata_mount_tests
    SOURCES
        filesystem/SaveDataMountTests.cpp
    LIBRARIES
        libkernel
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

# Small PRX modules: PlayGo (chunk set, codes), ConvertKeycode, Sysmodule unload policy
portps5_add_gtest(playgo_tests
    SOURCES
        modules/PlayGoTests.cpp
    LIBRARIES
        libScePlayGo
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

portps5_add_gtest(convert_keycode_tests
    SOURCES
        modules/ConvertKeycodeTests.cpp
    LIBRARIES
        libSceConvertKeycode
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
    LABELS
        unit
)

portps5_add_gtest(sysmodule_tests
    SOURCES
        modules/SysmoduleTests.cpp
    LIBRARIES
        libSceSysmodule
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
