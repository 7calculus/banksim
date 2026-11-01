#include "lab_result_view.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QSplitter>
#include <QTextBrowser>
#include <QVBoxLayout>

#include "theme.h"
#include "widgets.h"

using namespace banksim;

LabResultView::LabResultView(QWidget* parent) : QWidget(parent) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(10);

    // ---- verdict row
    auto* top = new QHBoxLayout;
    top->addWidget(makeLabel("Result", "sectionTitle"));
    verdict_ = new Badge;
    top->addWidget(verdict_);
    timing_ = makeLabel(QString(), "hint");
    top->addWidget(timing_, 1);
    root->addLayout(top);

    // ---- balances: initial / expected / actual
    balances_ = makeTable({"Account", "Initial balance", "Expected (correct result)", "Actual balance", "Check"});
    balances_->setMaximumHeight(112);
    balances_->setMinimumHeight(84);
    for (int c = 0; c < 4; ++c) balances_->setColumnWidth(c, 160);
    root->addWidget(balances_);

    // ---- middle: left = threads / outcomes / locks, right = sequence
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);

    auto* left = new QWidget;
    auto* ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 8, 0);
    ll->setSpacing(6);
    ll->addWidget(makeLabel("Thread states", "sectionTitle"));
    threads_ = makeTable({"Thread", "Customer", "Channel", "State", "Last action"});
    threads_->setMinimumHeight(100);
    threads_->setMaximumHeight(140);
    threads_->setColumnWidth(0, 76);
    threads_->setColumnWidth(1, 100);
    threads_->setColumnWidth(2, 80);
    threads_->setColumnWidth(3, 86);
    ll->addWidget(threads_);
    ll->addWidget(makeLabel("Transaction outcomes", "sectionTitle"));
    outcomes_ = makeTable({"Thread", "Customer", "Channel", "Operation", "Outcome"});
    outcomes_->setMinimumHeight(100);
    outcomes_->setMaximumHeight(140);
    outcomes_->setColumnWidth(0, 76);
    outcomes_->setColumnWidth(1, 100);
    outcomes_->setColumnWidth(2, 80);
    outcomes_->setColumnWidth(3, 170);
    ll->addWidget(outcomes_);
    ll->addWidget(makeLabel("Lock states", "sectionTitle"));
    locks_ = new QListWidget;
    locks_->setMinimumHeight(70);
    locks_->setFocusPolicy(Qt::NoFocus);
    ll->addWidget(locks_, 1);
    split->addWidget(left);

    auto* right = new QWidget;
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(8, 0, 0, 0);
    rl->setSpacing(6);
    rl->addWidget(makeLabel("Thread execution order", "sectionTitle"));
    sequence_ = makeTable({"#", "Time", "Thread", "Lock", "What happened"});
    sequence_->setColumnWidth(0, 34);
    sequence_->setColumnWidth(1, 90);
    sequence_->setColumnWidth(2, 76);
    sequence_->setColumnWidth(3, 76);
    rl->addWidget(sequence_, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 5);
    split->setStretchFactor(1, 6);
    split->setMinimumHeight(330);
    root->addWidget(split, 1);

    // ---- explanation
    root->addWidget(makeLabel("What happened?", "sectionTitle"));
    explanation_ = new QTextBrowser;
    explanation_->setMinimumHeight(110);
    explanation_->setMaximumHeight(170);
    root->addWidget(explanation_);

    clear();
}

void LabResultView::resetContent() {
    balances_->setRowCount(0);
    threads_->setRowCount(0);
    outcomes_->setRowCount(0);
    sequence_->setRowCount(0);
    locks_->clear();
    threadRow_.clear();
    lockInfo_.clear();
    snapshotLines_.clear();
    noLockMode_ = false;
}

void LabResultView::clear() {
    resetContent();
    running_ = false;
    verdict_->clearBadge();
    timing_->setText(QString());
    explanation_->setPlainText("Press the Run button to start this demonstration. Thread states, lock states and the execution "
                               "order appear here live while the threads run.");
}

void LabResultView::beginRun() {
    resetContent();
    running_ = true;
    verdict_->setBadge("RUNNING...", theme::kBlue, theme::kBlueBg);
    timing_->setText(QString());
    explanation_->setPlainText("Threads are running. Watch the thread states (blue = executing, yellow = waiting for a lock, "
                               "red = blocked / failed, green = completed).");
}

void LabResultView::liveEvent(const Event& e) {
    if (!running_) return;  // the final report replaces the live view
    applyEvent(e);
    refreshLocks();
}

void LabResultView::applyEvent(const Event& e) {
    // ---- thread table
    auto it = threadRow_.find(e.threadLabel);
    int row;
    if (it == threadRow_.end()) {
        row = threads_->rowCount();
        threads_->insertRow(row);
        threadRow_[e.threadLabel] = row;
        threads_->setItem(row, 0, makeCell(qs(e.threadLabel), QColor(), QColor(), Qt::AlignVCenter | Qt::AlignLeft, true));
        threads_->setItem(row, 1, makeCell(qs(e.customerName)));
        threads_->setItem(row, 2, makeCell(qs(e.channel)));
        threads_->setItem(row, 3, new QTableWidgetItem);
        threads_->setItem(row, 4, new QTableWidgetItem);
    } else {
        row = it->second;
    }
    threads_->setItem(row, 3, makeCell(toString(e.threadState), theme::stateColor(e.threadState), theme::stateBg(e.threadState),
                                       Qt::AlignVCenter | Qt::AlignLeft, true));
    threads_->setItem(row, 4, makeCell(qs(e.message)));

    // ---- lock tracking
    if (!e.accountId.empty() && e.lockState != LockState::None) {
        auto& li = lockInfo_[e.accountId];
        auto removeWaiter = [&] {
            li.waiters.erase(std::remove(li.waiters.begin(), li.waiters.end(), e.threadLabel), li.waiters.end());
        };
        switch (e.lockState) {
            case LockState::Waiting:
                if (std::find(li.waiters.begin(), li.waiters.end(), e.threadLabel) == li.waiters.end())
                    li.waiters.push_back(e.threadLabel);
                break;
            case LockState::Acquired:
                removeWaiter();
                li.holder = e.threadLabel;
                break;
            case LockState::Released:
                if (li.holder == e.threadLabel) li.holder.clear();
                break;
            case LockState::TimedOut:
                removeWaiter();
                break;
            case LockState::None: break;
        }
    }
    if (e.source == "lab:race") noLockMode_ = true;

    // ---- sequence table
    const int sr = sequence_->rowCount();
    sequence_->insertRow(sr);
    QColor fg = theme::stateColor(e.threadState);
    QColor bg;
    if (e.threadState == ThreadState::Waiting) bg = theme::kYellowBg;
    else if (e.threadState == ThreadState::Blocked || e.lockState == LockState::TimedOut) bg = theme::kRedBg;
    sequence_->setItem(sr, 0, makeCell(QString::number(sr + 1), QColor(), bg));
    sequence_->setItem(sr, 1, makeCell(qs(formatClock(e.timestampMs)), QColor(), bg));
    sequence_->setItem(sr, 2, makeCell(qs(e.threadLabel), fg, bg, Qt::AlignVCenter | Qt::AlignLeft, true));
    sequence_->setItem(sr, 3, makeCell(e.lockState == LockState::None ? QString("-") : QString(toString(e.lockState)), fg, bg));
    sequence_->setItem(sr, 4, makeCell(qs(e.message), fg, bg));
    sequence_->scrollToBottom();
}

void LabResultView::refreshLocks() {
    locks_->clear();
    if (noLockMode_ && lockInfo_.empty()) {
        auto* it = new QListWidgetItem("NO LOCK - the shared balance is completely unprotected");
        it->setForeground(theme::kRed);
        it->setBackground(theme::kRedBg);
        locks_->addItem(it);
    }
    for (const auto& kv : lockInfo_) {
        QString text = qs(kv.first) + ":  ";
        QColor fg = theme::kGreen, bg = theme::kGreenBg;
        if (!kv.second.holder.empty()) {
            text += "LOCKED by " + qs(kv.second.holder);
            fg = theme::kBlue;
            bg = theme::kBlueBg;
        } else {
            text += "free";
        }
        if (!kv.second.waiters.empty()) {
            QStringList w;
            for (const auto& s : kv.second.waiters) w << qs(s);
            text += "   |   WAITING: " + w.join(", ");
            fg = theme::kYellow;
            bg = theme::kYellowBg;
        }
        auto* it = new QListWidgetItem(text);
        it->setForeground(fg);
        it->setBackground(bg);
        locks_->addItem(it);
    }
    for (const auto& line : snapshotLines_) {
        auto* it = new QListWidgetItem("at deadlock:  " + qs(line));
        it->setForeground(theme::kRed);
        it->setBackground(theme::kRedBg);
        locks_->addItem(it);
    }
}

void LabResultView::setReport(const LabReport& r) {
    resetContent();
    running_ = false;

    // authoritative replay of the recorded execution
    snapshotLines_ = r.lockStates;
    if (r.kind == LabKind::Race) noLockMode_ = true;
    for (const auto& e : r.events) applyEvent(e);
    // the "at deadlock" snapshot is only meaningful for the deadlock demo itself
    if (r.kind != LabKind::Deadlock) snapshotLines_.clear();
    refreshLocks();

    // verdict
    if (r.problemDetected) verdict_->setBadge(qs(r.verdict), theme::kRed, theme::kRedBg);
    else if (r.correct) verdict_->setBadge(qs(r.verdict), theme::kGreen, theme::kGreenBg);
    else verdict_->setBadge(qs(r.verdict), theme::kYellow, theme::kYellowBg);
    timing_->setText(QString("finished in %1 ms").arg(r.elapsedMs));

    // balances
    for (const auto& a : r.accounts) {
        const int row = balances_->rowCount();
        balances_->insertRow(row);
        const bool match = a.actual == a.expected;
        const QColor fg = match ? theme::kGreen : theme::kRed;
        const QColor bg = match ? theme::kGreenBg : theme::kRedBg;
        balances_->setItem(row, 0, makeCell(qs(a.id), QColor(), QColor(), Qt::AlignVCenter | Qt::AlignLeft, true));
        balances_->setItem(row, 1, makeCell(qmoney(a.initial)));
        balances_->setItem(row, 2, makeCell(qmoney(a.expected), theme::kGreen, QColor(), Qt::AlignVCenter | Qt::AlignLeft, true));
        balances_->setItem(row, 3, makeCell(qmoney(a.actual), fg, bg, Qt::AlignVCenter | Qt::AlignLeft, true));
        QString check = match ? "MATCH" : "MISMATCH";
        if (!match && r.kind == LabKind::Deadlock) check = "NOT APPLIED (deadlock)";
        balances_->setItem(row, 4, makeCell(check, fg, bg, Qt::AlignVCenter | Qt::AlignLeft, true));
    }
    if (r.overdrawn > 0) {
        const int row = balances_->rowCount();
        balances_->insertRow(row);
        balances_->setItem(row, 0, makeCell("Money paid out that never existed", theme::kRed, theme::kRedBg, Qt::AlignVCenter | Qt::AlignLeft, true));
        balances_->setSpan(row, 0, 1, 3);
        balances_->setItem(row, 3, makeCell(qmoney(r.overdrawn), theme::kRed, theme::kRedBg, Qt::AlignVCenter | Qt::AlignLeft, true));
        balances_->setItem(row, 4, makeCell("RACE LOSS", theme::kRed, theme::kRedBg, Qt::AlignVCenter | Qt::AlignLeft, true));
    }

    // outcomes
    for (const auto& o : r.outcomes) {
        const int row = outcomes_->rowCount();
        outcomes_->insertRow(row);
        const QColor fg = o.success ? theme::kGreen : theme::kRed;
        const QColor bg = o.success ? theme::kGreenBg : theme::kRedBg;
        outcomes_->setItem(row, 0, makeCell(qs(o.threadLabel), QColor(), QColor(), Qt::AlignVCenter | Qt::AlignLeft, true));
        outcomes_->setItem(row, 1, makeCell(qs(o.customerName)));
        outcomes_->setItem(row, 2, makeCell(qs(o.channel)));
        outcomes_->setItem(row, 3, makeCell(qs(o.operation)));
        outcomes_->setItem(row, 4, makeCell(qs(o.result), fg, bg, Qt::AlignVCenter | Qt::AlignLeft, true));
    }

    // explanation
    QString html;
    if (!r.circularWait.empty()) html += "<p><b>Circular wait:</b><br><span style='color:#b91c1c'>" + qs(r.circularWait).toHtmlEscaped() + "</span></p>";
    const QStringList paras = qs(r.explanation).split("\n\n", Qt::SkipEmptyParts);
    for (const auto& p : paras) html += "<p>" + p.toHtmlEscaped().replace("\n", "<br>") + "</p>";
    explanation_->setHtml(html);
}
