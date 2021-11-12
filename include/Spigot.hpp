#pragma once

#include <thread>
#include <condition_variable>
#include <functional>

template<typename Element, unsigned int size>
class Spigot {

private:

    std::function<void(Element*)> Produce;
    enum ThreadState { Paused, Running, Halted };
    std::atomic<ThreadState> state;
    std::mutex producerFunctionLock;
    std::condition_variable stateChangedCondition;
    std::thread producerThread;
    Element buffer[size];
    std::atomic<unsigned int> readIndex;
    std::atomic<unsigned int> writeIndex;

public:

    inline Spigot() :
        producerThread(&Spigot::ProducerThread, this),
        readIndex(0),
        writeIndex(0) {
        Pause();
    }

    inline ~Spigot() {
        Run();
        state = Halted;
        producerThread.join();
    }

    inline void SetProducer(std::function<void(Element*)> producerFunction) {
        Produce = producerFunction;
    }

    inline void Run() {
        if (state != Running) {
            producerFunctionLock.unlock();
            state = Running;
            stateChangedCondition.notify_one();
        }
    }

    inline void Pause() {
        if (state != Paused) {
            state = Paused;
            producerFunctionLock.lock();
            writeIndex = (readIndex + 1) % size;
        }
    }

    inline Element* Current() {
        while (readIndex == writeIndex);
        return &buffer[readIndex];
    }

    inline Element* Next() {
        readIndex = (readIndex + 1) % size;
        return Current();
    }

private:

    inline void ProducerThread() {

        std::unique_lock<std::mutex> lk(producerFunctionLock);

        while (true) {

            while (state != Running) {
                stateChangedCondition.wait(lk);
                if (state == Halted) {
                    return;
                }
            }

            if (((writeIndex + 1) % size) != readIndex) {
                Produce(&buffer[writeIndex]);
                writeIndex = (writeIndex + 1) % size;
                continue;
            }

        }
    }
};
