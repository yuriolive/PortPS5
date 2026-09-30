// core/libs/prx/libSceAgcDriver/tests/RecorderTestSupport.hpp
//
// Shared fixtures for the Recorder and HostImport GoogleTests: a Vulkan test device (lavapipe on hosted
// CI, any device locally; it prefers one with VK_EXT_external_memory_host so the import path is
// testable) and a scriptable IWriteTracker. Header-only; no game data, no shaders.
#ifndef CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_RECORDERTESTSUPPORT_HPP
#define CORE_LIBS_PRX_LIBSCEAGCDRIVER_TESTS_RECORDERTESTSUPPORT_HPP

#include "prx/libSceAgcDriver/Graphics/include/BufferPool.hpp"
#include "prx/libSceAgcDriver/Graphics/include/Context.hpp"
#include "prx/libc/include/WriteTracker.hpp"
#include <SDL_loadso.h>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

namespace AgcDriverTest {

/** @brief Page-aligned host allocation (guest memory stand-in that host import can target). */
class AlignedBlock {
public:
    AlignedBlock(std::size_t bytes, std::size_t alignment) : size(bytes) {
#ifdef _WIN32
        data = static_cast<std::byte*>(_aligned_malloc(bytes, alignment));
#else
        data = static_cast<std::byte*>(std::aligned_alloc(alignment, bytes));
#endif
        if (data != nullptr) std::memset(data, 0, bytes);
    }
    ~AlignedBlock() {
#ifdef _WIN32
        _aligned_free(data);
#else
        std::free(data);
#endif
    }
    AlignedBlock(const AlignedBlock&) = delete;
    AlignedBlock& operator=(const AlignedBlock&) = delete;
    std::byte* Data() const { return data; }
    std::uint64_t Address() const { return reinterpret_cast<std::uint64_t>(data); }
    std::size_t Size() const { return size; }

private:
    std::byte* data = nullptr;
    std::size_t size;
};

/**
 * @brief IWriteTracker stand-in: every in-window page reports `state`; Collect runs the registered flush
 * hook (like the real tracker) and returns `generation`; MarkWritten calls are recorded.
 */
class FakeTracker final : public PortPS5::GuestMemory::IWriteTracker {
public:
    std::uint64_t Collect(std::uint64_t address, std::uint64_t bytes) override {
        PortPS5::GuestMemory::FlushHook currentHook;
        void* currentContext;
        {
            std::lock_guard lock(mutex);
            currentHook = hook;
            currentContext = hookContext;
        }
        // Like WriteWatchTracker::Collect: the hook runs first, with no tracker lock held.
        if (currentHook != nullptr) currentHook(currentContext, address, bytes);
        return generation.load();
    }
    PortPS5::GuestMemory::PinToken Pin(std::span<const PortPS5::GuestMemory::AddrRange>) override { return {1}; }
    void Unpin(PortPS5::GuestMemory::PinToken) noexcept override {}
    PortPS5::GuestMemory::PageState PageStateAt(std::uint64_t) const override { return state.load(); }
    std::uint64_t MarkWritten(std::uint64_t address, std::uint64_t bytes) override {
        std::lock_guard lock(mutex);
        written.emplace_back(address, bytes);
        return ++writtenGeneration;
    }
    void SetFlushHook(PortPS5::GuestMemory::FlushHook newHook, void* context) override {
        std::lock_guard lock(mutex);
        hook = newHook;
        hookContext = context;
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> Written() const {
        std::lock_guard lock(mutex);
        return written;
    }

    std::atomic<std::uint64_t> generation{1};
    std::atomic<PortPS5::GuestMemory::PageState> state{PortPS5::GuestMemory::PageState::ReadWrite};

private:
    mutable std::mutex mutex;
    PortPS5::GuestMemory::FlushHook hook = nullptr;
    void* hookContext = nullptr;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> written;
    std::uint64_t writtenGeneration = 100;
};

/**
 * @brief A real Vulkan compute-capable device. Construction never throws: check Ok() and skip the test
 * (GTEST_SKIP) when no Vulkan loader or device exists.
 */
class VulkanTestDevice {
public:
    VulkanTestDevice() {
        try {
            create();
        } catch (const std::exception& error) {
            failure = error.what();
            release();
        }
    }
    ~VulkanTestDevice() { release(); }
    VulkanTestDevice(const VulkanTestDevice&) = delete;
    VulkanTestDevice& operator=(const VulkanTestDevice&) = delete;

    bool Ok() const { return failure.empty(); }
    const std::string& Failure() const { return failure; }
    const AgcDriver::Graphics::Context& GetContext() const { return context; }
    bool Timeline() const { return timeline; }

private:
    template<typename TFunction>
    TFunction instanceFunction(const char* name) const {
        const auto result = reinterpret_cast<TFunction>(instanceProc(instance, name));
        AgcDriver::Graphics::Require(result != nullptr, name);
        return result;
    }

    void create() {
        using namespace AgcDriver::Graphics;
#ifdef _WIN32
        library = SDL_LoadObject("vulkan-1.dll");
#else
        library = SDL_LoadObject("libvulkan.so.1");
#endif
        Require(library != nullptr, "cannot load the Vulkan loader");
        instanceProc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(SDL_LoadFunction(library, "vkGetInstanceProcAddr"));
        Require(instanceProc != nullptr, "missing vkGetInstanceProcAddr");
        VkApplicationInfo application{VK_STRUCTURE_TYPE_APPLICATION_INFO};
        application.apiVersion = VK_API_VERSION_1_3;
        VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
        info.pApplicationInfo = &application;
        Check(reinterpret_cast<PFN_vkCreateInstance>(instanceProc(nullptr, "vkCreateInstance"))(&info, nullptr, &instance), "vkCreateInstance");
        std::uint32_t count = 0;
        const auto enumerate = instanceFunction<PFN_vkEnumeratePhysicalDevices>("vkEnumeratePhysicalDevices");
        Check(enumerate(instance, &count, nullptr), "vkEnumeratePhysicalDevices");
        Require(count != 0, "no Vulkan device");
        std::vector<VkPhysicalDevice> devices(count);
        Check(enumerate(instance, &count, devices.data()), "vkEnumeratePhysicalDevices");
        const auto enumerateExtensions = instanceFunction<PFN_vkEnumerateDeviceExtensionProperties>("vkEnumerateDeviceExtensionProperties");
        const auto hasExtension = [&](VkPhysicalDevice device, const char* name) {
            std::uint32_t n = 0;
            enumerateExtensions(device, nullptr, &n, nullptr);
            std::vector<VkExtensionProperties> available(n);
            enumerateExtensions(device, nullptr, &n, available.data());
            for (const auto& item : available) {
                if (std::strcmp(item.extensionName, name) == 0) return true;
            }
            return false;
        };
        // Prefer a device that can import host memory so the import path is exercised where possible.
        context.physical = devices.front();
        for (const auto device : devices) {
            if (hasExtension(device, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME)) {
                context.physical = device;
                break;
            }
        }
        const bool hostImport = hasExtension(context.physical, VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
        timeline = hasExtension(context.physical, VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
        const bool address = hasExtension(context.physical, VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);

        const auto queues = instanceFunction<PFN_vkGetPhysicalDeviceQueueFamilyProperties>("vkGetPhysicalDeviceQueueFamilyProperties");
        queues(context.physical, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        queues(context.physical, &count, families.data());
        std::uint32_t family = 0;
        while (family < count && (families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) ++family;
        Require(family < count, "no Vulkan compute queue");

        const float priority = 1;
        VkDeviceQueueCreateInfo queue{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
        queue.queueFamilyIndex = family;
        queue.queueCount = 1;
        queue.pQueuePriorities = &priority;
        std::vector<const char*> extensions;
        VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR};
        timelineFeatures.timelineSemaphore = VK_TRUE;
        VkPhysicalDeviceBufferDeviceAddressFeaturesKHR addressFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR};
        addressFeatures.bufferDeviceAddress = VK_TRUE;
        void* chain = nullptr;
        if (timeline) {
            extensions.push_back(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
            timelineFeatures.pNext = chain;
            chain = &timelineFeatures;
        }
        if (address) {
            extensions.push_back(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
            addressFeatures.pNext = chain;
            chain = &addressFeatures;
        }
        if (hostImport) extensions.push_back(VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME);
        VkDeviceCreateInfo device{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, chain};
        device.queueCreateInfoCount = 1;
        device.pQueueCreateInfos = &queue;
        device.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
        device.ppEnabledExtensionNames = extensions.data();
        Check(instanceFunction<PFN_vkCreateDevice>("vkCreateDevice")(context.physical, &device, nullptr, &context.device), "vkCreateDevice");
        context.deviceProc = instanceFunction<PFN_vkGetDeviceProcAddr>("vkGetDeviceProcAddr");
        instanceFunction<PFN_vkGetPhysicalDeviceMemoryProperties>("vkGetPhysicalDeviceMemoryProperties")(context.physical, &context.memory);
        VkPhysicalDeviceProperties properties{};
        instanceFunction<PFN_vkGetPhysicalDeviceProperties>("vkGetPhysicalDeviceProperties")(context.physical, &properties);
        context.limits = properties.limits;
        context.bufferDeviceAddress = address;
        context.externalMemoryHost = hostImport;
        if (hostImport) {
            VkPhysicalDeviceExternalMemoryHostPropertiesEXT host{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT};
            VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, &host};
            instanceFunction<PFN_vkGetPhysicalDeviceProperties2>("vkGetPhysicalDeviceProperties2")(context.physical, &properties2);
            context.hostImportAlignment = host.minImportedHostPointerAlignment;
        }
        context.Function<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(context.device, family, 0, &context.queue);
        VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        pool.queueFamilyIndex = family;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        Check(context.Function<PFN_vkCreateCommandPool>("vkCreateCommandPool")(context.device, &pool, nullptr, &context.pool), "vkCreateCommandPool");
        // One shared pool: every Context copy (Recorder, HostImport, Buffer) reuses it, and release()
        // drops the last reference before the device goes.
        GetBufferPool(context);
    }

    void release() noexcept {
        if (context.device != VK_NULL_HANDLE && context.deviceProc != nullptr) {
            context.Function<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(context.device);
            // The pooled buffers of Buffer must go before the device does.
            context.bufferPool.reset();
            if (context.pool != VK_NULL_HANDLE) context.Function<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(context.device, context.pool, nullptr);
            context.Function<PFN_vkDestroyDevice>("vkDestroyDevice")(context.device, nullptr);
        }
        context = AgcDriver::Graphics::Context{};
        if (instance != VK_NULL_HANDLE) reinterpret_cast<PFN_vkDestroyInstance>(instanceProc(instance, "vkDestroyInstance"))(instance, nullptr);
        instance = VK_NULL_HANDLE;
        if (library != nullptr) SDL_UnloadObject(library);
        library = nullptr;
    }

    void* library = nullptr;
    PFN_vkGetInstanceProcAddr instanceProc = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    AgcDriver::Graphics::Context context{};
    bool timeline = false;
    std::string failure;
};

}

#endif
