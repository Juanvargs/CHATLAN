#include "paths.h"
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <QJsonDocument>
#include <stdexcept>
#include <unistd.h>
#include <cerrno>

namespace chat {
// Devuelve el directorio de datos XDG del usuario para los perfiles del cliente.
QString dataRoot() {
    auto base = qEnvironmentVariable("XDG_DATA_HOME", QDir::homePath() + "/.local/share");
    return base + "/chat-lan-cpp";
}
QString defaultServerDir() { return dataRoot() + "/server"; }
// Crea el directorio con permisos restringidos al propietario.
void privateDir(const QString &path) {
    if (!QDir().mkpath(path) || !QFile::setPermissions(path, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner))
        throw std::runtime_error("No se pudo preparar el directorio privado");
}
// Lee el contenido de un archivo o informa el fallo mediante excepción.
QByteArray readFile(const QString &path) {
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) throw std::runtime_error(("No se pudo leer " + path).toStdString());
    return f.readAll();
}
// Escribe un archivo y aplica permisos privados cuando contiene secretos.
void writeFile(const QString &path, const QByteArray &data, bool secret) {
    privateDir(QFileInfo(path).absolutePath()); QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) throw std::runtime_error("No se pudo escribir archivo");
    f.setPermissions(secret ? QFile::ReadOwner | QFile::WriteOwner : QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther);
    if (f.write(data) != data.size() || !f.commit()) throw std::runtime_error("No se pudo guardar archivo");
}
// Crea una clave nueva sin reemplazar una existente y protege sus permisos.
bool writeNewSecret(const QString &path, const QByteArray &data) {
    privateDir(QFileInfo(path).absolutePath()); QTemporaryFile f(QFileInfo(path).absolutePath() + "/.secret-XXXXXX");
    if (!f.open() || f.write(data) != data.size() || !f.flush() || ::fsync(f.handle()) != 0) throw std::runtime_error("No se pudo guardar clave");
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    if (::link(QFile::encodeName(f.fileName()).constData(), QFile::encodeName(path).constData()) == 0) return true;
    if (errno == EEXIST) return false;
    throw std::runtime_error("No se pudo crear clave privada");
}
// Carga un objeto JSON desde disco y valida que sea un objeto.
QJsonObject readJson(const QString &path) {
    if (!QFile::exists(path)) return {};
    QJsonParseError error; auto doc = QJsonDocument::fromJson(readFile(path), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) throw std::runtime_error("Configuración JSON dañada");
    return doc.object();
}
void writeJson(const QString &path, const QJsonObject &object) { writeFile(path, QJsonDocument(object).toJson()); }
}
