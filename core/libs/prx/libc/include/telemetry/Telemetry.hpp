// Runtime telemetry core: frame-time log, watchdog and structured events
// (docs/spec/verification.md section 4.1, schema "portps5.telemetry/1").
//
// Subsystem: telemetry (libc, host-only, no guest-called exports). This header
// is the pure, clock-injected core: every method takes the current time in
// milliseconds from the caller, and output goes through a LineSink, so unit
// tests drive it with a synthetic clock and an in-memory sink and never sleep.
// The process-wide instance, the file sink and the watchdog thread live in
// src/Telemetry.cpp behind `_nid_no_patch` C exports (cross-prx rule in
// docs/spec/build-toolchain.md).
//
// Privacy: events carry only numbers and fixed enum-like strings chosen by
// the runtime, never guest text, paths or frame contents.
//
// Threading: Log serialises writes with one mutex (a flip-rate lock, not a hot
// path). Watchdog counters are atomics so guest threads never block on it.

#ifndef CORE_LIBS_PRX_LIBC_TELEMETRY_TELEMETRY_HPP
#define CORE_LIBS_PRX_LIBC_TELEMETRY_TELEMETRY_HPP

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>

namespace PortPS5 {
namespace Telemetry {

/** @brief Schema tag written in the run.start record and checked by tools/regress.py. */
inline constexpr const char* kSchema = "portps5.telemetry/1";
/** @brief A gap above this between presents is a stall (PRD 4.3). */
inline constexpr std::uint64_t kStallMs = 1000;
/** @brief No present, or no guest progress, for longer than this is a softlock (PRD 4.3). */
inline constexpr std::uint64_t kSoftlockMs = 30000;

/** @brief Destination for finished JSONL lines (no trailing newline in `line`). */
class LineSink {
public:
    virtual ~LineSink() = default;
    /**
     * @brief Persists one record.
     * @param line A complete JSON object without a newline.
     */
    virtual void Write(std::string_view line) = 0;
};

/** @brief Appends lines to a FILE* and flushes each one, so a crash loses nothing. */
class FileSink final : public LineSink {
public:
    /** @brief Takes ownership of `file`. @param file Open stream, or nullptr (writes are dropped). */
    explicit FileSink(std::FILE* file) : m_file(file) {}
    ~FileSink() override {
        if (m_file) std::fclose(m_file);
    }
    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    /** @brief Writes `line` plus a newline and flushes. */
    void Write(std::string_view line) override {
        if (!m_file) return;
        std::fwrite(line.data(), 1, line.size(), m_file);
        std::fputc('\n', m_file);
        std::fflush(m_file);
    }

private:
    std::FILE* m_file;
};

/** @brief One numeric field of a structured event. */
using Field = std::pair<const char*, double>;

/**
 * @brief Structured JSONL event writer and frame-time recorder.
 *
 * Event names and field keys must be string literals chosen by the runtime
 * (they are not escaped); values are numbers, so no game text can enter.
 */
class Log {
public:
    /** @brief Binds the sink. @param sink Not owned; must outlive the Log. */
    explicit Log(LineSink& sink) : m_sink(sink) {}

    /**
     * @brief Writes the mandatory first record.
     * @param width,height Output resolution. @param warmCache True when the pipeline cache is warm.
     * @param audioDevice "wasapi-default" or "none".
     */
    void RunStart(int width, int height, bool warmCache, const char* audioDevice) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sink.Write(std::string("{\"ev\":\"run.start\",\"schema\":\"") + kSchema + "\",\"resolution\":\"" +
                     std::to_string(width) + "x" + std::to_string(height) + "\",\"pipeline_cache\":\"" +
                     (warmCache ? "warm" : "cold") + "\",\"audio_device\":\"" + audioDevice + "\"}");
    }

    /**
     * @brief Records one presented frame.
     * @param nowMs Monotonic ms since run start at the present; also written as `t_ms` so the runner can see a hang after the last present. The first call only sets the
     *        baseline and writes nothing, since there is no previous present.
     * @return The interval in ms since the previous present, or 0 on the first call.
     */
    std::uint64_t Frame(std::uint64_t nowMs) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) return 0;
        const bool first = !m_havePresent;
        const std::uint64_t dt = first ? 0 : nowMs - m_lastPresentMs;
        m_havePresent = true;
        m_lastPresentMs = nowMs;
        if (!first) m_sink.Write("{\"ev\":\"frame\",\"dt_ms\":" + std::to_string(dt) + ",\"t_ms\":" + std::to_string(nowMs) + "}");
        return dt;
    }

    /**
     * @brief Writes a `heartbeat` record carrying the runtime clock.
     *
     * Written about once per second by the watchdog thread. tools/regress.py
     * compares the last heartbeat with the last present, both on this clock, to
     * see a hang after the final present of a run it had to kill.
     * @param nowMs Monotonic ms since run start.
     */
    void Heartbeat(std::uint64_t nowMs) { Event("heartbeat", {{"t_ms", static_cast<double>(nowMs)}}); }

    /**
     * @brief Writes a structured event with numeric fields.
     * @param name Event name literal (e.g. "audio.underrun", "dialog.open", "video_latency_ms").
     * @param fields Key literal and number pairs.
     */
    void Event(const char* name, std::initializer_list<Field> fields = {}) {
        std::string line = std::string("{\"ev\":\"") + name + "\"";
        for (const Field& f : fields) {
            line += std::string(",\"") + f.first + "\":" + FormatNumber(f.second);
        }
        line += "}";
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_closed) return;
        m_sink.Write(line);
        // run.end is the documented end marker: nothing may follow it, even from a
        // thread that raced past the runtime's stopped flag.
        if (std::string_view(name) == "run.end") m_closed = true;
    }

    /** @brief Formats a number as JSON (integers without a fraction, others with 3 decimals). */
    static std::string FormatNumber(double v) {
        if (v == static_cast<double>(static_cast<long long>(v))) return std::to_string(static_cast<long long>(v));
        char buf[40];
        std::snprintf(buf, sizeof buf, "%.3f", v);
        return buf;
    }

private:
    LineSink& m_sink;
    std::mutex m_mutex;
    bool m_closed = false;
    bool m_havePresent = false;
    std::uint64_t m_lastPresentMs = 0;
};

/** @brief Which heartbeat tripped the watchdog. */
enum class WatchdogVerdict { Ok, PresentStall, GuestStall };

/**
 * @brief Softlock detector over two heartbeats: presents and guest thread progress.
 *
 * Evaluate is a pure function of the heartbeats and the supplied time, so tests
 * use a synthetic clock. A verdict latches (reported once) because the runtime
 * reacts by logging and aborting, never by skipping work.
 */
class Watchdog {
public:
    /** @brief `limitMs` is the idle limit; the default is the PRD softlock definition. */
    explicit Watchdog(std::uint64_t limitMs = kSoftlockMs) : m_limitMs(limitMs) {}

    /** @brief Starts the idle clocks. Both heartbeats are measured from `nowMs` until first seen. */
    void Arm(std::uint64_t nowMs) {
        m_lastPresentMs.store(nowMs, std::memory_order_relaxed);
        m_lastGuestMs.store(nowMs, std::memory_order_relaxed);
        m_armed.store(true, std::memory_order_release);
    }
    /**
     * @brief Records a present (called from the presenter).
     *
     * The present check stays disabled until the first call, like the guest
     * check, so boot, a cold cache or a long first load cannot trip it and an
     * unwired presenter cannot false-positive. A title that never presents is
     * caught by tools/regress.py instead (silence from run start to the kill).
     */
    void NotePresent(std::uint64_t nowMs) {
        m_lastPresentMs.store(nowMs, std::memory_order_relaxed);
        m_presentSeen.store(true, std::memory_order_release);
    }
    /**
     * @brief Records guest thread progress (any guest-visible forward step).
     *
     * The guest-progress check stays disabled until the first call, so a
     * runtime that has not wired this heartbeat cannot false-positive.
     */
    void NoteGuestProgress(std::uint64_t nowMs) {
        m_lastGuestMs.store(nowMs, std::memory_order_relaxed);
        m_guestWired.store(true, std::memory_order_release);
    }

    /**
     * @brief Checks both heartbeats at `nowMs`.
     * @return Ok, or the first trip (latched: later calls return Ok).
     */
    WatchdogVerdict Evaluate(std::uint64_t nowMs, std::uint64_t* idleMsOut = nullptr) {
        if (!m_armed.load(std::memory_order_acquire) || m_tripped.load(std::memory_order_acquire)) {
            return WatchdogVerdict::Ok;
        }
        const std::uint64_t present = m_lastPresentMs.load(std::memory_order_relaxed);
        const std::uint64_t guest = m_lastGuestMs.load(std::memory_order_relaxed);
        const std::uint64_t presentIdle = nowMs > present ? nowMs - present : 0;
        const std::uint64_t guestIdle = nowMs > guest ? nowMs - guest : 0;
        WatchdogVerdict v = WatchdogVerdict::Ok;
        std::uint64_t idle = 0;
        if (m_presentSeen.load(std::memory_order_acquire) && presentIdle > m_limitMs) {
            v = WatchdogVerdict::PresentStall;
            idle = presentIdle;
        } else if (m_guestWired.load(std::memory_order_acquire) && guestIdle > m_limitMs) {
            v = WatchdogVerdict::GuestStall;
            idle = guestIdle;
        }
        if (v != WatchdogVerdict::Ok) {
            m_tripped.store(true, std::memory_order_release);
            if (idleMsOut) *idleMsOut = idle;
        }
        return v;
    }

private:
    const std::uint64_t m_limitMs;
    std::atomic<bool> m_armed{false};
    std::atomic<bool> m_tripped{false};
    std::atomic<bool> m_guestWired{false};
    std::atomic<bool> m_presentSeen{false};
    std::atomic<std::uint64_t> m_lastPresentMs{0};
    std::atomic<std::uint64_t> m_lastGuestMs{0};
};

/** @brief Snapshot returned by an audio counter source (the single host mixer, spec audio.md). */
struct AudioCounters {
    std::uint64_t underruns = 0;
    double latencyMs = 0.0;
};

/**
 * @brief Once-per-second sampler turning counters into structured events.
 *
 * Emits `audio.underrun` with the delta since the last sample, and, while an
 * FMV window is open, `av.offset` = audio latency minus video latency (the
 * skeleton in docs/spec/video-fmv.md) plus `video_latency_ms`.
 */
class Sampler {
public:
    /** @brief Binds the Log. @param log Not owned. */
    explicit Sampler(Log& log) : m_log(log) {}

    /** @brief Presenter publishes the latest video latency (submit to present return plus queued images). */
    void SetVideoLatencyMs(double ms) { m_videoLatencyMs.store(ms, std::memory_order_relaxed); }
    /** @brief Opens or closes the FMV window (driven by per-title references, never by code). */
    void SetFmvWindow(bool open) { m_fmvOpen.store(open, std::memory_order_relaxed); }

    /**
     * @brief Samples once; call from a 1 s timer.
     * @param audio Current mixer counters, or the zero value when no device is open.
     */
    void Sample(const AudioCounters& audio) {
        if (audio.underruns > m_lastUnderruns) {
            m_log.Event("audio.underrun", {{"n", static_cast<double>(audio.underruns - m_lastUnderruns)}});
        }
        m_lastUnderruns = audio.underruns;
        if (m_fmvOpen.load(std::memory_order_relaxed)) {
            const double video = m_videoLatencyMs.load(std::memory_order_relaxed);
            m_log.Event("video_latency_ms", {{"ms", video}});
            m_log.Event("av.offset", {{"ms", audio.latencyMs - video}});
        }
    }

private:
    Log& m_log;
    std::atomic<double> m_videoLatencyMs{0.0};
    std::atomic<bool> m_fmvOpen{false};
    std::uint64_t m_lastUnderruns = 0;
};

}  // namespace Telemetry
}  // namespace PortPS5

#endif  // CORE_LIBS_PRX_LIBC_TELEMETRY_TELEMETRY_HPP
