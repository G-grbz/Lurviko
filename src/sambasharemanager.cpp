#include "sambasharemanager.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHostInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <sys/stat.h>
#include <unistd.h>
#include <memory>

namespace {
QString message(const char* en, const char* tr)
{
    const auto fallback = QLocale::system().language() == QLocale::Turkish ? "tr" : "en";
    return QString::fromUtf8(QSettings().value("ui/language", fallback).toString() == "tr" ? tr : en);
}
bool safeText(const QString& value, int limit)
{
    return value.size() <= limit && !value.contains(QChar::Null)
        && !value.contains('\n') && !value.contains('\r');
}
QString localPath(const QString& value)
{
    const QUrl url(value);
    if (url.isLocalFile()) return QDir::cleanPath(url.toLocalFile());
    return url.scheme().isEmpty() && QDir::isAbsolutePath(value) ? QDir::cleanPath(value) : QString();
}
QString ownedDirectory(const QString& path)
{
    struct stat st {};
    const auto native = QFile::encodeName(path);
    if (::lstat(native.constData(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != ::getuid()) return {};
    return QFileInfo(path).canonicalFilePath();
}
QString normalizedAcl(const QString& acl)
{
    auto entries = acl.split(',', Qt::SkipEmptyParts);
    for (auto& entry : entries) entry = entry.trimmed();
    return entries.join(',');
}
bool simpleAcl(QString acl)
{
    acl = normalizedAcl(acl).toCaseFolded();
    return acl == "everyone:r" || acl == "everyone:f" || acl == "s-1-1-0:r" || acl == "s-1-1-0:f";
}
}

SambaShareManager::SambaShareManager(QObject* parent)
    : SambaShareManager(QStandardPaths::findExecutable("net"), parent) {}
SambaShareManager::SambaShareManager(const QString& program, QObject* parent)
    : QObject(parent), m_netProgram(program) {}
SambaShareManager::~SambaShareManager()
{
    for (auto* process : findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly)) {
        process->disconnect();
        for (auto* timer : process->findChildren<QTimer*>()) timer->stop();
        process->setParent(nullptr);
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
        if (process->state() == QProcess::NotRunning) process->deleteLater();
    }
}

bool SambaShareManager::editable() const
{
    if (!m_available || m_path.isEmpty()) return false;
    if (!shared()) return true;
    const auto record = m_shares.value(m_selected);
    return simpleAcl(record.acl) && !record.guest;
}
bool SambaShareManager::readOnly() const
{
    return !shared() || normalizedAcl(m_shares.value(m_selected).acl).endsWith(":R", Qt::CaseInsensitive);
}
QString SambaShareManager::shareName() const { return m_shares.value(m_selected).name; }
QString SambaShareManager::comment() const { return m_shares.value(m_selected).comment; }
QString SambaShareManager::address() const
{
    if (!shared()) return {};
    QUrl url; url.setScheme("smb"); url.setHost(QHostInfo::localHostName()); url.setPath('/' + shareName());
    return url.toString(QUrl::FullyEncoded);
}
QStringList SambaShareManager::shareNames() const
{
    QStringList names;
    for (const auto& share : m_shares) if (share.path == m_path) names.append(share.name);
    names.sort(Qt::CaseInsensitive);
    return names;
}
QVariantList SambaShareManager::localShares() const
{
    QVariantList result;
    auto names = m_shares.keys(); names.sort(Qt::CaseInsensitive);
    for (const auto& name : names) {
        const auto& share = m_shares[name];
        QUrl url; url.setScheme("smb"); url.setHost("localhost"); url.setPath('/' + share.name + '/');
        result.append(QVariantMap{{"name", share.name}, {"url", url.toString()}, {"path", share.path}});
    }
    return result;
}
void SambaShareManager::fail(const QString& error)
{
    m_error = error; m_busy = false; emit changed();
}
void SambaShareManager::clear()
{
    ++m_generation;
    m_pendingLocation.clear();
    if (m_process && !m_writing) m_process->kill();
    m_available = false; m_selected.clear(); m_path.clear(); m_shares.clear(); m_error.clear();
    if (!m_writing) m_busy = false;
    emit changed();
}
void SambaShareManager::run(const QStringList& args, bool writing,
                          std::function<void(bool, const QByteArray&, const QString&)> done)
{
    auto* process = new QProcess(this);
    m_process = process;
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    auto output = std::make_shared<QByteArray>();
    auto error = std::make_shared<QByteArray>();
    auto reason = std::make_shared<QString>();
    auto completed = std::make_shared<bool>(false);
    const auto finish = [this, process, timer, output, error, reason, completed, done](bool ok) {
        if (*completed) return;
        *completed = true; timer->stop();
        output->append(process->readAllStandardOutput().left(1024 * 1024 - output->size()));
        error->append(process->readAllStandardError().left(8192 - error->size()));
        if (m_process == process) m_process = nullptr;
        process->deleteLater();
        done(ok && reason->isEmpty(), *output,
             reason->isEmpty() ? QString::fromLocal8Bit(*error).trimmed() : *reason);
    };
    connect(process, &QProcess::readyReadStandardOutput, process, [process, output, reason] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 1024 * 1024) {
            output->truncate(1024 * 1024);
            *reason = message("Samba returned too much data.", "Samba çok fazla veri döndürdü.");
            process->kill();
        }
    });
    connect(process, &QProcess::readyReadStandardError, process, [process, error] {
        *error = (*error + process->readAllStandardError()).right(8192);
    });
    connect(timer, &QTimer::timeout, process, [process, reason] {
        *reason = message("Samba did not respond in time. Refresh to check the result.",
                          "Samba zamanında yanıt vermedi. Sonucu kontrol etmek için yenile.");
        process->kill();
    });
    connect(process, &QProcess::finished, process, [finish](int code, QProcess::ExitStatus status) {
        finish(status == QProcess::NormalExit && code == 0);
    });
    connect(process, &QProcess::errorOccurred, process, [process, reason, finish](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) { *reason = process->errorString(); finish(false); }
    });
    timer->start(writing ? 15000 : 5000);
    process->start(m_netProgram, args);
}
void SambaShareManager::inspect(const QString& location)
{
    if (m_writing) { m_pendingLocation = location; return; }
    clear();
    const auto token = m_generation;
    const auto path = localPath(location);
    if (path.isEmpty() || !safeText(path, 4096)) {
        fail(message("Choose one local folder to share.", "Paylaşmak için tek bir yerel klasör seç.")); return;
    }
    if (m_netProgram.isEmpty()) {
        fail(message("Install Samba to manage folder sharing.", "Klasör paylaşımını yönetmek için Samba kurulmalı.")); return;
    }
    m_busy = true; emit changed();
    auto* worker = new QFutureWatcher<QString>(this);
    connect(worker, &QFutureWatcher<QString>::finished, this, [this, worker, token] {
        const auto path = worker->result(); worker->deleteLater();
        if (token != m_generation) return;
        if (path.isEmpty()) {
            fail(message("Only local folders you own can be shared. Symbolic links are not shared here.",
                         "Yalnızca sahibi olduğun yerel klasörler paylaşılabilir. Sembolik bağlantılar burada paylaşılmaz.")); return;
        }
        m_path = path;
        readShares(token);
    });
    worker->setFuture(QtConcurrent::run([path] { return ownedDirectory(path); }));
}
void SambaShareManager::listShares()
{
    if (m_writing) return;
    clear();
    if (m_netProgram.isEmpty()) {
        fail(message("Install Samba to list local shares.", "Yerel paylaşımları listelemek için Samba kurulmalı.")); return;
    }
    m_busy = true; emit changed();
    readShares(m_generation);
}
void SambaShareManager::readShares(quint64 token)
{
    run({"--long", "--", "usershare", "info"}, false,
            [this, token](bool ok, const QByteArray& output, const QString& error) {
                if (token != m_generation) return;
                if (!ok) { fail(message("Samba sharing is unavailable: ", "Samba paylaşımı kullanılamıyor: ") + error); return; }
                Share share;
                const auto flush = [&] {
                    if (!share.name.isEmpty()) m_shares.insert(share.name.toCaseFolded(), share);
                };
                for (const auto& raw : QString::fromUtf8(output).split('\n')) {
                    const auto line = raw.trimmed();
                    if (line.startsWith('[') && line.endsWith(']')) {
                        flush(); share = {}; share.name = line.mid(1, line.size() - 2);
                    } else {
                        const int separator = line.indexOf('=');
                        if (separator < 0) continue;
                        const auto key = line.left(separator).trimmed().toLower();
                        const auto value = line.mid(separator + 1).trimmed();
                        if (key == "path") share.path = QDir::cleanPath(value);
                        else if (key == "comment") share.comment = value;
                        else if (key == "usershare_acl") share.acl = normalizedAcl(value);
                        else if (key == "guest_ok") share.guest = value == "y" || value == "yes";
                    }
                }
                flush();
                const auto names = shareNames();
                if (!names.isEmpty()) m_selected = names.first().toCaseFolded();
                m_available = true; m_busy = false; emit changed();
            });
}
void SambaShareManager::selectShare(const QString& name)
{
    if (m_busy || !m_shares.contains(name.toCaseFolded()) || m_shares.value(name.toCaseFolded()).path != m_path) return;
    m_selected = name.toCaseFolded(); m_error.clear(); emit changed();
}
void SambaShareManager::mutate(const QStringList& args, const QString& successMessage)
{
    const auto path = m_path;
    const auto token = m_generation;
    m_busy = m_writing = true; m_error.clear(); emit changed();
    // Recheck ownership away from the GUI immediately before running net.
    auto* worker = new QFutureWatcher<QString>(this);
    connect(worker, &QFutureWatcher<QString>::finished, this, [this, worker, args, path, token, successMessage] {
        const auto checked = worker->result(); worker->deleteLater();
        const auto finished = [this, path, token, successMessage](bool ok, const QByteArray&, const QString& error) {
            m_busy = m_writing = false;
            const auto feedback = ok ? successMessage
                : message("Sharing could not be updated: ", "Paylaşım güncellenemedi: ") + error;
            if (token == m_generation) m_error = ok ? QString() : feedback;
            emit changed(); emit operationFinished(ok, feedback);
            const auto next = m_pendingLocation; m_pendingLocation.clear();
            if (!next.isEmpty()) inspect(next);
            else if (token == m_generation && ok) inspect(path);
        };
        if (checked != path) {
            finished(false, {}, message("The folder changed. Refresh and try again.", "Klasör değişti. Yenileyip tekrar dene.")); return;
        }
        run(args, true, finished);
    });
    worker->setFuture(QtConcurrent::run([path] { return ownedDirectory(path); }));
}
void SambaShareManager::save(const QString& name, const QString& comment, bool readOnly)
{
    if (m_busy) return;
    static const QRegularExpression validName(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N} _.-]{0,79}$"));
    const auto key = name.toCaseFolded();
    if (!editable() || !validName.match(name).hasMatch() || name.endsWith(' ') || name.endsWith('.')
        || !safeText(comment, 256) || QStringList{"global", "homes", "printers"}.contains(key)) {
        fail(message("Check the share name and comment. Custom permissions must be managed separately.",
                     "Paylaşım adını ve açıklamayı kontrol et. Özel izinler ayrı yönetilmelidir.")); return;
    }
    if ((shared() && key != m_selected) || (m_shares.contains(key) && m_shares.value(key).path != m_path)) {
        fail(message("This name is already in use. Remove an existing share before renaming it.",
                     "Bu ad zaten kullanılıyor. Yeniden adlandırmadan önce mevcut paylaşımı kaldır.")); return;
    }
    mutate({"--", "usershare", "add", name, m_path, comment,
            readOnly ? "Everyone:R" : "Everyone:F", "guest_ok=n"},
           message("Folder sharing updated.", "Klasör paylaşımı güncellendi."));
}
void SambaShareManager::remove()
{
    if (m_busy || !m_available || !shared()) return;
    mutate({"--", "usershare", "delete", shareName()},
           message("Folder sharing removed.", "Klasör paylaşımı kaldırıldı."));
}
