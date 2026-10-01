// core/libs/tests/GuestTestPages.hpp
// Test helper: page-granular host memory with a chosen protection followed by
// an always-inaccessible guard page, used to hand PRX exports guest pointers
// that are unreadable, read-only, or run off the end of a mapping. Synthetic
// memory only. Not thread-safe; each object owns its mapping (RAII).

#ifndef CORE_LIBS_TESTS_GUESTTESTPAGES_HPP
#define CORE_LIBS_TESTS_GUESTTESTPAGES_HPP

#include <cstddef>
#include <cstdint>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace GuestTest {

// Protection of the usable pages of a GuestPages mapping.
enum class PageAccess { None, ReadOnly, ReadWrite };

// Owns `pages` pages of `access` followed by one PROT_NONE / PAGE_NOACCESS guard page.
class GuestPages {
public:
    static constexpr std::size_t kPage = 16 * 1024;  // multiple of every supported host page size

    // Maps the pages; data() is null if the host refused (tests ASSERT on it).
    explicit GuestPages(std::size_t pages, PageAccess access) : bytes_(pages * kPage) {
#ifdef _WIN32
        base_ = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, bytes_ + kPage, MEM_RESERVE | MEM_COMMIT, PAGE_NOACCESS));
        if (base_ && access != PageAccess::None) {
            DWORD old = 0;
            if (!VirtualProtect(base_, bytes_, access == PageAccess::ReadOnly ? PAGE_READONLY : PAGE_READWRITE, &old)) {
                VirtualFree(base_, 0, MEM_RELEASE);  // data() == nullptr tells the test the setup failed
                base_ = nullptr;
            }
        }
#else
        void* mapped = mmap(nullptr, bytes_ + kPage, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        base_ = mapped == MAP_FAILED ? nullptr : static_cast<std::uint8_t*>(mapped);
        if (base_ && access != PageAccess::None) {
            if (mprotect(base_, bytes_, access == PageAccess::ReadOnly ? PROT_READ : PROT_READ | PROT_WRITE) != 0) {
                munmap(base_, bytes_ + kPage);  // data() == nullptr tells the test the setup failed
                base_ = nullptr;
            }
        }
#endif
    }
    ~GuestPages() {
#ifdef _WIN32
        if (base_) VirtualFree(base_, 0, MEM_RELEASE);
#else
        if (base_) munmap(base_, bytes_ + kPage);
#endif
    }
    GuestPages(const GuestPages&) = delete;
    GuestPages& operator=(const GuestPages&) = delete;

    std::uint8_t* data() const { return base_; }
    std::size_t size() const { return bytes_; }
    // One past the last usable byte; dereferencing it faults (guard page).
    std::uint8_t* end() const { return base_ + bytes_; }

private:
    std::uint8_t* base_ = nullptr;
    std::size_t bytes_;
};

}  // namespace GuestTest

#endif
