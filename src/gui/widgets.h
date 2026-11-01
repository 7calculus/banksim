// widgets.h - small reusable GUI building blocks.
#pragma once

#include <QColor>
#include <QFrame>
#include <QLabel>
#include <QString>
#include <QStringList>
#include <QTableWidget>

#include "types.h"

class QPushButton;

QString qs(const std::string& s);               // UTF-8 std::string -> QString
QString qmoney(banksim::Money m);               // "Rs 10,000" with the rupee sign

// A white rounded panel (object name "card", styled by the application style sheet).
QFrame* makeCard(QWidget* parent = nullptr);
QLabel* makeLabel(const QString& text, const char* objectName = nullptr, QWidget* parent = nullptr);
// kind: "primary", "danger", "success" or nullptr for the neutral style.
QPushButton* makeButton(const QString& text, const char* kind = nullptr, QWidget* parent = nullptr);
QTableWidget* makeTable(const QStringList& headers, QWidget* parent = nullptr);
QTableWidgetItem* makeCell(const QString& text, const QColor& fg = QColor(), const QColor& bg = QColor(),
                           Qt::Alignment align = Qt::AlignVCenter | Qt::AlignLeft, bool bold = false);

// Dashboard tile: small title, big value, optional sub-line.
class StatCard : public QFrame {
    Q_OBJECT
public:
    explicit StatCard(const QString& title, QWidget* parent = nullptr);
    void setValue(const QString& v, const QColor& color = QColor());
    void setSub(const QString& s);

private:
    QLabel* title_;
    QLabel* value_;
    QLabel* sub_;
};

// Coloured pill, e.g. "RACE CONDITION DETECTED".
class Badge : public QLabel {
    Q_OBJECT
public:
    explicit Badge(QWidget* parent = nullptr);
    void setBadge(const QString& text, const QColor& fg, const QColor& bg);
    void clearBadge();
};
