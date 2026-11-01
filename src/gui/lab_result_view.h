// lab_result_view.h - shared panel that visualises one concurrency-lab run:
// verdict, initial/expected/actual balances, thread states, lock states, outcomes,
// the ordered execution sequence and a plain-language explanation.
#pragma once

#include <QWidget>
#include <map>
#include <string>
#include <vector>

#include "concurrency_lab.h"

class Badge;
class QLabel;
class QListWidget;
class QTableWidget;
class QTextBrowser;

class LabResultView : public QWidget {
    Q_OBJECT
public:
    explicit LabResultView(QWidget* parent = nullptr);

    void clear();                      // back to the empty "not run yet" state
    void beginRun();                   // live mode: events stream in while threads execute
    void liveEvent(const banksim::Event& e);
    void setReport(const banksim::LabReport& r);  // authoritative final result

private:
    struct LockInfo {
        std::string holder;
        std::vector<std::string> waiters;
    };
    void resetContent();
    void applyEvent(const banksim::Event& e);
    void refreshLocks();

    Badge* verdict_;
    QLabel* timing_;
    QTableWidget *balances_, *threads_, *outcomes_, *sequence_;
    QListWidget* locks_;
    QTextBrowser* explanation_;

    std::map<std::string, int> threadRow_;
    std::map<std::string, LockInfo> lockInfo_;
    std::vector<std::string> snapshotLines_;
    bool noLockMode_ = false;
    bool running_ = false;
};
