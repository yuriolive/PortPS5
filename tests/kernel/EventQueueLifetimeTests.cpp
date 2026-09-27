#include "common/TestHarness.hpp"
#include "prx/libkernel/Equeue/Equeue.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace {

using namespace PortPS5::Testing;

void CheckConcurrentResult(int result) {
    EXPECT_TRUE(result == SCE_OK || result == SCE_KERNEL_ERROR_EBADF || result == SCE_KERNEL_ERROR_ENOENT)
        << "Unexpected concurrent result: " << result;
}

void CountDeletedEvent(KernelEqueue, KernelEqueueEvent* event) {
    auto* count = static_cast<std::atomic<uint32_t>*>(event->filter.data);
    count->fetch_add(1, std::memory_order_relaxed);
}

struct DuplicateEventOwner {
    std::atomic<uint32_t> delete_count{0};
};

void QueueDuplicateEvent(KernelEqueueEvent* event, void* trigger_data) {
    auto next = event->event;
    next.data = reinterpret_cast<intptr_t>(trigger_data);
    if (event->triggered) {
        event->pendingEvents.push_back(next);
    } else {
        event->event = next;
        event->triggered = true;
    }
}

void ResetDuplicateEvent(KernelEqueueEvent* event) {
    event->triggered = false;
    event->event.data = 0;
}

void DeleteDuplicateEvent(KernelEqueue, KernelEqueueEvent* event) {
    auto* owner = static_cast<DuplicateEventOwner*>(event->filter.data);
    owner->delete_count.fetch_add(1, std::memory_order_relaxed);
}

void PoisonDuplicateEvent(KernelEqueueEvent*, void*) {
    FAIL() << "duplicate add replaced trigger callback";
}

TEST(EventQueueLifetime, DuplicateAddPreservesEventState) {
    KernelEqueue queue = 0;
    ASSERT_SCE_OK(sceKernelCreateEqueue(&queue, "duplicate-add"));

    auto original_owner = std::make_shared<DuplicateEventOwner>();
    std::weak_ptr<DuplicateEventOwner> weak_original = original_owner;
    KernelEqueueEvent original{};
    original.event.ident = 17;
    original.event.filter = EVFILT_VIDEO_OUT;
    original.event.udata = reinterpret_cast<void*>(0x1111);
    original.filter.data = original_owner.get();
    original.filter.owner = original_owner;
    original.filter.triggerFunc = QueueDuplicateEvent;
    original.filter.resetFunc = ResetDuplicateEvent;
    original.filter.deleteEventFunc = DeleteDuplicateEvent;

    EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, original));
    EXPECT_SCE_OK(EqueueTriggerEvent_nid_postfix(queue, 17, EVFILT_VIDEO_OUT, reinterpret_cast<void*>(0x1234)));
    EXPECT_SCE_OK(EqueueTriggerEvent_nid_postfix(queue, 17, EVFILT_VIDEO_OUT, reinterpret_cast<void*>(0x5678)));

    auto replacement_owner = std::make_shared<DuplicateEventOwner>();
    std::weak_ptr<DuplicateEventOwner> weak_replacement = replacement_owner;
    KernelEqueueEvent duplicate{};
    duplicate.triggered = false;
    duplicate.deadlineNs = 1;
    duplicate.event.ident = 17;
    duplicate.event.filter = EVFILT_VIDEO_OUT;
    duplicate.event.data = 0x7fffffff;
    duplicate.event.udata = reinterpret_cast<void*>(0x2222);
    duplicate.filter.data = replacement_owner.get();
    duplicate.filter.owner = replacement_owner;
    duplicate.filter.triggerFunc = PoisonDuplicateEvent;

    EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, duplicate));

    duplicate.filter.owner.reset();
    replacement_owner.reset();
    EXPECT_TRUE(weak_replacement.expired()) << "duplicate owner is not retained";
    original.filter.owner.reset();
    original_owner.reset();
    EXPECT_FALSE(weak_original.expired()) << "original event owner remains retained";

    KernelEvent events[2]{};
    int out = 0;
    KernelUseconds timeout = 0;
    EXPECT_SCE_OK(sceKernelWaitEqueue(queue, events, 2, &out, &timeout));
    EXPECT_EQ(out, 2);
    EXPECT_EQ(events[0].data, 0x1234);
    EXPECT_EQ(events[1].data, 0x5678);
    EXPECT_EQ(events[0].udata, reinterpret_cast<void*>(0x2222));
    EXPECT_EQ(events[1].udata, reinterpret_cast<void*>(0x2222));

    KernelEvent timer_event{};
    EXPECT_SCE_OK(sceKernelWaitEqueue(queue, &timer_event, 1, &out, &timeout));
    EXPECT_EQ(out, 1);
    EXPECT_EQ(timer_event.data, 0);
    EXPECT_EQ(timer_event.udata, reinterpret_cast<void*>(0x2222));

    auto retained_owner = weak_original.lock();
    ASSERT_NE(retained_owner, nullptr) << "original owner alive before delete";
    EXPECT_SCE_OK(EqueueDeleteEvent_nid_postfix(queue, 17, EVFILT_VIDEO_OUT));
    EXPECT_EQ(retained_owner->delete_count.load(std::memory_order_relaxed), 1);
    retained_owner.reset();
    EXPECT_TRUE(weak_original.expired()) << "original owner released on delete";
    EXPECT_SCE_OK(sceKernelDeleteEqueue(queue));
}

struct SimulatedVideoOutEventState;

struct SimulatedVideoOutRegistration {
    KernelEqueue handle = 0;
    std::shared_ptr<SimulatedVideoOutEventState> state;
    uint64_t marker = 0x123456789abcdef0ull;
};

struct SimulatedVideoOutEventState {
    SimulatedVideoOutEventState(std::atomic<uint32_t>& stage, std::atomic<uint32_t>& destroy_count)
        : stage(stage), destroy_count(destroy_count) {}

    ~SimulatedVideoOutEventState() { destroy_count.fetch_add(1, std::memory_order_relaxed); }

    std::mutex mutex;
    std::vector<std::shared_ptr<SimulatedVideoOutRegistration>> queues;
    std::atomic<uint32_t>& stage;
    std::atomic<uint32_t>& destroy_count;
    uint64_t marker = 0xfedcba9876543210ull;
};

void DetachSimulatedVideoOutEvent(KernelEqueue queue, KernelEqueueEvent* event) {
    auto* registration = static_cast<SimulatedVideoOutRegistration*>(event->filter.data);
    EXPECT_TRUE(registration != nullptr && registration->handle == queue)
        << "simulated registration identity";
    auto state = registration->state;
    EXPECT_NE(state, nullptr) << "simulated event owns shared state";
    state->stage.store(1, std::memory_order_release);
    while (state->stage.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }

    {
        std::lock_guard lock(state->mutex);
        const auto entry = std::find_if(
            state->queues.begin(), state->queues.end(),
            [registration](const auto& candidate) { return candidate.get() == registration; });
        if (entry != state->queues.end()) {
            state->queues.erase(entry);
        }
    }
    event->filter.owner.reset();
    EXPECT_EQ(state->marker, 0xfedcba9876543210ull);
    EXPECT_EQ(registration->marker, 0x123456789abcdef0ull);
}

TEST(EventQueueLifetime, CallbackStateOutlivesPort) {
    KernelEqueue queue = 0;
    ASSERT_SCE_OK(sceKernelCreateEqueue(&queue, "shared-port-state"));

    std::atomic<uint32_t> stage{0};
    std::atomic<uint32_t> destroy_count{0};
    auto port_state = std::make_shared<SimulatedVideoOutEventState>(stage, destroy_count);
    std::weak_ptr<SimulatedVideoOutEventState> weak_state = port_state;
    auto registration = std::make_shared<SimulatedVideoOutRegistration>();
    std::weak_ptr<SimulatedVideoOutRegistration> weak_registration = registration;
    registration->handle = queue;
    registration->state = port_state;
    port_state->queues.push_back(registration);

    KernelEqueueEvent event{};
    event.event.ident = 8;
    event.event.filter = EVFILT_VIDEO_OUT;
    event.filter.data = registration.get();
    event.filter.owner = registration;
    event.filter.deleteEventFunc = DetachSimulatedVideoOutEvent;
    EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, event));
    event.filter.owner.reset();

    std::jthread close([&] {
        EXPECT_SCE_OK(sceKernelDeleteEqueue(queue));
    });
    while (stage.load(std::memory_order_acquire) != 1) {
        std::this_thread::yield();
    }

    std::vector<std::shared_ptr<SimulatedVideoOutRegistration>> detached;
    {
        std::lock_guard lock(port_state->mutex);
        detached = std::move(port_state->queues);
    }
    registration.reset();
    detached.clear();
    port_state.reset();
    EXPECT_FALSE(weak_state.expired()) << "callback state outlives simulated port object";
    EXPECT_FALSE(weak_registration.expired()) << "registration outlives simulated port object";

    stage.store(2, std::memory_order_release);
    close.join();
    EXPECT_TRUE(weak_registration.expired()) << "detached registration is released";
    EXPECT_TRUE(weak_state.expired()) << "detached event state is released";
    EXPECT_EQ(destroy_count.load(std::memory_order_relaxed), 1)
        << "shared event state is destroyed exactly once";
}

struct OwnedCallbackPayload {
    OwnedCallbackPayload(std::atomic<uint32_t>& stage, std::atomic<uint32_t>& delete_count,
                         std::atomic<uint32_t>& destroy_count)
        : stage(stage), delete_count(delete_count), destroy_count(destroy_count) {}

    std::atomic<uint32_t>& stage;
    std::atomic<uint32_t>& delete_count;
    std::atomic<uint32_t>& destroy_count;
    uint64_t marker = 0xc0dec0dec0dec0deull;

    ~OwnedCallbackPayload() { destroy_count.fetch_add(1, std::memory_order_relaxed); }
};

void DeleteOwnedEvent(KernelEqueue queue, KernelEqueueEvent* event) {
    auto* payload = static_cast<OwnedCallbackPayload*>(event->filter.data);
    EXPECT_NE(payload, nullptr) << "owned callback payload";
    EXPECT_FALSE(EqueuePin_nid_postfix(queue)) << "owned callback runs after registry removal";
    payload->delete_count.fetch_add(1, std::memory_order_relaxed);
    event->filter.owner.reset();
    payload->stage.store(1, std::memory_order_release);
    while (payload->stage.load(std::memory_order_acquire) != 2) {
        std::this_thread::yield();
    }
    EXPECT_EQ(payload->marker, 0xc0dec0dec0dec0deull) << "owned callback payload remains valid";
}

TEST(EventQueueLifetime, CallbackOwnsPayload) {
    KernelEqueue queue = 0;
    ASSERT_SCE_OK(sceKernelCreateEqueue(&queue, "owned-callback"));

    std::atomic<uint32_t> stage{0};
    std::atomic<uint32_t> delete_count{0};
    std::atomic<uint32_t> destroy_count{0};
    auto registration = std::make_shared<OwnedCallbackPayload>(stage, delete_count, destroy_count);
    std::weak_ptr<OwnedCallbackPayload> weak_registration = registration;
    std::vector<std::shared_ptr<OwnedCallbackPayload>> port_registrations{registration};
    {
        KernelEqueueEvent event{};
        event.event.ident = 2;
        event.event.filter = EVFILT_VIDEO_OUT;
        event.filter.data = registration.get();
        event.filter.owner = registration;
        event.filter.deleteEventFunc = DeleteOwnedEvent;
        EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, event));
    }

    std::jthread close([&] {
        EXPECT_SCE_OK(sceKernelDeleteEqueue(queue));
    });
    while (stage.load(std::memory_order_acquire) != 1) {
        std::this_thread::yield();
    }

    EXPECT_FALSE(EqueuePin_nid_postfix(queue))
        << "owned callback queue removed while callback blocked";
    port_registrations.clear();
    registration.reset();
    EXPECT_FALSE(weak_registration.expired()) << "delete callback retains detached payload";

    stage.store(2, std::memory_order_release);
    close.join();
    EXPECT_EQ(delete_count.load(std::memory_order_relaxed), 1) << "owned callback runs exactly once";
    EXPECT_TRUE(weak_registration.expired()) << "owned callback payload released with event";
    EXPECT_EQ(destroy_count.load(std::memory_order_relaxed), 1)
        << "owned callback payload destroyed exactly once";
}

TEST(EventQueueLifetime, PinnedClose) {
    KernelEqueue queue = 0;
    ASSERT_SCE_OK(sceKernelCreateEqueue(&queue, "pinned-close"));

    std::atomic<uint32_t> delete_count{0};
    KernelEqueueEvent event{};
    event.event.ident = 1;
    event.event.filter = EVFILT_VIDEO_OUT;
    event.filter.data = &delete_count;
    event.filter.deleteEventFunc = CountDeletedEvent;
    EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, event));

    auto owner = EqueuePin_nid_postfix(queue);
    ASSERT_NE(owner, nullptr) << "pin live queue";
    EXPECT_SCE_OK(sceKernelDeleteEqueue(queue));
    EXPECT_EQ(delete_count.load(std::memory_order_relaxed), 1) << "close invokes callback once";
    EXPECT_FALSE(EqueuePin_nid_postfix(queue)) << "deleted queue leaves registry";
    EXPECT_EQ(EqueueTriggerEvent_nid_postfix(queue, 1, EVFILT_VIDEO_OUT, nullptr),
              SCE_KERNEL_ERROR_EBADF) << "stale trigger rejected";
    EXPECT_EQ(sceKernelDeleteEqueue(queue), SCE_KERNEL_ERROR_EBADF)
        << "second queue delete rejected";

    owner.reset();
    EXPECT_EQ(delete_count.load(std::memory_order_relaxed), 1)
        << "deferred destruction does not repeat callback";
}

TEST(EventQueueLifetime, StaleHandleNeverAliasesNewQueue) {
    KernelEqueue stale = 0;
    ASSERT_SCE_OK(sceKernelCreateEqueue(&stale, "stale-handle"));
    EXPECT_SCE_OK(sceKernelDeleteEqueue(stale));

    KernelEqueue replacement = 0;
    ASSERT_SCE_OK(sceKernelCreateEqueue(&replacement, "replacement"));
    EXPECT_NE(stale, replacement) << "queue handles are never recycled";
    EXPECT_FALSE(EqueuePin_nid_postfix(stale)) << "stale handle does not pin";
    EXPECT_EQ(sceKernelAddUserEvent(stale, 11), SCE_KERNEL_ERROR_EBADF)
        << "stale handle cannot mutate replacement";
    EXPECT_SCE_OK(sceKernelAddUserEvent(replacement, 11));
    EXPECT_EQ(sceKernelTriggerUserEvent(stale, 11, nullptr), SCE_KERNEL_ERROR_EBADF)
        << "stale handle cannot trigger replacement";
    EXPECT_SCE_OK(sceKernelTriggerUserEvent(replacement, 11, nullptr));
    EXPECT_SCE_OK(sceKernelDeleteEqueue(replacement));
}

TEST(EventQueueLifetime, ConcurrentCloseCallback) {
    for (uint32_t iteration = 0; iteration < 64; iteration++) {
        KernelEqueue queue = 0;
        ASSERT_SCE_OK(sceKernelCreateEqueue(&queue, "callback-race"));

        std::atomic<uint32_t> delete_count{0};
        KernelEqueueEvent callback_event{};
        callback_event.event.ident = 9;
        callback_event.event.filter = EVFILT_USER;
        callback_event.filter.data = &delete_count;
        callback_event.filter.deleteEventFunc = CountDeletedEvent;
        EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, callback_event));

        std::atomic<bool> start{false};
        std::jthread trigger([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (uint32_t i = 0; i < 256; i++) {
                CheckConcurrentResult(EqueueTriggerEvent_nid_postfix(
                    queue, 9, EVFILT_USER, nullptr));
            }
        });

        start.store(true, std::memory_order_release);
        EXPECT_SCE_OK(sceKernelDeleteEqueue(queue));
        trigger.join();
        EXPECT_EQ(delete_count.load(std::memory_order_relaxed), 1)
            << "concurrent close invokes callback exactly once (iteration " << iteration << ")";
    }
}

TEST(EventQueueLifetime, ConcurrentDelete) {
    for (uint32_t iteration = 0; iteration < 64; iteration++) {
        KernelEqueue queue = 0;
        ASSERT_SCE_OK(sceKernelCreateEqueue(&queue, "concurrent-delete"));
        KernelEqueueEvent event{};
        event.event.ident = 7;
        event.event.filter = EVFILT_USER;
        EXPECT_SCE_OK(EqueueAddEvent_nid_postfix(queue, event));

        std::atomic<bool> start{false};
        std::jthread mutate([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (uint32_t i = 0; i < 64; i++) {
                CheckConcurrentResult(EqueueAddEvent_nid_postfix(queue, event));
                CheckConcurrentResult(EqueueTriggerEvent_nid_postfix(queue, 7, EVFILT_USER, nullptr));
                CheckConcurrentResult(EqueueDeleteEvent_nid_postfix(queue, 7, EVFILT_USER));
            }
        });
        std::jthread trigger([&] {
            while (!start.load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (uint32_t i = 0; i < 128; i++) {
                CheckConcurrentResult(EqueueTriggerEvent_nid_postfix(queue, 7, EVFILT_USER, nullptr));
            }
        });

        start.store(true, std::memory_order_release);
        EXPECT_SCE_OK(sceKernelDeleteEqueue(queue));
        mutate.join();
        trigger.join();
        EXPECT_FALSE(EqueuePin_nid_postfix(queue)) << "concurrent queue removed from registry";
    }
}

} // namespace
