#define main chatLanAdminProgramMain
#include "../src/admin/gui.cpp"
#undef main

#include "common/crypto.h"
#include <QtTest>
#include <QTemporaryDir>
#include <QFile>

class AdminUiTests : public QObject {
    Q_OBJECT
private slots:
    void accessActionsFollowSelectedUser() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        chat::Identity identity(dir.path()+"/identity");
        chat::Store store(dir.path()+"/clients.sqlite3");
        const auto user=store.registerClient(identity.publicKey(),"Persona de prueba",false);
        const auto id=user["id"].toString();
        Admin panel(dir.path(),"chat-lan-admin-ui-test",true);
        panel.show();
        auto table=panel.findChild<QTableWidget*>("usersTable");
        auto access=panel.findChild<QPushButton*>("accessButton");
        auto disconnect=panel.findChild<QPushButton*>("disconnectButton");
        auto remove=panel.findChild<QPushButton*>("deleteButton");
        QVERIFY(table && access && disconnect && remove);
        QTRY_COMPARE(table->rowCount(),1);
        QVERIFY(!access->isEnabled());
        table->selectRow(0);
        QTRY_VERIFY(access->isEnabled());
        QVERIFY(!disconnect->isEnabled());
        QCOMPARE(access->text(),QString("Aprobar usuario"));
        access->click();
        QTRY_COMPARE(store.get(id)["status"].toString(),QString("approved"));
        QCOMPARE(access->text(),QString("Revocar acceso"));
        access->click();
        QTRY_COMPARE(store.get(id)["status"].toString(),QString("revoked"));
        QCOMPARE(access->text(),QString("Reactivar acceso"));
        panel.close();
    }
    void serviceButtonStartsAndStops() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto statePath=dir.path()+"/service-state";
        QFile state(statePath);
        QVERIFY(state.open(QIODevice::WriteOnly));
        state.write("inactive\n"); state.close();
        QFile script(dir.path()+"/systemctl");
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write("#!/bin/sh\ncase \"$2\" in\n"
                     "  is-active) cat \"$CHAT_LAN_FAKE_STATE\" ;;\n"
                     "  start) printf 'active\\n' > \"$CHAT_LAN_FAKE_STATE\" ;;\n"
                     "  stop) printf 'inactive\\n' > \"$CHAT_LAN_FAKE_STATE\" ;;\n"
                     "  restart) printf 'active\\n' > \"$CHAT_LAN_FAKE_STATE\" ;;\n"
                     "  *) exit 1 ;;\nesac\n");
        script.close();
        QVERIFY(script.setPermissions(QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner));
        const auto oldPath=qgetenv("PATH");
        qputenv("PATH",dir.path().toUtf8()+":"+oldPath);
        qputenv("CHAT_LAN_FAKE_STATE",statePath.toUtf8());
        {
            Admin panel(dir.path(),"fake-service",true);
            panel.show();
            auto toggle=panel.findChild<QPushButton*>("serviceToggle");
            auto save=panel.findChild<QPushButton*>("saveSettingsButton");
            auto feedback=panel.findChild<QLabel*>("feedback");
            QVERIFY(toggle);
            QVERIFY(save && feedback);
            QTRY_VERIFY(toggle->isEnabled());
            QCOMPARE(toggle->text(),QString("Encender servidor"));
            toggle->click();
            QTRY_COMPARE(toggle->text(),QString("Apagar servidor"));
            save->click();
            QTRY_VERIFY(QFile::exists(dir.path()+"/settings.json"));
            QTRY_VERIFY(feedback->text().contains("Configuración aplicada"));
            toggle->click();
            QTRY_COMPARE(toggle->text(),QString("Encender servidor"));
            panel.close();
        }
        qputenv("PATH",oldPath);
        qunsetenv("CHAT_LAN_FAKE_STATE");
    }
};

QTEST_MAIN(AdminUiTests)
#include "test_admin_ui.moc"
