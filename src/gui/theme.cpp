#include "theme.h"

#include <QApplication>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>

namespace theme {

QString css(const QColor& c) { return c.name(QColor::HexRgb); }

QColor stateColor(banksim::ThreadState s) {
    using banksim::ThreadState;
    switch (s) {
        case ThreadState::Executing: return kBlue;
        case ThreadState::Waiting: return kYellow;
        case ThreadState::Blocked: return kRed;
        case ThreadState::Completed: return kGreen;
        case ThreadState::Failed: return kRed;
        case ThreadState::Created: return kGrey;
    }
    return kGrey;
}

QColor stateBg(banksim::ThreadState s) {
    using banksim::ThreadState;
    switch (s) {
        case ThreadState::Executing: return kBlueBg;
        case ThreadState::Waiting: return kYellowBg;
        case ThreadState::Blocked: return kRedBg;
        case ThreadState::Completed: return kGreenBg;
        case ThreadState::Failed: return kRedBg;
        case ThreadState::Created: return kGreyBg;
    }
    return kGreyBg;
}

QColor statusColor(banksim::TxStatus s) {
    if (s == banksim::TxStatus::Success) return kGreen;
    if (s == banksim::TxStatus::Pending) return kBlue;
    return kRed;
}

QColor statusBg(banksim::TxStatus s) {
    if (s == banksim::TxStatus::Success) return kGreenBg;
    if (s == banksim::TxStatus::Pending) return kBlueBg;
    return kRedBg;
}

void apply(QApplication& app) {
    app.setStyle(QStyleFactory::create("Fusion"));

    QFont f = app.font();
    f.setFamily("Segoe UI");
    f.setStyleHint(QFont::SansSerif);
    f.setPointSize(10);
    app.setFont(f);

    QPalette p;
    p.setColor(QPalette::Window, QColor("#f1f5f9"));
    p.setColor(QPalette::WindowText, QColor("#0f172a"));
    p.setColor(QPalette::Base, Qt::white);
    p.setColor(QPalette::AlternateBase, QColor("#f8fafc"));
    p.setColor(QPalette::Text, QColor("#0f172a"));
    p.setColor(QPalette::Button, QColor("#e2e8f0"));
    p.setColor(QPalette::ButtonText, QColor("#0f172a"));
    p.setColor(QPalette::Highlight, QColor("#2563eb"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, QColor("#0f172a"));
    p.setColor(QPalette::ToolTipText, Qt::white);
    app.setPalette(p);

    app.setStyleSheet(R"QSS(
QMainWindow, QWidget#pageRoot { background: #f1f5f9; }
QToolTip { background: #0f172a; color: white; border: 0; padding: 4px 6px; }

/* ---------- sidebar ---------- */
QFrame#sidebar { background: #0f172a; }
QLabel#brandTitle { color: white; font-size: 22px; font-weight: 700; }
QLabel#brandSub { color: #94a3b8; font-size: 11px; }
QPushButton#navButton {
    color: #cbd5e1; background: transparent; border: 0; border-radius: 8px;
    text-align: left; padding: 11px 14px; font-size: 13px; }
QPushButton#navButton:hover { background: #1e293b; color: white; }
QPushButton#navButton:checked { background: #2563eb; color: white; font-weight: 600; }
QLabel#sideNote { color: #94a3b8; font-size: 11px; }
QLabel#sideMode { color: #86efac; font-size: 11px; font-weight: 600; }
QPushButton#sideReset {
    color: #fecaca; background: #1e293b; border: 1px solid #334155; border-radius: 8px; padding: 9px; }
QPushButton#sideReset:hover { background: #7f1d1d; color: white; }

/* ---------- cards ---------- */
QFrame#card { background: white; border: 1px solid #e2e8f0; border-radius: 12px; }
QFrame#statCard { background: white; border: 1px solid #e2e8f0; border-radius: 12px; }
QLabel#statTitle { color: #64748b; font-size: 11px; font-weight: 600; }
QLabel#statValue { color: #0f172a; font-size: 24px; font-weight: 700; }
QLabel#statSub { color: #94a3b8; font-size: 10px; }
QLabel#pageTitle { color: #0f172a; font-size: 22px; font-weight: 700; }
QLabel#pageSub { color: #64748b; font-size: 12px; }
QLabel#sectionTitle { color: #0f172a; font-size: 14px; font-weight: 700; }
QLabel#hint { color: #64748b; font-size: 11px; }

/* ---------- inputs ---------- */
QLineEdit, QComboBox, QSpinBox, QDoubleSpinBox {
    background: white; border: 1px solid #cbd5e1; border-radius: 6px; padding: 5px 8px; min-height: 22px; }
QLineEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid #2563eb; }
QComboBox QAbstractItemView { background: white; selection-background-color: #dbeafe; selection-color: #0f172a; }
QComboBox::drop-down { border: 0; width: 22px; }
QCheckBox { spacing: 6px; }
QGroupBox { background: white; border: 1px solid #e2e8f0; border-radius: 12px; margin-top: 14px; padding: 14px 12px 10px 12px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 14px; padding: 0 6px; color: #0f172a; }

/* ---------- buttons ---------- */
QPushButton { background: #e2e8f0; border: 0; border-radius: 8px; padding: 8px 14px; font-weight: 600; color: #0f172a; }
QPushButton:hover { background: #cbd5e1; }
QPushButton:disabled { background: #eef2f7; color: #94a3b8; }
QPushButton[kind="primary"] { background: #2563eb; color: white; }
QPushButton[kind="primary"]:hover { background: #1d4ed8; }
QPushButton[kind="primary"]:disabled { background: #93c5fd; color: white; }
QPushButton[kind="danger"] { background: #fee2e2; color: #991b1b; }
QPushButton[kind="danger"]:hover { background: #fecaca; }
QPushButton[kind="success"] { background: #16a34a; color: white; }
QPushButton[kind="success"]:hover { background: #15803d; }
QPushButton[kind="success"]:disabled { background: #86efac; color: white; }

/* ---------- tables ---------- */
QTableView, QTableWidget, QListWidget, QTextBrowser {
    background: white; alternate-background-color: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px;
    gridline-color: #eef2f7; selection-background-color: #dbeafe; selection-color: #0f172a; }
QHeaderView::section { background: #f1f5f9; color: #475569; border: 0; border-bottom: 1px solid #e2e8f0;
    padding: 7px 8px; font-weight: 700; font-size: 11px; }
QTableView::item { padding: 3px 6px; }

/* ---------- tabs ---------- */
QTabWidget::pane { border: 0; top: -1px; }
QTabBar::tab { background: transparent; color: #64748b; padding: 10px 18px; margin-right: 4px;
    border-bottom: 3px solid transparent; font-weight: 600; }
QTabBar::tab:selected { color: #2563eb; border-bottom: 3px solid #2563eb; }
QTabBar::tab:hover { color: #0f172a; }

QProgressBar { background: #e2e8f0; border: 0; border-radius: 6px; text-align: center; height: 14px; }
QProgressBar::chunk { background: #2563eb; border-radius: 6px; }
QScrollBar:vertical { background: transparent; width: 10px; }
QScrollBar::handle:vertical { background: #cbd5e1; border-radius: 5px; min-height: 30px; }
QScrollBar:horizontal { background: transparent; height: 10px; }
QScrollBar::handle:horizontal { background: #cbd5e1; border-radius: 5px; min-width: 30px; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QStatusBar { background: white; color: #475569; border-top: 1px solid #e2e8f0; }
QSplitter::handle { background: transparent; }
)QSS");
}

}  // namespace theme
