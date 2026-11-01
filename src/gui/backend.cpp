#include "backend.h"

#include <condition_variable>

#include "test_suite.h"

namespace {

// Tiny countdown latch (std::latch needs C++20).
struct Latch {
    explicit Latch(int n) : remaining(n) {}
    void arrive() {
        std::lock_guard<std::mutex> lk(m);
        --remaining;
        cv.notify_all();  // notify under the lock: the waiter may destroy the latch right after
    }
    void wait() {
        std::unique_lock<std::mutex> lk(m);
        cv.wait(lk, [&] { return remaining <= 0; });
    }
    std::mutex m;
    std::condition_variable cv;
    int remaining;
};

}  // namespace

Backend::Backend(QObject* parent) : QObject(parent), bank_(log_), engine_(bank_, log_) {
    qRegisterMetaType<std::shared_ptr<banksim::LabReport>>("std::shared_ptr<banksim::LabReport>");
    qRegisterMetaType<std::shared_ptr<banksim::StressReport>>("std::shared_ptr<banksim::StressReport>");

    bank_.loadSampleData();
    engine_.setProcessingDelayMs(250);

    // Worker threads push into these queues; the GUI thread drains them in batches.
    eventSink_ = log_.addSink([this](const banksim::Event& e) {
        {
            std::lock_guard<std::mutex> lk(queueMtx_);
            if (eventQueue_.size() > 200000) eventQueue_.erase(eventQueue_.begin(), eventQueue_.begin() + 100000);
            eventQueue_.push_back(e);
        }
        notifyActivity();
    });
    bank_.setRecordSink([this](const banksim::TxRecord& r) {
        {
            std::lock_guard<std::mutex> lk(queueMtx_);
            if (recordQueue_.size() > 100000) recordQueue_.erase(recordQueue_.begin(), recordQueue_.begin() + 50000);
            recordQueue_.push_back(r);
        }
        notifyActivity();
    });

    auto onError = [this](const std::string& label, const std::string& what) {
        banksim::Event e;
        e.source = "main";
        e.threadLabel = label;
        e.message = "Thread exception handled safely: " + what;
        e.threadState = banksim::ThreadState::Failed;
        e.status = banksim::TxStatus::Error;
        log_.add(std::move(e));
    };
    txThreads_.setErrorHandler(onError);
    jobs_.setErrorHandler(onError);
    labs_.setErrorHandler(onError);
    tests_.setErrorHandler(onError);
}

Backend::~Backend() {
    cancelStress_ = true;
    // Make sure no worker is still using the sinks that capture `this`.
    resetter_.joinAll();
    jobs_.joinAll();
    labs_.joinAll();
    tests_.joinAll();
    txThreads_.joinAll();
    log_.removeSink(eventSink_);
    bank_.setRecordSink(nullptr);
}

QString Backend::modeText() const {
    switch (labRunning_.load()) {
        case 1: return "LAB: Race condition (unsafe, isolated)";
        case 2: return "LAB: Synchronized (mutex)";
        case 3: return "LAB: Deadlock demo";
        case 4: return "LAB: Deadlock prevention";
        default: break;
    }
    if (testsRunning_.load()) return "Running automated tests";
    return "SAFE: per-account shared_mutex";
}

void Backend::notifyActivity() {
    if (!notifyPending_.exchange(true)) emit activity();
}

void Backend::drain(std::vector<banksim::Event>& events, std::vector<banksim::TxRecord>& records) {
    notifyPending_.store(false);  // clear first: anything arriving after the swap re-notifies
    std::lock_guard<std::mutex> lk(queueMtx_);
    events.swap(eventQueue_);
    records.swap(recordQueue_);
    eventQueue_.clear();
    recordQueue_.clear();
}

void Backend::setProcessingDelay(int ms) { engine_.setProcessingDelayMs(ms); }
void Backend::setAtmCashEnabled(bool on) { bank_.setAtmCashEnabled(on); }

// ----------------------------------------------------------------------- transactions

void Backend::executeSingle(banksim::TxRequest req) {
    if (resetting_.load()) return;
    if (txThreads_.activeCount() == 0) txThreads_.resetNumbering();
    txThreads_.spawn([this, req = std::move(req)](const std::string& label) mutable {
        req.threadLabel = label;
        engine_.execute(std::move(req));
    });
}

void Backend::executeConcurrent(std::vector<banksim::TxRequest> reqs, bool deterministicOrder) {
    if (resetting_.load() || reqs.empty()) return;
    jobs_.spawn([this, reqs = std::move(reqs), deterministicOrder](const std::string&) {
        const int n = static_cast<int>(reqs.size());
        if (txThreads_.activeCount() == 0) txThreads_.resetNumbering();
        auto barrier = std::make_shared<banksim::CyclicBarrier>(n);
        auto gate = std::make_shared<banksim::OrderedStart>(n);
        auto latch = std::make_shared<Latch>(n);
        for (int i = 0; i < n; ++i) {
            txThreads_.spawn([this, i, r = reqs[static_cast<std::size_t>(i)], barrier, gate, latch,
                              deterministicOrder](const std::string& label) mutable {
                r.threadLabel = label;
                if (deterministicOrder) {
                    gate->waitTurn(i);  // arrive at the account mutex in list order, still overlapping
                    r.onLockQueued = [gate, i] { gate->signal(i); };
                } else {
                    barrier->arriveAndWait();  // everybody starts at the same instant
                }
                engine_.execute(std::move(r));
                gate->signal(i);
                latch->arrive();
            });
        }
        latch->wait();
        emit busyChanged();
    });
}

void Backend::startStress(banksim::StressConfig cfg) {
    if (resetting_.load() || stressRunning_.exchange(true)) return;
    cancelStress_ = false;
    cfg.cancel = &cancelStress_;
    emit busyChanged();
    jobs_.spawn([this, cfg](const std::string&) {
        auto rep = std::make_shared<banksim::StressReport>(banksim::runStressTest(bank_, engine_, txThreads_, log_, cfg));
        stressRunning_ = false;
        emit stressFinished(rep);
        emit busyChanged();
    });
}

void Backend::resetSimulation() {
    if (resetting_.exchange(true)) return;
    emit busyChanged();
    resetter_.spawn([this](const std::string&) {
        cancelStress_ = true;   // stop a running stress test at its next operation
        jobs_.joinAll();        // finish every batch / stress orchestrator
        labs_.joinAll();
        txThreads_.joinAll();   // and every outstanding customer thread
        cancelStress_ = false;
        stressRunning_ = false;
        bank_.reset();
        log_.clear();
        {
            std::lock_guard<std::mutex> lk(queueMtx_);
            eventQueue_.clear();
            recordQueue_.clear();
        }
        txThreads_.resetNumbering();
        resetting_ = false;
        emit resetFinished();
        emit busyChanged();
    });
}

// ----------------------------------------------------------------------------- labs

bool Backend::beginLab(int kind) {
    int expected = 0;
    if (!labRunning_.compare_exchange_strong(expected, kind)) return false;
    emit labStarted(kind - 1);
    emit busyChanged();
    return true;
}

void Backend::endLab() {
    labRunning_ = 0;
    emit busyChanged();
}

void Backend::runRace(banksim::RaceConfig cfg) {
    if (!beginLab(1)) return;
    labs_.spawn([this, cfg](const std::string&) {
        auto rep = std::make_shared<banksim::LabReport>(banksim::runRaceCondition(&log_, cfg));
        endLab();
        emit labFinished(rep);
    });
}

void Backend::runSync(banksim::SyncConfig cfg) {
    if (!beginLab(2)) return;
    labs_.spawn([this, cfg](const std::string&) {
        auto rep = std::make_shared<banksim::LabReport>(banksim::runSynchronized(&log_, cfg));
        endLab();
        emit labFinished(rep);
    });
}

void Backend::runDeadlock(banksim::DeadlockConfig cfg) {
    if (!beginLab(cfg.ordered ? 4 : 3)) return;
    labs_.spawn([this, cfg](const std::string&) {
        auto rep = std::make_shared<banksim::LabReport>(banksim::runDeadlockDemo(&log_, cfg));
        endLab();
        emit labFinished(rep);
    });
}

void Backend::runTests() {
    if (testsRunning_.exchange(true)) return;
    emit busyChanged();
    tests_.spawn([this](const std::string&) {
        int passed = 0, total = 0;
        banksim::runAllTests([&](const banksim::TestResult& r) {
            ++total;
            if (r.passed) ++passed;
            emit testProgress(r.number, QString::fromStdString(r.name), r.passed, QString::fromStdString(r.detail),
                              static_cast<qlonglong>(r.ms));
        });
        testsRunning_ = false;
        emit testsFinished(passed, total);
        emit busyChanged();
    });
}
