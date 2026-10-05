#include "crypto.h"
#include "paths.h"
#include "protocol.h"
#include <QFile>
#include <QSslCertificate>
#include <QCryptographicHash>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509v3.h>
#include <stdexcept>

namespace chat {
template<class T, auto Free> using Ptr = std::unique_ptr<T, decltype(Free)>;
static QByteArray pem(EVP_PKEY *key) {
    Ptr<BIO, BIO_free> bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!bio || !PEM_write_bio_PrivateKey(bio.get(), key, nullptr, nullptr, 0, nullptr, nullptr)) throw std::runtime_error("Error PEM");
    char *data; auto size = BIO_get_mem_data(bio.get(), &data); return QByteArray(data, size);
}
Identity::Identity(const QString &directory) {
    auto path = directory + "/identity.key";
    if (!QFile::exists(path)) {
        Key generated(EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519"), EVP_PKEY_free);
        if (!generated) throw std::runtime_error("Error generando Ed25519");
        writeNewSecret(path, pem(generated.get()));
    }
    auto bytes = readFile(path);
    Ptr<BIO, BIO_free> bio(BIO_new_mem_buf(bytes.constData(), bytes.size()), BIO_free);
    key = Key(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr), EVP_PKEY_free);
    if (!key || EVP_PKEY_id(key.get()) != EVP_PKEY_ED25519) throw std::runtime_error("Identidad dañada: recuperación requerida");
}
QString Identity::publicKey() const {
    unsigned char data[32]; size_t size = sizeof data;
    if (!EVP_PKEY_get_raw_public_key(key.get(), data, &size) || size != 32) throw std::runtime_error("Error clave pública");
    return QString::fromLatin1(QByteArray(reinterpret_cast<char*>(data), size).toBase64());
}
QByteArray Identity::sign(const QByteArray &payload) const {
    Ptr<EVP_MD_CTX, EVP_MD_CTX_free> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    QByteArray signature(64, '\0'); size_t size = 64;
    if (!context || EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1 ||
        EVP_DigestSign(context.get(), reinterpret_cast<unsigned char*>(signature.data()), &size,
            reinterpret_cast<const unsigned char*>(payload.constData()), payload.size()) != 1) throw std::runtime_error("Error firma");
    signature.resize(size); return signature;
}
bool verify(const QString &publicKey, const QByteArray &payload, const QByteArray &signature) {
    auto raw = fromBase64(publicKey, 32);
    Key key(EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, reinterpret_cast<const unsigned char*>(raw.constData()), raw.size()), EVP_PKEY_free);
    Ptr<EVP_MD_CTX, EVP_MD_CTX_free> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    return key && context && EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) == 1 &&
        EVP_DigestVerify(context.get(), reinterpret_cast<const unsigned char*>(signature.constData()), signature.size(),
            reinterpret_cast<const unsigned char*>(payload.constData()), payload.size()) == 1;
}
QString randomNonce() {
    QByteArray nonce(32, '\0'); if (RAND_bytes(reinterpret_cast<unsigned char*>(nonce.data()), nonce.size()) != 1) throw std::runtime_error("Error aleatorio");
    return QString::fromLatin1(nonce.toBase64());
}
QString certificateFingerprint(const QString &path) {
    QSslCertificate cert(readFile(path)); if (cert.isNull()) throw std::runtime_error("Certificado inválido");
    return QString::fromLatin1(cert.digest(QCryptographicHash::Sha256).toHex());
}
void initializeTls(const QString &directory) {
    privateDir(directory);
    if (QFile::exists(directory + "/server.pem") || QFile::exists(directory + "/server.key")) throw std::runtime_error("Certificado o clave existentes: no se reemplazan");
    Key key(EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "prime256v1"), EVP_PKEY_free);
    Ptr<X509, X509_free> cert(X509_new(), X509_free);
    if (!key || !cert) throw std::runtime_error("Error TLS");
    X509_set_version(cert.get(), 2);
    auto serial = QByteArray::fromBase64(randomNonce().toLatin1()).left(16); serial[0] = char((quint8(serial[0]) & 0x7f) | 1);
    Ptr<BIGNUM, BN_free> number(BN_bin2bn(reinterpret_cast<const unsigned char*>(serial.constData()), serial.size(), nullptr), BN_free);
    Ptr<ASN1_INTEGER, ASN1_INTEGER_free> integer(BN_to_ASN1_INTEGER(number.get(), nullptr), ASN1_INTEGER_free);
    X509_set_serialNumber(cert.get(), integer.get());
    X509_gmtime_adj(X509_getm_notBefore(cert.get()), -300); X509_gmtime_adj(X509_getm_notAfter(cert.get()), 365L * 86400);
    X509_set_pubkey(cert.get(), key.get()); auto name = X509_get_subject_name(cert.get());
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, reinterpret_cast<const unsigned char*>("Chat LAN C++"), -1, -1, 0);
    X509_set_issuer_name(cert.get(), name);
    X509V3_CTX ctx; X509V3_set_ctx(&ctx, cert.get(), cert.get(), nullptr, nullptr, 0);
    auto extension = [&](int nid, const char *value) {
        Ptr<X509_EXTENSION, X509_EXTENSION_free> ext(X509V3_EXT_conf_nid(nullptr, &ctx, nid, value), X509_EXTENSION_free);
        if (!ext || X509_add_ext(cert.get(), ext.get(), -1) != 1) throw std::runtime_error("Error extensión TLS");
    };
    extension(NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
    extension(NID_basic_constraints, "critical,CA:TRUE,pathlen:0");
    extension(NID_key_usage, "critical,digitalSignature,keyCertSign,cRLSign");
    extension(NID_ext_key_usage, "serverAuth");
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0) throw std::runtime_error("Error certificado TLS");
    Ptr<BIO, BIO_free> bio(BIO_new(BIO_s_mem()), BIO_free);
    if (!PEM_write_bio_X509(bio.get(), cert.get())) throw std::runtime_error("Error PEM TLS");
    char *data; auto size = BIO_get_mem_data(bio.get(), &data);
    if (!writeNewSecret(directory + "/server.key", pem(key.get())) || !writeNewSecret(directory + "/server.pem", QByteArray(data, size))) throw std::runtime_error("Conflicto creando TLS");
}
}
