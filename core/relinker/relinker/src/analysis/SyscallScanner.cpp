#include <relinker/analysis/SyscallScanner.hpp>
#include <codegen/CodegenException.hpp>
#include <codegen/x86/X64InstructionDecoder.hpp>
#include <domain/CodeMap.hpp>
#include <iostream>
#include <memory>
#include <sstream>

namespace Relinker {

// Why these four pairs: guest code must never execute host syscalls.
// 0F 05 SYSCALL, CD 80 INT 0x80, 0F 34 SYSENTER, 0F 07 SYSRET.
static constexpr std::uint8_t SYSCALL_BYTE0 = 0x0F;
static constexpr std::uint8_t SYSCALL_BYTE1 = 0x05;
static constexpr std::uint8_t INT80_BYTE0 = 0xCD;
static constexpr std::uint8_t INT80_BYTE1 = 0x80;
static constexpr std::uint8_t SYSENTER_BYTE0 = 0x0F;
static constexpr std::uint8_t SYSENTER_BYTE1 = 0x34;
static constexpr std::uint8_t SYSRET_BYTE0 = 0x0F;
static constexpr std::uint8_t SYSRET_BYTE1 = 0x07;

namespace {

bool _isSyscallPair(std::uint8_t b0, std::uint8_t b1) {
    return (b0 == SYSCALL_BYTE0 && b1 == SYSCALL_BYTE1) ||
           (b0 == INT80_BYTE0 && b1 == INT80_BYTE1) ||
           (b0 == SYSENTER_BYTE0 && b1 == SYSENTER_BYTE1) ||
           (b0 == SYSRET_BYTE0 && b1 == SYSRET_BYTE1);
}

Domain::FileByteOffset _fileOffsetOf(
    const std::vector<Domain::ProgramHeader>& headers,
    Domain::VirtualAddress address,
    std::size_t size,
    const std::vector<std::uint8_t>& imageBytes) {
    for (const auto& header : headers) {
        if (header.Type != 1 || address < header.MappedAddress)
            continue;
        const auto offset = address - header.MappedAddress;
        if (offset >= header.FileSize || size > header.FileSize - offset)
            continue;
        (void)imageBytes;
        return header.Offset + offset;
    }
    throw RelinkerException("Syscall scan: unmapped code address", address);
}

}

class SyscallScanner : public ISyscallScanner {
public:
    void ScanCodeSectionForSyscalls(
        const std::vector<std::uint8_t>& codeSection,
        FileByteOffset codeSectionOffset,
        FileByteOffset codeSectionSize) override;
    void ScanImage(
        const std::vector<std::uint8_t>& imageBytes,
        const std::vector<ProgramHeader>& headers,
        const Domain::CodeMap& codeMap) override;
};

void SyscallScanner::ScanCodeSectionForSyscalls(
    const std::vector<std::uint8_t>& codeSection,
    const FileByteOffset codeSectionOffset,
    const FileByteOffset codeSectionSize) {
    // Legacy single-segment entry: decode sequentially. New code passes
    // through ScanImage with a shared CodeMap; this stays for unit tests.
    const std::size_t limit = std::min(codeSection.size(), static_cast<std::size_t>(codeSectionSize));
    const Codegen::X64InstructionDecoder decoder;
    std::size_t i = 0;
    while (i < limit) {
        const std::size_t available = limit - i;
        std::size_t length = 0;
        try {
            length = decoder.Decode(codeSection.data() + i, available);
        } catch (const Codegen::CodegenException& e) {
            throw RelinkerException(std::string("Syscall scan: ") + e.what(), codeSectionOffset + i);
        }
        if (length == 0)
            throw RelinkerException("Syscall scan: zero-length instruction", codeSectionOffset + i);
        if (length >= 2 && _isSyscallPair(codeSection[i], codeSection[i + 1])) {
            const FileByteOffset instrOffset = codeSectionOffset + i;
            std::ostringstream msg;
            msg << "Forbidden syscall instruction at code offset 0x" << std::hex << instrOffset;
            throw RelinkerException(msg.str(), instrOffset);
        }
        i += length;
    }
}

void SyscallScanner::ScanImage(
    const std::vector<std::uint8_t>& imageBytes,
    const std::vector<ProgramHeader>& headers,
    const Domain::CodeMap& codeMap) {
    // Only proven starts are errors. A syscall-like pair in Unproven is
    // data (jump tables, literal pools), so it is only counted and logged.
    for (const auto start : codeMap.Starts) {
        Domain::FileByteOffset fileOffset = 0;
        try {
            fileOffset = _fileOffsetOf(headers, start, 1, imageBytes);
        } catch (const RelinkerException&) {
            continue;
        }
        if (fileOffset + 1 >= imageBytes.size())
            continue;
        if (_isSyscallPair(imageBytes[fileOffset], imageBytes[fileOffset + 1])) {
            std::ostringstream msg;
            msg << "Forbidden syscall instruction at code address 0x" << std::hex << start;
            throw RelinkerException(msg.str(), fileOffset);
        }
    }
    std::size_t unprovenBytes = 0;
    for (const auto& range : codeMap.Unproven)
        unprovenBytes += static_cast<std::size_t>(range.End - range.Begin);
    if (unprovenBytes > 0)
        std::cout << "Syscall scan: " << unprovenBytes << " unproven bytes ignored (" << codeMap.Unproven.size() << " ranges)\n";
}

std::unique_ptr<ISyscallScanner> MakeSyscallScanner() {
    return std::make_unique<SyscallScanner>();
}

class NullSyscallScanner : public ISyscallScanner {
public:
    void ScanCodeSectionForSyscalls(
        const std::vector<std::uint8_t>&,
        FileByteOffset,
        FileByteOffset
    ) override {}
    void ScanImage(
        const std::vector<std::uint8_t>&,
        const std::vector<ProgramHeader>&,
        const Domain::CodeMap&
    ) override {}
};

std::unique_ptr<ISyscallScanner> MakeNullSyscallScanner() {
    return std::make_unique<NullSyscallScanner>();
}

}
