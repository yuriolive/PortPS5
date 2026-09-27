#ifndef ELFPATCHER_WINDOWS_WINDOWSTLSBUILDER_HPP
#define ELFPATCHER_WINDOWS_WINDOWSTLSBUILDER_HPP

#include <elfpatcher/windows/WindowsLoadImage.hpp>
#include <domain/CodeMap.hpp>

namespace Elfpatcher::Windows {

class WindowsTlsBuilder {
public:
    PeDirectory Build(const std::vector<std::uint8_t>& source, const std::vector<Domain::ProgramHeader>& headers, const WindowsLoadImage& image, std::vector<PeSection>& sections, std::vector<std::uint32_t>& relocations, std::uint32_t& nextRva, std::uint32_t* tlsIndexRva = nullptr) const;
    PeDirectory BuildWithCodeMap(const std::vector<std::uint8_t>& source, const std::vector<Domain::ProgramHeader>& headers, const WindowsLoadImage& image, std::vector<PeSection>& sections, std::vector<std::uint32_t>& relocations, std::uint32_t& nextRva, const Domain::CodeMap& codeMap, std::uint32_t* tlsIndexRva = nullptr) const;
};

}

#endif
