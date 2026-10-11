#include <QtTest>
#include <cstdio>
#include <QProcess>
#include <QTemporaryDir>
#include "vaultsecurity.h"
#include "vaultrecovery.h"
#include "archivearguments.h"
#include "fileaccesssecurity.h"
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include "syscall_doubles.h"

class SecurityTests : public QObject {
    Q_OBJECT
private slots:
    void recoveryKeyEncoding() {
        const QByteArray key = VaultRecovery::generateKey(), other = VaultRecovery::generateKey();
        QCOMPARE(key.size(), 32); QCOMPARE(other.size(), 32); QVERIFY(key != other);
        QString encoded = VaultRecovery::encodeKey(key);
        QVERIFY(encoded.startsWith("LRK1-")); QCOMPARE(encoded.size(), 85);
        QByteArray decoded;
        QVERIFY(VaultRecovery::decodeKey(encoded, &decoded)); QCOMPARE(decoded, key);
        QVERIFY(VaultRecovery::decodeKey(" \n" + encoded.toLower() + "\t", &decoded)); QCOMPARE(decoded, key);
        encoded[5] = encoded[5] == 'A' ? 'B' : 'A';
        QVERIFY(!VaultRecovery::decodeKey(encoded, &decoded));
        for (const auto &bad : {QString("LRK2-"), QString(257, 'A'), QString("ＬＲＫ１"), QString("LRK1-" + QString(72, 'G'))})
            QVERIFY(!VaultRecovery::decodeKey(bad, &decoded));
        QVERIFY(VaultRecovery::encodeKey({}).isEmpty());
    }
    void recoveryAuthenticatedEnvelope() {
        const QByteArray key = VaultRecovery::generateKey(), master = VaultRecovery::generateKey();
        QJsonObject header{{"vaultId", "11223344-5566-7788-99aa-bbccddeeff00"}};
        QVERIFY(VaultRecovery::wrap(&header, master, key)); QVERIFY(VaultRecovery::hasEnvelope(header));
        QByteArray result;
        QVERIFY(VaultRecovery::unwrap(header, key, &result)); QCOMPARE(result, master);
        QVERIFY(!VaultRecovery::unwrap(header, VaultRecovery::generateKey(), &result));
        auto corrupt = header; corrupt["vaultId"] = "11223344-5566-7788-99aa-bbccddeeff01";
        QVERIFY(!VaultRecovery::unwrap(corrupt, key, &result));
        const QJsonObject original = header["recovery"].toObject();
        for (const QString &name : {QString("iv"), QString("tag"), QString("wrappedKey"), QString("id")}) {
            corrupt = header; auto envelope = original;
            QString value = envelope[name].toString(); value[0] = value[0] == 'a' ? 'b' : 'a'; envelope[name] = value;
            corrupt["recovery"] = envelope;
            QVERIFY2(!VaultRecovery::unwrap(corrupt, key, &result), qPrintable(name));
        }
        for (const QJsonValue &bad : {QJsonValue(true), QJsonValue(1.5), QJsonValue("1"), QJsonValue(2)}) {
            corrupt = header; auto envelope = original; envelope["version"] = bad; corrupt["recovery"] = envelope;
            QVERIFY(!VaultRecovery::hasEnvelope(corrupt));
        }
        auto replacement = header;
        const QByteArray newKey = VaultRecovery::generateKey();
        QVERIFY(VaultRecovery::wrap(&replacement, master, newKey));
        QVERIFY(!VaultRecovery::unwrap(replacement, key, &result));
        QVERIFY(VaultRecovery::unwrap(replacement, newKey, &result)); QCOMPARE(result, master);
        replacement.remove("recovery"); QVERIFY(!VaultRecovery::unwrap(replacement, newKey, &result));
    }
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
        const auto first = VaultSecurity::createRuntimeDirectory(disk.path());
        const auto second = VaultSecurity::createRuntimeDirectory({});
        QVERIFY(first); QVERIFY(second); QVERIFY(first->path() != second->path());
        QVERIFY(VaultSecurity::isMemoryFilesystem(first->path()));
        QVERIFY(VaultSecurity::isMemoryFilesystem(second->path()));
        QVERIFY(!(QFileInfo(first->path()).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther)));
        const int lock = ::open(QFile::encodeName(first->path() + "/.lurviko-session.lock").constData(), O_RDWR | O_CLOEXEC);
        QVERIFY(lock >= 0); QVERIFY(::flock(lock, LOCK_EX | LOCK_NB) != 0); ::close(lock);
        QFile file(first->path() + "/preview.txt"); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("plaintext"); file.close();
        QVERIFY(first->clearContents()); QVERIFY(!file.exists());
        QVERIFY(QFileInfo::exists(first->path() + "/.lurviko-session.lock"));
    }
    void runtimeOrphanCleanup() {
        QTemporaryDir parent("/dev/shm/lurviko-security-parent-XXXXXX"); QVERIFY(parent.isValid());
        const auto own = VaultSecurity::createRuntimeDirectory(parent.path()); QVERIFY(own);
        QFile preview(own->path() + "/keep.txt"); QVERIFY(preview.open(QIODevice::WriteOnly)); preview.write("live"); preview.close();
        QProcess live;
        live.start(QCoreApplication::applicationFilePath(), {"--hold-runtime", parent.path()});
        QVERIFY(live.waitForReadyRead());
        const QString other = QString::fromUtf8(live.readLine()).trimmed(); QVERIFY(!other.isEmpty());
        auto result = VaultSecurity::cleanupStaleRuntimeDirectories({parent.path()}, own->path());
        QCOMPARE(result.removed, 0); QVERIFY(QFileInfo::exists(other)); QVERIFY(preview.exists());
        live.kill(); QVERIFY(live.waitForFinished());
        QVERIFY(QFileInfo::exists(other + "/plaintext.txt")); // Simulated crash residue.
        result = VaultSecurity::cleanupStaleRuntimeDirectories({parent.path()}, own->path());
        QCOMPARE(result.removed, 1); QVERIFY(!QFileInfo::exists(other)); QVERIFY(preview.exists());
        const QString orphan = parent.filePath("lurviko-private-old-session");
        const QString legacy = parent.filePath("lurviko-private");
        const QString foreign = parent.filePath("lurviko-private-foreign");
        const QString external = parent.filePath("outside");
        for (const QString &path : {orphan, legacy, foreign, external}) QVERIFY(QDir().mkpath(path));
        QFile outside(external + "/keep.txt"); QVERIFY(outside.open(QIODevice::WriteOnly)); outside.write("outside"); outside.close();
        QVERIFY(QDir().mkpath(orphan + "/nested"));
        QFile residue(orphan + "/nested/plain.txt"); QVERIFY(residue.open(QIODevice::WriteOnly)); residue.write("stale"); residue.close();
        QVERIFY(QFile::link(external, orphan + "/escape"));
        const QString linked = parent.filePath("lurviko-private-symlink"); QVERIFY(QFile::link(external, linked));
        // Simulate foreign ownership at the descriptor boundary used by the
        // real sweep; changing file ownership would require root.
        SecurityTestDoubles::foreignOwnerPath = QFile::encodeName(foreign);
        result = VaultSecurity::cleanupStaleRuntimeDirectories({parent.path()}, own->path());
        SecurityTestDoubles::foreignOwnerPath.clear();
        QCOMPARE(result.removed, 2); QCOMPARE(result.failed, 0);
        QVERIFY(!QFileInfo::exists(orphan)); QVERIFY(!QFileInfo::exists(legacy));
        QVERIFY(QFileInfo::exists(foreign)); QVERIFY(QFileInfo(linked).isSymLink()); QVERIFY(outside.exists());
        QVERIFY(QFileInfo::exists(own->path() + "/.lurviko-session.lock"));
        QVERIFY(own->clearContents());
        const int lock = ::open(QFile::encodeName(own->path() + "/.lurviko-session.lock").constData(), O_RDWR | O_CLOEXEC);
        QVERIFY(lock >= 0); QVERIFY(::flock(lock, LOCK_EX | LOCK_NB) != 0); ::close(lock);
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
    void localAccessMountBoundary() {
        QTemporaryDir fixture;
        const QString root = fixture.filePath("tree");
        const QString mount = root + "/mounted";
        QVERIFY(QDir().mkpath(mount));
        QVERIFY(::chmod(QFile::encodeName(root).constData(), 0700) == 0);
        QVERIFY(::chmod(QFile::encodeName(mount).constData(), 0700) == 0);
        QFile inside(mount + "/keep.txt"); QVERIFY(inside.open(QIODevice::WriteOnly)); inside.write("keep"); inside.close();
        QVERIFY(::chmod(QFile::encodeName(inside.fileName()).constData(), 0600) == 0);
        const auto task = std::make_shared<FileAccessTask>();
        SecurityTestDoubles::differentDevicePath = QFile::encodeName(mount);
        FileAccessSecurity::applyLocalAccess(root, 0755, uid_t(-1), gid_t(-1), true, task);
        SecurityTestDoubles::differentDevicePath.clear();
        QCOMPARE(task->skipped.load(), 1); QCOMPARE(task->failed.load(), 0); QCOMPARE(task->changed.load(), 1);
        struct stat st {};
        QVERIFY(::stat(QFile::encodeName(mount).constData(), &st) == 0); QCOMPARE(st.st_mode & 0777, mode_t(0700));
        QVERIFY(::stat(QFile::encodeName(inside.fileName()).constData(), &st) == 0); QCOMPARE(st.st_mode & 0777, mode_t(0600));
        QVERIFY(inside.open(QIODevice::ReadOnly)); QCOMPARE(inside.readAll(), QByteArray("keep"));
    }
    void localAccessPreservesSpecialBits() {
        QTemporaryDir fixture;
        const QString root = fixture.filePath("tree"); QVERIFY(QDir().mkpath(root));
        QFile program(root + "/program"); QVERIFY(program.open(QIODevice::WriteOnly)); program.write("unchanged"); program.close();
        QVERIFY(::chmod(QFile::encodeName(root).constData(), 03755) == 0);
        QVERIFY(::chmod(QFile::encodeName(program.fileName()).constData(), 04755) == 0);
        const auto task = std::make_shared<FileAccessTask>();
        FileAccessSecurity::applyLocalAccess(root, 0700, uid_t(-1), gid_t(-1), true, task);
        QCOMPARE(task->failed.load(), 0); QCOMPARE(task->changed.load(), 2);
        struct stat st {};
        QVERIFY(::stat(QFile::encodeName(root).constData(), &st) == 0); QCOMPARE(st.st_mode & 07777, mode_t(03700));
        QVERIFY(::stat(QFile::encodeName(program.fileName()).constData(), &st) == 0); QCOMPARE(st.st_mode & 07777, mode_t(04700));
        QVERIFY(program.open(QIODevice::ReadOnly)); QCOMPARE(program.readAll(), QByteArray("unchanged"));
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
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 3 && app.arguments()[1] == "--hold-runtime") {
        const auto directory = VaultSecurity::createRuntimeDirectory(app.arguments()[2]);
        if (!directory) return 1;
        QFile file(directory->path() + "/plaintext.txt");
        if (!file.open(QIODevice::WriteOnly)) return 2;
        file.write("crash residue"); file.close();
        std::printf("%s\n", qPrintable(directory->path())); std::fflush(stdout);
        while (true) ::pause();
    }
    SecurityTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "security_tests.moc"
