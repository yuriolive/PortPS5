// core/libs/prx/libSceAgcDriver/tests/CrashReporter.cpp
//
// Last-chance crash reporter for the lavapipe driver GoogleTests (bean portps5-3maf). agc_recorder_tests
// segfaults intermittently on the hosted lavapipe runner only, in a different test each time, after
// `[ RUN ]` and before `[ OK ]`; it never reproduces under gdb or locally, and Windows Error Reporting
// LocalDumps writes nothing on the runner. GoogleTest has no SEH guard in this MinGW build
// (GTEST_HAS_SEH is defined only for MSVC/Borland, gtest-port.h), so a fault on any thread, main or
// driver worker, reaches this process-wide unhandled-exception filter, which:
//   - prints the exception code, fault address, faulting and main thread ids at once (plain stderr writes,
//     no loader lock), so even a wedged report still says which thread died;
//   - on a helper thread, prints the faulting thread's stack as module+RVA (the CI log names the module:
//     test exe vs vulkan_lvp.dll vs loader, without symbols) and writes a full minidump to
//     $CRASHDUMP_DIR (or the working directory);
//   - waits for the helper at most HelperTimeoutMs, then chains to the previous filter, so the exit code
//     and ctest's SEGFAULT verdict are unchanged.
// Why a helper thread: module lookup, SymInitialize and MiniDumpWriteDump take the loader lock. The leading
// hypothesis is a fault during vulkan-1.dll/lavapipe unload, where another thread can hold that lock in
// FreeLibrary/DllMain while waiting on the faulting worker; doing the work inline would hang forever and
// turn the SEGFAULT into a ctest Timeout. The bounded wait keeps the crash a crash (thread start itself
// needs the loader lock for DLL_THREAD_ATTACH, so a wedged lock just yields the timeout line instead).
// Test-only diagnostics: never linked into a prx. Installed by a static initializer before main().
// Threading: one report per process (atomic `reported`); a second crashing thread just continues the search.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <gtest/gtest.h>

namespace {

LPTOP_LEVEL_EXCEPTION_FILTER previousFilter = nullptr;
std::atomic<bool> reported{false};
DWORD mainThreadId = 0;
// Bounded wait for the helper thread (stack walk + full dump of a lavapipe test process takes ~1-2 s).
constexpr DWORD HelperTimeoutMs = 30000;

/** Prints `address` as module+RVA (the module is found from the loaded-module list, no symbols needed). */
void PrintAddress(const char* label, DWORD64 address) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, reinterpret_cast<LPCSTR>(address), &module) && module != nullptr) {
        GetModuleFileNameA(module, name, sizeof(name));
        std::fprintf(stderr, "[crash] %s %s+0x%llx\n", label, name, static_cast<unsigned long long>(address - reinterpret_cast<DWORD64>(module)));
    } else {
        std::fprintf(stderr, "[crash] %s 0x%llx (no module: unloaded or JIT code)\n", label, static_cast<unsigned long long>(address));
    }
}

/** Walks the faulting thread's stack from its exception context (a copy, so any thread may walk it). */
void PrintStack(const CONTEXT& faulting) {
    CONTEXT context = faulting;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    const HANDLE process = GetCurrentProcess();
    const HANDLE thread = GetCurrentThread();
    for (int depth = 0; depth < 48; ++depth) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
        if (frame.AddrPC.Offset == 0) break;
        char label[16];
        std::snprintf(label, sizeof(label), "#%d", depth);
        PrintAddress(label, frame.AddrPC.Offset);
    }
}

/** Writes a full-memory minidump of this process to $CRASHDUMP_DIR (or the working directory). */
void WriteDump(EXCEPTION_POINTERS* exception, DWORD faultingThread) {
    char path[MAX_PATH];
    const char* directory = std::getenv("CRASHDUMP_DIR");
    std::snprintf(path, sizeof(path), "%s\\agc_recorder_tests.%lu.dmp", directory != nullptr ? directory : ".", GetCurrentProcessId());
    const HANDLE file = CreateFileA(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        std::fprintf(stderr, "[crash] cannot create %s (error %lu)\n", path, GetLastError());
        return;
    }
    MINIDUMP_EXCEPTION_INFORMATION info{faultingThread, exception, FALSE};
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithFullMemory | MiniDumpWithHandleData | MiniDumpWithThreadInfo);
    const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type, &info, nullptr, nullptr);
    CloseHandle(file);
    std::fprintf(stderr, ok ? "[crash] minidump written to %s\n" : "[crash] MiniDumpWriteDump failed for %s\n", path);
}

/** What the helper thread needs: the crash, and the faulting thread id the minidump attributes it to. */
struct HelperArgs {
    EXCEPTION_POINTERS* exception;
    DWORD faultingThread;
};

/** Loader-lock work, off the faulting thread: module lookup, stack walk and the minidump. */
DWORD WINAPI ReportHelper(void* parameter) {
    const auto& args = *static_cast<const HelperArgs*>(parameter);
    PrintAddress("faulting pc", reinterpret_cast<DWORD64>(args.exception->ExceptionRecord->ExceptionAddress));
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);
    PrintStack(*args.exception->ContextRecord);
    std::fflush(stderr);
    WriteDump(args.exception, args.faultingThread);
    std::fflush(stderr);
    return 0;
}

LONG WINAPI ReportCrash(EXCEPTION_POINTERS* exception) {
    if (!reported.exchange(true)) {
        const auto* record = exception->ExceptionRecord;
        // Unconditional and loader-lock free: printed before anything that could wedge.
        std::fprintf(stderr, "[crash] unhandled exception 0x%08lx on thread %lu (main thread %lu)\n", record->ExceptionCode, GetCurrentThreadId(), mainThreadId);
        if (record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record->NumberParameters >= 2) {
            std::fprintf(stderr, "[crash] %s at 0x%llx\n", record->ExceptionInformation[0] == 8 ? "execute" : record->ExceptionInformation[0] == 1 ? "write" : "read", static_cast<unsigned long long>(record->ExceptionInformation[1]));
        }
        std::fprintf(stderr, "[crash] faulting pc 0x%llx\n", static_cast<unsigned long long>(reinterpret_cast<DWORD64>(record->ExceptionAddress)));
        std::fflush(stderr);
        HelperArgs args{exception, GetCurrentThreadId()};
        const HANDLE helper = CreateThread(nullptr, 0, &ReportHelper, &args, 0, nullptr);
        if (helper == nullptr) {
            std::fprintf(stderr, "[crash] cannot start the report helper (error %lu); no stack or dump\n", GetLastError());
        } else {
            if (WaitForSingleObject(helper, HelperTimeoutMs) != WAIT_OBJECT_0) {
                std::fprintf(stderr, "[crash] report helper did not finish in %lu ms (loader lock held?); stack/dump may be incomplete\n", HelperTimeoutMs);
            }
            CloseHandle(helper);
        }
        std::fflush(stderr);
    }
    return previousFilter != nullptr ? previousFilter(exception) : EXCEPTION_CONTINUE_SEARCH;
}

// Installed after the MinGW CRT's own filter, which it chains to so the exit code stays the same.
[[maybe_unused]] const bool installed = [] {
    mainThreadId = GetCurrentThreadId();
    previousFilter = SetUnhandledExceptionFilter(&ReportCrash);
    return true;
}();

}
// Invariant: a fault on a worker thread (the suspected shape of the lavapipe crash) is reported by
// the filter, with the faulting module, a stack and a minidump, and still kills the process (the filter
// must not swallow the crash). Fails without the reporter: the child dies silently and the regex misses.
TEST(CrashReporterDeathTest, WorkerThreadFaultIsReportedAndStillFatal) {
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    // GoogleTest's Windows regex is the simple dialect (no brackets, '.' stops at a newline), so each
    // report line is matched by its own death assertion.
    // Each child writes a ~100 MB full dump: they go to a private directory the parent removes at the end,
    // never to the CI's CRASHDUMP_DIR (whose real dumps must survive). The "threadsafe" style re-runs this
    // body in every child, so the parent names the directory once and hands it down in an inherited
    // test-only variable; the parent's own CRASHDUMP_DIR is restored afterwards.
    const char* inherited = std::getenv("CRASH_REPORTER_TEST_DIR");
    const std::filesystem::path dumps = inherited != nullptr ? std::filesystem::path(inherited) : std::filesystem::temp_directory_path() / ("portps5_crash_reporter_" + std::to_string(GetCurrentProcessId()));
    const char* original = std::getenv("CRASHDUMP_DIR");
    const std::string restore = original != nullptr ? original : "";
    _putenv_s("CRASH_REPORTER_TEST_DIR", dumps.string().c_str());
    _putenv_s("CRASHDUMP_DIR", dumps.string().c_str());
    std::filesystem::create_directories(dumps);
    const auto crashOnWorker = [] {
        std::thread([] { *static_cast<volatile int*>(nullptr) = 1; }).join();
    };
    EXPECT_DEATH(crashOnWorker(), R"(\[crash\] unhandled exception 0xc0000005 on thread \d+ \(main thread \d+\))");
    EXPECT_DEATH(crashOnWorker(), R"(\[crash\] write at 0x0)");
    EXPECT_DEATH(crashOnWorker(), R"(\[crash\] #0 .*agc_recorder_tests\.exe\+0x)");
    EXPECT_DEATH(crashOnWorker(), R"(\[crash\] minidump written to)");
    std::error_code ignored;
    std::filesystem::remove_all(dumps, ignored);
    _putenv_s("CRASH_REPORTER_TEST_DIR", "");
    _putenv_s("CRASHDUMP_DIR", restore.c_str());
}

#endif
