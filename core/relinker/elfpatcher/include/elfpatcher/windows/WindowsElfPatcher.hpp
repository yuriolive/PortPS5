#ifndef ELFPATCHER_WINDOWS_WINDOWSELFPATCHER_HPP
#define ELFPATCHER_WINDOWS_WINDOWSELFPATCHER_HPP

#include <elfpatcher/general/IElfPatcher.hpp>

namespace Elfpatcher::Windows {

// Windows PE patcher: rewrites a guest ELF into a Windows PE image.
// The optional windowsGui flag selects IMAGE_SUBSYSTEM_WINDOWS_GUI (2) over
// IMAGE_SUBSYSTEM_WINDOWS_CUI (3) so converted games launch without a console
// window. Defaults to false to keep existing callers (including tests) unchanged.
class WindowsPePatcher : public IElfPatcher {
public:
    explicit WindowsPePatcher(bool windowsGui = false);

    std::vector<std::uint8_t> Patch(const std::vector<std::uint8_t>& sourceElf, const std::vector<Domain::ProgramHeader>& originalHeaders, const Domain::SysVDynamicSection& dynamicSection, std::uint64_t originalPltGotVaddr, const std::string& runPath, bool lazyBinding, bool dependencyDiagnostics, const std::vector<Codegen::TrampolineSite>& trampolines) override;

private:
    bool _windowsGui;
};

}

#endif
