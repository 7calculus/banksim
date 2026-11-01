// bank.h - the central banking system. ATMs, online banking and mobile banking are only
// *access points*; every one of them talks to the single Bank instance below.
#pragma once

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "account.h"
#include "event_log.h"
#include "transaction.h"

namespace banksim {

struct BankStats {
    std::uint64_t total = 0;
    std::uint64_t success = 0;
    std::uint64_t failed = 0;
};

enum class AuthResult { Ok, CustomerNotFound, AccountNotFound, Denied };

class Bank {
public:
    explicit Bank(EventLog& log);

    EventLog& log() { return log_; }

    // ---- setup -------------------------------------------------------------------
    // Wipes everything and installs the demo data (4 customers, 5 accounts, one joint).
    void loadSampleData();
    bool addCustomer(const std::string& id, const std::string& name, std::string* err = nullptr);
    bool addAccount(const std::string& id, const std::string& type, Money balance,
                    const std::vector<std::string>& holderIds, std::string* err = nullptr);
    bool addHolder(const std::string& accountId, const std::string& customerId,
                   unsigned permissions = PermFull, std::string* err = nullptr);
    // Configure a starting balance (also becomes the value restored by reset()).
    bool setStartingBalance(const std::string& accountId, Money balance, std::string* err = nullptr);
    std::string nextAccountId() const;  // "ACC1006", ...

    // Restore initial balances, clear history and statistics, refill ATM cash.
    // Callers must make sure no worker thread is running (see Backend::resetSimulation).
    void reset();

    // ---- lookup (all thread-safe) -------------------------------------------------
    std::shared_ptr<Account> findAccount(const std::string& id) const;
    std::string customerName(const std::string& id) const;
    bool customerExists(const std::string& id) const;
    AuthResult authorize(const std::string& customerId, const std::string& accountId,
                         unsigned needed) const;

    std::vector<CustomerView> customers() const;
    std::vector<AccountView> accounts() const;
    AccountView account(const std::string& id) const;  // id empty in result if unknown
    std::size_t customerCount() const;
    std::size_t accountCount() const;
    Money totalDeposits() const;  // sum of all balances (takes each account's reader lock)

    // ---- transaction bookkeeping ---------------------------------------------------
    std::string nextTxId();                       // unique, thread-safe
    void record(const TxRecord& r);               // history + statistics
    std::vector<TxRecord> history() const;
    std::vector<TxRecord> recentHistory(std::size_t n) const;
    std::size_t historySize() const;
    BankStats stats() const;
    // Called (under the history lock) for each recorded transaction - must be fast.
    void setRecordSink(std::function<void(const TxRecord&)> sink);

    // ---- optional ATM physical cash -------------------------------------------------
    void setAtmCashEnabled(bool on) { atmCashEnabled_.store(on); }
    bool atmCashEnabled() const { return atmCashEnabled_.load(); }
    Money atmCash(Channel c) const;
    void setAtmCash(Channel c, Money amount);
    // Leaf lock. Returns false if a physical ATM lacks the cash. Non-ATM channels: true.
    bool tryDispenseCash(Channel c, Money amount);

    static constexpr Money kDefaultAtmCash = 50000;

private:
    bool addCustomerLocked(const std::string& id, const std::string& name);
    static int atmIndex(Channel c);

    EventLog& log_;

    mutable std::shared_mutex registryMtx_;  // guards the two maps + holders/permissions
    std::unordered_map<std::string, std::shared_ptr<Account>> accounts_;
    std::map<std::string, Customer> customers_;  // ordered for stable GUI display

    std::atomic<std::uint64_t> txCounter_{0};

    mutable std::mutex histMtx_;
    std::vector<TxRecord> history_;
    std::function<void(const TxRecord&)> recordSink_;
    std::atomic<std::uint64_t> total_{0}, success_{0}, failed_{0};

    std::atomic<bool> atmCashEnabled_{false};
    mutable std::mutex atmMtx_;
    Money atmCash_[3] = {kDefaultAtmCash, kDefaultAtmCash, kDefaultAtmCash};
};

}  // namespace banksim
