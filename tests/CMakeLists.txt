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

# Cross-PRX verbatim symbol resolution and Config::Loader export stability
portps5_add_gtest(config_cross_prx_tests
    SOURCES
        config/ConfigCrossPrxTests.cpp
    LIBRARIES
        libc
    INCLUDES
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/core/libs
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
