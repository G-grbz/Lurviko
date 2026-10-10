#include <QtTest>
#include <QProcess>
#include <QTemporaryDir>
#include "vaultsecurity.h"
#include "archivearguments.h"

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
