// accounts_page.h - Screen 2: Customers and Accounts.
#pragma once

#include <QString>
#include <QWidget>

class Backend;
class QLineEdit;
class QTableWidget;
class QLabel;

class AccountsPage : public QWidget {
    Q_OBJECT
public:
    explicit AccountsPage(Backend& backend, QWidget* parent = nullptr);
    void refresh();

signals:
    void structureChanged();  // customers / accounts were added or modified

private slots:
    void newCustomer();
    void newAccount();
    void addHolder();
    void setBalance();
    void applyFilter();

private:
    Backend& backend_;
    QLineEdit* search_;
    QTableWidget* table_;
    QLabel* summary_;
    QString lastSignature_;
};
