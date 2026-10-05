#pragma once
#include "common/crypto.h"
#include "common/protocol.h"
#include <QObject>
#include <QSslSocket>
#include <QUdpSocket>
#include <QTimer>

namespace chat {
class Connection : public QObject {
    Q_OBJECT
    Identity &identity;
    QSslSocket socket;
    QUdpSocket discovery;
    QTimer retry, heartbeat, deadline;
    Decoder decoder;
    QString host, name, fingerprint;
    quint16 port = 8765, discoveryPort = 8764;
    int delay = 1;
    qint64 lastRx = 0;
    bool running = false, authenticated = false;
    void connectSocket();
    void discover();
    void readDiscovery();
    void read();
    void disconnected();
public:
    explicit Connection(Identity &identity, QObject *parent = nullptr);
    void start(const QString &host, quint16 port, const QString &certificate, const QString &name, quint16 discoveryPort = 8764);
    void stop();
    void send(const QJsonObject &message);
    bool isRunning() const { return running; }
    bool isAuthenticated() const { return authenticated; }
signals:
    void message(const QJsonObject &message);
    void status(const QString &text);
    void online(bool connected);
    void serverFound(const QString &host, int port);
};
}
