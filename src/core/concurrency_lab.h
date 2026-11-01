// concurrency_lab.h - the teaching demonstrations.
//
//   Mode 1  runRaceCondition()  - deliberately UNSAFE, completely isolated from the real
//                                 engine. Uses only std::atomic loads/stores plus a barrier to
//                                 force a *defined* (no C++ undefined behaviour) bad interleaving.
//   Mode 2  runSynchronized()   - the same scenario on the real, mutex-protected engine.
//   Mode 3  see deadlock_simulator.h
//
// Every run is reproducible and returns a LabReport the GUI can render.
#pragma once

#include <string>
#include <vector>

#include "bank.h"
#include "event_log.h"
#include "transaction_engine.h"

namespace banksim {

enum class LabKind { Race, Synchronized, Deadlock, DeadlockPrevented };

struct LabParticipant {
    std::string customerId;
    std::string customerName;
    Channel channel = Channel::ATM1;
    TxType type = TxType::Withdrawal;  // Deposit or Withdrawal
    Money amount = 0;
};

struct LabAccountRow {
    std::string id;
    Money initial = 0;
    Money expected = 0;
    Money actual = 0;
};

struct LabOutcome {
    std::string threadLabel;
    std::string customerName;
    std::string channel;
    std::string operation;  // "Withdraw Rs 7,000"
    std::string result;     // text shown in the outcome table
    bool success = false;
    ThreadState finalState = ThreadState::Completed;
};

struct LabReport {
    LabKind kind = LabKind::Race;
    std::string title;
    std::vector<LabAccountRow> accounts;
    std::vector<LabOutcome> outcomes;
    std::vector<Event> events;       // ordered execution sequence (with lock states)
    std::vector<std::string> lockStates;  // human-readable lock snapshot lines

    bool problemDetected = false;  // race condition / deadlock actually happened
    bool correct = false;          // final state equals the expected one
    std::string verdict;           // short badge text
    std::string explanation;       // plain-language explanation of what happened
    std::string circularWait;      // deadlock runs: description of the wait cycle
    long long elapsedMs = 0;
    Money overdrawn = 0;           // race runs: money paid out that never existed
    int reportedSuccesses = 0;
    int expectedSuccesses = 0;
};

// Collects every event of a lab run into its own log and forwards a copy to the global
// monitor log. The forwarded copies keep their timestamps and get fresh sequence numbers.
class LabCollector {
public:
    explicit LabCollector(EventLog* global);
    ~LabCollector();
    LabCollector(const LabCollector&) = delete;
    LabCollector& operator=(const LabCollector&) = delete;
    EventLog& log() { return local_; }
    std::vector<Event> events() const { return local_.snapshot(); }

private:
    EventLog local_;
    int sinkId_ = 0;
};

struct RaceConfig {
    std::string accountId = "ACC1001";
    Money initial = 10000;
    std::vector<LabParticipant> participants;  // empty -> defaultJointWithdrawals()
    int stepDelayMs = 250;                     // pause between phases so the sequence is watchable
};

struct SyncConfig {
    std::string accountId = "ACC1001";
    Money initial = 10000;
    std::vector<LabParticipant> participants;  // empty -> defaultJointWithdrawals()
    int processingMs = 300;                    // time each transaction spends in its critical section
};

// Customer A withdraws Rs 7,000 at ATM 1 while Customer B withdraws Rs 6,000 at ATM 2.
std::vector<LabParticipant> defaultJointWithdrawals();

// What a correct, serial execution of the participants (in order) would produce.
struct ExpectedOutcome {
    Money finalBalance = 0;
    std::vector<bool> success;
};
ExpectedOutcome computeExpected(Money initial, const std::vector<LabParticipant>& p);

LabReport runRaceCondition(EventLog* globalLog, RaceConfig cfg);
LabReport runSynchronized(EventLog* globalLog, SyncConfig cfg);

}  // namespace banksim
