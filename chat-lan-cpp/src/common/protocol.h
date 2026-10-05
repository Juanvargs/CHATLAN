#pragma once
#include <QJsonObject>
#include <QList>
#include <QByteArray>

namespace chat {
constexpr quint32 MaxFrame = 65536;
QJsonObject event(const QString &type, QJsonObject fields = {});
QByteArray encode(const QJsonObject &message);
class Decoder {
    QByteArray buffer;
public:
    QList<QJsonObject> feed(const QByteArray &data);
};
QByteArray fromBase64(const QString &value, int expected);
QByteArray proofPayload(const QString &nonce, const QString &publicKey, const QString &fingerprint);
}
