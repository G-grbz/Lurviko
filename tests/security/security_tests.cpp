#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include "vaultsecurity.h"
#include "archivearguments.h"
#include "fileaccesssecurity.h"
#include <sys/stat.h>
#include <unistd.h>

class SecurityTests : public QObject {
    Q_OBJECT
private slots:
    void kdfBounds() {
        QJsonObject header{{"iterations", 3}, {"memCostKiB", 65536}, {"lanes", 1}, {"threads", 1}};
        VaultSecurity::Argon2Parameters parsed;
        QVERIFY(VaultSecurity::parseArgon2Parameters(header, &parsed));
        QCOMPARE(parsed.memoryKiB, quint32(65536));
        for (const QString &name : header.keys()) {
            for (const QJsonValue &invalid : {QJsonValue(-1), QJsonValue(0), QJsonValue(1.5),
                    QJsonValue(4294967295.0), QJsonValue("3"), QJsonValue(true), QJsonValue()}) {
                auto corrupt = header; corrupt[name] = invalid;
                QVERIFY2(!VaultSecurity::parseArgon2Parameters(corrupt, &parsed), qPrintable(name));
            }
            auto missing = header; missing.remove(name);
            QVERIFY(!VaultSecurity::parseArgon2Parameters(missing, &parsed));
        }
        header["iterations"] = 10; header["memCostKiB"] = 262144; header["lanes"] = 8; header["threads"] = 8;
        QVERIFY(VaultSecurity::parseArgon2Parameters(header, &parsed));
        header["threads"] = 9;
        QVERIFY(!VaultSecurity::parseArgon2Parameters(header, &parsed));
    }
    void pathBoundaries() {
        QTemporaryDir parent;
        const QString root = parent.filePath("lurviko-private");
        const QString sibling = parent.filePath("lurviko-private-other");
        QVERIFY(QDir().mkpath(root + "/nested")); QVERIFY(QDir().mkpath(sibling));
        QVERIFY(VaultSecurity::containsFile(root, root + "/file"));
        QVERIFY(VaultSecurity::containsFile(root, root + "/nested/file"));
        QVERIFY(!VaultSecurity::containsFile(root, sibling + "/file"));
        QVERIFY(!VaultSecurity::containsFile(root, root + "/../lurviko-private-other/file"));
        QVERIFY(QFile::link(sibling, root + "/escape"));
        QVERIFY(!VaultSecurity::containsFile(root, root + "/escape/file"));
        QVERIFY(!VaultSecurity::containsFile({}, root + "/file"));
    }
    void runtimeStorage() {
        QTemporaryDir disk;
        const QString first = VaultSecurity::createRuntimeDirectory(disk.path());
        const QString second = VaultSecurity::createRuntimeDirectory({});
        QVERIFY(!first.isEmpty()); QVERIFY(!second.isEmpty()); QVERIFY(first != second);
        QVERIFY(VaultSecurity::isMemoryFilesystem(first));
        QVERIFY(VaultSecurity::isMemoryFilesystem(second));
        QVERIFY(!(QFileInfo(first).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther)));
        QVERIFY(QDir(first).removeRecursively()); QVERIFY(QDir(second).removeRecursively());
    }
    void administratorTreePreflight() {
        QTemporaryDir fixture;
        const QString root = fixture.filePath("tree");
        QVERIFY(QDir().mkpath(root + "/child"));
        const QString victim = fixture.filePath("outside.txt");
        QFile file(victim); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("outside"); file.close();
        QVERIFY(::chmod(QFile::encodeName(victim).constData(), 0600) == 0);
        QVERIFY(QFile::link(victim, root + "/child/link"));
        QVERIFY(::chmod(QFile::encodeName(root).constData(), 0700) == 0);
        QVERIFY(::chmod(QFile::encodeName(root + "/child").constData(), 0700) == 0);
        QVERIFY(FileAccessSecurity::checkAdministratorTree(root, ::getuid()).safe);
        // Test double for a privileged path-based operation: if it ran, it
        // would follow the link and chmod the external file. The actual shared
        // preflight must stop dispatch before any fake administrator job runs.
        int dispatched = 0;
        const auto fakeAdministrator = [&] {
            if (!FileAccessSecurity::checkAdministratorTree(root, ::getuid()).safe) return false;
            ++dispatched;
            return ::chmod(QFile::encodeName(root + "/child/link").constData(), 0777) == 0;
        };
        QVERIFY(::chmod(QFile::encodeName(root + "/child").constData(), 0777) == 0);
        QVERIFY(!fakeAdministrator()); QCOMPARE(dispatched, 0);
        struct stat outside {}; QVERIFY(::stat(QFile::encodeName(victim).constData(), &outside) == 0);
        QCOMPARE(outside.st_mode & 0777, mode_t(0600));
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("outside")); file.close();
        QVERIFY(::chmod(QFile::encodeName(root + "/child").constData(), 0770) == 0);
        QVERIFY(!FileAccessSecurity::checkAdministratorTree(root, ::getuid()).safe);
        QVERIFY(::chmod(QFile::encodeName(root + "/child").constData(), 0700) == 0);
        if (::getuid() != 0)
            QVERIFY(!FileAccessSecurity::checkAdministratorTree(root, ::getuid() + 1).safe);
        std::atomic_bool cancelled{true};
        QVERIFY(!FileAccessSecurity::checkAdministratorTree(root, ::getuid(), &cancelled).safe);
        QVERIFY(!FileAccessSecurity::checkAdministratorTree(root + "/child/link", ::getuid()).safe);
    }
    void archiveOptionsAreOperands() {
        QTemporaryDir first; QTemporaryDir second;
        const QStringList names = {"--help", "-C", "@list", "İşler Güçler.txt"};
        QStringList arguments{"-cf", first.filePath("out.tar")};
        int index = 0;
        for (const QString &name : names) {
            const QString path = (index++ % 2 ? second : first).filePath(name);
            QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); file.write(name.toUtf8()); file.close();
            const QStringList operand = ArchiveArguments::sourceOperand(QFileInfo(path));
            QCOMPARE(operand.last(), "./" + name);
            arguments << operand;
        }
        QProcess archive; archive.start("bsdtar", arguments);
        QVERIFY(archive.waitForFinished()); QCOMPARE(archive.exitCode(), 0);
        const QString output = second.filePath("extract"); QVERIFY(QDir().mkpath(output));
        archive.start("bsdtar", {"-xf", first.filePath("out.tar"), "-C", output});
        QVERIFY(archive.waitForFinished()); QCOMPARE(archive.exitCode(), 0);
        for (const QString &name : names) {
            QFile file(QDir(output).filePath(name)); QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), name.toUtf8());
        }
    }
};
QTEST_GUILESS_MAIN(SecurityTests)
#include "security_tests.moc"
