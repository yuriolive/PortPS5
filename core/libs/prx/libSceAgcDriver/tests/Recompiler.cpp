/**
 * Compute dispatch through the real shader recompiler and Vulkan device.
 *
 * Each TEST runs in its own process (gtest_discover_tests), which matters here: the driver owns a
 * process-wide Vulkan device and a latched asynchronous error that LibcRunShutdown re-throws, so
 * tests must not share a process. Needs a Vulkan device (lavapipe on hosted CI), hence the
 * 'lavapipe' label rather than 'unit'.
 */
#include "prx/libSceAgcDriver/Execution/include/Driver.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include "prx/libSceAgcDriver/Submit/include/Acb.hpp"
#include "prx/libSceAgcDriver/Submit/include/Dcb.hpp"
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <array>
#include <stdexcept>
#include <vector>

namespace {

// COMPUTE_NUM_THREAD_X/Y/Z and COMPUTE_PGM_RSRC2 as SH register DWORD offsets (see ShaderInputState.cpp).
constexpr std::uint32_t kComputeNumThreadX = 0x207;
constexpr std::uint32_t kComputePgmRsrc2 = 0x213;

/**
 * Registers a one-instruction compute shader (s_endpgm) and submits a 1x1x1 dispatch on ACB queue 0x20.
 * @param programThreadCounts when true, emits SET_SH_REG for COMPUTE_NUM_THREAD_X/Y/Z = 1 before the
 *        dispatch, the way a real title does; when false the dispatch is submitted without them.
 */
void SubmitEmptyComputeDispatch(bool programThreadCounts) {
    alignas(256) static const std::array<std::uint32_t, 1> code{0xbf810000};
    static Shader shader{};
    shader = Shader{};
    shader.file_header = 0x34333231;
    shader.version = 0x18;
    shader.header_size = sizeof(Shader);
    shader.shader_size = sizeof(code);
    shader.code = code.data();
    AgcDriverRegisterShader_nid_postfix(&shader);

    const auto address = reinterpret_cast<std::uintptr_t>(code.data());
    static std::vector<std::uint32_t> commands;
    commands = {
        // SET_SH_REG COMPUTE_PGM_LO/HI: program address in 256-byte units, split across two registers.
        0xc0027600, 0x20c, static_cast<std::uint32_t>(address >> 8u), static_cast<std::uint32_t>(address >> 40u),
        // SET_SH_REG COMPUTE_PGM_RSRC2 = 0 (no user data, no scratch).
        0xc0017600, kComputePgmRsrc2, 0,
    };
    if (programThreadCounts) {
        commands.insert(commands.end(), {0xc0037600, kComputeNumThreadX, 1, 1, 1});
    }
    // DISPATCH_DIRECT 1x1x1.
    commands.insert(commands.end(), {0xc0031500, 1, 1, 1, 0x8041});
    static Packet packet;
    packet = Packet{commands.data(), static_cast<std::uint32_t>(commands.size()), 0, {}};
    sceAgcDriverSubmitAcb(0x20, &packet);
}

}  // namespace

// Invariant: a compute dispatch with programmed thread counts goes through the real recompiler and
// the Vulkan device and completes; neither WaitIdle nor shutdown reports an error.
TEST(AgcDriverRecompiler, ProgrammedComputeDispatchCompletes) {
    SubmitEmptyComputeDispatch(true);
    EXPECT_NO_THROW(AgcDriverWaitIdle_nid_postfix());
    EXPECT_NO_THROW(LibcRunShutdown_nid_postfix());
}

// Invariant: a dispatch without COMPUTE_NUM_THREAD_X/Y/Z is rejected with the register named in hex
// (0x207), not silently skipped, and the failure stays latched: a second WaitIdle and shutdown
// both re-report it instead of losing it. Expected failure mode if the latch regresses: one of the
// later calls returns normally.
TEST(AgcDriverRecompiler, MissingThreadCountRegistersFailAndStayLatched) {
    SubmitEmptyComputeDispatch(false);
    constexpr const char* kExpected = "missing register at DWORD 0x207";
    EXPECT_THAT([] { AgcDriverWaitIdle_nid_postfix(); }, ::testing::ThrowsMessage<std::runtime_error>(::testing::HasSubstr(kExpected)));
    EXPECT_THAT([] { AgcDriverWaitIdle_nid_postfix(); }, ::testing::ThrowsMessage<std::runtime_error>(::testing::HasSubstr(kExpected)));
    EXPECT_THAT([] { LibcRunShutdown_nid_postfix(); }, ::testing::ThrowsMessage<std::runtime_error>(::testing::HasSubstr(kExpected)));
}
