#include "monitor_page.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>

#include "backend.h"
#include "event_model.h"
#include "widgets.h"

MonitorPage::MonitorPage(Backend& backend, QWidget* parent) : QWidget(parent), backend_(backend) {
    setObjectName("pageRoot");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(28, 24, 28, 20);
    root->setSpacing(12);

    auto* head = new QHBoxLayout;
    auto* titles = new QVBoxLayout;
    titles->addWidget(makeLabel("Live Thread Monitor", "pageTitle"));
    titles->addWidget(makeLabel("Chronological log of every thread, lock and transaction. Updates in real time while worker threads run.", "pageSub"));
    head->addLayout(titles, 1);
    auto* clearBtn = makeButton("Clear log", "danger");
    auto* exportBtn = makeButton("Export CSV");
    head->addWidget(exportBtn);
    head->addWidget(clearBtn);
    root->addLayout(head);

    // ---- filters
    auto* fcard = makeCard();
    auto* fl = new QHBoxLayout(fcard);
    fl->setContentsMargins(14, 10, 14, 10);
    customerEdit_ = new QLineEdit;
    customerEdit_->setPlaceholderText("Customer (name or id)");
    accountEdit_ = new QLineEdit;
    accountEdit_->setPlaceholderText("Account (e.g. ACC1001)");
    threadEdit_ = new QLineEdit;
    threadEdit_->setPlaceholderText("Thread (e.g. Thread-02)");
    statusCombo_ = new QComboBox;
    statusCombo_->addItems({"All statuses", "Successful", "Failed / rejected", "In progress"});
    sourceCombo_ = new QComboBox;
    sourceCombo_->addItems({"Bank + labs", "Main bank only", "Concurrency labs only"});
    autoScroll_ = new QCheckBox("Auto-scroll");
    autoScroll_->setChecked(true);
    fl->addWidget(customerEdit_, 2);
    fl->addWidget(accountEdit_, 2);
    fl->addWidget(threadEdit_, 2);
    fl->addWidget(statusCombo_, 2);
    fl->addWidget(sourceCombo_, 2);
    fl->addWidget(autoScroll_);
    root->addWidget(fcard);

    // ---- table
    model_ = new EventModel(this);
    proxy_ = new EventFilterProxy(this);
    proxy_->setSourceModel(model_);
    view_ = new QTableView;
    view_->setModel(proxy_);
    view_->setAlternatingRowColors(true);
    view_->setSelectionBehavior(QAbstractItemView::SelectRows);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setShowGrid(false);
    view_->verticalHeader()->setVisible(false);
    view_->verticalHeader()->setDefaultSectionSize(26);
    view_->horizontalHeader()->setStretchLastSection(true);
    view_->horizontalHeader()->setHighlightSections(false);
    view_->setFocusPolicy(Qt::NoFocus);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSize(9);
    view_->setFont(mono);
    const int widths[EventModel::ColumnCount] = {96, 84, 100, 106, 104, 76, 92, 84, 130, 400};
    for (int c = 0; c < EventModel::ColumnCount; ++c) view_->setColumnWidth(c, widths[c]);
    root->addWidget(view_, 1);

    countLabel_ = makeLabel(QString(), "hint");
    root->addWidget(countLabel_);

    connect(customerEdit_, &QLineEdit::textChanged, this, [this](const QString& t) { proxy_->setCustomer(t); updateCount(); });
    connect(accountEdit_, &QLineEdit::textChanged, this, [this](const QString& t) { proxy_->setAccount(t); updateCount(); });
    connect(threadEdit_, &QLineEdit::textChanged, this, [this](const QString& t) { proxy_->setThread(t); updateCount(); });
    connect(statusCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        proxy_->setStatusFilter(static_cast<EventFilterProxy::StatusFilter>(i));
        updateCount();
    });
    connect(sourceCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int i) {
        proxy_->setSourceFilter(static_cast<EventFilterProxy::SourceFilter>(i));
        updateCount();
    });
    connect(clearBtn, &QPushButton::clicked, this, &MonitorPage::clearEvents);
    connect(exportBtn, &QPushButton::clicked, this, &MonitorPage::exportCsv);
    updateCount();
}

void MonitorPage::appendEvents(const std::vector<banksim::Event>& batch) {
    if (batch.empty()) return;
    model_->append(batch);
    if (autoScroll_->isChecked()) view_->scrollToBottom();
    updateCount();
}

void MonitorPage::clearEvents() {
    model_->clear();
    updateCount();
}

void MonitorPage::updateCount() {
    countLabel_->setText(QString("%1 events shown of %2 recorded").arg(proxy_->rowCount()).arg(model_->size()));
}

void MonitorPage::exportCsv() {
    const QString path = QFileDialog::getSaveFileName(this, "Export thread monitor", "banksim_events.csv", "CSV files (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export failed", "Could not write " + path);
        return;
    }
    QTextStream out(&f);
    out.setEncoding(QStringConverter::Utf8);
    auto esc = [](QString s) { return "\"" + s.replace("\"", "\"\"") + "\""; };
    for (int c = 0; c < EventModel::ColumnCount; ++c)
        out << (c ? "," : "") << esc(model_->headerData(c, Qt::Horizontal, Qt::DisplayRole).toString());
    out << "\n";
    for (int r = 0; r < proxy_->rowCount(); ++r) {
        for (int c = 0; c < EventModel::ColumnCount; ++c)
            out << (c ? "," : "") << esc(proxy_->data(proxy_->index(r, c), Qt::DisplayRole).toString());
        out << "\n";
    }
}
