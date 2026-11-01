// monitor_page.h - Screen 5: Live Thread Monitor.
#pragma once

#include <QWidget>
#include <vector>

#include "event_log.h"

class Backend;
class EventModel;
class EventFilterProxy;
class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QTableView;

class MonitorPage : public QWidget {
    Q_OBJECT
public:
    explicit MonitorPage(Backend& backend, QWidget* parent = nullptr);
    void appendEvents(const std::vector<banksim::Event>& batch);
    void clearEvents();

private slots:
    void exportCsv();
    void updateCount();

private:
    Backend& backend_;
    EventModel* model_;
    EventFilterProxy* proxy_;
    QTableView* view_;
    QLineEdit *customerEdit_, *accountEdit_, *threadEdit_;
    QComboBox *statusCombo_, *sourceCombo_;
    QCheckBox* autoScroll_;
    QLabel* countLabel_;
};
