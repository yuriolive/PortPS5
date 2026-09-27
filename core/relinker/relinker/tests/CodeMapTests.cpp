#include <relinker/analysis/CodeMap.hpp>
#include <relinker/analysis/CodeInstructionCollector.hpp>
#include <relinker/analysis/SyscallScanner.hpp>
#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/IInstructionScanner.hpp>
#include <codegen/CodegenException.hpp>
#include <domain/CodeMap.hpp>
#include <functional>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void requireFailure(const std::function<void()>& operation, const char* message) {
    try {
        operation();
    } catch (const Relinker::RelinkerException&) {
        return;
    } catch (const Codegen::CodegenException&) {
        return;
    }
    throw std::runtime_error(message);
}

template<typename TValue>
void write(Bytes& bytes, std::size_t offset, TValue value) {
    if (offset > bytes.size() || sizeof(value) > bytes.size() - offset)
        throw std::runtime_error("Test fixture write is out of bounds");
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}

// Minimal image: ELF header entry at offset 24, one executable segment.
// Collector reads entry from bytes[24] and maps via headers.
struct Fixture {
    Bytes Bytes;
    std::vector<Domain::ProgramHeader> Headers;
};

Fixture makeFixture(const Bytes& code, Domain::VirtualAddress vaddr = 0x1000) {
    constexpr std::size_t kHeaderSize = 64;
    constexpr std::size_t kCodeOffset = 0x200;
    Bytes bytes(kCodeOffset + code.size(), 0);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    write<std::uint64_t>(bytes, 24, vaddr);
    std::copy(code.begin(), code.end(), bytes.begin() + kCodeOffset);
    Domain::ProgramHeader ph{};
    ph.Type = 1;
    ph.Flags = 1;
    ph.Offset = kCodeOffset;
    ph.MappedAddress = vaddr;
    ph.FileSize = code.size();
    ph.MemorySize = code.size();
    ph.Alignment = 0x1000;
    return {std::move(bytes), {ph}};
}

void jumpTableDesync() {
    // EB 06 jumps over 6 data bytes to C3. Linear sweep decodes the data;
    // recursive descent follows the branch and leaves data unproven.
    const Bytes code = {0xEB, 0x06, 0x0F, 0x05, 0xCC, 0xCC, 0xCC, 0xCC, 0xC3};
    auto fixture = makeFixture(code);
    const auto codeMap = Relinker::BuildCodeMap(fixture.Bytes, fixture.Headers);

    require(codeMap.Contains(0x1000), "Entry start missing from CodeMap");
    require(codeMap.Contains(0x1008), "Jump target missing from CodeMap");
    require(!codeMap.Contains(0x1002), "Literal-pool byte was treated as code");
    require(codeMap.IsTarget(0x1008), "Jump target missing from BranchTargets");

    bool foundUnproven = false;
    for (const auto& range : codeMap.Unproven) {
        if (range.Begin <= 0x1002 && range.End >= 0x1008)
            foundUnproven = true;
    }
    require(foundUnproven, "Literal pool was not reported as Unproven");

    // Linear sweep desyncs: it steps into the data bytes.
    const auto scanner = Codegen::MakeInstructionScanner();
    const std::vector<std::uint8_t> segment(code.begin(), code.end());
    const auto matches = scanner->ScanCodeSection(segment, 0, segment.size());
    bool sweepHitsData = false;
    for (const auto& match : matches) {
        if (match.Offset == 2)
            sweepHitsData = true;
    }
    require(sweepHitsData, "Linear sweep did not desync on the literal pool");
}

void syscallProvenVsUnproven() {
    // Syscall at a proven start is an error.
    {
        const Bytes code = {0x0F, 0x05, 0xC3};
        auto fixture = makeFixture(code);
        const auto codeMap = Relinker::BuildCodeMap(fixture.Bytes, fixture.Headers);
        auto scanner = Relinker::MakeSyscallScanner();
        requireFailure([&] { scanner->ScanImage(fixture.Bytes, fixture.Headers, codeMap); },
                       "Proven syscall was not reported");
    }
    // Syscall-like bytes hidden in Unproven data are only logged.
    {
        const Bytes code = {0xEB, 0x06, 0x0F, 0x05, 0xCC, 0xCC, 0xCC, 0xCC, 0xC3};
        auto fixture = makeFixture(code);
        const auto codeMap = Relinker::BuildCodeMap(fixture.Bytes, fixture.Headers);
        auto scanner = Relinker::MakeSyscallScanner();
        scanner->ScanImage(fixture.Bytes, fixture.Headers, codeMap);
    }
}

Domain::CodeMap manualMap(Domain::VirtualAddress base, const std::vector<Domain::VirtualAddress>& starts,
                          const std::vector<Domain::VirtualAddress>& targets = {}) {
    Domain::CodeMap map;
    for (const auto v : starts)
        map.Starts.insert(v);
    for (const auto v : targets)
        map.BranchTargets.insert(v);
    (void)base;
    return map;
}

void registerFormResidual() {
    // 66 0F 79 CA is EXTRQ register form: relink succeeds, site is Residual.
    Bytes file(0x300, 0xCC);
    const Bytes site = {0x66, 0x0F, 0x79, 0xCA, 0xC3};
    std::copy(site.begin(), site.end(), file.begin() + 0x200);
    const Domain::ProgramHeader ph{1, 5, 0x200, 0x1000, 0, 0x100, 0x100, 0x1000};
    auto map = manualMap(0x1000, {0x1000, 0x1004});
    const auto result = Codegen::MakeAmd64OnlyConverter()->Convert(file, {ph}, map);
    require(result.Trampolines.empty(), "Register form produced a stub");
    require(result.Residuals.size() == 1, "Register form was not recorded as Residual");
    require(result.Residuals[0].Address == 0x1000, "Residual address is wrong");
    require(result.Bytes == file, "Residual site bytes were modified");
}

void branchIntoStubFails() {
    // INSERTQ immediate form needing a stub, with a branch landing inside it.
    Bytes file(0x300, 0xCC);
    const Bytes site = {0xF2, 0x44, 0x0F, 0x78, 0xCC, 0x10, 0x10, 0xC3};
    std::copy(site.begin(), site.end(), file.begin() + 0x200);
    const Domain::ProgramHeader ph{1, 5, 0x200, 0x1000, 0, 0x100, 0x100, 0x1000};
    // Branch target 0x1001 lies strictly inside the 7-byte site at 0x1000.
    auto map = manualMap(0x1000, {0x1000, 0x1007}, {0x1001});
    requireFailure([&] { (void)Codegen::MakeAmd64OnlyConverter()->Convert(file, {ph}, map); },
                   "Branch into a stub site was accepted");
}

void trampolineSiteRecorded() {
    Bytes file(0x300, 0xCC);
    const Bytes site = {0xF2, 0x44, 0x0F, 0x78, 0xCC, 0x10, 0x10, 0xC3};
    std::copy(site.begin(), site.end(), file.begin() + 0x200);
    const Domain::ProgramHeader ph{1, 5, 0x200, 0x1000, 0, 0x100, 0x100, 0x1000};
    auto map = manualMap(0x1000, {0x1000, 0x1007});
    const auto result = Codegen::MakeAmd64OnlyConverter()->Convert(file, {ph}, map);
    require(result.Trampolines.size() == 1, "Trampoline site was not recorded");
    require(result.Residuals.empty(), "Trampoline site leaked into Residual");
    require(result.Bytes == file, "Converter modified bytes for a trampoline site");
    require(result.Trampolines[0].Address == 0x1000, "Trampoline address is wrong");
}

}

int main() {
    try {
        jumpTableDesync();
        syscallProvenVsUnproven();
        registerFormResidual();
        branchIntoStubFails();
        trampolineSiteRecorded();
        std::cout << "CodeMap tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
