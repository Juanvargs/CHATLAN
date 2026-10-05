#include "client/window.h"
#include "server/server.h"
#include "common/paths.h"
#include "common/crypto.h"
#include <QtTest>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTemporaryDir>

class UiTests : public QObject {
    Q_OBJECT
private slots:
    void initialStateAndSafeRendering() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        chat::Window window(dir.path());
        QVERIFY(window.findChild<QPushButton*>("connectButton"));
        QVERIFY(!window.findChild<QPushButton*>("sendButton")->isEnabled());
        QVERIFY(!window.findChild<QLineEdit*>("messageInput")->isEnabled());
        chat::MessageView view;
        view.setMessages({chat::event("message", {{"sender", QJsonObject{{"id", "other"}, {"name", "<script>"}, {"code", 1}}},
                                                     {"text", "<b>texto</b>"}, {"time", "2026-01-01T00:00:00Z"}})}, "mine");
        QTRY_VERIFY(view.findChild<QLabel*>("messageBubble"));
        auto bubble = view.findChild<QLabel*>("messageBubble");
        QVERIFY(bubble->text().contains("&lt;script&gt;"));
        QVERIFY(bubble->text().contains("&lt;b&gt;texto&lt;/b&gt;"));
    }

    void completeWindowConversation() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto serverDir = dir.path() + "/server";
        chat::initializeTls(serverDir);
        chat::ServerOptions options;
        options.directory = serverDir;
        options.host = "127.0.0.1";
        options.port = 0;
        options.discoveryPort = 0;
        chat::ChatServer server(options);
        server.start();

        auto profile = dir.path() + "/profile";
        chat::writeJson(profile + "/connection.json", {{"name", "Usuario ventana"},
            {"host", "127.0.0.1"}, {"port", server.port()},
            {"discovery_port", server.discoveryPort()}, {"certificate", serverDir + "/server.pem"}});
        chat::Window window(profile);
        window.show();
        window.connectChat();
        QTRY_VERIFY_WITH_TIMEOUT(window.network().isAuthenticated(), 10000);
        auto input = window.findChild<QLineEdit*>("messageInput");
        QVERIFY(input->isEnabled());
        input->setText("Mensaje desde la ventana");
        window.sendMessage();
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<chat::MessageView*>("messageView")
            ->plainText().contains("Mensaje desde la ventana"), 5000);
        QCOMPARE(input->text(), QString());
        auto secondProfile=dir.path()+"/second";
        chat::writeJson(secondProfile+"/connection.json", {{"name", "Otra ventana"},
            {"host", "127.0.0.1"}, {"port", server.port()},
            {"discovery_port", server.discoveryPort()}, {"certificate", serverDir+"/server.pem"}});
        chat::Window second(secondProfile);
        second.show(); second.connectChat();
        QTRY_VERIFY_WITH_TIMEOUT(second.network().isAuthenticated(), 10000);
        auto secondChats=second.findChild<QListWidget*>("conversations");
        QTRY_VERIFY_WITH_TIMEOUT([&] { for(int i=0;i<secondChats->count();++i)
            if(secondChats->item(i)->data(Qt::UserRole)==window.clientId()) return true; return false; }(), 5000);
        for(int i=0;i<secondChats->count();++i) if(secondChats->item(i)->data(Qt::UserRole)==window.clientId()) secondChats->setCurrentRow(i);
        second.findChild<QLineEdit*>("messageInput")->setText("Privado sin leer"); second.sendMessage();
        auto chats=window.findChild<QListWidget*>("conversations");
        QTRY_VERIFY_WITH_TIMEOUT([&] { for(int i=0;i<chats->count();++i) {
            auto item=chats->item(i);
            if(item->data(Qt::UserRole)!=second.clientId()) continue;
            auto badge=chats->itemWidget(item)->findChild<QLabel*>("unreadBadge");
            return badge && badge->text()=="1" && badge->isVisible();
        } return false; }(), 5000);
        for(int i=0;i<chats->count();++i) if(chats->item(i)->data(Qt::UserRole)==second.clientId()) chats->setCurrentRow(i);
        QTRY_VERIFY_WITH_TIMEOUT(window.findChild<chat::MessageView*>("messageView")->plainText().contains("Privado sin leer"), 5000);
        QVERIFY(!chats->itemWidget(chats->currentItem())->findChild<QLabel*>("unreadBadge")->isVisible());
        second.close();
        window.close();
        server.stop();
    }

    void goServerConversation() {
        const auto certificate=qEnvironmentVariable("CHAT_LAN_GO_CERT");
        if(certificate.isEmpty()) QSKIP("Se ejecuta con un servidor Go externo temporal");
        const auto port=qEnvironmentVariableIntValue("CHAT_LAN_GO_PORT");
        const auto discoveryPort=qEnvironmentVariableIntValue("CHAT_LAN_GO_DISCOVERY_PORT");
        QVERIFY(port>0 && port<=65535);
        QVERIFY(discoveryPort>0 && discoveryPort<=65535);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        auto aliceProfile=dir.path()+"/alice";
        auto bobProfile=dir.path()+"/bob";
        for(auto pair:QList<QPair<QString,QString>>{{aliceProfile,"Alicia C++"},{bobProfile,"Beto C++"}})
            chat::writeJson(pair.first+"/connection.json",{{"name",pair.second},{"host",pair.first==bobProfile?"":"127.0.0.1"},{"port",port},
                {"discovery_port",discoveryPort},{"certificate",certificate}});
        chat::Window alice(aliceProfile),bob(bobProfile);
        alice.show(); bob.show(); alice.connectChat(); bob.connectChat();
        QTRY_VERIFY_WITH_TIMEOUT(alice.network().isAuthenticated(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(bob.network().isAuthenticated(),10000);
        auto aliceInput=alice.findChild<QLineEdit*>("messageInput");
        auto bobInput=bob.findChild<QLineEdit*>("messageInput");
        auto aliceMessages=alice.findChild<chat::MessageView*>("messageView");
        auto bobMessages=bob.findChild<chat::MessageView*>("messageView");
        QVERIFY(aliceInput && bobInput && aliceMessages && bobMessages);
        aliceInput->setText("General de C++ a Go"); alice.sendMessage();
        QTRY_VERIFY_WITH_TIMEOUT(bobMessages->plainText().contains("General de C++ a Go"),5000);
        auto conversations=bob.findChild<QListWidget*>("conversations");
        QVERIFY(conversations);
        QTRY_VERIFY_WITH_TIMEOUT([&] { for(int i=0;i<conversations->count();++i)
            if(conversations->item(i)->data(Qt::UserRole)==alice.clientId()) return true; return false; }(),5000);
        for(int i=0;i<conversations->count();++i) if(conversations->item(i)->data(Qt::UserRole)==alice.clientId()) conversations->setCurrentRow(i);
        bobInput->setText("Privado de C++ a Go"); bob.sendMessage();
        auto aliceConversations=alice.findChild<QListWidget*>("conversations");
        QTRY_VERIFY_WITH_TIMEOUT([&] { for(int i=0;i<aliceConversations->count();++i)
            if(aliceConversations->item(i)->data(Qt::UserRole)==bob.clientId()) return true; return false; }(),5000);
        for(int i=0;i<aliceConversations->count();++i) if(aliceConversations->item(i)->data(Qt::UserRole)==bob.clientId()) aliceConversations->setCurrentRow(i);
        QTRY_VERIFY_WITH_TIMEOUT(aliceMessages->plainText().contains("Privado de C++ a Go"),5000);
        alice.close(); bob.close();
    }
};

QTEST_MAIN(UiTests)
#include "test_ui.moc"
