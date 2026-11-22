#pragma once

#include <atomic>
#include <array>
#include <cstdint>
#include <memory>

namespace mm
{

/** Hands immutable state objects from the message thread to the audio thread
    without locks or allocation on the audio side.

    - publish() is called on the message thread with a freshly built object.
    - acquire() is called by the audio thread at the start of each block. If a
      newer object is waiting, it becomes current and the old one is moved to a
      retire queue.
    - collectGarbage() is called on the message thread (e.g. from a timer) and
      deletes retired objects.

    getAcknowledgedGeneration() tells the message thread which published
    generation the audio thread is using, so resources referenced only by older
    generations (such as removed hosted plug-ins) can be destroyed safely.
*/
template <typename T>
class RtSwap
{
public:
    RtSwap() = default;

    ~RtSwap()
    {
        collectGarbage();
        delete pending.exchange (nullptr);
        delete current;
    }

    RtSwap (const RtSwap&) = delete;
    RtSwap& operator= (const RtSwap&) = delete;

    //==============================================================================
    /** Message thread. Returns the generation number given to the object. */
    uint64_t publish (std::unique_ptr<T> next)
    {
        const auto gen = ++publishedGeneration;
        next->generation = gen;

        // If the audio thread never picked up the previous pending object we
        // can delete it here: the exchange guarantees the audio thread can't
        // also take it.
        delete pending.exchange (next.release());
        return gen;
    }

    /** Message thread. Deletes objects the audio thread has finished with. */
    void collectGarbage()
    {
        for (;;)
        {
            const auto r = readIndex.load (std::memory_order_relaxed);
            if (r == writeIndex.load (std::memory_order_acquire))
                break;

            delete retired[r % retired.size()];
            readIndex.store (r + 1, std::memory_order_release);
        }
    }

    uint64_t getAcknowledgedGeneration() const noexcept   { return acknowledged.load (std::memory_order_acquire); }
    uint64_t getPublishedGeneration() const noexcept      { return publishedGeneration; }

    //==============================================================================
    /** Audio thread (or the message thread while audio is known to be stopped). */
    T* acquire() noexcept
    {
        const auto w = writeIndex.load (std::memory_order_relaxed);
        const bool retireQueueFull = (w - readIndex.load (std::memory_order_acquire)) >= retired.size();

        // If the retire queue is full, keep using the current object for now
        // rather than blocking or leaking.
        if (! retireQueueFull)
        {
            if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
            {
                if (current != nullptr)
                {
                    retired[w % retired.size()] = current;
                    writeIndex.store (w + 1, std::memory_order_release);
                }

                current = next;
                acknowledged.store (current->generation, std::memory_order_release);
            }
        }

        return current;
    }

    /** Audio thread: the object returned by the last acquire(). */
    T* get() const noexcept  { return current; }

private:
    std::atomic<T*> pending { nullptr };
    T* current = nullptr;

    std::array<T*, 32> retired {};
    std::atomic<size_t> writeIndex { 0 }, readIndex { 0 };

    uint64_t publishedGeneration = 0;
    std::atomic<uint64_t> acknowledged { 0 };
};

} // namespace mm
