#include "sambasharemanager.h"
#include "sambashareindex.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHostInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QNetworkInterface>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <pwd.h>
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
bool isEveryone(const QString& name)
{
    const auto folded = name.trimmed().toCaseFolded();
    return folded == QLatin1String("everyone") || folded == QLatin1String("s-1-1-0");
}
// Local users and groups, DOMAIN\user and SIDs. Never ',' or ':' (the ACL
// separators), a leading '-' or control characters.
QString accountName(const QString& name)
{
    static const QRegularExpression valid(QStringLiteral("^[\\p{L}\\p{N}_][\\p{L}\\p{N}_.$@\\\\ -]{0,127}$"));
    const auto value = name.trimmed();
    if (isEveryone(value)) return QStringLiteral("Everyone");
    if (!valid.match(value).hasMatch() || value.endsWith(' ')) return {};
    return value;
}
bool simpleAcl(QString acl)
{
    acl = normalizedAcl(acl).toCaseFolded();
    return acl == "everyone:r" || acl == "everyone:f" || acl == "s-1-1-0:r" || acl == "s-1-1-0:f";
}
// Parses "name:R,other:F" into (name, access) pairs, keeping their order.
QList<QPair<QString, QString>> parseAcl(const QString& acl)
{
    QList<QPair<QString, QString>> result;
    for (const auto& raw : acl.split(',', Qt::SkipEmptyParts)) {
        const auto entry = raw.trimmed();
        const int separator = entry.lastIndexOf(':');
        if (separator <= 0) continue;
        const auto access = entry.mid(separator + 1).trimmed().toUpper();
        if (access != "R" && access != "F" && access != "D") continue;
        result.append({entry.left(separator).trimmed(), access});
    }
    return result;
}
QString shareNameError(const QString& name, const QString& comment)
{
    static const QRegularExpression validName(QStringLiteral("^[\\p{L}\\p{N}][\\p{L}\\p{N} _.-]{0,79}$"));
    const auto key = name.toCaseFolded();
    if (!validName.match(name).hasMatch() || name.endsWith(' ') || name.endsWith('.')
        || !safeText(comment, 256) || QStringList{"global", "homes", "printers"}.contains(key))
        return message("Check the share name and comment.", "Paylaşım adını ve açıklamayı kontrol et.");
    return {};
}
// Login accounts of this computer: UID_MIN..UID_MAX with a real shell.
QStringList readLocalUsers()
{
    uint minimum = 1000, maximum = 60000;
    QFile defs(QStringLiteral("/etc/login.defs"));
    if (defs.open(QIODevice::ReadOnly | QIODevice::Text)) {
        for (const auto& line : QString::fromUtf8(defs.read(256 * 1024)).split('\n')) {
            const auto parts = line.simplified().split(' ');
            if (parts.size() < 2) continue;
            bool ok = false;
            const uint value = parts.at(1).toUInt(&ok);
            if (!ok) continue;
            if (parts.first() == QLatin1String("UID_MIN")) minimum = value;
            else if (parts.first() == QLatin1String("UID_MAX")) maximum = value;
        }
    }
    QStringList users;
    QFile passwd(QStringLiteral("/etc/passwd"));
    if (!passwd.open(QIODevice::ReadOnly | QIODevice::Text)) return users;
    for (const auto& line : QString::fromUtf8(passwd.read(4 * 1024 * 1024)).split('\n')) {
        const auto fields = line.split(':');
        if (fields.size() < 7) continue;
        bool ok = false;
        const uint uid = fields.at(2).toUInt(&ok);
        const auto shell = fields.at(6).trimmed();
        if (!ok || uid < minimum || uid > maximum) continue;
        if (shell.endsWith(QLatin1String("nologin")) || shell.endsWith(QLatin1String("/false"))) continue;
        if (!fields.first().isEmpty()) users.append(fields.first());
    }
    users.removeDuplicates();
    users.sort(Qt::CaseInsensitive);
    return users;
}
QString currentUserName()
{
    if (const passwd* entry = ::getpwuid(::getuid()); entry && entry->pw_name)
        return QString::fromLocal8Bit(entry->pw_name);
    return qEnvironmentVariable("USER");
}

// --- Filesystem permissions for a share ---------------------------------
struct PermissionItem {
    QString path;
    mode_t current = 0;
    mode_t wanted = 0;
    bool owned = false;
    QString role; // "share", "owner" or "traverse"
};

QString modeOctal(mode_t mode) { return QString::asprintf("%04o", unsigned(mode & 07777)); }
QString modeText(mode_t mode)
{
    QString text;
    const char letters[] = "rwxrwxrwx";
    for (int i = 0; i < 9; ++i)
        text += (mode & (0400 >> i)) ? QChar::fromLatin1(letters[i]) : QChar('-');
    return text;
}

// 0 none, 1 read, 2 full: the strongest access given to anyone but the
// owner (they connect as other accounts and use the "others" bits), and
// the access of the owner's own account.
QPair<int, int> accessLevels(const QVariantList& entries, const QString& owner)
{
    int others = 0, self = 0;
    for (const auto& value : entries) {
        const auto entry = value.toMap();
        const auto access = entry.value("access").toString().toUpper();
        const int level = access == QLatin1String("F") ? 2 : access == QLatin1String("R") ? 1 : 0;
        if (!level) continue;
        const auto name = accountName(entry.value("name").toString());
        if (name.isEmpty()) continue;
        if (!owner.isEmpty() && name.compare(owner, Qt::CaseInsensitive) == 0) {
            self = std::max(self, level);
            continue;
        }
        others = std::max(others, level);
        // Everyone includes the owner's account as well.
        if (name == QLatin1String("Everyone")) self = std::max(self, level);
    }
    return {others, self};
}

QList<PermissionItem> permissionItems(const QString& folder, int others, int self)
{
    QList<PermissionItem> items;
    if (folder.isEmpty() || (!others && !self)) return items;
    const uid_t uid = ::getuid();
    struct stat st {};
    if (::lstat(QFile::encodeName(folder).constData(), &st) || !S_ISDIR(st.st_mode)) return items;
    mode_t need = 0;
    if (self) need |= S_IRUSR | S_IXUSR | (self == 2 ? S_IWUSR : 0);
    if (others) need |= S_IROTH | S_IXOTH | (others == 2 ? S_IWOTH : 0);
    if ((st.st_mode & need) != need)
        items.append({folder, st.st_mode & 07777, (st.st_mode | need) & 07777, st.st_uid == uid,
                      others ? QStringLiteral("share") : QStringLiteral("owner")});
    if (!others) return items;
    // Every parent must let others pass through (x), not list (r).
    QString path = folder;
    while (path != QLatin1String("/")) {
        path = QFileInfo(path).path();
        struct stat parent {};
        if (::lstat(QFile::encodeName(path).constData(), &parent) || !S_ISDIR(parent.st_mode)) break;
        if (!(parent.st_mode & S_IXOTH))
            items.append({path, parent.st_mode & 07777, (parent.st_mode | S_IXOTH) & 07777,
                          parent.st_uid == uid, QStringLiteral("traverse")});
    }
    return items;
}

// fchmod on a descriptor opened without following links: the folder that
// was checked is the folder that changes.
QString applyOwnedMode(const QString& path, mode_t wanted, bool exact = false, mode_t expected = 0)
{
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return QString::fromLocal8Bit(::strerror(errno));
    struct stat st {};
    QString error;
    if (::fstat(fd, &st) || st.st_uid != ::getuid()
            || (exact && (st.st_mode & 07777) != (expected & 07777)))
        error = message("The folder changed.", "Klasör değişti.");
    else if (::fchmod(fd, (exact ? wanted : (st.st_mode | wanted)) & 07777))
        error = QString::fromLocal8Bit(::strerror(errno));
    ::close(fd);
    return error;
}

// What fixPermissions changed for a share: { path: (before, after) }, so
// that only those bits can be taken back later.
struct PermissionRecord { mode_t before = 0; mode_t after = 0; };
QString recordKey(const QString& share)
{
    return QStringLiteral("sambaPermissions/") + QString::fromLatin1(QUrl::toPercentEncoding(share));
}
QHash<QString, PermissionRecord> readRecords(const QString& share)
{
    QHash<QString, PermissionRecord> records;
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    const auto array = QJsonDocument::fromJson(settings.value(recordKey(share)).toByteArray()).array();
    for (const auto& value : array) {
        const auto object = value.toObject();
        const auto path = object.value("path").toString();
        if (!path.startsWith('/')) continue;
        records.insert(path, {mode_t(object.value("before").toInt() & 07777), mode_t(object.value("after").toInt() & 07777)});
    }
    return records;
}
void writeRecords(const QString& share, const QHash<QString, PermissionRecord>& records)
{
    QSettings settings(QStringLiteral("Lurviko"), QStringLiteral("Lurviko"));
    if (records.isEmpty()) { settings.remove(recordKey(share)); return; }
    QJsonArray array;
    for (auto it = records.cbegin(); it != records.cend(); ++it)
        array.append(QJsonObject{{"path", it.key()}, {"before", int(it.value().before)}, {"after", int(it.value().after)}});
    settings.setValue(recordKey(share), QJsonDocument(array).toJson(QJsonDocument::Compact));
}
// First change keeps the original mode; later ones move "after".
void rememberChange(const QString& share, const QString& path, mode_t before, mode_t after)
{
    auto records = readRecords(share);
    const auto existing = records.constFind(path);
    records.insert(path, {existing == records.cend() ? before : existing->before, after});
    writeRecords(share, records);
}

struct Environment { bool guests = false; int maxShares = -1; QString service = QStringLiteral("unknown"); };

QByteArray runSync(const QString& program, const QStringList& args, int timeoutMs, int* exitCode = nullptr)
{
    if (program.isEmpty()) return {};
    QProcess process;
    process.start(program, args);
    if (!process.waitForStarted(timeoutMs)) return {};
    process.closeWriteChannel();
    if (!process.waitForFinished(timeoutMs)) { process.kill(); process.waitForFinished(1000); return {}; }
    if (exitCode) *exitCode = process.exitCode();
    return process.readAllStandardOutput().left(64 * 1024);
}

Environment probeEnvironment(const QString& testparm, const QString& systemctl)
{
    Environment environment;
    const auto guests = QString::fromUtf8(runSync(testparm, {"-s", "--parameter-name", "usershare allow guests"}, 4000))
                            .trimmed().toCaseFolded();
    environment.guests = guests == QLatin1String("yes") || guests == QLatin1String("true");
    bool ok = false;
    const int maximum = QString::fromUtf8(runSync(testparm, {"-s", "--parameter-name", "usershare max shares"}, 4000))
                            .trimmed().toInt(&ok);
    environment.maxShares = ok ? maximum : -1;
    const auto states = QString::fromUtf8(runSync(systemctl, {"is-active", "smb.service", "smbd.service"}, 4000))
                            .split('\n', Qt::SkipEmptyParts);
    if (states.contains(QStringLiteral("active"))) environment.service = QStringLiteral("active");
    else if (!states.isEmpty()) environment.service = QStringLiteral("inactive");
    return environment;
}

QString jsonText(const QJsonValue& value)
{
    if (value.isString()) return value.toString();
    if (value.isDouble()) return QString::number(value.toDouble());
    if (value.isObject()) {
        const auto object = value.toObject();
        for (const auto key : {"cipher", "name", "value"})
            if (object.contains(QLatin1String(key))) return object.value(QLatin1String(key)).toString();
    }
    return {};
}

// smbstatus --json: sessions, tree connections and open files.
QVariantMap parseStatus(const QByteArray& output, const QString& errorText)
{
    QVariantMap result;
    const int start = output.indexOf('{');
    const auto document = QJsonDocument::fromJson(start >= 0 ? output.mid(start) : QByteArray());
    const auto root = document.object();
    const bool rootOnly = output.contains("only works as root") || errorText.contains(QLatin1String("only works as root"));
    if (document.isNull() || (!root.contains("sessions") && !root.contains("tcons"))) {
        result.insert("requiresAdmin", rootOnly || !document.isNull());
        if (!rootOnly && document.isNull())
            result.insert("error", errorText.isEmpty()
                ? message("Samba status is unavailable.", "Samba durumu alınamadı.") : errorText);
        result.insert("version", root.value("version").toString());
        return result;
    }
    QVariantList sessions;
    for (const auto& value : root.value("sessions").toObject()) {
        const auto session = value.toObject();
        sessions.append(QVariantMap{
            {"user", session.value("username").toString()},
            {"group", session.value("groupname").toString()},
            {"machine", session.value("remote_machine").toString()},
            {"host", session.value("hostname").toString()},
            {"protocol", session.value("session_dialect").toString()},
            {"encryption", jsonText(session.value("encryption"))},
            {"signing", jsonText(session.value("signing"))}});
    }
    QVariantList connections;
    for (const auto& value : root.value("tcons").toObject()) {
        const auto tcon = value.toObject();
        connections.append(QVariantMap{
            {"share", tcon.value("service").toString()},
            {"machine", tcon.value("machine").toString()},
            {"connectedAt", tcon.value("connected_at").toString()},
            {"encryption", jsonText(tcon.value("encryption"))}});
    }
    result.insert("sessions", sessions);
    result.insert("connections", connections);
    result.insert("openFiles", root.value("open_files").toObject().size());
    result.insert("requiresAdmin", false);
    result.insert("version", root.value("version").toString());
    return result;
}
}

SambaShareManager::SambaShareManager(QObject* parent)
    : SambaShareManager(QStandardPaths::findExecutable("net"), parent) {}
SambaShareManager::SambaShareManager(const QString& program, QObject* parent)
    : QObject(parent), m_netProgram(program)
{
    initialiseHelpers();
}
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

void SambaShareManager::initialiseHelpers()
{
    // Absolute paths only: pkexec runs the program it is given as root.
    const auto find = [](const char* name) { return QStandardPaths::findExecutable(QString::fromLatin1(name)); };
    m_pkexec = find("pkexec");
    m_smbpasswd = find("smbpasswd");
    m_smbstatus = find("smbstatus");
    m_testparm = find("testparm");
    m_systemctl = find("systemctl");
    m_chmod = find("chmod");
    m_userName = currentUserName();
    m_localUsers = readLocalUsers();
    if (!m_userName.isEmpty() && !m_localUsers.contains(m_userName)) m_localUsers.prepend(m_userName);
}

void SambaShareManager::setHelperPrograms(const QString& pkexec, const QString& smbpasswd, const QString& smbstatus,
                                          const QString& testparm, const QString& systemctl)
{
    m_pkexec = pkexec; m_smbpasswd = smbpasswd; m_smbstatus = smbstatus;
    m_testparm = testparm; m_systemctl = systemctl;
    m_environmentLoaded = false;
}

void SambaShareManager::setLocalUsersForTest(const QStringList& users)
{
    m_localUsers = users;
    if (!m_userName.isEmpty() && !m_localUsers.contains(m_userName)) m_localUsers.prepend(m_userName);
    emit changed();
}

bool SambaShareManager::editable() const
{
    return m_available && !m_path.isEmpty();
}
bool SambaShareManager::readOnly() const
{
    return !shared() || normalizedAcl(m_shares.value(m_selected).acl).endsWith(":R", Qt::CaseInsensitive);
}
bool SambaShareManager::guestOk() const { return shared() && m_shares.value(m_selected).guest; }
bool SambaShareManager::passwordChangeAvailable() const
{
    return !m_pkexec.isEmpty() && !m_smbpasswd.isEmpty() && !m_userName.isEmpty();
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

QString SambaShareManager::normalizedAccount(const QString& name) const
{
    return accountName(name);
}

QVariantList SambaShareManager::aclEntries() const
{
    // Existing rules first in their saved form; a new share starts with
    // Everyone: read only, like Dolphin.
    QHash<QString, QString> access;
    QStringList custom;
    if (shared()) {
        for (const auto& [name, value] : parseAcl(m_shares.value(m_selected).acl)) {
            const auto key = isEveryone(name) ? QStringLiteral("everyone") : name.toCaseFolded();
            access.insert(key, value);
            if (key != QLatin1String("everyone")) custom.append(name);
        }
    } else {
        access.insert(QStringLiteral("everyone"), QStringLiteral("R"));
    }
    QVariantList result;
    result.append(QVariantMap{{"name", QStringLiteral("Everyone")}, {"access", access.value("everyone")},
                              {"kind", QStringLiteral("everyone")}, {"current", false}});
    QStringList listed;
    for (const auto& user : m_localUsers) {
        listed.append(user.toCaseFolded());
        result.append(QVariantMap{{"name", user}, {"access", access.value(user.toCaseFolded())},
                                  {"kind", QStringLiteral("user")}, {"current", user == m_userName}});
    }
    for (const auto& name : custom) {
        if (listed.contains(name.toCaseFolded())) continue;
        listed.append(name.toCaseFolded());
        result.append(QVariantMap{{"name", name}, {"access", access.value(name.toCaseFolded())},
                                  {"kind", QStringLiteral("custom")}, {"current", false}});
    }
    return result;
}

QString SambaShareManager::aclString(const QVariantList& entries, QString* error)
{
    QStringList parts, seen;
    bool grants = false;
    for (const auto& value : entries) {
        const auto entry = value.toMap();
        const auto access = entry.value("access").toString().toUpper();
        if (access.isEmpty()) continue;
        const auto name = accountName(entry.value("name").toString());
        if (name.isEmpty() || (access != "R" && access != "F" && access != "D")) {
            if (error) *error = message("An access rule has an invalid user name.",
                                        "Bir erişim kuralında geçersiz kullanıcı adı var.");
            return {};
        }
        if (seen.contains(name.toCaseFolded())) continue;
        seen.append(name.toCaseFolded());
        grants = grants || access != "D";
        parts.append(name + ':' + access);
    }
    if (!grants) {
        if (error) *error = message("Give at least one user read or full access.",
                                    "En az bir kullanıcıya okuma veya tam erişim ver.");
        return {};
    }
    return parts.join(',');
}

QVariantList SambaShareManager::shareAddresses() const
{
    QVariantList result;
    const auto name = shared() ? shareName() : QString();
    if (name.isEmpty()) return result;
    const auto add = [&](const QString& label, const QString& host, bool ipv6 = false) {
        QUrl url; url.setScheme("smb"); url.setHost(host); url.setPath('/' + name);
        // Windows reaches IPv6 hosts through the ipv6-literal.net form.
        const auto uncHost = ipv6 ? QString(host).replace(':', '-') + QStringLiteral(".ipv6-literal.net") : host;
        result.append(QVariantMap{{"label", label}, {"url", url.toString(QUrl::FullyEncoded)},
                                  {"unc", QStringLiteral("\\\\") + uncHost + '\\' + name}});
    };
    const auto host = QHostInfo::localHostName();
    add(message("Computer name", "Bilgisayar adı"), host);
    if (!host.contains('.')) add(message("Local network (mDNS)", "Yerel ağ (mDNS)"), host + QStringLiteral(".local"));
    for (const auto& interface : QNetworkInterface::allInterfaces()) {
        const auto flags = interface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
                || (flags & QNetworkInterface::IsLoopBack))
            continue;
        for (const auto& entry : interface.addressEntries()) {
            const auto ip = entry.ip();
            if (ip.isLoopback() || ip.isNull()) continue;
            const bool v6 = ip.protocol() == QAbstractSocket::IPv6Protocol;
            if (v6 && ip.isLinkLocal()) continue;
            add(interface.humanReadableName() + (v6 ? QStringLiteral(" · IPv6") : QStringLiteral(" · IPv4")),
                ip.toString(), v6);
        }
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
void SambaShareManager::refreshEnvironment()
{
    m_environmentLoaded = true;
    auto* worker = new QFutureWatcher<Environment>(this);
    connect(worker, &QFutureWatcher<Environment>::finished, this, [this, worker] {
        const auto environment = worker->result(); worker->deleteLater();
        m_guestsAllowed = environment.guests;
        m_maxShares = environment.maxShares;
        m_serviceState = environment.service;
        emit environmentChanged();
    });
    worker->setFuture(QtConcurrent::run(probeEnvironment, m_testparm, m_systemctl));
}
void SambaShareManager::inspect(const QString& location)
{
    if (m_writing) { m_pendingLocation = location; return; }
    clear();
    if (!m_environmentLoaded) refreshEnvironment();
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
            // Badges follow at once, even where the directory is not watched.
            if (ok) SambaShareIndex::instance()->refresh();
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
    const auto key = name.toCaseFolded();
    const auto record = m_shares.value(m_selected);
    // The simple form never replaces custom rules or guest access.
    if (!editable() || (shared() && (!simpleAcl(record.acl) || record.guest)) || !shareNameError(name, comment).isEmpty()) {
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
void SambaShareManager::saveShare(const QString& name, const QString& comment,
                                  const QVariantList& entries, bool guestOk)
{
    if (m_busy) return;
    if (!editable()) {
        fail(message("Choose one local folder to share.", "Paylaşmak için tek bir yerel klasör seç.")); return;
    }
    if (const auto problem = shareNameError(name, comment); !problem.isEmpty()) { fail(problem); return; }
    const auto key = name.toCaseFolded();
    if ((shared() && key != m_selected) || (m_shares.contains(key) && m_shares.value(key).path != m_path)) {
        fail(message("This name is already in use. Remove an existing share before renaming it.",
                     "Bu ad zaten kullanılıyor. Yeniden adlandırmadan önce mevcut paylaşımı kaldır.")); return;
    }
    // Guests are only offered when smb.conf allows them; an existing guest
    // share keeps working even if detection failed.
    if (guestOk && !m_guestsAllowed && !this->guestOk()) {
        fail(message("Guest access is disabled in the system Samba configuration.",
                     "Konuk erişimi sistemin Samba yapılandırmasında kapalı.")); return;
    }
    QString problem;
    const auto acl = aclString(entries, &problem);
    if (acl.isEmpty()) { fail(problem); return; }
    mutate({"--", "usershare", "add", name, m_path, comment, acl,
            guestOk ? QStringLiteral("guest_ok=y") : QStringLiteral("guest_ok=n")},
           message("Folder sharing updated.", "Klasör paylaşımı güncellendi."));
}
void SambaShareManager::remove()
{
    if (m_busy || !m_available || !shared()) return;
    mutate({"--", "usershare", "delete", shareName()},
           message("Folder sharing removed.", "Klasör paylaşımı kaldırıldı."));
}

void SambaShareManager::changePassword(const QString& password)
{
    if (m_passwordBusy) return;
    if (!passwordChangeAvailable()) {
        emit passwordFinished(false, message("Samba password tools or polkit (pkexec) are not installed.",
                                             "Samba parola araçları veya polkit (pkexec) kurulu değil."));
        return;
    }
    if (password.isEmpty() || password.size() > 256 || password.contains('\n')
            || password.contains('\r') || password.contains(QChar::Null)) {
        emit passwordFinished(false, message("The password is empty, too long or contains a line break.",
                                             "Parola boş, çok uzun veya satır sonu içeriyor."));
        return;
    }
    m_passwordBusy = true; emit passwordBusyChanged();
    auto* process = new QProcess(this);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    auto error = std::make_shared<QByteArray>();
    auto secret = std::make_shared<QByteArray>((password + '\n' + password + '\n').toUtf8());
    const auto wipe = [secret] { secret->fill('\0'); secret->clear(); };
    auto completed = std::make_shared<bool>(false);
    const auto done = [this, process, timer, error, wipe, completed](bool ok, int code, bool timedOut) {
        if (*completed) return;
        *completed = true;
        timer->stop();
        wipe();
        process->deleteLater();
        m_passwordBusy = false; emit passwordBusyChanged();
        QString feedback;
        if (ok) feedback = message("Samba password saved.", "Samba parolası kaydedildi.");
        else if (timedOut) feedback = message("Authorization timed out.", "Yetkilendirme zaman aşımına uğradı.");
        else if (code == 126) feedback = message("Authorization was cancelled.", "Yetkilendirme iptal edildi.");
        else if (code == 127) feedback = message("Not authorized to change the Samba password.",
                                                 "Samba parolasını değiştirme yetkin yok.");
        else feedback = message("The Samba password could not be changed: ", "Samba parolası değiştirilemedi: ")
                        + QString::fromLocal8Bit(*error).trimmed();
        emit passwordFinished(ok, feedback);
    };
    connect(process, &QProcess::readyReadStandardError, process, [process, error] {
        *error = (*error + process->readAllStandardError()).right(4096);
    });
    // stdout of smbpasswd -s is not needed; drain it so it never blocks.
    connect(process, &QProcess::readyReadStandardOutput, process, [process] { process->readAllStandardOutput(); });
    connect(process, &QProcess::started, process, [process, secret] {
        process->write(*secret);
        process->closeWriteChannel();
        secret->fill('\0');
    });
    connect(process, &QProcess::finished, process, [done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0, code, false);
    });
    connect(process, &QProcess::errorOccurred, process, [process, error, done](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart) return;
        *error = process->errorString().toUtf8();
        done(false, -1, false);
    });
    connect(timer, &QTimer::timeout, process, [process, done] {
        process->kill();
        done(false, -1, true);
    });
    timer->start(180000);
    // -a adds the Samba account when missing and otherwise sets the new
    // password; -s reads it (twice) from stdin. Only the user's own account.
    process->start(m_pkexec, {m_smbpasswd, QStringLiteral("-a"), QStringLiteral("-s"), m_userName});
}

void SambaShareManager::loadStatus(bool asAdministrator)
{
    if (m_statusBusy) return;
    if (m_smbstatus.isEmpty() || (asAdministrator && m_pkexec.isEmpty())) {
        m_status = QVariantMap{{"error", message("smbstatus is not installed.", "smbstatus kurulu değil.")}};
        emit statusChanged();
        return;
    }
    refreshEnvironment();
    m_statusBusy = true; emit statusChanged();
    const auto token = ++m_statusGeneration;
    auto* process = new QProcess(this);
    auto* timer = new QTimer(process);
    timer->setSingleShot(true);
    auto output = std::make_shared<QByteArray>();
    auto error = std::make_shared<QByteArray>();
    auto completed = std::make_shared<bool>(false);
    const auto done = [this, process, timer, output, error, token, asAdministrator, completed](int code, bool timedOut) {
        if (*completed) return;
        *completed = true;
        timer->stop();
        output->append(process->readAllStandardOutput());
        process->deleteLater();
        if (token != m_statusGeneration) return;
        m_statusBusy = false;
        const auto errorText = QString::fromLocal8Bit(*error).trimmed();
        if (timedOut) {
            m_status = QVariantMap{{"error", message("smbstatus did not respond.", "smbstatus yanıt vermedi.")}};
        } else if (asAdministrator && (code == 126 || code == 127)) {
            m_status = QVariantMap{{"requiresAdmin", true},
                                   {"error", message("Authorization was cancelled.", "Yetkilendirme iptal edildi.")}};
        } else {
            m_status = parseStatus(*output, errorText);
        }
        m_status.insert("administrator", asAdministrator);
        emit statusChanged();
    };
    connect(process, &QProcess::readyReadStandardOutput, process, [process, output] {
        output->append(process->readAllStandardOutput());
        if (output->size() > 4 * 1024 * 1024) process->kill();
    });
    connect(process, &QProcess::readyReadStandardError, process, [process, error] {
        *error = (*error + process->readAllStandardError()).right(4096);
    });
    connect(process, &QProcess::finished, process, [done](int code, QProcess::ExitStatus) { done(code, false); });
    connect(process, &QProcess::errorOccurred, process, [error, process, done](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart) return;
        *error = process->errorString().toUtf8();
        done(-1, false);
    });
    connect(timer, &QTimer::timeout, process, [process, done] { process->kill(); done(-1, true); });
    timer->start(asAdministrator ? 180000 : 15000);
    if (asAdministrator)
        process->start(m_pkexec, {m_smbstatus, QStringLiteral("--json")});
    else
        process->start(m_smbstatus, {QStringLiteral("--json")});
    process->closeWriteChannel();
}

QVariantMap SambaShareManager::permissionPlan(const QVariantList& entries, bool guestOk) const
{
    Q_UNUSED(guestOk) // guests get Everyone's rights, already in entries
    const auto [others, self] = accessLevels(entries, m_userName);
    const auto items = permissionItems(m_path, others, self);
    QVariantList list;
    bool requiresAdmin = false;
    for (const auto& item : items) {
        requiresAdmin = requiresAdmin || !item.owned;
        list.append(QVariantMap{{"path", item.path}, {"role", item.role}, {"owned", item.owned},
                                {"currentMode", modeOctal(item.current)}, {"newMode", modeOctal(item.wanted)},
                                {"currentText", modeText(item.current)}, {"newText", modeText(item.wanted)}});
    }
    return QVariantMap{{"needed", !items.isEmpty()}, {"items", list}, {"requiresAdmin", requiresAdmin},
                       {"level", others == 2 ? "full" : others == 1 ? "read" : "none"},
                       {"worldWritable", others == 2}};
}

QStringList SambaShareManager::otherSharedPaths() const
{
    QStringList paths = m_otherSharesOverride ? m_otherSharesForTest : SambaShareIndex::instance()->sharedPaths();
    paths.removeAll(m_path);
    return paths;
}

void SambaShareManager::runPermissionHelper(const QStringList& chmodArgs, const QString& success,
                                            std::function<void(bool, const QString&)> finish)
{
    // chmod as root through polkit, as an argument list without a shell.
    auto* process = new QProcess(this);
    auto completed = std::make_shared<bool>(false);
    auto error = std::make_shared<QByteArray>();
    const auto done = [process, completed, error, finish, success](bool ok, int code) {
        if (*completed) return;
        *completed = true;
        process->deleteLater();
        if (ok) finish(true, success);
        else if (code == 126) finish(false, message("Authorization was cancelled.", "Yetkilendirme iptal edildi."));
        else if (code == 127) finish(false, message("Not authorized to change these folders.",
                                                    "Bu klasörleri değiştirme yetkin yok."));
        else finish(false, message("Permissions could not be changed: ", "İzinler değiştirilemedi: ")
                    + QString::fromLocal8Bit(*error).trimmed());
    };
    connect(process, &QProcess::readyReadStandardError, process, [process, error] {
        *error = (*error + process->readAllStandardError()).right(4096);
    });
    connect(process, &QProcess::finished, process, [done](int code, QProcess::ExitStatus status) {
        done(status == QProcess::NormalExit && code == 0, code);
    });
    connect(process, &QProcess::errorOccurred, process, [process, error, done](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart) return;
        *error = process->errorString().toUtf8();
        done(false, -1);
    });
    process->start(m_pkexec, QStringList{m_chmod} + chmodArgs);
    process->closeWriteChannel();
}

void SambaShareManager::fixPermissions(const QVariantList& entries, bool guestOk)
{
    Q_UNUSED(guestOk)
    if (m_permissionsBusy || m_path.isEmpty()) return;
    const auto [others, self] = accessLevels(entries, m_userName);
    const auto items = permissionItems(m_path, others, self);
    if (items.isEmpty()) {
        emit permissionsFinished(true, message("The folder permissions already allow this access.",
                                               "Klasör izinleri bu erişime zaten uygun."));
        return;
    }
    QList<PermissionItem> owned, administered;
    for (const auto& item : items) {
        if (item.owned) owned.append(item);
        else if (item.role == QLatin1String("traverse")) administered.append(item);
    }
    if (!administered.isEmpty() && (m_pkexec.isEmpty() || m_chmod.isEmpty())) {
        emit permissionsFinished(false, message("Changing folders of other users needs polkit (pkexec).",
                                                "Başka kullanıcılara ait klasörleri değiştirmek için polkit (pkexec) gerekir."));
        return;
    }
    m_permissionsBusy = true; emit permissionsBusyChanged();
    const auto finish = [this](bool ok, const QString& feedback) {
        m_permissionsBusy = false; emit permissionsBusyChanged();
        emit permissionsFinished(ok, feedback);
    };
    const QString share = m_path;
    for (const auto& item : owned) {
        const auto error = applyOwnedMode(item.path, item.wanted);
        if (!error.isEmpty()) {
            finish(false, message("Permissions could not be changed: ", "İzinler değiştirilemedi: ")
                   + item.path + " — " + error);
            return;
        }
        rememberChange(share, item.path, item.current, item.wanted);
    }
    const auto success = message("Permissions updated.", "İzinler güncellendi.");
    if (administered.isEmpty()) { finish(true, success); return; }
    QStringList args{QStringLiteral("o+x"), QStringLiteral("--")};
    for (const auto& item : administered) args.append(item.path);
    runPermissionHelper(args, success, [finish, administered, share](bool ok, const QString& feedback) {
        if (ok)
            for (const auto& item : administered) rememberChange(share, item.path, item.current, item.wanted);
        finish(ok, feedback);
    });
}

QVariantMap SambaShareManager::restorePlan(const QVariantList& entries, bool guestOk, bool removing) const
{
    Q_UNUSED(guestOk)
    QVariantList list;
    QStringList skipped;
    bool requiresAdmin = false;
    const auto records = readRecords(m_path);
    if (m_path.isEmpty() || records.isEmpty())
        return QVariantMap{{"needed", false}, {"items", list}, {"skipped", skipped}};
    const auto [others, self] = removing ? QPair<int, int>{0, 0} : accessLevels(entries, m_userName);
    const QStringList shares = otherSharedPaths();
    QStringList paths = records.keys();
    // The shared folder first, then its parents from the nearest.
    std::sort(paths.begin(), paths.end(), [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const auto& path : paths) {
        const auto record = records.value(path);
        struct stat st {};
        if (::lstat(QFile::encodeName(path).constData(), &st) || !S_ISDIR(st.st_mode)) continue;
        const mode_t current = st.st_mode & 07777;
        const mode_t added = record.after & ~record.before & 07777;
        if (!added) continue;
        if (current != record.after) { skipped.append(path); continue; }
        mode_t required = 0;
        const bool shareFolder = path == m_path;
        if (shareFolder) {
            if (self) required |= S_IRUSR | S_IXUSR | (self == 2 ? S_IWUSR : 0);
            if (others) required |= S_IROTH | S_IXOTH | (others == 2 ? S_IWOTH : 0);
        } else if (others) {
            required |= S_IXOTH;
        }
        // Another share below this folder still needs to pass through it.
        const QString prefix = path == QLatin1String("/") ? path : path + '/';
        for (const auto& other : shares)
            if (other.startsWith(prefix)) { required |= S_IXOTH; break; }
        const mode_t removable = added & ~required;
        if (!removable) continue;
        const mode_t target = current & ~removable;
        const bool owned = st.st_uid == ::getuid();
        requiresAdmin = requiresAdmin || !owned;
        list.append(QVariantMap{{"path", path}, {"role", shareFolder ? "share" : "traverse"}, {"owned", owned},
                                {"currentMode", modeOctal(current)}, {"newMode", modeOctal(target)},
                                {"currentText", modeText(current)}, {"newText", modeText(target)},
                                {"originalMode", modeOctal(record.before)}});
    }
    return QVariantMap{{"needed", !list.isEmpty()}, {"items", list}, {"skipped", skipped},
                       {"requiresAdmin", requiresAdmin}, {"removing", removing}};
}

void SambaShareManager::restorePermissions(const QVariantList& entries, bool guestOk, bool removing)
{
    if (m_permissionsBusy || m_path.isEmpty()) return;
    const auto plan = restorePlan(entries, guestOk, removing);
    const QString share = m_path;
    const auto forget = [share, removing](const QList<QPair<QString, mode_t>>& applied) {
        auto records = readRecords(share);
        for (const auto& [path, mode] : applied) {
            const auto it = records.find(path);
            if (it == records.end()) continue;
            if (mode == it->before) records.erase(it);
            else it->after = mode;
        }
        // A removed share leaves nothing to take back later.
        writeRecords(share, removing ? QHash<QString, PermissionRecord>() : records);
    };
    const auto items = plan.value("items").toList();
    if (items.isEmpty()) {
        if (removing) forget({});
        emit permissionsRestored(true, message("No permissions to take back.", "Geri alınacak izin yok."));
        return;
    }
    QList<QPair<QString, mode_t>> owned;
    QList<QPair<QString, mode_t>> administered;
    for (const auto& value : items) {
        const auto item = value.toMap();
        const mode_t target = mode_t(item.value("newMode").toString().toUInt(nullptr, 8));
        if (item.value("owned").toBool()) owned.append({item.value("path").toString(), target});
        else administered.append({item.value("path").toString(), target});
    }
    if (!administered.isEmpty() && (m_pkexec.isEmpty() || m_chmod.isEmpty())) {
        emit permissionsRestored(false, message("Changing folders of other users needs polkit (pkexec).",
                                                "Başka kullanıcılara ait klasörleri değiştirmek için polkit (pkexec) gerekir."));
        return;
    }
    m_permissionsBusy = true; emit permissionsBusyChanged();
    const auto finish = [this](bool ok, const QString& feedback) {
        m_permissionsBusy = false; emit permissionsBusyChanged();
        emit permissionsRestored(ok, feedback);
    };
    QList<QPair<QString, mode_t>> applied;
    for (const auto& value : items) {
        const auto item = value.toMap();
        if (!item.value("owned").toBool()) continue;
        const QString path = item.value("path").toString();
        const mode_t current = mode_t(item.value("currentMode").toString().toUInt(nullptr, 8));
        const mode_t target = mode_t(item.value("newMode").toString().toUInt(nullptr, 8));
        // Exact: only if the folder still has the mode the plan was made for.
        const auto error = applyOwnedMode(path, target, true, current);
        if (!error.isEmpty()) {
            forget(applied);
            finish(false, message("Permissions could not be restored: ", "İzinler geri alınamadı: ") + path + " — " + error);
            return;
        }
        applied.append({path, target});
    }
    const auto success = message("Permissions restored.", "İzinler geri alındı.");
    if (administered.isEmpty()) { forget(applied); finish(true, success); return; }
    // Parents of other users only ever got o+x from us.
    QStringList args{QStringLiteral("o-x"), QStringLiteral("--")};
    for (const auto& [path, mode] : administered) args.append(path);
    runPermissionHelper(args, success, [finish, forget, applied, administered](bool ok, const QString& feedback) {
        auto all = applied;
        if (ok) all += administered;
        forget(all);
        finish(ok, feedback);
    });
}
