#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

// Optional header extension. Never change legacy blob/manifest magic or AAD.
namespace VaultRecovery {
QByteArray generateKey();
QString encodeKey(const QByteArray &key);
bool decodeKey(const QString &text, QByteArray *key);
bool hasEnvelope(const QJsonObject &header);
bool wrap(QJsonObject *header, const QByteArray &master, const QByteArray &key);
bool unwrap(const QJsonObject &header, const QByteArray &key, QByteArray *master);
}
