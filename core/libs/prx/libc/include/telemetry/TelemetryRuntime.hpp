// Process-wide telemetry entry points (docs/spec/verification.md section 4.1).
//
// Subsystem: telemetry (libc). Host-only C exports with verbatim
// `_nid_no_patch` names so other prx (presenter, audio mixer, dialogs) resolve
// them across DLL boundaries (cross-prx rule, docs/spec/build-toolchain.md).
// None is guest-callable, so none uses APS5_VABI. Every function is safe to
// call before Start or after Shutdown (it does nothing), so callers need no
// guards. Start runs once at start-up; the rest are callable from any thread.
//
// Call sites owned by other work (kept out of this change on purpose):
// the presenter calls NotePresent and SetVideoLatencyMs once per present, and
// guest thread hooks call NoteGuestProgress.

#ifndef CORE_LIBS_PRX_LIBC_TELEMETRY_TELEMETRYRUNTIME_HPP
#define CORE_LIBS_PRX_LIBC_TELEMETRY_TELEMETRYRUNTIME_HPP

#include <cstdint>

#include "prx/libc/include/telemetry/Telemetry.hpp"

/** @brief Source of audio counters, registered by the single host mixer. */
using PortPS5TelemetryAudioSource = PortPS5::Telemetry::AudioCounters (*)();
/** @brief Called by the watchdog before it aborts, to write per-queue state via Event. */
using PortPS5TelemetryDiagnosticsHook = void (*)();

/**
 * @brief Opens <installDir>/logs/telemetry.jsonl, writes run.start, arms the watchdog.
 * @param installDir Directory of the converted executable.
 * @param width,height Output resolution. @param warmCache Non-zero when the pipeline cache is warm.
 * @param audioDevice "wasapi-default" or "none".
 * @return true when the log was opened; false leaves telemetry off (and logs why).
 */
extern "C" bool PortPS5_Telemetry_Start_nid_no_patch(const char* installDir, int width, int height,
                                                     int warmCache, const char* audioDevice);
/** @brief Records one present: writes a frame record and feeds the watchdog. */
extern "C" void PortPS5_Telemetry_NotePresent_nid_no_patch();
/** @brief Records guest thread progress; enables the guest half of the watchdog. */
extern "C" void PortPS5_Telemetry_NoteGuestProgress_nid_no_patch();
/** @brief Writes a numeric-only structured event (e.g. "dialog.open", "spirv.compile", "pipeline.create"). */
extern "C" void PortPS5_Telemetry_Event_nid_no_patch(const char* name, const char* key, double value);
/** @brief Registers the audio counter source sampled once per second. */
extern "C" void PortPS5_Telemetry_SetAudioSource_nid_no_patch(PortPS5TelemetryAudioSource source);
/** @brief Registers the hook that dumps per-queue state when the watchdog trips. */
extern "C" void PortPS5_Telemetry_SetDiagnosticsHook_nid_no_patch(PortPS5TelemetryDiagnosticsHook hook);
/** @brief Publishes the latest video latency (submit to present return plus queued images). */
extern "C" void PortPS5_Telemetry_SetVideoLatencyMs_nid_no_patch(double ms);
/** @brief Opens or closes the FMV window used by the A/V offset sampler. */
extern "C" void PortPS5_Telemetry_SetFmvWindow_nid_no_patch(bool open);
/** @brief Writes run.end (clean end marker) and stops the watchdog thread. */
extern "C" void PortPS5_Telemetry_Shutdown_nid_no_patch(std::uint64_t captureSplit, std::uint64_t writeFaults);

#endif  // CORE_LIBS_PRX_LIBC_TELEMETRY_TELEMETRYRUNTIME_HPP
