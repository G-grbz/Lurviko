#include "sambasharemanager.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <unistd.h>
#include <sys/stat.h>
#include <QFileInfo>

class SambaShareTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString program, folder, statePath;
    void state(QJsonObject data = {}) {
        QFile file(statePath); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QJsonDocument(data).toJson());
    }
    QJsonObject readState() {
        QFile file(statePath); if (!file.open(QIODevice::ReadOnly)) return {};
        return QJsonDocument::fromJson(file.readAll()).object();
    }
    QJsonObject record(const QString& path, const QString& acl = "Everyone:R") {
        return {{"path", path}, {"comment", "Existing"}, {"usershare_acl", acl}, {"guest_ok", "n"}};
    }
private slots:
    void initTestCase() {
        QVERIFY(root.isValid());
        folder = root.path() + "/Klasör $(touch injected)";
        QVERIFY(QDir().mkpath(folder));
        statePath = root.path() + "/state.json";
        program = root.path() + "/fake-net";
        QFile file(program); QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"py(#!/usr/bin/python3
import json, os, sys, time
path=os.environ['LURVIKO_SAMBA_TEST_STATE']
state=json.load(open(path))
args=sys.argv[1:]
assert '--' in args
command=args[args.index('--')+1:]
assert command[0]=='usershare'
if command[1]=='info':
    time.sleep(state.get('delay',0))
    if state.get('fail_read'):
        print('usershares disabled',file=sys.stderr);sys.exit(1)
    for name,share in state.get('shares',{}).items():
        print('['+name+']')
        for key,value in share.items():print(key+'='+value)
elif command[1] in ('add','delete'):
    if state.get('fail_write'):
        print('access denied',file=sys.stderr);sys.exit(1)
    state['last_args']=args
    shares=state.setdefault('shares',{})
    if command[1]=='add':
        assert len(command)==7
        assert command[6] in ('guest_ok=n','guest_ok=y')
        shares[command[2]]={'path':command[3],'comment':command[4],'usershare_acl':command[5]+',','guest_ok':command[6][-1]}
    else:shares.pop(command[2],None)
    json.dump(state,open(path,'w'))
else:sys.exit(2)
)py");
        file.close();
        QVERIFY(file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));
        qputenv("LURVIKO_SAMBA_TEST_STATE", statePath.toUtf8());
    }
    void init() { state(); }
    void lifecycleAndLiteralArguments() {
        SambaShareManager manager(program, nullptr);
        QSignalSpy finished(&manager, &SambaShareManager::operationFinished);
        manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        QVERIFY(!manager.shared()); QVERIFY(manager.editable());
        manager.save("Belgeler", "--configfile=/tmp/should-not-be-an-option", true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(0).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(manager.shared()); QVERIFY(manager.readOnly());
        QCOMPARE(manager.comment(), QString("--configfile=/tmp/should-not-be-an-option"));
        const auto args = readState().value("last_args").toArray();
        QCOMPARE(args.first().toString(), QString("--"));
        QCOMPARE(args.at(4).toString(), folder);
        QVERIFY(!QFile::exists(root.path() + "/injected"));
        manager.save("Belgeler", "Writable", false);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(!manager.readOnly()); QVERIFY(manager.address().startsWith("smb://"));
        manager.remove();
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 3, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(!manager.shared());
    }
    void collisionAndCustomPermissionsArePreserved() {
        const QString other = root.path() + "/other"; QVERIFY(QDir().mkpath(other));
        state({{"shares", QJsonObject{{"Existing", record(other)}, {"Custom", record(folder, "alice:R,bob:F")}}}});
        SambaShareManager manager(program, nullptr); manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        // The access table edits custom rules; the simple Everyone form
        // still refuses to replace them.
        QVERIFY(manager.shared()); QVERIFY(manager.editable());
        manager.save("Custom", "Do not overwrite custom ACL", false);
        QVERIFY(!manager.error().isEmpty());
        QCOMPARE(readState().value("shares").toObject().value("Custom").toObject().value("usershare_acl").toString(), QString("alice:R,bob:F"));
        manager.remove(); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(readState().value("shares").toObject().contains("Existing"));
        manager.save("Existing", "Collision", false);
        QVERIFY(!manager.error().isEmpty());
        QCOMPARE(readState().value("shares").toObject().value("Existing").toObject().value("path").toString(), other);
    }
    void listsExistingSharesWithoutChangingDefinitions() {
        const auto shares = QJsonObject{{"Docs #1", record(folder)}, {"Pictures", record(root.path() + "/other")}};
        state({{"shares", shares}});
        SambaShareManager manager(program, nullptr);
        manager.listShares(); QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        QVERIFY(!manager.shared()); QVERIFY(!manager.editable()); QCOMPARE(manager.localShares().size(), 2);
        for (const auto& item : manager.localShares()) {
            const auto entry = item.toMap(); const QUrl url(entry.value("url").toString());
            QCOMPARE(url.scheme(), QString("smb")); QCOMPARE(url.host(), QString("localhost"));
            QCOMPARE(url.path(), '/' + entry.value("name").toString() + '/');
            QVERIFY(url.query().isEmpty()); QVERIFY(url.fragment().isEmpty());
        }
        QCOMPARE(readState().value("shares").toObject(), shares); QVERIFY(!readState().contains("last_args"));
        manager.clear(); QVERIFY(manager.localShares().isEmpty());
    }
    void invalidNamesFilesLinksAndRemoteLocations() {
        SambaShareManager manager(program, nullptr); manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        for (const auto& name : QStringList{"--configfile", "../share", "bad;name", "global", "trailing."}) {
            manager.save(name, "", true); QVERIFY(!manager.error().isEmpty()); QVERIFY(!manager.busy());
        }
        QVERIFY(!readState().contains("last_args"));
        manager.inspect("sftp://example.test/folder"); QVERIFY(!manager.available());
        const auto filePath = root.path() + "/file";
        { QFile file(filePath); QVERIFY(file.open(QIODevice::WriteOnly)); }
        manager.inspect(filePath); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000); QVERIFY(!manager.available());
        const auto link = root.path() + "/link";
        QCOMPARE(::symlink(folder.toUtf8().constData(), link.toUtf8().constData()), 0);
        manager.inspect(link); QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000); QVERIFY(!manager.available());
    }
    QString script(const QString& name, const QByteArray& body) {
        const QString path = root.path() + '/' + name;
        QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
        file.write(body); file.close();
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        return path;
    }
    void accessTableRoundTrip() {
        state({{"shares", QJsonObject{{"Team", record(folder, "Everyone:R,alice:F,CORP\\bob:D")}}}});
        SambaShareManager manager(program, nullptr);
        manager.setLocalUsersForTest({"alice", "carol"});
        manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        QHash<QString, QString> access;
        for (const auto& value : manager.aclEntries()) {
            const auto entry = value.toMap();
            access.insert(entry.value("name").toString(), entry.value("access").toString());
        }
        QCOMPARE(access.value("Everyone"), QString("R"));
        QCOMPARE(access.value("alice"), QString("F"));
        QCOMPARE(access.value("carol"), QString());
        QCOMPARE(access.value("CORP\\bob"), QString("D"));

        QSignalSpy finished(&manager, &SambaShareManager::operationFinished);
        const QVariantList entries{
            QVariantMap{{"name", "Everyone"}, {"access", ""}},
            QVariantMap{{"name", "alice"}, {"access", "R"}},
            QVariantMap{{"name", "carol"}, {"access", "F"}},
            QVariantMap{{"name", "CORP\\bob"}, {"access", "D"}}};
        manager.saveShare("Team", "Shared", entries, false);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(0).toBool());
        const auto args = readState().value("last_args").toArray();
        QCOMPARE(args.at(6).toString(), QString("alice:R,carol:F,CORP\\bob:D"));
        QCOMPARE(args.at(7).toString(), QString("guest_ok=n"));
    }
    void accessTableRejectsInjectionAndEmptyRules() {
        SambaShareManager manager(program, nullptr);
        manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        for (const auto& name : QStringList{"alice:F", "a,b", "-rf", "x\ny", ""}) {
            manager.saveShare("Docs", "", {QVariantMap{{"name", name}, {"access", "F"}}}, false);
            QVERIFY(!manager.error().isEmpty());
        }
        manager.saveShare("Docs", "", {QVariantMap{{"name", "Everyone"}, {"access", "D"}}}, false);
        QVERIFY(!manager.error().isEmpty());
        // Guests need smb.conf permission (no testparm here: unknown = no).
        manager.saveShare("Docs", "", {QVariantMap{{"name", "Everyone"}, {"access", "R"}}}, true);
        QVERIFY(!manager.error().isEmpty());
        QVERIFY(!readState().contains("last_args"));
    }
    void passwordTravelsOnStdinOnly() {
        const QString log = root.path() + "/pkexec.json";
        const auto pkexec = script("fake-pkexec", R"py(#!/usr/bin/python3
import json, os, sys
json.dump({'argv': sys.argv[1:], 'stdin': sys.stdin.read()}, open(os.environ['LURVIKO_PKEXEC_LOG'], 'w'))
sys.exit(int(os.environ.get('LURVIKO_PKEXEC_EXIT', '0')))
)py");
        qputenv("LURVIKO_PKEXEC_LOG", log.toUtf8());
        SambaShareManager manager(program, nullptr);
        manager.setHelperPrograms(pkexec, "/usr/bin/smbpasswd", {}, {}, {});
        QVERIFY(manager.passwordChangeAvailable());
        QSignalSpy finished(&manager, &SambaShareManager::passwordFinished);
        const QString secret = "s3cr3t pass";
        manager.changePassword(secret);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(0).toBool());
        QFile file(log); QVERIFY(file.open(QIODevice::ReadOnly));
        const auto recorded = QJsonDocument::fromJson(file.readAll()).object();
        const auto argv = recorded.value("argv").toArray();
        QCOMPARE(argv.at(0).toString(), QString("/usr/bin/smbpasswd"));
        QCOMPARE(argv.at(1).toString(), QString("-a"));
        QCOMPARE(argv.at(2).toString(), QString("-s"));
        QCOMPARE(argv.at(3).toString(), manager.userName());
        for (const auto& arg : argv) QVERIFY(!arg.toString().contains(secret));
        QCOMPARE(recorded.value("stdin").toString(), secret + '\n' + secret + '\n');
        // Cancelled authorization and invalid passwords.
        qputenv("LURVIKO_PKEXEC_EXIT", "126");
        manager.changePassword(secret);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QVERIFY(!finished.last().at(0).toBool());
        qunsetenv("LURVIKO_PKEXEC_EXIT");
        manager.changePassword("two\nlines");
        QCOMPARE(finished.size(), 3);
        QVERIFY(!finished.last().at(0).toBool());
    }
    void statusParsesSessionsAndRootOnly() {
        const auto smbstatus = script("fake-smbstatus", R"py(#!/usr/bin/python3
import json, os, sys
if os.environ.get('LURVIKO_STATUS_ROOT_ONLY'):
    print('smbstatus only works as root!')
    print(json.dumps({'timestamp': 'now', 'version': '4.25.1'}))
    sys.exit(0)
print(json.dumps({'version': '4.25.1', 'sessions': {'1': {'username': 'alice', 'groupname': 'users',
  'remote_machine': '192.168.1.5', 'hostname': 'ipv4:192.168.1.5:50000', 'session_dialect': 'SMB3_11',
  'encryption': {'cipher': 'AES-128-GCM'}, 'signing': {'cipher': 'AES-128-GMAC'}}},
  'tcons': {'7': {'service': 'Docs', 'machine': '192.168.1.5', 'connected_at': '2026-11-17T10:00:00'}},
  'open_files': {'/srv/a': {}, '/srv/b': {}}}))
)py");
        SambaShareManager manager(program, nullptr);
        manager.setHelperPrograms({}, {}, smbstatus, {}, {});
        manager.loadStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.statusBusy() && !manager.status().isEmpty(), 5000);
        auto status = manager.status();
        QCOMPARE(status.value("requiresAdmin").toBool(), false);
        const auto session = status.value("sessions").toList().value(0).toMap();
        QCOMPARE(session.value("user").toString(), QString("alice"));
        QCOMPARE(session.value("protocol").toString(), QString("SMB3_11"));
        QCOMPARE(session.value("encryption").toString(), QString("AES-128-GCM"));
        QCOMPARE(status.value("connections").toList().value(0).toMap().value("share").toString(), QString("Docs"));
        QCOMPARE(status.value("openFiles").toInt(), 2);
        qputenv("LURVIKO_STATUS_ROOT_ONLY", "1");
        manager.loadStatus();
        QTRY_VERIFY_WITH_TIMEOUT(!manager.statusBusy() && manager.status().value("requiresAdmin").toBool(), 5000);
        qunsetenv("LURVIKO_STATUS_ROOT_ONLY");
    }
    void permissionPlanAddsOnlyMissingBits() {
        // parent 0700 (others cannot pass through), share folder 0755.
        const QString parent = root.path() + "/private";
        const QString share = parent + "/Share";
        QVERIFY(QDir().mkpath(share));
        QVERIFY(::chmod(parent.toUtf8().constData(), 0700) == 0);
        QVERIFY(::chmod(share.toUtf8().constData(), 0755) == 0);
        QVERIFY(::chmod(root.path().toUtf8().constData(), 0755) == 0);
        SambaShareManager manager(program, nullptr);
        manager.inspect(share);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        const auto me = manager.userName();

        // Only the owner: nothing to change.
        auto plan = manager.permissionPlan({QVariantMap{{"name", me}, {"access", "F"}}}, false);
        QVERIFY(!plan.value("needed").toBool());

        // Everyone read: 0755 already has r-x for others; the parent needs x.
        plan = manager.permissionPlan({QVariantMap{{"name", "Everyone"}, {"access", "R"}}}, false);
        QVERIFY(plan.value("needed").toBool());
        auto items = plan.value("items").toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toMap().value("path").toString(), parent);
        QCOMPARE(items.at(0).toMap().value("currentMode").toString(), QString("0700"));
        QCOMPARE(items.at(0).toMap().value("newMode").toString(), QString("0701"));
        QCOMPARE(items.at(0).toMap().value("role").toString(), QString("traverse"));

        // Full control for another user: 0755 -> 0757 on the share too.
        plan = manager.permissionPlan({QVariantMap{{"name", me}, {"access", "F"}},
                                       QVariantMap{{"name", "alice"}, {"access", "F"}}}, false);
        items = plan.value("items").toList();
        QCOMPARE(items.size(), 2);
        QCOMPARE(items.at(0).toMap().value("path").toString(), share);
        QCOMPARE(items.at(0).toMap().value("newMode").toString(), QString("0757"));
        QCOMPARE(items.at(0).toMap().value("newText").toString(), QString("rwxr-xrwx"));
        QCOMPARE(plan.value("level").toString(), QString("full"));
        QVERIFY(!plan.value("requiresAdmin").toBool());

        // Deny grants nothing.
        QVERIFY(!manager.permissionPlan({QVariantMap{{"name", me}, {"access", "F"}},
                                         QVariantMap{{"name", "bob"}, {"access", "D"}}}, false).value("needed").toBool());

        QSignalSpy finished(&manager, &SambaShareManager::permissionsFinished);
        manager.fixPermissions({QVariantMap{{"name", "alice"}, {"access", "F"}}}, false);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(0).toBool());
        QCOMPARE(QFileInfo(share).permissions() & QFile::WriteOther, QFile::WriteOther);
        struct stat st {};
        QCOMPARE(::stat(parent.toUtf8().constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 07777), 0701);
        QCOMPARE(::stat(share.toUtf8().constData(), &st), 0);
        QCOMPARE(int(st.st_mode & 07777), 0757);
        QVERIFY(!manager.permissionPlan({QVariantMap{{"name", "alice"}, {"access", "F"}}}, false).value("needed").toBool());
        QVERIFY(::chmod(share.toUtf8().constData(), 0755) == 0);
    }
    void restoreTakesBackOnlyOurBits() {
        const QString parent = root.path() + "/restore";
        const QString share = parent + "/Share";
        const QString sibling = parent + "/Other";
        QVERIFY(QDir().mkpath(share)); QVERIFY(QDir().mkpath(sibling));
        QVERIFY(::chmod(root.path().toUtf8().constData(), 0755) == 0);
        QVERIFY(::chmod(parent.toUtf8().constData(), 0700) == 0);
        QVERIFY(::chmod(share.toUtf8().constData(), 0750) == 0);
        SambaShareManager manager(program, nullptr);
        manager.setOtherSharesForTest({});
        manager.inspect(share);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        const QVariantList full{QVariantMap{{"name", "Everyone"}, {"access", "F"}}};
        const QVariantList read{QVariantMap{{"name", "Everyone"}, {"access", "R"}}};
        QSignalSpy fixed(&manager, &SambaShareManager::permissionsFinished);
        manager.fixPermissions(full, false);
        QTRY_COMPARE_WITH_TIMEOUT(fixed.size(), 1, 5000);
        const auto mode = [](const QString& path) { struct stat st {}; ::stat(path.toUtf8().constData(), &st); return int(st.st_mode & 07777); };
        QCOMPARE(mode(share), 0757); QCOMPARE(mode(parent), 0701);

        // Full -> read: only o+w goes; r-x for others and the parent x stay.
        auto plan = manager.restorePlan(read, false, false);
        QVERIFY(plan.value("needed").toBool());
        auto items = plan.value("items").toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toMap().value("newMode").toString(), QString("0755"));
        QCOMPARE(items.at(0).toMap().value("originalMode").toString(), QString("0750"));
        QSignalSpy restored(&manager, &SambaShareManager::permissionsRestored);
        manager.restorePermissions(read, false, false);
        QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 1, 5000);
        QVERIFY(restored.last().at(0).toBool());
        QCOMPARE(mode(share), 0755); QCOMPARE(mode(parent), 0701);
        QVERIFY(!manager.restorePlan(read, false, false).value("needed").toBool());

        // Removing: back to the original modes, except what another share needs
        // and what the user changed by hand in between.
        manager.setOtherSharesForTest({sibling});
        plan = manager.restorePlan(read, false, true);
        items = plan.value("items").toList();
        QCOMPARE(items.size(), 1);
        QCOMPARE(items.at(0).toMap().value("path").toString(), share);
        QCOMPARE(items.at(0).toMap().value("newMode").toString(), QString("0750"));
        manager.setOtherSharesForTest({});
        QVERIFY(::chmod(parent.toUtf8().constData(), 0711) == 0);    // changed by hand
        plan = manager.restorePlan(read, false, true);
        QCOMPARE(plan.value("skipped").toStringList(), QStringList{parent});
        manager.restorePermissions(read, false, true);
        QTRY_COMPARE_WITH_TIMEOUT(restored.size(), 2, 5000);
        QCOMPARE(mode(share), 0750); QCOMPARE(mode(parent), 0711);
        // Nothing is remembered once the share is gone.
        QVERIFY(!manager.restorePlan(read, false, true).value("needed").toBool());
        QVERIFY(::chmod(parent.toUtf8().constData(), 0755) == 0);
    }
    void failureAndObsoleteRequests() {
        state({{"fail_read", true}});
        SambaShareManager manager(program, nullptr); manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(!manager.busy(), 5000);
        QVERIFY(!manager.available()); QVERIFY(manager.error().contains("usershares disabled"));
        state({{"delay", 0.3}});
        manager.inspect(folder); QVERIFY(manager.busy()); manager.clear();
        QTest::qWait(400); QVERIFY(!manager.available()); QVERIFY(manager.path().isEmpty());
        state({{"fail_write", true}}); manager.inspect(folder);
        QTRY_VERIFY_WITH_TIMEOUT(manager.available() && !manager.busy(), 5000);
        QSignalSpy finished(&manager, &SambaShareManager::operationFinished);
        manager.save("Failure", "", true);
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(!finished.last().at(0).toBool()); QVERIFY(!manager.shared());
        QVERIFY(manager.error().contains("access denied"));
    }
};
QTEST_GUILESS_MAIN(SambaShareTests)
#include "samba_share_tests.moc"
