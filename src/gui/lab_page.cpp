#include "lab_page.h"

#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>

#include "backend.h"
#include "lab_result_view.h"
#include "test_suite.h"
#include "theme.h"
#include "widgets.h"

using namespace banksim;

namespace {

QSpinBox* moneySpin(int lo, int hi, int value, int step = 500) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setGroupSeparatorShown(true);
    s->setPrefix(QString::fromUtf8("₹ "));
    s->setValue(value);
    return s;
}

QSpinBox* msSpin(int lo, int hi, int value, int step = 50) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setSingleStep(step);
    s->setSuffix(" ms");
    s->setValue(value);
    return s;
}

// Wraps a tab's content in a scroll area so the page works on small screens.
QWidget* scrollable(QWidget* content) {
    auto* area = new QScrollArea;
    area->setWidgetResizable(true);
    area->setFrameShape(QFrame::NoFrame);
    content->setObjectName("pageRoot");
    area->setWidget(content);
    return area;
}

QString joinAccounts(const LabReport& r, bool actual) {
    QStringList l;
    for (const auto& a : r.accounts) l << qs(a.id) + " " + qmoney(actual ? a.actual : a.expected);
    return l.join("   ");
}

}  // namespace

// ================================================================== ComparePanel

ComparePanel::ComparePanel(const QString& title, const QString& leftName, const QString& rightName, QWidget* parent)
    : QWidget(parent), leftName_(leftName), rightName_(rightName) {
    auto* l = new QVBoxLayout(this);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    l->addWidget(makeLabel(title, "sectionTitle"));
    table_ = makeTable({"", leftName, rightName});
    table_->setMinimumHeight(250);
    table_->setMaximumHeight(270);
    table_->setColumnWidth(0, 230);
    table_->setColumnWidth(1, 330);
    l->addWidget(table_);
    setReports(nullptr, nullptr);
}

void ComparePanel::setReports(const LabReport* left, const LabReport* right) {
    table_->setRowCount(0);
    struct Row {
        QString name;
        std::function<QString(const LabReport&)> get;
        bool colorByProblem = false;
    };
    const std::vector<Row> rows = {
        {"Verdict", [](const LabReport& r) { return qs(r.verdict); }, true},
        {"Correct result should be", [](const LabReport& r) { return joinAccounts(r, false); }, false},
        {"Actual final balance", [](const LabReport& r) { return joinAccounts(r, true); }, false},
        {"Transactions that reported success",
         [](const LabReport& r) { return QString("%1 (correct answer: %2)").arg(r.reportedSuccesses).arg(r.expectedSuccesses); }, false},
        {"Problem occurred?",
         [](const LabReport& r) { return QString(r.problemDetected ? "YES - " : "NO") + (r.problemDetected ? (r.kind == LabKind::Race ? "race condition" : "deadlock") : ""); },
         true},
        {"Money paid out that never existed", [](const LabReport& r) { return r.overdrawn > 0 ? qmoney(r.overdrawn) : QString("-"); }, false},
        {"Run time", [](const LabReport& r) { return QString("%1 ms").arg(r.elapsedMs); }, false},
    };
    for (const auto& row : rows) {
        const int i = table_->rowCount();
        table_->insertRow(i);
        table_->setItem(i, 0, makeCell(row.name, QColor(), QColor(), Qt::AlignVCenter | Qt::AlignLeft, true));
        const LabReport* reps[2] = {left, right};
        for (int c = 0; c < 2; ++c) {
            if (!reps[c]) {
                table_->setItem(i, c + 1, makeCell("not run yet", theme::kGrey));
                continue;
            }
            QColor fg, bg;
            if (row.colorByProblem) {
                fg = reps[c]->problemDetected ? theme::kRed : theme::kGreen;
                bg = reps[c]->problemDetected ? theme::kRedBg : theme::kGreenBg;
            }
            table_->setItem(i, c + 1, makeCell(row.get(*reps[c]), fg, bg, Qt::AlignVCenter | Qt::AlignLeft, row.colorByProblem));
        }
    }
}

// ===================================================================== LabPage

LabPage::LabPage(Backend& backend, QWidget* parent) : QWidget(parent), backend_(backend) {
    setObjectName("pageRoot");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 12);
    root->setSpacing(8);
    root->addWidget(makeLabel("Concurrency Lab", "pageTitle"));
    root->addWidget(makeLabel("Isolated demonstrations: they use their own accounts and never touch the balances of the main simulation.", "pageSub"));

    tabs_ = new QTabWidget;
    tabs_->setDocumentMode(true);
    root->addWidget(tabs_, 1);

    // =============================================================== 1. race condition
    {
        auto* page = new QWidget;
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 8, 8, 8);
        l->setSpacing(12);

        auto* cfg = makeCard();
        auto* g = new QGridLayout(cfg);
        g->setContentsMargins(16, 12, 16, 12);
        g->setHorizontalSpacing(12);
        g->addWidget(makeLabel("Mode 1 - Race condition (NO synchronization)", "sectionTitle"), 0, 0, 1, 6);
        auto* info = makeLabel("Two customers use the same joint account at the same time. The threads read the balance, decide, "
                               "and write back WITHOUT a lock. A barrier forces every thread to read before any thread writes - a "
                               "controlled, repeatable interleaving built only from std::atomic operations (no undefined behaviour).", "hint");
        info->setWordWrap(true);
        g->addWidget(info, 1, 0, 1, 6);
        preset_ = new QComboBox;
        preset_->addItems({QString::fromUtf8("Joint account: two withdrawals (₹7,000 + ₹6,000 from ₹10,000)"),
                           QString::fromUtf8("Lost update: two deposits (₹500 + ₹500 into ₹1,000)"), "Custom"});
        initial_ = moneySpin(0, 100000000, 10000);
        opA_ = new QComboBox;
        opA_->addItems({"Withdraw", "Deposit"});
        opB_ = new QComboBox;
        opB_->addItems({"Withdraw", "Deposit"});
        amountA_ = moneySpin(1, 100000000, 7000);
        amountB_ = moneySpin(1, 100000000, 6000);
        stepDelay_ = msSpin(0, 2000, 250);
        g->addWidget(new QLabel("Scenario"), 2, 0);
        g->addWidget(preset_, 2, 1, 1, 5);
        g->addWidget(new QLabel("Initial balance"), 3, 0);
        g->addWidget(initial_, 3, 1);
        g->addWidget(new QLabel("Customer A (ATM 1)"), 3, 2);
        g->addWidget(opA_, 3, 3);
        g->addWidget(amountA_, 3, 4);
        g->addWidget(new QLabel("Step delay (slow motion)"), 4, 0);
        g->addWidget(stepDelay_, 4, 1);
        g->addWidget(new QLabel("Customer B (ATM 2)"), 4, 2);
        g->addWidget(opB_, 4, 3);
        g->addWidget(amountB_, 4, 4);
        runRaceBtn_ = makeButton("Run Race Condition", "danger");
        runBothBtn_ = makeButton("Run Unsafe vs Safe (side by side)", "primary");
        auto* br = new QHBoxLayout;
        br->addStretch(1);
        br->addWidget(runRaceBtn_);
        br->addWidget(runBothBtn_);
        g->addLayout(br, 5, 0, 1, 6);
        l->addWidget(cfg);

        raceView_ = new LabResultView;
        l->addWidget(raceView_);
        raceCompare_ = new ComparePanel("Unsafe vs synchronized, side by side", "UNSAFE - no lock", "SAFE - mutex");
        l->addWidget(raceCompare_);
        tabs_->addTab(scrollable(page), "1  Race Condition");
    }

    // ============================================================== 2. synchronization
    {
        auto* page = new QWidget;
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 8, 8, 8);
        l->setSpacing(12);
        auto* cfg = makeCard();
        auto* g = new QGridLayout(cfg);
        g->setContentsMargins(16, 12, 16, 12);
        g->addWidget(makeLabel("Mode 2 - Synchronization (account-level mutex)", "sectionTitle"), 0, 0, 1, 4);
        auto* info = makeLabel("The same scenario as the Race Condition tab (change it there), now executed by the real banking engine: "
                               "request lock -> wait if busy -> critical section -> re-read the latest balance -> validate -> update -> "
                               "release (RAII locking, std::shared_mutex).", "hint");
        info->setWordWrap(true);
        g->addWidget(info, 1, 0, 1, 4);
        processing_ = msSpin(0, 3000, 300);
        g->addWidget(new QLabel("Time each transaction holds the lock"), 2, 0);
        g->addWidget(processing_, 2, 1);
        runSyncBtn_ = makeButton("Run Synchronized", "success");
        g->addWidget(runSyncBtn_, 2, 3);
        g->setColumnStretch(2, 1);
        l->addWidget(cfg);
        syncView_ = new LabResultView;
        l->addWidget(syncView_);
        syncCompare_ = new ComparePanel("Unsafe vs synchronized, side by side", "UNSAFE - no lock", "SAFE - mutex");
        l->addWidget(syncCompare_);
        tabs_->addTab(scrollable(page), "2  Synchronization");
    }

    // ==================================================================== 3. deadlock
    {
        auto* page = new QWidget;
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 8, 8, 8);
        l->setSpacing(12);
        auto* cfg = makeCard();
        auto* g = new QGridLayout(cfg);
        g->setContentsMargins(16, 12, 16, 12);
        g->setHorizontalSpacing(12);
        g->addWidget(makeLabel("Mode 3 - Deadlock (inconsistent lock order)", "sectionTitle"), 0, 0, 1, 6);
        auto* info = makeLabel("Thread-01 transfers A -> B while Thread-02 transfers B -> A. Each locks its SOURCE account first, so each ends up "
                               "holding one lock and waiting for the other one: a circular wait. The second lock is requested with a time-out "
                               "(std::timed_mutex), so the application never freezes.", "hint");
        info->setWordWrap(true);
        g->addWidget(info, 1, 0, 1, 6);
        balA_ = moneySpin(0, 100000000, 10000);
        balB_ = moneySpin(0, 100000000, 5000);
        amtAB_ = moneySpin(1, 100000000, 2000, 100);
        amtBA_ = moneySpin(1, 100000000, 1000, 100);
        timeout_ = msSpin(200, 10000, 2000, 100);
        hold_ = msSpin(0, 3000, 300);
        g->addWidget(new QLabel("Account A balance"), 2, 0);
        g->addWidget(balA_, 2, 1);
        g->addWidget(new QLabel("Account B balance"), 2, 2);
        g->addWidget(balB_, 2, 3);
        g->addWidget(new QLabel("Lock time-out"), 2, 4);
        g->addWidget(timeout_, 2, 5);
        g->addWidget(new QLabel("Transfer A -> B"), 3, 0);
        g->addWidget(amtAB_, 3, 1);
        g->addWidget(new QLabel("Transfer B -> A"), 3, 2);
        g->addWidget(amtBA_, 3, 3);
        g->addWidget(new QLabel("Work while holding 1st lock"), 3, 4);
        g->addWidget(hold_, 3, 5);
        runDeadBtn_ = makeButton("Run Deadlock Demo", "danger");
        runDeadBothBtn_ = makeButton("Run Deadlock vs Prevention (side by side)", "primary");
        auto* br = new QHBoxLayout;
        br->addStretch(1);
        br->addWidget(runDeadBtn_);
        br->addWidget(runDeadBothBtn_);
        g->addLayout(br, 4, 0, 1, 6);
        l->addWidget(cfg);
        deadView_ = new LabResultView;
        l->addWidget(deadView_);
        deadCompare_ = new ComparePanel("Deadlock vs deadlock prevention", "DEADLOCK - inconsistent order", "PREVENTED - ordered locks");
        l->addWidget(deadCompare_);
        tabs_->addTab(scrollable(page), "3  Deadlock");
    }

    // ======================================================== 4. deadlock prevention
    {
        auto* page = new QWidget;
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 8, 8, 8);
        l->setSpacing(12);
        auto* cfg = makeCard();
        auto* g = new QGridLayout(cfg);
        g->setContentsMargins(16, 12, 16, 12);
        g->addWidget(makeLabel("Deadlock prevention - consistent lock ordering", "sectionTitle"), 0, 0, 1, 3);
        auto* info = makeLabel("Identical traffic (A -> B and B -> A, same amounts and timings as the Deadlock tab), but every transfer now acquires "
                               "the account locks in ASCENDING account-id order, whatever its direction. A circular wait can therefore never form.", "hint");
        info->setWordWrap(true);
        g->addWidget(info, 1, 0, 1, 3);
        runPrevBtn_ = makeButton("Run Deadlock Prevention", "success");
        g->setColumnStretch(0, 1);
        g->addWidget(runPrevBtn_, 2, 2);
        l->addWidget(cfg);
        prevView_ = new LabResultView;
        l->addWidget(prevView_);
        prevCompare_ = new ComparePanel("Deadlock vs deadlock prevention", "DEADLOCK - inconsistent order", "PREVENTED - ordered locks");
        l->addWidget(prevCompare_);
        tabs_->addTab(scrollable(page), "4  Deadlock Prevention");
    }

    // ================================================================== 5. tests
    {
        auto* page = new QWidget;
        auto* l = new QVBoxLayout(page);
        l->setContentsMargins(0, 8, 8, 8);
        l->setSpacing(12);
        auto* cfg = makeCard();
        auto* h = new QHBoxLayout(cfg);
        h->setContentsMargins(16, 12, 16, 12);
        auto* t = new QVBoxLayout;
        t->addWidget(makeLabel("Automated tests", "sectionTitle"));
        auto* info = makeLabel("Runs the 10 scenarios of the specification plus 5 extra checks of the banking rules. Each test launches its own worker threads "
                               "on private accounts, so the main simulation is not affected.", "hint");
        info->setWordWrap(true);
        t->addWidget(info);
        h->addLayout(t, 1);
        runTestsBtn_ = makeButton("Run All Tests", "success");
        h->addWidget(runTestsBtn_);
        l->addWidget(cfg);
        testProgress_ = new QProgressBar;
        testProgress_->setRange(0, testCount());
        testProgress_->setValue(0);
        testProgress_->setTextVisible(false);
        l->addWidget(testProgress_);
        testSummary_ = makeLabel("Not run yet.", "sectionTitle");
        l->addWidget(testSummary_);
        testTable_ = makeTable({"#", "Test", "Result", "Time", "What was verified"});
        testTable_->setColumnWidth(0, 36);
        testTable_->setColumnWidth(1, 380);
        testTable_->setColumnWidth(2, 80);
        testTable_->setColumnWidth(3, 80);
        testTable_->setMinimumHeight(520);
        testTable_->setWordWrap(true);
        testTable_->setTextElideMode(Qt::ElideNone);
        l->addWidget(testTable_, 1);
        tabs_->addTab(scrollable(page), "5  Automated Tests");
    }

    connect(preset_, qOverload<int>(&QComboBox::currentIndexChanged), this, &LabPage::applyPreset);
    connect(runRaceBtn_, &QPushButton::clicked, this, [this] { runRace(false); });
    connect(runBothBtn_, &QPushButton::clicked, this, [this] { runRace(true); });
    connect(runSyncBtn_, &QPushButton::clicked, this, &LabPage::runSync);
    connect(runDeadBtn_, &QPushButton::clicked, this, [this] { runDeadlock(false); });
    connect(runDeadBothBtn_, &QPushButton::clicked, this, [this] { runDeadlock(true); });
    connect(runPrevBtn_, &QPushButton::clicked, this, &LabPage::runPrevention);
    connect(runTestsBtn_, &QPushButton::clicked, this, &LabPage::runTests);
    for (QComboBox* c : {opA_, opB_})
        connect(c, qOverload<int>(&QComboBox::activated), this, [this] { preset_->setCurrentIndex(2); });
    for (QSpinBox* s : {initial_, amountA_, amountB_})
        connect(s, qOverload<int>(&QSpinBox::valueChanged), this, [this] {
            preset_->blockSignals(true);  // user edited a value by hand: the scenario is now "Custom"
            preset_->setCurrentIndex(2);
            preset_->blockSignals(false);
        });

    applyPreset(0);
    refreshButtons();
}

void LabPage::showTab(int tab) { tabs_->setCurrentIndex(tab); }

bool LabPage::hasReport(LabKind kind) const {
    switch (kind) {
        case LabKind::Race: return lastRace_ != nullptr;
        case LabKind::Synchronized: return lastSync_ != nullptr;
        case LabKind::Deadlock: return lastDead_ != nullptr;
        case LabKind::DeadlockPrevented: return lastPrev_ != nullptr;
    }
    return false;
}

void LabPage::applyPreset(int index) {
    if (index == 0) {
        preset_->blockSignals(true);
        initial_->blockSignals(true);
        amountA_->blockSignals(true);
        amountB_->blockSignals(true);
        initial_->setValue(10000);
        opA_->setCurrentIndex(0);
        opB_->setCurrentIndex(0);
        amountA_->setValue(7000);
        amountB_->setValue(6000);
        preset_->blockSignals(false);
        initial_->blockSignals(false);
        amountA_->blockSignals(false);
        amountB_->blockSignals(false);
    } else if (index == 1) {
        initial_->blockSignals(true);
        amountA_->blockSignals(true);
        amountB_->blockSignals(true);
        initial_->setValue(1000);
        opA_->setCurrentIndex(1);
        opB_->setCurrentIndex(1);
        amountA_->setValue(500);
        amountB_->setValue(500);
        initial_->blockSignals(false);
        amountA_->blockSignals(false);
        amountB_->blockSignals(false);
    }
}

// ------------------------------------------------------------------ configuration

RaceConfig LabPage::raceConfig() const {
    RaceConfig c;
    c.initial = initial_->value();
    c.stepDelayMs = stepDelay_->value();
    c.participants = {
        {"CUST-A", "Customer A", Channel::ATM1, opA_->currentIndex() == 0 ? TxType::Withdrawal : TxType::Deposit, amountA_->value()},
        {"CUST-B", "Customer B", Channel::ATM2, opB_->currentIndex() == 0 ? TxType::Withdrawal : TxType::Deposit, amountB_->value()},
    };
    return c;
}

SyncConfig LabPage::syncConfig() const {
    const RaceConfig r = raceConfig();
    SyncConfig c;
    c.initial = r.initial;
    c.participants = r.participants;
    c.processingMs = processing_->value();
    return c;
}

DeadlockConfig LabPage::deadlockConfig(bool ordered) const {
    DeadlockConfig c;
    c.balanceA = balA_->value();
    c.balanceB = balB_->value();
    c.amountAtoB = amtAB_->value();
    c.amountBtoA = amtBA_->value();
    c.timeoutMs = timeout_->value();
    c.holdMs = hold_->value();
    c.ordered = ordered;
    return c;
}

// -------------------------------------------------------------------- actions

void LabPage::runRace(bool thenSynchronized) {
    if (backend_.labRunning() || backend_.isResetting()) return;
    chain_ = thenSynchronized ? Chain::SyncAfterRace : Chain::None;
    backend_.runRace(raceConfig());
}

void LabPage::runSync() {
    if (backend_.labRunning() || backend_.isResetting()) return;
    chain_ = Chain::None;
    backend_.runSync(syncConfig());
}

void LabPage::runDeadlock(bool thenPrevention) {
    if (backend_.labRunning() || backend_.isResetting()) return;
    chain_ = thenPrevention ? Chain::PreventionAfterDeadlock : Chain::None;
    backend_.runDeadlock(deadlockConfig(false));
}

void LabPage::runPrevention() {
    if (backend_.labRunning() || backend_.isResetting()) return;
    chain_ = Chain::None;
    backend_.runDeadlock(deadlockConfig(true));
}

void LabPage::refreshButtons() {
    const bool free = !backend_.labRunning() && !backend_.isResetting();
    for (QPushButton* b : {runRaceBtn_, runBothBtn_, runSyncBtn_, runDeadBtn_, runDeadBothBtn_, runPrevBtn_}) b->setEnabled(free);
    runTestsBtn_->setEnabled(!backend_.testsRunning());
}

static LabResultView* viewFor(int kind, LabResultView* a, LabResultView* b, LabResultView* c, LabResultView* d) {
    switch (kind) {
        case 0: return a;
        case 1: return b;
        case 2: return c;
        default: return d;
    }
}

void LabPage::onLabStarted(int kind) {
    viewFor(kind, raceView_, syncView_, deadView_, prevView_)->beginRun();
    tabs_->setCurrentIndex(kind);
    refreshButtons();
}

void LabPage::onEvents(const std::vector<Event>& events) {
    for (const auto& e : events) {
        if (e.source == "lab:race") raceView_->liveEvent(e);
        else if (e.source == "lab:sync") syncView_->liveEvent(e);
        else if (e.source == "lab:deadlock") deadView_->liveEvent(e);
        else if (e.source == "lab:ordered") prevView_->liveEvent(e);
    }
}

void LabPage::onLabFinished(const std::shared_ptr<LabReport>& rep) {
    switch (rep->kind) {
        case LabKind::Race: lastRace_ = rep; raceView_->setReport(*rep); break;
        case LabKind::Synchronized: lastSync_ = rep; syncView_->setReport(*rep); break;
        case LabKind::Deadlock: lastDead_ = rep; deadView_->setReport(*rep); break;
        case LabKind::DeadlockPrevented: lastPrev_ = rep; prevView_->setReport(*rep); break;
    }
    updateComparisons();
    refreshButtons();

    // "run both" buttons: start the second half as soon as the first one is done
    const Chain next = chain_;
    chain_ = Chain::None;
    if (next == Chain::SyncAfterRace && rep->kind == LabKind::Race) backend_.runSync(syncConfig());
    if (next == Chain::PreventionAfterDeadlock && rep->kind == LabKind::Deadlock) backend_.runDeadlock(deadlockConfig(true));
}

void LabPage::updateComparisons() {
    raceCompare_->setReports(lastRace_.get(), lastSync_.get());
    syncCompare_->setReports(lastRace_.get(), lastSync_.get());
    deadCompare_->setReports(lastDead_.get(), lastPrev_.get());
    prevCompare_->setReports(lastDead_.get(), lastPrev_.get());
}

// ---------------------------------------------------------------------- tests

void LabPage::runTests() {
    if (backend_.testsRunning()) return;
    tabs_->setCurrentIndex(TestsTab);
    testTable_->setRowCount(0);
    testProgress_->setValue(0);
    testSummary_->setStyleSheet(QString("color:%1;").arg(theme::css(theme::kBlue)));
    testSummary_->setText("Running tests...");
    backend_.runTests();
    refreshButtons();
}

void LabPage::onTestProgress(int number, const QString& name, bool passed, const QString& detail, qlonglong ms) {
    const int row = testTable_->rowCount();
    testTable_->insertRow(row);
    const QColor fg = passed ? theme::kGreen : theme::kRed;
    const QColor bg = passed ? theme::kGreenBg : theme::kRedBg;
    testTable_->setItem(row, 0, makeCell(QString::number(number)));
    testTable_->setItem(row, 1, makeCell(name, QColor(), QColor(), Qt::AlignVCenter | Qt::AlignLeft, true));
    testTable_->setItem(row, 2, makeCell(passed ? "PASS" : "FAIL", fg, bg, Qt::AlignVCenter | Qt::AlignCenter, true));
    testTable_->setItem(row, 3, makeCell(QString("%1 ms").arg(ms), QColor(), QColor(), Qt::AlignVCenter | Qt::AlignRight));
    auto* d = makeCell(detail);
    d->setToolTip(detail);
    testTable_->setItem(row, 4, d);
    testTable_->setRowHeight(row, 46);
    testProgress_->setValue(row + 1);
    testTable_->scrollToBottom();
}

int LabPage::testRowCount() const { return testTable_->rowCount(); }

void LabPage::onTestsFinished(int passed, int total) {
    const bool ok = passed == total;
    testSummary_->setStyleSheet(QString("color:%1; font-size:16px;").arg(theme::css(ok ? theme::kGreen : theme::kRed)));
    testSummary_->setText(QString("%1 of %2 tests passed%3").arg(passed).arg(total).arg(ok ? " - all green" : " - FAILURES"));
    refreshButtons();
}
