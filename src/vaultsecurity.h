#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <memory>
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
class RuntimeDirectory {
public:
    ~RuntimeDirectory();
    RuntimeDirectory(const RuntimeDirectory &) = delete;
    RuntimeDirectory &operator=(const RuntimeDirectory &) = delete;
    QString path() const { return m_path; }
    bool clearContents() const;
private:
    friend std::unique_ptr<RuntimeDirectory> createRuntimeDirectory(const QString &preferredParent);
    RuntimeDirectory(int parent, int directory, int lock, QString name, QString path);
    int m_parentFd;
    int m_directoryFd;
    int m_lockFd;
    QString m_name;
    QString m_path;
};
std::unique_ptr<RuntimeDirectory> createRuntimeDirectory(const QString &preferredParent);
struct CleanupResult { int removed = 0; int skipped = 0; int failed = 0; };
// Explicit parents make the sweep testable without scanning unrelated sessions.
CleanupResult cleanupStaleRuntimeDirectories(const QStringList &parents, const QString &currentDirectory = {});
QStringList runtimeCleanupParents(const QString &preferredParent);
bool containsFile(const QString &directory, const QString &path);
}
