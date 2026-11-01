// transaction_engine.h - the SAFE banking engine: deposit, withdrawal, balance enquiry and
// atomic fund transfer protected by per-account mutexes (RAII locking, ordered acquisition).
// The deliberately broken "unsafe" variant lives in concurrency_lab.cpp and is completely
// isolated from this class.
#pragma once

#include <atomic>

#include "bank.h"
#include "transaction.h"

namespace banksim {

class TransactionEngine {
public:
    TransactionEngine(Bank& bank, EventLog& log);

    // Executes one transaction on the calling thread. Never throws; failures are reported
    // through TxResult::status. Every call is recorded in the bank's history and logged.
    TxResult execute(TxRequest req);

    // Simulated bank-side processing time spent *inside* the critical section. 0 = none.
    // A non-zero value makes lock contention visible in the thread monitor.
    void setProcessingDelayMs(int ms) { delayMs_.store(ms); }
    int processingDelayMs() const { return delayMs_.load(); }

    // Diagnostics used by the automated tests and the dashboard:
    // the highest number of threads that were simultaneously inside a critical section /
    // simultaneously reading under a shared lock.
    int maxConcurrentCritical() const { return critMax_.load(); }
    int maxConcurrentReaders() const { return readMax_.load(); }
    void resetDiagnostics();

    static constexpr Money kMaxAmount = 1000000000;  // sanity limit (1 billion)

private:
    void simulateProcessing() const;

    Bank& bank_;
    EventLog& log_;
    std::atomic<int> delayMs_{0};
    std::atomic<int> critNow_{0}, critMax_{0};
    std::atomic<int> readNow_{0}, readMax_{0};
};

}  // namespace banksim
