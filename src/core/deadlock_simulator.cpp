#include "deadlock_simulator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#include "thread_manager.h"

namespace banksim {

namespace {

struct LabAcct {
    std::string id;
    std::timed_mutex m;  // timed so that a stuck thread can give up instead of hanging forever
    Money balance = 0;   // guarded by m
};

// Who holds / waits for what - used to detect and describe the circular wait.
class WaitGraph {
public:
    explicit WaitGraph(int n) : holds_(static_cast<std::size_t>(n)), waits_(static_cast<std::size_t>(n)),
                                labels_(static_cast<std::size_t>(n)) {}
    void setLabel(int t, const std::string& l) { std::lock_guard<std::mutex> lk(m_); labels_[idx(t)] = l; }
    void setHolds(int t, const std::string& acct) { std::lock_guard<std::mutex> lk(m_); holds_[idx(t)] = acct; }
    void setWaits(int t, const std::string& acct) { std::lock_guard<std::mutex> lk(m_); waits_[idx(t)] = acct; }

    std::string holderOf(const std::string& acct) {
        std::lock_guard<std::mutex> lk(m_);
        for (std::size_t t = 0; t < holds_.size(); ++t)
            if (holds_[t] == acct) return labels_[t];
        return "nobody";
    }

    // Returns a description of a wait cycle if one exists ("" otherwise) and fills `lines`.
    std::string findCycle(std::vector<std::string>& lines) {
        std::lock_guard<std::mutex> lk(m_);
        const std::size_t n = holds_.size();
        for (std::size_t start = 0; start < n; ++start) {
            std::size_t cur = start;
            std::string desc;
            for (std::size_t step = 0; step <= n; ++step) {
                if (waits_[cur].empty()) break;
                std::size_t holder = n;
                for (std::size_t t = 0; t < n; ++t)
                    if (holds_[t] == waits_[cur]) holder = t;
                if (holder == n) break;
                desc += labels_[cur] + " holds " + holds_[cur] + " and waits for " + waits_[cur] +
                        " (held by " + labels_[holder] + ")  ->  ";
                cur = holder;
                if (cur == start) {
                    desc += "back to " + labels_[start] + ": CIRCULAR WAIT";
                    lines.clear();
                    for (std::size_t t = 0; t < n; ++t) {
                        if (!holds_[t].empty())
                            lines.push_back(holds_[t] + ": LOCKED by " + labels_[t]);
                        if (!waits_[t].empty())
                            lines.push_back(labels_[t] + " is WAITING for " + waits_[t]);
                    }
                    return desc;
                }
            }
        }
        return "";
    }

private:
    static std::size_t idx(int t) { return static_cast<std::size_t>(t); }
    std::mutex m_;
    std::vector<std::string> holds_, waits_, labels_;
};

}  // namespace

LabReport runDeadlockDemo(EventLog* globalLog, DeadlockConfig cfg) {
    LabReport rep;
    rep.kind = cfg.ordered ? LabKind::DeadlockPrevented : LabKind::Deadlock;
    rep.title = cfg.ordered ? "Deadlock prevention - consistent lock ordering"
                            : "Deadlock - inconsistent lock ordering";
    const std::string src = cfg.ordered ? "lab:ordered" : "lab:deadlock";

    LabCollector collector(globalLog);
    EventLog& log = collector.log();

    LabAcct accts[2];
    accts[0].id = cfg.accountA;
    accts[0].balance = cfg.balanceA;
    accts[1].id = cfg.accountB;
    accts[1].balance = cfg.balanceB;

    struct Plan {
        LabParticipant who;
        int from, to;
        Money amount;
    };
    const Plan plans[2] = {
        {{"CUST-A", "Customer A", Channel::ATM1, TxType::Transfer, cfg.amountAtoB}, 0, 1, cfg.amountAtoB},
        {{"CUST-B", "Customer B", Channel::ATM2, TxType::Transfer, cfg.amountBtoA}, 1, 0, cfg.amountBtoA},
    };

    WaitGraph graph(2);
    CyclicBarrier startBarrier(2);
    CyclicBarrier holdBarrier(2);  // inconsistent mode only: both threads hold their first lock
    std::atomic<bool> cycleReported{false};
    std::mutex timeoutMtx;
    std::condition_variable timeoutCv;
    int timedOutCount = 0;

    std::mutex repMtx;
    LabOutcome outcomes[2];
    std::string labels[2];
    bool completed[2] = {false, false};
    bool timedOut[2] = {false, false};

    auto emit = [&](const LabParticipant& p, const std::string& label, const std::string& account, ThreadState ts,
                    LockState ls, TxStatus st, const std::string& msg) {
        Event e;
        e.source = src;
        e.threadLabel = label;
        e.threadId = currentThreadIdString();
        e.customerId = p.customerId;
        e.customerName = p.customerName;
        e.channel = toString(p.channel);
        e.txType = toString(p.type);
        e.accountId = account;
        e.message = msg;
        e.threadState = ts;
        e.lockState = ls;
        e.status = st;
        log.add(std::move(e));
    };

    const auto t0 = std::chrono::steady_clock::now();
    {
        ThreadManager tm("Thread");
        for (int i = 0; i < 2; ++i) {
            tm.spawn([&, i](const std::string& label) {
                const Plan& plan = plans[i];
                LabAcct* src_ = &accts[plan.from];
                LabAcct* dst_ = &accts[plan.to];
                LabAcct* first = src_;
                LabAcct* second = dst_;
                if (cfg.ordered && first->id > second->id) std::swap(first, second);  // THE FIX
                labels[i] = label;
                graph.setLabel(i, label);
                auto E = [&](const std::string& acct, ThreadState ts, LockState ls, TxStatus st, const std::string& m) {
                    emit(plan.who, label, acct, ts, ls, st, m);
                };

                startBarrier.arriveAndWait();
                E(src_->id, ThreadState::Executing, LockState::None, TxStatus::Pending,
                  "Fund Transfer initiated (" + formatMoney(plan.amount) + " " + src_->id + " -> " + dst_->id +
                      "), lock order: " + first->id + " then " + second->id);

                // ---- first lock
                if (!first->m.try_lock()) {
                    E(first->id, ThreadState::Waiting, LockState::Waiting, TxStatus::Pending,
                      "Waiting for mutex on " + first->id + " (held by " + graph.holderOf(first->id) + ")");
                    graph.setWaits(i, first->id);
                    first->m.lock();
                    graph.setWaits(i, "");
                }
                graph.setHolds(i, first->id);
                E(first->id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                  "Mutex acquired on " + first->id + " (holding it while processing)");

                if (!cfg.ordered) holdBarrier.arriveAndWait();  // both threads now hold exactly one lock
                if (cfg.holdMs > 0) std::this_thread::sleep_for(std::chrono::milliseconds(cfg.holdMs));

                // ---- second lock (timed)
                bool got = second->m.try_lock();
                if (!got) {
                    graph.setWaits(i, second->id);
                    E(second->id, ThreadState::Waiting, LockState::Waiting, TxStatus::Pending,
                      "Waiting for mutex on " + second->id + " (held by " + graph.holderOf(second->id) +
                          ") while still holding " + first->id);
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(cfg.timeoutMs);
                    while (!got) {
                        got = second->m.try_lock_for(std::chrono::milliseconds(50));
                        if (got) break;
                        std::vector<std::string> lines;
                        const std::string cyc = graph.findCycle(lines);
                        if (!cyc.empty() && !cycleReported.exchange(true)) {
                            std::lock_guard<std::mutex> lk(repMtx);
                            rep.circularWait = cyc;
                            rep.lockStates = lines;
                            E(second->id, ThreadState::Blocked, LockState::Waiting, TxStatus::Pending,
                              "DEADLOCK: circular wait detected - " + cyc);
                        }
                        if (std::chrono::steady_clock::now() >= deadline) break;
                    }
                    graph.setWaits(i, "");
                }

                if (!got) {
                    // Time-out: give up. Both threads decide together, THEN release, so neither
                    // can sneak through the lock the other one is about to free.
                    E(second->id, ThreadState::Blocked, LockState::TimedOut, TxStatus::Error,
                      "TIMEOUT after " + std::to_string(cfg.timeoutMs) + " ms waiting for " + second->id +
                          " - deadlock confirmed, aborting transfer");
                    timedOut[i] = true;
                    {
                        std::unique_lock<std::mutex> lk(timeoutMtx);
                        ++timedOutCount;
                        timeoutCv.notify_all();
                        timeoutCv.wait_for(lk, std::chrono::milliseconds(1500), [&] { return timedOutCount >= 2; });
                    }
                    graph.setHolds(i, "");
                    E(first->id, ThreadState::Failed, LockState::Released, TxStatus::Error,
                      "Mutex released on " + first->id + " (back-off) - transfer aborted, no money moved");
                    first->m.unlock();
                    outcomes[i].result = "ABORTED - deadlock time-out (no money moved)";
                    outcomes[i].success = false;
                    outcomes[i].finalState = ThreadState::Failed;
                    return;
                }

                // ---- both locks held: perform the transfer atomically
                E(second->id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                  "Mutex acquired on " + second->id + " - both accounts locked");
                bool ok = false;
                if (src_->balance >= plan.amount) {
                    src_->balance -= plan.amount;
                    dst_->balance += plan.amount;
                    ok = true;
                }
                E(src_->id, ok ? ThreadState::Completed : ThreadState::Failed, LockState::None,
                  ok ? TxStatus::Success : TxStatus::InsufficientFunds,
                  ok ? "Transfer successful: " + formatMoney(plan.amount) + " " + src_->id + " -> " + dst_->id
                     : "Transfer rejected: insufficient balance");
                graph.setHolds(i, "");
                E(second->id, ThreadState::Executing, LockState::Released, TxStatus::Pending, "Mutex released on " + second->id);
                second->m.unlock();
                E(first->id, ThreadState::Executing, LockState::Released, TxStatus::Pending, "Mutex released on " + first->id);
                first->m.unlock();
                completed[i] = ok;
                outcomes[i].result = ok ? "Transfer COMPLETED" : "Rejected - insufficient funds";
                outcomes[i].success = ok;
                outcomes[i].finalState = ok ? ThreadState::Completed : ThreadState::Failed;
            });
        }
        tm.joinAll();
    }
    rep.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    // ---------------------------------------------------------------- analysis
    for (int i = 0; i < 2; ++i) {
        const Plan& p = plans[i];
        outcomes[i].threadLabel = labels[i];
        outcomes[i].customerName = p.who.customerName;
        outcomes[i].channel = toString(p.who.channel);
        outcomes[i].operation = "Transfer " + formatMoney(p.amount) + " " + accts[p.from].id + " -> " + accts[p.to].id;
        if (outcomes[i].success) ++rep.reportedSuccesses;
        rep.outcomes.push_back(outcomes[i]);
    }
    rep.expectedSuccesses = 2;

    // Expected = both transfers complete (they are independent of order here).
    Money expA = cfg.balanceA, expB = cfg.balanceB;
    expA -= cfg.amountAtoB;
    expB += cfg.amountAtoB;
    expB -= cfg.amountBtoA;
    expA += cfg.amountBtoA;
    rep.accounts.push_back({accts[0].id, cfg.balanceA, expA, accts[0].balance});
    rep.accounts.push_back({accts[1].id, cfg.balanceB, expB, accts[1].balance});

    const bool deadlocked = timedOut[0] || timedOut[1];
    const bool both = completed[0] && completed[1];
    rep.problemDetected = deadlocked;
    rep.correct = both && accts[0].balance == expA && accts[1].balance == expB;
    rep.events = collector.events();

    if (!cfg.ordered) {
        if (deadlocked) {
            rep.verdict = "DEADLOCK DETECTED";
            if (rep.lockStates.empty())
                rep.lockStates = {accts[0].id + ": LOCKED", accts[1].id + ": LOCKED"};
            rep.explanation =
                "Thread-01 locked " + accts[0].id + " and then asked for " + accts[1].id + ". At the same time Thread-02 locked " +
                accts[1].id + " and asked for " + accts[0].id + ". Each thread holds the lock the other one needs and will not release "
                "it until it gets the second lock: a circular wait. With plain mutexes both threads would block forever.\n\n"
                "Here the second lock is requested with a time-out (std::timed_mutex::try_lock_for), so after " +
                std::to_string(cfg.timeoutMs) + " ms both transfers back off and release their locks. No money moved; the balances are "
                "unchanged, but neither customer's transfer was completed.";
        } else {
            rep.verdict = "No deadlock this run";
            rep.explanation = "The threads happened not to wait on each other in a cycle during this run.";
        }
    } else {
        rep.verdict = both && !deadlocked ? "NO DEADLOCK - both transfers completed" : "UNEXPECTED RESULT";
        rep.lockStates = {accts[0].id + ": free (released in order)", accts[1].id + ": free (released in order)"};
        rep.explanation =
            "Both transfers now acquire the locks in the same global order - ascending account id (" + std::min(accts[0].id, accts[1].id) +
            " first, then " + std::max(accts[0].id, accts[1].id) + ") - regardless of the transfer direction. "
            "Whichever thread gets the first lock proceeds; the other waits (WAITING) for it and can never hold a lock that the "
            "first thread needs. A circular wait is therefore impossible, so deadlock cannot occur.\n\n"
            "Both transfers completed and the final balances are correct.";
    }
    return rep;
}

}  // namespace banksim
