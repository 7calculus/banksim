// types.h - fundamental enums and helpers shared by the whole BankSim engine.
// The core library is deliberately Qt-free so the concurrency logic can be
// built and tested without any GUI dependency.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace banksim {

// All money is stored as whole rupees in a signed 64-bit integer.
using Money = std::int64_t;

enum class TxType { Deposit, Withdrawal, BalanceEnquiry, Transfer };

enum class Channel { ATM1, ATM2, ATM3, OnlineBanking, MobileBanking };

enum class TxStatus {
    Pending,
    Success,
    InsufficientFunds,
    InvalidAmount,
    Unauthorized,
    SameAccount,
    AccountNotFound,
    CustomerNotFound,
    AtmCashInsufficient,
    Error
};

enum class ThreadState { Created, Executing, Waiting, Blocked, Completed, Failed };

enum class LockState { None, Waiting, Acquired, Released, TimedOut };

// Access permissions a customer holds on an account (bit mask).
enum Permission : unsigned {
    PermView = 1u,
    PermDeposit = 2u,
    PermWithdraw = 4u,
    PermTransfer = 8u,
    PermFull = PermView | PermDeposit | PermWithdraw | PermTransfer
};

const char* toString(TxType t);
const char* toString(Channel c);
const char* toString(TxStatus s);
const char* toString(ThreadState s);
const char* toString(LockState s);

inline bool isSuccess(TxStatus s) { return s == TxStatus::Success; }
inline bool isAtm(Channel c) {
    return c == Channel::ATM1 || c == Channel::ATM2 || c == Channel::ATM3;
}

const std::vector<Channel>& allChannels();
const std::vector<TxType>& allTxTypes();

// "10000" -> "<rupee sign>10,000"  (UTF-8)
std::string formatMoney(Money m);

// Epoch milliseconds -> "HH:MM:SS.mmm" (local time)
std::string formatClock(std::int64_t epochMs);

// Current time as epoch milliseconds.
std::int64_t nowMs();

// Stable, short, printable id for the calling OS thread (e.g. "T3F2A").
std::string currentThreadIdString();

}  // namespace banksim
