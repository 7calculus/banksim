// backend.h - owns the banking engine and every worker thread, and is the ONLY place where
// worker threads talk to the GUI. Workers never touch widgets: results travel as queued Qt
// signals (or as batched queues drained on the GUI thread), so the GUI never blocks.
#pragma once

#include <QObject>
#include <QString>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

#include "bank.h"
#include "concurrency_lab.h"
#include "deadlock_simulator.h"
#include "event_log.h"
#include "stress_test.h"
#include "thread_manager.h"
#include "transaction_engine.h"

Q_DECLARE_METATYPE(std::shared_ptr<banksim::LabReport>)
Q_DECLARE_METATYPE(std::shared_ptr<banksim::StressReport>)

class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(QObject* parent = nullptr);
    ~Backend() override;  // cancels and joins every worker thread

    banksim::Bank& bank() { return bank_; }
    banksim::TransactionEngine& engine() { return engine_; }
    banksim::EventLog& log() { return log_; }

    int activeThreads() const { return txThreads_.activeCount(); }
    bool isResetting() const { return resetting_.load(); }
    bool labRunning() const { return labRunning_.load() != 0; }
    bool testsRunning() const { return testsRunning_.load(); }
    bool stressRunning() const { return stressRunning_.load(); }
    QString modeText() const;

    // -- settings
    void setProcessingDelay(int ms);
    void setAtmCashEnabled(bool on);

    // -- main-bank actions (all run on std::thread workers)
    void executeSingle(banksim::TxRequest req);
    void executeConcurrent(std::vector<banksim::TxRequest> reqs, bool deterministicOrder);
    void startStress(banksim::StressConfig cfg);
    void cancelStress() { cancelStress_.store(true); }
    void resetSimulation();

    // -- isolated concurrency lab (own accounts, own engine)
    void runRace(banksim::RaceConfig cfg);
    void runSync(banksim::SyncConfig cfg);
    void runDeadlock(banksim::DeadlockConfig cfg);  // cfg.ordered selects demo vs prevention
    void runTests();

    // Moves everything queued by the worker threads since the last call into the vectors.
    void drain(std::vector<banksim::Event>& events, std::vector<banksim::TxRecord>& records);

signals:
    void activity();  // events/records are waiting - call drain() (coalesced)
    void labStarted(int kind);  // banksim::LabKind as int
    void labFinished(std::shared_ptr<banksim::LabReport> report);
    void stressFinished(std::shared_ptr<banksim::StressReport> report);
    void testProgress(int number, QString name, bool passed, QString detail, qlonglong ms);
    void testsFinished(int passed, int total);
    void resetFinished();
    void busyChanged();

private:
    void notifyActivity();
    bool beginLab(int kind);
    void endLab();

    // Declaration order matters: members below are destroyed first, so every thread manager
    // has already joined its threads before the log / bank / engine they use disappear.
    banksim::EventLog log_;
    banksim::Bank bank_;
    banksim::TransactionEngine engine_;

    std::mutex queueMtx_;
    std::vector<banksim::Event> eventQueue_;
    std::vector<banksim::TxRecord> recordQueue_;
    std::atomic<bool> notifyPending_{false};
    int eventSink_ = 0;

    std::atomic<bool> cancelStress_{false};
    std::atomic<bool> stressRunning_{false};
    std::atomic<bool> testsRunning_{false};
    std::atomic<bool> resetting_{false};
    std::atomic<int> labRunning_{0};   // 0 = none, otherwise 1 + LabKind
    std::atomic<int> lastLab_{0};

    banksim::ThreadManager txThreads_{"Thread"};   // simulated customer transactions
    banksim::ThreadManager jobs_{"Job"};           // batch / stress orchestration
    banksim::ThreadManager labs_{"Lab"};
    banksim::ThreadManager tests_{"Test"};
    banksim::ThreadManager resetter_{"Reset"};
};
