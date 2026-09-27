// Windows trampoline builder tests: no libc dependency, no CPU execution.
// The libc SSE4a trap is owned by another agent; here we verify that
// --to-intel records trampoline sites and the PE builder emits .amdstub.
#include <codegen/CodegenException.hpp>
#include <codegen/IAmd64OnlyConverter.hpp>
#include <codegen/x86/IAmd64OnlyInstructionMatcher.hpp>
#include <codegen/x86/Sse4aOperands.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <domain/CodeMap.hpp>
#include <elfpatcher/windows/WindowsLoadImage.hpp>
#include <elfpatcher/windows/WindowsPeWriter.hpp>
#include <elfpatcher/windows/WindowsTrampolineBuilder.hpp>
#include <io/BufferUtils.hpp>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using namespace Elfpatcher::Windows;
using Bytes = std::vector<std::uint8_t>;

void require(bool condition, const std::string& message) {
    if (!condition)
        throw std::runtime_error(message);
}

void requireFailure(const std::function<void()>& operation, const char* message) {
    try {
        operation();
    } catch (const Domain::RelinkerException&) {
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

template<typename TValue>
TValue read(const Bytes& bytes, std::size_t offset) {
    TValue value;
    if (offset > bytes.size() || sizeof(value) > bytes.size() - offset)
        throw std::runtime_error("Test fixture read is out of bounds");
    std::memcpy(&value, bytes.data() + offset, sizeof(value));
    return value;
}

Domain::CodeMap mapForSegment(const Bytes& file, const Domain::ProgramHeader& ph) {
    Domain::CodeMap map;
    const Codegen::X64InstructionDecoder decoder;
    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(ph.FileSize)) {
        const auto vaddr = ph.MappedAddress + offset;
        map.Starts.insert(vaddr);
        const std::size_t fileOff = static_cast<std::size_t>(ph.Offset) + offset;
        const std::size_t available = file.size() - fileOff;
        std::size_t len = 1;
        try {
            len = decoder.Decode(file.data() + fileOff, available);
        } catch (const Codegen::CodegenException&) {
            len = 1;
        }
        if (len == 0)
            len = 1;
        try {
            const auto info = decoder.DecodeInstruction(file.data() + fileOff, available);
            if (info.HasBranchTarget && !info.HasRipRelativeDisp) {
                const auto target = static_cast<std::int64_t>(vaddr + info.Length) + info.BranchDisp;
                if (target >= 0)
                    map.BranchTargets.insert(static_cast<Domain::VirtualAddress>(target));
            }
        } catch (const Codegen::CodegenException&) {
        }
        offset += len;
    }
    return map;
}

Bytes elfFixture(const Bytes& text) {
    Bytes bytes(0x400);
    bytes[0] = 0x7F;
    bytes[1] = 'E';
    bytes[2] = 'L';
    bytes[3] = 'F';
    bytes[4] = 2;
    bytes[5] = 1;
    bytes[6] = 1;
    write<std::uint16_t>(bytes, 16, 3);
    write<std::uint16_t>(bytes, 18, 62);
    write<std::uint64_t>(bytes, 24, 0x1000);
    write<std::uint64_t>(bytes, 32, 64);
    write<std::uint16_t>(bytes, 54, 56);
    write<std::uint16_t>(bytes, 56, 2);
    std::fill(bytes.begin() + 0x200, bytes.begin() + 0x300, 0xCC);
    std::copy(text.begin(), text.end(), bytes.begin() + 0x200);
    return bytes;
}

std::vector<Domain::ProgramHeader> elfHeaders() {
    return {{1, 5, 0x200, 0x1000, 0, 0x100, 0x100, 0x1000}, {1, 6, 0x300, 0x2000, 0, 0x100, 0x100, 0x1000}};
}

void peBuilder() {
    const Bytes site = {0xF2, 0x0F, 0x78, 0xDB, 0x08, 0x08};
    Bytes text = {0xEB, 0x06};
    text.insert(text.end(), site.begin(), site.end());
    text.push_back(0xC3);
    const auto source = elfFixture(text);
    const auto headers = elfHeaders();
    const auto map = mapForSegment(source, headers[0]);
    const auto converted = Codegen::MakeAmd64OnlyConverter()->Convert(source, {headers[0]}, map);
    require(converted.Trampolines.size() == 1, "PE fixture conversion did not produce one stub");
    const WindowsLoadImage image(converted.Bytes, headers);
    auto sections = image.BuildSections();
    auto nextRva = image.GetEndRva();
    const auto stubSectionRva = nextRva;
    WindowsTrampolineBuilder().Build(converted.Trampolines, image, sections, nextRva);
    require(sections.size() == 3 && sections.back().Name == ".amdstub" && sections.back().Rva == stubSectionRva && (sections.back().Characteristics & SectionExecute) != 0 && nextRva == AlignRva(stubSectionRva + sections.back().Data.size()), "Stub section was not appended");
    const auto& text0 = sections[0];
    const auto siteRva = LoadRva + 2;
    require(text0.Data[2] == 0xE9 && text0.Data[7] == 0x90 && text0.Data[8] == 0xC3, "PE site was not replaced by a jump");
    const auto stubRva = static_cast<std::uint32_t>(static_cast<std::int64_t>(siteRva + 5) + read<std::int32_t>(text0.Data, 3));
    require(stubRva == stubSectionRva && stubRva % 16 == 0, "PE jump does not land on the 16-aligned stub");
    const auto& body = sections.back().Data;
    require(body.size() == 32 && body[0] == 0x66 && body[9] == 0xE9 && body[16] == 0x00 && body[17] == 0x00 && body[18] == 0x02, "PE stub body is wrong");
    require(static_cast<std::int64_t>(stubRva + 9 + 5) + read<std::int32_t>(body, 10) == siteRva + 6, "PE stub does not return past the site");
    require(read<std::int32_t>(body, 5) == 7 && (stubRva + 16) % 16 == 0, "PE mask constant is not 16-aligned");
    std::array<PeDirectory, 16> directories{};
    const auto file = WindowsPeWriter().Write(sections, LoadRva, directories);
    require(file.size() > 0x400, "PE writer rejected the stub section");
    auto altered = converted.Bytes;
    altered[0x205] = 0xDC;
    requireFailure([&] {
        const WindowsLoadImage alteredImage(altered, headers);
        auto alteredSections = alteredImage.BuildSections();
        auto rva = alteredImage.GetEndRva();
        WindowsTrampolineBuilder().Build(converted.Trampolines, alteredImage, alteredSections, rva);
    }, "Changed PE site bytes were accepted");
    requireFailure([&] {
        auto unmapped = converted.Trampolines;
        unmapped[0].Address = 0x5000;
        auto freshSections = image.BuildSections();
        auto rva = image.GetEndRva();
        WindowsTrampolineBuilder().Build(unmapped, image, freshSections, rva);
    }, "Unmapped PE site was accepted");
    auto untouched = image.BuildSections();
    auto untouchedRva = image.GetEndRva();
    WindowsTrampolineBuilder().Build({}, image, untouched, untouchedRva);
    require(untouched.size() == 2 && untouchedRva == image.GetEndRva(), "Empty site list changed the image");
}

void residualLeavesBytes() {
    // Register form stays in place for the runtime trap; no stub is emitted.
    const Bytes site = {0x66, 0x0F, 0x79, 0xCA};
    Bytes text = {0xEB, 0x04};
    text.insert(text.end(), site.begin(), site.end());
    text.push_back(0xC3);
    const auto source = elfFixture(text);
    const auto headers = elfHeaders();
    const auto map = mapForSegment(source, headers[0]);
    const auto converted = Codegen::MakeAmd64OnlyConverter()->Convert(source, {headers[0]}, map);
    require(converted.Trampolines.empty(), "Residual site produced a stub");
    require(converted.Residuals.size() == 1, "Residual site was not recorded");
    require(converted.Bytes == source, "Residual site bytes were modified");
    const WindowsLoadImage image(converted.Bytes, headers);
    auto sections = image.BuildSections();
    auto nextRva = image.GetEndRva();
    WindowsTrampolineBuilder().Build(converted.Trampolines, image, sections, nextRva);
    require(sections.size() == 2, "Empty trampoline list changed the image");
}

}

int main() {
    try {
        peBuilder();
        residualLeavesBytes();
        std::cout << "AMD64-only Windows tests passed\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
