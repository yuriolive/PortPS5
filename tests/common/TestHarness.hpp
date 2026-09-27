#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace PortPS5::Testing {

// Common PS5 / SCE error codes
inline constexpr int32_t SCE_OK = 0;
inline constexpr int32_t SCE_KERNEL_ERROR_EBADF     = -2147418090;
inline constexpr int32_t SCE_KERNEL_ERROR_EFAULT    = -2147418103;
inline constexpr int32_t SCE_KERNEL_ERROR_EINVAL    = -2147418107;
inline constexpr int32_t SCE_KERNEL_ERROR_ENOENT    = -2147418095;
inline constexpr int32_t SCE_KERNEL_ERROR_ETIMEDOUT = -2147418077;
inline constexpr int32_t SCE_KERNEL_ERROR_EBUSY     = static_cast<int32_t>(0x80020010);
inline constexpr int32_t SCE_KERNEL_ERROR_EAGAIN    = static_cast<int32_t>(0x80020023);
inline constexpr int32_t SCE_KERNEL_ERROR_EDEADLK   = static_cast<int32_t>(0x8002000B);
inline constexpr int32_t SCE_KERNEL_ERROR_ENOMEM    = static_cast<int32_t>(0x8002000C);
inline constexpr int32_t SCE_KERNEL_ERROR_EPERM     = static_cast<int32_t>(0x80020001);

// PS5 page size constant (16 KB)
inline constexpr size_t PS5_GUEST_PAGE_SIZE = 16384;

#define EXPECT_SCE_OK(expr) EXPECT_EQ((expr), ::PortPS5::Testing::SCE_OK)
#define ASSERT_SCE_OK(expr) ASSERT_EQ((expr), ::PortPS5::Testing::SCE_OK)
#define EXPECT_SCE_ERR(expected, expr) EXPECT_EQ((expr), (expected))
#define ASSERT_SCE_ERR(expected, expr) ASSERT_EQ((expr), (expected))

// Scoped temporary directory fixture ensuring hermetic test environments
class TempDirectoryFixture : public ::testing::Test {
protected:
    void SetUp() override {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto tid = std::hash<std::thread::id>{}(std::this_thread::get_id());
        std::random_device rd;
        const auto nonce = rd();

        m_tempDir = std::filesystem::temp_directory_path() /
                    ("portps5_test_" + std::to_string(now) + "_" + std::to_string(tid) + "_" + std::to_string(nonce));

        std::error_code ec;
        std::filesystem::create_directories(m_tempDir, ec);
        ASSERT_FALSE(ec) << "Failed to create temp directory: " << ec.message();
    }

    void TearDown() override {
        if (!m_tempDir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(m_tempDir, ec);
        }
    }

    [[nodiscard]] const std::filesystem::path& TempDir() const noexcept {
        return m_tempDir;
    }

    [[nodiscard]] std::filesystem::path MakeSubPath(const std::string& name) const {
        return m_tempDir / name;
    }

private:
    std::filesystem::path m_tempDir;
};

// Memory helper for testing guest 16 KB page-aligned allocations and guard pages
class GuestMemoryHelper {
public:
    static void* AllocatePages(size_t pageCount, bool withGuardPage = false) {
        const size_t allocPages = withGuardPage ? (pageCount + 2) : pageCount;
        const size_t totalBytes = allocPages * PS5_GUEST_PAGE_SIZE;

#ifdef _WIN32
        void* ptr = VirtualAlloc(nullptr, totalBytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!ptr) {
            return nullptr;
        }

        if (withGuardPage) {
            DWORD oldProtect = 0;
            // Set first page as PAGE_NOACCESS
            VirtualProtect(ptr, PS5_GUEST_PAGE_SIZE, PAGE_NOACCESS, &oldProtect);
            // Set last page as PAGE_NOACCESS
            auto* lastPage = static_cast<uint8_t*>(ptr) + (allocPages - 1) * PS5_GUEST_PAGE_SIZE;
            VirtualProtect(lastPage, PS5_GUEST_PAGE_SIZE, PAGE_NOACCESS, &oldProtect);
            return static_cast<uint8_t*>(ptr) + PS5_GUEST_PAGE_SIZE;
        }
        return ptr;
#else
        void* ptr = mmap(nullptr, totalBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (ptr == MAP_FAILED) {
            return nullptr;
        }
        if (withGuardPage) {
            mprotect(ptr, PS5_GUEST_PAGE_SIZE, PROT_NONE);
            auto* lastPage = static_cast<uint8_t*>(ptr) + (allocPages - 1) * PS5_GUEST_PAGE_SIZE;
            mprotect(lastPage, PS5_GUEST_PAGE_SIZE, PROT_NONE);
            return static_cast<uint8_t*>(ptr) + PS5_GUEST_PAGE_SIZE;
        }
        return ptr;
#endif
    }

    static void FreePages(void* ptr, size_t pageCount, bool withGuardPage = false) {
        if (!ptr) {
            return;
        }
#ifdef _WIN32
        void* base = withGuardPage ? static_cast<void*>(static_cast<uint8_t*>(ptr) - PS5_GUEST_PAGE_SIZE) : ptr;
        VirtualFree(base, 0, MEM_RELEASE);
#else
        const size_t allocPages = withGuardPage ? (pageCount + 2) : pageCount;
        void* base = withGuardPage ? static_cast<void*>(static_cast<uint8_t*>(ptr) - PS5_GUEST_PAGE_SIZE) : ptr;
        munmap(base, allocPages * PS5_GUEST_PAGE_SIZE);
#endif
    }
};

} // namespace PortPS5::Testing
