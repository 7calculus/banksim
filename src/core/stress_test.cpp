#include "stress_test.h"

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <random>
#include <set>

namespace banksim {

StressReport runStressTest(Bank& bank, TransactionEngine& engine, ThreadManager& tm, EventLog& log,
                           const StressConfig& cfg) {
    StressReport rep;
    rep.threads = cfg.threads;

    const std::vector<AccountView> accts = bank.accounts();
    std::vector<std::pair<std::string, std::string>> pairs;  // (customer, account) the customer may use
    for (const auto& a : accts)
        for (const auto& h : a.holderIds) pairs.emplace_back(h, a.id);
    if (pairs.empty() || cfg.threads <= 0 || cfg.opsPerThread <= 0) return rep;

    rep.initialTotal = bank.totalDeposits();
    const int oldDelay = engine.processingDelayMs();
    const bool oldVerbose = log.verbose();
    engine.setProcessingDelayMs(cfg.processingMs);
    log.setVerbose(cfg.verboseEvents);
    engine.resetDiagnostics();

    std::atomic<std::uint64_t> ops{0}, ok{0}, bad{0};
    std::atomic<Money> deposited{0}, withdrawn{0};
    std::vector<std::vector<std::string>> ids(static_cast<std::size_t>(cfg.threads));

    std::mutex doneMtx;
    std::condition_variable doneCv;
    int remaining = cfg.threads;

    const auto t0 = std::chrono::steady_clock::now();
    for (int t = 0; t < cfg.threads; ++t) {
        tm.spawn([&, t](const std::string& label) {
            std::mt19937 rng(cfg.seed + static_cast<unsigned>(t) * 7919u);
            auto& myIds = ids[static_cast<std::size_t>(t)];
            myIds.reserve(static_cast<std::size_t>(cfg.opsPerThread));
            for (int k = 0; k < cfg.opsPerThread; ++k) {
                if (cfg.cancel && cfg.cancel->load()) break;
                const auto& pr = pairs[rng() % pairs.size()];
                TxRequest r;
                r.customerId = pr.first;
                r.accountId = pr.second;
                r.channel = allChannels()[rng() % allChannels().size()];
                r.amount = 10 + static_cast<Money>(rng() % 1991);
                r.threadLabel = label;
                const unsigned roll = rng() % 100;
                if (roll < 30) {
                    r.type = TxType::Deposit;
                } else if (roll < 60) {
                    r.type = TxType::Withdrawal;
                } else if (roll < 80) {
                    r.type = TxType::BalanceEnquiry;
                } else {
                    r.type = TxType::Transfer;
                    do {
                        r.destAccountId = accts[rng() % accts.size()].id;
                    } while (r.destAccountId == r.accountId && accts.size() > 1);
                }
                const TxType type = r.type;
                const Money amount = r.amount;
                const TxResult res = engine.execute(std::move(r));
                myIds.push_back(res.txId);
                ++ops;
                if (res.ok()) {
                    ++ok;
                    if (type == TxType::Deposit) deposited += amount;
                    if (type == TxType::Withdrawal) withdrawn += amount;
                } else {
                    ++bad;
                }
            }
            // Notify while holding the lock: the waiting thread owns doneCv and may destroy it
            // the moment it observes remaining == 0.
            std::lock_guard<std::mutex> lk(doneMtx);
            --remaining;
            doneCv.notify_all();
        });
    }
    {
        std::unique_lock<std::mutex> lk(doneMtx);
        doneCv.wait(lk, [&] { return remaining == 0; });
    }
    rep.elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();

    // ---- verification (all workers have finished; every balance is final)
    rep.operations = ops.load();
    rep.succeeded = ok.load();
    rep.failed = bad.load();
    rep.expectedTotal = rep.initialTotal + deposited.load() - withdrawn.load();
    rep.actualTotal = bank.totalDeposits();
    for (const auto& a : bank.accounts())
        if (a.balance < 0) rep.noNegativeBalance = false;
    std::set<std::string> uniq;
    std::size_t count = 0;
    for (const auto& v : ids) {
        count += v.size();
        uniq.insert(v.begin(), v.end());
    }
    rep.uniqueTxIds = uniq.size() == count;
    rep.cancelled = cfg.cancel && cfg.cancel->load();
    rep.maxConcurrentCritical = engine.maxConcurrentCritical();
    rep.consistent = rep.actualTotal == rep.expectedTotal && rep.noNegativeBalance && rep.uniqueTxIds;

    engine.setProcessingDelayMs(oldDelay);
    log.setVerbose(oldVerbose);
    return rep;
}

}  // namespace banksim
