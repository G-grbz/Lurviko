#include "gitoperations.h"
#include "gitstatuscache.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLockFile>
#include <QMap>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#include <QPromise>
#include <QReadWriteLock>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <QtConcurrent/QtConcurrentRun>
#include <atomic>
#include <algorithm>
#include <functional>
#include <exception>
#include <limits>
#include <signal.h>

namespace {
constexpr int maxOutput = 8 * 1024 * 1024;
constexpr int maxChanges = 10000;
struct Failure { QString text; };
QString text(bool tr, const char* en, const char* turkish) { return QString::fromUtf8(tr ? turkish : en); }
QString displayError(QString value)
{
    static const QRegularExpression ansi(QStringLiteral("\\x1b\\[[0-9;]*[A-Za-z]"));
    static const QRegularExpression userInfo(QStringLiteral("((?:https?|ssh|git)://)[^\\s/]*@"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression secret(QStringLiteral("((?:access_token|token|password)=)[^&\\s]+"), QRegularExpression::CaseInsensitiveOption);
    value.remove(ansi); value.replace(userInfo, "\\1***@"); value.replace(secret, "\\1***");
    return value.left(8192).trimmed();
}
QString pathOutput(QByteArray output)
{
    if (output.endsWith('\n')) output.chop(1);
    return QFile::decodeName(output);
}
QString oid(const QByteArray& value)
{
    const auto result = QString::fromLatin1(value.trimmed());
    static const QRegularExpression valid(QStringLiteral("^[0-9a-f]{40}([0-9a-f]{24})?$"));
    return valid.match(result).hasMatch() ? result : QString();
}
bool safePath(const QByteArray& path)
{
    if (path.isEmpty() || path.startsWith('/') || path.contains('\0')) return false;
    for (const auto& segment : path.split('/'))
        if (segment.isEmpty() || segment == "." || segment == ".." || segment == ".git") return false;
    return true;
}
QThreadPool* pool()
{
    static auto* workers = [] { auto* p = new QThreadPool(QCoreApplication::instance()); p->setMaxThreadCount(2); p->setExpiryTimeout(1500); return p; }();
    return workers;
}
struct Command { int code = -1; QByteArray output; QString error; };
struct Input {
    QString program, directory, kind, dirtyState;
    QVariantMap parameters;
    bool tr = false, force = false;
    std::shared_ptr<std::atomic_bool> cancelled;
    std::function<void(const QString&)> progress;
};
class Runner {
public:
    Input input;
    QString directory;
    explicit Runner(const Input& value) : input(value), directory(value.directory) {}
    void check() const {
        if (input.cancelled->load(std::memory_order_relaxed))
            throw Failure{text(input.tr, "Operation cancelled.", "İşlem iptal edildi.")};
    }
    Command run(const QStringList& args, const QByteArray& data = {}, const QString& index = {},
                bool required = true, bool network = false, bool noHooks = false, bool ignoreCancellation = false, int timeoutMs = 0)
    {
        if (!ignoreCancellation) check();
        // These buffers also outlive QProcess destruction if a late child
        // output signal arrives while the process is being cleaned up.
        QByteArray output, errors;
        QString stopped;
        QProcess process;
        // An interrupted Git command must not leave its SSH/credential child
        // process behind. This session belongs only to this invocation.
        process.setUnixProcessParameters(QProcess::UnixProcessFlag::CreateNewSession);
        auto env = QProcessEnvironment::systemEnvironment();
        for (const auto& key : {"GIT_DIR", "GIT_WORK_TREE", "GIT_COMMON_DIR", "GIT_INDEX_FILE", "GIT_OBJECT_DIRECTORY", "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE"}) env.remove(key);
        env.insert("GIT_TERMINAL_PROMPT", "0"); env.insert("GIT_OPTIONAL_LOCKS", "0");
        env.insert("GIT_NO_REPLACE_OBJECTS", "1"); env.insert("GIT_NO_LAZY_FETCH", "1");
        if (!index.isEmpty()) env.insert("GIT_INDEX_FILE", index);
        process.setProcessEnvironment(env);
        QStringList command{"-c", "core.fsmonitor=false", "-c", "gc.auto=0", "-c", "maintenance.auto=false",
                            "-c", "fetch.recurseSubmodules=false", "-c", "protocol.ext.allow=never", "-c", "color.ui=false"};
        if (noHooks) command << "-c" << "core.hooksPath=/dev/null";
        if (!directory.isEmpty()) command << "-C" << directory;
        command << args;
        QElapsedTimer clock; clock.start();
        process.start(input.program, command);
        if (!process.waitForStarted(3000)) throw Failure{displayError(process.errorString())};
        const auto processGroup = process.processId();
        if (!data.isEmpty()) process.write(data);
        process.closeWriteChannel();
        qint64 lastProgress = 0;
        const auto collect = [&] {
            const auto bytes = process.readAllStandardOutput();
            const auto remaining = maxOutput - output.size();
            output += bytes.left(remaining);
            if (bytes.size() > remaining && stopped.isEmpty())
                stopped = text(input.tr, "Git output exceeded the display limit.", "Git çıktısı görüntüleme sınırını aştı.");
            errors = (errors + process.readAllStandardError()).right(32768);
        };
        // Drain each ready chunk instead of allowing QProcess's internal buffer
        // to accumulate an arbitrarily large status/diff during a wait.
        QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, collect);
        QObject::connect(&process, &QProcess::readyReadStandardError, &process, collect);
        do {
            process.waitForFinished(50); collect();
            if (!ignoreCancellation && input.cancelled->load()) stopped = text(input.tr, "Operation cancelled.", "İşlem iptal edildi.");
            if (clock.elapsed() > (timeoutMs > 0 ? timeoutMs : (network ? 120000 : 30000))) stopped = text(input.tr, "Git did not respond in time.", "Git zamanında yanıt vermedi.");
            if (network && input.progress && clock.elapsed() - lastProgress >= 250 && !errors.isEmpty()) {
                auto lines = QString::fromUtf8(errors).replace('\r', '\n').split('\n', Qt::SkipEmptyParts);
                if (!lines.isEmpty()) input.progress(displayError(lines.last()).left(250));
                lastProgress = clock.elapsed();
            }
            if (!stopped.isEmpty() && process.state() != QProcess::NotRunning) {
                if (processGroup > 1) ::kill(-processGroup, SIGTERM);
                process.terminate();
                const bool finished = process.waitForFinished(500);
                if (processGroup > 1) ::kill(-processGroup, SIGKILL);
                if (!finished) { process.kill(); process.waitForFinished(1000); }
            }
        } while (process.state() != QProcess::NotRunning && stopped.isEmpty());
        collect();
        if (!stopped.isEmpty()) throw Failure{stopped};
        Command result;
        result.code = process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
        result.output = output;
        result.error = displayError(QString::fromUtf8(errors));
        if (required && result.code != 0)
            throw Failure{result.error.isEmpty() ? text(input.tr, "Git operation failed.", "Git işlemi başarısız oldu.") : result.error};
        return result;
    }
};
struct Context {
    QString root, gitDir, commonDir, ref, head, branch, upstream, remote, remoteRef, remoteUrl;
    QStringList pushUrls;
    bool detached = false, dirty = false, dirtyKnown = false, countsKnown = false, inProgress = false;
    int ahead = 0, behind = 0;
    QVariantList changes;
    QHash<QString, QList<QByteArray>> paths;
    bool truncated = false, conflicts = false;
};
struct Result {
    QVariantMap info;
    QVariantList changes, history, upstreamTargets, tags, steps;
    QVariantMap tagInfo;
    QString kind, detailText, error, message, root, commonDir, localCommit, localTag;
    QStringList touched;
    bool ok = false, wrote = false;
};
QVariantMap publicInfo(const Context& c)
{
    return {{"root", c.root}, {"branch", c.branch}, {"head", c.head}, {"detached", c.detached},
            {"unborn", c.head.isEmpty()}, {"upstream", c.upstream}, {"remote", c.remote},
            {"remoteBranch", c.remoteRef}, {"webUrl", GitOperations::remoteWebUrl(c.remoteUrl).toString()},
            {"ahead", c.ahead}, {"behind", c.behind}, {"countsKnown", c.countsKnown},
            {"dirty", c.dirty}, {"dirtyKnown", c.dirtyKnown}, {"inProgress", c.inProgress},
            {"changesTruncated", c.truncated}};
}
Context resolve(Runner& git)
{
    Context c;
    c.root = pathOutput(git.run({"rev-parse", "--show-toplevel"}).output);
    if (c.root.isEmpty() || !QDir::isAbsolutePath(c.root)) throw Failure{text(git.input.tr, "Not a working Git repository.", "Çalışma kopyası olan bir Git deposu değil.")};
    git.directory = c.root;
    c.gitDir = pathOutput(git.run({"rev-parse", "--absolute-git-dir"}).output);
    c.commonDir = pathOutput(git.run({"rev-parse", "--path-format=absolute", "--git-common-dir"}).output);
    if (c.gitDir.isEmpty() || c.commonDir.isEmpty() || !QDir::isAbsolutePath(c.commonDir)) throw Failure{"Invalid Git directory."};
    return c;
}
void parseChanges(Context& c, const QByteArray& output)
{
    qsizetype at = 0;
    while (at < output.size()) {
        const auto end = output.indexOf('\0', at);
        if (end < at + 4 || output.at(at + 2) != ' ') throw Failure{"Invalid Git status response."};
        const auto xy = output.mid(at, 2);
        auto path = output.mid(at + 3, end - at - 3);
        const bool directoryEntry = path.endsWith('/');
        if (directoryEntry) path.chop(1);
        at = end + 1;
        QList<QByteArray> paths{path};
        QByteArray old;
        if (xy.contains('R') || xy.contains('C')) {
            const auto oldEnd = output.indexOf('\0', at);
            if (oldEnd < 0) throw Failure{"Invalid renamed Git path."};
            old = output.mid(at, oldEnd - at); if (xy.contains('R')) paths.append(old); at = oldEnd + 1;
        }
        for (const auto& p : paths) if (!safePath(p)) throw Failure{"Invalid repository-relative Git path."};
        const bool ignored = xy == "!!", untracked = xy == "??";
        const bool conflict = xy.contains('U') || xy == "AA" || xy == "DD";
        if (!ignored) c.dirty = true;
        c.conflicts = c.conflicts || conflict;
        if (c.changes.size() >= maxChanges) { c.truncated = true; continue; }
        const auto id = QString::fromLatin1(path.toBase64());
        QString status = ignored ? "ignored" : conflict ? "conflict" : untracked ? "untracked"
            : xy.contains('R') ? "renamed" : xy.contains('D') ? "deleted" : xy.contains('A') ? "added" : "modified";
        const bool selectable = !ignored && !conflict && !directoryEntry;
        if (selectable) c.paths.insert(id, paths);
        c.changes.append(QVariantMap{{"id", id}, {"path", QFile::decodeName(path)}, {"oldPath", QFile::decodeName(old)},
            {"status", status}, {"staged", !untracked && !ignored && xy.at(0) != ' '},
            {"unstaged", !untracked && !ignored && xy.at(1) != ' '}, {"untracked", untracked},
            {"deleted", xy.contains('D')}, {"conflict", conflict}, {"directory", directoryEntry}, {"selectable", selectable}});
    }
    c.dirtyKnown = true;
}
void snapshot(Runner& git, Context& c, bool details)
{
    const auto symbolic = git.run({"symbolic-ref", "-q", "HEAD"}, {}, {}, false);
    c.ref = QString::fromUtf8(symbolic.output).trimmed();
    c.head = oid(git.run({"rev-parse", "--verify", "HEAD"}, {}, {}, false).output);
    c.detached = c.ref.isEmpty();
    c.branch = c.detached ? c.head.left(8) : c.ref.mid(QStringLiteral("refs/heads/").size());
    if (!c.detached && !c.ref.startsWith("refs/heads/")) throw Failure{"Unsupported HEAD reference."};
    if (!c.ref.isEmpty() && !c.head.isEmpty()) {
        const auto fields = git.run({"for-each-ref", "--count=1", "--format=%(refname)%00%(upstream)%00%(upstream:remotename)%00%(upstream:remoteref)%00", c.ref}).output.split('\0');
        if (fields.size() >= 4 && QString::fromUtf8(fields.at(0)) == c.ref) {
            c.upstream = QString::fromUtf8(fields.at(1)); c.remote = QString::fromUtf8(fields.at(2)); c.remoteRef = QString::fromUtf8(fields.at(3));
        }
    }
    if (!c.upstream.isEmpty() && !c.head.isEmpty()) {
        const auto counts = git.run({"rev-list", "--left-right", "--count", "HEAD...@{upstream}"}, {}, {}, false);
        const auto values = QString::fromLatin1(counts.output).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        bool left = false, right = false;
        if (counts.code == 0 && values.size() == 2) {
            c.ahead = values.at(0).toInt(&left); c.behind = values.at(1).toInt(&right);
            c.countsKnown = left && right && c.ahead >= 0 && c.behind >= 0;
        }
    }
    if (!c.remote.isEmpty() && c.remote != ".") {
        c.remoteUrl = QString::fromUtf8(git.run({"remote", "get-url", "--", c.remote}, {}, {}, false).output).trimmed();
        c.pushUrls = QString::fromUtf8(git.run({"remote", "get-url", "--push", "--all", "--", c.remote}, {}, {}, false).output).trimmed().split('\n', Qt::SkipEmptyParts);
    } else if (c.remote.isEmpty()) {
        const auto remotes = QString::fromUtf8(git.run({"remote"}).output).split('\n', Qt::SkipEmptyParts);
        if (remotes.size() == 1) c.remoteUrl = QString::fromUtf8(git.run({"remote", "get-url", "--", remotes.first()}, {}, {}, false).output).trimmed();
    }
    const QDir dir(c.gitDir);
    for (const auto& path : {"MERGE_HEAD", "CHERRY_PICK_HEAD", "REVERT_HEAD", "rebase-merge", "rebase-apply", "sequencer"})
        c.inProgress = c.inProgress || QFileInfo::exists(dir.filePath(path));
    if (details) {
        parseChanges(c, git.run({"status", "--porcelain=v1", "-z", "--untracked-files=all"}).output);
    } else {
        c.dirtyKnown = git.input.dirtyState != "repository" && !git.input.dirtyState.isEmpty();
        c.dirty = c.dirtyKnown && git.input.dirtyState != "clean";
    }
}
void assertSame(const Input& input, const Context& expected, const Context& actual)
{
    if (expected.root != actual.root || expected.commonDir != actual.commonDir || expected.ref != actual.ref || expected.head != actual.head
        || expected.upstream != actual.upstream || expected.remote != actual.remote || expected.remoteRef != actual.remoteRef || expected.remoteUrl != actual.remoteUrl || expected.pushUrls != actual.pushUrls)
        throw Failure{text(input.tr, "The branch, HEAD, upstream or remote address changed. Refresh before trying again.", "Dal, HEAD, upstream veya uzak depo adresi değişti. Tekrar denemeden önce yenile.")};
}
bool standardRemote(const QString& url)
{
    if (url.isEmpty() || url.contains('\n') || url.contains('\r') || url.contains(QChar::Null)) return false;
    if (QDir::isAbsolutePath(url)) return true;
    if (QRegularExpression("^[A-Za-z][A-Za-z0-9+.-]*::").match(url).hasMatch()) return false;
    if (!url.contains("://")) return true; // local path or scp-style SSH target
    const QUrl parsed(url);
    return parsed.isValid() && QStringList{"https", "http", "ssh", "git", "file"}.contains(parsed.scheme().toLower());
}
void requireUpstream(const Input& input, const Context& c)
{
    if (c.detached || c.head.isEmpty()) throw Failure{text(input.tr, "Choose a branch with a commit before synchronizing.", "Eşitlemeden önce commit içeren bir dal seç.")};
    if (c.upstream.isEmpty() || c.remote.isEmpty() || c.remoteRef.isEmpty())
        throw Failure{text(input.tr, "No upstream is configured. Choose it explicitly in repository information.", "Upstream tanımlı değil. Depo bilgilerinden açıkça seç.")};
    if (c.remote != "." && (!c.upstream.startsWith("refs/remotes/") || !c.remoteRef.startsWith("refs/heads/") || !standardRemote(c.remoteUrl)))
        throw Failure{text(input.tr, "This upstream uses an unsupported remote or reference mapping.", "Bu upstream desteklenmeyen bir uzak depo veya referans eşlemesi kullanıyor.")};
}
void fetchUpstream(Runner& git, const Context& c)
{
    requireUpstream(git.input, c);
    if (c.remote == ".") return;
    git.run({"fetch", "--progress", "--no-recurse-submodules", "--", c.remote, "+" + c.remoteRef + ":" + c.upstream}, {}, {}, true, true);
}
QStringList nulPaths(const QByteArray& bytes, const QString& root)
{
    QStringList result;
    for (const auto& p : bytes.split('\0')) if (!p.isEmpty()) {
        if (!safePath(p)) throw Failure{"Invalid changed Git path."};
        result.append(QDir(root).filePath(QFile::decodeName(p)));
    }
    return result;
}
void pushUpstream(Runner& git, const Context& c)
{
    requireUpstream(git.input, c);
    if (c.remote != "." && (c.pushUrls.isEmpty() || std::any_of(c.pushUrls.cbegin(), c.pushUrls.cend(), [](const QString& url) { return !standardRemote(url); })))
        throw Failure{text(git.input.tr, "Unsupported Push remote protocol.", "Desteklenmeyen Push uzak depo protokolü.")};
    git.run({"push", "--progress", "--porcelain", "--", c.remote, c.head + ":" + c.remoteRef}, {}, {}, true, true);
}
struct Version {
    quint64 major = 0, minor = 0, patch = 0;
    bool prefix = true, stable = false, valid = false;
};
Version parseVersion(const QString& value)
{
    // A patch increment must never become a minor bump at .9: 1.5.9 -> 1.5.10.
    static const QRegularExpression semver(QStringLiteral(R"(^(v?)(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-([0-9A-Za-z.-]+))?(?:\+([0-9A-Za-z.-]+))?$)"));
    const auto match = semver.match(value);
    Version version;
    if (!match.hasMatch()) return version;
    bool validMajor = false, validMinor = false, validPatch = false;
    version.major = match.captured(2).toULongLong(&validMajor);
    version.minor = match.captured(3).toULongLong(&validMinor);
    version.patch = match.captured(4).toULongLong(&validPatch);
    version.prefix = !match.captured(1).isEmpty();
    version.stable = match.captured(5).isEmpty();
    version.valid = validMajor && validMinor && validPatch;
    return version;
}
bool newer(const Version& a, const Version& b)
{
    if (a.major != b.major) return a.major > b.major;
    if (a.minor != b.minor) return a.minor > b.minor;
    return a.patch > b.patch;
}
QString formatVersion(const Version& v, const QString& bump)
{
    const auto limit = std::numeric_limits<quint64>::max();
    if ((bump == "major" && v.major == limit) || (bump == "minor" && v.minor == limit)
        || (bump == "patch" && v.patch == limit)) return {};
    const auto major = v.major + (bump == "major");
    const auto minor = bump == "major" ? 0 : v.minor + (bump == "minor");
    const auto patch = bump == "patch" ? v.patch + 1 : 0;
    return (v.prefix ? "v" : "") + QString::number(major) + '.' + QString::number(minor) + '.' + QString::number(patch);
}
struct TagSummary {
    QMap<QString, QString> local, remote; // Refname -> immutable tag object ID
    QMap<QString, QString> commits, remoteCommits;
    QString remoteName, remoteError;
    bool verified = false;
};
QString tagRemote(Runner& git, const Context& c)
{
    if (!c.remote.isEmpty() && c.remote != ".") return c.remote;
    const auto remotes = QString::fromUtf8(git.run({"remote"}).output).split('\n', Qt::SkipEmptyParts);
    return remotes.size() == 1 ? remotes.first() : QString();
}
TagSummary inspectTags(Runner& git, const Context& c, bool requireRemote)
{
    TagSummary summary;
    const auto records = git.run({"for-each-ref", "--format=%(refname:strip=2)%00%(objectname)%00%(*objectname)%00", "refs/tags"}).output.split('\0');
    for (int i = 0; i + 2 < records.size(); i += 3) {
        const auto name = QString::fromUtf8(records.at(i)).trimmed();
        const auto object = oid(records.at(i + 1));
        const auto peeled = oid(records.at(i + 2));
        if (name.isEmpty() || object.isEmpty()) continue;
        summary.local.insert(name, object);
        summary.commits.insert(name, peeled.isEmpty() ? object : peeled);
    }
    summary.remoteName = tagRemote(git, c);
    if (summary.remoteName.isEmpty()) {
        summary.remoteError = text(git.input.tr, "Select a remote before verifying tags.", "Tag doğrulamak için uzak depo seç.");
    } else {
        try {
            const auto url = QString::fromUtf8(git.run({"remote", "get-url", "--", summary.remoteName}).output).trimmed();
            const auto pushUrls = QString::fromUtf8(git.run({"remote", "get-url", "--push", "--all", "--", summary.remoteName}).output).trimmed().split('\n', Qt::SkipEmptyParts);
            if (!standardRemote(url) || pushUrls.isEmpty() || std::any_of(pushUrls.cbegin(), pushUrls.cend(), [](const QString& address) { return !standardRemote(address); }))
                throw Failure{text(git.input.tr, "Unsupported remote address.", "Desteklenmeyen uzak depo adresi.")};
            const auto bytes = git.run({"ls-remote", "--tags", "--", summary.remoteName}, {}, {}, true, true, true, false, 15000).output;
            for (const auto& line : bytes.split('\n')) {
                const int tab = line.indexOf('\t');
                if (tab < 1) continue;
                const auto hash = oid(line.left(tab));
                auto ref = QString::fromUtf8(line.mid(tab + 1));
                if (!ref.startsWith("refs/tags/") || hash.isEmpty()) continue;
                ref.remove(0, 10);
                if (ref.endsWith("^{}")) {
                    ref.chop(3);
                    summary.remoteCommits.insert(ref, hash); // Annotated tag's target commit.
                    continue;
                }
                summary.remote.insert(ref, hash);
            }
            summary.verified = true;
        } catch (const Failure& err) { summary.remoteError = err.text; }
    }
    if (requireRemote && !summary.verified) throw Failure{
        text(git.input.tr, "Remote tags could not be verified. No commit or tag was created: ", "Uzak tag'ler doğrulanamadı. Commit veya tag oluşturulmadı: ") + summary.remoteError};
    return summary;
}
QVariantMap tagInformation(const TagSummary& summary)
{
    Version latest; bool hasStable = false; QString name;
    const auto consider = [&](const QString& candidate) {
        const auto version = parseVersion(candidate);
        if (version.valid && version.stable && (!hasStable || newer(version, latest)
            || (!newer(latest, version) && version.prefix && !latest.prefix))) {
            latest = version; name = candidate; hasStable = true;
        }
    };
    for (auto it = summary.local.cbegin(); it != summary.local.cend(); ++it) consider(it.key());
    for (auto it = summary.remote.cbegin(); it != summary.remote.cend(); ++it) consider(it.key());
    if (!hasStable) { latest.prefix = true; latest.major = 0; latest.minor = 0; latest.patch = 0; }
    return {{"latest", name}, {"assumedInitial", !hasStable},
            {"patch", hasStable ? formatVersion(latest, "patch") : "v0.1.0"},
            {"minor", hasStable ? formatVersion(latest, "minor") : "v0.1.0"},
            {"major", hasStable ? formatVersion(latest, "major") : "v1.0.0"},
            {"remoteVerified", summary.verified}, {"remoteName", summary.remoteName},
            {"remoteError", summary.remoteError}};
}
QVariantList tagRows(const TagSummary& summary)
{
    QVariantList rows;
    QSet<QString> names;
    for (auto it = summary.local.cbegin(); it != summary.local.cend(); ++it) names.insert(it.key());
    for (auto it = summary.remote.cbegin(); it != summary.remote.cend(); ++it) names.insert(it.key());
    QStringList sorted = names.values(); sorted.sort(Qt::CaseInsensitive);
    for (const auto& name : sorted)
        rows.append(QVariantMap{{"name", name}, {"local", summary.local.contains(name)},
            {"remote", summary.remote.contains(name)}, {"commit", summary.commits.value(name, summary.remoteCommits.value(name, summary.remote.value(name)))},
            {"mismatch", summary.local.contains(name) && summary.remote.contains(name) && summary.local.value(name) != summary.remote.value(name)}});
    return rows;
}
void requireTag(const Input& input, const TagSummary& tags, const QString& name)
{
    if (!parseVersion(name).valid || name.size() > 120)
        throw Failure{text(input.tr, "Enter a valid Semantic Version tag (e.g. v1.6.0).", "Geçerli bir SemVer etiketi gir (ör. v1.6.0).")};
    if (tags.local.contains(name) || tags.remote.contains(name))
        throw Failure{text(input.tr, "That tag already exists locally or remotely. Choose a new version.", "Bu tag yerelde veya uzakta zaten var. Yeni bir sürüm seç.")};
}
void step(Result& result, const QString& kind, const QString& message)
{
    result.steps.append(QVariantMap{{"kind", kind}, {"message", message}});
}
void createAnnotatedTag(Runner& git, const QString& tag, const QString& commit)
{
    git.run({"tag", "-a", "-m", "Release " + tag, "--", tag, commit}, {}, {}, true, false, true);
}
void sendTag(Runner& git, const QString& remote, const QString& tag)
{
    git.run({"push", "--porcelain", "--", remote, "refs/tags/" + tag + ":refs/tags/" + tag}, {}, {}, true, true);
}
Result perform(const Input& input)
{
    Result result; result.kind = input.kind;
    Runner git(input);
    std::unique_ptr<QLockFile> lock;
    try {
        auto c = resolve(git);
        result.root = c.root; result.commonDir = c.commonDir;
        const bool writing = QStringList{"sync", "fetch", "pull", "push", "commit", "upstream", "create-tag", "push-tag"}.contains(input.kind);
        if (writing) {
            const auto directory = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/git-operations";
            if (!QDir().mkpath(directory)) throw Failure{text(input.tr, "Cannot create the Git operation lock directory.", "Git işlem kilidi dizini oluşturulamadı.")};
            const auto name = QString::fromLatin1(QCryptographicHash::hash(QFile::encodeName(c.commonDir), QCryptographicHash::Sha256).toHex());
            lock = std::make_unique<QLockFile>(directory + '/' + name + ".lock");
            lock->setStaleLockTime(0);
            if (!lock->tryLock(0)) throw Failure{text(input.tr, "Another Lurviko operation is writing this repository or one of its worktrees.", "Başka bir Lurviko işlemi bu depoya veya worktree'lerinden birine yazıyor.")};
        }
        snapshot(git, c, input.kind == "changes" || input.kind == "commit" || input.kind == "sync" || input.kind == "pull");
        result.info = publicInfo(c);
        if (input.kind == "metadata") { result.ok = true; return result; }
        if (input.kind == "tags") { const auto tags = inspectTags(git, c, false); result.tags = tagRows(tags); result.tagInfo = tagInformation(tags); result.ok = true; return result; }
        if (input.kind == "changes") { result.changes = c.changes; result.ok = true; return result; }
        if (input.kind == "history") {
            if (!c.head.isEmpty()) {
                auto records = git.run({"log", "-n", "50", "--date=iso-strict", "--format=%H%x00%h%x00%an%x00%aI%x00%s%x00"}).output;
                const auto fields = records.split('\0');
                for (int i = 0; i + 4 < fields.size(); i += 5) {
                    const auto hash = oid(fields.at(i)); if (hash.isEmpty()) continue;
                    result.history.append(QVariantMap{{"hash", hash}, {"shortHash", QString::fromUtf8(fields.at(i+1))},
                        {"author", QString::fromUtf8(fields.at(i+2))}, {"date", QString::fromUtf8(fields.at(i+3))}, {"subject", QString::fromUtf8(fields.at(i+4)).left(1000)}});
                }
            }
            result.ok = true; return result;
        }
        if (input.kind == "diff" || input.kind == "compare") {
            if (input.kind == "compare") {
                requireUpstream(input, c);
                result.detailText = QString::fromUtf8(git.run({"log", "--left-right", "--oneline", "--max-count=100", "HEAD...@{upstream}"}).output)
                    + "\n" + QString::fromUtf8(git.run({"diff", "--no-ext-diff", "--no-textconv", "--stat", "HEAD", c.upstream, "--"}).output);
            } else {
                result.detailText = text(input.tr, "Staged changes\n", "Stage edilmiş değişiklikler\n")
                    + QString::fromUtf8(git.run({"diff", "--cached", "--no-ext-diff", "--no-textconv", "--", "."}).output)
                    + text(input.tr, "\nUnstaged changes\n", "\nStage edilmemiş değişiklikler\n")
                    + QString::fromUtf8(git.run({"diff", "--no-ext-diff", "--no-textconv", "--", "."}).output);
            }
            result.detailText = result.detailText.left(1024 * 1024);
            result.ok = true; return result;
        }
        if (input.kind == "targets" || input.kind == "upstream") {
            const auto records = git.run({"for-each-ref", "--format=%(refname)%00%(symref)%00", "refs/remotes/"}).output.split('\0');
            QStringList targets;
            for (int i = 0; i + 1 < records.size(); i += 2) {
                const auto ref = QString::fromUtf8(records.at(i)).trimmed();
                if (!ref.startsWith("refs/remotes/") || !records.at(i + 1).isEmpty()) continue;
                targets.append(ref);
                result.upstreamTargets.append(QVariantMap{{"ref", ref}, {"label", ref.mid(13)}});
            }
            if (input.kind == "targets") { result.ok = true; return result; }
            const auto selected = input.parameters.value("ref").toString();
            if (c.detached || c.head.isEmpty() || !targets.contains(selected)) throw Failure{text(input.tr, "Select an existing remote branch as upstream.", "Upstream olarak mevcut bir uzak dal seç.")};
            auto current = resolve(git); snapshot(git, current, false); assertSame(input, c, current);
            result.wrote = true;
            git.run({"branch", "--set-upstream-to=" + selected, "--", c.branch});
            result.message = text(input.tr, "Upstream configured.", "Upstream tanımlandı.");
        } else if (input.kind == "fetch") {
            if (!c.upstream.isEmpty() && !c.remote.isEmpty()) { result.wrote = true; fetchUpstream(git, c); }
            else {
                const auto remotes = QString::fromUtf8(git.run({"remote"}).output).split('\n', Qt::SkipEmptyParts);
                if (remotes.isEmpty()) throw Failure{text(input.tr, "No remote repository is configured.", "Uzak depo tanımlı değil.")};
                for (const auto& remote : remotes) {
                    const auto url = QString::fromUtf8(git.run({"remote", "get-url", "--", remote}).output).trimmed();
                    if (!standardRemote(url)) throw Failure{text(input.tr, "Unsupported remote protocol.", "Desteklenmeyen uzak depo protokolü.")};
                    git.run({"check-ref-format", "refs/remotes/" + remote + "/probe"});
                    result.wrote = true;
                    git.run({"fetch", "--progress", "--no-recurse-submodules", "--prune", "--", remote,
                             "+refs/heads/*:refs/remotes/" + remote + "/*"}, {}, {}, true, true);
                }
            }
            result.message = text(input.tr, "Remote information fetched.", "Uzak depo bilgileri alındı.");
        } else if (input.kind == "sync" || input.kind == "pull") {
            requireUpstream(input, c);
            if (c.inProgress) throw Failure{text(input.tr, "Finish the current merge or rebase first.", "Önce mevcut merge veya rebase işlemini tamamla.")};
            result.wrote = true; fetchUpstream(git, c);
            auto fresh = resolve(git); snapshot(git, fresh, true); assertSame(input, c, fresh); c = fresh;
            if (!c.countsKnown) throw Failure{text(input.tr, "The upstream branch could not be resolved after Fetch.", "Fetch sonrasında upstream dalı bulunamadı.")};
            if (c.ahead && c.behind) throw Failure{text(input.tr, "Both branches contain new commits. Compare the histories and resolve them manually; no merge or rebase was performed.", "Her iki dalda da yeni commit var. Geçmişleri karşılaştırıp elle çöz; merge veya rebase yapılmadı.")};
            if (c.behind) {
                if (c.dirty || c.inProgress) throw Failure{text(input.tr, "Commit or safely save your local changes before updating. No automatic stash or overwrite was performed.", "Güncellemeden önce yerel değişikliklerini commit et veya güvenli biçimde sakla. Otomatik stash veya üzerine yazma yapılmadı.")};
                const auto target = oid(git.run({"rev-parse", "--verify", "@{upstream}"}).output);
                if (target.isEmpty()) throw Failure{"Invalid upstream commit."};
                result.touched = nulPaths(git.run({"diff", "--name-only", "--no-renames", "-z", c.head, target, "--"}).output, c.root);
                auto latest = resolve(git); snapshot(git, latest, true); assertSame(input, c, latest);
                if (latest.dirty || latest.inProgress) throw Failure{text(input.tr, "Local changes appeared. Update cancelled.", "Yerel değişiklik oluştu. Güncelleme iptal edildi.")};
                git.run({"merge", "--ff-only", "--no-edit", target}, {}, {}, true, false, true);
                result.message = text(input.tr, "Repository updated by fast-forward.", "Depo fast-forward ile güncellendi.");
            } else if (c.ahead && input.kind == "sync") {
                try { pushUpstream(git, c); }
                catch (const Failure& failure) {
                    if (!input.cancelled->load()) { try { fetchUpstream(git, c); } catch (...) {} }
                    throw failure;
                }
                result.message = text(input.tr, "Local commits pushed.", "Yerel commit'ler gönderildi.");
            } else result.message = text(input.tr, "Repository is already up to date.", "Depo zaten güncel.");
        } else if (input.kind == "push") {
            requireUpstream(input, c);
            auto latest = resolve(git); snapshot(git, latest, false); assertSame(input, c, latest);
            result.wrote = true;
            try { pushUpstream(git, c); }
            catch (const Failure& failure) {
                if (!input.cancelled->load()) { try { fetchUpstream(git, c); } catch (...) {} }
                throw failure;
            }
            result.message = text(input.tr, "Local commits pushed.", "Yerel commit'ler gönderildi.");
        } else if (input.kind == "commit") {
            if (c.inProgress || c.conflicts) throw Failure{text(input.tr, "Resolve the current merge, rebase or conflicts before committing here.", "Buradan commit etmeden önce mevcut merge, rebase veya çakışmaları çöz.")};
            const auto message = input.parameters.value("message").toString();
            const auto tag = input.parameters.value("tag").toString();
            TagSummary preflight;
            if (!tag.isEmpty()) {
                if (!input.parameters.value("pushAfter").toBool())
                    throw Failure{text(input.tr, "Pushing a new tag requires Push after commit.", "Yeni tag gönderimi için Commit sonrası Push gerekli.")};
                requireUpstream(input, c);
                preflight = inspectTags(git, c, true);
                requireTag(input, preflight, tag);
            }
            const auto ids = input.parameters.value("ids").toStringList();
            if (ids.isEmpty() || ids.size() > maxChanges || message.trimmed().isEmpty() || message.contains(QChar::Null) || message.size() > 65536)
                throw Failure{text(input.tr, "Select files and enter a commit message.", "Dosyaları seç ve commit mesajı gir.")};
            QSet<QByteArray> selected, primary;
            for (const auto& id : ids) {
                if (!c.paths.contains(id)) throw Failure{text(input.tr, "A selected change is no longer available. Refresh the file list.", "Seçilen bir değişiklik artık yok. Dosya listesini yenile.")};
                primary.insert(c.paths.value(id).first());
                for (const auto& p : c.paths.value(id)) selected.insert(p);
            }
            QByteArray pathspec, stagePaths, removePaths;
            for (const auto& p : primary) { stagePaths += p; stagePaths += '\0'; }
            for (const auto& id : ids) {
                const auto paths = c.paths.value(id);
                if (paths.size() == 2 && !primary.contains(paths.at(1))) { removePaths += paths.at(1); removePaths += '\0'; }
            }
            for (const auto& p : selected) { pathspec += p; pathspec += '\0'; }
            QTemporaryDir temporary(QDir::tempPath() + "/lurviko-git-index-XXXXXX");
            if (!temporary.isValid()) throw Failure{"Cannot create a temporary Git index."};
            const auto index = temporary.path() + "/index";
            git.run(c.head.isEmpty() ? QStringList{"read-tree", "--empty"} : QStringList{"read-tree", c.head}, {}, index, true, false, true);
            if (!removePaths.isEmpty()) git.run({"update-index", "--force-remove", "-z", "--stdin"}, removePaths, index, true, false, true);
            git.run({"--literal-pathspecs", "add", "--all", "--pathspec-from-file=-", "--pathspec-file-nul"}, stagePaths, index, true, false, true);
            const auto actual = git.run({"diff", "--cached", "--no-ext-diff", "--no-textconv", "--no-renames", "--name-only", "-z"}, {}, index).output;
            bool any = false;
            for (const auto& p : actual.split('\0')) if (!p.isEmpty()) {
                any = true;
                if (!selected.contains(p)) throw Failure{text(input.tr, "The selected paths changed into a different file set. Nothing was committed.", "Seçilen yollar farklı bir dosya kümesine dönüştü. Commit oluşturulmadı.")};
            }
            if (!any) throw Failure{text(input.tr, "The selected files have no changes to commit.", "Seçilen dosyalarda commit edilecek değişiklik yok.")};
            const auto tree = oid(git.run({"write-tree"}, {}, index, true, false, true).output);
            if (tree.isEmpty()) throw Failure{"Cannot create the selected Git tree."};
            auto latest = resolve(git); snapshot(git, latest, false); assertSame(input, c, latest);
            QStringList args{"commit-tree", tree};
            if (!c.head.isEmpty()) args << "-p" << c.head;
            const auto signing = git.run({"config", "--bool", "commit.gpgsign"}, {}, {}, false);
            if (signing.code != 0 && signing.code != 1)
                throw Failure{signing.error.isEmpty() ? text(input.tr, "Invalid commit signing configuration.", "Commit imzalama ayarı geçersiz.") : signing.error};
            if (signing.code == 0 && signing.output.trimmed() == "true") args << "-S";
            args << "-F" << "-";
            const auto commit = oid(git.run(args, message.toUtf8() + '\n', index, true, true, true).output);
            if (commit.isEmpty()) throw Failure{"Cannot create the selected commit."};
            latest = resolve(git); snapshot(git, latest, false); assertSame(input, c, latest);
            const auto targetRef = c.ref.isEmpty() ? QStringLiteral("HEAD") : c.ref;
            result.wrote = true;
            const auto old = c.head.isEmpty() ? QString(commit.size(), '0') : c.head;
            try { git.run({"update-ref", "--no-deref", "-m", "commit: " + message.section('\n', 0, 0).left(200), targetRef, commit, old}, {}, {}, true, false, true); }
            catch (const Failure&) {
                const auto now = git.run({"rev-parse", "--verify", targetRef}, {}, {}, false, false, true, true);
                if (oid(now.output) == commit) result.localCommit = commit;
                throw;
            }
            result.localCommit = commit;
            step(result, "success", text(input.tr, "Commit created: ", "Commit oluşturuldu: ") + commit.left(8));
            git.run({"--literal-pathspecs", "reset", "-q", commit, "--pathspec-from-file=-", "--pathspec-file-nul"}, pathspec, {}, true, false, true);
            if (!tag.isEmpty()) {
                createAnnotatedTag(git, tag, commit);
                result.localTag = tag;
                step(result, "success", text(input.tr, "Local tag created: ", "Yerel tag oluşturuldu: ") + tag);
            }
            if (input.parameters.value("pushAfter").toBool()) {
                auto committed = resolve(git); snapshot(git, committed, false);
                auto expected = c; expected.head = commit;
                assertSame(input, expected, committed);
                try { pushUpstream(git, committed); step(result, "success", text(input.tr, "Branch pushed.", "Dal uzak depoya gönderildi.")); }
                catch (const Failure& failure) {
                    if (!input.cancelled->load()) { try { fetchUpstream(git, committed); } catch (...) {} }
                    throw failure;
                }
            }
            if (!tag.isEmpty()) {
                sendTag(git, c.remote, tag);
                step(result, "success", text(input.tr, "Tag pushed: ", "Tag uzak depoya gönderildi: ") + tag);
            }
            result.message = text(input.tr, "Commit created: ", "Commit oluşturuldu: ") + commit.left(8);
        } else if (input.kind == "create-tag" || input.kind == "push-tag") {
            if (c.head.isEmpty()) throw Failure{text(input.tr, "Cannot tag an empty repository.", "Boş depoda tag oluşturulamaz.")};
            const auto name = input.parameters.value("name").toString();
            if (!parseVersion(name).valid || name.size() > 120) throw Failure{text(input.tr, "Invalid SemVer tag.", "Geçersiz SemVer etiketi.")};
            const bool send = input.parameters.value("send").toBool() || input.kind == "push-tag";
            if (send) requireUpstream(input, c);
            const auto tags = inspectTags(git, c, send);
            if (input.kind == "create-tag") {
                requireTag(input, tags, name);
                if (send) {
                    const auto remoteTip = git.run({"ls-remote", "--heads", "--", tags.remoteName, c.remoteRef}, {}, {}, true, true, true, false, 15000).output;
                    if (!remoteTip.startsWith(c.head.toUtf8() + '\t'))
                        throw Failure{text(input.tr, "Push the current commit to its upstream branch before tagging remotely.", "Tag'i göndermeden önce geçerli commit'i upstream dalına gönder.")};
                }
                createAnnotatedTag(git, name, c.head);
                result.localTag = name; result.wrote = true;
                step(result, "success", text(input.tr, "Local tag created: ", "Yerel tag oluşturuldu: ") + name);
            } else {
                if (!tags.local.contains(name)) throw Failure{text(input.tr, "This tag does not exist locally.", "Bu tag yerelde bulunmuyor.")};
                if (tags.remote.contains(name)) {
                    if (tags.remote.value(name) != tags.local.value(name))
                        throw Failure{text(input.tr, "Remote tag points to another object; refusing to overwrite it.", "Uzak tag başka bir nesneye işaret ediyor; üzerine yazılmayacak.")};
                    step(result, "info", text(input.tr, "Tag is already present remotely: ", "Tag zaten uzakta mevcut: ") + name);
                    result.message = text(input.tr, "Tag is already synchronized.", "Tag zaten eşitlenmiş.");
                    result.ok = true; return result;
                }
                // Only publish tags on a commit already reachable from the configured upstream.
                const auto target = oid(git.run({"rev-parse", "--verify", "refs/tags/" + name + "^{commit}"}).output);
                const auto tipBytes = git.run({"ls-remote", "--heads", "--", tags.remoteName, c.remoteRef}, {}, {}, true, true, true, false, 15000).output;
                const auto tip = oid(tipBytes.left(tipBytes.indexOf('\t')));
                if (target.isEmpty() || tip.isEmpty() || git.run({"merge-base", "--is-ancestor", target, tip}, {}, {}, false).code != 0)
                    throw Failure{text(input.tr, "The tag target is not confirmed on the remote branch. Push the branch first.", "Tag'in hedefi uzak dalda doğrulanamadı. Önce dalı gönder.")};
            }
            if (send) {
                sendTag(git, tags.remoteName, name); result.wrote = true;
                step(result, "success", text(input.tr, "Tag pushed: ", "Tag uzak depoya gönderildi: ") + name);
            }
            result.message = send ? text(input.tr, "Tag published: ", "Tag yayımlandı: ") + name
                                  : text(input.tr, "Tag created locally: ", "Tag yerelde oluşturuldu: ") + name;
        }
        result.ok = true;
    } catch (const Failure& failure) {
        result.error = displayError(failure.text);
        step(result, "error", result.error);
        if (!result.localCommit.isEmpty()) {
            step(result, "info", text(input.tr, "Local commit preserved: ", "Yerel commit korundu: ") + result.localCommit.left(8));
            result.error = text(input.tr, "Local commit preserved: ", "Yerel commit korundu: ") + result.localCommit.left(8) + "\n" + result.error;
        }
        if (!result.localTag.isEmpty()) step(result, "info", text(input.tr, "Local tag preserved for retry: ", "Yerel tag yeniden deneme için korundu: ") + result.localTag);
    } catch (const std::exception& failure) { result.error = displayError(QString::fromUtf8(failure.what())); step(result, "error", result.error); }
    return result;
}
}

struct GitOperations::Private {
    QString program, directory, discoveredRoot, pendingKind = "metadata", error, progress, lastCommit;
    QVariantMap info;
    QVariantList changes, history, upstreamTargets, tags, steps;
    QVariantMap tagInfo;
    QString detailText;
    QPointer<GitStatusCache> cache;
    QPointer<QFutureWatcher<Result>> reader, writer;
    QTimer debounce;
    std::shared_ptr<std::atomic_bool> readCancelled, writeCancelled;
    quint64 generation = 0;
    bool pending = false, loading = false, busy = false, closing = false, metadataAfter = false;
};

GitOperations::GitOperations(QObject* parent)
    : GitOperations(QStandardPaths::findExecutable("git"), parent) {}
GitOperations::GitOperations(const QString& program, QObject* parent) : QObject(parent), d(new Private)
{
    d->program = program; d->cache = GitStatusCache::instance();
    d->debounce.setSingleShot(true); d->debounce.setInterval(250);
    connect(&d->debounce, &QTimer::timeout, this, [this] { read("metadata", true); });
    connect(d->cache, &GitStatusCache::directoryReady, this, [this](const QString& directory, const QString& root) {
        if (d->closing || directory != d->directory) return;
        const bool rootChanged = root != d->discoveredRoot;
        d->discoveredRoot = root;
        if (root.isEmpty()) {
            ++d->generation; if (d->readCancelled) d->readCancelled->store(true);
            d->info.clear(); d->loading = false; d->pending = false; emit changed(); return;
        }
        // directoryReady is shared with file models. A model refresh must not
        // enqueue another full detail read behind the one already running.
        // Subsequent statusChanged events coalesce through our metadata timer.
        if (rootChanged || (!d->reader && d->info.isEmpty())) d->pending = true;
        if (d->pending) startRead();
    });
    connect(d->cache, &GitStatusCache::statusChanged, this, [this](const QString& root) {
        if (root == d->discoveredRoot && !d->directory.isEmpty()) d->debounce.start();
    });
}
GitOperations::~GitOperations()
{
    d->closing = true;
    if (d->readCancelled) d->readCancelled->store(true);
    if (d->writeCancelled) d->writeCancelled->store(true);
    if (d->cache) d->cache->request(this, {});
}
bool GitOperations::available() const { return !d->info.value("root").toString().isEmpty(); }
bool GitOperations::loading() const { return d->loading; }
bool GitOperations::busy() const { return d->busy; }
QVariantMap GitOperations::info() const { return d->info; }
QVariantList GitOperations::changes() const { return d->changes; }
QVariantList GitOperations::history() const { return d->history; }
QVariantList GitOperations::upstreamTargets() const { return d->upstreamTargets; }
QVariantList GitOperations::tags() const { return d->tags; }
QVariantMap GitOperations::tagInfo() const { return d->tagInfo; }
QVariantList GitOperations::operationSteps() const { return d->steps; }
QString GitOperations::detailText() const { return d->detailText; }
QString GitOperations::error() const { return d->error; }
QString GitOperations::progress() const { return d->progress; }
QString GitOperations::lastCommit() const { return d->lastCommit; }
void GitOperations::setContext(const QString& location, bool directory)
{
    const QUrl url(location);
    QString path = url.isLocalFile() ? url.toLocalFile() : url.scheme().isEmpty() && QDir::isAbsolutePath(location) ? location : QString();
    if (!path.isEmpty()) path = directory ? QDir::cleanPath(path) : QFileInfo(path).absolutePath();
    if (path == d->directory) return;
    ++d->generation; if (d->readCancelled) d->readCancelled->store(true);
    d->directory = path; d->discoveredRoot.clear(); d->info.clear(); d->changes.clear(); d->history.clear(); d->upstreamTargets.clear(); d->tags.clear(); d->tagInfo.clear(); d->steps.clear(); d->detailText.clear();
    d->pendingKind = "metadata"; d->pending = false; d->metadataAfter = false;
    d->loading = !path.isEmpty() && !d->program.isEmpty();
    d->error.clear(); d->debounce.stop(); emit changed(); emit detailsChanged();
    d->cache->request(this, d->loading ? path : QString());
}
void GitOperations::read(const QString& kind, bool)
{
    if (d->directory.isEmpty() || d->program.isEmpty()) return;
    if (kind == "metadata" && d->reader && d->pendingKind != "metadata") { d->metadataAfter = true; return; }
    ++d->generation; if (d->readCancelled) d->readCancelled->store(true);
    d->pendingKind = kind; d->pending = true; d->loading = true; if (kind != "metadata") d->error.clear(); emit changed();
    if (d->discoveredRoot.isEmpty()) d->cache->request(this, d->directory);
    else startRead();
}
void GitOperations::startRead()
{
    if (d->reader || !d->pending || d->directory.isEmpty() || d->discoveredRoot.isEmpty()) return;
    d->pending = false;
    Input input; input.program = d->program; input.directory = d->directory; input.kind = d->pendingKind;
    input.dirtyState = d->cache->status(d->discoveredRoot, d->discoveredRoot);
    input.cancelled = d->readCancelled = std::make_shared<std::atomic_bool>(false);
    input.tr = QSettings().value("ui/language", QLocale::system().language() == QLocale::Turkish ? "tr" : "en").toString() == "tr";
    const auto generation = d->generation;
    auto* watcher = new QFutureWatcher<Result>(this); d->reader = watcher;
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, generation] {
        const auto result = watcher->result(); d->reader = nullptr; watcher->deleteLater();
        if (generation == d->generation) {
            d->loading = false;
            if (!result.error.isEmpty() || result.kind != "metadata") d->error = result.error;
            if (result.ok) {
                auto information = result.info;
                // Lightweight branch reads do not replace the loaded file
                // list, so they must retain that list's truncation warning.
                if (result.kind != "changes" && d->info.contains("changesTruncated"))
                    information.insert("changesTruncated", d->info.value("changesTruncated"));
                d->info = information;
                if (result.kind == "changes") d->changes = result.changes;
                if (result.kind == "history") d->history = result.history;
                if (result.kind == "targets") d->upstreamTargets = result.upstreamTargets;
                if (result.kind == "tags") { d->tags = result.tags; d->tagInfo = result.tagInfo; }
                if (result.kind == "diff" || result.kind == "compare") d->detailText = result.detailText;
                emit detailsChanged();
            } else if (d->info.isEmpty()) d->info.clear();
            emit changed();
        }
        if (d->pending) startRead();
        else if (d->metadataAfter) { d->metadataAfter = false; d->debounce.start(); }
    });
    watcher->setFuture(QtConcurrent::run(pool(), [input] { return perform(input); }));
}
void GitOperations::write(const QString& kind, const QVariantMap& parameters)
{
    const bool tr = QSettings().value("ui/language", QLocale::system().language() == QLocale::Turkish ? "tr" : "en").toString() == "tr";
    if (d->busy) { emit operationFinished(kind, false, text(tr, "A Git operation is already running.", "Bir Git işlemi zaten sürüyor.")); return; }
    if (!available()) { emit operationFinished(kind, false, text(tr, "No Git repository is available.", "Kullanılabilir Git deposu yok.")); return; }
    Input input; input.program = d->program; input.directory = d->info.value("root").toString(); input.kind = kind; input.parameters = parameters;
    input.cancelled = d->writeCancelled = std::make_shared<std::atomic_bool>(false);
    input.tr = tr;
    d->busy = true; d->error.clear(); d->lastCommit.clear(); d->steps.clear(); d->progress = text(tr, "Git operation in progress…", "Git işlemi sürüyor…"); emit changed();
    auto* watcher = new QFutureWatcher<Result>(this); d->writer = watcher;
    connect(watcher, &QFutureWatcher<Result>::progressTextChanged, this, [this](const QString& value) {
        if (d->busy) { d->progress = value; emit changed(); }
    });
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        const auto result = watcher->result(); watcher->deleteLater(); d->writer = nullptr;
        d->busy = false; d->progress.clear(); d->error = result.error; d->lastCommit = result.localCommit; d->steps = result.steps;
        emit changed();
        if (result.wrote) emit repositoryMutated(result.root, result.commonDir, result.touched);
        if (!d->directory.isEmpty()) { read("metadata", true); d->cache->request(this, d->directory, true); }
        emit operationFinished(result.kind, result.ok, result.ok ? result.message : result.error);
    });
    // Progress travels through the future, so a worker never dereferences a GUI
    // QObject even if its pane is destroyed during a network operation.
    watcher->setFuture(QtConcurrent::run(pool(), [input](QPromise<Result>& promise) mutable {
        int progress = 0;
        promise.setProgressRange(0, std::numeric_limits<int>::max());
        input.progress = [&promise, &progress](const QString& value) { promise.setProgressValueAndText(++progress, value); };
        promise.addResult(perform(input));
    }));
}
void GitOperations::refresh() { if (!d->directory.isEmpty()) { d->cache->request(this, d->directory, true); read("metadata", true); } }
void GitOperations::loadChanges() { read("changes", true); }
void GitOperations::loadHistory() { read("history", true); }
void GitOperations::loadDiff(bool compare) { read(compare ? "compare" : "diff", true); }
void GitOperations::loadUpstreams() { read("targets", true); }
void GitOperations::loadTags() { read("tags", true); }
void GitOperations::clearResult() { if (!d->busy) { d->steps.clear(); d->error.clear(); emit changed(); } }
void GitOperations::synchronize() { write("sync"); }
void GitOperations::fetch() { write("fetch"); }
void GitOperations::pull() { write("pull"); }
void GitOperations::push() { write("push"); }
void GitOperations::commit(const QStringList& ids, const QString& message, bool pushAfter, const QString& tag) { write("commit", {{"ids", ids}, {"message", message}, {"pushAfter", pushAfter}, {"tag", tag}}); }
void GitOperations::createTag(const QString& name, bool pushRemote) { write("create-tag", {{"name", name}, {"send", pushRemote}}); }
void GitOperations::pushTag(const QString& name) { write("push-tag", {{"name", name}}); }
void GitOperations::setUpstream(const QString& ref) { write("upstream", {{"ref", ref}}); }
void GitOperations::cancel() { if (d->writeCancelled) d->writeCancelled->store(true); }
void GitOperations::openRemote()
{
    const QUrl url(d->info.value("webUrl").toString());
    if (url.isValid() && (url.scheme() == "https" || url.scheme() == "http") && !url.host().isEmpty()) QDesktopServices::openUrl(url);
}
QUrl GitOperations::remoteWebUrl(const QString& remote)
{
    if (!standardRemote(remote)) return {};
    QUrl result(remote);
    if (result.scheme() != "http" && result.scheme() != "https") {
        if (result.scheme() == "ssh" || result.scheme() == "git") { result.setScheme("https"); result.setPort(-1); }
        else {
            if (remote.contains("://")) return {};
            static const QRegularExpression scp(QStringLiteral(R"(^(?:[^/@:]+@)?(\[[^\]]+\]|[^/:]+):(.+)$)"));
            const auto match = scp.match(remote);
            if (!match.hasMatch()) return {};
            result = QUrl(); result.setScheme("https"); result.setHost(match.captured(1).startsWith('[') ? match.captured(1).mid(1, match.captured(1).size()-2) : match.captured(1)); result.setPath('/' + match.captured(2));
        }
    }
    result.setUserInfo(QString()); result.setQuery(QString()); result.setFragment(QString());
    auto path = result.path(); if (path.endsWith(".git")) path.chop(4); result.setPath(path);
    return result.host().isEmpty() ? QUrl() : result;
}
