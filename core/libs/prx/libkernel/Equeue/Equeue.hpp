// PortPS5 libkernel event queue (kqueue-style equeue) public interface.
//
// Subsystem: libkernel Equeue. Owns KernelEqueuePrivate (shared_ptr lifetime,
// pinned for the duration of each call) and the guest-visible sceKernel*Equeue
// exports. Every export uses APS5_VABI (System V ABI). Error returns are SCE
// kernel codes (0x80020000 | errno) from KernelErrors.hpp; the EQUEUE_ERROR_*
// names below are kept as aliases so existing callers (VideoOut) build
// unchanged while the values can no longer drift from the shared table.
#ifndef CORE_LIBS_PRX_LIBKERNEL_EQUEUE_EQUEUE_HPP
#define CORE_LIBS_PRX_LIBKERNEL_EQUEUE_EQUEUE_HPP

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "SceTypes.hpp"
#include "prx/libc/include/general/VabiMacros.hpp"
#include "prx/libkernel/KernelErrors.hpp"

static constexpr int16_t EVFILT_USER = -11;
static constexpr int16_t EVFILT_VIDEO_OUT = -13;
static constexpr int16_t EVFILT_HRTIMER = -15;

static constexpr uint16_t EV_ADD = 0x0001;
static constexpr uint16_t EV_ONESHOT = 0x0010;
static constexpr uint16_t EV_CLEAR = 0x0020;
static constexpr uint16_t EV_ERROR = 0x4000;

static constexpr int EQUEUE_OK = 0;
// Aliases of the shared SCE codes. They used to be an unrelated 0x8001xxxx
// range (ETIMEDOUT was 0x80010023), so guests comparing against the real
// SCE_KERNEL_ERROR_ETIMEDOUT (0x8002003C) saw a different value (AnyPS5
// 97cae145 / 20810712).
static constexpr int EQUEUE_ERROR_EBADF = SCE_KERNEL_ERROR_EBADF;
static constexpr int EQUEUE_ERROR_EFAULT = SCE_KERNEL_ERROR_EFAULT;
static constexpr int EQUEUE_ERROR_EINVAL = SCE_KERNEL_ERROR_EINVAL;
static constexpr int EQUEUE_ERROR_ENOENT = SCE_KERNEL_ERROR_ENOENT;
static constexpr int EQUEUE_ERROR_ETIMEDOUT = SCE_KERNEL_ERROR_ETIMEDOUT;

struct KernelEqueueEvent;

using EqueueTriggerFunc = void (*)(KernelEqueueEvent* event, void* triggerData);
using EqueueResetFunc = void (*)(KernelEqueueEvent* event);
using EqueueDeleteFunc = void (*)(KernelEqueue eq, KernelEqueueEvent* event);

struct KernelFilter {
    void* data = nullptr;
    std::shared_ptr<void> owner;
    EqueueTriggerFunc triggerFunc = nullptr;
    EqueueResetFunc resetFunc = nullptr;
    EqueueDeleteFunc deleteEventFunc = nullptr;
};

struct KernelEqueueEvent {
    bool triggered = false;
    uint64_t deadlineNs = 0;
    KernelEvent event;
    KernelFilter filter;
    std::deque<KernelEvent> pendingEvents;
};

class KernelEqueuePrivate {
public:
    explicit KernelEqueuePrivate(KernelEqueue handle);
    ~KernelEqueuePrivate();

    KernelEqueuePrivate(const KernelEqueuePrivate&) = delete;
    KernelEqueuePrivate& operator=(const KernelEqueuePrivate&) = delete;

    const std::string& GetName() const { return m_name; }
    void SetName(std::string name) { m_name = std::move(name); }

    int AddEvent(const KernelEqueueEvent& event);
    int TriggerEvent(uintptr_t ident, int16_t filter, void* triggerData);
    int DeleteEvent(uintptr_t ident, int16_t filter);
    int GetTriggeredEvents(KernelEvent* ev, int num);
    int WaitForEvents(KernelEvent* ev, int num, uint32_t micros);
    void Close();

private:
    static uint64_t MonotonicNs();
    void TriggerExpiredTimers(uint64_t nowNs);
    bool NextTimerWaitMicros(uint64_t nowNs, uint32_t* out) const;

    std::list<KernelEqueueEvent> m_events;
    std::mutex m_mutex;
    std::condition_variable_any m_cond;
    std::string m_name;
    KernelEqueue m_handle;
    bool m_closed = false;
};

using KernelEqueueRef = std::shared_ptr<KernelEqueuePrivate>;
extern "C" {

/** Create an event queue. @return EQUEUE_OK, or SCE_KERNEL_ERROR_EINVAL for a null out/name pointer. */
int APS5_VABI sceKernelCreateEqueue(KernelEqueue* eq, const char* name);
/** Delete a queue. @return EQUEUE_OK, or SCE_KERNEL_ERROR_EBADF for an unknown/already deleted handle. */
int APS5_VABI sceKernelDeleteEqueue(KernelEqueue eq);
/**
 * Wait for events. @param timo null = block forever, else microseconds.
 * @return EQUEUE_OK with *out events, SCE_KERNEL_ERROR_ETIMEDOUT (*out == 0),
 *         SCE_KERNEL_ERROR_EBADF (bad/closed queue), SCE_KERNEL_ERROR_EFAULT
 *         (null event buffer) or SCE_KERNEL_ERROR_EINVAL (num < 1, null out).
 */
int APS5_VABI sceKernelWaitEqueue(KernelEqueue eq, KernelEvent* ev, int num, int* out, const KernelUseconds* timo);
/** Register a level-triggered user event. @return EQUEUE_OK or an SCE error (EBADF for a bad queue). */
int APS5_VABI sceKernelAddUserEvent(KernelEqueue eq, int id);
/** Register an edge-triggered user event. @return EQUEUE_OK or an SCE error (EBADF for a bad queue). */
int APS5_VABI sceKernelAddUserEventEdge(KernelEqueue eq, int id);
/** Trigger a registered user event. @return EQUEUE_OK, SCE_KERNEL_ERROR_ENOENT (unknown id) or EBADF. */
int APS5_VABI sceKernelTriggerUserEvent(KernelEqueue eq, int id, void* udata);
/** Remove a user event. @return EQUEUE_OK, SCE_KERNEL_ERROR_ENOENT (unknown id) or EBADF. */
int APS5_VABI sceKernelDeleteUserEvent(KernelEqueue eq, int id);

/** Pin a queue handle for the duration of a call; empty when the handle is unknown (not an export). */
KernelEqueueRef EqueuePin_nid_postfix(KernelEqueue eq);
/** Add a prepared event to a queue (host-internal, used by VideoOut). @return EQUEUE_OK or SCE_KERNEL_ERROR_EBADF. */
int APS5_VABI EqueueAddEvent_nid_postfix(KernelEqueue eq, const KernelEqueueEvent& event);
/** Trigger an event by (ident, filter). @return EQUEUE_OK, SCE_KERNEL_ERROR_ENOENT or EBADF. */
int APS5_VABI EqueueTriggerEvent_nid_postfix(KernelEqueue eq, uintptr_t ident, int16_t filter, void* triggerData);
/** Delete an event by (ident, filter). @return EQUEUE_OK, SCE_KERNEL_ERROR_ENOENT or EBADF. */
int APS5_VABI EqueueDeleteEvent_nid_postfix(KernelEqueue eq, uintptr_t ident, int16_t filter);

}

#endif
