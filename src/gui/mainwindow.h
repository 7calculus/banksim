// mainwindow.h - application shell: sidebar navigation, the five screens, and the glue that
// moves data from the Backend's worker threads to the pages on the GUI thread.
#pragma once

#include <QMainWindow>

#include "backend.h"

class AccountsPage;
class DashboardPage;
class LabPage;
class MonitorPage;
class SimulatorPage;
class QButtonGroup;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Developer aid used for documentation / smoke testing: drives every screen through its
    // main function and saves a screenshot of each into `dir`. Returns false on failure.
    bool runScreenshotTour(const QString& dir);

protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onActivity();
    void onTick();
    void onResetFinished();
    void goTo(int page);

private:
    void refreshCurrentPage();
    void refreshStatus();

    Backend backend_;

    QStackedWidget* stack_;
    QButtonGroup* navGroup_;
    QLabel *sideMode_, *statusLabel_;
    QPushButton* sideReset_;
    QTimer* timer_;

    DashboardPage* dashboard_;
    AccountsPage* accounts_;
    SimulatorPage* simulator_;
    LabPage* lab_;
    MonitorPage* monitor_;
};
