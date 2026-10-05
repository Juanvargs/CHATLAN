#pragma once
#include <QString>
#include <QJsonObject>
#include <QByteArray>
namespace chat {
QString dataRoot();
void privateDir(const QString &path);
QByteArray readFile(const QString &path);
void writeFile(const QString &path, const QByteArray &data, bool secret = true);
bool writeNewSecret(const QString &path, const QByteArray &data);
QJsonObject readJson(const QString &path);
void writeJson(const QString &path, const QJsonObject &object);
QString defaultServerDir();
}
