#include "bank.h"

#include <algorithm>
#include <cstdio>

namespace banksim {

Bank::Bank(EventLog& log) : log_(log) {}

// ------------------------------------------------------------------------ setup

bool Bank::addCustomerLocked(const std::string& id, const std::string& name) {
    if (id.empty() || customers_.count(id)) return false;
    Customer c;
    c.id = id;
    c.name = name;
    customers_.emplace(id, std::move(c));
    return true;
}

void Bank::loadSampleData() {
    {
        std::unique_lock<std::shared_mutex> lk(registryMtx_);
        accounts_.clear();
        customers_.clear();
    }
    {
        std::lock_guard<std::mutex> lk(histMtx_);
        history_.clear();
        total_ = success_ = failed_ = 0;
    }
    addCustomer("CUST-A", "Customer A");
    addCustomer("CUST-B", "Customer B");
    addCustomer("CUST-C", "Customer C");
    addCustomer("CUST-D", "Customer D");
    // ACC1001 is the joint account used by the headline demo (Customer A + Customer B).
    addAccount("ACC1001", "Savings", 10000, {"CUST-A", "CUST-B"});
    addAccount("ACC1002", "Savings", 25000, {"CUST-C"});
    addAccount("ACC1003", "Current", 15000, {"CUST-D"});
    addAccount("ACC1004", "Savings", 8000, {"CUST-A"});
    addAccount("ACC1005", "Savings", 5000, {"CUST-B"});
    std::lock_guard<std::mutex> lk(atmMtx_);
    for (auto& c : atmCash_) c = kDefaultAtmCash;
}

bool Bank::addCustomer(const std::string& id, const std::string& name, std::string* err) {
    std::unique_lock<std::shared_mutex> lk(registryMtx_);
    if (id.empty() || name.empty()) {
        if (err) *err = "Customer id and name are required.";
        return false;
    }
    if (!addCustomerLocked(id, name)) {
        if (err) *err = "A customer with id " + id + " already exists.";
        return false;
    }
    return true;
}

std::string Bank::nextAccountId() const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    int maxNum = 1000;
    for (const auto& kv : accounts_) {
        if (kv.first.rfind("ACC", 0) == 0) {
            try {
                maxNum = std::max(maxNum, std::stoi(kv.first.substr(3)));
            } catch (...) {
            }
        }
    }
    return "ACC" + std::to_string(maxNum + 1);
}

bool Bank::addAccount(const std::string& idIn, const std::string& type, Money balance,
                      const std::vector<std::string>& holderIds, std::string* err) {
    std::string id = idIn.empty() ? nextAccountId() : idIn;
    std::unique_lock<std::shared_mutex> lk(registryMtx_);
    if (accounts_.count(id)) {
        if (err) *err = "Account " + id + " already exists.";
        return false;
    }
    if (holderIds.empty()) {
        if (err) *err = "An account needs at least one holder.";
        return false;
    }
    if (balance < 0) {
        if (err) *err = "Starting balance cannot be negative.";
        return false;
    }
    for (const auto& h : holderIds) {
        if (!customers_.count(h)) {
            if (err) *err = "Unknown customer " + h + ".";
            return false;
        }
    }
    std::string digits;
    for (char ch : id)
        if (ch >= '0' && ch <= '9') digits += ch;
    auto acc = std::make_shared<Account>(id, "441000" + digits, type, balance);
    for (const auto& h : holderIds) {
        if (std::find(acc->holders.begin(), acc->holders.end(), h) != acc->holders.end()) continue;
        acc->holders.push_back(h);
        acc->permissions[h] = PermFull;
        customers_[h].accountIds.push_back(id);
    }
    accounts_.emplace(id, std::move(acc));
    return true;
}

bool Bank::addHolder(const std::string& accountId, const std::string& customerId,
                     unsigned permissions, std::string* err) {
    std::unique_lock<std::shared_mutex> lk(registryMtx_);
    auto a = accounts_.find(accountId);
    auto c = customers_.find(customerId);
    if (a == accounts_.end() || c == customers_.end()) {
        if (err) *err = "Unknown account or customer.";
        return false;
    }
    auto& acc = *a->second;
    if (std::find(acc.holders.begin(), acc.holders.end(), customerId) != acc.holders.end()) {
        if (err) *err = customerId + " is already a holder of " + accountId + ".";
        return false;
    }
    acc.holders.push_back(customerId);
    acc.permissions[customerId] = permissions;
    c->second.accountIds.push_back(accountId);
    return true;
}

bool Bank::setStartingBalance(const std::string& accountId, Money balance, std::string* err) {
    if (balance < 0) {
        if (err) *err = "Balance cannot be negative.";
        return false;
    }
    std::unique_lock<std::shared_mutex> lk(registryMtx_);
    auto a = accounts_.find(accountId);
    if (a == accounts_.end()) {
        if (err) *err = "Unknown account.";
        return false;
    }
    a->second->initialBalance = balance;
    std::unique_lock<std::shared_mutex> al(a->second->mtx);  // registry -> account order
    a->second->setBalance(balance);
    return true;
}

void Bank::reset() {
    {
        std::unique_lock<std::shared_mutex> lk(registryMtx_);
        for (auto& kv : accounts_) {
            std::unique_lock<std::shared_mutex> al(kv.second->mtx);
            kv.second->setBalance(kv.second->initialBalance);
        }
    }
    {
        std::lock_guard<std::mutex> lk(histMtx_);
        history_.clear();
        total_ = success_ = failed_ = 0;
    }
    std::lock_guard<std::mutex> lk(atmMtx_);
    for (auto& c : atmCash_) c = kDefaultAtmCash;
}

// ----------------------------------------------------------------------- lookup

std::shared_ptr<Account> Bank::findAccount(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    auto it = accounts_.find(id);
    return it == accounts_.end() ? nullptr : it->second;
}

std::string Bank::customerName(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    auto it = customers_.find(id);
    return it == customers_.end() ? std::string() : it->second.name;
}

bool Bank::customerExists(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    return customers_.count(id) != 0;
}

AuthResult Bank::authorize(const std::string& customerId, const std::string& accountId,
                           unsigned needed) const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    if (!customers_.count(customerId)) return AuthResult::CustomerNotFound;
    auto a = accounts_.find(accountId);
    if (a == accounts_.end()) return AuthResult::AccountNotFound;
    auto p = a->second->permissions.find(customerId);
    if (p == a->second->permissions.end()) return AuthResult::Denied;
    return (p->second & needed) == needed ? AuthResult::Ok : AuthResult::Denied;
}

std::vector<CustomerView> Bank::customers() const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    std::vector<CustomerView> out;
    for (const auto& kv : customers_) out.push_back({kv.second.id, kv.second.name, kv.second.accountIds});
    return out;
}

static AccountView makeView(const Account& a, const std::map<std::string, Customer>& customers) {
    AccountView v;
    v.id = a.id;
    v.number = a.number;
    v.type = a.type;
    v.initialBalance = a.initialBalance;
    v.holderIds = a.holders;
    for (const auto& h : a.holders) {
        auto it = customers.find(h);
        v.holderNames.push_back(it == customers.end() ? h : it->second.name);
    }
    v.joint = a.holders.size() > 1;
    v.balance = a.publishedBalance();  // lock-free: never blocks behind a running transaction
    return v;
}

std::vector<AccountView> Bank::accounts() const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    std::vector<AccountView> out;
    out.reserve(accounts_.size());
    for (const auto& kv : accounts_) out.push_back(makeView(*kv.second, customers_));
    std::sort(out.begin(), out.end(), [](const AccountView& a, const AccountView& b) { return a.id < b.id; });
    return out;
}

AccountView Bank::account(const std::string& id) const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    auto it = accounts_.find(id);
    if (it == accounts_.end()) return AccountView{};
    return makeView(*it->second, customers_);
}

std::size_t Bank::customerCount() const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    return customers_.size();
}

std::size_t Bank::accountCount() const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    return accounts_.size();
}

Money Bank::totalDeposits() const {
    std::shared_lock<std::shared_mutex> lk(registryMtx_);
    Money sum = 0;
    for (const auto& kv : accounts_) sum += kv.second->publishedBalance();
    return sum;
}

// ---------------------------------------------------------------- bookkeeping

std::string Bank::nextTxId() {
    const std::uint64_t n = ++txCounter_;
    char buf[24];
    std::snprintf(buf, sizeof buf, "TXN%06llu", static_cast<unsigned long long>(n));
    return buf;
}

void Bank::record(const TxRecord& r) {
    std::lock_guard<std::mutex> lk(histMtx_);
    history_.push_back(r);
    ++total_;
    if (r.status == TxStatus::Success) ++success_; else ++failed_;
    if (recordSink_) recordSink_(r);
}

std::vector<TxRecord> Bank::history() const {
    std::lock_guard<std::mutex> lk(histMtx_);
    return history_;
}

std::vector<TxRecord> Bank::recentHistory(std::size_t n) const {
    std::lock_guard<std::mutex> lk(histMtx_);
    const std::size_t start = history_.size() > n ? history_.size() - n : 0;
    return std::vector<TxRecord>(history_.begin() + static_cast<std::ptrdiff_t>(start), history_.end());
}

std::size_t Bank::historySize() const {
    std::lock_guard<std::mutex> lk(histMtx_);
    return history_.size();
}

BankStats Bank::stats() const {
    BankStats s;
    s.total = total_.load();
    s.success = success_.load();
    s.failed = failed_.load();
    return s;
}

void Bank::setRecordSink(std::function<void(const TxRecord&)> sink) {
    std::lock_guard<std::mutex> lk(histMtx_);
    recordSink_ = std::move(sink);
}

// ------------------------------------------------------------------- ATM cash

int Bank::atmIndex(Channel c) {
    switch (c) {
        case Channel::ATM1: return 0;
        case Channel::ATM2: return 1;
        case Channel::ATM3: return 2;
        default: return -1;
    }
}

Money Bank::atmCash(Channel c) const {
    const int i = atmIndex(c);
    if (i < 0) return 0;
    std::lock_guard<std::mutex> lk(atmMtx_);
    return atmCash_[i];
}

void Bank::setAtmCash(Channel c, Money amount) {
    const int i = atmIndex(c);
    if (i < 0) return;
    std::lock_guard<std::mutex> lk(atmMtx_);
    atmCash_[i] = amount;
}

bool Bank::tryDispenseCash(Channel c, Money amount) {
    const int i = atmIndex(c);
    if (i < 0 || !atmCashEnabled_.load()) return true;
    std::lock_guard<std::mutex> lk(atmMtx_);
    if (atmCash_[i] < amount) return false;
    atmCash_[i] -= amount;
    return true;
}

}  // namespace banksim
