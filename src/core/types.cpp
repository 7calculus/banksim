#include "types.h"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <functional>
#include <thread>

namespace banksim {

const char* toString(TxType t) {
    switch (t) {
        case TxType::Deposit: return "Deposit";
        case TxType::Withdrawal: return "Withdrawal";
        case TxType::BalanceEnquiry: return "Balance Enquiry";
        case TxType::Transfer: return "Fund Transfer";
    }
    return "?";
}

const char* toString(Channel c) {
    switch (c) {
        case Channel::ATM1: return "ATM 1";
        case Channel::ATM2: return "ATM 2";
        case Channel::ATM3: return "ATM 3";
        case Channel::OnlineBanking: return "Online Banking";
        case Channel::MobileBanking: return "Mobile Banking";
    }
    return "?";
}

const char* toString(TxStatus s) {
    switch (s) {
        case TxStatus::Pending: return "Pending";
        case TxStatus::Success: return "Success";
        case TxStatus::InsufficientFunds: return "Insufficient funds";
        case TxStatus::InvalidAmount: return "Invalid amount";
        case TxStatus::Unauthorized: return "Unauthorized";
        case TxStatus::SameAccount: return "Same account";
        case TxStatus::AccountNotFound: return "Account not found";
        case TxStatus::CustomerNotFound: return "Customer not found";
        case TxStatus::AtmCashInsufficient: return "ATM cash insufficient";
        case TxStatus::Error: return "Error";
    }
    return "?";
}

const char* toString(ThreadState s) {
    switch (s) {
        case ThreadState::Created: return "Created";
        case ThreadState::Executing: return "Executing";
        case ThreadState::Waiting: return "Waiting";
        case ThreadState::Blocked: return "Blocked";
        case ThreadState::Completed: return "Completed";
        case ThreadState::Failed: return "Failed";
    }
    return "?";
}

const char* toString(LockState s) {
    switch (s) {
        case LockState::None: return "-";
        case LockState::Waiting: return "Waiting";
        case LockState::Acquired: return "Acquired";
        case LockState::Released: return "Released";
        case LockState::TimedOut: return "Timed out";
    }
    return "?";
}

const std::vector<Channel>& allChannels() {
    static const std::vector<Channel> v = {Channel::ATM1, Channel::ATM2, Channel::ATM3,
                                           Channel::OnlineBanking, Channel::MobileBanking};
    return v;
}

const std::vector<TxType>& allTxTypes() {
    static const std::vector<TxType> v = {TxType::Deposit, TxType::Withdrawal,
                                          TxType::BalanceEnquiry, TxType::Transfer};
    return v;
}

std::string formatMoney(Money m) {
    const bool neg = m < 0;
    std::string digits = std::to_string(neg ? -m : m);
    std::string grouped;
    int count = 0;
    for (auto it = digits.rbegin(); it != digits.rend(); ++it) {
        if (count == 3) {
            grouped.insert(grouped.begin(), ',');
            count = 0;
        }
        grouped.insert(grouped.begin(), *it);
        ++count;
    }
    // U+20B9 INDIAN RUPEE SIGN, spelled as raw UTF-8 bytes so it is independent
    // of the compiler's source character set.
    std::string out;
    if (neg) out += '-';
    out += "\xE2\x82\xB9";
    out += grouped;
    return out;
}

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string formatClock(std::int64_t epochMs) {
    std::time_t secs = static_cast<std::time_t>(epochMs / 1000);
    int ms = static_cast<int>(epochMs % 1000);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &secs);
#else
    localtime_r(&secs, &tmv);
#endif
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ms);
    return buf;
}

std::string currentThreadIdString() {
    const std::size_t h = std::hash<std::thread::id>{}(std::this_thread::get_id());
    char buf[24];
    std::snprintf(buf, sizeof buf, "T%05zX", static_cast<std::size_t>(h & 0xFFFFF));
    return buf;
}

}  // namespace banksim
