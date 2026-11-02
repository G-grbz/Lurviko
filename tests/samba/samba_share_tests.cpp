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
        shares[command[2]]={'path':command[3],'comment':command[4],'usershare_acl':command[5]+',','guest_ok':'n'}
        assert command[6]=='guest_ok=n'
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
        QVERIFY(manager.shared()); QVERIFY(!manager.editable());
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
