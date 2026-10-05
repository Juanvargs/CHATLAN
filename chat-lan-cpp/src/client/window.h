#pragma once
#include "connection.h"
#include <QMainWindow>
#include <QScrollArea>
#include <QVBoxLayout>
#include <QListWidget>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QJsonArray>
#include <QHash>
#include <QSet>

namespace chat {
class MessageView : public QScrollArea {
    Q_OBJECT
    QWidget *body;
    QVBoxLayout *column;
    QTimer renderTimer;
    QList<QJsonObject> pending;
    QString myId;
    void rebuild();
public:
    explicit MessageView(QWidget *parent = nullptr);
    void setMessages(const QList<QJsonObject> &messages, const QString &id);
    void scrollToBottom();
    QString plainText() const;
};
class Window : public QMainWindow {
    Q_OBJECT
    QString profile;
    Identity identity;
    Connection connection;
    QJsonObject config;
    QString myId;
    QHash<QString,QList<QJsonObject>> histories;
    QHash<QString,QJsonObject> knownUsers;
    QHash<QString,int> unreadCounts;
    QSet<QString> onlineUsers, seen;
    QList<QString> seenOrder;
    QLineEdit *name, *text, *search;
    QLabel *idLabel, *state, *chatTitle, *chatSubtitle, *chatAvatar, *selfAvatar;
    QPushButton *connectButton, *sendButton;
    QListWidget *targets;
    MessageView *messages;
    QSystemTrayIcon *tray;
    void saveConfig();
    void onMessage(const QJsonObject &packet);
    void rebuildTargets();
    void renderHistory();
    void filterTargets();
    void markConversationRead();
    QString target() const;
protected:
    void closeEvent(QCloseEvent *event) override;
public:
    explicit Window(const QString &profile, QWidget *parent = nullptr);
    Connection &network() { return connection; }
    QString clientId() const { return myId; }
    void connectChat();
    void sendMessage();
};
QString theme();
QString chooseProfile(QWidget *parent = nullptr);
}
