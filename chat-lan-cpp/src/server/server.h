#pragma once
#include "common/store.h"
#include "common/protocol.h"
#include <QObject>
#include <QSslServer>
#include <QSslSocket>
#include <QUdpSocket>
#include <QTimer>
#include <QHash>
#include <QSet>
#include <QQueue>
#include <memory>

namespace chat {
struct ServerOptions {
    QString directory;
    QString host = "0.0.0.0";
    quint16 port = 8765;
    quint16 discoveryPort = 8764;
    QString allow = "auto";
    bool autoApprove = true;
};
class ChatServer : public QObject {
    Q_OBJECT
    struct Peer {
        QSslSocket *socket;
        Decoder decoder;
        QJsonObject user;
        QString publicKey, name, nonce;
        int stage = 0;
        qint64 lastRx = 0, lastWrite = 0;
        QQueue<qint64> recent;
    };
    using PeerPtr = std::shared_ptr<Peer>;
    struct Delivery { QString sender; QSet<QString> waiting; qint64 time; };
    ServerOptions options;
    Store store;
    QSslServer tcp;
    QUdpSocket udp;
    QTimer monitorTimer;
    QString fingerprint;
    QHash<QSslSocket*,PeerPtr> peers;
    QHash<QString,PeerPtr> sessions;
    QHash<QString,Delivery> deliveries;
    bool stopping = false;
    void accept();
    void read(const PeerPtr &peer);
    void dispatch(const PeerPtr &peer, const QJsonObject &message);
    bool send(const PeerPtr &peer, const QJsonObject &message);
    void finish(const PeerPtr &peer, const QString &kind = {});
    void presence();
    void sendHistory(const PeerPtr &peer);
    void monitor();
    bool allowed(const QHostAddress &address) const;
public:
    explicit ChatServer(ServerOptions options, QObject *parent = nullptr);
    ~ChatServer() override;
    void start();
    void stop();
    quint16 port() const { return tcp.serverPort(); }
    quint16 discoveryPort() const { return udp.localPort(); }
    int onlineCount() const { return sessions.size(); }
    QString certificateHash() const { return fingerprint; }
};
QJsonObject publicUser(const QJsonObject &user);
}
