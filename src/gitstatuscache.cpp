#include "gitstatuscache.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QtConcurrent>
#include <utility>

namespace {
qint64 now() { return QDateTime::currentMSecsSinceEpoch(); }
int priority(const QString &status) {
    if (status == "conflict") return 7;
    if (status == "modified") return 6;
    if (status == "deleted") return 5;
    if (status == "renamed") return 4;
    if (status == "added") return 3;
    if (status == "untracked") return 2;
    if (status == "ignored") return 1;
    return 0;
}
QString classify(const QByteArray &xy) {
    if (xy == "??") return "untracked";
    if (xy == "!!") return "ignored";
    if (xy.contains('U') || xy == "AA" || xy == "DD") return "conflict";
    if (xy.contains('R') || xy.contains('C')) return "renamed";
    if (xy.contains('D')) return "deleted";
    if (xy.contains('A')) return "added";
    if (xy.contains('M') || xy.contains('T')) return "modified";
    return {};
}
}
GitStatusCache *GitStatusCache::instance()
{
    static QPointer<GitStatusCache> cache;
    if (!cache) cache = new GitStatusCache(QCoreApplication::instance());
    return cache;
}
GitStatusCache::GitStatusCache(QObject *parent) : QObject(parent), m_git(QStandardPaths::findExecutable("git"))
{
    m_childTimer.setSingleShot(true);
    connect(&m_childTimer, &QTimer::timeout, this, &GitStatusCache::discoverChildren);
    m_debounce.setInterval(250); m_debounce.setSingleShot(true);
    connect(&m_debounce, &QTimer::timeout, this, &GitStatusCache::pump);
    const auto changed = [this](const QString &path) {
        for (const auto &root : m_watchRoots.values(path)) schedule(root, true);
    };
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, changed);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, changed);
}
GitStatusCache::Snapshot GitStatusCache::parsePorcelain(const QByteArray &data)
{
    Snapshot result;
    auto add = [&](QString path, const QString &state) {
        if (path.endsWith('/')) path.chop(1);
        if (path.isEmpty() || QDir::isAbsolutePath(path) || path == ".." || path.startsWith("../")) return;
        const auto put = [&](const QString &key) {
            if (priority(state) > priority(result.paths.value(key))) result.paths.insert(key, state);
        };
        put(path);
        if (state != "ignored" && priority(state) > priority(result.summary)) result.summary = state;
        if (state == "ignored") return; // Ignored children do not imply an ignored parent.
        int slash;
        while ((slash = path.lastIndexOf('/')) >= 0) { path.truncate(slash); put(path); }
    };
    qsizetype offset = 0;
    while (offset < data.size()) {
        const auto end = data.indexOf('\0', offset);
        if (end < 0 || end - offset < 4) { result.valid = false; break; }
        const QByteArray xy = data.mid(offset, 2);
        if (data[offset + 2] != ' ') { result.valid = false; break; }
        const QString state = classify(xy);
        const QString path = QString::fromUtf8(data.constData() + offset + 3, end - offset - 3);
        offset = end + 1;
        if (!state.isEmpty()) add(path, state);
        if (xy.contains('R') || xy.contains('C')) {
            const auto oldEnd = data.indexOf('\0', offset);
            if (oldEnd < 0) { result.valid = false; break; }
            if (!state.isEmpty()) add(QString::fromUtf8(data.constData() + offset, oldEnd - offset), state);
            offset = oldEnd + 1;
        }
        if (result.paths.size() > 100000) { result.valid = false; break; }
    }
    if (!result.valid) result.paths.clear();
    return result;
}
void GitStatusCache::request(QObject *owner, const QString &directory, bool force)
{
    if (!m_owners.contains(owner)) connect(owner, &QObject::destroyed, this, [this, owner] { m_owners.remove(owner); });
    m_owners[owner] = directory;
    // A tab/column leaving a repository may free a pinned snapshot slot.
    if (!m_waiting.isEmpty()) QTimer::singleShot(0, this, &GitStatusCache::retryWaiting);
    if (force) {
        const auto children = m_children.value(owner);
        for (const auto &path : children) {
            auto cached = m_childDirectories.find(path);
            if (cached != m_childDirectories.end()) cached->time = 0;
            emit directoryStatusChanged(path);
        }
    }
    if (directory.isEmpty() || m_git.isEmpty()) { emit directoryReady(directory, {}); return; }
    const auto cached = m_directories.constFind(directory);
    if (!force && cached != m_directories.cend() && now() - cached->time < (cached->root.isEmpty() ? 10000 : 300000)) {
        discovered(directory, *cached, false); return;
    }
    if (m_discovering.contains(directory)) { m_discovering[directory] = m_discovering[directory] || force; return; }
    m_discovering.insert(directory, force);
    auto *watcher = new QFutureWatcher<Discovery>(this);
    connect(watcher, &QFutureWatcher<Discovery>::finished, this, [this, watcher, directory] {
        auto result = watcher->result(); watcher->deleteLater();
        const bool force = m_discovering.take(directory);
        m_directories.insert(directory, result);
        if (m_directories.size() > 256) {
            const auto active = m_owners.values();
            for (auto it = m_directories.begin(); it != m_directories.end(); ++it)
                if (!active.contains(it.key())) { m_directories.erase(it); break; }
        }
        discovered(directory, result, force);
    });
    watcher->setFuture(QtConcurrent::run([directory] { return discover(directory, true); }));
}
GitStatusCache::Discovery GitStatusCache::discover(const QString &directory, bool ancestors)
{
    Discovery result; result.time = now();
    QDir dir(directory);
    if (!dir.exists()) return result;
    for (;;) {
        const QFileInfo marker(dir.filePath(".git"));
        if (marker.isDir() && QFileInfo(marker.filePath() + "/HEAD").isFile()) {
            result.root = dir.absolutePath(); result.gitDirectory = marker.absoluteFilePath(); return result;
        }
        if (marker.isFile() && marker.size() < 8192) {
            QFile file(marker.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly)) {
                const QByteArray first = file.readLine(8192).trimmed();
                if (first.startsWith("gitdir: ")) {
                    const QString gitDir = QDir::cleanPath(dir.absoluteFilePath(QString::fromUtf8(first.mid(8))));
                    if (QFileInfo(gitDir + "/HEAD").isFile()) { result.root = dir.absolutePath(); result.gitDirectory = gitDir; return result; }
                }
            }
        }
        if (!ancestors || !dir.cdUp()) return result;
    }
}

QString GitStatusCache::directoryStatus(QObject *owner, const QString &directory)
{
    if (m_git.isEmpty() || directory.isEmpty()) return {};
    if (!m_children.contains(owner)) connect(owner, &QObject::destroyed, this, [this, owner] { m_children.remove(owner); });
    auto &children = m_children[owner];
    if (!children.contains(directory)) {
        if (children.size() >= 256) children.removeFirst();
        children.append(directory);
    }
    const auto cached = m_childDirectories.constFind(directory);
    const bool known = cached != m_childDirectories.cend();
    if ((!known || now() - cached->time > (cached->root.isEmpty() ? 10000 : 300000)) && m_childQueue.size() < 512) {
        m_childQueue.insert(directory);
        if (!m_childScan && !m_childTimer.isActive()) m_childTimer.start();
    }
    if (!known || cached->root.isEmpty()) return {};
    return m_summaries.value(directory, QStringLiteral("repository"));
}
void GitStatusCache::resetChildren(QObject *owner)
{
    auto it = m_children.find(owner);
    if (it != m_children.end()) it->clear();
}
void GitStatusCache::discoverChildren()
{
    if (m_childScan || m_childQueue.isEmpty()) return;
    QStringList paths;
    for (auto it = m_childQueue.begin(); it != m_childQueue.end() && paths.size() < 32; ) {
        paths.append(*it); it = m_childQueue.erase(it);
    }
    m_childScan = true;
    auto *watcher = new QFutureWatcher<QHash<QString, Discovery>>(this);
    connect(watcher, &QFutureWatcher<QHash<QString, Discovery>>::finished, this, [this, watcher] {
        const auto found = watcher->result(); watcher->deleteLater(); m_childScan = false;
        for (auto it = found.cbegin(); it != found.cend(); ++it) {
            m_childDirectories.insert(it.key(), it.value());
            if (!it->root.isEmpty() && interested(it->root)) discovered(it.key(), it.value(), m_summaries.contains(it.key()));
            emit directoryStatusChanged(it.key());
        }
        // Metadata, queue and full snapshots are independently bounded. A
        // parent showing many repositories keeps their small root summaries.
        while (m_childDirectories.size() > 512) {
            auto oldest = m_childDirectories.begin();
            for (auto it = m_childDirectories.begin(); it != m_childDirectories.end(); ++it)
                if (it->time < oldest->time) oldest = it;
            m_summaries.remove(oldest.key()); m_childDirectories.erase(oldest);
        }
        if (!m_childQueue.isEmpty()) m_childTimer.start();
    });
    watcher->setFuture(QtConcurrent::run([paths] {
        QHash<QString, Discovery> result;
        for (const auto &path : paths) result.insert(path, discover(path, false));
        return result;
    }));
}
void GitStatusCache::retryWaiting()
{
    const auto waiting = m_waiting;
    m_waiting.clear();
    for (auto it = waiting.cbegin(); it != waiting.cend(); ++it)
        if (interested(it.key())) discovered(it.key(), it.value(), false);
}

void GitStatusCache::discovered(const QString &directory, Discovery discovery, bool force)
{
    emit directoryReady(directory, discovery.root);
    if (discovery.root.isEmpty()) return;
    auto it = m_repositories.find(discovery.root);
    if (it == m_repositories.end()) {
        if (m_repositories.size() >= 8) {
            for (auto old = m_repositories.begin(); old != m_repositories.end(); ++old) {
                if (!directlyInterested(old.key()) && !m_pending.contains(old.key()) && !old.value()->process && !old.value()->parsing) {
                    unwatch(old.value()); m_repositories.erase(old); break;
                }
            }
            if (m_repositories.size() >= 8) {
                if (m_waiting.size() < 256) m_waiting.insert(discovery.root, discovery);
                return;
            }
        }
        auto repo = std::make_shared<Repository>(); repo->root = discovery.root; repo->gitDirectory = discovery.gitDirectory;
        it = m_repositories.insert(discovery.root, repo);
        watch(repo);
    }
    if (force || now() - (*it)->updated > 2000) schedule(discovery.root, force);
}
bool GitStatusCache::directlyInterested(const QString &root) const
{
    for (const auto &directory : m_owners) if (m_directories.value(directory).root == root) return true;
    return false;
}
bool GitStatusCache::interested(const QString &root) const
{
    if (directlyInterested(root)) return true;
    for (const auto &children : m_children) if (children.contains(root)) return true;
    return false;
}
void GitStatusCache::schedule(const QString &root, bool force)
{
    const auto repo = m_repositories.value(root);
    if (!repo || !interested(root)) return;
    if (repo->process || repo->parsing) { repo->dirty |= force; return; }
    m_pending.insert(root);
    if (!m_debounce.isActive()) m_debounce.start();
}
void GitStatusCache::pump()
{
    const auto queued = m_pending.values();
    for (const auto &root : queued) {
        if (m_running >= 2) break;
        const auto repo = m_repositories.value(root);
        if (!repo || !interested(root)) { m_pending.remove(root); continue; }
        if (now() - repo->started < 1500) continue;
        m_pending.remove(root);
        if (repo->process || repo->parsing) { repo->dirty = true; continue; }
        ++m_running; repo->started = now(); repo->dirty = false;
        auto *process = new QProcess(this); repo->process = process;
        auto output = std::make_shared<QByteArray>();
        auto failed = std::make_shared<bool>(false);
        auto completed = std::make_shared<bool>(false);
        auto environment = QProcessEnvironment::systemEnvironment();
        environment.insert("GIT_OPTIONAL_LOCKS", "0"); environment.insert("GIT_TERMINAL_PROMPT", "0");
        process->setProcessEnvironment(environment);
        auto *timeout = new QTimer(process); timeout->setSingleShot(true); timeout->setInterval(15000);
        connect(timeout, &QTimer::timeout, process, [process, failed] { *failed = true; process->kill(); });
        const auto read = [process, output, failed] {
            const auto chunk = process->readAllStandardOutput();
            if (output->size() + chunk.size() > 8 * 1024 * 1024) { *failed = true; process->kill(); return; }
            output->append(chunk);
        };
        connect(process, &QProcess::readyReadStandardOutput, this, read);
        connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
        const auto finish = [this, repo, process, output, failed, completed, timeout, read](bool success) {
            if (std::exchange(*completed, true)) return;
            timeout->stop(); read(); repo->process.clear(); process->deleteLater(); --m_running;
            if (!success || *failed) { QTimer::singleShot(0, this, &GitStatusCache::retryWaiting); if (repo->dirty) schedule(repo->root, true); if (!m_pending.isEmpty()) m_debounce.start(); return; }
            repo->parsing = true;
            auto *watcher = new QFutureWatcher<Snapshot>(this);
            connect(watcher, &QFutureWatcher<Snapshot>::finished, this, [this, repo, watcher] {
                auto snapshot = watcher->result(); watcher->deleteLater(); repo->parsing = false;
                if (snapshot.valid) {
                    m_summaries.insert(repo->root, snapshot.summary.isEmpty() ? QStringLiteral("clean") : snapshot.summary);
                    // Keep summaries bounded even when navigating through many repositories.
                    if (m_summaries.size() > 512) {
                        for (auto it = m_summaries.begin(); it != m_summaries.end(); ++it)
                            if (it.key() != repo->root) { m_summaries.erase(it); break; }
                    }
                    repo->snapshot = std::move(snapshot); repo->updated = now(); emit statusChanged(repo->root);
                    emit directoryStatusChanged(repo->root);
                }
                watch(repo);
                QTimer::singleShot(0, this, &GitStatusCache::retryWaiting);
                if (repo->dirty) schedule(repo->root, true);
                if (!m_pending.isEmpty()) m_debounce.start();
            });
            watcher->setFuture(QtConcurrent::run([output] { return parsePorcelain(*output); }));
            if (!m_pending.isEmpty()) m_debounce.start();
        };
        connect(process, &QProcess::finished, this, [finish](int code, QProcess::ExitStatus state) { finish(code == 0 && state == QProcess::NormalExit); });
        connect(process, &QProcess::errorOccurred, this, [finish](QProcess::ProcessError error) { if (error == QProcess::FailedToStart) finish(false); });
        // Disable executable fsmonitor hooks from repository configuration.
        process->start(m_git, {"-c", "core.fsmonitor=false", "-C", root, "status", "--porcelain=v1", "-z",
                              "--ignored=matching", "--untracked-files=normal", "--ignore-submodules=dirty"});
        timeout->start();
    }
    if (!m_pending.isEmpty() && !m_debounce.isActive()) m_debounce.start();
    if (!m_waiting.isEmpty() && m_running < 2) QTimer::singleShot(0, this, &GitStatusCache::retryWaiting);
}
void GitStatusCache::unwatch(const std::shared_ptr<Repository> &repo)
{
    for (const auto &path : repo->watches) {
        m_watchRoots.remove(path, repo->root);
        if (!m_watchRoots.contains(path) && (m_watcher.files().contains(path) || m_watcher.directories().contains(path))) m_watcher.removePath(path);
    }
    repo->watches.clear();
}
void GitStatusCache::watch(const std::shared_ptr<Repository> &repo)
{
    unwatch(repo);
    const QDir dir(repo->gitDirectory);
    for (const QString &path : {repo->gitDirectory, dir.filePath("index"), dir.filePath("HEAD"), dir.filePath("logs/HEAD"), dir.filePath("packed-refs")}) {
        if (!QFileInfo::exists(path)) continue;
        if (!m_watchRoots.contains(path)) m_watcher.addPath(path);
        m_watchRoots.insert(path, repo->root); repo->watches.append(path);
    }
}
QString GitStatusCache::status(const QString &root, const QString &path) const
{
    const auto repo = m_repositories.value(root);
    const QString prefix = root.endsWith('/') ? root : root + '/';
    if (path == root) return m_summaries.value(root, QStringLiteral("repository"));
    if (!repo || !path.startsWith(prefix)) return {};
    QString relative = path.mid(prefix.size());
    const QString direct = repo->snapshot.paths.value(relative);
    if (!direct.isEmpty()) return direct;
    int slash;
    while ((slash = relative.lastIndexOf('/')) >= 0) {
        relative.truncate(slash);
        const QString inherited = repo->snapshot.paths.value(relative);
        if (inherited == "ignored" || inherited == "untracked") return inherited;
    }
    return {};
}
