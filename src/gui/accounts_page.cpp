#include "accounts_page.h"

#include <algorithm>
#include <map>

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QVBoxLayout>

#include "backend.h"
#include "theme.h"
#include "widgets.h"

namespace {
const QColor kJointBg(0xee, 0xf2, 0xff);
const QColor kJointFg(0x43, 0x38, 0xca);

QSpinBox* moneySpin(int value, QWidget* parent = nullptr) {
    auto* s = new QSpinBox(parent);
    s->setRange(0, 1000000000);
    s->setSingleStep(500);
    s->setPrefix(QString::fromUtf8("₹ "));
    s->setGroupSeparatorShown(true);
    s->setValue(value);
    return s;
}
}  // namespace

AccountsPage::AccountsPage(Backend& backend, QWidget* parent) : QWidget(parent), backend_(backend) {
    setObjectName("pageRoot");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(12);

    auto* head = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->addWidget(makeLabel("Customers & Accounts", "pageTitle"));
    titles->addWidget(makeLabel("Joint accounts (several customers, one account) are highlighted. Their holders contend for the same mutex.", "pageSub"));
    head->addLayout(titles, 1);
    root->addLayout(head);

    auto* bar = makeCard();
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(14, 10, 14, 10);
    search_ = new QLineEdit;
    search_->setPlaceholderText("Search by customer, account, type or holder...");
    search_->setClearButtonEnabled(true);
    bl->addWidget(search_, 1);
    auto* bCust = makeButton("+ New customer", "primary");
    auto* bAcct = makeButton("+ New account", "primary");
    auto* bHold = makeButton("Add joint holder");
    auto* bBal = makeButton("Set starting balance");
    bl->addWidget(bCust);
    bl->addWidget(bAcct);
    bl->addWidget(bHold);
    bl->addWidget(bBal);
    root->addWidget(bar);

    table_ = makeTable({"Customer ID", "Customer name", "Account number", "Account type", "Balance", "Account holders"});
    const int widths[6] = {100, 140, 130, 150, 120, 300};
    for (int c = 0; c < 6; ++c) table_->setColumnWidth(c, widths[c]);
    root->addWidget(table_, 1);
    summary_ = makeLabel(QString(), "hint");
    root->addWidget(summary_);

    connect(search_, &QLineEdit::textChanged, this, &AccountsPage::applyFilter);
    connect(bCust, &QPushButton::clicked, this, &AccountsPage::newCustomer);
    connect(bAcct, &QPushButton::clicked, this, &AccountsPage::newAccount);
    connect(bHold, &QPushButton::clicked, this, &AccountsPage::addHolder);
    connect(bBal, &QPushButton::clicked, this, &AccountsPage::setBalance);
    refresh();
}

void AccountsPage::refresh() {
    auto& bank = backend_.bank();
    const auto accounts = bank.accounts();
    const auto customers = bank.customers();

    struct Row {
        QString custId, custName, accNum, accType, balance, holders, accId;
        bool joint = false;
        banksim::Money bal = 0;
    };
    std::vector<Row> rows;
    std::map<std::string, bool> hasAccount;
    for (const auto& a : accounts) {
        QStringList names;
        for (const auto& n : a.holderNames) names << qs(n);
        for (std::size_t i = 0; i < a.holderIds.size(); ++i) {
            Row r;
            r.custId = qs(a.holderIds[i]);
            r.custName = qs(a.holderNames[i]);
            r.accNum = qs(a.number) + "  (" + qs(a.id) + ")";
            r.accType = qs(a.type) + (a.joint ? "  •  JOINT" : "  •  Individual");
            r.bal = a.balance;
            r.balance = qmoney(a.balance);
            r.holders = names.join("  +  ") + (a.joint ? QString("   (%1 holders)").arg(a.holderIds.size()) : QString());
            r.accId = qs(a.id);
            r.joint = a.joint;
            rows.push_back(r);
            hasAccount[a.holderIds[i]] = true;
        }
    }
    for (const auto& c : customers) {
        if (hasAccount.count(c.id)) continue;
        Row r;
        r.custId = qs(c.id);
        r.custName = qs(c.name);
        r.accNum = "-";
        r.accType = "No account yet";
        r.balance = "-";
        r.holders = "-";
        rows.push_back(r);
    }

    QString sig;
    for (const auto& r : rows) sig += r.custId + r.accNum + r.accType + r.balance + r.holders + "|";
    if (sig == lastSignature_) return;  // nothing visible changed: keep scroll position and selection
    lastSignature_ = sig;

    table_->setRowCount(0);
    for (const auto& r : rows) {
        const int row = table_->rowCount();
        table_->insertRow(row);
        const QColor bg = r.joint ? kJointBg : QColor();
        table_->setItem(row, 0, makeCell(r.custId, QColor(), bg));
        table_->setItem(row, 1, makeCell(r.custName, QColor(), bg, Qt::AlignVCenter | Qt::AlignLeft, true));
        table_->setItem(row, 2, makeCell(r.accNum, QColor(), bg));
        table_->setItem(row, 3, makeCell(r.accType, r.joint ? kJointFg : QColor(), bg, Qt::AlignVCenter | Qt::AlignLeft, r.joint));
        table_->setItem(row, 4, makeCell(r.balance, QColor(), bg, Qt::AlignVCenter | Qt::AlignRight, true));
        table_->setItem(row, 5, makeCell(r.holders, r.joint ? kJointFg : QColor(), bg));
    }
    summary_->setText(QString("%1 customers, %2 accounts, %3 joint account(s), total deposits %4")
                          .arg(customers.size())
                          .arg(accounts.size())
                          .arg(std::count_if(accounts.begin(), accounts.end(), [](const banksim::AccountView& a) { return a.joint; }))
                          .arg(qmoney(bank.totalDeposits())));
    applyFilter();
}

void AccountsPage::applyFilter() {
    const QString q = search_->text().trimmed();
    for (int r = 0; r < table_->rowCount(); ++r) {
        bool match = q.isEmpty();
        for (int c = 0; c < table_->columnCount() && !match; ++c) {
            const auto* it = table_->item(r, c);
            if (it && it->text().contains(q, Qt::CaseInsensitive)) match = true;
        }
        table_->setRowHidden(r, !match);
    }
}

// ----------------------------------------------------------------------------- dialogs

void AccountsPage::newCustomer() {
    auto& bank = backend_.bank();
    QDialog dlg(this);
    dlg.setWindowTitle("New customer");
    auto* form = new QFormLayout(&dlg);
    const int n = static_cast<int>(bank.customerCount());
    auto* id = new QLineEdit(n < 26 ? QString("CUST-%1").arg(QChar('A' + n)) : QString("CUST-%1").arg(n + 1));
    auto* name = new QLineEdit(n < 26 ? QString("Customer %1").arg(QChar('A' + n)) : QString("Customer %1").arg(n + 1));
    form->addRow("Customer ID", id);
    form->addRow("Name", name);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    std::string err;
    if (!bank.addCustomer(id->text().trimmed().toStdString(), name->text().trimmed().toStdString(), &err)) {
        QMessageBox::warning(this, "Cannot create customer", qs(err));
        return;
    }
    refresh();
    emit structureChanged();
}

void AccountsPage::newAccount() {
    auto& bank = backend_.bank();
    const auto customers = bank.customers();
    if (customers.empty()) {
        QMessageBox::information(this, "No customers", "Create a customer first.");
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("New account");
    dlg.setMinimumWidth(380);
    auto* form = new QFormLayout(&dlg);
    form->addRow("Account ID", new QLabel(qs(bank.nextAccountId())));
    auto* type = new QComboBox;
    type->addItems({"Savings", "Current"});
    form->addRow("Account type", type);
    auto* balance = moneySpin(10000);
    form->addRow("Starting balance", balance);
    auto* holders = new QListWidget;
    holders->setMaximumHeight(150);
    for (const auto& c : customers) {
        auto* it = new QListWidgetItem(qs(c.name) + "  (" + qs(c.id) + ")", holders);
        it->setFlags(it->flags() | Qt::ItemIsUserCheckable);
        it->setCheckState(Qt::Unchecked);
        it->setData(Qt::UserRole, qs(c.id));
    }
    form->addRow("Holders (tick 2+ for a joint account)", holders);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    std::vector<std::string> ids;
    for (int i = 0; i < holders->count(); ++i)
        if (holders->item(i)->checkState() == Qt::Checked) ids.push_back(holders->item(i)->data(Qt::UserRole).toString().toStdString());
    std::string err;
    if (!bank.addAccount("", type->currentText().toStdString(), balance->value(), ids, &err)) {
        QMessageBox::warning(this, "Cannot create account", qs(err));
        return;
    }
    refresh();
    emit structureChanged();
}

void AccountsPage::addHolder() {
    auto& bank = backend_.bank();
    const auto accounts = bank.accounts();
    const auto customers = bank.customers();
    if (accounts.empty() || customers.empty()) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Add joint holder");
    dlg.setMinimumWidth(380);
    auto* form = new QFormLayout(&dlg);
    auto* acct = new QComboBox;
    for (const auto& a : accounts) acct->addItem(qs(a.id) + "  -  " + qs(a.type) + "  " + qmoney(a.balance), qs(a.id));
    auto* cust = new QComboBox;
    for (const auto& c : customers) cust->addItem(qs(c.name) + "  (" + qs(c.id) + ")", qs(c.id));
    auto* perm = new QComboBox;
    perm->addItem("Full access (view, deposit, withdraw, transfer)", static_cast<int>(banksim::PermFull));
    perm->addItem("View + deposit only", static_cast<int>(banksim::PermView | banksim::PermDeposit));
    perm->addItem("View only", static_cast<int>(banksim::PermView));
    form->addRow("Account", acct);
    form->addRow("New holder", cust);
    form->addRow("Permissions", perm);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    std::string err;
    if (!bank.addHolder(acct->currentData().toString().toStdString(), cust->currentData().toString().toStdString(),
                        static_cast<unsigned>(perm->currentData().toInt()), &err)) {
        QMessageBox::warning(this, "Cannot add holder", qs(err));
        return;
    }
    refresh();
    emit structureChanged();
}

void AccountsPage::setBalance() {
    auto& bank = backend_.bank();
    const auto accounts = bank.accounts();
    if (accounts.empty()) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Set starting balance");
    dlg.setMinimumWidth(360);
    auto* form = new QFormLayout(&dlg);
    auto* acct = new QComboBox;
    for (const auto& a : accounts) acct->addItem(qs(a.id) + "  -  now " + qmoney(a.balance), qs(a.id));
    auto* amount = moneySpin(10000);
    form->addRow("Account", acct);
    form->addRow("Starting balance", amount);
    form->addRow(new QLabel("This balance is also what \"Reset Simulation\" restores."));
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    std::string err;
    if (!bank.setStartingBalance(acct->currentData().toString().toStdString(), amount->value(), &err)) {
        QMessageBox::warning(this, "Cannot set balance", qs(err));
        return;
    }
    refresh();
    emit structureChanged();
}
