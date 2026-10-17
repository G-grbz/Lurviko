#include "gitstatuscache.h"
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QProcess>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QTimer>
#include <array>

namespace {
void write(const QString &path, const QByteArray &data) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(data), data.size());
}
QByteArray contents(const QString &path) { QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll(); }
QString quote(QString text) { return "'" + text.replace("'", "'\\''") + "'"; }
}
class GitTests : public QObject {
    Q_OBJECT
private slots:
    void repositoriesSeenFromParent() {
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString git = QStandardPaths::findExecutable("git"); QVERIFY(!git.isEmpty());
        QStringList roots;
        for (int i = 0; i < 12; ++i) {
            const QString root = temporary.path() + QString("/project-%1").arg(i);
            QDir().mkpath(root); roots.append(root);
            auto command = [&](const QStringList &args) {
                QProcess process;
                process.start(git, QStringList{"-c", "user.name=Test", "-c", "user.email=test@example.invalid", "-C", root} + args);
                return process.waitForFinished(10000) && process.exitCode() == 0;
            };
            QVERIFY(command({"init", "-q"})); write(root + "/file.txt", "initial");
            QVERIFY(command({"add", "."})); QVERIFY(command({"commit", "-qm", "fixture"}));
            if (i % 2 == 0) write(root + "/file.txt", "changed");
        }
        GitStatusCache cache; QObject owner;
        QSignalSpy updates(&cache, &GitStatusCache::statusChanged);
        int beats = 0; QTimer heartbeat; heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, [&] { ++beats; }); heartbeat.start();
        for (const auto &root : roots) cache.directoryStatus(&owner, root);
        // More sibling repositories than the full-snapshot cache can hold.
        // All must retain their root status without being entered first.
        const auto allReady = [&] {
            for (int i = 0; i < roots.size(); ++i)
                if (cache.directoryStatus(&owner, roots[i]) != (i % 2 == 0 ? "modified" : "clean")) return false;
            return true;
        };
        QTRY_VERIFY_WITH_TIMEOUT(allReady(), 15000);
        QVERIFY(beats > 10);
        const int scans = updates.count(); QTest::qWait(2200);
        QCOMPARE(updates.count(), scans); // Reading badges does not start idle rescans.
        const QString plain = temporary.path() + "/plain";
        QDir().mkpath(plain + "/.git");
        cache.directoryStatus(&owner, plain); QTest::qWait(200);
        QVERIFY(cache.directoryStatus(&owner, plain).isEmpty());
        // Refresh a sibling from the parent and preserve previously evicted summaries.
        write(roots[1] + "/file.txt", "new edit");
        cache.request(&owner, temporary.path(), true);
        QTRY_COMPARE_WITH_TIMEOUT(cache.directoryStatus(&owner, roots[1]), QString("modified"), 10000);
        QCOMPARE(cache.directoryStatus(&owner, roots[0]), QString("modified"));
        // Open directories pin their full snapshot. Waiting siblings must
        // resume when those panes leave, without depending on another edit.
        GitStatusCache pinned; std::array<QObject, 8> panes; QObject parent;
        QSignalSpy pinnedUpdates(&pinned, &GitStatusCache::statusChanged);
        for (int i = 0; i < 8; ++i) pinned.request(&panes[i], roots[i]);
        QTRY_VERIFY_WITH_TIMEOUT(pinnedUpdates.count() >= 8, 15000);
        pinned.directoryStatus(&parent, roots[8]); QTest::qWait(400);
        QCOMPARE(pinned.directoryStatus(&parent, roots[8]), QString("repository"));
        for (auto &pane : panes) pinned.request(&pane, {});
        QTRY_COMPARE_WITH_TIMEOUT(pinned.directoryStatus(&parent, roots[8]), QString("modified"), 10000);
    }
    void porcelain() {
        QByteArray records;
        auto add = [&](const QByteArray &record) { records += record; records += '\0'; };
        add(" M docs/a.txt"); add("A  new.txt"); add("?? fresh/"); add(" D gone/file.txt");
        add("R  renamed/ı new\nline.txt"); add("old/name.txt"); add("!! cache/"); add("UU conflict.txt");
        auto result = GitStatusCache::parsePorcelain(records);
        QVERIFY(result.valid);
        QCOMPARE(result.paths.value("docs/a.txt"), "modified"); QCOMPARE(result.paths.value("docs"), "modified");
        QCOMPARE(result.paths.value("new.txt"), "added"); QCOMPARE(result.paths.value("fresh"), "untracked");
        QCOMPARE(result.paths.value("gone"), "deleted"); QCOMPARE(result.paths.value("old"), "renamed");
        QCOMPARE(result.paths.value(QString::fromUtf8("renamed/ı new\nline.txt")), "renamed");
        QCOMPARE(result.paths.value("cache"), "ignored"); QCOMPARE(result.paths.value("conflict.txt"), "conflict");
        auto ignored = GitStatusCache::parsePorcelain(QByteArray("!! normal/cache/\0", 17));
        QVERIFY(ignored.valid); QVERIFY(!ignored.paths.contains("normal"));
        QVERIFY(!GitStatusCache::parsePorcelain(" M missing-terminator").valid);
        QVERIFY(!GitStatusCache::parsePorcelain(QByteArray("R  one\0", 7)).valid);
        QVERIFY(GitStatusCache::parsePorcelain(QByteArray("?? ../outside\0", 14)).paths.isEmpty());
        QByteArray excessive;
        for (int i = 0; i < 100001; ++i) { excessive += "?? " + QByteArray::number(i); excessive += '\0'; }
        QVERIFY(!GitStatusCache::parsePorcelain(excessive).valid);
    }
    void asyncRepositoriesAndCache() {
        const QString git = QStandardPaths::findExecutable("git");
        QVERIFY2(!git.isEmpty(), "Git is required for this regression test");
        QTemporaryDir temporary; QVERIFY(temporary.isValid());
        const QString root = temporary.path() + "/repo";
        QDir().mkpath(root);
        auto command = [&](const QStringList &args) {
            QProcess process; process.start(git, QStringList{"-c", "core.fsmonitor=false", "-C", root} + args);
            if (!process.waitForFinished(10000)) return false;
            return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        };
        QVERIFY(command({"init", "-q"}));
        QVERIFY(command({"config", "user.email", "test@example.invalid"}));
        QVERIFY(command({"config", "user.name", "Test"}));
        write(root + "/docs/changed.txt", "before"); write(root + "/gone/file.txt", "delete");
        write(root + "/old.txt", "rename"); write(root + "/clean.txt", "clean");
        write(root + "/.gitignore", "cache/\n");
        QVERIFY(command({"add", "."})); QVERIFY(command({"commit", "-qm", "fixture"}));
        write(root + "/docs/changed.txt", "after change"); QFile::remove(root + "/gone/file.txt");
        QVERIFY(command({"mv", "old.txt", "renamed.txt"}));
        write(root + "/added.txt", "stage"); QVERIFY(command({"add", "added.txt"}));
        write(root + "/fresh/new.txt", "new"); write(root + "/cache/thumb.png", "ignored");
        const QString hook = temporary.path() + "/fsmonitor";
        const QString hookMarker = temporary.path() + "/hook-executed";
        write(hook, ("#!/bin/sh\ntouch " + quote(hookMarker) + "\nexit 1\n").toUtf8());
        QVERIFY(QFile::setPermissions(hook, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        QVERIFY(command({"config", "core.fsmonitor", hook}));
        const QString calls = temporary.path() + "/git-calls";
        const QString binary = temporary.path() + "/bin/git";
        write(binary, ("#!/bin/sh\necho status >> " + quote(calls) + "\nexec " + quote(git) + " \"$@\"\n").toUtf8());
        QVERIFY(QFile::setPermissions(binary, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        const QByteArray oldPath = qgetenv("PATH"); qputenv("PATH", QFileInfo(binary).absolutePath().toUtf8() + ':' + oldPath);
        GitStatusCache cache;
        qputenv("PATH", oldPath);
        QObject first, second, outside;
        QSignalSpy ready(&cache, &GitStatusCache::directoryReady), updates(&cache, &GitStatusCache::statusChanged);
        int heartbeats = 0; QTimer heartbeat; heartbeat.setInterval(5);
        connect(&heartbeat, &QTimer::timeout, [&] { ++heartbeats; }); heartbeat.start();
        cache.request(&first, root); cache.request(&second, root + "/docs");
        QTRY_VERIFY_WITH_TIMEOUT(updates.count() >= 1, 10000);
        QCOMPARE(cache.status(root, root + "/docs/changed.txt"), "modified");
        QCOMPARE(cache.status(root, root + "/docs"), "modified");
        QCOMPARE(cache.status(root, root + "/gone"), "deleted");
        QCOMPARE(cache.status(root, root + "/renamed.txt"), "renamed");
        QCOMPARE(cache.status(root, root + "/added.txt"), "added");
        QCOMPARE(cache.status(root, root + "/fresh/new.txt"), "untracked");
        QCOMPARE(cache.status(root, root + "/cache/thumb.png"), "ignored");
        QVERIFY(cache.status(root, root + "-other/added.txt").isEmpty());
        QVERIFY(!QFileInfo::exists(hookMarker)); QVERIFY(heartbeats > 10);
        // Both visible directories share one scan; revisits reuse its snapshot.
        QCOMPARE(contents(calls).count('\n'), 1);
        cache.request(&first, root); cache.request(&second, root + "/docs");
        const QString nonrepo = temporary.path() + "/plain"; QDir().mkpath(nonrepo);
        cache.request(&outside, nonrepo);
        QTRY_VERIFY_WITH_TIMEOUT(ready.count() >= 5, 5000);
        QTest::qWait(2200);
        QCOMPARE(contents(calls).count('\n'), 1); // No idle polling or Git calls outside repositories.
        // Index changes are observed even if the browser directory did not change.
        QVERIFY(command({"add", "docs/changed.txt"}));
        const int before = updates.count();
        QTRY_VERIFY_WITH_TIMEOUT(updates.count() > before, 7000);
        // A watched file's metadata change requests a shared debounced refresh.
        write(root + "/clean.txt", "changed now"); cache.request(&first, root, true);
        QTRY_COMPARE_WITH_TIMEOUT(cache.status(root, root + "/clean.txt"), QString("modified"), 7000);
        QVERIFY(!QFileInfo::exists(hookMarker));
        // Linked worktrees use a .git file rather than a directory.
        const QString worktree = temporary.path() + "/worktree";
        QVERIFY(command({"worktree", "add", "--detach", worktree, "HEAD"}));
        write(worktree + "/clean.txt", "worktree change");
        QObject worktreeOwner; cache.request(&worktreeOwner, worktree);
        QTRY_COMPARE_WITH_TIMEOUT(cache.status(worktree, worktree + "/clean.txt"), QString("modified"), 10000);
        QVERIFY(heartbeats > 100);
    }
};
QTEST_GUILESS_MAIN(GitTests)
#include "git_tests.moc"
