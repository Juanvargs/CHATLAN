#include "common/crypto.h"
#include "common/paths.h"
#include "common/protocol.h"
#include "common/store.h"
#include "client/connection.h"
#include "server/server.h"
#include <QtTest>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QFile>
#include <QUuid>

class ChatTests : public QObject {
    Q_OBJECT
private slots:
    void protocolAndIdentity() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        chat::Identity identity(dir.path());
        chat::Identity same(dir.path());
        QCOMPARE(identity.publicKey(), same.publicKey());
        auto payload = QByteArray("mensaje de prueba");
        QVERIFY(chat::verify(identity.publicKey(), payload, identity.sign(payload)));
        QVERIFY(!chat::verify(identity.publicKey(), payload + "x", identity.sign(payload)));

        auto packet = chat::event("message", {{"text", "áé🙂"}});
        auto encoded = chat::encode(packet);
        chat::Decoder decoder;
        QVERIFY(decoder.feed(encoded.left(3)).isEmpty());
        QVERIFY(decoder.feed(encoded.mid(3, 5)).isEmpty());
        auto result = decoder.feed(encoded.mid(8));
        QCOMPARE(result.size(), 1);
        QCOMPARE(result[0], packet);
        QByteArray invalid(4, '\0');
        QVERIFY_EXCEPTION_THROWN(decoder.feed(invalid), std::runtime_error);
    }

    void accountsAndHistory() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        chat::Store store(dir.path() + "/clients.sqlite3");
        QStringList ids;
        for (int i = 0; i < 11; ++i) {
            chat::Identity identity(dir.path() + "/p" + QString::number(i));
            auto user = store.registerClient(identity.publicKey(), "Usuario " + QString::number(i), true);
            ids.append(user["id"].toString());
            QCOMPARE(user["status"].toString(), i < 10 ? QString("approved") : QString("pending"));
        }
        store.setStatus(ids[0], "revoked");
        store.setStatus(ids[10], "approved");
        QVERIFY_EXCEPTION_THROWN(store.setStatus(ids[0], "approved"), std::runtime_error);

        auto messageId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QVERIFY(store.saveMessage(messageId, ids[1], ids[2], "Privado", "2026-01-01T00:00:00Z"));
        QVERIFY(!store.saveMessage(messageId, ids[1], ids[2], "Privado", "2026-01-01T00:00:00Z"));
        QVERIFY_EXCEPTION_THROWN(store.saveMessage(messageId, ids[1], ids[2], "Distinto", "2026-01-01T00:00:00Z"), std::runtime_error);
        QCOMPARE(store.history(ids[2]).size(), 1);
        QCOMPARE(store.history(ids[3]).size(), 0);
        auto oldPublic = store.get(ids[2])["public"].toString();
        chat::Identity replacement(dir.path() + "/replacement");
        auto pending = store.registerClient(replacement.publicKey(), "Reemplazo", true);
        QCOMPARE(pending["status"].toString(), QString("pending"));
        store.recover(ids[2], replacement.publicKey());
        QCOMPARE(store.get(ids[2])["public"].toString(), replacement.publicKey());
        QVERIFY(store.get(pending["id"].toString()).isEmpty());
        QCOMPARE(store.registerClient(oldPublic, "Clave vieja", true)["status"].toString(), QString("revoked"));
        auto backup = dir.path() + "/backup.sqlite3";
        store.backup(backup);
        QVERIFY(QFile::exists(backup));
        QVERIFY_EXCEPTION_THROWN(store.backup(backup), std::runtime_error);
        store.deleteClient(ids[1]);
        QCOMPARE(store.messageCount(), 0);
        QVERIFY(store.get(ids[1]).isEmpty());
        store.setOnline(ids[3], true);
        store.deleteClient(ids[3]);
        QCOMPARE(store.get(ids[3])["delete_requested"].toInt(), 1);
        store.resetOnline();
        QVERIFY(store.get(ids[3]).isEmpty());
    }

    void encryptedConversation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        chat::initializeTls(dir.path() + "/server");
        chat::ServerOptions options;
        options.directory = dir.path() + "/server";
        options.host = "127.0.0.1";
        options.port = 0;
        options.discoveryPort = 0;
        chat::ChatServer server(options);
        server.start();
        QVERIFY(server.port() != 0);

        chat::Identity first(dir.path() + "/first");
        chat::Identity second(dir.path() + "/second");
        chat::Connection alice(first), bob(second);
        QSignalSpy aliceOnline(&alice, &chat::Connection::online);
        QSignalSpy bobOnline(&bob, &chat::Connection::online);
        QSignalSpy aliceMessages(&alice, &chat::Connection::message);
        QSignalSpy bobMessages(&bob, &chat::Connection::message);
        alice.start("127.0.0.1", server.port(), options.directory + "/server.pem", "Alicia", server.discoveryPort());
        bob.start("127.0.0.1", server.port(), options.directory + "/server.pem", "Beto", server.discoveryPort());
        QTRY_VERIFY_WITH_TIMEOUT(alice.isAuthenticated(), 10000);
        QTRY_VERIFY_WITH_TIMEOUT(bob.isAuthenticated(), 10000);
        QVERIFY(aliceOnline.size() > 0);
        QVERIFY(bobOnline.size() > 0);
        QString aliceId, bobId;
        for (const auto &row : aliceMessages) {
            auto packet = qvariant_cast<QJsonObject>(row[0]);
            if (packet["type"] == "authenticated") aliceId = packet["id"].toString();
        }
        for (const auto &row : bobMessages) {
            auto packet = qvariant_cast<QJsonObject>(row[0]);
            if (packet["type"] == "authenticated") bobId = packet["id"].toString();
        }
        QVERIFY(!aliceId.isEmpty());
        QVERIFY(!bobId.isEmpty());
        auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        alice.send(chat::event("send_message", {{"message_id", id}, {"to", bobId}, {"text", "Hola Beto"}}));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto &row : bobMessages)
                if (qvariant_cast<QJsonObject>(row[0])["message_id"] == id) return true;
            return false;
        }(), 5000);
        chat::Store store(options.directory + "/clients.sqlite3");
        QCOMPARE(store.history(bobId).size(), 1);
        QCOMPARE(store.history(aliceId).size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto &row : aliceMessages)
                if (qvariant_cast<QJsonObject>(row[0])["type"] == "delivered") return true;
            return false;
        }(), 5000);
        store.setStatus(bobId, "revoked");
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto &row : bobMessages)
                if (qvariant_cast<QJsonObject>(row[0])["type"] == "revoked") return true;
            return false;
        }(), 5000);
        alice.stop(); bob.stop(); server.stop();
    }

    void pythonServerCompatibility() {
        auto certificate = qEnvironmentVariable("CHAT_LAN_PYTHON_CERT");
        if (certificate.isEmpty()) QSKIP("Se ejecuta con un servidor Python externo temporal");
        auto port = qEnvironmentVariableIntValue("CHAT_LAN_PYTHON_PORT");
        QVERIFY(port > 0 && port <= 65535);
        QTemporaryDir profile;
        QVERIFY(profile.isValid());
        chat::Identity identity(profile.path());
        chat::Connection client(identity);
        QSignalSpy messages(&client, &chat::Connection::message);
        client.start("127.0.0.1", static_cast<quint16>(port), certificate, "Cliente C++", 8764);
        QTRY_VERIFY_WITH_TIMEOUT(client.isAuthenticated(), 10000);
        auto id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        client.send(chat::event("send_message", {{"message_id", id}, {"to", "general"}, {"text", "Hola desde C++"}}));
        QTRY_VERIFY_WITH_TIMEOUT([&] {
            for (const auto &row : messages) {
                auto packet = qvariant_cast<QJsonObject>(row[0]);
                if (packet["type"] == "accepted" && packet["message_id"] == id) return true;
            }
            return false;
        }(), 5000);
        client.stop();
    }
};

QTEST_GUILESS_MAIN(ChatTests)
#include "test_chat.moc"
