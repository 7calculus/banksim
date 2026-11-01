// event_log.h - thread-safe chronological event log used by the Live Thread Monitor.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include "types.h"

namespace banksim {

// One line of the thread monitor.
struct Event {
    std::uint64_t seq = 0;          // assigned by EventLog::add (strictly increasing)
    std::int64_t timestampMs = 0;   // epoch milliseconds
    std::string source = "main";    // "main", "lab:race", "lab:sync", "lab:deadlock", ...
    std::string threadLabel;        // "Thread-01"
    std::string threadId;           // OS thread id (short hex)
    std::string customerId;
    std::string customerName;
    std::string channel;            // human-readable channel
    std::string txType;             // human-readable transaction type
    std::string accountId;
    std::string txId;
    std::string message;
    ThreadState threadState = ThreadState::Created;
    LockState lockState = LockState::None;
    TxStatus status = TxStatus::Pending;
};

class EventLog {
public:
    using Sink = std::function<void(const Event&)>;

    // Stores the event, assigns seq/timestamp and notifies all sinks. Sinks run while
    // the log's mutex is held (this keeps delivery order identical to storage order),
    // so they must be fast and must never call back into the same EventLog.
    std::uint64_t add(Event e);

    int addSink(Sink s);
    void removeSink(int id);

    // Detailed lock-level events (waiting / acquired / released / critical section).
    // Turned off for large stress tests to keep the log small.
    void setVerbose(bool v) { verbose_.store(v); }
    bool verbose() const { return verbose_.load(); }

    std::vector<Event> snapshot() const;
    std::size_t size() const;
    void clear();

    static constexpr std::size_t kMaxEvents = 200000;

private:
    mutable std::mutex mtx_;
    std::deque<Event> events_;
    std::vector<std::pair<int, Sink>> sinks_;
    std::uint64_t nextSeq_ = 1;
    int nextSinkId_ = 1;
    std::atomic<bool> verbose_{true};
};

}  // namespace banksim
