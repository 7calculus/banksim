// simulator_page.h - Screen 3: Banking Simulator.
#pragma once

#include <QWidget>
#include <memory>
#include <vector>

#include "stress_test.h"
#include "transaction.h"

class Backend;
class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;

class SimulatorPage : public QWidget {
    Q_OBJECT
public:
    explicit SimulatorPage(Backend& backend, QWidget* parent = nullptr);

    void reloadChoices();  // refill customer / account combo boxes from the bank
    void refresh();        // balances, button states
    void appendRecords(const std::vector<banksim::TxRecord>& records);

    // Preloaded demo: Customer A withdraws Rs 7,000 at ATM 1 while Customer B withdraws
    // Rs 6,000 at ATM 2 from the joint account ACC1001 (resets the simulation first if needed).
    void loadJointDemo();
    void runJointDemo();

public slots:
    void onStressFinished(const std::shared_ptr<banksim::StressReport>& rep);
    void onResetFinished();
    void startStress();

private slots:
    void updateAccountChoices();
    void onTypeChanged();
    void executeOne();
    void addToBatch();
    void executeConcurrently();
    void removeBatchItem();
    void clearBatch();

private:
    banksim::TxRequest buildRequest() const;
    void refreshBatchTable();
    void applyEngineSettings();
    QString describe(const banksim::TxRequest& r) const;

    Backend& backend_;

    QComboBox *customer_, *account_, *channel_, *type_, *dest_;
    QSpinBox *amount_, *threads_, *delay_, *stressThreads_, *stressOps_;
    QCheckBox *showAll_, *ordered_, *atmCash_;
    QPushButton *execBtn_, *addBtn_, *concBtn_, *demoBtn_, *stressBtn_, *cancelBtn_, *resetBtn_, *removeBtn_, *clearBtn_;
    QTableWidget *batchTable_, *results_, *balances_;
    QLabel *stressResult_, *batchHint_, *atmLabel_;

    std::vector<banksim::TxRequest> batch_;
    bool pendingDemo_ = false;
};
