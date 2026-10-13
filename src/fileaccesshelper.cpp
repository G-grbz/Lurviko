#include "fileaccesssecurity.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QtConcurrent>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace {
void report(const std::shared_ptr<FileAccessTask> &task, bool finished, const QString &code = {})
{
    const QJsonObject message{{"finished", finished}, {"cancelled", task->cancelled.load()},
        {"changed", task->changed.load()}, {"failed", task->failed.load()}, {"skipped", task->skipped.load()},
        {"error", finished ? task->firstError : QString()}, {"code", code}};
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, line.size(), stdout);
    std::fflush(stdout);
}
bool integer(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 &result)
{
    if (!value.isDouble()) return false;
    const double number = value.toDouble();
    if (!std::isfinite(number) || number < minimum || number > maximum || std::floor(number) != number) return false;
    result = static_cast<qint64>(number);
    return true;
}
// Pin every component. Even intermediate symlinks must not redirect a root
// request outside the explicitly selected tree while authorization is pending.
int openRoot(const QString &path)
{
    int fd = ::open("/", O_PATH | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0) return -1;
    for (const auto &component : path.split('/', Qt::SkipEmptyParts)) {
        const int child = ::openat(fd, QFile::encodeName(component).constData(), O_PATH | O_NOFOLLOW | O_CLOEXEC);
        const int error = errno;
        ::close(fd);
        if (child < 0) { errno = error; return -1; }
        struct stat st {};
        if (::fstat(child, &st) != 0 || (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode))) {
            ::close(child); errno = ELOOP; return -1;
        }
        fd = child;
    }
    return fd;
}
struct Root { int fd; QString path; bool directory; };
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    auto task = std::make_shared<FileAccessTask>();
    if (argc != 1) { ++task->failed; report(task, true, "invalid-request"); return 1; }
    // One bounded JSON request, followed by a cancellation pipe. No shell,
    // arbitrary executable, file payload or password is accepted by the helper.
    QByteArray input;
    char byte;
    while (input.size() <= 1024 * 1024) {
        if (::read(STDIN_FILENO, &byte, 1) != 1) { ++task->failed; report(task, true, "invalid-request"); return 1; }
        if (byte == '\n') break;
        input += byte;
    }
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(input, &parseError);
    const auto request = document.object();
    qint64 mode, mask, owner, group;
    const auto paths = request.value("paths").toArray();
    const auto identities = request.value("identities").toArray();
    if (input.size() > 1024 * 1024 || parseError.error != QJsonParseError::NoError || !document.isObject()
        || !integer(request.value("mode"), 0, 0777, mode) || !integer(request.value("mask"), 0, 0777, mask)
        || !integer(request.value("owner"), -1, UINT32_MAX - 1, owner)
        || !integer(request.value("group"), -1, UINT32_MAX - 1, group)
        || !request.value("recursive").isBool() || paths.isEmpty() || paths.size() > 4096
        || identities.size() != paths.size()) {
        ++task->failed; report(task, true, "invalid-request"); return 1;
    }
    std::vector<Root> roots;
    const auto closeRoots = [&] { for (const auto &root : roots) ::close(root.fd); };
    // Pin ALL selected roots before making ANY changes. Refuse malformed,
    // missing, symbolic or special roots; descendants are never followed.
    for (qsizetype index = 0; index < paths.size(); ++index) {
        const auto entry = paths.at(index);
        const QString path = entry.toString();
        if (!entry.isString() || !QDir::isAbsolutePath(path) || path.contains(QChar::Null)
            || path != QDir::cleanPath(path) || QFile::encodeName(path).size() > 4096) {
            closeRoots(); ++task->failed; report(task, true, "invalid-request"); return 1;
        }
        const int fd = openRoot(path);
        struct stat st {};
        if (fd < 0 || ::fstat(fd, &st) != 0) {
            const int error = errno;
            if (fd >= 0) ::close(fd);
            closeRoots(); ++task->failed;
            task->firstError = path + ": " + QString::fromLocal8Bit(std::strerror(error));
            report(task, true, "unsafe-root"); return 1;
        }
        bool deviceOk = false, inodeOk = false;
        const auto identity = identities.at(index).toObject();
        const auto device = identity.value("device").toString().toULongLong(&deviceOk);
        const auto inode = identity.value("inode").toString().toULongLong(&inodeOk);
        if (!deviceOk || !inodeOk || device != qulonglong(st.st_dev) || inode != qulonglong(st.st_ino)) {
            ::close(fd); closeRoots(); ++task->failed;
            task->firstError = path;
            report(task, true, "root-changed"); return 1;
        }
        roots.push_back({fd, path, S_ISDIR(st.st_mode)});
    }
    const int flags = ::fcntl(STDIN_FILENO, F_GETFL);
    if (flags < 0 || ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) {
        closeRoots(); ++task->failed; report(task, true, "invalid-request"); return 1;
    }
    QSocketNotifier cancellation(STDIN_FILENO, QSocketNotifier::Read);
    const auto checkCancellation = [&] {
        char buffer[128];
        const auto count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
        if (count >= 0) { task->cancelled = true; cancellation.setEnabled(false); }
    };
    QObject::connect(&cancellation, &QSocketNotifier::activated, &app, checkCancellation);
    checkCancellation(); // Honor cancellation queued during authorization before starting work.
    QTimer progress;
    progress.setInterval(100);
    QObject::connect(&progress, &QTimer::timeout, &app, [&] { report(task, false); });
    QFutureWatcher<void> watcher;
    QObject::connect(&watcher, &QFutureWatcher<void>::finished, &app, [&] {
        progress.stop(); closeRoots(); report(task, true); app.exit(task->failed ? 1 : 0);
    });
    progress.start();
    watcher.setFuture(QtConcurrent::run([roots, task, mode, mask, owner, group, recursive = request.value("recursive").toBool()] {
        for (const auto &root : roots) {
            if (task->cancelled) break;
            FileAccessSecurity::applyDescriptorAccess(root.fd, root.path, mode, uid_t(owner), gid_t(group),
                                                      recursive && root.directory, task, mask);
        }
    }));
    return app.exec();
}
