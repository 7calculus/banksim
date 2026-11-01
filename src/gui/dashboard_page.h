// dashboard_page.h - Screen 1: Main Dashboard.
#pragma once

#include <QWidget>

class Backend;
class StatCard;
class QTableWidget;

class DashboardPage : public QWidget {
    Q_OBJECT
public:
    explicit DashboardPage(Backend& backend, QWidget* parent = nullptr);
    void refresh();

signals:
    void runDemoRequested();
    void openLabRequested();
    void runTestsRequested();

private:
    Backend& backend_;
    StatCard *customers_, *accounts_, *deposits_, *threads_, *total_, *success_, *failed_, *mode_;
    QTableWidget* recent_;
};
