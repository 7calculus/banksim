#include "simulator_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "backend.h"
#include "theme.h"
#include "widgets.h"

using banksim::TxType;

SimulatorPage::SimulatorPage(Backend& backend, QWidget* parent) : QWidget(parent), backend_(backend) {
    setObjectName("pageRoot");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(12);

    root->addWidget(makeLabel("Banking Simulator", "pageTitle"));
    root->addWidget(makeLabel("Every transaction runs on its own worker thread. Fire one, queue several, or start a stress test - the GUI stays responsive.", "pageSub"));

    // ================================================================= top row
    auto* top = new QHBoxLayout;
    top->setSpacing(14);

    // ---- new transaction card
    auto* tx = makeCard();
    auto* tl = new QGridLayout(tx);
    tl->setContentsMargins(16, 14, 16, 14);
    tl->setHorizontalSpacing(12);
    tl->setVerticalSpacing(6);
    tl->addWidget(makeLabel("New transaction", "sectionTitle"), 0, 0, 1, 2);
    customer_ = new QComboBox;
    channel_ = new QComboBox;
    for (auto c : banksim::allChannels()) channel_->addItem(banksim::toString(c), static_cast<int>(c));
    account_ = new QComboBox;
    type_ = new QComboBox;
    for (auto t : banksim::allTxTypes()) type_->addItem(banksim::toString(t), static_cast<int>(t));
    type_->setCurrentIndex(1);  // withdrawal
    amount_ = new QSpinBox;
    amount_->setRange(-1000000, 1000000000);  // negatives allowed on purpose: the engine must reject them
    amount_->setSingleStep(500);
    amount_->setGroupSeparatorShown(true);
    amount_->setPrefix(QString::fromUtf8("₹ "));
    amount_->setValue(1000);
    dest_ = new QComboBox;
    tl->addWidget(new QLabel("Customer"), 1, 0);
    tl->addWidget(new QLabel("Channel"), 1, 1);
    tl->addWidget(customer_, 2, 0);
    tl->addWidget(channel_, 2, 1);
    tl->addWidget(new QLabel("Account"), 3, 0);
    tl->addWidget(new QLabel("Transaction type"), 3, 1);
    tl->addWidget(account_, 4, 0);
    tl->addWidget(type_, 4, 1);
    tl->addWidget(new QLabel("Amount"), 5, 0);
    tl->addWidget(new QLabel("Destination account (transfers)"), 5, 1);
    tl->addWidget(amount_, 6, 0);
    tl->addWidget(dest_, 6, 1);
    showAll_ = new QCheckBox("Also list accounts this customer is NOT authorized for (to test the rule)");
    tl->addWidget(showAll_, 7, 0, 1, 2);
    execBtn_ = makeButton("Execute Transaction", "primary");
    addBtn_ = makeButton("Add to concurrent batch");
    auto* br = new QHBoxLayout;
    br->addWidget(execBtn_);
    br->addWidget(addBtn_);
    tl->addLayout(br, 8, 0, 1, 2);
    top->addWidget(tx, 5);

    // ---- concurrent batch card
    auto* cc = makeCard();
    auto* cl = new QVBoxLayout(cc);
    cl->setContentsMargins(16, 14, 16, 14);
    cl->setSpacing(6);
    cl->addWidget(makeLabel("Concurrent execution", "sectionTitle"));
    batchHint_ = makeLabel("Queue is empty: \"Execute Concurrently\" will start N copies of the transaction on the left.", "hint");
    batchHint_->setWordWrap(true);
    cl->addWidget(batchHint_);
    batchTable_ = makeTable({"#", "Customer", "Channel", "Transaction"});
    batchTable_->setMaximumHeight(118);
    batchTable_->setColumnWidth(0, 30);
    batchTable_->setColumnWidth(1, 100);
    batchTable_->setColumnWidth(2, 100);
    cl->addWidget(batchTable_);
    auto* qrow = new QHBoxLayout;
    removeBtn_ = makeButton("Remove selected");
    clearBtn_ = makeButton("Clear queue");
    demoBtn_ = makeButton("Load joint-account demo");
    qrow->addWidget(removeBtn_);
    qrow->addWidget(clearBtn_);
    qrow->addWidget(demoBtn_);
    cl->addLayout(qrow);
    auto* opts = new QGridLayout;
    threads_ = new QSpinBox;
    threads_->setRange(1, 500);
    threads_->setValue(5);
    delay_ = new QSpinBox;
    delay_->setRange(0, 3000);
    delay_->setSingleStep(50);
    delay_->setValue(250);
    delay_->setSuffix(" ms");
    opts->addWidget(new QLabel("Concurrent customer threads"), 0, 0);
    opts->addWidget(threads_, 0, 1);
    opts->addWidget(new QLabel("Bank processing time in critical section"), 1, 0);
    opts->addWidget(delay_, 1, 1);
    cl->addLayout(opts);
    ordered_ = new QCheckBox("Deterministic arrival order (queue order; ticked = reproducible)");
    ordered_->setChecked(true);
    atmCash_ = new QCheckBox("Limit ATMs by physical cash (optional, ₹50,000 per ATM)");
    cl->addWidget(ordered_);
    cl->addWidget(atmCash_);
    atmLabel_ = makeLabel(QString(), "hint");
    cl->addWidget(atmLabel_);
    concBtn_ = makeButton("Execute Concurrently", "success");
    cl->addWidget(concBtn_);
    top->addWidget(cc, 6);
    root->addLayout(top);

    // ================================================================ stress row
    auto* stress = makeCard();
    auto* sl = new QHBoxLayout(stress);
    sl->setContentsMargins(16, 10, 16, 10);
    sl->setSpacing(10);
    sl->addWidget(makeLabel("Stress test", "sectionTitle"));
    stressThreads_ = new QSpinBox;
    stressThreads_->setRange(1, 500);
    stressThreads_->setValue(20);
    stressThreads_->setSuffix(" threads");
    stressOps_ = new QSpinBox;
    stressOps_->setRange(1, 5000);
    stressOps_->setValue(50);
    stressOps_->setSuffix(" ops each");
    stressBtn_ = makeButton("Start Stress Test", "primary");
    cancelBtn_ = makeButton("Cancel");
    stressResult_ = makeLabel("Random deposits, withdrawals, enquiries and transfers; afterwards total money is verified.", "hint");
    stressResult_->setWordWrap(true);
    resetBtn_ = makeButton("Reset Simulation", "danger");
    sl->addWidget(stressThreads_);
    sl->addWidget(stressOps_);
    sl->addWidget(stressBtn_);
    sl->addWidget(cancelBtn_);
    sl->addWidget(stressResult_, 1);
    sl->addWidget(resetBtn_);
    root->addWidget(stress);

    // ============================================================ results row
    auto* bottom = new QHBoxLayout;
    bottom->setSpacing(14);
    auto* rc = makeCard();
    auto* rl = new QVBoxLayout(rc);
    rl->setContentsMargins(16, 12, 16, 12);
    rl->addWidget(makeLabel("Transaction results (newest first)", "sectionTitle"));
    results_ = makeTable({"Time", "Tx ID", "Thread", "Customer", "Channel", "Type", "Account", "Amount", "Status", "Balance after", "Message"});
    results_->setStyleSheet("QTableWidget { border: 0; }");
    const int w[11] = {84, 80, 70, 86, 64, 90, 70, 76, 112, 88, 300};
    for (int c = 0; c < 11; ++c) results_->setColumnWidth(c, w[c]);
    rl->addWidget(results_, 1);
    bottom->addWidget(rc, 1);

    auto* bc = makeCard();
    auto* bl = new QVBoxLayout(bc);
    bl->setContentsMargins(16, 12, 16, 12);
    bl->addWidget(makeLabel("Account balances (live)", "sectionTitle"));
    balances_ = makeTable({"Account", "Holders", "Balance"});
    balances_->setStyleSheet("QTableWidget { border: 0; }");
    balances_->setColumnWidth(0, 70);
    balances_->setColumnWidth(1, 150);
    balances_->horizontalHeader()->setStretchLastSection(true);
    bl->addWidget(balances_, 1);
    bc->setFixedWidth(360);
    bottom->addWidget(bc, 0);
    root->addLayout(bottom, 1);

    // ================================================================ wiring
    connect(customer_, qOverload<int>(&QComboBox::currentIndexChanged), this, &SimulatorPage::updateAccountChoices);
    connect(showAll_, &QCheckBox::toggled, this, &SimulatorPage::updateAccountChoices);
    connect(type_, qOverload<int>(&QComboBox::currentIndexChanged), this, &SimulatorPage::onTypeChanged);
    connect(execBtn_, &QPushButton::clicked, this, &SimulatorPage::executeOne);
    connect(addBtn_, &QPushButton::clicked, this, &SimulatorPage::addToBatch);
    connect(concBtn_, &QPushButton::clicked, this, &SimulatorPage::executeConcurrently);
    connect(demoBtn_, &QPushButton::clicked, this, &SimulatorPage::loadJointDemo);
    connect(removeBtn_, &QPushButton::clicked, this, &SimulatorPage::removeBatchItem);
    connect(clearBtn_, &QPushButton::clicked, this, &SimulatorPage::clearBatch);
    connect(stressBtn_, &QPushButton::clicked, this, &SimulatorPage::startStress);
    connect(cancelBtn_, &QPushButton::clicked, this, [this] { backend_.cancelStress(); });
    connect(resetBtn_, &QPushButton::clicked, this, [this] { backend_.resetSimulation(); });
    connect(atmCash_, &QCheckBox::toggled, this, [this] { applyEngineSettings(); refresh(); });
    connect(delay_, qOverload<int>(&QSpinBox::valueChanged), this, [this] { applyEngineSettings(); });

    reloadChoices();
    onTypeChanged();
    refreshBatchTable();
    refresh();
}

// ------------------------------------------------------------------- choices

void SimulatorPage::reloadChoices() {
    const QString prevCust = customer_->currentData().toString();
    const QString prevDest = dest_->currentData().toString();
    const auto customers = backend_.bank().customers();
    const auto accounts = backend_.bank().accounts();

    customer_->blockSignals(true);
    customer_->clear();
    for (const auto& c : customers) customer_->addItem(qs(c.name), qs(c.id));
    const int ci = customer_->findData(prevCust);
    customer_->setCurrentIndex(ci >= 0 ? ci : 0);
    customer_->blockSignals(false);

    dest_->clear();
    for (const auto& a : accounts) dest_->addItem(qs(a.id) + " - " + qs(a.type) + (a.joint ? " (joint)" : ""), qs(a.id));
    const int di = dest_->findData(prevDest);
    if (di >= 0) dest_->setCurrentIndex(di);
    else if (dest_->count() > 1) dest_->setCurrentIndex(1);
    updateAccountChoices();
}

void SimulatorPage::updateAccountChoices() {
    const QString cust = customer_->currentData().toString();
    const QString prev = account_->currentData().toString();
    account_->clear();
    for (const auto& a : backend_.bank().accounts()) {
        bool holder = false;
        for (const auto& h : a.holderIds) holder = holder || qs(h) == cust;
        if (!holder && !showAll_->isChecked()) continue;
        account_->addItem(qs(a.id) + " - " + qs(a.type) + (a.joint ? " (joint)" : "") + (holder ? "" : "  [not authorized]"), qs(a.id));
    }
    const int i = account_->findData(prev);
    if (i >= 0) account_->setCurrentIndex(i);
}

void SimulatorPage::onTypeChanged() {
    const auto t = static_cast<TxType>(type_->currentData().toInt());
    dest_->setEnabled(t == TxType::Transfer);
    amount_->setEnabled(t != TxType::BalanceEnquiry);
}

// --------------------------------------------------------------- requests

banksim::TxRequest SimulatorPage::buildRequest() const {
    banksim::TxRequest r;
    r.type = static_cast<TxType>(type_->currentData().toInt());
    r.customerId = customer_->currentData().toString().toStdString();
    r.accountId = account_->currentData().toString().toStdString();
    r.destAccountId = r.type == TxType::Transfer ? dest_->currentData().toString().toStdString() : std::string();
    r.amount = r.type == TxType::BalanceEnquiry ? 0 : amount_->value();
    r.channel = static_cast<banksim::Channel>(channel_->currentData().toInt());
    return r;
}

QString SimulatorPage::describe(const banksim::TxRequest& r) const {
    QString s = banksim::toString(r.type);
    if (r.type == TxType::Transfer) s += QString(" %1  %2 -> %3").arg(qmoney(r.amount), qs(r.accountId), qs(r.destAccountId));
    else if (r.type != TxType::BalanceEnquiry) s += QString(" %1  on %2").arg(qmoney(r.amount), qs(r.accountId));
    else s += " on " + qs(r.accountId);
    return s;
}

void SimulatorPage::applyEngineSettings() {
    backend_.setProcessingDelay(delay_->value());
    backend_.setAtmCashEnabled(atmCash_->isChecked());
}

void SimulatorPage::executeOne() {
    if (account_->count() == 0 || customer_->count() == 0) return;
    applyEngineSettings();
    backend_.executeSingle(buildRequest());
}

void SimulatorPage::addToBatch() {
    if (account_->count() == 0 || customer_->count() == 0) return;
    batch_.push_back(buildRequest());
    refreshBatchTable();
}

void SimulatorPage::removeBatchItem() {
    const int row = batchTable_->currentRow();
    if (row < 0 || row >= static_cast<int>(batch_.size())) return;
    batch_.erase(batch_.begin() + row);
    refreshBatchTable();
}

void SimulatorPage::clearBatch() {
    batch_.clear();
    refreshBatchTable();
}

void SimulatorPage::refreshBatchTable() {
    batchTable_->setRowCount(0);
    int i = 0;
    for (const auto& r : batch_) {
        const int row = batchTable_->rowCount();
        batchTable_->insertRow(row);
        batchTable_->setItem(row, 0, makeCell(QString::number(++i)));
        batchTable_->setItem(row, 1, makeCell(qs(backend_.bank().customerName(r.customerId))));
        batchTable_->setItem(row, 2, makeCell(banksim::toString(r.channel)));
        batchTable_->setItem(row, 3, makeCell(describe(r)));
    }
    batchHint_->setText(batch_.empty()
                            ? "Queue is empty: \"Execute Concurrently\" starts the number of threads below, each running the transaction on the left."
                            : QString("%1 queued transaction(s): \"Execute Concurrently\" runs them all at once, one thread each.").arg(batch_.size()));
    threads_->setEnabled(batch_.empty());
}

void SimulatorPage::executeConcurrently() {
    if (customer_->count() == 0) return;
    applyEngineSettings();
    std::vector<banksim::TxRequest> reqs = batch_;
    if (reqs.empty()) {
        if (account_->count() == 0) return;
        for (int i = 0; i < threads_->value(); ++i) reqs.push_back(buildRequest());
    }
    backend_.executeConcurrent(std::move(reqs), ordered_->isChecked());
}

void SimulatorPage::loadJointDemo() {
    batch_.clear();
    auto mk = [](const char* cust, banksim::Channel ch) {
        banksim::TxRequest r;
        r.type = TxType::Withdrawal;
        r.customerId = cust;
        r.accountId = "ACC1001";
        r.channel = ch;
        return r;
    };
    auto a = mk("CUST-A", banksim::Channel::ATM1);
    a.amount = 7000;
    auto b = mk("CUST-B", banksim::Channel::ATM2);
    b.amount = 6000;
    batch_ = {a, b};
    ordered_->setChecked(true);
    delay_->setValue(400);
    refreshBatchTable();
}

void SimulatorPage::runJointDemo() {
    loadJointDemo();
    const auto acc = backend_.bank().account("ACC1001");
    if (!acc.id.empty() && acc.balance != acc.initialBalance) {
        // restore the joint account to its starting balance first, then run
        pendingDemo_ = true;
        backend_.resetSimulation();
        return;
    }
    executeConcurrently();
}

void SimulatorPage::onResetFinished() {
    batch_.clear();
    results_->setRowCount(0);
    stressResult_->setText("Simulation reset: balances restored, history cleared.");
    stressResult_->setStyleSheet(QString());
    reloadChoices();
    refreshBatchTable();
    if (pendingDemo_) {
        pendingDemo_ = false;
        loadJointDemo();
        executeConcurrently();
    }
    refresh();
}

// ------------------------------------------------------------------ stress

void SimulatorPage::startStress() {
    banksim::StressConfig cfg;
    cfg.threads = stressThreads_->value();
    cfg.opsPerThread = stressOps_->value();
    cfg.seed = 12345;
    cfg.processingMs = 0;
    stressResult_->setStyleSheet(QString("color:%1; font-weight:600;").arg(theme::css(theme::kBlue)));
    stressResult_->setText(QString("Running %1 threads x %2 operations ...").arg(cfg.threads).arg(cfg.opsPerThread));
    backend_.startStress(cfg);
    refresh();
}

void SimulatorPage::onStressFinished(const std::shared_ptr<banksim::StressReport>& rep) {
    const auto& r = *rep;
    const bool ok = r.consistent;
    stressResult_->setStyleSheet(QString("color:%1; font-weight:600;").arg(theme::css(ok ? theme::kGreen : theme::kRed)));
    stressResult_->setText(QString("%1  %2 operations (%3 ok, %4 rejected) on %5 threads in %6 ms. Total money expected %7, actual %8; "
                                   "no negative balance: %9; unique transaction ids: %10; up to %11 threads in critical sections at once.")
                               .arg(ok ? "PASS" : "FAIL")
                               .arg(r.operations)
                               .arg(r.succeeded)
                               .arg(r.failed)
                               .arg(r.threads)
                               .arg(r.elapsedMs)
                               .arg(qmoney(r.expectedTotal), qmoney(r.actualTotal))
                               .arg(r.noNegativeBalance ? "yes" : "NO")
                               .arg(r.uniqueTxIds ? "yes" : "NO")
                               .arg(r.maxConcurrentCritical));
    if (r.cancelled) stressResult_->setText(stressResult_->text() + "  (cancelled early)");
    refresh();
}

// ----------------------------------------------------------------- results

void SimulatorPage::appendRecords(const std::vector<banksim::TxRecord>& records) {
    if (records.empty()) return;
    results_->setUpdatesEnabled(false);
    for (const auto& r : records) {
        if (r.source != "main") continue;
        results_->insertRow(0);
        results_->setItem(0, 0, makeCell(qs(banksim::formatClock(r.timestampMs))));
        results_->setItem(0, 1, makeCell(qs(r.txId)));
        results_->setItem(0, 2, makeCell(qs(r.threadLabel)));
        results_->setItem(0, 3, makeCell(qs(r.customerName)));
        results_->setItem(0, 4, makeCell(banksim::toString(r.channel)));
        results_->setItem(0, 5, makeCell(banksim::toString(r.type)));
        results_->setItem(0, 6, makeCell(qs(r.accountId)));
        results_->setItem(0, 7, makeCell(r.type == TxType::BalanceEnquiry ? QString("-") : qmoney(r.amount), QColor(), QColor(),
                                         Qt::AlignVCenter | Qt::AlignRight));
        results_->setItem(0, 8, makeCell(banksim::toString(r.status), theme::statusColor(r.status), theme::statusBg(r.status),
                                         Qt::AlignVCenter | Qt::AlignLeft, true));
        const bool showBal = r.status == banksim::TxStatus::Success || r.status == banksim::TxStatus::InsufficientFunds ||
                             r.status == banksim::TxStatus::AtmCashInsufficient;
        results_->setItem(0, 9, makeCell(showBal ? qmoney(r.balanceAfter) : QString("-"), QColor(), QColor(),
                                         Qt::AlignVCenter | Qt::AlignRight, true));
        results_->setItem(0, 10, makeCell(qs(r.message)));
    }
    while (results_->rowCount() > 800) results_->removeRow(results_->rowCount() - 1);
    results_->setUpdatesEnabled(true);
}

void SimulatorPage::refresh() {
    const bool resetting = backend_.isResetting();
    const bool stressing = backend_.stressRunning();
    stressBtn_->setEnabled(!stressing && !resetting);
    cancelBtn_->setEnabled(stressing);
    execBtn_->setEnabled(!resetting);
    concBtn_->setEnabled(!resetting);
    demoBtn_->setEnabled(!resetting);
    resetBtn_->setEnabled(!resetting);

    // balances (lock-free snapshot - never blocks behind a running transaction)
    const auto accounts = backend_.bank().accounts();
    if (balances_->rowCount() != static_cast<int>(accounts.size())) {
        balances_->setRowCount(static_cast<int>(accounts.size()));
    }
    int row = 0;
    for (const auto& a : accounts) {
        QStringList names;
        for (const auto& n : a.holderNames) names << qs(n);
        const QColor bg = a.joint ? QColor(0xee, 0xf2, 0xff) : QColor();
        balances_->setItem(row, 0, makeCell(qs(a.id), QColor(), bg, Qt::AlignVCenter | Qt::AlignLeft, true));
        balances_->setItem(row, 1, makeCell(names.join(" + "), QColor(), bg));
        const bool changed = a.balance != a.initialBalance;
        balances_->setItem(row, 2, makeCell(qmoney(a.balance), changed ? theme::kBlue : QColor(), bg, Qt::AlignVCenter | Qt::AlignRight, true));
        ++row;
    }
    if (backend_.bank().atmCashEnabled()) {
        atmLabel_->setText(QString("ATM cash: ATM 1 %1 | ATM 2 %2 | ATM 3 %3")
                               .arg(qmoney(backend_.bank().atmCash(banksim::Channel::ATM1)),
                                    qmoney(backend_.bank().atmCash(banksim::Channel::ATM2)),
                                    qmoney(backend_.bank().atmCash(banksim::Channel::ATM3))));
    } else {
        atmLabel_->setText(QString());
    }
}
