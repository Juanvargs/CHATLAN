#pragma once
#include <QByteArray>
#include <QString>
#include <memory>
#include <openssl/evp.h>
namespace chat {
using Key = std::shared_ptr<EVP_PKEY>;
class Identity {
    Key key;
public:
    explicit Identity(const QString &directory);
    QString publicKey() const;
    QByteArray sign(const QByteArray &payload) const;
};
bool verify(const QString &publicKey, const QByteArray &payload, const QByteArray &signature);
QString randomNonce();
QString certificateFingerprint(const QString &path);
void initializeTls(const QString &directory);
}
