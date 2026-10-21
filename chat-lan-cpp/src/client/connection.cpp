#include "connection.h"
#include "common/paths.h"
#include <QSslConfiguration>
#include <QSslCertificate>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QNetworkDatagram>
#include <QDateTime>
#include <QRandomGenerator>
#include <stdexcept>

namespace chat {
// Configura temporizadores, validación TLS y reacciones a eventos de red.
Connection::Connection(Identity &value, QObject *parent) : QObject(parent), identity(value) {
    retry.setSingleShot(true); deadline.setSingleShot(true); heartbeat.setInterval(20000);
    connect(&retry,&QTimer::timeout,this,&Connection::connectSocket);
    connect(&deadline,&QTimer::timeout,this,[this] { socket.abort(); disconnected(); });
    connect(&heartbeat,&QTimer::timeout,this,[this] {
        if (QDateTime::currentMSecsSinceEpoch()-lastRx > 60000) socket.abort();
        else if (authenticated) { try { send(chat::event("ping")); } catch(const std::exception &e) { emit status(e.what()); socket.abort(); } }
    });
    connect(&socket,&QSslSocket::encrypted,this,[this] {
        auto actual = QString::fromLatin1(socket.peerCertificate().digest(QCryptographicHash::Sha256).toHex());
        if (actual != fingerprint) { running = false; emit status("Huella del servidor incorrecta"); socket.abort(); return; }
        lastRx = QDateTime::currentMSecsSinceEpoch();
        try { send(chat::event("hello",{{"name",name},{"public",identity.publicKey()}})); } catch(const std::exception &e) { emit status(e.what()); stop(); }
    });
    connect(&socket,&QSslSocket::readyRead,this,&Connection::read);
    connect(&socket,&QSslSocket::disconnected,this,&Connection::disconnected);
    connect(&socket,&QSslSocket::errorOccurred,this,[this](QAbstractSocket::SocketError) { if(running) { emit status(socket.errorString()); if(socket.state()==QAbstractSocket::UnconnectedState) disconnected(); } });
    connect(&socket,&QSslSocket::sslErrors,this,[this](const QList<QSslError> &) {
        running = false; retry.stop(); emit status("Certificado no válido: revisa certificado y fecha del equipo"); socket.abort(); emit online(false);
    });
    connect(&discovery,&QUdpSocket::readyRead,this,&Connection::readDiscovery);
}
// Carga el certificado de confianza e inicia búsqueda o conexión a una IP conocida.
void Connection::start(const QString &address, quint16 tcpPort, const QString &certificate, const QString &label, quint16 udpPort) {
    stop(); host = address; port = tcpPort; name = label; discoveryPort = udpPort;
    auto cert = QSslCertificate(readFile(certificate)); if(cert.isNull()) throw std::runtime_error("Certificado inválido");
    fingerprint = QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
    auto config = QSslConfiguration::defaultConfiguration(); config.setCaCertificates({cert});
    config.setPeerVerifyMode(QSslSocket::VerifyPeer); config.setProtocol(QSsl::TlsV1_2OrLater);
    socket.setSslConfiguration(config); socket.setPeerVerifyName("localhost"); socket.setReadBufferSize(131080);
    running = true; delay = 1; discover();
    if(host.trimmed().isEmpty()) { emit status("Buscando servidor en la LAN…"); retry.start(3000); }
    else connectSocket();
}
// Envía la petición UDP de descubrimiento por broadcast y loopback.
void Connection::discover() {
    discovery.close();
    if (!discovery.bind(QHostAddress::AnyIPv4,0)) return;
    discovery.writeDatagram("CHAT_LAN_DISCOVER_V1",QHostAddress::Broadcast,discoveryPort);
    discovery.writeDatagram("CHAT_LAN_DISCOVER_V1",QHostAddress::LocalHost,discoveryPort);
}
// Acepta respuestas del servicio cuya huella coincida con el certificado fijado.
void Connection::readDiscovery() {
    while(discovery.hasPendingDatagrams()) {
        auto datagram = discovery.receiveDatagram(4096); auto data = QJsonDocument::fromJson(datagram.data()).object();
        auto newPort = data["port"].toInt();
        if(!running || data["service"] != "chat-lan" || data["fingerprint"].toString() != fingerprint || newPort < 1 || newPort > 65535) continue;
        auto address = datagram.senderAddress().toString();
        discovery.close();
        if (address != host || newPort != port) {
            host = address; port = newPort; emit serverFound(host,port);
            // Cierra sin disparar la reconexión automática y programa un solo intento nuevo.
            running = false; socket.abort(); running = true; retry.stop(); deadline.stop();
            QTimer::singleShot(100,this,[this] { if(running) connectSocket(); });
        }
        return;
    }
}
// Abre TLS al servidor y activa el tiempo límite de conexión.
void Connection::connectSocket() {
    if(!running) return;
    if(host.trimmed().isEmpty()) { discover(); emit status("Buscando servidor en la LAN…"); retry.start(3000); return; }
    if(socket.state()!=QAbstractSocket::UnconnectedState) { running=false; socket.abort(); running=true; }
    decoder = Decoder{}; authenticated = false;
    emit status("Conectando…"); socket.connectToHostEncrypted(host,port); deadline.start(20000);
}
// Codifica y envía un evento solo por un socket cifrado y con cola acotada.
void Connection::send(const QJsonObject &value) {
    if(!socket.isEncrypted()) throw std::runtime_error("Sin conexión cifrada");
    auto data = encode(value);
    if(socket.bytesToWrite()+data.size()>262144) throw std::runtime_error("Conexión saturada");
    if(socket.write(data)!=data.size()) throw std::runtime_error("No se pudo enviar");
}
// Procesa eventos entrantes, firma desafíos y actualiza el estado del cliente.
void Connection::read() {
    try {
        while(socket.bytesAvailable()) for(auto packet : decoder.feed(socket.read(65536))) {
            lastRx = QDateTime::currentMSecsSinceEpoch(); auto kind = packet["type"].toString();
            if(kind=="challenge") {
                if(packet["fingerprint"].toString()!=fingerprint) throw std::runtime_error("Servidor incorrecto");
                auto signature = identity.sign(proofPayload(packet["nonce"].toString(),identity.publicKey(),fingerprint));
                send(chat::event("proof",{{"signature",QString::fromLatin1(signature.toBase64())}}));
            } else if(kind=="authenticated") {
                authenticated=true; delay=1; retry.stop(); deadline.stop(); heartbeat.start();
                emit online(true); emit status("Conectado");
            } else if(kind=="message") {
                send(chat::event("received",{{"sender",packet["sender"].toObject()["id"]},{"message_id",packet["message_id"]}}));
            } else if(QStringList{"registration_pending","revoked","replaced","admin_disconnected","account_deleted"}.contains(kind)) {
                running=false; retry.stop(); deadline.stop(); heartbeat.stop();
                const QHash<QString,QString> labels{{"registration_pending","Alta pendiente. Consulta al administrador y vuelve a conectar."},{"revoked","Identidad revocada o clave reemplazada."},{"replaced","Este perfil se abrió en otra instancia."},{"admin_disconnected","El administrador desconectó esta sesión."},{"account_deleted","El administrador eliminó esta cuenta."}};
                emit status(labels.value(kind));
            }
            emit message(packet);
        }
    } catch(const std::exception &e) { emit status(QString::fromUtf8(e.what())); stop(); }
}
void Connection::disconnected() {
    authenticated=false; emit online(false); heartbeat.stop(); deadline.stop();
    if(running && !retry.isActive()) {
        discover(); retry.start(delay*1000+QRandomGenerator::global()->bounded(1000));
        emit status(QString("Desconectado. Reintentando en %1 s…").arg(delay)); delay=qMin(30,delay*2);
    }
}
void Connection::stop() {
    running=false; authenticated=false; retry.stop(); heartbeat.stop(); deadline.stop(); discovery.close(); socket.abort(); emit online(false);
}
}
