#include "event_model.h"

#include <QBrush>
#include <QColor>
#include <QFont>

#include "theme.h"
#include "widgets.h"

int EventModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(events_.size());
}

int EventModel::columnCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : ColumnCount; }

QVariant EventModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole) return {};
    static const char* names[ColumnCount] = {"Time",    "Thread",       "Customer",   "Channel", "Type",
                                             "Account", "Thread state", "Lock state", "Status",  "Event"};
    return names[section];
}

QVariant EventModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(events_.size())) return {};
    const banksim::Event& e = events_[static_cast<std::size_t>(index.row())];
    switch (role) {
        case Qt::DisplayRole:
            switch (index.column()) {
                case Time: return qs(banksim::formatClock(e.timestampMs));
                case Thread: return qs(e.threadLabel);
                case Customer: return qs(e.customerName.empty() ? e.customerId : e.customerName);
                case Channel: return qs(e.channel);
                case Type: return qs(e.txType);
                case Account: return qs(e.accountId);
                case ThreadState: return banksim::toString(e.threadState);
                case LockState: return banksim::toString(e.lockState);
                case Status: return e.status == banksim::TxStatus::Pending ? QString("In progress") : QString(banksim::toString(e.status));
                case Message: return qs(e.message);
            }
            break;
        case Qt::ToolTipRole:
            return qs(e.message);
        case Qt::BackgroundRole:
            if (index.column() == ThreadState) return QBrush(theme::stateBg(e.threadState));
            if (index.column() == Status && e.status != banksim::TxStatus::Pending) return QBrush(theme::statusBg(e.status));
            if (index.column() == LockState && e.lockState == banksim::LockState::Waiting) return QBrush(theme::kYellowBg);
            if (index.column() == LockState && e.lockState == banksim::LockState::TimedOut) return QBrush(theme::kRedBg);
            break;
        case Qt::ForegroundRole:
            if (index.column() == ThreadState) return QBrush(theme::stateColor(e.threadState));
            if (index.column() == Status && e.status != banksim::TxStatus::Pending) return QBrush(theme::statusColor(e.status));
            if (index.column() == LockState && e.lockState == banksim::LockState::Waiting) return QBrush(theme::kYellow);
            if (index.column() == LockState && e.lockState == banksim::LockState::TimedOut) return QBrush(theme::kRed);
            if (index.column() == LockState && e.lockState == banksim::LockState::Acquired) return QBrush(theme::kBlue);
            break;
        case Qt::FontRole:
            if (index.column() == ThreadState || index.column() == Status) {
                QFont f;
                f.setBold(true);
                return f;
            }
            break;
    }
    return {};
}

void EventModel::append(const std::vector<banksim::Event>& batch) {
    if (batch.empty()) return;
    // Keep memory bounded: drop the oldest rows once the cap is exceeded.
    const int overflow = static_cast<int>(events_.size() + batch.size()) - kMaxRows;
    if (overflow > 0 && !events_.empty()) {
        const int drop = std::min<int>(static_cast<int>(events_.size()), overflow + kMaxRows / 6);
        beginRemoveRows({}, 0, drop - 1);
        events_.erase(events_.begin(), events_.begin() + drop);
        endRemoveRows();
    }
    const int first = static_cast<int>(events_.size());
    beginInsertRows({}, first, first + static_cast<int>(batch.size()) - 1);
    events_.insert(events_.end(), batch.begin(), batch.end());
    endInsertRows();
}

void EventModel::clear() {
    beginResetModel();
    events_.clear();
    endResetModel();
}

bool EventFilterProxy::filterAcceptsRow(int row, const QModelIndex&) const {
    const auto* m = static_cast<const EventModel*>(sourceModel());
    const banksim::Event& e = m->eventAt(row);
    auto has = [](const std::string& hay, const QString& needle) {
        return QString::fromStdString(hay).contains(needle, Qt::CaseInsensitive);
    };
    if (!customer_.isEmpty() && !has(e.customerId, customer_) && !has(e.customerName, customer_)) return false;
    if (!account_.isEmpty() && !has(e.accountId, account_)) return false;
    if (!thread_.isEmpty() && !has(e.threadLabel, thread_) && !has(e.threadId, thread_)) return false;
    switch (status_) {
        case OnlySuccess: if (e.status != banksim::TxStatus::Success) return false; break;
        case OnlyFailed: if (e.status == banksim::TxStatus::Success || e.status == banksim::TxStatus::Pending) return false; break;
        case OnlyInProgress: if (e.status != banksim::TxStatus::Pending) return false; break;
        case AllStatuses: break;
    }
    const bool lab = e.source.rfind("lab:", 0) == 0;
    if (source_ == MainOnly && lab) return false;
    if (source_ == LabsOnly && !lab) return false;
    return true;
}
