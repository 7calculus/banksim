#include "transaction_engine.h"

#include <chrono>
#include <thread>

namespace banksim {

namespace {

// Builds and publishes monitor events for one transaction.
class Emitter {
public:
    Emitter(EventLog& log, const TxRequest& req, std::string txId, std::string custName)
        : log_(log), req_(req), txId_(std::move(txId)), custName_(std::move(custName)) {}

    // `detail` events (lock-level chatter) are skipped when the log is not verbose.
    void operator()(const std::string& account, ThreadState ts, LockState ls, TxStatus st,
                    const std::string& msg, bool detail = false) const {
        if (detail && !log_.verbose()) return;
        Event e;
        e.source = req_.source;
        e.threadLabel = req_.threadLabel.empty() ? "Main-Thread" : req_.threadLabel;
        e.threadId = currentThreadIdString();
        e.customerId = req_.customerId;
        e.customerName = custName_;
        e.channel = toString(req_.channel);
        e.txType = toString(req_.type);
        e.accountId = account;
        e.txId = txId_;
        e.message = msg;
        e.threadState = ts;
        e.lockState = ls;
        e.status = st;
        log_.add(std::move(e));
    }

private:
    EventLog& log_;
    const TxRequest& req_;
    std::string txId_;
    std::string custName_;
};

// RAII ownership of one account mutex, with monitor events for "waiting / acquired /
// released". The lock is released in the destructor (also on exceptions).
class AccountGuard {
public:
    AccountGuard(const Emitter& em, Account& acc, bool shared, std::function<void()>* hook)
        : em_(em), acc_(acc), shared_(shared) {
        const char* kind = shared ? "shared/reader" : "exclusive/writer";
        const bool got = shared ? acc.mtx.try_lock_shared() : acc.mtx.try_lock();
        if (got) {
            locked_ = true;
            em_(acc.id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                std::string("Mutex acquired on ") + acc.id + " (" + kind + ")", true);
            fireHook(hook);
        } else {
            // Someone else holds the mutex: announce that this thread is now blocked on it.
            em_(acc.id, ThreadState::Waiting, LockState::Waiting, TxStatus::Pending,
                std::string("Waiting for mutex on ") + acc.id + " (" + kind + ")");
            fireHook(hook);
            if (shared) acc.mtx.lock_shared(); else acc.mtx.lock();
            locked_ = true;
            em_(acc.id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                std::string("Mutex acquired on ") + acc.id + " (" + kind + ")");
        }
    }

    ~AccountGuard() { release(); }
    AccountGuard(const AccountGuard&) = delete;
    AccountGuard& operator=(const AccountGuard&) = delete;

    void release() {
        if (!locked_) return;
        locked_ = false;
        // Logged *before* unlocking so the monitor order is always: released -> next acquired.
        em_(acc_.id, ThreadState::Executing, LockState::Released, TxStatus::Pending,
            std::string("Mutex released on ") + acc_.id);
        if (shared_) acc_.mtx.unlock_shared(); else acc_.mtx.unlock();
    }

private:
    static void fireHook(std::function<void()>* hook) {
        if (hook && *hook) {
            auto h = std::move(*hook);
            *hook = nullptr;
            h();
        }
    }
    const Emitter& em_;
    Account& acc_;
    bool shared_;
    bool locked_ = false;
};

// Tracks "how many threads are inside a critical section right now" and the maximum.
struct ConcurrencyScope {
    ConcurrencyScope(std::atomic<int>& now, std::atomic<int>& max) : now_(now) {
        const int cur = ++now_;
        int prev = max.load();
        while (cur > prev && !max.compare_exchange_weak(prev, cur)) {
        }
    }
    ~ConcurrencyScope() { --now_; }
    std::atomic<int>& now_;
};

std::string describe(const TxRequest& r) {
    std::string s = std::string(toString(r.type)) + " initiated";
    if (r.type == TxType::Transfer)
        s += " (" + formatMoney(r.amount) + " " + r.accountId + " -> " + r.destAccountId + ")";
    else if (r.type != TxType::BalanceEnquiry)
        s += " (" + formatMoney(r.amount) + ")";
    return s;
}

}  // namespace

// ---------------------------------------------------------------------------------

TransactionEngine::TransactionEngine(Bank& bank, EventLog& log) : bank_(bank), log_(log) {}

void TransactionEngine::resetDiagnostics() {
    critMax_ = 0;
    readMax_ = 0;
}

void TransactionEngine::simulateProcessing() const {
    const int d = delayMs_.load();
    if (d > 0) std::this_thread::sleep_for(std::chrono::milliseconds(d));
}

TxResult TransactionEngine::execute(TxRequest req) {
    TxResult res;
    res.txId = bank_.nextTxId();
    res.threadLabel = req.threadLabel;
    const std::string custName = bank_.customerName(req.customerId);
    const std::string threadId = currentThreadIdString();
    Emitter em(log_, req, res.txId, custName);
    bool finalEmitted = false;
    std::string logAccount = req.accountId;

    // Publishes the outcome (once) as the thread's final monitor event.
    auto finish = [&](TxStatus st, const std::string& msg) {
        res.status = st;
        res.message = msg;
        finalEmitted = true;
        em(logAccount, isSuccess(st) ? ThreadState::Completed : ThreadState::Failed, LockState::None, st, msg);
    };

    try {
        em(req.accountId, ThreadState::Executing, LockState::None, TxStatus::Pending, describe(req));

        // ------------------------------------------------------------- validation
        if (!bank_.customerExists(req.customerId)) {
            finish(TxStatus::CustomerNotFound, "Unknown customer " + req.customerId);
        } else if (req.type != TxType::BalanceEnquiry && (req.amount <= 0 || req.amount > kMaxAmount)) {
            finish(TxStatus::InvalidAmount, "Invalid amount: amount must be positive (got " + formatMoney(req.amount) + ")");
        } else if (req.type == TxType::Transfer && req.accountId == req.destAccountId) {
            finish(TxStatus::SameAccount, "A transfer needs two different accounts");
        } else {
            unsigned need = PermView;
            switch (req.type) {
                case TxType::Deposit: need = PermDeposit; break;
                case TxType::Withdrawal: need = PermWithdraw; break;
                case TxType::Transfer: need = PermTransfer; break;
                case TxType::BalanceEnquiry: need = PermView; break;
            }
            switch (bank_.authorize(req.customerId, req.accountId, need)) {
                case AuthResult::AccountNotFound:
                    finish(TxStatus::AccountNotFound, "Account " + req.accountId + " does not exist");
                    break;
                case AuthResult::Denied:
                    finish(TxStatus::Unauthorized, custName + " is not authorized for " + toString(req.type) +
                                                       " on " + req.accountId);
                    break;
                case AuthResult::CustomerNotFound:
                    finish(TxStatus::CustomerNotFound, "Unknown customer " + req.customerId);
                    break;
                case AuthResult::Ok:
                    break;
            }
        }

        std::shared_ptr<Account> acc, dst;
        if (!finalEmitted) {
            acc = bank_.findAccount(req.accountId);
            if (req.type == TxType::Transfer) {
                dst = bank_.findAccount(req.destAccountId);
                if (!dst) finish(TxStatus::AccountNotFound, "Destination account " + req.destAccountId + " does not exist");
            }
            if (!acc && !finalEmitted) finish(TxStatus::AccountNotFound, "Account " + req.accountId + " does not exist");
        }

        // ------------------------------------------------------------- execution
        if (!finalEmitted) {
            switch (req.type) {
                case TxType::Deposit: {
                    AccountGuard g(em, *acc, false, &req.onLockQueued);
                    ConcurrencyScope cs(critNow_, critMax_);
                    em(acc->id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                       "Critical section: current balance " + formatMoney(acc->balance), true);
                    const Money bal = acc->balance;  // read the LATEST balance under the lock
                    simulateProcessing();
                    acc->setBalance(bal + req.amount);
                    res.balanceAfter = acc->balance;
                    finish(TxStatus::Success, "Deposit successful, new balance " + formatMoney(res.balanceAfter));
                    break;
                }
                case TxType::Withdrawal: {
                    AccountGuard g(em, *acc, false, &req.onLockQueued);
                    ConcurrencyScope cs(critNow_, critMax_);
                    const Money bal = acc->balance;
                    em(acc->id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                       "Critical section: read balance " + formatMoney(bal), true);
                    simulateProcessing();
                    res.balanceAfter = bal;
                    if (bal < req.amount) {
                        finish(TxStatus::InsufficientFunds,
                               "Insufficient balance: requested " + formatMoney(req.amount) + ", available " + formatMoney(bal));
                    } else if (!bank_.tryDispenseCash(req.channel, req.amount)) {
                        finish(TxStatus::AtmCashInsufficient,
                               std::string(toString(req.channel)) + " does not have enough physical cash");
                    } else {
                        acc->setBalance(bal - req.amount);
                        res.balanceAfter = acc->balance;
                        finish(TxStatus::Success, "Withdrawal successful, new balance " + formatMoney(res.balanceAfter));
                    }
                    break;
                }
                case TxType::BalanceEnquiry: {
                    AccountGuard g(em, *acc, true, &req.onLockQueued);  // shared: readers run in parallel
                    ConcurrencyScope cs(critNow_, critMax_);
                    ConcurrencyScope rs(readNow_, readMax_);
                    const Money bal = acc->balance;
                    simulateProcessing();
                    res.balanceAfter = bal;
                    finish(TxStatus::Success, "Balance enquiry: " + formatMoney(bal));
                    break;
                }
                case TxType::Transfer: {
                    // Deadlock prevention: ALWAYS lock the lower account id first, whatever the
                    // direction of the transfer, so two opposite transfers can never wait on
                    // each other in a cycle.
                    Account* first = acc.get();
                    Account* second = dst.get();
                    if (first->id > second->id) std::swap(first, second);
                    AccountGuard g1(em, *first, false, &req.onLockQueued);
                    AccountGuard g2(em, *second, false, nullptr);
                    ConcurrencyScope cs(critNow_, critMax_);
                    const Money bal = acc->balance;
                    em(acc->id, ThreadState::Executing, LockState::Acquired, TxStatus::Pending,
                       "Critical section: both accounts locked, source balance " + formatMoney(bal), true);
                    simulateProcessing();
                    res.balanceAfter = bal;
                    res.destBalanceAfter = dst->balance;
                    if (bal < req.amount) {
                        // Nothing has been modified yet - a failed transfer leaves both accounts untouched.
                        finish(TxStatus::InsufficientFunds,
                               "Transfer rejected: requested " + formatMoney(req.amount) + ", available " + formatMoney(bal));
                    } else {
                        acc->setBalance(bal - req.amount);   // debit and credit happen while BOTH
                        dst->setBalance(dst->balance + req.amount);  // locks are held -> atomic
                        res.balanceAfter = acc->balance;
                        res.destBalanceAfter = dst->balance;
                        finish(TxStatus::Success, "Transfer successful: " + formatMoney(req.amount) + " " + acc->id +
                                                      " -> " + dst->id + ", new balance " + formatMoney(res.balanceAfter));
                    }
                    break;
                }
            }
        }
    } catch (const std::exception& e) {
        if (!finalEmitted) finish(TxStatus::Error, std::string("Exception: ") + e.what());
    } catch (...) {
        if (!finalEmitted) finish(TxStatus::Error, "Unknown exception");
    }

    TxRecord rec;
    rec.txId = res.txId;
    rec.timestampMs = nowMs();
    rec.type = req.type;
    rec.customerId = req.customerId;
    rec.customerName = custName;
    rec.accountId = req.accountId;
    rec.destAccountId = req.destAccountId;
    rec.channel = req.channel;
    rec.threadLabel = req.threadLabel;
    rec.threadId = threadId;
    rec.amount = req.amount;
    rec.status = res.status;
    rec.balanceAfter = res.balanceAfter;
    rec.message = res.message;
    rec.source = req.source;
    bank_.record(rec);
    return res;
}

}  // namespace banksim
