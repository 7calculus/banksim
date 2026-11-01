// stress_test.h - automated randomized load test on the real (safe) engine, verified with
// a conservation-of-money invariant.
#pragma once

#include <atomic>

#include "bank.h"
#include "thread_manager.h"
#include "transaction_engine.h"

namespace banksim {

struct StressConfig {
    int threads = 20;
    int opsPerThread = 50;
    unsigned seed = 12345;
    int processingMs = 0;                       // simulated time inside each critical section
    bool verboseEvents = false;                 // lock-level events for every operation (slow, huge)
    const std::atomic<bool>* cancel = nullptr;  // optional cooperative cancellation
};

struct StressReport {
    int threads = 0;
    std::uint64_t operations = 0;
    std::uint64_t succeeded = 0;
    std::uint64_t failed = 0;
    Money initialTotal = 0;
    Money expectedTotal = 0;  // initial + successful deposits - successful withdrawals
    Money actualTotal = 0;
    bool noNegativeBalance = true;
    bool uniqueTxIds = true;
    bool consistent = false;
    bool cancelled = false;
    long long elapsedMs = 0;
    int maxConcurrentCritical = 0;
};

// Precondition: no other thread is modifying the bank while the test runs.
StressReport runStressTest(Bank& bank, TransactionEngine& engine, ThreadManager& tm, EventLog& log,
                           const StressConfig& cfg);

}  // namespace banksim
