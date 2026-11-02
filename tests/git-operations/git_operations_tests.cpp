#include "gitoperations.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <signal.h>

namespace {
QString gitProgram;
QByteArray command(const QString& root, const QStringList& args, bool ok = true)
{
    QProcess p; p.start(gitProgram, QStringList{"-c", "core.fsmonitor=false", "-C", root} + args);
    if (!p.waitForFinished(15000) || (ok && p.exitCode() != 0))
        qFatal("Fixture Git command failed: %s", p.readAllStandardError().constData());
    return p.readAllStandardOutput();
}
void writeFile(const QString& path, const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size()) qFatal("Cannot create Git fixture");
}
QByteArray readFile(const QString& path) { QFile f(path); return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray(); }
void identity(const QString& root) { command(root, {"config", "user.name", "Lurviko Test"}); command(root, {"config", "user.email", "test@example.invalid"}); }
QString head(const QString& root) { return QString::fromLatin1(command(root, {"rev-parse", "HEAD"})).trimmed(); }
QString rowId(const GitOperations& api, const QString& path)
{
    for (const auto& item : api.changes()) if (item.toMap().value("path").toString() == path) return item.toMap().value("id").toString();
    return {};
}
struct Fixture {
    QTemporaryDir temp;
    QString local, remote, peer;
    Fixture() {
        local = temp.path() + "/local"; remote = temp.path() + "/remote.git"; peer = temp.path() + "/peer";
        QDir().mkpath(local); command(local, {"init", "-q", "-b", "main"}); identity(local);
        writeFile(local + "/one.txt", "one\n"); writeFile(local + "/two.txt", "two\n");
        command(local, {"add", "."}); command(local, {"commit", "-qm", "initial"});
        command(temp.path(), {"init", "--bare", "-q", remote});
        command(remote, {"symbolic-ref", "HEAD", "refs/heads/main"});
        command(local, {"remote", "add", "origin", remote}); command(local, {"push", "-qu", "origin", "main"});
        command(temp.path(), {"clone", "-q", remote, peer}); identity(peer);
    }
    void localCommit(const QString& filename, const QByteArray& bytes) {
        writeFile(local + '/' + filename, bytes); command(local, {"add", "--", filename}); command(local, {"commit", "-qm", "local"});
    }
    void remoteCommit(const QString& filename, const QByteArray& bytes) {
        writeFile(peer + '/' + filename, bytes); command(peer, {"add", "--", filename}); command(peer, {"commit", "-qm", "remote"}); command(peer, {"push", "-q"});
    }
};
}
class GitOperationsTests : public QObject {
    Q_OBJECT
    QTemporaryDir helpers;
    QString wrapper;
private slots:
    void initTestCase() {
        gitProgram = QStandardPaths::findExecutable("git"); QVERIFY(!gitProgram.isEmpty());
        wrapper = helpers.path() + "/git-wrapper";
        writeFile(wrapper, R"py(#!/usr/bin/python3
import os,sys,time,subprocess
args=sys.argv[1:]
mode=os.environ.get('LURVIKO_GIT_TEST_MODE','')
real=os.environ['LURVIKO_GIT_TEST_REAL']
if os.environ.get('LURVIKO_GIT_TEST_LOG'):
    import json
    with open(os.environ['LURVIKO_GIT_TEST_LOG'],'a') as log:log.write(json.dumps(args)+'\n')
if 'status' in args and mode=='oversized':
    sys.stdout.buffer.write(b'x'*(9*1024*1024));sys.exit(0)
if 'fetch' in args:
    if mode=='child':
        child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])
        with open(os.environ['LURVIKO_GIT_TEST_CHILD'],'w') as marker:marker.write(str(child.pid))
        time.sleep(30)
    if mode=='slow-fetch':time.sleep(0.7)
    if mode=='auth':
        print("fatal: Authentication failed for 'https://secret-token@git.example.invalid/repo.git'",file=sys.stderr);sys.exit(128)
    if mode=='switch':
        directory=args[args.index('-C')+1]
        subprocess.run([real,'-C',directory,'switch','-q','other'],check=True)
    if mode in ('redirect','redirect-push'):
        directory=args[args.index('-C')+1]
        subprocess.run([real,'-C',directory,'remote','set-url']+(['--push'] if mode=='redirect-push' else [])+['origin',os.environ['LURVIKO_GIT_TEST_REDIRECT']],check=True)
if 'reset' in args and mode=='fail-reset':
    print('index lock failure',file=sys.stderr);sys.exit(128)
os.execv(real,[real]+args)
)py");
        QVERIFY(QFile::setPermissions(wrapper, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("LURVIKO_GIT_TEST_REAL", gitProgram.toUtf8());
    }
    void init() { qunsetenv("LURVIKO_GIT_TEST_MODE"); qunsetenv("LURVIKO_GIT_TEST_LOG"); }
    void selectedCommitPreservesOtherStagingAndWorktree() {
        Fixture f;
        writeFile(f.local + "/one.txt", "selected current\n");
        writeFile(f.local + "/two.txt", "staged other\n"); command(f.local, {"add", "two.txt"});
        writeFile(f.local + "/two.txt", "unstaged other remains\n");
        const auto staged = command(f.local, {"show", ":two.txt"});
        const auto remoteBefore = head(f.remote);
        GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        const auto id = rowId(api, "one.txt"); QVERIFY(!id.isEmpty());
        QSignalSpy result(&api, &GitOperations::operationFinished);
        api.commit({id}, "Selected only\n\nBody with Turkish: ı ş ü");
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(command(f.local, {"show", "HEAD:one.txt"}), QByteArray("selected current\n"));
        QCOMPARE(command(f.local, {"show", "HEAD:two.txt"}), QByteArray("two\n"));
        QCOMPARE(command(f.local, {"show", ":two.txt"}), staged);
        QCOMPARE(readFile(f.local + "/two.txt"), QByteArray("unstaged other remains\n"));
        QCOMPARE(head(f.remote), remoteBefore);
        QCOMPARE(head(f.local), api.lastCommit());
    }
    void renamedDeletedUntrackedAndRecreatedSource() {
        Fixture f;
        command(f.local, {"mv", "one.txt", "renamed.txt"});
        writeFile(f.local + "/one.txt", "unselected recreated source\n");
        QVERIFY(QFile::remove(f.local + "/two.txt"));
        writeFile(f.local + "/new file\nı.txt", "new\n");
        GitOperations api; api.setContext(f.local); QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        QStringList ids{rowId(api, "renamed.txt"), rowId(api, "two.txt"), rowId(api, "new file\nı.txt")};
        for (const auto& id : ids) QVERIFY(!id.isEmpty());
        QSignalSpy result(&api, &GitOperations::operationFinished); api.commit(ids, "Rename/delete/add selected files");
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(readFile(f.local + "/one.txt"), QByteArray("unselected recreated source\n"));
        const auto tree = command(f.local, {"ls-tree", "--name-only", "HEAD"});
        QVERIFY(!tree.contains("one.txt")); QVERIFY(!tree.contains("two.txt")); QVERIFY(tree.contains("renamed.txt"));
        QVERIFY(command(f.local, {"status", "--porcelain"}).contains("?? one.txt"));
    }
    void unbornCommitAndCommitHooksDoNotIncludeOtherFiles() {
        QTemporaryDir temp; command(temp.path(), {"init", "-q", "-b", "main"}); identity(temp.path());
        writeFile(temp.path() + "/chosen", "chosen"); writeFile(temp.path() + "/other", "other");
        command(temp.path(), {"add", "other"});
        const auto hook = temp.path() + "/.git/hooks/pre-commit";
        writeFile(hook, "#!/bin/sh\necho hook > hook-ran\ngit add .\n");
        QFile::setPermissions(hook, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        GitOperations api; api.setContext(temp.path()); QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QVERIFY(api.info().value("unborn").toBool());
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.commit({rowId(api, "chosen")}, "Initial selected commit");
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(command(temp.path(), {"ls-tree", "--name-only", "HEAD"}).trimmed(), QByteArray("chosen"));
        QCOMPARE(command(temp.path(), {"show", ":other"}), QByteArray("other"));
        QVERIFY(!QFile::exists(temp.path() + "/hook-ran"));
    }
    void syncBehindFastForwardsAndAheadPushes() {
        Fixture f; f.remoteCommit("one.txt", "remote update\n");
        GitOperations api; api.setContext(f.local); QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.synchronize();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(head(f.local), head(f.peer)); QCOMPARE(readFile(f.local + "/one.txt"), QByteArray("remote update\n"));
        QTRY_VERIFY_WITH_TIMEOUT(!api.loading() && api.info().value("countsKnown").toBool(), 10000);
        QCOMPARE(api.info().value("ahead").toInt(), 0); QCOMPARE(api.info().value("behind").toInt(), 0);
        f.localCommit("two.txt", "local update\n");
        api.synchronize(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(head(f.remote), head(f.local));
    }
    void fetchUpdatesCountsWithoutTouchingWorktree() {
        Fixture f; GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QCOMPARE(api.info().value("behind").toInt(), 0);
        const auto before = head(f.local);
        f.remoteCommit("one.txt", "remote only\n");
        // No background network request claims knowledge of the new commit.
        api.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        QCOMPARE(api.info().value("behind").toInt(), 0);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.fetch();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY(result.last().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!api.loading() && api.info().value("behind").toInt() == 1, 10000);
        QCOMPARE(api.info().value("ahead").toInt(), 0);
        QCOMPARE(head(f.local), before); QCOMPARE(readFile(f.local + "/one.txt"), QByteArray("one\n"));
    }
    void equalDoesNotCreateCommitsAndPullDoesNotPush() {
        Fixture f; GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        const auto before = head(f.local); QSignalSpy result(&api, &GitOperations::operationFinished);
        api.synchronize(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY(result.last().at(1).toBool()); QCOMPARE(head(f.local), before); QCOMPARE(head(f.remote), before);
        f.localCommit("two.txt", "local only\n"); const auto remote = head(f.remote);
        api.pull(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 15000);
        QVERIFY(result.last().at(1).toBool()); QCOMPARE(head(f.remote), remote);
    }
    void divergentAndDirtyHistoriesAreNotMerged() {
        Fixture f; f.localCommit("one.txt", "local\n"); f.remoteCommit("two.txt", "remote\n");
        const auto before = head(f.local); GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.synchronize();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(!result.last().at(1).toBool()); QCOMPARE(head(f.local), before);
        QTRY_VERIFY_WITH_TIMEOUT(!api.loading() && api.info().value("countsKnown").toBool(), 10000);
        QCOMPARE(api.info().value("ahead").toInt(), 1); QCOMPARE(api.info().value("behind").toInt(), 1);
        Fixture dirty; dirty.remoteCommit("two.txt", "remote\n"); writeFile(dirty.local + "/one.txt", "dirty\n");
        const auto dirtyBefore = head(dirty.local); api.setContext(dirty.local); QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        api.synchronize(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 15000);
        QVERIFY(!result.last().at(1).toBool()); QCOMPARE(head(dirty.local), dirtyBefore);
        QCOMPARE(readFile(dirty.local + "/one.txt"), QByteArray("dirty\n"));
    }
    void missingUpstreamDetachedAndExplicitConfiguration() {
        Fixture f; command(f.local, {"branch", "--unset-upstream"});
        GitOperations api; api.setContext(f.local); QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QVERIFY(api.info().value("upstream").toString().isEmpty()); QVERIFY(!api.info().value("countsKnown").toBool());
        QSignalSpy result(&api, &GitOperations::operationFinished); api.synchronize();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000); QVERIFY(!result.last().at(1).toBool());
        api.loadUpstreams(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000); QVERIFY(!api.upstreamTargets().isEmpty());
        api.setUpstream("refs/remotes/origin/main"); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 10000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(command(f.local, {"rev-parse", "--symbolic-full-name", "@{upstream}"}).trimmed(), QByteArray("refs/remotes/origin/main"));
        command(f.local, {"checkout", "--detach", "-q"}); api.refresh();
        QTRY_VERIFY_WITH_TIMEOUT(!api.loading() && api.info().value("detached").toBool(), 10000);
        QCOMPARE(api.info().value("branch").toString(), head(f.local).left(8));
        api.synchronize(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 3, 10000); QVERIFY(!result.last().at(1).toBool());
    }
    void rejectedPushRefreshesCountsAndKeepsLocalCommit() {
        Fixture f; f.localCommit("one.txt", "left\n"); f.remoteCommit("two.txt", "right\n");
        const auto before = head(f.local); GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.push();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(!result.last().at(1).toBool()); QCOMPARE(head(f.local), before);
        QTRY_VERIFY_WITH_TIMEOUT(!api.loading() && api.info().value("countsKnown").toBool(), 10000);
        QCOMPARE(api.info().value("ahead").toInt(), 1); QCOMPARE(api.info().value("behind").toInt(), 1);
    }
    void commitSuccessPushFailureAndIndexFailurePreserveCommit() {
        Fixture f;
        writeFile(f.remote + "/hooks/pre-receive", "#!/bin/sh\necho rejected-by-server >&2\nexit 1\n");
        QFile::setPermissions(f.remote + "/hooks/pre-receive", QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        const auto remote = head(f.remote); writeFile(f.local + "/one.txt", "new local\n");
        GitOperations api; api.setContext(f.local); QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.commit({rowId(api, "one.txt")}, "Local survives rejection", true);
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000); QVERIFY(!result.last().at(1).toBool()); QVERIFY(!api.lastCommit().isEmpty());
        QCOMPARE(head(f.local), api.lastCommit()); QCOMPARE(head(f.remote), remote);
        QVERIFY(result.last().at(2).toString().contains("rejected-by-server"));
        Fixture indexFailure; writeFile(indexFailure.local + "/one.txt", "commit despite index refresh failure\n");
        GitOperations failed(wrapper, nullptr); failed.setContext(indexFailure.local);
        QTRY_VERIFY_WITH_TIMEOUT(failed.available() && !failed.loading(), 10000);
        failed.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!failed.loading(), 10000);
        qputenv("LURVIKO_GIT_TEST_MODE", "fail-reset"); QSignalSpy partial(&failed, &GitOperations::operationFinished);
        failed.commit({rowId(failed, "one.txt")}, "Keep actual commit"); QTRY_COMPARE_WITH_TIMEOUT(partial.size(), 1, 15000);
        QVERIFY(!partial.last().at(1).toBool()); QVERIFY(!failed.lastCommit().isEmpty()); QCOMPARE(head(indexFailure.local), failed.lastCommit());
    }
    void authenticationErrorsAreRedactedAndBranchChangesStopSync() {
        Fixture f; GitOperations api(wrapper, nullptr); api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        qputenv("LURVIKO_GIT_TEST_MODE", "auth"); QSignalSpy result(&api, &GitOperations::operationFinished); api.fetch();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 10000); QVERIFY(!result.last().at(1).toBool());
        QVERIFY(!result.last().at(2).toString().contains("secret-token")); QVERIFY(result.last().at(2).toString().contains("Authentication failed"));
        command(f.local, {"branch", "other"}); f.remoteCommit("one.txt", "remote\n"); const auto before = head(f.local);
        qputenv("LURVIKO_GIT_TEST_MODE", "switch"); api.synchronize();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 15000); QVERIFY(!result.last().at(1).toBool()); QCOMPARE(head(f.local), before);
        QCOMPARE(command(f.local, {"branch", "--show-current"}).trimmed(), QByteArray("other"));
    }
    void worktreesShareWriterLockAndCancellationKeepsUiResponsive() {
        Fixture f; const auto worktree = f.temp.path() + "/worktree";
        command(f.local, {"worktree", "add", "-q", "-b", "feature", worktree}); command(worktree, {"branch", "--set-upstream-to=origin/main"});
        GitOperations first(wrapper, nullptr), second(wrapper, nullptr);
        first.setContext(f.local); second.setContext(worktree);
        QTRY_VERIFY_WITH_TIMEOUT(first.available() && second.available() && !first.loading() && !second.loading(), 10000);
        qputenv("LURVIKO_GIT_TEST_MODE", "slow-fetch");
        QSignalSpy one(&first, &GitOperations::operationFinished), two(&second, &GitOperations::operationFinished);
        int beats = 0; QTimer timer; timer.setInterval(5); connect(&timer, &QTimer::timeout, [&] { ++beats; }); timer.start();
        first.fetch(); QTest::qWait(200); second.fetch();
        QTRY_COMPARE_WITH_TIMEOUT(two.size(), 1, 10000); QVERIFY(!two.last().at(1).toBool());
        first.cancel(); QTRY_COMPARE_WITH_TIMEOUT(one.size(), 1, 10000); QVERIFY(!one.last().at(1).toBool()); QVERIFY(beats > 10);
        qunsetenv("LURVIKO_GIT_TEST_MODE"); second.fetch(); QTRY_COMPARE_WITH_TIMEOUT(two.size(), 2, 10000);
        QVERIFY2(two.last().at(1).toBool(), qPrintable(two.last().at(2).toString()));
    }
    void nestedRepositoriesHistoryDiffAndSafeWebUrls() {
        Fixture f; const auto nested = f.local + "/nested";
        QDir().mkpath(nested); command(nested, {"init", "-q", "-b", "inside"}); identity(nested);
        writeFile(nested + "/file", "nested\n"); command(nested, {"add", "."}); command(nested, {"commit", "-qm", "Nested commit"});
        GitOperations api; api.setContext(nested + "/file", false);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000); QCOMPARE(api.info().value("root").toString(), nested);
        api.loadHistory(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000); QCOMPARE(api.history().size(), 1);
        writeFile(nested + "/file", "changed\n"); api.loadDiff(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000); QVERIFY(api.detailText().contains("changed"));
        api.setContext(f.temp.path()); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000); QVERIFY(!api.available());
        QCOMPARE(GitOperations::remoteWebUrl("git@gitlab.example:group/repo.git").toString(), QString("https://gitlab.example/group/repo"));
        QCOMPARE(GitOperations::remoteWebUrl("https://secret@gitea.example:3000/u/r.git?token=secret").toString(), QString("https://gitea.example:3000/u/r"));
        QVERIFY(GitOperations::remoteWebUrl("file:///tmp/repo.git").isEmpty());
        QVERIFY(GitOperations::remoteWebUrl("ext::sh arbitrary").isEmpty());
        QCOMPARE(GitOperations::remoteWebUrl("git@[2001:db8::1]:team/repo.git").toString(), QString("https://[2001:db8::1]/team/repo"));
    }
    void signingFailureDoesNotAdvanceHeadAndPathsStayLiteral() {
        Fixture f;
        const auto before = head(f.local);
        writeFile(f.local + "/:(glob)*", "literal path\n");
        writeFile(f.local + "/other-new", "must remain untracked\n");
        command(f.local, {"config", "commit.gpgsign", "invalid-boolean"});
        GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        const auto selected = rowId(api, ":(glob)*"); QVERIFY(!selected.isEmpty());
        QSignalSpy result(&api, &GitOperations::operationFinished);
        api.commit({selected}, "Do not silently skip signing");
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY(!result.last().at(1).toBool()); QCOMPARE(head(f.local), before);
        command(f.local, {"config", "commit.gpgsign", "false"});
        api.commit({selected}, "Literal path");
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 2, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
        QCOMPARE(command(f.local, {"show", "HEAD::(glob)*"}), QByteArray("literal path\n"));
        QVERIFY(command(f.local, {"status", "--porcelain"}).contains("?? other-new"));
    }
    void idleAndNonRepositoryContextsDoNotPollGit() {
        Fixture f; const auto trace = helpers.path() + "/idle-trace";
        QFile::remove(trace); qputenv("LURVIKO_GIT_TEST_LOG", trace.toUtf8());
        GitOperations api(wrapper, nullptr); api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QTest::qWait(700); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        const auto settled = readFile(trace); QVERIFY(!settled.isEmpty());
        QTest::qWait(1100); QCOMPARE(readFile(trace), settled);
        QVERIFY(!settled.contains("\"fetch\""));
        api.setContext(f.temp.path()); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        QVERIFY(!api.available()); const auto outside = readFile(trace);
        QTest::qWait(400); QCOMPARE(readFile(trace), outside);
    }
    void largeChangeListsStayBoundedAndDoNotBlockTheUi() {
        Fixture f;
        for (int i = 0; i < 10020; ++i) writeFile(f.local + "/new-" + QString::number(i), "x");
        GitOperations api; api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 15000);
        int beats = 0; QTimer timer; timer.setInterval(1); connect(&timer, &QTimer::timeout, [&] { ++beats; }); timer.start();
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 15000);
        QCOMPARE(api.changes().size(), 10000); QVERIFY(api.info().value("changesTruncated").toBool());
        QVERIFY(beats > 0);
        api.refresh(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 15000);
        QVERIFY(api.info().value("changesTruncated").toBool());
    }
    void destroyingAnOperatingPaneReleasesItsWriterLock() {
        Fixture f; qputenv("LURVIKO_GIT_TEST_MODE", "slow-fetch");
        auto* api = new GitOperations(wrapper, nullptr); api->setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api->available() && !api->loading(), 10000);
        api->fetch(); QTest::qWait(100); delete api;
        QTest::qWait(1100);
        qunsetenv("LURVIKO_GIT_TEST_MODE");
        GitOperations next; next.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(next.available() && !next.loading(), 10000);
        QSignalSpy result(&next, &GitOperations::operationFinished); next.fetch();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY2(result.last().at(1).toBool(), qPrintable(result.last().at(2).toString()));
    }
    void cancellationStopsCredentialChildProcesses() {
        Fixture f; const auto marker = helpers.path() + "/child-pid";
        QFile::remove(marker); qputenv("LURVIKO_GIT_TEST_CHILD", marker.toUtf8());
        qputenv("LURVIKO_GIT_TEST_MODE", "child");
        GitOperations api(wrapper, nullptr); api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.fetch();
        QTRY_VERIFY_WITH_TIMEOUT(!readFile(marker).isEmpty(), 10000);
        const auto child = readFile(marker).toLongLong(); QVERIFY(child > 1);
        api.cancel(); QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 5000);
        QVERIFY(!result.last().at(1).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(::kill(child, 0) < 0 || readFile("/proc/" + QString::number(child) + "/stat").contains(") Z "), 5000);
    }
    void oversizedDetailsFailWithAVisibleLimitError() {
        Fixture f; GitOperations api(wrapper, nullptr); api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        qputenv("LURVIKO_GIT_TEST_MODE", "oversized");
        api.loadChanges(); QTRY_VERIFY_WITH_TIMEOUT(!api.loading(), 10000);
        QVERIFY(api.error().contains("limit") || api.error().contains(QString::fromUtf8("sınır")));
        QVERIFY(api.available()); QVERIFY(api.changes().isEmpty());
    }
    void changedRemoteAddressStopsSyncBeforePush_data() {
        QTest::addColumn<QByteArray>("mode");
        QTest::newRow("fetch-url") << QByteArray("redirect");
        QTest::newRow("push-url") << QByteArray("redirect-push");
    }
    void changedRemoteAddressStopsSyncBeforePush() {
        QFETCH(QByteArray, mode);
        Fixture f;
        const auto other = f.temp.path() + "/other.git";
        command(f.temp.path(), {"clone", "-q", "--bare", f.remote, other});
        const auto before = head(other); f.localCommit("one.txt", "keep local\n");
        GitOperations api(wrapper, nullptr); api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        qputenv("LURVIKO_GIT_TEST_MODE", mode); qputenv("LURVIKO_GIT_TEST_REDIRECT", other.toUtf8());
        QSignalSpy result(&api, &GitOperations::operationFinished); api.synchronize();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY(!result.last().at(1).toBool()); QCOMPARE(head(other), before); QCOMPARE(head(f.remote), before);
    }
    void unsupportedPushUrlDoesNotStartPush() {
        Fixture f; f.localCommit("one.txt", "local\n");
        command(f.local, {"remote", "set-url", "--push", "origin", "custom-helper::payload"});
        const auto trace = helpers.path() + "/push-trace";
        QFile::remove(trace); qputenv("LURVIKO_GIT_TEST_LOG", trace.toUtf8());
        GitOperations api(wrapper, nullptr); api.setContext(f.local);
        QTRY_VERIFY_WITH_TIMEOUT(api.available() && !api.loading(), 10000);
        QSignalSpy result(&api, &GitOperations::operationFinished); api.push();
        QTRY_COMPARE_WITH_TIMEOUT(result.size(), 1, 15000);
        QVERIFY(!result.last().at(1).toBool());
        const auto lines = readFile(trace).split('\n');
        for (const auto& line : lines) {
            const auto args = QJsonDocument::fromJson(line).array();
            QVERIFY(!args.contains("push"));
        }
    }
};
QTEST_GUILESS_MAIN(GitOperationsTests)
#include "git_operations_tests.moc"
