#include "server.h"
#include "common/crypto.h"
#include "common/paths.h"
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QSslKey>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkInterface>
#include <QNetworkDatagram>
#include <QProcess>
#include <QUuid>
#include <QDebug>
#include <stdexcept>

namespace chat {
static qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
QJsonObject publicUser(const QJsonObject &user) { return {{"id",user["id"]},{"name",user["name"]},{"code",user["code"]}}; }
ChatServer::ChatServer(ServerOptions value, QObject *parent)
    : QObject(parent), options(std::move(value)), store(options.directory + "/clients.sqlite3") {
    connect(&tcp,&QTcpServer::pendingConnectionAvailable,this,&ChatServer::accept);
    connect(&udp,&QUdpSocket::readyRead,this,[this] {
        while (udp.hasPendingDatagrams()) {
            auto datagram = udp.receiveDatagram(1024);
            if (datagram.data() != "CHAT_LAN_DISCOVER_V1" || !allowed(datagram.senderAddress())) continue;
            auto response = QJsonDocument(QJsonObject{{"service","chat-lan"},{"port",tcp.serverPort()},{"fingerprint",fingerprint}}).toJson(QJsonDocument::Compact);
            udp.writeDatagram(response,datagram.senderAddress(),datagram.senderPort());
        }
    });
    monitorTimer.setInterval(1000); connect(&monitorTimer,&QTimer::timeout,this,&ChatServer::monitor);
}
ChatServer::~ChatServer() { stop(); }
// Abre sockets UDP/TLS, configura certificados y comienza a aceptar clientes.
void ChatServer::start() {
    if (!QSslSocket::supportsSsl()) throw std::runtime_error("Backend TLS no disponible");
    QSslCertificate certificate(readFile(options.directory + "/server.pem"));
    auto keyBytes = readFile(options.directory + "/server.key");
    QSslKey key(keyBytes,QSsl::Ec);
    if (key.isNull()) key = QSslKey(keyBytes,QSsl::Rsa);
    if (certificate.isNull() || key.isNull()) throw std::runtime_error("Certificado/clave TLS inválidos");
    fingerprint = certificateFingerprint(options.directory + "/server.pem");
    QSslConfiguration config = QSslConfiguration::defaultConfiguration();
    config.setLocalCertificate(certificate); config.setPrivateKey(key);
    config.setProtocol(QSsl::TlsV1_2OrLater); config.setPeerVerifyMode(QSslSocket::VerifyNone);
    tcp.setSslConfiguration(config); tcp.setHandshakeTimeout(10000); tcp.setMaxPendingConnections(30);
    if (!tcp.listen(QHostAddress(options.host),options.port)) throw std::runtime_error(tcp.errorString().toStdString());
    if (!udp.bind(QHostAddress(options.host),options.discoveryPort))
        qWarning().noquote() << "Descubrimiento UDP no disponible:" << udp.errorString();
    store.resetOnline(); stopping = false; monitorTimer.start();
    qInfo().noquote() << "Chat LAN C++:" << tcp.serverAddress().toString() << port() << "UDP" << discoveryPort();
}
// Decide si una IP remota coincide con la política local de acceso.
bool ChatServer::allowed(const QHostAddress &address) const {
    if (address.protocol() != QAbstractSocket::IPv4Protocol) return false;
    if (options.allow != "auto") {
        for (auto cidr : options.allow.split(',')) { auto subnet = QHostAddress::parseSubnet(cidr.trimmed()); if (subnet.second >= 0 && address.isInSubnet(subnet)) return true; }
        return false;
    }
    if (address.isLoopback()) return true;
    QProcess process; process.start("ip",{"-j","-4","route","show","default"});
    if (!process.waitForFinished(500)) return false;
    auto routes = QJsonDocument::fromJson(process.readAllStandardOutput()).array();
    if (routes.isEmpty()) return false;
    auto name = routes.first().toObject()["dev"].toString();
    for (auto entry : QNetworkInterface::interfaceFromName(name).addressEntries())
        if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol && address.isInSubnet(entry.ip(),entry.prefixLength())) return true;
    return false;
}
// Acepta conexiones TLS, verifica acceso por IP y crea estado por cliente.
void ChatServer::accept() {
    while (tcp.hasPendingConnections()) {
        auto socket = qobject_cast<QSslSocket*>(tcp.nextPendingConnection());
        if (!socket) continue;
        if (peers.size() >= 30 || !allowed(socket->peerAddress())) { socket->abort(); socket->deleteLater(); continue; }
        auto peer = std::make_shared<Peer>(); peer->socket = socket; peer->lastRx = peer->lastWrite = now();
        peers.insert(socket,peer); socket->setReadBufferSize(131080);
        connect(socket,&QSslSocket::readyRead,this,[this,peer] { read(peer); });
        connect(socket,&QSslSocket::bytesWritten,this,[peer](qint64) { peer->lastWrite = now(); });
        connect(socket,&QSslSocket::disconnected,this,[this,peer] {
            auto id = peer->user["id"].toString();
            if (!id.isEmpty() && sessions.value(id) == peer) {
                sessions.remove(id);
                try { store.setOnline(id,false); } catch (const std::exception &e) { qWarning() << e.what(); }
                if (!stopping) presence();
            }
            peers.remove(peer->socket); peer->socket->deleteLater();
        });
        if (socket->bytesAvailable()) read(peer);
    }
}
bool ChatServer::send(const PeerPtr &peer, const QJsonObject &message) {
    if (peer->socket->state() != QAbstractSocket::ConnectedState) return false;
    auto wire = encode(message);
    if (peer->socket->bytesToWrite() + wire.size() > 2 * 1024 * 1024) { peer->socket->abort(); return false; }
    if (!peer->socket->bytesToWrite()) peer->lastWrite = now();
    return peer->socket->write(wire) == wire.size();
}
void ChatServer::finish(const PeerPtr &peer, const QString &kind) {
    if (!kind.isEmpty()) send(peer,chat::event(kind));
    peer->stage = -1; peer->socket->disconnectFromHost();
    auto socket = peer->socket;
    QTimer::singleShot(2000,socket,[socket] { socket->abort(); });
}
// Decodifica tramas del cliente y las envía al manejador de eventos.
void ChatServer::read(const PeerPtr &peer) {
    if (peer->stage < 0) return;
    try {
        do {
            auto data = peer->socket->read(65536);
            if (data.isEmpty()) break;
            for (const auto &message : peer->decoder.feed(data)) {
                if (peer->stage < 0) break;
                peer->lastRx = now(); dispatch(peer,message);
            }
        } while(peer->socket->bytesAvailable());
    } catch (const std::exception &e) {
        qWarning() << "Solicitud rechazada:" << e.what();
        send(peer,chat::event("error",{{"text",QString::fromUtf8(e.what())}})); finish(peer);
    }
}
// Procesa autenticación, acuses, keepalive y mensajes del protocolo.
void ChatServer::dispatch(const PeerPtr &peer, const QJsonObject &message) {
    auto kind = message["type"].toString();
    if (peer->stage == 0) {
        if (kind != "hello") throw std::runtime_error("Se esperaba hello");
        peer->publicKey = message["public"].toString(); fromBase64(peer->publicKey,32);
        peer->name = message["name"].toString(); peer->nonce = randomNonce(); peer->stage = 1;
        send(peer,chat::event("challenge",{{"nonce",peer->nonce},{"fingerprint",fingerprint}})); return;
    }
    if (peer->stage == 1) {
        if (kind != "proof" || !verify(peer->publicKey,proofPayload(peer->nonce,peer->publicKey,fingerprint),fromBase64(message["signature"].toString(),64))) throw std::runtime_error("Firma inválida");
        peer->nonce.clear(); peer->user = store.registerClient(peer->publicKey,peer->name,options.autoApprove);
        auto status = peer->user["status"].toString();
        if (status != "approved") {
            auto type = status == "deleted" ? "account_deleted" : status == "pending" ? "registration_pending" : "revoked";
            send(peer,chat::event(type,publicUser(peer->user))); finish(peer); return;
        }
        auto id = peer->user["id"].toString(); auto old = sessions.value(id);
        sessions.insert(id,peer); peer->stage = 2; store.setOnline(id,true);
        if (old && old != peer) finish(old,"replaced");
        send(peer,chat::event("authenticated",publicUser(peer->user))); sendHistory(peer); presence(); return;
    }
    auto sender = peer->user["id"].toString();
    if (sessions.value(sender) != peer) { finish(peer,"replaced"); return; }
    auto current = store.get(sender);
    if (current.isEmpty() || current["delete_requested"].toInt()) { finish(peer,"account_deleted"); return; }
    if (current["status"] != "approved" || current["public"].toString() != peer->publicKey) { finish(peer,"revoked"); return; }
    while(!peer->recent.isEmpty() && peer->recent.head() < now()-5000) peer->recent.dequeue();
    if (peer->recent.size() >= 50) throw std::runtime_error("Demasiadas solicitudes");
    peer->recent.enqueue(now());
    if (kind == "ping") { send(peer,chat::event("pong")); return; }
    if (kind == "received") {
        auto key = message["sender"].toString() + ":" + message["message_id"].toString();
        auto it = deliveries.find(key);
        if (it != deliveries.end() && it->waiting.remove(sender)) {
            auto original = sessions.value(it->sender);
            if (original) send(original,chat::event("delivered",{{"message_id",message["message_id"]},{"recipient",sender}}));
        }
        return;
    }
    if (kind != "send_message") throw std::runtime_error("Tipo inválido");
    auto id = message["message_id"].toString(); auto target = message["to"].toString(); auto text = message["text"].toString().trimmed();
    if (QUuid(id).isNull() || text.isEmpty() || text.size() > 2000 || target.isEmpty()) throw std::runtime_error("Mensaje inválido");
    QList<PeerPtr> recipients;
    if (target == "general") recipients = sessions.values(); else {
        auto recipient = sessions.value(target); auto row = store.get(target);
        if (!recipient || row["status"] != "approved" || row["delete_requested"].toInt()) { send(peer,chat::event("error",{{"message_id",id},{"text","Destinatario desconectado"}})); return; }
        recipients.append(recipient);
    }
    auto timestamp = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    if (!store.saveMessage(id,sender,target,text,timestamp)) { send(peer,chat::event("accepted",{{"message_id",id},{"duplicate",true}})); return; }
    auto packet = chat::event("message",{{"message_id",id},{"sender",publicUser(peer->user)},{"to",target},{"text",text},{"time",timestamp}});
    Delivery delivery{sender,{},now()};
    for (auto recipient : recipients) {
        auto row = store.get(recipient->user["id"].toString());
        if (row["status"] == "approved" && !row["delete_requested"].toInt() && row["public"].toString() == recipient->publicKey && send(recipient,packet)) delivery.waiting.insert(recipient->user["id"].toString());
    }
    if (target != "general" && !recipients.contains(peer)) send(peer,packet);
    deliveries.insert(sender + ":" + id,delivery);
    send(peer,chat::event("accepted",{{"message_id",id},{"duplicate",false}}));
}
// Difunde la lista actual de usuarios conectados.
void ChatServer::presence() {
    QJsonArray users; for (auto peer : sessions) users.append(publicUser(peer->user));
    for (auto peer : sessions.values()) send(peer,chat::event("presence",{{"users",users}}));
}
// Envía al cliente su historial pertinente al finalizar autenticación.
void ChatServer::sendHistory(const PeerPtr &peer) {
    for (auto row : store.history(peer->user["id"].toString())) {
        QJsonObject sender{{"id",row["sender"]},{"name",row["sender_name"]},{"code",row["sender_code"]}};
        send(peer,chat::event("message",{{"message_id",row["message_id"]},{"sender",sender},{"to",row["target"]},{"text",row["text"]},{"time",row["time"]},{"history",true}}));
    }
}
// Revisa periódicamente solicitudes administrativas y sesiones inactivas.
void ChatServer::monitor() {
    try {
        bool reset = false;
        for (auto peer : sessions.values()) {
            auto id = peer->user["id"].toString(); auto row = store.get(id);
            if (row.isEmpty() || row["delete_requested"].toInt()) { store.finalizeDelete(id); finish(peer,"account_deleted"); reset = true; }
            else if (row["disconnect_requested"].toInt()) { store.clearDisconnect(id); finish(peer,"admin_disconnected"); }
            else if (row["status"] != "approved" || row["public"].toString() != peer->publicKey) finish(peer,"revoked");
        }
        if (reset) for (auto peer : sessions.values()) if (peer->stage == 2) { send(peer,chat::event("history_reset")); sendHistory(peer); }
        for (auto peer : peers.values()) {
            if (now()-peer->lastRx > (peer->stage < 2 ? 15000 : 90000) || (peer->socket->bytesToWrite() && now()-peer->lastWrite > 5000)) peer->socket->abort();
        }
        for (auto it = deliveries.begin(); it != deliveries.end();) { if (now()-it->time > 300000) it = deliveries.erase(it); else ++it; }
        while(deliveries.size() > 1000) deliveries.erase(deliveries.begin());
    } catch (const std::exception &e) { qWarning() << "Monitor:" << e.what(); }
}
// Detiene timers y cierra sockets y sesiones activas.
void ChatServer::stop() {
    stopping = true; monitorTimer.stop(); udp.close(); tcp.close();
    for (auto peer : peers.values()) peer->socket->abort();
}
}
