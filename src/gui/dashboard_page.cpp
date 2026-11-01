#include "dashboard_page.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "backend.h"
#include "theme.h"
#include "widgets.h"

DashboardPage::DashboardPage(Backend& backend, QWidget* parent) : QWidget(parent), backend_(backend) {
    setObjectName("pageRoot");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(16);

    root->addWidget(makeLabel("Dashboard", "pageTitle"));
    root->addWidget(makeLabel("One central bank, many access points. Every ATM, online and mobile session talks to the same Bank Engine.", "pageSub"));

    auto* grid = new QGridLayout;
    grid->setSpacing(14);
    customers_ = new StatCard("Total customers");
    accounts_ = new StatCard("Total bank accounts");
    deposits_ = new StatCard("Total bank deposits");
    threads_ = new StatCard("Active threads");
    total_ = new StatCard("Total transactions");
    success_ = new StatCard("Successful");
    failed_ = new StatCard("Failed / rejected");
    mode_ = new StatCard("Concurrency mode");
    StatCard* cards[8] = {customers_, accounts_, deposits_, threads_, total_, success_, failed_, mode_};
    for (int i = 0; i < 8; ++i) grid->addWidget(cards[i], i / 4, i % 4);
    root->addLayout(grid);

    auto* lower = new QHBoxLayout;
    lower->setSpacing(16);

    // ---- recent activity
    auto* recentCard = makeCard();
    auto* rl = new QVBoxLayout(recentCard);
    rl->setContentsMargins(16, 14, 16, 14);
    rl->addWidget(makeLabel("Recent transaction activity", "sectionTitle"));
    recent_ = makeTable({"Time", "Tx ID", "Thread", "Customer", "Channel", "Type", "Account", "Amount", "Status"});
    recent_->setStyleSheet("QTableWidget { border: 0; }");
    const int widths[9] = {86, 86, 80, 96, 106, 100, 80, 90, 140};
    for (int c = 0; c < 9; ++c) recent_->setColumnWidth(c, widths[c]);
    rl->addWidget(recent_, 1);
    lower->addWidget(recentCard, 3);

    // ---- quick start
    auto* quick = makeCard();
    auto* ql = new QVBoxLayout(quick);
    ql->setContentsMargins(18, 16, 18, 16);
    ql->setSpacing(10);
    ql->addWidget(makeLabel("Quick start", "sectionTitle"));
    auto* desc = new QLabel(
        "<b>Joint-account scenario</b> (preloaded)<br>"
        "Account <b>ACC1001</b>, balance <b>₹10,000</b>, held jointly by Customer A and Customer B.<br><br>"
        "• Customer A withdraws <b>₹7,000</b> at ATM 1<br>"
        "• Customer B withdraws <b>₹6,000</b> at ATM 2<br>"
        "• both at the same time<br><br>"
        "With the account mutex only one succeeds and the final balance is <b>₹3,000</b>.");
    desc->setWordWrap(true);
    desc->setTextFormat(Qt::RichText);
    ql->addWidget(desc);
    auto* demoBtn = makeButton("Run joint-account demo", "primary");
    auto* labBtn = makeButton("Open Concurrency Lab");
    auto* testBtn = makeButton("Run automated tests", "success");
    ql->addWidget(demoBtn);
    ql->addWidget(labBtn);
    ql->addWidget(testBtn);
    ql->addStretch(1);
    lower->addWidget(quick, 1);
    root->addLayout(lower, 1);

    connect(demoBtn, &QPushButton::clicked, this, &DashboardPage::runDemoRequested);
    connect(labBtn, &QPushButton::clicked, this, &DashboardPage::openLabRequested);
    connect(testBtn, &QPushButton::clicked, this, &DashboardPage::runTestsRequested);
    refresh();
}

void DashboardPage::refresh() {
    auto& bank = backend_.bank();
    const auto st = bank.stats();
    customers_->setValue(QString::number(bank.customerCount()));
    accounts_->setValue(QString::number(bank.accountCount()));
    deposits_->setValue(qmoney(bank.totalDeposits()));
    const int active = backend_.activeThreads();
    threads_->setValue(QString::number(active), active > 0 ? theme::kBlue : QColor());
    threads_->setSub(QString("max %1 in critical sections at once").arg(backend_.engine().maxConcurrentCritical()));
    total_->setValue(QString::number(st.total));
    success_->setValue(QString::number(st.success), theme::kGreen);
    failed_->setValue(QString::number(st.failed), st.failed ? theme::kRed : QColor());
    const QString mode = backend_.modeText();
    mode_->setValue(mode.startsWith("SAFE") ? "SAFE" : (mode.startsWith("LAB") ? "LAB" : "TESTING"),
                    mode.startsWith("SAFE") ? theme::kGreen : theme::kBlue);
    mode_->setSub(mode.startsWith("SAFE") ? "per-account shared_mutex" : mode);

    const auto recent = bank.recentHistory(14);
    recent_->setRowCount(0);
    for (auto it = recent.rbegin(); it != recent.rend(); ++it) {
        const auto& r = *it;
        const int row = recent_->rowCount();
        recent_->insertRow(row);
        recent_->setItem(row, 0, makeCell(qs(banksim::formatClock(r.timestampMs))));
        recent_->setItem(row, 1, makeCell(qs(r.txId)));
        recent_->setItem(row, 2, makeCell(qs(r.threadLabel)));
        recent_->setItem(row, 3, makeCell(qs(r.customerName)));
        recent_->setItem(row, 4, makeCell(banksim::toString(r.channel)));
        recent_->setItem(row, 5, makeCell(banksim::toString(r.type)));
        recent_->setItem(row, 6, makeCell(qs(r.accountId)));
        recent_->setItem(row, 7, makeCell(r.type == banksim::TxType::BalanceEnquiry ? QString("-") : qmoney(r.amount),
                                          QColor(), QColor(), Qt::AlignVCenter | Qt::AlignRight));
        recent_->setItem(row, 8, makeCell(banksim::toString(r.status), theme::statusColor(r.status),
                                          theme::statusBg(r.status), Qt::AlignVCenter | Qt::AlignLeft, true));
    }
}
