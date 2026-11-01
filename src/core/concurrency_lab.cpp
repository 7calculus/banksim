#include "concurrency_lab.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include <thread>

#include "thread_manager.h"

namespace banksim {

// ------------------------------------------------------------------ LabCollector

LabCollector::LabCollector(EventLog* global) {
    if (global) {
        sinkId_ = local_.addSink([global](const Event& e) { global->add(e); });
    }
}

LabCollector::~LabCollector() {
    if (sinkId_) local_.removeSink(sinkId_);
}

// ------------------------------------------------------------------- helpers

std::vector<LabParticipant> defaultJointWithdrawals() {
    return {
        {"CUST-A", "Customer A", Channel::ATM1, TxType::Withdrawal, 7000},
        {"CUST-B", "Customer B", Channel::ATM2, TxType::Withdrawal, 6000},
    };
}

ExpectedOutcome computeExpected(Money initial, const std::vector<LabParticipant>& p) {
    ExpectedOutcome e;
    Money bal = initial;
    for (const auto& x : p) {
        if (x.type == TxType::Deposit) {
            bal += x.amount;
            e.success.push_back(true);
        } else if (bal >= x.amount) {
            bal -= x.amount;
            e.success.push_back(true);
        } else {
            e.success.push_back(false);
        }
    }
    e.finalBalance = bal;
    return e;
}

namespace {

std::string opText(const LabParticipant& p) {
    return std::string(p.type == TxType::Deposit ? "Deposit " : "Withdraw ") + formatMoney(p.amount);
}

void sleepMs(int ms) {
    if (ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

Event makeEvent(const std::string& source, const std::string& label, const LabParticipant& p,
                const std::string& account, ThreadState ts, LockState ls, TxStatus st,
                const std::string& msg) {
    Event e;
    e.source = source;
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
    return e;
}

}  // namespace

// ============================================================================
// MODE 1 - race condition (isolated, deterministic, free of C++ undefined behaviour)
// ============================================================================

LabReport runRaceCondition(EventLog* globalLog, RaceConfig cfg) {
    if (cfg.participants.empty()) cfg.participants = defaultJointWithdrawals();
    const auto& parts = cfg.participants;
    const int n = static_cast<int>(parts.size());

    LabReport rep;
    rep.kind = LabKind::Race;
    rep.title = "Race condition - NO synchronization";

    LabCollector collector(globalLog);
    EventLog& log = collector.log();
    const std::string src = "lab:race";

    // The shared balance is an std::atomic so every individual read/write is well-defined.
    // The *bug* is the unprotected read-check-write SEQUENCE, not the individual accesses.
    std::atomic<Money> balance{cfg.initial};

    CyclicBarrier barrier(n);
    std::mutex turnMtx;
    std::condition_variable turnCv;
    int turn = 0;

    std::vector<Money> readValue(static_cast<std::size_t>(n), 0);
    std::vector<char> decidedOk(static_cast<std::size_t>(n), 0);
    std::vector<std::string> labels(static_cast<std::size_t>(n));
    std::vector<Money> written(static_cast<std::size_t>(n), -1);

    const auto t0 = std::chrono::steady_clock::now();
    {
        ThreadManager tm("Thread");
        for (int i = 0; i < n; ++i) {
            tm.spawn([&, i](const std::string& label) {
                const auto& p = parts[static_cast<std::size_t>(i)];
                const std::size_t k = static_cast<std::size_t>(i);
                labels[k] = label;
                auto emit = [&](ThreadState ts, LockState ls, TxStatus st, const std::string& msg) {
                    log.add(makeEvent(src, label, p, cfg.accountId, ts, ls, st, msg));
                };

                // ---- phase 0: everybody starts at the same instant
                barrier.arriveAndWait();
                emit(ThreadState::Executing, LockState::None, TxStatus::Pending,
                     std::string(toString(p.type)) + " initiated (" + formatMoney(p.amount) + ") - no lock will be taken");
                sleepMs(cfg.stepDelayMs);

                // ---- phase 1: READ the balance (every thread sees the same stale value)
                readValue[k] = balance.load();
                emit(ThreadState::Executing, LockState::None, TxStatus::Pending,
                     "READ balance = " + formatMoney(readValue[k]) + " (no mutex held)");
                barrier.arriveAndWait();  // all threads have now read the SAME initial balance
                sleepMs(cfg.stepDelayMs);

                // ---- phase 2: CHECK using the stale value
                bool ok = true;
                std::string msg;
                if (p.type == TxType::Withdrawal) {
                    ok = readValue[k] >= p.amount;
                    msg = "CHECK " + formatMoney(p.amount) + " <= " + formatMoney(readValue[k]) + "? " +
                          (ok ? "YES - funds look sufficient" : "NO - insufficient funds");
                } else {
                    msg = "CHECK deposit of " + formatMoney(p.amount) + " accepted";
                }
                decidedOk[k] = ok;
                emit(ok ? ThreadState::Executing : ThreadState::Failed, LockState::None, TxStatus::Pending, msg);
                barrier.arriveAndWait();  // every decision is made before anybody writes
                sleepMs(cfg.stepDelayMs);

                // ---- phase 3: WRITE - strictly one after the other, in thread order, so the
                // lost update happens identically on every run and on every machine.
                {
                    std::unique_lock<std::mutex> lk(turnMtx);
                    turnCv.wait(lk, [&] { return turn == i; });
                }
                if (ok) {
                    const Money newBal = p.type == TxType::Deposit ? readValue[k] + p.amount : readValue[k] - p.amount;
                    balance.store(newBal);  // computed from the STALE read: overwrites other updates
                    written[k] = newBal;
                    emit(ThreadState::Executing, LockState::None, TxStatus::Pending,
                         "WRITE balance = " + formatMoney(newBal) + " (computed from stale " + formatMoney(readValue[k]) + ")");
                    emit(ThreadState::Completed, LockState::None, TxStatus::Success,
                         std::string(p.type == TxType::Deposit ? "Deposit" : "Withdrawal") + " reported SUCCESSFUL");
                } else {
                    emit(ThreadState::Failed, LockState::None, TxStatus::InsufficientFunds,
                         "Withdrawal rejected: insufficient balance");
                }
                {
                    std::lock_guard<std::mutex> lk(turnMtx);
                    ++turn;
                }
                turnCv.notify_all();
            });
        }
        tm.joinAll();
    }
    rep.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    // ---------------------------------------------------------------- analysis
    const ExpectedOutcome exp = computeExpected(cfg.initial, parts);
    const Money actual = balance.load();

    rep.accounts.push_back({cfg.accountId, cfg.initial, exp.finalBalance, actual});
    Money paidOut = 0, deposited = 0;
    bool outcomeMismatch = false;
    for (int i = 0; i < n; ++i) {
        const auto& p = parts[static_cast<std::size_t>(i)];
        const bool ok = decidedOk[static_cast<std::size_t>(i)] != 0;
        LabOutcome o;
        o.threadLabel = labels[static_cast<std::size_t>(i)];
        o.customerName = p.customerName;
        o.channel = toString(p.channel);
        o.operation = opText(p);
        o.success = ok;
        o.finalState = ok ? ThreadState::Completed : ThreadState::Failed;
        o.result = ok ? "Reported SUCCESS" : "Rejected";
        if (ok != exp.success[static_cast<std::size_t>(i)]) {
            outcomeMismatch = true;
            o.result += ok ? " (should have FAILED)" : " (should have succeeded)";
        }
        if (ok) {
            ++rep.reportedSuccesses;
            (p.type == TxType::Withdrawal ? paidOut : deposited) += p.amount;
        }
        if (exp.success[static_cast<std::size_t>(i)]) ++rep.expectedSuccesses;
        rep.outcomes.push_back(o);
    }
    rep.overdrawn = std::max<Money>(0, paidOut - (cfg.initial + deposited));
    rep.problemDetected = (actual != exp.finalBalance) || outcomeMismatch;
    rep.correct = !rep.problemDetected;
    rep.events = collector.events();
    rep.lockStates.push_back(cfg.accountId + ": NO LOCK - the account is unprotected");

    if (rep.problemDetected) {
        rep.verdict = "RACE CONDITION DETECTED";
        std::string ex = "Every thread read the same starting balance (" + formatMoney(cfg.initial) +
                         ") before any thread wrote its result. Each one then made its decision from that stale value, "
                         "and each wrote back a balance computed from it, so the writes overwrote each other (a lost update).\n\n";
        if (rep.overdrawn > 0) {
            ex += "Result: " + std::to_string(rep.reportedSuccesses) + " withdrawals were reported successful and " +
                  formatMoney(paidOut) + " would be paid out of an account that only held " +
                  formatMoney(cfg.initial + deposited) + " - the bank loses " + formatMoney(rep.overdrawn) + ".\n\n";
        }
        ex += "The account now shows " + formatMoney(actual) + ", but the correct balance is " + formatMoney(exp.finalBalance) + ".";
        rep.explanation = ex;
    } else {
        rep.verdict = "No race manifested";
        rep.explanation = "With these amounts the unsynchronized interleaving happened to produce the correct result "
                          "(for example because only one thread actually wrote). Use two withdrawals that together exceed "
                          "the balance, or two deposits, to see the lost update.";
    }
    return rep;
}

// ============================================================================
// MODE 2 - the same scenario, protected by the real engine's per-account mutex
// ============================================================================

LabReport runSynchronized(EventLog* globalLog, SyncConfig cfg) {
    if (cfg.participants.empty()) cfg.participants = defaultJointWithdrawals();
    const auto& parts = cfg.participants;
    const int n = static_cast<int>(parts.size());

    LabReport rep;
    rep.kind = LabKind::Synchronized;
    rep.title = "Synchronization - per-account mutex";

    LabCollector collector(globalLog);
    // A private bank + engine: the lab never touches the balances of the main simulation.
    Bank bank(collector.log());
    std::set<std::string> seen;
    std::vector<std::string> holders;
    for (const auto& p : parts) {
        if (seen.insert(p.customerId).second) {
            bank.addCustomer(p.customerId, p.customerName);
            holders.push_back(p.customerId);
        }
    }
    bank.addAccount(cfg.accountId, "Savings", cfg.initial, holders);
    TransactionEngine engine(bank, collector.log());
    engine.setProcessingDelayMs(cfg.processingMs);

    OrderedStart gate(n);
    std::vector<TxResult> results(static_cast<std::size_t>(n));
    std::vector<std::string> labels(static_cast<std::size_t>(n));

    const auto t0 = std::chrono::steady_clock::now();
    {
        ThreadManager tm("Thread");
        for (int i = 0; i < n; ++i) {
            tm.spawn([&, i](const std::string& label) {
                const std::size_t k = static_cast<std::size_t>(i);
                const auto& p = parts[k];
                labels[k] = label;
                gate.waitTurn(i);  // thread i starts once thread i-1 holds / is queued for the mutex
                TxRequest r;
                r.type = p.type;
                r.customerId = p.customerId;
                r.accountId = cfg.accountId;
                r.amount = p.amount;
                r.channel = p.channel;
                r.threadLabel = label;
                r.source = "lab:sync";
                r.onLockQueued = [&gate, i] { gate.signal(i); };
                results[k] = engine.execute(std::move(r));
                gate.signal(i);
            });
        }
        tm.joinAll();
    }
    rep.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    const ExpectedOutcome exp = computeExpected(cfg.initial, parts);
    const AccountView av = bank.account(cfg.accountId);
    rep.accounts.push_back({cfg.accountId, cfg.initial, exp.finalBalance, av.balance});

    bool outcomeMismatch = false;
    for (int i = 0; i < n; ++i) {
        const std::size_t k = static_cast<std::size_t>(i);
        const auto& p = parts[k];
        LabOutcome o;
        o.threadLabel = labels[k];
        o.customerName = p.customerName;
        o.channel = toString(p.channel);
        o.operation = opText(p);
        o.success = results[k].ok();
        o.finalState = o.success ? ThreadState::Completed : ThreadState::Failed;
        o.result = o.success ? "SUCCESS" : std::string("Rejected - ") + toString(results[k].status);
        if (o.success != exp.success[k]) outcomeMismatch = true;
        if (o.success) ++rep.reportedSuccesses;
        if (exp.success[k]) ++rep.expectedSuccesses;
        rep.outcomes.push_back(o);
    }
    rep.correct = !outcomeMismatch && av.balance == exp.finalBalance && av.balance >= 0;
    rep.problemDetected = !rep.correct;
    rep.events = collector.events();
    rep.lockStates.push_back(cfg.accountId + ": std::shared_mutex - free (all threads released it)");
    rep.verdict = rep.correct ? "CONSISTENT - race prevented" : "INCONSISTENT (unexpected)";
    if (rep.correct) {
        rep.explanation =
            "Each transaction had to acquire the account's mutex before touching the balance. While one thread was "
            "inside the critical section the other threads were blocked (WAITING). When the lock was released the next "
            "thread read the LATEST balance, so a withdrawal that no longer fit was rejected instead of overdrawing "
            "the account.\n\nFinal balance " + formatMoney(av.balance) + " equals the correct serial result.";
    } else {
        rep.explanation = "Unexpected result - the final state differs from the serial execution.";
    }
    return rep;
}

}  // namespace banksim
