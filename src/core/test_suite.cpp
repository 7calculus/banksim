#include "test_suite.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <random>
#include <set>

#include "bank.h"
#include "concurrency_lab.h"
#include "deadlock_simulator.h"
#include "stress_test.h"
#include "thread_manager.h"
#include "transaction_engine.h"

namespace banksim {

namespace {

// A private bank + engine so tests never disturb each other or the GUI's bank.
struct Fixture {
    EventLog log;
    Bank bank;
    TransactionEngine engine;
    ThreadManager tm;
    Fixture() : bank(log), engine(bank, log), tm("Thread") { log.setVerbose(false); }
};

TxRequest req(TxType t, const std::string& cust, const std::string& acct, Money amount,
              const std::string& dest = "", Channel ch = Channel::ATM1) {
    TxRequest r;
    r.type = t;
    r.customerId = cust;
    r.accountId = acct;
    r.destAccountId = dest;
    r.amount = amount;
    r.channel = ch;
    return r;
}

// Executes all requests on separate worker threads released by a start barrier.
std::vector<TxResult> runParallel(Fixture& f, std::vector<TxRequest> reqs) {
    std::vector<TxResult> out(reqs.size());
    CyclicBarrier bar(static_cast<int>(reqs.size()));
    for (std::size_t i = 0; i < reqs.size(); ++i) {
        f.tm.spawn([&, i](const std::string& label) {
            reqs[i].threadLabel = label;
            bar.arriveAndWait();
            out[i] = f.engine.execute(reqs[i]);
        });
    }
    f.tm.joinAll();
    return out;
}

Money bal(Fixture& f, const std::string& acct) { return f.bank.account(acct).balance; }

using Body = std::function<bool(std::string&)>;
struct TestCase {
    std::string name;
    Body body;
};

// --------------------------------------------------------------------- the tests

bool t1_sequentialDeposit(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addAccount("ACC1001", "Savings", 1000, {"CUST-A"});
    const TxResult r = f.engine.execute(req(TxType::Deposit, "CUST-A", "ACC1001", 500));
    d = "Balance " + formatMoney(1000) + " + deposit " + formatMoney(500) + " -> " + formatMoney(bal(f, "ACC1001")) +
        " (expected " + formatMoney(1500) + ")";
    return r.ok() && bal(f, "ACC1001") == 1500;
}

bool t2_sequentialWithdrawal(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addAccount("ACC1001", "Savings", 10000, {"CUST-A"});
    const TxResult r = f.engine.execute(req(TxType::Withdrawal, "CUST-A", "ACC1001", 2000));
    d = "Balance " + formatMoney(10000) + " - withdrawal " + formatMoney(2000) + " -> " + formatMoney(bal(f, "ACC1001")) +
        " (expected " + formatMoney(8000) + ")";
    return r.ok() && bal(f, "ACC1001") == 8000;
}

bool t3_unsafeWithdrawal(std::string& d) {
    bool all = true;
    LabReport first;
    for (int i = 0; i < 5; ++i) {
        RaceConfig cfg;
        cfg.stepDelayMs = 0;
        LabReport r = runRaceCondition(nullptr, cfg);
        if (i == 0) first = r;
        const bool bad = r.reportedSuccesses == 2 && r.problemDetected && r.accounts[0].actual != 3000 &&
                         r.overdrawn == 3000;
        all = all && bad;
    }
    d = "5/5 runs reproduced the race: both withdrawals (" + formatMoney(7000) + " + " + formatMoney(6000) +
        ") reported SUCCESS on a " + formatMoney(10000) + " account; final balance " +
        formatMoney(first.accounts[0].actual) + " instead of " + formatMoney(first.accounts[0].expected) +
        " (bank loses " + formatMoney(first.overdrawn) + ")";
    return all;
}

bool t4_safeWithdrawal(std::string& d) {
    bool all = true;
    for (int i = 0; i < 5; ++i) {
        SyncConfig cfg;
        cfg.processingMs = 40;
        LabReport r = runSynchronized(nullptr, cfg);
        const int okCount = static_cast<int>(std::count_if(r.outcomes.begin(), r.outcomes.end(),
                                                           [](const LabOutcome& o) { return o.success; }));
        all = all && okCount == 1 && r.accounts[0].actual == 3000 && r.correct && r.outcomes[0].success;
    }
    d = "5/5 runs: Customer A's " + formatMoney(7000) + " succeeded, Customer B's " + formatMoney(6000) +
        " was rejected after waiting for the mutex; final balance " + formatMoney(3000);
    return all;
}

bool t5_concurrentDeposits(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addAccount("ACC1001", "Savings", 1000, {"CUST-A"});
    bool all = true;
    Money last = 0;
    // 20 rounds at full speed, then 5 rounds where every deposit spends 3 ms inside the
    // critical section - that widens the read-modify-write window so that a missing lock
    // would corrupt the balance on practically every round.
    for (int round = 0; round < 25; ++round) {
        f.engine.setProcessingDelayMs(round < 20 ? 0 : 3);
        f.bank.setStartingBalance("ACC1001", 1000);
        std::vector<TxRequest> reqs;
        for (int i = 0; i < 10; ++i) reqs.push_back(req(TxType::Deposit, "CUST-A", "ACC1001", 500));
        auto res = runParallel(f, reqs);
        last = bal(f, "ACC1001");
        all = all && last == 6000 && std::all_of(res.begin(), res.end(), [](const TxResult& r) { return r.ok(); });
    }
    d = "25 rounds x 10 threads x " + formatMoney(500) + " on " + formatMoney(1000) + " -> " + formatMoney(last) +
        " every round (expected " + formatMoney(6000) + ")";
    return all;
}

bool t6_independentAccounts(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-X", "Customer X");
    for (int i = 1; i <= 4; ++i) f.bank.addAccount("ACC300" + std::to_string(i), "Savings", 1000, {"CUST-X"});
    f.engine.setProcessingDelayMs(150);
    f.engine.resetDiagnostics();
    std::vector<TxRequest> reqs;
    for (int i = 1; i <= 4; ++i) {
        const std::string acct = "ACC300" + std::to_string(i);
        reqs.push_back(i % 2 ? req(TxType::Deposit, "CUST-X", acct, 500) : req(TxType::Withdrawal, "CUST-X", acct, 300));
    }
    const auto t0 = std::chrono::steady_clock::now();
    auto res = runParallel(f, reqs);
    const long long ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    bool correct = true;
    for (int i = 1; i <= 4; ++i) correct = correct && bal(f, "ACC300" + std::to_string(i)) == (i % 2 ? 1500 : 700);
    const int maxCrit = f.engine.maxConcurrentCritical();
    d = "4 transactions on 4 different accounts, each holding its lock for 150 ms: up to " + std::to_string(maxCrit) +
        " ran inside critical sections at the same time; " + std::to_string(ms) + " ms total (a single global lock would need >= 600 ms)";
    return correct && maxCrit >= 3 && std::all_of(res.begin(), res.end(), [](const TxResult& r) { return r.ok(); });
}

bool t7_concurrentTransfers(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-X", "Customer X");
    const std::vector<std::string> ids = {"ACC4001", "ACC4002", "ACC4003", "ACC4004"};
    for (const auto& id : ids) f.bank.addAccount(id, "Savings", 5000, {"CUST-X"});
    const Money total0 = f.bank.totalDeposits();

    for (int t = 0; t < 8; ++t) {
        f.tm.spawn([&, t](const std::string& label) {
            std::mt19937 rng(1000u + static_cast<unsigned>(t));
            for (int k = 0; k < 300; ++k) {
                const std::string a = ids[rng() % ids.size()];
                std::string b;
                do { b = ids[rng() % ids.size()]; } while (b == a);
                TxRequest r = req(TxType::Transfer, "CUST-X", a, 1 + static_cast<Money>(rng() % 3000), b);
                r.threadLabel = label;
                f.engine.execute(std::move(r));
            }
        });
    }
    f.tm.joinAll();

    // Replay only the SUCCESSFUL transfers over the initial balances.
    std::map<std::string, Money> expected;
    for (const auto& id : ids) expected[id] = 5000;
    int ok = 0, failed = 0;
    bool failuresClean = true;
    for (const auto& r : f.bank.history()) {
        if (r.type != TxType::Transfer) continue;
        if (r.status == TxStatus::Success) {
            expected[r.accountId] -= r.amount;
            expected[r.destAccountId] += r.amount;
            ++ok;
        } else {
            ++failed;
            failuresClean = failuresClean && r.status == TxStatus::InsufficientFunds;
        }
    }
    bool sidesMatch = true, nonNeg = true;
    for (const auto& id : ids) {
        sidesMatch = sidesMatch && bal(f, id) == expected[id];
        nonNeg = nonNeg && bal(f, id) >= 0;
    }
    const Money total1 = f.bank.totalDeposits();
    d = "2400 concurrent transfers (" + std::to_string(ok) + " succeeded, " + std::to_string(failed) +
        " rejected for insufficient funds). Total money " + formatMoney(total0) + " -> " + formatMoney(total1) +
        "; every balance equals the replay of the successful transfers";
    return total0 == total1 && sidesMatch && nonNeg && failuresClean && ok + failed == 2400;
}

bool t8_deadlock(std::string& d) {
    bool all = true;
    long long maxMs = 0;
    std::string cyc;
    for (int i = 0; i < 3; ++i) {
        DeadlockConfig cfg;
        cfg.timeoutMs = 400;
        cfg.holdMs = 100;
        cfg.ordered = false;
        const LabReport r = runDeadlockDemo(nullptr, cfg);
        maxMs = std::max(maxMs, r.elapsedMs);
        if (i == 0) cyc = r.circularWait;
        const bool unchanged = r.accounts[0].actual == r.accounts[0].initial && r.accounts[1].actual == r.accounts[1].initial;
        all = all && r.problemDetected && !r.circularWait.empty() && r.reportedSuccesses == 0 && unchanged &&
              r.elapsedMs < 3000;
    }
    d = "3/3 runs deadlocked and recovered via timed-lock time-out (longest " + std::to_string(maxMs) +
        " ms, the application never froze). Detected: " + cyc;
    return all;
}

bool t9_deadlockPrevention(std::string& d) {
    bool all = true;
    long long maxMs = 0;
    for (int i = 0; i < 3; ++i) {
        DeadlockConfig cfg;
        cfg.timeoutMs = 400;
        cfg.holdMs = 100;
        cfg.ordered = true;
        const LabReport r = runDeadlockDemo(nullptr, cfg);
        maxMs = std::max(maxMs, r.elapsedMs);
        all = all && !r.problemDetected && r.reportedSuccesses == 2 && r.correct;
    }
    d = "3/3 runs: identical opposite transfers (A->B and B->A) with locks taken in ascending id order - no deadlock, "
        "both completed, correct balances (longest " + std::to_string(maxMs) + " ms)";
    return all;
}

bool t10_concurrentEnquiries(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addAccount("ACC1001", "Savings", 10000, {"CUST-A"});
    const int writers = 4, readers = 6;
    std::vector<std::vector<Money>> seen(static_cast<std::size_t>(readers));
    CyclicBarrier bar(writers + readers);
    for (int w = 0; w < writers; ++w) {
        f.tm.spawn([&](const std::string& label) {
            bar.arriveAndWait();
            for (int k = 0; k < 200; ++k) {
                TxRequest a = req(TxType::Deposit, "CUST-A", "ACC1001", 1000);
                a.threadLabel = label;
                f.engine.execute(std::move(a));
                TxRequest b = req(TxType::Withdrawal, "CUST-A", "ACC1001", 1000);
                b.threadLabel = label;
                f.engine.execute(std::move(b));
            }
        });
    }
    for (int r = 0; r < readers; ++r) {
        f.tm.spawn([&, r](const std::string& label) {
            bar.arriveAndWait();
            for (int k = 0; k < 500; ++k) {
                TxRequest q = req(TxType::BalanceEnquiry, "CUST-A", "ACC1001", 0);
                q.threadLabel = label;
                seen[static_cast<std::size_t>(r)].push_back(f.engine.execute(std::move(q)).balanceAfter);
            }
        });
    }
    f.tm.joinAll();
    std::size_t n = 0;
    bool consistent = true;
    for (const auto& v : seen)
        for (Money m : v) {
            ++n;
            // A reader may only ever observe 10,000 + k*1,000 (k = writers currently between
            // their deposit and withdrawal) - never a torn / partial value.
            consistent = consistent && m >= 10000 && m <= 10000 + writers * 1000 && (m - 10000) % 1000 == 0;
        }
    const bool finalOk = bal(f, "ACC1001") == 10000;

    // Readers use a shared lock, so several of them must be able to read at the same time.
    f.engine.setProcessingDelayMs(60);
    f.engine.resetDiagnostics();
    std::vector<TxRequest> rs;
    for (int i = 0; i < 5; ++i) rs.push_back(req(TxType::BalanceEnquiry, "CUST-A", "ACC1001", 0));
    runParallel(f, rs);
    const int maxReaders = f.engine.maxConcurrentReaders();
    d = std::to_string(n) + " enquiries ran during 1600 concurrent writes; all returned a consistent value (" + formatMoney(10000) +
        " + k x " + formatMoney(1000) + "); final balance " + formatMoney(bal(f, "ACC1001")) + "; up to " +
        std::to_string(maxReaders) + " readers held the shared lock simultaneously";
    return consistent && finalOk && maxReaders >= 2;
}

bool t11_validationRules(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addCustomer("CUST-B", "Customer B");
    f.bank.addAccount("ACC1001", "Savings", 1000, {"CUST-A"});
    f.bank.addAccount("ACC1002", "Savings", 500, {"CUST-B"});
    auto st = [&](TxRequest r) { return f.engine.execute(std::move(r)).status; };
    bool ok = true;
    ok = ok && st(req(TxType::Deposit, "CUST-A", "ACC1001", -50)) == TxStatus::InvalidAmount;
    ok = ok && st(req(TxType::Withdrawal, "CUST-A", "ACC1001", 0)) == TxStatus::InvalidAmount;
    ok = ok && st(req(TxType::Withdrawal, "CUST-B", "ACC1001", 10)) == TxStatus::Unauthorized;
    ok = ok && st(req(TxType::Transfer, "CUST-A", "ACC1001", 10, "ACC1001")) == TxStatus::SameAccount;
    ok = ok && st(req(TxType::BalanceEnquiry, "CUST-A", "ACC9999", 0)) == TxStatus::AccountNotFound;
    ok = ok && st(req(TxType::Withdrawal, "CUST-A", "ACC1001", 5000)) == TxStatus::InsufficientFunds;
    const TxResult tr = f.engine.execute(req(TxType::Transfer, "CUST-A", "ACC1001", 5000, "ACC1002"));
    ok = ok && tr.status == TxStatus::InsufficientFunds && bal(f, "ACC1001") == 1000 && bal(f, "ACC1002") == 500;
    d = "negative/zero amount, unauthorized customer, same-account transfer, unknown account, overdraft and a failed transfer "
        "(both balances untouched: " + formatMoney(bal(f, "ACC1001")) + " / " + formatMoney(bal(f, "ACC1002")) + ")";
    return ok;
}

bool t12_uniqueIds(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addAccount("ACC1001", "Savings", 1000, {"CUST-A"});
    std::vector<std::vector<std::string>> ids(20);
    CyclicBarrier bar(20);
    for (int t = 0; t < 20; ++t) {
        f.tm.spawn([&, t](const std::string&) {
            bar.arriveAndWait();
            for (int k = 0; k < 100; ++k)
                ids[static_cast<std::size_t>(t)].push_back(
                    f.engine.execute(req(TxType::BalanceEnquiry, "CUST-A", "ACC1001", 0)).txId);
        });
    }
    f.tm.joinAll();
    std::set<std::string> uniq;
    std::size_t n = 0;
    for (const auto& v : ids) { n += v.size(); uniq.insert(v.begin(), v.end()); }
    d = std::to_string(n) + " transaction ids generated by 20 threads, " + std::to_string(uniq.size()) +
        " distinct; history holds " + std::to_string(f.bank.historySize()) + " records";
    return uniq.size() == n && f.bank.historySize() == n;
}

bool t13_stress(std::string& d) {
    Fixture f;
    f.bank.loadSampleData();
    StressConfig cfg;
    cfg.threads = 16;
    cfg.opsPerThread = 250;
    const StressReport r = runStressTest(f.bank, f.engine, f.tm, f.log, cfg);
    d = std::to_string(r.operations) + " random operations on 16 threads in " + std::to_string(r.elapsedMs) + " ms (" +
        std::to_string(r.succeeded) + " ok / " + std::to_string(r.failed) + " rejected). Total money expected " +
        formatMoney(r.expectedTotal) + ", actual " + formatMoney(r.actualTotal);
    return r.consistent && r.operations == 4000;
}

bool t14_atmCash(std::string& d) {
    Fixture f;
    f.bank.addCustomer("CUST-A", "Customer A");
    f.bank.addAccount("ACC1001", "Savings", 10000, {"CUST-A"});
    f.bank.setAtmCashEnabled(true);
    f.bank.setAtmCash(Channel::ATM1, 5000);
    bool ok = f.engine.execute(req(TxType::Withdrawal, "CUST-A", "ACC1001", 4000, "", Channel::ATM1)).ok();
    const TxResult r2 = f.engine.execute(req(TxType::Withdrawal, "CUST-A", "ACC1001", 2000, "", Channel::ATM1));
    ok = ok && r2.status == TxStatus::AtmCashInsufficient && bal(f, "ACC1001") == 6000;
    ok = ok && f.engine.execute(req(TxType::Withdrawal, "CUST-A", "ACC1001", 2000, "", Channel::OnlineBanking)).ok();
    d = "ATM 1 held " + formatMoney(5000) + ": " + formatMoney(4000) + " dispensed, the next " + formatMoney(2000) +
        " refused (ATM cash short, account untouched), the same amount via online banking succeeded";
    return ok && bal(f, "ACC1001") == 4000;
}

bool t15_reset(std::string& d) {
    Fixture f;
    f.bank.loadSampleData();
    f.engine.execute(req(TxType::Withdrawal, "CUST-A", "ACC1001", 3000));
    f.engine.execute(req(TxType::Deposit, "CUST-C", "ACC1002", 99));
    const bool changed = bal(f, "ACC1001") == 7000;
    f.bank.reset();
    d = "after reset: ACC1001 = " + formatMoney(bal(f, "ACC1001")) + ", ACC1002 = " + formatMoney(bal(f, "ACC1002")) +
        ", history records = " + std::to_string(f.bank.historySize());
    return changed && bal(f, "ACC1001") == 10000 && bal(f, "ACC1002") == 25000 && f.bank.historySize() == 0 &&
           f.bank.stats().total == 0;
}

const std::vector<TestCase>& allCases() {
    static const std::vector<TestCase> cases = {
        {"Sequential deposit", t1_sequentialDeposit},
        {"Sequential withdrawal", t2_sequentialWithdrawal},
        {"Concurrent withdrawal - UNSAFE mode (race condition shown)", t3_unsafeWithdrawal},
        {"Concurrent withdrawal - SAFE mode (mutex)", t4_safeWithdrawal},
        {"Concurrent deposits (10 threads)", t5_concurrentDeposits},
        {"Concurrent operations on different accounts", t6_independentAccounts},
        {"Concurrent fund transfers (atomicity, money conserved)", t7_concurrentTransfers},
        {"Deadlock demonstration (circular wait + time-out)", t8_deadlock},
        {"Deadlock prevention (consistent lock ordering)", t9_deadlockPrevention},
        {"Concurrent balance enquiries (consistent reads)", t10_concurrentEnquiries},
        {"Banking rules / validation", t11_validationRules},
        {"Unique transaction IDs under concurrency", t12_uniqueIds},
        {"Randomized stress test (conservation of money)", t13_stress},
        {"Optional ATM physical cash limit", t14_atmCash},
        {"Reset simulation", t15_reset},
    };
    return cases;
}

}  // namespace

int testCount() { return static_cast<int>(allCases().size()); }

std::vector<TestResult> runAllTests(const std::function<void(const TestResult&)>& progress) {
    std::vector<TestResult> out;
    int n = 0;
    for (const auto& tc : allCases()) {
        TestResult r;
        r.number = ++n;
        r.name = tc.name;
        const auto t0 = std::chrono::steady_clock::now();
        try {
            r.passed = tc.body(r.detail);
        } catch (const std::exception& e) {
            r.passed = false;
            r.detail = std::string("exception: ") + e.what();
        }
        r.ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        out.push_back(r);
        if (progress) progress(r);
    }
    return out;
}

}  // namespace banksim
