// Execution harness shared by the SSE4a (EXTRQ/INSERTQ) lowering tests.
//
// Subsystem: relinker codegen tests (header only, test code). Builds a tiny
// thunk in executable memory that
//   - loads all 16 XMM registers with known values,
//   - sets the arithmetic flags to a known pattern,
//   - fills the 128-byte SysV red zone below RSP with a known pattern,
// runs the code under test, and then captures XMM registers, red zone and
// flags. The code under test is either a lowered stub body (made position
// correct with StubMiddle) or the original SSE4a instruction bytes (hardware
// oracle, only on hosts where HostHasSse4a()). Single-threaded; every call
// maps and unmaps its own executable page. x86-64 only (the whole header is
// compiled out elsewhere).
#ifndef RELINKER_TESTS_SSE4A_EXECUTION_HARNESS_HPP
#define RELINKER_TESTS_SSE4A_EXECUTION_HARNESS_HPP

#include <codegen/x86/Sse4aLowering.hpp>

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <vector>

#if defined(__x86_64__)
#include <cpuid.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace Sse4aHarness {

using Bytes = std::vector<std::uint8_t>;


// The harness thunk is called with the System V convention on every host so
// that its argument registers (RDI, RSI) are the same on Windows and Linux.
#define TEST_SYSV __attribute__((sysv_abi))

// Arithmetic flags forced before the stub runs: CF PF AF ZF SF OF.
constexpr std::uint64_t kFlagMask = 0x8D5;
constexpr std::uint32_t kFlagsIn = 0x8D7;  // bit 1 is a reserved 1

struct Harness {
    // In: the 16 XMM registers (2 qwords each). Out: same layout, after the run.
    std::uint64_t Xmm[16][2];
    // Out only: the 16 qwords [rsp-8*k], k = 1..16 (the red zone).
    std::uint64_t RedZone[16];
    // Out only: RFLAGS after the run.
    std::uint64_t Flags;
};

constexpr std::uint64_t RedZonePattern(const int slot) {
    return 0xA5A5000000000000ull + static_cast<std::uint64_t>(slot) * 0x0101010101ull;
}

inline void Append(Bytes& out, std::initializer_list<std::uint8_t> bytes) {
    out.insert(out.end(), bytes.begin(), bytes.end());
}

inline void AppendU32(Bytes& out, const std::uint32_t value) {
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
}

// movdqu xmmN, [rdi + 16*N]   (F3 [REX.R] 0F 6F /r with mod=10 rm=rdi, disp32)
// movdqu [rsi + 16*N], xmmN   (F3 [REX.R] 0F 7F /r with mod=10 rm=rsi, disp32)
inline void XmmMove(Bytes& out, const bool load, const int n) {
    out.push_back(0xF3);
    if (n >= 8)
        out.push_back(0x44);  // REX.R selects xmm8..15 in the reg field
    out.push_back(0x0F);
    out.push_back(load ? 0x6F : 0x7F);
    out.push_back(static_cast<std::uint8_t>(0x80 | ((n & 7) << 3) | (load ? 7 : 6)));
    AppendU32(out, static_cast<std::uint32_t>(16 * n));
}

inline Bytes Prologue() {
    Bytes out;
    // Flags first: PUSH/POPFQ use [rsp-8], which belongs to the red-zone pattern.
    Append(out, {0x68});            // push imm32 (sign-extended)
    AppendU32(out, kFlagsIn);
    Append(out, {0x9D});            // popfq
    for (int k = 1; k <= 16; ++k) {
        Append(out, {0x48, 0xB8});  // movabs rax, imm64
        const auto value = RedZonePattern(k);
        for (int i = 0; i < 8; ++i)
            out.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
        // mov [rsp + disp8], rax with disp8 = -8*k
        Append(out, {0x48, 0x89, 0x44, 0x24, static_cast<std::uint8_t>(-8 * k)});
    }
    for (int n = 0; n < 16; ++n)
        XmmMove(out, true, n);
    // The stub must start 16-byte aligned: its constants are read with aligned
    // PAND/PADDQ m128 operands. The thunk base is page aligned.
    while (out.size() % 16 != 0)
        out.push_back(0x90);
    return out;
}

inline Bytes Epilogue() {
    Bytes out;
    for (int n = 0; n < 16; ++n)
        XmmMove(out, false, n);
    for (int k = 1; k <= 16; ++k) {
        Append(out, {0x48, 0x8B, 0x44, 0x24, static_cast<std::uint8_t>(-8 * k)});  // mov rax,[rsp-8k]
        Append(out, {0x48, 0x89, 0x86});                                            // mov [rsi+disp32],rax
        AppendU32(out, static_cast<std::uint32_t>(offsetof(Harness, RedZone) + 8 * (k - 1)));
    }
    Append(out, {0x9C, 0x58});  // pushfq; pop rax
    Append(out, {0x48, 0x89, 0x86});
    AppendU32(out, static_cast<std::uint32_t>(offsetof(Harness, Flags)));
    Append(out, {0xC3});
    return out;
}

using Thunk = void(TEST_SYSV*)(const Harness* in, Harness* out);

inline void* MapExecutable(const std::size_t size) {
#ifdef _WIN32
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return p == MAP_FAILED ? nullptr : p;
#endif
}

inline void UnmapExecutable(void* p, const std::size_t size) {
#ifdef _WIN32
    (void)size;
    VirtualFree(p, 0, MEM_RELEASE);
#else
    munmap(p, size);
#endif
}

// Runs `middle` (already position fixed up) between the prologue and epilogue.
inline Harness RunThunk(const Bytes& middle, const Harness& in) {
    const auto prologue = Prologue();
    const auto epilogue = Epilogue();
    Bytes image = prologue;
    image.insert(image.end(), middle.begin(), middle.end());
    image.insert(image.end(), epilogue.begin(), epilogue.end());
    const std::size_t mapSize = 4096 * ((image.size() + 4095) / 4096);
    void* code = MapExecutable(mapSize);
    EXPECT_NE(code, nullptr) << "cannot map executable memory";
    Harness out{};
    if (code == nullptr)
        return out;
    std::memcpy(code, image.data(), image.size());
    reinterpret_cast<Thunk>(code)(&in, &out);
    UnmapExecutable(code, mapSize);
    return out;
}

// A lowered stub whose placeholder return branch is pointed at the epilogue
// that directly follows the body (instead of the instruction after a site).
inline Bytes StubMiddle(const Codegen::LoweredBody& stub) {
    Bytes body = stub.Bytes;
    // The body ends in constants; the epilogue is appended after them, so the
    // branch must skip over those bytes: target = end of body.
    const auto branchEnd = stub.ReturnBranchOffset + 5;
    const auto displacement = static_cast<std::int32_t>(body.size() - branchEnd);
    EXPECT_EQ(body[stub.ReturnBranchOffset], 0xE9) << "return branch placeholder is not a jmp rel32";
    for (int i = 0; i < 4; ++i)
        body[stub.ReturnBranchOffset + 1 + i] = static_cast<std::uint8_t>(static_cast<std::uint32_t>(displacement) >> (8 * i));
    return body;
}

// Distinct non-trivial canary in every XMM lane, so a clobbered or swapped
// register shows up in the comparison.
inline Harness CanaryInput() {
    Harness in{};
    for (int n = 0; n < 16; ++n) {
        in.Xmm[n][0] = 0x1111111111111111ull * (n + 1) ^ 0x0123456789ABCDEFull;
        in.Xmm[n][1] = 0xFEDCBA9876543210ull + static_cast<std::uint64_t>(n) * 0x0F0F0F0F0F0F0F0Full;
    }
    return in;
}

inline bool HostHasSse4a() {
    unsigned a = 0, b = 0, c = 0, d = 0;
    if (__get_cpuid_max(0x80000000u, nullptr) < 0x80000001u)
        return false;
    __get_cpuid(0x80000001u, &a, &b, &c, &d);
    return (c & (1u << 6)) != 0;  // CPUID Fn8000_0001 ECX[6] = SSE4A
}

}  // namespace Sse4aHarness

#endif  // __x86_64__

#endif  // RELINKER_TESTS_SSE4A_EXECUTION_HARNESS_HPP
