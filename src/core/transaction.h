// transaction.h - request / result / history record of a banking transaction.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "types.h"

namespace banksim {

struct TxRequest {
    TxType type = TxType::BalanceEnquiry;
    std::string customerId;
    std::string accountId;       // account being operated on (source for transfers)
    std::string destAccountId;   // transfers only
    Money amount = 0;
    Channel channel = Channel::ATM1;

    std::string threadLabel;     // filled in by the worker thread that executes it
    std::string source = "main"; // event-source tag (main bank vs. a concurrency lab)

    // Optional hook: invoked once the engine has made its first lock attempt (the
    // thread is now either inside the critical section or queued behind the mutex).
    // The labs use it to start threads in a reproducible arrival order.
    std::function<void()> onLockQueued;
};

struct TxResult {
    std::string txId;
    TxStatus status = TxStatus::Pending;
    std::string message;
    Money balanceAfter = 0;       // balance of the primary account after the operation
    Money destBalanceAfter = 0;   // transfers: destination balance after the operation
    std::string threadLabel;
    bool ok() const { return status == TxStatus::Success; }
};

// Persistent history entry.
struct TxRecord {
    std::string txId;
    std::int64_t timestampMs = 0;
    TxType type = TxType::BalanceEnquiry;
    std::string customerId;
    std::string customerName;
    std::string accountId;
    std::string destAccountId;
    Channel channel = Channel::ATM1;
    std::string threadLabel;
    std::string threadId;
    Money amount = 0;
    TxStatus status = TxStatus::Pending;
    Money balanceAfter = 0;
    std::string message;
    std::string source = "main";
};

}  // namespace banksim
