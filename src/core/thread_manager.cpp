#include "thread_manager.h"

#include <cstdio>

namespace banksim {

// ------------------------------------------------------------------ CyclicBarrier

void CyclicBarrier::arriveAndWait() {
    std::unique_lock<std::mutex> lk(m_);
    const unsigned gen = generation_;
    if (++waiting_ == count_) {
        waiting_ = 0;
        ++generation_;
        cv_.notify_all();
        return;
    }
    cv_.wait(lk, [&] { return gen != generation_; });
}

// ------------------------------------------------------------------ OrderedStart

void OrderedStart::waitTurn(int index, std::chrono::milliseconds timeout) {
    if (index <= 0) return;
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, timeout, [&] { return signalled_[static_cast<std::size_t>(index - 1)]; });
}

void OrderedStart::signal(int index) {
    std::lock_guard<std::mutex> lk(m_);
    if (index < 0 || static_cast<std::size_t>(index) >= signalled_.size()) return;
    signalled_[static_cast<std::size_t>(index)] = true;
    cv_.notify_all();
}

// ------------------------------------------------------------------ ThreadManager

ThreadManager::ThreadManager(std::string prefix) : prefix_(std::move(prefix)) {}

ThreadManager::~ThreadManager() { joinAll(); }

void ThreadManager::setErrorHandler(ErrorHandler h) {
    std::lock_guard<std::mutex> lk(mtx_);
    onError_ = std::move(h);
}

void ThreadManager::reapFinishedLocked() {
    for (auto it = workers_.begin(); it != workers_.end();) {
        if (it->done->load() && it->th.joinable()) {
            it->th.join();  // already finished, returns immediately
            it = workers_.erase(it);
        } else {
            ++it;
        }
    }
}

std::string ThreadManager::spawn(Task task) {
    std::lock_guard<std::mutex> lk(mtx_);
    reapFinishedLocked();

    char buf[48];
    std::snprintf(buf, sizeof buf, "%s-%02d", prefix_.c_str(), ++counter_);
    const std::string label = buf;

    auto done = std::make_shared<std::atomic<bool>>(false);
    ++active_;
    ++spawned_;
    ErrorHandler handler = onError_;
    std::thread th([this, task = std::move(task), label, done, handler]() {
        try {
            task(label);
        } catch (const std::exception& e) {
            if (handler) handler(label, e.what());
        } catch (...) {
            if (handler) handler(label, "unknown exception");
        }
        --active_;
        done->store(true);
    });
    workers_.push_back({std::move(th), done});
    return label;
}

void ThreadManager::joinAll() {
    std::vector<Worker> local;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        local.swap(workers_);
    }
    for (auto& w : local)
        if (w.th.joinable()) w.th.join();
}

void ThreadManager::resetNumbering() {
    std::lock_guard<std::mutex> lk(mtx_);
    counter_ = 0;
}

}  // namespace banksim
