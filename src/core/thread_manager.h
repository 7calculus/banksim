// thread_manager.h - worker-thread lifetime management plus the small synchronisation
// helpers (barrier / ordered start) used to make the concurrency demos reproducible.
#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace banksim {

// Reusable ("cyclic") barrier built from a mutex + condition_variable. Behaves like
// std::barrier but works with every C++17 toolchain (MSVC, MinGW, GCC) without relying
// on <barrier> being available.
class CyclicBarrier {
public:
    explicit CyclicBarrier(int participants) : count_(participants) {}
    void arriveAndWait();

private:
    std::mutex m_;
    std::condition_variable cv_;
    const int count_;
    int waiting_ = 0;
    unsigned generation_ = 0;
};

// Lets thread i proceed only after thread i-1 has "signalled" (i.e. it already holds, or is
// queued for, its first lock). This produces a deterministic arrival order at a mutex while
// the threads still genuinely overlap in time. A time-out guarantees it can never hang.
class OrderedStart {
public:
    explicit OrderedStart(int n) : signalled_(static_cast<std::size_t>(n), false) {}
    void waitTurn(int index, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000));
    void signal(int index);  // idempotent

private:
    std::mutex m_;
    std::condition_variable cv_;
    std::vector<bool> signalled_;
};

// Owns std::thread objects. Every simulated customer transaction runs on one of these
// worker threads; the GUI thread never executes banking work.
class ThreadManager {
public:
    using Task = std::function<void(const std::string& label)>;
    using ErrorHandler = std::function<void(const std::string& label, const std::string& what)>;

    explicit ThreadManager(std::string prefix = "Thread");
    ~ThreadManager();  // joins every outstanding thread

    ThreadManager(const ThreadManager&) = delete;
    ThreadManager& operator=(const ThreadManager&) = delete;

    // Starts a worker; returns its label ("Thread-01"). Exceptions escaping the task are
    // caught, reported through the error handler, and never terminate the process.
    std::string spawn(Task task);
    void joinAll();
    int activeCount() const { return active_.load(); }
    int totalSpawned() const { return spawned_.load(); }
    void resetNumbering();  // call only when no thread is running
    void setErrorHandler(ErrorHandler h);

private:
    struct Worker {
        std::thread th;
        std::shared_ptr<std::atomic<bool>> done;
    };
    void reapFinishedLocked();

    std::string prefix_;
    mutable std::mutex mtx_;
    std::vector<Worker> workers_;
    std::atomic<int> active_{0};
    std::atomic<int> spawned_{0};
    int counter_ = 0;
    ErrorHandler onError_;
};

}  // namespace banksim
