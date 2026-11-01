// event_model.h - table model + filter proxy behind the Live Thread Monitor.
#pragma once

#include <QAbstractTableModel>
#include <QSortFilterProxyModel>
#include <vector>

#include "event_log.h"

class EventModel : public QAbstractTableModel {
    Q_OBJECT
public:
    enum Column { Time, Thread, Customer, Channel, Type, Account, ThreadState, LockState, Status, Message, ColumnCount };

    explicit EventModel(QObject* parent = nullptr) : QAbstractTableModel(parent) {}

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    void append(const std::vector<banksim::Event>& batch);
    void clear();
    const banksim::Event& eventAt(int row) const { return events_[static_cast<std::size_t>(row)]; }
    int size() const { return static_cast<int>(events_.size()); }

    static constexpr int kMaxRows = 60000;

private:
    std::vector<banksim::Event> events_;
};

class EventFilterProxy : public QSortFilterProxyModel {
    Q_OBJECT
public:
    enum StatusFilter { AllStatuses, OnlySuccess, OnlyFailed, OnlyInProgress };
    enum SourceFilter { AllSources, MainOnly, LabsOnly };

    explicit EventFilterProxy(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}
    void setCustomer(const QString& t) { customer_ = t.trimmed(); invalidateFilter(); }
    void setAccount(const QString& t) { account_ = t.trimmed(); invalidateFilter(); }
    void setThread(const QString& t) { thread_ = t.trimmed(); invalidateFilter(); }
    void setStatusFilter(StatusFilter s) { status_ = s; invalidateFilter(); }
    void setSourceFilter(SourceFilter s) { source_ = s; invalidateFilter(); }

protected:
    bool filterAcceptsRow(int row, const QModelIndex& parent) const override;

private:
    QString customer_, account_, thread_;
    StatusFilter status_ = AllStatuses;
    SourceFilter source_ = AllSources;
};
