#pragma once

#include <atomic>
#include <thread>
#include <functional>
#include <memory>


template<typename Element, typename ProducerData, unsigned int size>
class Spigot {

private:
    
    enum ThreadState { Running, PauseRequested, Paused, Halted };

public:

    ProducerData producerData;

private:

    // TODO I might actually need pad bytes
    Element* const buffer;
    std::function<void(Element*, ProducerData*)> producerFunction;
    std::thread producerThread;
    alignas(64) std::atomic<ThreadState> state;
    alignas(64) std::atomic<unsigned int> readIndex;
    alignas(64) std::atomic<unsigned int> writeIndex;

public:

    explicit inline Spigot(std::function<void(Element*, ProducerData*)> target) :
        buffer(static_cast<Element*>(std::malloc(sizeof(Element) * size))),
        producerFunction(target),
        producerThread(&Spigot::ProducerThread, this),
        state(Paused),
        readIndex(0),
        writeIndex(0) {
        // TODO raise bad_alloc maybe?
    }

    // Spigot is non-copyable
    Spigot(const Spigot&) = delete;
    Spigot& operator=(const Spigot&) = delete;

    inline ~Spigot() {
        state.store(Halted, std::memory_order_release);
        producerThread.join();

        // TODO Destroy stored objects. Spigot may not be around for the whole app
        //      only dtor if not constexpr trivially-destructible

        std::free(buffer);
    }

    /*
     * Resume execution of the service thread. This makes it safe to call either
     * Current or Advance, but unsafe to access producerData.
     */
    inline void Run() {
        state.store(Running, std::memory_order_release);
    }

    /*
     * Pause execution of the service thread. This makes it safe to access
     * producerData, but unsafe to call either Current or Advance.
     */
    inline void Pause() {

        // Store a pause request "eventually", and then aggressively spin until
        // the service thread has written the pause state.
        state.store(PauseRequested, std::memory_order_release);
        while (state.load(std::memory_order_acquire) != Paused);

        // TODO destroy objects in the queue if constexpr not trivially dtorable

        readIndex.store(0, std::memory_order_release);
        writeIndex.store(0, std::memory_order_release);

    }

    /*
     * Spin until the queue is not empty, and then return a pointer to the top
     * Element in the queue. This will spin every time, so call it once and
     * store the result in a local variable.
     */
    inline Element* Current() {

        // Spin until the buffer is not empty
        const unsigned int loadedReadIndex = readIndex.load(std::memory_order_relaxed);
        while (loadedReadIndex == writeIndex.load(std::memory_order_acquire));

        // Return the now-safe pointer to the top of the queue
        return &buffer[loadedReadIndex];
    }

    /*
     * Destroy the top Element of the queue, guaranteeing that the next call to
     * Current() will return the next Element of the queue.
     */
    inline void Advance() {

        // Spin until the buffer is not empty
        const unsigned int loadedReadIndex = readIndex.load(std::memory_order_relaxed);
        while (loadedReadIndex == writeIndex.load(std::memory_order_acquire));

        // Destroy the top-of-queue object
        if constexpr (!std::is_trivially_destructible<Element>::value) {
            buffer[loadedReadIndex].~T();
        }

        // Advance the read index
        unsigned int nextReadIndex = (loadedReadIndex + 1) % size;
        readIndex.store(nextReadIndex, std::memory_order_release);
    }

private:

    inline void ProducerThread() {
        while (true) {
            const unsigned int loadedWriteIndex = writeIndex.load(std::memory_order_relaxed);
            unsigned int nextWriteIndex = (loadedWriteIndex + 1) % size;

            ThreadState sampledState = state.load(std::memory_order_acquire);
            unsigned int loadedReadIndex = readIndex.load(std::memory_order_acquire);
            while (loadedReadIndex == nextWriteIndex || sampledState != Running) {
                if (sampledState == Halted) {
                    return;
                }
                sampledState = state.load(std::memory_order_acquire);
                loadedReadIndex = readIndex.load(std::memory_order_acquire);
            }

            // Produce a new element into the queue
            producerFunction(&buffer[loadedWriteIndex], &producerData);

            // Advance the write index
            writeIndex.store(nextWriteIndex, std::memory_order_release);
        }
    }
};
