#include "protocol.h"
#include <QJsonDocument>
#include <QtEndian>
#include <stdexcept>

namespace chat {
QJsonObject event(const QString &type, QJsonObject fields) {
    fields.insert("v", 1); fields.insert("type", type); return fields;
}
QByteArray encode(const QJsonObject &message) {
    auto body = QJsonDocument(message).toJson(QJsonDocument::Compact);
    if (body.isEmpty() || body.size() > MaxFrame) throw std::runtime_error("Trama demasiado grande");
    QByteArray wire(4, '\0'); qToBigEndian<quint32>(body.size(), wire.data()); return wire + body;
}
QList<QJsonObject> Decoder::feed(const QByteArray &data) {
    if (data.size() > 4 * MaxFrame) throw std::runtime_error("Lectura demasiado grande");
    buffer += data; QList<QJsonObject> result;
    while (buffer.size() >= 4) {
        auto size = qFromBigEndian<quint32>(buffer.constData());
        if (!size || size > MaxFrame) throw std::runtime_error("Longitud inválida");
        if (buffer.size() < 4 + size) break;
        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(buffer.mid(4, size), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("JSON inválido");
        auto obj = doc.object();
        if (obj.value("v").toInt(-1) != 1 || !obj.value("type").isString()) throw std::runtime_error("Protocolo incompatible");
        result.append(obj); buffer.remove(0, 4 + size);
    }
    if (buffer.size() > 4 + MaxFrame) throw std::runtime_error("Buffer inválido");
    return result;
}
QByteArray fromBase64(const QString &value, int expected) {
    auto bytes = QByteArray::fromBase64(value.toLatin1(), QByteArray::AbortOnBase64DecodingErrors);
    if (bytes.size() != expected) throw std::runtime_error("Credencial inválida");
    return bytes;
}
QByteArray proofPayload(const QString &nonce, const QString &publicKey, const QString &fingerprint) {
    if (fingerprint.size() != 64) throw std::runtime_error("Huella inválida");
    auto fp = QByteArray::fromHex(fingerprint.toLatin1());
    if (fp.size() != 32) throw std::runtime_error("Huella inválida");
    return QByteArray("chat-lan/auth/v1\0", 17) + fromBase64(nonce, 32) + fromBase64(publicKey, 32) + fp;
}
}
