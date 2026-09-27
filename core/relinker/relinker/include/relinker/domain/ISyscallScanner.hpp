#ifndef RELINKER_DOMAIN_ISYSCALLSCANNER_HPP
#define RELINKER_DOMAIN_ISYSCALLSCANNER_HPP

#include <relinker/domain/Types.hpp>
#include <domain/CodeMap.hpp>
#include <vector>

namespace Relinker {

class ISyscallScanner {
public:
    virtual ~ISyscallScanner() = default;

    virtual void ScanCodeSectionForSyscalls(
        const std::vector<std::uint8_t>& codeSection,
        FileByteOffset codeSectionOffset,
        FileByteOffset codeSectionSize
    ) = 0;

    virtual void ScanImage(
        const std::vector<std::uint8_t>& imageBytes,
        const std::vector<ProgramHeader>& headers,
        const Domain::CodeMap& codeMap
    ) = 0;
};

}

#endif
