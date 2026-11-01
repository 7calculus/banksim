// lab_page.h - Screen 4: Concurrency Lab (race condition, synchronization, deadlock,
// deadlock prevention) plus the automated test runner.
#pragma once

#include <QWidget>
#include <memory>
#include <vector>

#include "concurrency_lab.h"
#include "deadlock_simulator.h"

class Backend;
class LabResultView;
class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

// Side-by-side comparison of two lab results (unsafe vs safe, deadlock vs prevention).
class ComparePanel : public QWidget {
    Q_OBJECT
public:
    ComparePanel(const QString& title, const QString& leftName, const QString& rightName, QWidget* parent = nullptr);
    void setReports(const banksim::LabReport* left, const banksim::LabReport* right);

private:
    QTableWidget* table_;
    QString leftName_, rightName_;
};

class LabPage : public QWidget {
    Q_OBJECT
public:
    enum Tab { RaceTab, SyncTab, DeadlockTab, PreventionTab, TestsTab };

    explicit LabPage(Backend& backend, QWidget* parent = nullptr);

    void showTab(int tab);
    bool hasReport(banksim::LabKind kind) const;
    int testRowCount() const;
    void onEvents(const std::vector<banksim::Event>& events);
    void refreshButtons();

    // public so the dashboard / tour can trigger them
    void runRace(bool thenSynchronized);
    void runSync();
    void runDeadlock(bool thenPrevention);
    void runPrevention();
    void runTests();

public slots:
    void onLabStarted(int kind);
    void onLabFinished(const std::shared_ptr<banksim::LabReport>& report);
    void onTestProgress(int number, const QString& name, bool passed, const QString& detail, qlonglong ms);
    void onTestsFinished(int passed, int total);

private:
    banksim::RaceConfig raceConfig() const;
    banksim::SyncConfig syncConfig() const;
    banksim::DeadlockConfig deadlockConfig(bool ordered) const;
    void applyPreset(int index);
    void updateComparisons();

    Backend& backend_;
    QTabWidget* tabs_;
    LabResultView *raceView_, *syncView_, *deadView_, *prevView_;
    ComparePanel *raceCompare_, *syncCompare_, *deadCompare_, *prevCompare_;

    // race / sync configuration
    QComboBox *preset_, *opA_, *opB_;
    QSpinBox *initial_, *amountA_, *amountB_, *stepDelay_, *processing_;
    // deadlock configuration
    QSpinBox *balA_, *balB_, *amtAB_, *amtBA_, *timeout_, *hold_;

    QPushButton *runRaceBtn_, *runBothBtn_, *runSyncBtn_, *runDeadBtn_, *runDeadBothBtn_, *runPrevBtn_, *runTestsBtn_;

    // tests tab
    QTableWidget* testTable_;
    QProgressBar* testProgress_;
    QLabel* testSummary_;

    std::shared_ptr<banksim::LabReport> lastRace_, lastSync_, lastDead_, lastPrev_;
    enum class Chain { None, SyncAfterRace, PreventionAfterDeadlock } chain_ = Chain::None;
};
