#include "store.h"
#include "paths.h"
#include "protocol.h"
#include <QFile>
#include <QFileInfo>
#include <QUuid>
#include <QSet>
#include <functional>
#include <memory>
#include <stdexcept>

namespace chat {
static void fail(sqlite3 *db) { throw std::runtime_error(sqlite3_errmsg(db)); }
Store::Store(const QString &path) {
    privateDir(QFileInfo(path).absolutePath());
    if (sqlite3_open(QFile::encodeName(path).constData(), &db) != SQLITE_OK) {
        auto error = QString::fromUtf8(sqlite3_errmsg(db)); sqlite3_close(db); db = nullptr; throw std::runtime_error(error.toStdString());
    }
    try {
        sqlite3_busy_timeout(db, 1000);
        execute("PRAGMA journal_mode=WAL"); execute("PRAGMA synchronous=FULL");
        execute("CREATE TABLE IF NOT EXISTS clients(code INTEGER PRIMARY KEY AUTOINCREMENT,id TEXT UNIQUE NOT NULL,public TEXT UNIQUE NOT NULL,name TEXT NOT NULL,status TEXT NOT NULL DEFAULT 'pending')");
        QSet<QString> columns; for (auto row : query("PRAGMA table_info(clients)")) columns.insert(row["name"].toString());
        for (auto pair : QList<QPair<QString, QString>>{{"online","INTEGER NOT NULL DEFAULT 0"},{"last_seen","TEXT"},{"disconnect_requested","INTEGER NOT NULL DEFAULT 0"},{"delete_requested","INTEGER NOT NULL DEFAULT 0"}})
            if (!columns.contains(pair.first)) execute("ALTER TABLE clients ADD COLUMN " + pair.first + " " + pair.second);
        execute("CREATE TABLE IF NOT EXISTS messages(seq INTEGER PRIMARY KEY AUTOINCREMENT,message_id TEXT NOT NULL,sender TEXT NOT NULL,target TEXT NOT NULL,text TEXT NOT NULL,time TEXT NOT NULL)");
        execute("CREATE INDEX IF NOT EXISTS message_operation ON messages(sender,message_id)");
        execute("CREATE TABLE IF NOT EXISTS deleted_clients(public TEXT PRIMARY KEY,id TEXT NOT NULL,code INTEGER NOT NULL,name TEXT NOT NULL,deleted_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)");
        execute("CREATE TABLE IF NOT EXISTS blocked_keys(public TEXT PRIMARY KEY,reason TEXT NOT NULL)");
        QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner);
    } catch (...) { sqlite3_close(db); db = nullptr; throw; }
}
Store::~Store() { if (db) sqlite3_close(db); }
QList<QJsonObject> Store::query(const QString &sql, const QVariantList &params) {
    sqlite3_stmt *raw = nullptr;
    if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &raw, nullptr) != SQLITE_OK) fail(db);
    std::unique_ptr<sqlite3_stmt, decltype(&sqlite3_finalize)> statement(raw, sqlite3_finalize);
    for (int i = 0; i < params.size(); ++i) {
        auto value = params[i]; int rc;
        if (value.isNull()) rc = sqlite3_bind_null(raw, i + 1);
        else if (value.metaType().id() == QMetaType::Int || value.metaType().id() == QMetaType::LongLong || value.metaType().id() == QMetaType::Bool) rc = sqlite3_bind_int64(raw, i + 1, value.toLongLong());
        else { auto bytes = value.toString().toUtf8(); rc = sqlite3_bind_text(raw, i + 1, bytes.constData(), bytes.size(), SQLITE_TRANSIENT); }
        if (rc != SQLITE_OK) fail(db);
    }
    QList<QJsonObject> rows; int result;
    while ((result = sqlite3_step(raw)) == SQLITE_ROW) {
        QJsonObject row;
        for (int i = 0; i < sqlite3_column_count(raw); ++i) {
            auto name = QString::fromUtf8(sqlite3_column_name(raw, i));
            switch (sqlite3_column_type(raw, i)) {
            case SQLITE_INTEGER: row[name] = qint64(sqlite3_column_int64(raw, i)); break;
            case SQLITE_NULL: row[name] = QJsonValue::Null; break;
            default: row[name] = QString::fromUtf8(reinterpret_cast<const char*>(sqlite3_column_text(raw, i)), sqlite3_column_bytes(raw, i)); break;
            }
        }
        rows.append(row);
    }
    if (result != SQLITE_DONE) fail(db);
    return rows;
}
void Store::execute(const QString &sql, const QVariantList &params) { query(sql, params); }
void Store::transaction(const std::function<void()> &action) {
    execute("BEGIN IMMEDIATE");
    try { action(); execute("COMMIT"); } catch (...) { sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr); throw; }
}
QList<QJsonObject> Store::clients() { return query("SELECT * FROM clients ORDER BY code"); }
QJsonObject Store::get(const QString &id) { auto rows = query("SELECT * FROM clients WHERE id=?", {id}); return rows.isEmpty() ? QJsonObject{} : rows.first(); }
QJsonObject Store::registerClient(const QString &publicKey, const QString &name, bool autoApprove) {
    fromBase64(publicKey, 32);
    if (name.trimmed().isEmpty() || name.trimmed().size() > 32) throw std::runtime_error("Nombre inválido (1–32 caracteres)");
    for (auto c : name) if (c.unicode() < 32) throw std::runtime_error("Nombre inválido");
    QJsonObject user;
    transaction([&] {
        auto deleted = query("SELECT * FROM deleted_clients WHERE public=?", {publicKey});
        if (!deleted.isEmpty()) { user = deleted.first(); user["status"] = "deleted"; return; }
        if (!query("SELECT 1 FROM blocked_keys WHERE public=?", {publicKey}).isEmpty()) { user = {{"status","revoked"}}; return; }
        auto rows = query("SELECT * FROM clients WHERE public=?", {publicKey});
        auto approved = query("SELECT count(*) AS n FROM clients WHERE status='approved'").first()["n"].toInt();
        if (!rows.isEmpty()) {
            user = rows.first();
            if (autoApprove && approved < 10 && user["status"] == "pending") { execute("UPDATE clients SET status='approved' WHERE id=?", {user["id"].toString()}); user["status"] = "approved"; }
            return;
        }
        if (query("SELECT count(*) AS n FROM clients WHERE status='pending'").first()["n"].toInt() >= 30) throw std::runtime_error("Demasiadas solicitudes pendientes");
        auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        execute("INSERT INTO clients(id,public,name,status) VALUES(?,?,?,?)", {id,publicKey,name.trimmed(),autoApprove && approved < 10 ? "approved" : "pending"}); user = get(id);
    }); return user;
}
void Store::setOnline(const QString &id, bool online) { execute("UPDATE clients SET online=?,last_seen=datetime('now'),disconnect_requested=0 WHERE id=?", {int(online),id}); }
void Store::resetOnline() {
    transaction([&] {
        for (const auto &row : query("SELECT * FROM clients WHERE delete_requested=1")) deleteRow(row);
        execute("UPDATE clients SET online=0,disconnect_requested=0");
    });
}
void Store::setStatus(const QString &id, const QString &status) {
    if (status != "approved" && status != "revoked") throw std::runtime_error("Estado inválido");
    transaction([&] {
        auto row = get(id); if (row.isEmpty()) throw std::runtime_error("Cliente inexistente");
        if (status == "approved" && row["status"] != "approved" && query("SELECT count(*) AS n FROM clients WHERE status='approved'").first()["n"].toInt() >= 10) throw std::runtime_error("Ya hay diez clientes autorizados");
        execute("UPDATE clients SET status=? WHERE id=?", {status,id});
    });
}
void Store::disconnectClient(const QString &id) { if (get(id).isEmpty()) throw std::runtime_error("Cliente inexistente"); execute("UPDATE clients SET disconnect_requested=1 WHERE id=?", {id}); }
void Store::clearDisconnect(const QString &id) { execute("UPDATE clients SET disconnect_requested=0 WHERE id=?", {id}); }
void Store::deleteRow(const QJsonObject &row) {
    execute("INSERT OR REPLACE INTO deleted_clients(public,id,code,name) VALUES(?,?,?,?)", {row["public"].toString(),row["id"].toString(),row["code"].toInt(),row["name"].toString()});
    execute("DELETE FROM messages WHERE sender=? OR target=?", {row["id"].toString(),row["id"].toString()});
    execute("DELETE FROM clients WHERE id=?", {row["id"].toString()});
}
void Store::deleteClient(const QString &id) {
    transaction([&] {
        auto row = get(id); if (row.isEmpty()) throw std::runtime_error("Cliente inexistente");
        if (row["online"].toInt()) execute("UPDATE clients SET delete_requested=1 WHERE id=?", {id}); else deleteRow(row);
    });
}
void Store::finalizeDelete(const QString &id) { transaction([&] { auto row = get(id); if (!row.isEmpty()) deleteRow(row); }); }
void Store::recover(const QString &id, const QString &publicKey) {
    fromBase64(publicKey, 32);
    transaction([&] {
        auto row = get(id); if (row.isEmpty()) throw std::runtime_error("Cliente inexistente");
        if (!query("SELECT 1 FROM deleted_clients WHERE public=? UNION SELECT 1 FROM blocked_keys WHERE public=?", {publicKey,publicKey}).isEmpty()) throw std::runtime_error("Clave bloqueada");
        if (row["public"] == publicKey) return;
        auto others = query("SELECT * FROM clients WHERE public=? AND id<>?", {publicKey,id});
        if (!others.isEmpty()) {
            if (others.first()["status"] != "pending") throw std::runtime_error("La clave ya pertenece a otra identidad aprobada");
            execute("DELETE FROM clients WHERE id=?", {others.first()["id"].toString()});
        }
        execute("INSERT OR REPLACE INTO blocked_keys(public,reason) VALUES(?,'recovered')", {row["public"].toString()});
        execute("UPDATE clients SET public=? WHERE id=?", {publicKey,id});
    });
}
bool Store::saveMessage(const QString &id, const QString &sender, const QString &target, const QString &text, const QString &time) {
    bool inserted = false;
    transaction([&] {
        auto rows = query("SELECT target,text FROM messages WHERE sender=? AND message_id=?", {sender,id});
        if (!rows.isEmpty()) {
            if (rows.first()["target"] != target || rows.first()["text"] != text) throw std::runtime_error("ID reutilizado con otro contenido");
            return;
        }
        execute("INSERT INTO messages(message_id,sender,target,text,time) VALUES(?,?,?,?,?)", {id,sender,target,text,time}); inserted = true;
    }); return inserted;
}
QList<QJsonObject> Store::history(const QString &id, int limit) {
    auto rows = query("SELECT m.*,c.name AS sender_name,c.code AS sender_code FROM messages m JOIN clients c ON c.id=m.sender WHERE m.target='general' OR m.sender=? OR m.target=? ORDER BY m.seq DESC LIMIT ?", {id,id,limit});
    std::reverse(rows.begin(), rows.end()); return rows;
}
int Store::messageCount() { return query("SELECT count(*) AS n FROM messages").first()["n"].toInt(); }
void Store::backup(const QString &destination) {
    if (QFile::exists(destination)) throw std::runtime_error("El destino ya existe");
    privateDir(QFileInfo(destination).absolutePath()); sqlite3 *out = nullptr;
    if (sqlite3_open(QFile::encodeName(destination).constData(), &out) != SQLITE_OK) { if(out) sqlite3_close(out); throw std::runtime_error("No se pudo abrir copia"); }
    auto handle = sqlite3_backup_init(out,"main",db,"main");
    int result = handle ? sqlite3_backup_step(handle,-1) : SQLITE_ERROR;
    int finish = handle ? sqlite3_backup_finish(handle) : SQLITE_ERROR;
    sqlite3_close(out);
    if (result != SQLITE_DONE || finish != SQLITE_OK) { QFile::remove(destination); throw std::runtime_error("Copia SQLite fallida"); }
    QFile::setPermissions(destination, QFile::ReadOwner | QFile::WriteOwner);
}
}
