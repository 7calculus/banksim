#include "widgets.h"

#include <QAbstractItemView>
#include <QHeaderView>
#include <QPushButton>
#include <QVBoxLayout>

#include "theme.h"

QString qs(const std::string& s) { return QString::fromStdString(s); }

QString qmoney(banksim::Money m) { return qs(banksim::formatMoney(m)); }

QFrame* makeCard(QWidget* parent) {
    auto* f = new QFrame(parent);
    f->setObjectName("card");
    return f;
}

QLabel* makeLabel(const QString& text, const char* objectName, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    if (objectName) l->setObjectName(objectName);
    return l;
}

QPushButton* makeButton(const QString& text, const char* kind, QWidget* parent) {
    auto* b = new QPushButton(text, parent);
    if (kind) b->setProperty("kind", kind);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}

QTableWidget* makeTable(const QStringList& headers, QWidget* parent) {
    auto* t = new QTableWidget(0, headers.size(), parent);
    t->setHorizontalHeaderLabels(headers);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setAlternatingRowColors(true);
    t->setShowGrid(false);
    t->verticalHeader()->setVisible(false);
    t->verticalHeader()->setDefaultSectionSize(30);
    t->horizontalHeader()->setHighlightSections(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setWordWrap(false);
    t->setFocusPolicy(Qt::NoFocus);
    return t;
}

QTableWidgetItem* makeCell(const QString& text, const QColor& fg, const QColor& bg, Qt::Alignment align, bool bold) {
    auto* it = new QTableWidgetItem(text);
    it->setTextAlignment(align);
    if (fg.isValid()) it->setForeground(fg);
    if (bg.isValid()) it->setBackground(bg);
    if (bold) {
        QFont f = it->font();
        f.setBold(true);
        it->setFont(f);
    }
    it->setToolTip(text);
    return it;
}

// ---------------------------------------------------------------- StatCard

StatCard::StatCard(const QString& title, QWidget* parent) : QFrame(parent) {
    setObjectName("statCard");
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(16, 12, 16, 12);
    lay->setSpacing(2);
    title_ = new QLabel(title.toUpper(), this);
    title_->setObjectName("statTitle");
    value_ = new QLabel("-", this);
    value_->setObjectName("statValue");
    sub_ = new QLabel(QString(), this);
    sub_->setObjectName("statSub");
    lay->addWidget(title_);
    lay->addWidget(value_);
    lay->addWidget(sub_);
}

void StatCard::setValue(const QString& v, const QColor& color) {
    value_->setText(v);
    if (color.isValid())
        value_->setStyleSheet(QString("color: %1;").arg(theme::css(color)));
    else
        value_->setStyleSheet(QString());
}

void StatCard::setSub(const QString& s) { sub_->setText(s); }

// ------------------------------------------------------------------- Badge

Badge::Badge(QWidget* parent) : QLabel(parent) {
    setAlignment(Qt::AlignCenter);
    clearBadge();
}

void Badge::setBadge(const QString& text, const QColor& fg, const QColor& bg) {
    setText(text);
    setStyleSheet(QString("background:%1; color:%2; border-radius:12px; padding:5px 14px; font-weight:700;")
                      .arg(theme::css(bg), theme::css(fg)));
    show();
}

void Badge::clearBadge() {
    setText("NOT RUN YET");
    setStyleSheet("background:#e2e8f0; color:#64748b; border-radius:12px; padding:5px 14px; font-weight:700;");
}
