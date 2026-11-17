#include "sftpdirectory.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHostAddress>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <memory>
#include <pwd.h>
#include <unistd.h>

namespace {

QString message(const char *en, const char *tr)
{
    const auto fallback = QLocale::system().language() == QLocale::Turkish ? "tr" : "en";
    return QString::fromUtf8(QSettings().value("ui/language", fallback).toString() == "tr" ? tr : en);
}

// Host names (RFC 1123 labels, underscores tolerated as in many LANs),
// IPv4 and IPv6 literals. Nothing that could become an option or a path.
bool validHost(const QString &host)
{
    if (host.isEmpty() || host.size() > 253 || host.startsWith('-'))
        return false;
    QHostAddress address;
    if (address.setAddress(host))
        return true;
    static const QRegularExpression name(QStringLiteral(
        "^(?=.{1,253}$)[A-Za-z0-9_](?:[A-Za-z0-9_-]{0,61}[A-Za-z0-9_])?(?:\\.[A-Za-z0-9_](?:[A-Za-z0-9_-]{0,61}[A-Za-z0-9_])?)*\\.?$"));
    return name.match(host).hasMatch();
}

bool validUser(const QString &user)
{
    static const QRegularExpression name(QStringLiteral("^[A-Za-z0-9_][A-Za-z0-9_.@\\\\$-]{0,63}$"));
    return user.isEmpty() || name.match(user).hasMatch();
}

QStringList words(const QString &line)
{
    // "Key value", "Key=value" and quoted values.
    QStringList result;
    QString current;
    bool quoted = false;
    for (const QChar c : line) {
        if (c == '"') { quoted = !quoted; continue; }
        if (!quoted && (c.isSpace() || (c == '=' && result.isEmpty()))) {
            if (!current.isEmpty()) { result << current; current.clear(); }
            continue;
        }
        current += c;
    }
    if (!current.isEmpty()) result << current;
    return result;
}

QString homeSsh()
{
    return QDir::homePath() + QStringLiteral("/.ssh");
}

QStringList expandInclude(const QString &pattern, const QString &sshDirectory)
{
    QString path = pattern;
    if (path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    else if (!QDir::isAbsolutePath(path))
        path = sshDirectory + '/' + path;
    const QFileInfo info(path);
    const QDir dir = info.dir();
    if (!info.fileName().contains('*') && !info.fileName().contains('?'))
        return {info.filePath()};
    QStringList files;
    for (const auto &entry : dir.entryInfoList({info.fileName()}, QDir::Files, QDir::Name))
        files << entry.filePath();
    return files;
}

struct Loaded { QVariantList config; QVariantList known; };

// Git hosting accepts SSH only for git: it has no files to browse over SFTP.
bool gitService(const QString &host, const QString &user)
{
    static const QStringList hosts{"github.com", "gitlab.com", "bitbucket.org", "codeberg.org",
                                   "aur.archlinux.org", "ssh.dev.azure.com", "vs-ssh.visualstudio.com",
                                   "git.sr.ht", "gitea.com", "salsa.debian.org", "invent.kde.org"};
    const QString name = host.toLower();
    for (const auto &service : hosts)
        if (name == service || name.endsWith('.' + service))
            return true;
    return user == QLatin1String("git") || user == QLatin1String("aur");
}

} // namespace

SftpDirectory::SftpDirectory(QObject *parent)
    : QObject(parent), m_sshDirectory(homeSsh()),
      m_avahi(QStandardPaths::findExecutable(QStringLiteral("avahi-browse")))
{
}

SftpDirectory::~SftpDirectory()
{
    stopDiscovery();
}

QString SftpDirectory::localUser() const
{
    if (const passwd *entry = ::getpwuid(::getuid()); entry && entry->pw_name)
        return QString::fromLocal8Bit(entry->pw_name);
    return qEnvironmentVariable("USER");
}

QVariantList SftpDirectory::parseConfig(const QString &file, const QString &sshDirectoryHint, int depth)
{
    QVariantList result;
    if (depth > 4)
        return result;
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly | QIODevice::Text) || handle.size() > 1024 * 1024)
        return result;
    // Relative Include paths are relative to ~/.ssh (here: the top file's folder).
    const QString sshDirectory = sshDirectoryHint.isEmpty() ? QFileInfo(file).absolutePath() : sshDirectoryHint;
    QStringList block;          // aliases of the current Host block
    QVariantMap settings;       // HostName/User/Port of that block
    bool inMatch = false;
    const auto flush = [&] {
        for (const auto &alias : block) {
            const int port = settings.value("port", 22).toInt();
            result.append(QVariantMap{{"alias", alias}, {"hostName", settings.value("hostName", alias)},
                                      {"user", settings.value("user")}, {"port", port}});
        }
        block.clear();
        settings.clear();
    };
    for (const QString &raw : QString::fromUtf8(handle.readAll()).split('\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QStringList parts = words(line);
        if (parts.isEmpty())
            continue;
        const QString key = parts.first().toLower();
        if (key == QLatin1String("include")) {
            for (int i = 1; i < parts.size(); ++i)
                for (const auto &included : expandInclude(parts.at(i), sshDirectory))
                    result += parseConfig(included, sshDirectory, depth + 1);
            continue;
        }
        if (key == QLatin1String("host")) {
            flush();
            inMatch = false;
            for (int i = 1; i < parts.size(); ++i) {
                const QString alias = parts.at(i);
                // Patterns and negations are rules, not servers to list.
                if (alias.contains('*') || alias.contains('?') || alias.startsWith('!'))
                    continue;
                if (validHost(alias))
                    block << alias;
            }
            continue;
        }
        if (key == QLatin1String("match")) {
            flush();
            inMatch = true;
            continue;
        }
        if (inMatch || block.isEmpty() || parts.size() < 2)
            continue;
        // First value wins, as in ssh(1).
        if (key == QLatin1String("hostname") && !settings.contains("hostName"))
            settings.insert("hostName", parts.at(1));
        else if (key == QLatin1String("user") && !settings.contains("user"))
            settings.insert("user", parts.at(1));
        else if (key == QLatin1String("port") && !settings.contains("port"))
            settings.insert("port", parts.at(1).toInt() > 0 && parts.at(1).toInt() < 65536 ? parts.at(1).toInt() : 22);
    }
    flush();
    return result;
}

QVariantList SftpDirectory::parseKnownHosts(const QString &file)
{
    QVariantList result;
    QFile handle(file);
    if (!handle.open(QIODevice::ReadOnly | QIODevice::Text) || handle.size() > 8 * 1024 * 1024)
        return result;
    QStringList seen;
    for (const QString &raw : QString::fromUtf8(handle.readAll()).split('\n')) {
        QStringList parts = raw.trimmed().split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts);
        if (parts.size() < 2 || parts.first().startsWith('#'))
            continue;
        if (parts.first().startsWith('@')) {          // @cert-authority / @revoked
            if (parts.first() == QLatin1String("@revoked")) continue;
            parts.removeFirst();
        }
        for (QString host : parts.first().split(',')) {
            if (host.startsWith(QLatin1String("|1|")) || host.contains('*') || host.contains('?') || host.startsWith('!'))
                continue;                               // hashed or pattern
            int port = 22;
            static const QRegularExpression bracketed(QStringLiteral("^\\[(.+)\\]:(\\d+)$"));
            if (const auto match = bracketed.match(host); match.hasMatch()) {
                host = match.captured(1);
                port = match.captured(2).toInt();
            }
            if (!validHost(host))
                continue;
            const QString key = host.toLower() + ':' + QString::number(port);
            if (seen.contains(key))
                continue;
            seen << key;
            result.append(QVariantMap{{"host", host}, {"port", port}});
            if (result.size() >= 200)
                return result;
        }
    }
    return result;
}

void SftpDirectory::refresh()
{
    const quint64 generation = ++m_generation;
    m_loading = true;
    emit hostsChanged();
    const QString directory = m_sshDirectory;
    auto *watcher = new QFutureWatcher<Loaded>(this);
    connect(watcher, &QFutureWatcher<Loaded>::finished, this, [this, watcher, generation] {
        Loaded loaded = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        QStringList configured;
        QVariantList config;
        for (const auto &value : loaded.config) {
            auto entry = value.toMap();
            const QString alias = entry.value("alias").toString();
            if (configured.contains(alias.toLower()))
                continue;
            if (gitService(entry.value("hostName").toString(), entry.value("user").toString())
                    || gitService(alias, QString())) {
                configured << alias.toLower() << entry.value("hostName").toString().toLower();
                continue;
            }
            configured << alias.toLower() << entry.value("hostName").toString().toLower();
            // The alias is what ssh (and KIO's libssh) resolve with the config.
            entry.insert("url", buildUrl(alias, QString(), 22, QString()));
            config.append(entry);
        }
        QVariantList known;
        for (const auto &value : loaded.known) {
            auto entry = value.toMap();
            const QString host = entry.value("host").toString();
            if (configured.contains(host.toLower()) || gitService(host, QString()))
                continue;
            entry.insert("url", buildUrl(host, QString(), entry.value("port").toInt(), QString()));
            known.append(entry);
        }
        m_configHosts = config;
        m_knownHosts = known;
        m_loading = false;
        emit hostsChanged();
    });
    watcher->setFuture(QtConcurrent::run([directory] {
        return Loaded{parseConfig(directory + QStringLiteral("/config")),
                      parseKnownHosts(directory + QStringLiteral("/known_hosts"))};
    }));
    discover();
}

void SftpDirectory::stopDiscovery()
{
    for (const auto &process : std::as_const(m_processes)) {
        if (!process) continue;
        process->disconnect();
        process->kill();
        process->deleteLater();
    }
    m_processes.clear();
    m_pendingDiscovery = 0;
}

void SftpDirectory::discover()
{
    stopDiscovery();
    m_discovered.clear();
    if (m_avahi.isEmpty()) {
        m_discovering = false;
        m_discoveryError = message("Install Avahi to find SSH servers on the local network.",
                                   "Yerel ağdaki SSH sunucularını bulmak için Avahi kurulmalı.");
        emit discoveredChanged();
        return;
    }
    m_discovering = true;
    m_discoveryError.clear();
    emit discoveredChanged();
    // -r resolve, -t stop after the cache is listed, -p parsable output.
    for (const QString &type : {QStringLiteral("_sftp-ssh._tcp"), QStringLiteral("_ssh._tcp")}) {
        auto *process = new QProcess(this);
        m_processes << process;
        ++m_pendingDiscovery;
        auto *timer = new QTimer(process);
        timer->setSingleShot(true);
        auto completed = std::make_shared<bool>(false);
        const auto done = [this, process, completed] {
            if (*completed) return;
            *completed = true;
            const QString output = QString::fromUtf8(process->readAllStandardOutput().left(1024 * 1024));
            const QString error = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
            process->deleteLater();
            m_processes.removeAll(process);
            QStringList seen;
            for (const auto &value : std::as_const(m_discovered))
                seen << value.toMap().value("host").toString().toLower() + ':' + value.toMap().value("port").toString();
            for (const QString &line : output.split('\n')) {
                // =;iface;proto;name;type;domain;hostname;address;port;txt
                const QStringList fields = line.split(';');
                if (fields.size() < 9 || fields.first() != QLatin1String("="))
                    continue;
                QString host = fields.at(6);
                if (host.endsWith('.')) host.chop(1);
                const QString address = fields.at(7);
                const int port = fields.at(8).toInt();
                if (!validHost(host) || port <= 0 || port > 65535)
                    continue;
                const QString key = host.toLower() + ':' + QString::number(port);
                if (seen.contains(key))
                    continue;
                seen << key;
                QString name = fields.at(3);
                name.replace(QLatin1String("\\032"), QLatin1String(" "));
                m_discovered.append(QVariantMap{{"name", name}, {"host", host}, {"address", address},
                                                {"port", port}, {"url", buildUrl(host, QString(), port, QString())}});
            }
            if (m_discovered.isEmpty() && !error.isEmpty() && m_discoveryError.isEmpty())
                m_discoveryError = message("Local network discovery is unavailable (is the Avahi service running?).",
                                           "Yerel ağ keşfi kullanılamıyor (Avahi servisi çalışıyor mu?).");
            if (--m_pendingDiscovery <= 0) {
                m_pendingDiscovery = 0;
                m_discovering = false;
            }
            emit discoveredChanged();
        };
        connect(process, &QProcess::finished, process, [done] { done(); });
        connect(process, &QProcess::errorOccurred, process, [done](QProcess::ProcessError failure) {
            if (failure == QProcess::FailedToStart) done();
        });
        connect(timer, &QTimer::timeout, process, [process, done] { process->kill(); done(); });
        timer->start(8000);
        process->start(m_avahi, {QStringLiteral("-r"), QStringLiteral("-t"), QStringLiteral("-p"), type});
        process->closeWriteChannel();
    }
}

QVariantMap SftpDirectory::parseTarget(const QString &text) const
{
    QVariantMap result{{"valid", false}, {"host", QString()}, {"user", QString()}, {"port", 22}, {"path", QString()}};
    QString value = text.trimmed();
    if (value.isEmpty() || value.size() > 2048 || value.contains('\n'))
        return result;
    QString user, host, path;
    int port = 22;
    if (value.contains(QLatin1String("://"))) {
        const QUrl url(value);
        if (!url.isValid() || (url.scheme() != QLatin1String("sftp") && url.scheme() != QLatin1String("ssh")
                               && url.scheme() != QLatin1String("fish"))) {
            result.insert("error", message("Use an sftp:// address or host name.", "sftp:// adresi veya sunucu adı yaz."));
            return result;
        }
        user = url.userName();
        host = url.host();
        port = url.port(22);
        path = url.path();
    } else {
        const int at = value.lastIndexOf('@');
        if (at >= 0) { user = value.left(at); value = value.mid(at + 1); }
        static const QRegularExpression v6(QStringLiteral("^\\[([^\\]]+)\\](?::(\\d+))?(/.*)?$"));
        // host[:port][/path] or scp-style host:path
        static const QRegularExpression plain(QStringLiteral("^([^:/]+)(?::(\\d+))?(?::?(/.*|~.*|[^/].*))?$"));
        if (const auto match = v6.match(value); match.hasMatch()) {
            host = match.captured(1);
            if (!match.captured(2).isEmpty()) port = match.captured(2).toInt();
            path = match.captured(3);
        } else if (const auto match = plain.match(value); match.hasMatch()) {
            host = match.captured(1);
            if (!match.captured(2).isEmpty()) port = match.captured(2).toInt();
            path = match.captured(3);
        } else {
            result.insert("error", message("Check the server address.", "Sunucu adresini kontrol et."));
            return result;
        }
    }
    // sftp:// paths are absolute: "~/x" and scp-style relative paths open the
    // home folder instead of a wrong folder from the root.
    if (!path.startsWith('/'))
        path.clear();
    if (!validHost(host)) {
        result.insert("error", message("Check the server name.", "Sunucu adını kontrol et."));
        return result;
    }
    if (!validUser(user)) {
        result.insert("error", message("Check the user name.", "Kullanıcı adını kontrol et."));
        return result;
    }
    if (port <= 0 || port > 65535) {
        result.insert("error", message("The port must be between 1 and 65535.", "Port 1 ile 65535 arasında olmalı."));
        return result;
    }
    result.insert("valid", true);
    result.insert("host", host);
    result.insert("user", user);
    result.insert("port", port);
    result.insert("path", path);
    return result;
}

QString SftpDirectory::buildUrl(const QString &host, const QString &user, int port, const QString &path) const
{
    const QString cleanHost = host.trimmed();
    const QString cleanUser = user.trimmed();
    if (!validHost(cleanHost) || !validUser(cleanUser) || port < 0 || port > 65535)
        return {};
    QUrl url;
    url.setScheme(QStringLiteral("sftp"));
    url.setHost(cleanHost);
    if (!url.isValid() || url.host().isEmpty())
        return {};
    if (!cleanUser.isEmpty())
        url.setUserName(cleanUser);
    if (port > 0 && port != 22)
        url.setPort(port);
    QString cleanPath = path.trimmed();
    if (!cleanPath.isEmpty()) {
        if (cleanPath.contains(QChar::Null) || cleanPath.contains('\n'))
            return {};
        if (!cleanPath.startsWith('/'))
            cleanPath.prepend('/');
        url.setPath(QDir::cleanPath(cleanPath));
    }
    return url.toString(QUrl::FullyEncoded);
}
