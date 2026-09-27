#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libc/include/Shutdown.hpp"
#include "prx/libSceSystemService/SystemService.hpp"
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <cstring>
#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif
extern "C" int APS5_VABI sceSystemServiceLoadExec(const char*, const char* const*);
extern "C" void APS5_VABI _Exit_nid_postfix(int);
namespace {
bool cleaned = false;
void Cleanup() { cleaned = true; }
void VerifyExit() {
    if (!cleaned) std::_Exit(1);
    std::puts("Guest shutdown and atexit completed");
}
void Require(bool value) { if (!value) std::abort(); }
void UnexpectedCleanup() { std::_Exit(3); }
}
int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1], "--loadexec") == 0) {
        // Child mode: replacing the executable is unsupported and aborts.
        // Returning normally would mean LoadExec wrongly succeeded.
        sceSystemServiceLoadExec("/app0/another.bin", nullptr);
        std::_Exit(7);
    }
    if (argc > 1 && std::strcmp(argv[1], "--immediate") == 0) {
        LibcRegisterShutdown_nid_postfix(UnexpectedCleanup);
        Require(std::atexit(UnexpectedCleanup) == 0);
        _Exit_nid_postfix(0);
        return 2;
    }
    if (argc > 1) {
        LibcRegisterShutdown_nid_postfix(Cleanup);
        Require(std::atexit(VerifyExit) == 0);
        sceSystemServiceLoadExec("exit", nullptr);
        return 2;
    }
    Require(sceSystemServiceLoadExec(nullptr, nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
    Require(sceSystemServiceLoadExec("", nullptr) == SYSTEM_SERVICE_ERROR_PARAMETER);
    // Replacing the executable aborts the process, so the abort is observed
    // in a child: any exit (abort, terminate, or the _Exit(7) fallback) is
    // non-zero, while a wrongful success would exit 0.
#ifdef _WIN32
    char* childArgs[] = {argv[0], const_cast<char*>("--loadexec"), nullptr};
    const intptr_t status = _spawnv(_P_WAIT, argv[0], childArgs);
    Require(status != 0);
#else
    const pid_t child = fork();
    Require(child >= 0);
    if (child == 0) {
        sceSystemServiceLoadExec("/app0/another.bin", nullptr);
        _exit(7);
    }
    int status = 0;
    Require(waitpid(child, &status, 0) == child);
    Require(!WIFEXITED(status) || WEXITSTATUS(status) != 0);
#endif
}
