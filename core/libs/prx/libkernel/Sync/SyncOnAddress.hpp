#ifndef CORE_LIBS_PRX_LIBKERNEL_SYNC_SYNC_ON_ADDRESS_HPP
#define CORE_LIBS_PRX_LIBKERNEL_SYNC_SYNC_ON_ADDRESS_HPP

#include <chrono>
#include <cstdint>

namespace PortPS5::Kernel::SyncOnAddress {

// Callback type invoked periodically during long waits to poll guest signal delivery.
using signal_poll_func_t = void (*)();

// PS5 / SCE kernel return codes for synchronization operations.
constexpr int32_t OK = 0;
// SCE error indicating invalid address alignment, null pointer, or negative wake count (0x80020016).
constexpr int32_t KERNEL_ERROR_EINVAL = static_cast<int32_t>(0x80020016);
// SCE error indicating wait timed out before address value changed or wake was triggered (0x8002003C).
constexpr int32_t KERNEL_ERROR_ETIMEDOUT = static_cast<int32_t>(0x8002003C);

/**
 * @brief Waits on a 32-bit memory location until its value differs from expected or it is woken.
 *
 * @param address Pointer to the 32-bit word to observe (must be 4-byte aligned and non-null).
 * @param expected Expected value. If *address != expected at call time, returns OK immediately.
 * @param timeout_micros Optional timeout in microseconds (nullptr indicates indefinite wait).
 * @param signal_poll Optional callback invoked every 10ms slice to service guest signals.
 * @return OK on value change or wake, KERNEL_ERROR_EINVAL on bad alignment/null, KERNEL_ERROR_ETIMEDOUT on expiry.
 */
int Wait32(volatile uint32_t* address, uint32_t expected, const uint32_t* timeout_micros,
           signal_poll_func_t signal_poll = nullptr);

/**
 * @brief Waits on a 64-bit memory location until its value differs from expected or it is woken.
 *
 * @param address Pointer to the 64-bit word to observe (must be 8-byte aligned and non-null).
 * @param expected Expected 64-bit value to compare atomically.
 * @param timeout_micros Optional timeout in microseconds (nullptr indicates indefinite wait).
 * @param signal_poll Optional callback invoked every 10ms slice to service guest signals.
 * @return OK on value change or wake, KERNEL_ERROR_EINVAL on bad alignment/null, KERNEL_ERROR_ETIMEDOUT on expiry.
 */
int Wait64(volatile uint64_t* address, uint64_t expected, const uint32_t* timeout_micros,
           signal_poll_func_t signal_poll = nullptr);

/**
 * @brief Waits on a 64-bit memory location with nanosecond timeout granularity.
 *
 * @param address Pointer to the 64-bit word to observe (must be 8-byte aligned and non-null).
 * @param expected Expected 64-bit value to compare atomically.
 * @param timeout Duration timeout in nanoseconds.
 * @param signal_poll Optional callback invoked periodically to service guest signals.
 * @return OK on value change or wake, KERNEL_ERROR_EINVAL on bad alignment/null, KERNEL_ERROR_ETIMEDOUT on expiry.
 */
int Wait64(volatile uint64_t* address, uint64_t expected, std::chrono::nanoseconds timeout,
           signal_poll_func_t signal_poll = nullptr);

/**
 * @brief Wakes one, multiple, or all threads waiting on the specified memory address.
 *
 * @param address Pointer to the address being waited on (must be at least 4-byte aligned and non-null).
 * @param count Number of waiters to wake (1 for single, INT_MAX for all, 0 is a valid no-op).
 * @return OK on success, KERNEL_ERROR_EINVAL if address is misaligned/null or count is negative.
 */
int Wake(volatile void* address, int32_t count);

} // namespace PortPS5::Kernel::SyncOnAddress

#endif // CORE_LIBS_PRX_LIBKERNEL_SYNC_SYNC_ON_ADDRESS_HPP
