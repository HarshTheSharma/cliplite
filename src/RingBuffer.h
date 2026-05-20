#pragma once
#include <array>
#include <atomic>
#include <vector>
#include <mutex>

// Single-producer single-consumer ring buffer, overwrites oldest slot when full.
// push() uses try_to_lock so the capture thread never blocks on the encoder.
// snapshot() holds the lock only for the copy so push() rarely has to yield.
// N must be a power of two.
template<typename T, size_t N>
class RingBuffer {
    static_assert(N > 1 && (N & (N - 1)) == 0, "N must be a power of two > 1");

    std::array<T, N> buf_{};
    alignas(64) std::atomic<size_t> head_{0}; // next write position
    alignas(64) std::atomic<size_t> tail_{0}; // oldest live position

    mutable std::mutex snap_mu_;

public:
    // returns false if a snapshot is running; caller should drop the frame
    bool push(T item) {
        std::unique_lock lk(snap_mu_, std::try_to_lock);
        if (!lk) return false;

        const size_t h = head_.load(std::memory_order_relaxed);
        buf_[h & (N - 1)] = std::move(item);
        head_.store(h + 1, std::memory_order_release);

        // keep the window at N by bumping tail when the buffer is full
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (h + 1 - t > N)
            tail_.store(t + 1, std::memory_order_release);

        return true;
    }

    void snapshot(std::vector<T>& out, size_t max_frames) const {
        std::lock_guard lk(snap_mu_);
        const size_t h = head_.load(std::memory_order_acquire);
        const size_t t = tail_.load(std::memory_order_acquire);
        const size_t avail = h - t;
        const size_t start = (avail > max_frames) ? h - max_frames : t;

        out.clear();
        out.reserve(h - start);
        for (size_t i = start; i < h; ++i)
            out.push_back(buf_[i & (N - 1)]);
    }

    // Invokes fn with a const reference to the oldest item while holding the
    // snapshot lock. Returns false if the ring is empty or the lock is held
    // (caller should retry). Used by the capture thread to read the oldest
    // frame's timestamp before deciding whether to evict it.
    template<typename Fn>
    bool with_oldest(Fn&& fn) const {
        std::unique_lock lk(snap_mu_, std::try_to_lock);
        if (!lk) return false;
        const size_t t = tail_.load(std::memory_order_relaxed);
        const size_t h = head_.load(std::memory_order_relaxed);
        if (t == h) return false;
        fn(buf_[t & (N - 1)]);
        return true;
    }

    // frees the pool_ref in the oldest slot so the texture returns to the pool
    // try_to_lock so this never blocks the capture thread; retry next frame on false
    bool evict_oldest() {
        std::unique_lock lk(snap_mu_, std::try_to_lock);
        if (!lk) return false;
        const size_t t = tail_.load(std::memory_order_relaxed);
        if (t == head_.load(std::memory_order_relaxed)) return false; // empty
        buf_[t & (N - 1)] = T{};
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    size_t size() const noexcept {
        return head_.load(std::memory_order_acquire)
             - tail_.load(std::memory_order_acquire);
    }
};
