#include "checksumcalculator.h"
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

class ChecksumTests : public QObject {
    Q_OBJECT
    QTemporaryDir root;
    QString write(const QString& name, const QByteArray& data) {
        const QString path = root.filePath(name);
        QFile file(path);
        if (file.open(QIODevice::WriteOnly)) file.write(data);
        return path;
    }
private slots:
    void knownVectorsInOnePass() {
        ChecksumCalculator calculator;
        calculator.setPath(QUrl::fromLocalFile(write("abc.txt", "abc")).toString());
        QSignalSpy finished(&calculator, &ChecksumCalculator::finished);
        calculator.compute(ChecksumCalculator::algorithms());
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QVERIFY(finished.last().at(0).toBool());
        const auto r = calculator.results();
        QCOMPARE(r.value("MD5").toString(), QString("900150983cd24fb0d6963f7d28e17f72"));
        QCOMPARE(r.value("SHA1").toString(), QString("a9993e364706816aba3e25717850c26c9cd0d89d"));
        QCOMPARE(r.value("SHA256").toString(), QString("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
        QCOMPARE(r.value("SHA3-256").toString(), QString("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532"));
        QCOMPARE(r.value("CRC32").toString(), QString("352441c2"));
        QCOMPARE(r.value("BLAKE2b-512").toString(), QString(
            "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
            "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"));
        QCOMPARE(r.size(), ChecksumCalculator::algorithms().size());
        QCOMPARE(calculator.progress(), 1.0);
    }
    void largeFileProgressAndCancel() {
        QByteArray block(1024 * 1024, 'x');
        const QString path = root.filePath("large.bin");
        { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); for (int i = 0; i < 96; ++i) file.write(block); }
        ChecksumCalculator calculator;
        calculator.setPath(path);
        calculator.compute({"SHA512", "SHA3-512", "BLAKE2b-512"});
        QVERIFY(calculator.busy());
        QCOMPARE(calculator.pending().size(), 3);
        calculator.cancel();
        QVERIFY(!calculator.busy());
        QTest::qWait(200);
        QVERIFY(calculator.results().isEmpty());
        // A second request while running joins the same pass.
        QSignalSpy finished(&calculator, &ChecksumCalculator::finished);
        calculator.compute({"MD5"});
        calculator.compute({"SHA256"});
        QCOMPARE(calculator.pending(), QStringList({"MD5", "SHA256"}));
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
        QCOMPARE(calculator.results().value("SHA256").toString(),
                 QString::fromLatin1(ChecksumCalculator::hashFile(path, "SHA256")));
        QVERIFY(calculator.results().contains("MD5"));
    }
    void recognisesPastedForms_data() {
        QTest::addColumn<QString>("text");
        QTest::addColumn<QString>("hex");
        QTest::addColumn<QStringList>("candidates");
        QTest::addColumn<QString>("fileName");
        const QString sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        QTest::newRow("bare") << " " + sha.toUpper() + " " << sha << QStringList{"SHA256", "SHA3-256", "BLAKE2s-256"} << "";
        QTest::newRow("gnu") << sha + "  abc.txt" << sha << QStringList{"SHA256", "SHA3-256", "BLAKE2s-256"} << "abc.txt";
        QTest::newRow("gnu-binary") << sha + " *abc.txt" << sha << QStringList{"SHA256", "SHA3-256", "BLAKE2s-256"} << "abc.txt";
        QTest::newRow("bsd") << "SHA256 (abc.txt) = " + sha << sha << QStringList{"SHA256"} << "abc.txt";
        QTest::newRow("prefixed") << "sha3-256:" + QString("3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532")
                                  << "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532" << QStringList{"SHA3-256"} << "";
        QTest::newRow("sri") << "sha256-" + QString::fromLatin1(QByteArray::fromHex(sha.toLatin1()).toBase64()) << sha << QStringList{"SHA256"} << "";
        QTest::newRow("md5") << "900150983cd24fb0d6963f7d28e17f72" << "900150983cd24fb0d6963f7d28e17f72" << QStringList{"MD5"} << "";
        QTest::newRow("sfv") << "abc.txt 352441C2" << "352441c2" << QStringList{"CRC32"} << "abc.txt";
    }
    void recognisesPastedForms() {
        QFETCH(QString, text); QFETCH(QString, hex); QFETCH(QStringList, candidates); QFETCH(QString, fileName);
        const auto parsed = ChecksumCalculator::identify(text);
        QVERIFY(parsed.value("valid").toBool());
        QCOMPARE(parsed.value("hex").toString(), hex);
        QCOMPARE(parsed.value("candidates").toStringList(), candidates);
        QCOMPARE(parsed.value("fileName").toString(), fileName);
    }
    void rejectsGarbage() {
        for (const auto& text : QStringList{"", "hello", "xyz123", "abcd", "SHA256 (a) = 900150983cd24fb0d6963f7d28e17f72",
                                            QString(129, 'a')})
            QVERIFY2(!ChecksumCalculator::identify(text).value("valid").toBool(), qPrintable(text));
    }
    void comparesAndFindsVerificationFiles() {
        const QString file = write("image.iso", "abc");
        const QString sha = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
        write("SHA256SUMS", QByteArray("0000000000000000000000000000000000000000000000000000000000000000  other.iso\n")
              + sha.toLatin1() + "  image.iso\n");
        write("image.iso.md5", "900150983cd24fb0d6963f7d28e17f72\n");
        ChecksumCalculator calculator;
        calculator.setPath(file);
        QCOMPARE(calculator.compare(sha), QString());
        QSignalSpy finished(&calculator, &ChecksumCalculator::finished);
        calculator.compute({"SHA256", "SHA3-256", "BLAKE2s-256"});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(calculator.compare(sha + "  image.iso"), QString("match"));
        QCOMPARE(calculator.matchingAlgorithm(sha), QString("SHA256"));
        QCOMPARE(calculator.compare(QString(sha).replace(0, 1, "c")), QString("mismatch"));

        QSignalSpy sidecars(&calculator, &ChecksumCalculator::sidecarsChanged);
        calculator.findSidecars();
        QTRY_VERIFY_WITH_TIMEOUT(sidecars.size() > 0, 5000);
        QHash<QString, QString> found;
        for (const auto& value : calculator.sidecars()) {
            const auto entry = value.toMap();
            found.insert(entry.value("fileName").toString(),
                         entry.value("algorithm").toString() + ':' + entry.value("expected").toString());
        }
        QCOMPARE(found.value("SHA256SUMS"), "SHA256:" + sha);
        QCOMPARE(found.value("image.iso.md5"), QString("MD5:900150983cd24fb0d6963f7d28e17f72"));
    }
    void writesVerificationFileWithoutReplacing() {
        const QString file = write("data.bin", "abc");
        ChecksumCalculator calculator;
        calculator.setPath(file);
        QCOMPARE(calculator.writeSidecar("SHA256").isEmpty(), false);   // not computed yet
        QSignalSpy finished(&calculator, &ChecksumCalculator::finished);
        calculator.compute({"SHA256"});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 5000);
        QCOMPARE(calculator.writeSidecar("SHA256"), QString());
        QFile sidecar(file + ".sha256");
        QVERIFY(sidecar.open(QIODevice::ReadOnly));
        QCOMPARE(sidecar.readAll(), QByteArray("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad  data.bin\n"));
        QVERIFY(!calculator.writeSidecar("SHA256").isEmpty());          // exists: never replaced
        // A symlink planted at the target is not followed.
        const QString victim = write("victim.txt", "keep");
        QVERIFY(QFile::link(victim, file + ".md5"));
        calculator.compute({"MD5"});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 5000);
        QVERIFY(!calculator.writeSidecar("MD5").isEmpty());
        QFile kept(victim); QVERIFY(kept.open(QIODevice::ReadOnly)); QCOMPARE(kept.readAll(), QByteArray("keep"));
    }
    void rejectsNonLocalAndDirectories() {
        ChecksumCalculator calculator;
        calculator.setPath("sftp://example.test/file");
        calculator.compute({"MD5"});
        QVERIFY(!calculator.busy()); QVERIFY(!calculator.error().isEmpty());
        calculator.setPath(root.path());
        calculator.compute({"MD5"});
        QVERIFY(!calculator.busy()); QVERIFY(!calculator.error().isEmpty());
    }
};
QTEST_GUILESS_MAIN(ChecksumTests)
#include "checksum_tests.moc"
