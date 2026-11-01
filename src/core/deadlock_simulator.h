// deadlock_simulator.h - Mode 3: controlled deadlock demonstration and its prevention.
//
// Two opposite transfers (A -> B and B -> A) run on two isolated accounts:
//   * ordered == false : each thread locks its SOURCE first, then its destination. Both
//                        threads end up holding one lock and waiting for the other's ->
//                        circular wait (deadlock). std::timed_mutex time-outs guarantee that
//                        the demonstration always ends; the application never freezes.
//   * ordered == true  : locks are always taken in ascending account-id order, so the circular
//                        wait cannot form and both transfers complete.
#pragma once

#include "concurrency_lab.h"

namespace banksim {

struct DeadlockConfig {
    std::string accountA = "ACC-A";
    std::string accountB = "ACC-B";
    Money balanceA = 10000;
    Money balanceB = 5000;
    Money amountAtoB = 2000;  // Thread-01: A -> B (Customer A, ATM 1)
    Money amountBtoA = 1000;  // Thread-02: B -> A (Customer B, ATM 2)
    int timeoutMs = 2000;     // how long a thread waits for its second lock before giving up
    int holdMs = 300;         // "processing" time spent holding the first lock (same in both modes)
    bool ordered = false;     // false = deadlock demo, true = deadlock prevention
};

LabReport runDeadlockDemo(EventLog* globalLog, DeadlockConfig cfg);

}  // namespace banksim
