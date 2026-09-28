#ifndef ELFPATCHER_WINDOWS_PEWRITER_HPP
#define ELFPATCHER_WINDOWS_PEWRITER_HPP

#include <elfpatcher/windows/WindowsPeFormat.hpp>

namespace Elfpatcher::Windows {

class WindowsPeWriter {
public:
    // Writes the PE image. windowsGui selects IMAGE_SUBSYSTEM_WINDOWS_GUI (2)
    // instead of IMAGE_SUBSYSTEM_WINDOWS_CUI (3); defaults to false so
    // existing callers are unaffected.
    std::vector<std::uint8_t> Write(const std::vector<PeSection>& sections, std::uint32_t entryRva, const std::array<PeDirectory, 16>& directories, bool windowsGui = false) const;
};

}

#endif
