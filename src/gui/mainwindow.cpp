#include "mainwindow.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QDir>
#include <QEventLoop>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <functional>

#include "accounts_page.h"
#include "dashboard_page.h"
#include "lab_page.h"
#include "monitor_page.h"
#include "simulator_page.h"
#include "test_suite.h"
#include "widgets.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("BankSim - Multithreaded Banking & ATM Simulator");
    resize(1360, 860);
    setMinimumSize(1120, 720);

    auto* central = new QWidget;
    auto* h = new QHBoxLayout(central);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // =============================================================== sidebar
    auto* side = new QFrame;
    side->setObjectName("sidebar");
    side->setFixedWidth(236);
    auto* sl = new QVBoxLayout(side);
    sl->setContentsMargins(16, 22, 16, 16);
    sl->setSpacing(4);
    sl->addWidget(makeLabel("BankSim", "brandTitle"));
    sl->addWidget(makeLabel("Multithreaded Banking &\nATM Simulator", "brandSub"));
    sl->addSpacing(22);

    navGroup_ = new QButtonGroup(this);
    navGroup_->setExclusive(true);
    const char* names[5] = {"Dashboard", "Customers && Accounts", "Banking Simulator", "Concurrency Lab", "Live Thread Monitor"};
    for (int i = 0; i < 5; ++i) {
        auto* b = new QPushButton(names[i]);
        b->setObjectName("navButton");
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        navGroup_->addButton(b, i);
        sl->addWidget(b);
    }
    sl->addStretch(1);
    sideMode_ = makeLabel(QString(), "sideMode");
    sideMode_->setWordWrap(true);
    sl->addWidget(sideMode_);
    sl->addWidget(makeLabel("One central Bank Engine.\nATMs, online and mobile\nbanking are only access points.", "sideNote"));
    sl->addSpacing(8);
    sideReset_ = new QPushButton("Reset Simulation");
    sideReset_->setObjectName("sideReset");
    sideReset_->setCursor(Qt::PointingHandCursor);
    sl->addWidget(sideReset_);
    h->addWidget(side);

    // =============================================================== pages
    stack_ = new QStackedWidget;
    dashboard_ = new DashboardPage(backend_);
    accounts_ = new AccountsPage(backend_);
    simulator_ = new SimulatorPage(backend_);
    lab_ = new LabPage(backend_);
    monitor_ = new MonitorPage(backend_);
    stack_->addWidget(dashboard_);
    stack_->addWidget(accounts_);
    stack_->addWidget(simulator_);
    stack_->addWidget(lab_);
    stack_->addWidget(monitor_);
    h->addWidget(stack_, 1);
    setCentralWidget(central);

    statusLabel_ = new QLabel;
    statusBar()->addPermanentWidget(statusLabel_, 1);
    statusBar()->setSizeGripEnabled(false);

    // =============================================================== wiring
    connect(navGroup_, &QButtonGroup::idClicked, this, &MainWindow::goTo);
    connect(sideReset_, &QPushButton::clicked, this, [this] { backend_.resetSimulation(); });

    // Worker threads -> GUI thread: always queued.
    const auto q = Qt::QueuedConnection;
    connect(&backend_, &Backend::activity, this, &MainWindow::onActivity, q);
    connect(&backend_, &Backend::labStarted, lab_, &LabPage::onLabStarted, q);
    connect(&backend_, &Backend::labFinished, lab_, &LabPage::onLabFinished, q);
    connect(&backend_, &Backend::testProgress, lab_, &LabPage::onTestProgress, q);
    connect(&backend_, &Backend::testsFinished, lab_, &LabPage::onTestsFinished, q);
    connect(&backend_, &Backend::stressFinished, simulator_, &SimulatorPage::onStressFinished, q);
    connect(&backend_, &Backend::resetFinished, this, &MainWindow::onResetFinished, q);
    connect(&backend_, &Backend::busyChanged, this, [this] {
        lab_->refreshButtons();
        sideReset_->setEnabled(!backend_.isResetting());
        sideReset_->setText(backend_.isResetting() ? "Resetting..." : "Reset Simulation");
        refreshCurrentPage();
        refreshStatus();
    }, q);

    connect(dashboard_, &DashboardPage::runDemoRequested, this, [this] {
        goTo(2);
        simulator_->runJointDemo();
    });
    connect(dashboard_, &DashboardPage::openLabRequested, this, [this] { goTo(3); });
    connect(dashboard_, &DashboardPage::runTestsRequested, this, [this] {
        goTo(3);
        lab_->runTests();
    });
    connect(accounts_, &AccountsPage::structureChanged, simulator_, &SimulatorPage::reloadChoices);

    timer_ = new QTimer(this);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &MainWindow::onTick);
    timer_->start();

    navGroup_->button(0)->setChecked(true);
    refreshStatus();
}

MainWindow::~MainWindow() = default;  // Backend's destructor cancels and joins every worker thread

void MainWindow::closeEvent(QCloseEvent* e) {
    timer_->stop();
    backend_.cancelStress();
    e->accept();
}

void MainWindow::goTo(int page) {
    stack_->setCurrentIndex(page);
    if (auto* b = navGroup_->button(page)) b->setChecked(true);
    if (page == 2) simulator_->reloadChoices();
    refreshCurrentPage();
}

void MainWindow::onActivity() {
    std::vector<banksim::Event> events;
    std::vector<banksim::TxRecord> records;
    backend_.drain(events, records);
    monitor_->appendEvents(events);
    lab_->onEvents(events);
    simulator_->appendRecords(records);
}

void MainWindow::onTick() {
    refreshCurrentPage();
    refreshStatus();
}

void MainWindow::refreshCurrentPage() {
    switch (stack_->currentIndex()) {
        case 0: dashboard_->refresh(); break;
        case 1: accounts_->refresh(); break;
        case 2: simulator_->refresh(); break;
        default: break;
    }
}

void MainWindow::refreshStatus() {
    const auto st = backend_.bank().stats();
    statusLabel_->setText(QString("  Active threads: %1    |    Transactions: %2  (%3 successful, %4 failed)    |    Total deposits: %5")
                              .arg(backend_.activeThreads())
                              .arg(st.total)
                              .arg(st.success)
                              .arg(st.failed)
                              .arg(qmoney(backend_.bank().totalDeposits())));
    sideMode_->setText("MODE: " + backend_.modeText());
}

void MainWindow::onResetFinished() {
    monitor_->clearEvents();
    simulator_->onResetFinished();
    accounts_->refresh();
    refreshCurrentPage();
    refreshStatus();
}

// =============================================================================
// Developer screenshot tour
// =============================================================================

bool MainWindow::runScreenshotTour(const QString& dir) {
    QDir().mkpath(dir);
    QTextStream out(stdout);
    bool ok = true;

    auto pump = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    auto waitUntil = [&](const std::function<bool()>& pred, int timeoutMs, const char* what) {
        int waited = 0;
        while (!pred() && waited < timeoutMs) {
            pump(50);
            waited += 50;
        }
        const bool good = pred();
        if (!good) {
            out << "TOUR TIMEOUT waiting for " << what << "\n";
            ok = false;
        }
        return good;
    };
    auto shot = [&](const QString& name) {
        const QString path = dir + "/" + name + ".png";
        if (!grab().save(path)) {
            out << "could not save " << path << "\n";
            ok = false;
        } else {
            out << "saved " << path << "\n";
        }
    };

    resize(1500, 1180);
    show();
    pump(500);

    goTo(0);
    pump(300);
    shot("01_dashboard");

    // ---- joint-account demo on the main bank
    goTo(2);
    simulator_->runJointDemo();
    pump(330);  // mid-run: thread A inside the critical section, thread B waiting
    shot("02_simulator_midrun");
    waitUntil([&] { return backend_.bank().stats().total >= 2 && backend_.activeThreads() == 0; }, 5000, "joint demo");
    pump(400);
    const auto acc = backend_.bank().account("ACC1001");
    out << "joint demo final balance ACC1001 = " << QString::fromStdString(banksim::formatMoney(acc.balance)) << "\n";
    if (acc.balance != 3000) { out << "TOUR FAIL: expected 3000\n"; ok = false; }
    shot("03_simulator_after");

    goTo(4);
    pump(300);
    shot("04_monitor");

    goTo(1);
    pump(300);
    shot("05_accounts");

    // ---- stress test
    goTo(2);
    simulator_->startStress();
    waitUntil([&] { return !backend_.stressRunning(); }, 15000, "stress test");
    pump(500);
    shot("06_simulator_stress");

    // ---- labs
    goTo(3);
    lab_->runRace(true);  // race, then synchronized
    waitUntil([&] { return lab_->hasReport(banksim::LabKind::Race); }, 8000, "race lab");
    pump(200);
    lab_->showTab(LabPage::RaceTab);
    pump(200);
    shot("07_lab_race");
    waitUntil([&] { return lab_->hasReport(banksim::LabKind::Synchronized); }, 8000, "sync lab");
    pump(300);
    lab_->showTab(LabPage::SyncTab);
    pump(200);
    shot("08_lab_sync");

    lab_->runDeadlock(true);  // deadlock, then prevention
    pump(900);
    shot("09_lab_deadlock_live");
    waitUntil([&] { return lab_->hasReport(banksim::LabKind::Deadlock); }, 12000, "deadlock lab");
    pump(300);
    lab_->showTab(LabPage::DeadlockTab);
    pump(200);
    shot("10_lab_deadlock");
    waitUntil([&] { return lab_->hasReport(banksim::LabKind::DeadlockPrevented); }, 12000, "prevention lab");
    pump(300);
    lab_->showTab(LabPage::PreventionTab);
    pump(200);
    shot("11_lab_prevention");

    lab_->runTests();
    waitUntil([&] { return !backend_.testsRunning() && lab_->testRowCount() >= banksim::testCount(); }, 60000, "tests");
    pump(400);
    shot("12_lab_tests");

    goTo(4);
    pump(300);
    shot("13_monitor_all");
    return ok;
}
