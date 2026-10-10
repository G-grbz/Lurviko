#pragma once
#include <QJsonObject>
#include <QString>
namespace VaultSecurity {
struct Argon2Parameters {
    quint32 iterations = 3;
    quint32 memoryKiB = 65536;
    quint32 lanes = 1;
    quint32 threads = 1;
};
// NIST SP 800-38D section 5.2.1.1: 2^39 - 256 bits per invocation.
constexpr qint64 MaxGcmPlaintextBytes = (qint64(1) << 36) - 32;
bool parseArgon2Parameters(const QJsonObject &header, Argon2Parameters *result);
bool isMemoryFilesystem(const QString &path);
QString createRuntimeDirectory(const QString &preferredParent);
bool containsFile(const QString &directory, const QString &path);
}
