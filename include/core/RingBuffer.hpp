#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

namespace biomus {

// ============================================================================
// Single-Producer Single-Consumer (or Multi-Producer guarded on non-RT side)
// Lock-Free Ring Buffer for Real-Time Audio Thread Consumption
// ============================================================================
template <typename T, size_t CapacityPow2>
class SpscRingBuffer {
    static_assert((CapacityPow2 & (CapacityPow2 - 1)) == 0, "Capacity must be a power of 2");
    static_assert(std::is_trivially_copyable_v<T>, "Element must be trivially copyable");

public:
    bool push(const T& item) noexcept {
        const size_t w = writeIdx_.load(std::memory_order_relaxed);
        const size_t next = (w + 1) & kMask;
        if (next == readIdx_.load(std::memory_order_acquire)) {
            return false; // full
        }
        buffer_[w] = item;
        writeIdx_.store(next, std::memory_order_release);
        return true;
    }

    bool pop(T& out) noexcept {
        const size_t r = readIdx_.load(std::memory_order_relaxed);
        if (r == writeIdx_.load(std::memory_order_acquire)) {
            return false; // empty
        }
        out = buffer_[r];
        readIdx_.store((r + 1) & kMask, std::memory_order_release);
        return true;
    }

    size_t popBulk(T* dst, size_t maxCount) noexcept {
        size_t count = 0;
        while (count < maxCount && pop(dst[count])) {
            ++count;
        }
        return count;
    }

    bool empty() const noexcept {
        return readIdx_.load(std::memory_order_acquire) ==
               writeIdx_.load(std::memory_order_acquire);
    }

    void clear() noexcept {
        readIdx_.store(writeIdx_.load(std::memory_order_relaxed), std::memory_order_release);
    }

private:
    static constexpr size_t kMask = CapacityPow2 - 1;
    alignas(64) std::atomic<size_t> writeIdx_{0};
    alignas(64) std::atomic<size_t> readIdx_{0};
    std::array<T, CapacityPow2> buffer_{};
};

// ============================================================================
// Wait-Free Seqlock Snapshot Publisher (1 Writer [Audio Thread] -> N Readers)
// Audio thread never blocks; readers retry if torn read occurs.
// ============================================================================
template <typename T>
class SeqLockSnapshot {
    static_assert(std::is_trivially_copyable_v<T>, "Snapshot must be trivially copyable");

public:
    explicit SeqLockSnapshot(const T& initial = T{}) noexcept : data_(initial) {}

    // Called ONLY from the single writer (audio thread) — completely wait-free!
    void publish(const T& value) noexcept {
        const uint64_t s = seq_.load(std::memory_order_relaxed);
        seq_.store(s + 1, std::memory_order_release); // odd = write in progress
        std::atomic_signal_fence(std::memory_order_acq_rel);
        data_ = value;
        std::atomic_signal_fence(std::memory_order_acq_rel);
        seq_.store(s + 2, std::memory_order_release); // even = valid snapshot
    }

    // Called from any reader thread (OLED, WebSocket, Tests)
    T read() const noexcept {
        T copy{};
        uint64_t s1 = 0, s2 = 0;
        do {
            s1 = seq_.load(std::memory_order_acquire);
            if (s1 & 1ULL) {
                continue; // writer active
            }
            std::atomic_signal_fence(std::memory_order_acq_rel);
            copy = data_;
            std::atomic_signal_fence(std::memory_order_acq_rel);
            s2 = seq_.load(std::memory_order_acquire);
        } while (s1 != s2);
        return copy;
    }

private:
    alignas(64) std::atomic<uint64_t> seq_{0};
    T data_{};
};

} // namespace biomus
