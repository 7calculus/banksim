// account.h / customer.h - the shared state the whole simulator fights over.
#pragma once

#include <atomic>
#include <map>
#include <memory>
#include <shared_mutex>
#include <string>
#include <vector>

#include "types.h"

namespace banksim {

// A bank account. Every account owns its OWN reader/writer mutex - there is no global
// bank lock - so transactions on different accounts run fully in parallel.
//
//   * deposits / withdrawals / transfers  -> std::unique_lock<std::shared_mutex>  (writer)
//   * balance enquiries                   -> std::shared_lock<std::shared_mutex>  (reader)
class Account {
public:
    Account(std::string id_, std::string number_, std::string type_, Money initial)
        : id(std::move(id_)),
          number(std::move(number_)),
          type(std::move(type_)),
          initialBalance(initial),
          balance(initial),
          published(initial) {}

    const std::string id;      // e.g. "ACC1001"
    const std::string number;  // e.g. "4410001001"
    const std::string type;    // "Savings" / "Current"

    // Guarded by Bank's registry mutex:
    std::vector<std::string> holders;           // customer ids
    std::map<std::string, unsigned> permissions;  // customer id -> Permission mask
    Money initialBalance;                       // restored by Bank::reset()

    // Guarded by `mtx`:
    mutable std::shared_mutex mtx;
    Money balance;

    // Lock-free mirror of `balance`, refreshed by setBalance() inside the critical section.
    // The GUI reads this so that repainting never has to wait for an account mutex that a
    // slow transaction is holding. The authoritative value is always `balance` under `mtx`.
    std::atomic<Money> published;

    // Caller must hold `mtx` exclusively.
    void setBalance(Money m) {
        balance = m;
        published.store(m, std::memory_order_release);
    }
    Money publishedBalance() const { return published.load(std::memory_order_acquire); }
};

struct Customer {
    std::string id;    // e.g. "CUST-A"
    std::string name;  // e.g. "Customer A"
    std::vector<std::string> accountIds;
};

// Immutable snapshots handed to the GUI / tests.
struct AccountView {
    std::string id, number, type;
    Money balance = 0;
    Money initialBalance = 0;
    std::vector<std::string> holderIds;
    std::vector<std::string> holderNames;
    bool joint = false;
};

struct CustomerView {
    std::string id, name;
    std::vector<std::string> accountIds;
};

}  // namespace banksim
