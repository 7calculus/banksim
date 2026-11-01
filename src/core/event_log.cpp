#include "event_log.h"

#include <algorithm>

namespace banksim {

std::uint64_t EventLog::add(Event e) {
    std::lock_guard<std::mutex> lk(mtx_);
    e.seq = nextSeq_++;
    if (e.timestampMs == 0) e.timestampMs = nowMs();
    events_.push_back(e);
    if (events_.size() > kMaxEvents) events_.pop_front();
    for (auto& s : sinks_) s.second(e);
    return e.seq;
}

int EventLog::addSink(Sink s) {
    std::lock_guard<std::mutex> lk(mtx_);
    const int id = nextSinkId_++;
    sinks_.emplace_back(id, std::move(s));
    return id;
}

void EventLog::removeSink(int id) {
    std::lock_guard<std::mutex> lk(mtx_);
    sinks_.erase(std::remove_if(sinks_.begin(), sinks_.end(),
                                [id](const auto& p) { return p.first == id; }),
                 sinks_.end());
}

std::vector<Event> EventLog::snapshot() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return std::vector<Event>(events_.begin(), events_.end());
}

std::size_t EventLog::size() const {
    std::lock_guard<std::mutex> lk(mtx_);
    return events_.size();
}

void EventLog::clear() {
    std::lock_guard<std::mutex> lk(mtx_);
    events_.clear();
}

}  // namespace banksim
