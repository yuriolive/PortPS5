// Process-wide telemetry instance, file sink and watchdog thread
// (docs/spec/verification.md section 4.1). The pure logic is in
// include/telemetry/Telemetry.hpp and is unit-tested with a synthetic clock;
// this file only adds the real clock, the file and one polling thread.

#include "prx/libc/include/telemetry/TelemetryRuntime.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

#include "prx/libc/include/General.hpp"

namespace {
using namespace PortPS5::Telemetry;
using Clock = std::chrono::steady_clock;

/**
 * Everything the runtime owns. Heap allocated once by Start and intentionally
 * never freed before process exit, so a late NotePresent from a straggler
 * thread cannot touch a destroyed object. Shutdown only stops the thread.
 */
struct Runtime {
    explicit Runtime(std::FILE* f) : sink(f), log(sink), sampler(log) {}
    FileSink sink;
    Log log;
    Watchdog watchdog;
    Sampler sampler;
    Clock::time_point origin = Clock::now();
    std::atomic<PortPS5TelemetryAudioSource> audio{nullptr};
    std::atomic<PortPS5TelemetryDiagnosticsHook> hook{nullptr};
    std::thread thread;
    std::mutex stopMutex;
    std::condition_variable stopCv;
    bool stop = false;
    // Set before run.end is written so late callers (straggler presenter, teardown
    // dialogs) cannot append records after the documented end marker.
    std::atomic<bool> stopped{false};

    /** Milliseconds since Start on the monotonic clock. */
    std::uint64_t NowMs() const {
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - origin).count());
    }
};

std::atomic<Runtime*> g_runtime{nullptr};

/** The runtime if started and not yet shut down, else nullptr (every export is then a no-op). */
Runtime* Active() {
    Runtime* rt = g_runtime.load();
    return (rt && !rt->stopped.load(std::memory_order_acquire)) ? rt : nullptr;
}

/**
 * Watchdog thread body: once per second sample the audio counters and check
 * both heartbeats. A trip writes the softlock event, runs the diagnostics
 * hook, writes run.end and aborts through the logging abort path. It never
 * skips work: a hung title is reported and ended, not papered over.
 */
void WatchdogLoop(Runtime* rt) {
    std::unique_lock<std::mutex> lock(rt->stopMutex);
    while (!rt->stop) {
        rt->stopCv.wait_for(lock, std::chrono::seconds(1), [rt] { return rt->stop; });
        if (rt->stop) break;
        lock.unlock();
        PortPS5TelemetryAudioSource src = rt->audio.load();
        rt->sampler.Sample(src ? src() : AudioCounters{});
        std::uint64_t idleMs = 0;
        const WatchdogVerdict v = rt->watchdog.Evaluate(rt->NowMs(), &idleMs);
        if (v != WatchdogVerdict::Ok) {
            rt->log.Event("softlock", {{"idle_ms", static_cast<double>(idleMs)},
                                       {"reason", v == WatchdogVerdict::PresentStall ? 1.0 : 2.0}});
            if (PortPS5TelemetryDiagnosticsHook h = rt->hook.load()) h();
            rt->log.Event("run.end", {{"capture_split", 0}, {"write_faults", 0}});
            Unsupported("telemetry watchdog: softlock (no present or no guest progress)");
        }
        lock.lock();
    }
}
}  // namespace

extern "C" bool PortPS5_Telemetry_Start_nid_no_patch(const char* installDir, int width, int height,
                                                     int warmCache, const char* audioDevice) {
    if (g_runtime.load()) return true;
    std::error_code ec;
    const std::filesystem::path dir = std::filesystem::path(installDir ? installDir : ".") / "logs";
    std::filesystem::create_directories(dir, ec);
    std::FILE* f = std::fopen((dir / "telemetry.jsonl").string().c_str(), "wb");
    if (!f) {
        std::fprintf(stderr, "telemetry: cannot open logs/telemetry.jsonl; telemetry is off\n");
        return false;
    }
    auto* rt = new Runtime(f);
    rt->log.RunStart(width, height, warmCache != 0, audioDevice ? audioDevice : "none");
    rt->watchdog.Arm(rt->NowMs());
    Runtime* expected = nullptr;
    if (!g_runtime.compare_exchange_strong(expected, rt)) {
        // Lost a start race: the winner owns the file; drop ours (closes via FileSink).
        delete rt;
        return true;
    }
    rt->thread = std::thread(WatchdogLoop, rt);
    return true;
}

extern "C" void PortPS5_Telemetry_NotePresent_nid_no_patch() {
    if (Runtime* rt = Active()) {
        const std::uint64_t now = rt->NowMs();
        rt->watchdog.NotePresent(now);
        rt->log.Frame(now);
    }
}

extern "C" void PortPS5_Telemetry_NoteGuestProgress_nid_no_patch() {
    if (Runtime* rt = Active()) rt->watchdog.NoteGuestProgress(rt->NowMs());
}

extern "C" void PortPS5_Telemetry_Event_nid_no_patch(const char* name, const char* key, double value) {
    if (Runtime* rt = Active()) {
        if (key) {
            rt->log.Event(name, {{key, value}});
        } else {
            rt->log.Event(name);
        }
    }
}

extern "C" void PortPS5_Telemetry_SetAudioSource_nid_no_patch(PortPS5TelemetryAudioSource source) {
    if (Runtime* rt = Active()) rt->audio.store(source);
}

extern "C" void PortPS5_Telemetry_SetDiagnosticsHook_nid_no_patch(PortPS5TelemetryDiagnosticsHook hook) {
    if (Runtime* rt = Active()) rt->hook.store(hook);
}

extern "C" void PortPS5_Telemetry_SetVideoLatencyMs_nid_no_patch(double ms) {
    if (Runtime* rt = Active()) rt->sampler.SetVideoLatencyMs(ms);
}

extern "C" void PortPS5_Telemetry_SetFmvWindow_nid_no_patch(bool open) {
    if (Runtime* rt = Active()) rt->sampler.SetFmvWindow(open);
}

extern "C" void PortPS5_Telemetry_Shutdown_nid_no_patch(std::uint64_t captureSplit, std::uint64_t writeFaults) {
    Runtime* rt = g_runtime.load();
    if (!rt) return;
    {
        std::lock_guard<std::mutex> lock(rt->stopMutex);
        if (rt->stop) return;
        rt->stop = true;
        rt->stopped.store(true, std::memory_order_release);
    }
    rt->stopCv.notify_all();
    if (rt->thread.joinable()) rt->thread.join();
    rt->log.Event("run.end", {{"capture_split", static_cast<double>(captureSplit)},
                              {"write_faults", static_cast<double>(writeFaults)}});
}
