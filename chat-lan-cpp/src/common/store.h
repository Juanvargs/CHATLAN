#pragma once
#include <QJsonObject>
#include <QList>
#include <QVariant>
#include <sqlite3.h>

namespace chat {
class Store {
    sqlite3 *db = nullptr;
    QList<QJsonObject> query(const QString &sql, const QVariantList &params = {});
    void execute(const QString &sql, const QVariantList &params = {});
    void transaction(const std::function<void()> &action);
    void deleteRow(const QJsonObject &row);
public:
    explicit Store(const QString &path);
    ~Store();
    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;
    QJsonObject registerClient(const QString &publicKey, const QString &name, bool autoApprove = true);
    QList<QJsonObject> clients();
    QJsonObject get(const QString &id);
    void setOnline(const QString &id, bool online);
    void resetOnline();
    void setStatus(const QString &id, const QString &status);
    void disconnectClient(const QString &id);
    void clearDisconnect(const QString &id);
    void deleteClient(const QString &id);
    void finalizeDelete(const QString &id);
    void recover(const QString &id, const QString &publicKey);
    bool saveMessage(const QString &id, const QString &sender, const QString &target, const QString &text, const QString &time);
    QList<QJsonObject> history(const QString &id, int limit = 200);
    int messageCount();
    void backup(const QString &destination);
};
}
