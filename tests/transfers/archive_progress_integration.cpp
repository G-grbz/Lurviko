#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QTimer>
#include <KJob>
#include <cstdio>
#include <functional>
#include <unistd.h>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec(); }
static bool until(const std::function<bool()> &check, int timeout = 15000) {
    QElapsedTimer clock; clock.start();
    while (!check() && clock.elapsed() < timeout) pause(10);
    return check();
}
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("ArchiveProgress");
    const QString root = QString::fromLocal8Bit(qgetenv("LURVIKO_ARCHIVE_TEST_ROOT"));
    const QString source = root + "/Veriler Türkçe";
    require(QDir().mkpath(source + "/nested"), "archive fixture directory created");
    QFile random("/dev/urandom"); require(random.open(QIODevice::ReadOnly), "random fixture source opens");
    const QByteArray block = random.read(1024 * 1024);
    QFile large(source + "/Büyük öğe.bin"); require(large.open(QIODevice::WriteOnly), "large fixture opens");
    for (int i = 0; i < 96; ++i) require(large.write(block) == block.size(), "large fixture written");
    large.close();
    for (const auto &name : {QString(".hidden"), QString("nested/--help"), QString("şarkı.txt")}) {
        QFile file(source + "/" + name); require(file.open(QIODevice::WriteOnly), "small fixture opens"); file.write("unchanged");
    }
    QSettings().setValue("ui/language", "tr");
    FileOperations operations; operations.setKioProgressEnabled(false);
    bool finished = false, success = false;
    QString message, title, sourceLabel, archiveLabel, lastDescription;
    qulonglong total = 0, processed = 0, bytes = 0; unsigned long finalPercent = 0;
    bool intermediate = false, speedReported = false;
    QObject::connect(&operations, &FileOperations::operationFinished, &app,
                     [&](bool ok, const QString &text, bool, bool) { finished = true; success = ok; message = text; });
    operations.compressItem(QUrl::fromLocalFile(source).toString(), "tar.gz");
    const auto jobs = operations.findChildren<KJob *>(); require(!jobs.isEmpty(), "compression exposes a KJob");
    KJob *job = jobs.last();
    QObject::connect(job, &KJob::description, &app, [&](KJob *, const QString &text,
                       const QPair<QString, QString> &first, const QPair<QString, QString> &second) {
        title = text; sourceLabel = first.first; archiveLabel = second.first; lastDescription = second.second;
    });
    QObject::connect(job, &KJob::percentChanged, &app, [&](KJob *, unsigned long value) {
        finalPercent = value; if (value > 0 && value < 100) intermediate = true;
    });
    QObject::connect(job, &KJob::speed, &app, [&](KJob *, unsigned long value) { speedReported |= value > 0; });
    QObject::connect(job, &KJob::result, &app, [&](KJob *result) {
        total = result->totalAmount(KJob::Items); processed = result->processedAmount(KJob::Items);
        bytes = result->processedAmount(KJob::Bytes);
    });
    require(until([&] { return intermediate || finished; }) && !finished, "real compression reports intermediate progress");
    const int id = operations.operations().first().toMap().value("id").toInt();
    require(operations.toggleOperationPause(id), "compression can pause");
    pause(150);
    require(operations.operations().first().toMap().value("paused").toBool(), "paused state reaches integrated progress");
    require(operations.toggleOperationPause(id), "compression can resume");
    require(until([&] { return finished; }) && success, qPrintable("compression completes: " + message));
    require(title == "Arşiv oluşturuluyor" && sourceLabel == "Kaynak" && archiveLabel == "Arşiv",
            "KIO title and fields use the selected Turkish language");
    const QString archive = root + "/Veriler Türkçe.tar.gz";
    require(lastDescription.contains(archive) && lastDescription.contains("6 / 6 öğe"),
            "KIO exposes destination and completed entry counts");
    require(total == 6 && processed == total && finalPercent == 100 && bytes == qulonglong(QFileInfo(archive).size())
            && speedReported, "native job reports exact final entries, archive output size and measured speed");
    require(operations.operations().first().toMap().value("processedItems").toULongLong() == 6,
            "integrated progress receives the same real counters");
    QProcess list; list.start("bsdtar", {"-tf", archive}); require(list.waitForFinished() && list.exitCode() == 0,
            "created archive is valid");
    require(list.readAllStandardOutput().contains("nested/--help"), "option-like filename is archived safely");
    // Switching the application to English must override the machine's locale.
    QSettings().setValue("ui/language", "en"); finished = false; success = false; title.clear();
    operations.compressItem(QUrl::fromLocalFile(source + "/şarkı.txt").toString(), "zip");
    job = operations.findChildren<KJob *>().last();
    QObject::connect(job, &KJob::description, &app, [&](KJob *, const QString &text,
                    const QPair<QString, QString> &first, const QPair<QString, QString> &second) {
        title = text; sourceLabel = first.first; archiveLabel = second.first;
    });
    require(until([&] { return finished; }) && success && title == "Creating archive"
            && sourceLabel == "Source" && archiveLabel == "Archive", "English selection overrides system language");
    list.start("python3", {"-c", "import sys,zipfile\nwith zipfile.ZipFile(sys.argv[1]) as z:\n"
        " assert z.namelist() == ['şarkı.txt']\n assert z.read('şarkı.txt') == b'unchanged'\n",
        source + "/şarkı.zip"});
    require(list.waitForFinished() && list.exitCode() == 0,
            "real ZIP creation stores exact relative names usable by Ark selected extraction");
    finished = false; success = false;
    operations.compressItem(QUrl::fromLocalFile(source).toString(), "zip");
    const int cancelId = operations.operations().last().toMap().value("id").toInt();
    require(until([&] { const auto rows = operations.operations(); return !rows.isEmpty()
                       && rows.last().toMap().value("processedBytes").toULongLong() > 0; }), "cancel fixture starts writing");
    require(operations.cancelOperation(cancelId), "compression cancellation is available");
    require(until([&] { return finished; }) && !success && !QFileInfo::exists(root + "/Veriler Türkçe.zip"),
            "cancellation removes only the incomplete output archive");
    require(QFileInfo(source + "/Büyük öğe.bin").size() == 96LL * 1024 * 1024, "source remains untouched");
    // The streamed listing must preserve the real extraction/overwrite flow,
    // including Unicode names and an inspection failure before any write.
    const QString output = root + "/extracted";
    require(QDir().mkpath(output), "extraction target created");
    const QString smallArchiveUrl = QUrl::fromLocalFile(source + "/şarkı.zip").toString();
    const QString outputUrl = QUrl::fromLocalFile(output).toString();
    finished = false;
    operations.extractArchive(smallArchiveUrl, outputUrl, false);
    require(until([&] { return finished; }) && success, "streamed archive inspection starts extraction");
    QFile recovered(output + "/şarkı.txt");
    require(recovered.open(QIODevice::ReadOnly) && recovered.readAll() == "unchanged", "Unicode entry extracted intact");
    recovered.close();
    require(recovered.open(QIODevice::WriteOnly), "overwrite fixture opens"); recovered.write("keep until confirmed"); recovered.close();
    bool overwriteRequested = false;
    QObject::connect(&operations, &FileOperations::archiveOverwriteConfirmationRequired, &app,
                     [&](const QString &, const QString &, bool, const QStringList &conflicts) {
        overwriteRequested = conflicts.contains("şarkı.txt");
    });
    finished = false;
    operations.extractArchive(smallArchiveUrl, outputUrl, false);
    require(until([&] { return overwriteRequested || finished; }) && overwriteRequested && !finished,
            "streaming preserves overwrite confirmation");
    require(recovered.open(QIODevice::ReadOnly) && recovered.readAll() == "keep until confirmed", "inspection never overwrites a conflict");
    recovered.close();
    operations.extractArchiveOverwrite(smallArchiveUrl, outputUrl, false);
    require(until([&] { return finished; }) && success, "confirmed extraction succeeds");
    const QString invalidArchive = root + "/invalid.zip";
    QFile invalid(invalidArchive); require(invalid.open(QIODevice::WriteOnly), "invalid fixture created"); invalid.write("not an archive"); invalid.close();
    finished = false;
    operations.extractArchive(QUrl::fromLocalFile(invalidArchive).toString(), outputUrl, false);
    require(until([&] { return finished; }) && !success && !message.isEmpty(), "listing errors are reported without extracting");
    require(recovered.open(QIODevice::ReadOnly) && recovered.readAll() == "unchanged", "failed inspection preserves existing files");
    recovered.close();

    const QString secret = QString::fromUtf8("Uzun şifre ; $(test) 123!");
    const QString secure = root + "/secure";
    require(QDir().mkpath(secure + "/empty") && QDir().mkpath(secure + "/nested"), "encrypted fixture created");
    for (const QString &name : {QString(".hidden"), QString("nested/--help"), QString("literal[1]*.txt")}) {
        QFile file(secure + "/" + name); require(file.open(QIODevice::WriteOnly), "encrypted input opens");
        file.write("private payload");
    }
    QFile outside(root + "/outside-secret.txt"); require(outside.open(QIODevice::WriteOnly), "outside fixture opens");
    outside.write("must not be followed"); outside.close();
    require(::symlink("../outside-secret.txt", QFile::encodeName(secure + "/link").constData()) == 0,
            "encrypted symlink fixture created");
    const auto fileUrl = [](const QString &path) { return QUrl::fromLocalFile(path).toString(); };
    auto tool = [&](const QStringList &arguments, const QString &password) {
        list.start("7z", arguments);
        require(list.waitForStarted(), "7zip validation starts");
        list.write(password.toUtf8() + '\n'); list.closeWriteChannel();
        require(list.waitForFinished(), "7zip validation finishes");
        return list.exitCode();
    };
    for (const QString &format : {QString("zip"), QString("7z")}) {
        const QString activePassword = format == "zip" ? QString("Long ASCII ; $(test) 123!") : secret;
        finished = false; success = false;
        operations.compressEncryptedItems({fileUrl(secure)}, format, activePassword);
        auto *child = operations.findChildren<QProcess *>().last();
        bool inspected = false;
        QObject::connect(child, &QProcess::started, &app, [&, child] {
            require(child->arguments().contains("-p") && !child->arguments().join(' ').contains(activePassword),
                    "password is supplied using a bare -p, never in argv");
            QFile cmd(QStringLiteral("/proc/%1/cmdline").arg(child->processId()));
            require(cmd.open(QIODevice::ReadOnly) && !cmd.readAll().contains(activePassword.toUtf8()),
                    "real child command line contains no password");
            QFile environment(QStringLiteral("/proc/%1/environ").arg(child->processId()));
            require(environment.open(QIODevice::ReadOnly) && !environment.readAll().contains(activePassword.toUtf8()),
                    "real child environment contains no password");
            const QString staging = QFileInfo(child->arguments().at(child->arguments().indexOf("--") - 1)).absolutePath();
            require((QFileInfo(staging).permissions() & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup
                    | QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)) == 0, "staging directory is private");
            inspected = true;
        });
        const bool completed = until([&] { return finished; });
        if (!completed || !success || !inspected)
            std::fprintf(stderr, "ENCRYPTION: format=%s completed=%d success=%d inspected=%d error=%s\n",
                         qPrintable(format), completed, success, inspected, qPrintable(message));
        require(completed && success && inspected, "encrypted compression succeeds");
        const QString encrypted = secure + "." + format;
        require(QFileInfo::exists(encrypted), "encrypted archive published");
        require(tool({"t", "-bso2", encrypted}, "wrong password") != 0, "wrong password cannot validate archive");
        require(tool({"t", "-bso2", encrypted}, activePassword) == 0, "correct password opens archive (including Unicode for 7z)");
        require(tool({"x", "-so", "-bso2", encrypted, "--", "secure/nested/--help"}, activePassword) == 0
                && list.readAllStandardOutput() == "private payload", "encrypted contents recover byte for byte");
        require(tool({"x", "-so", "-bso2", encrypted, "--", "secure/link"}, activePassword) == 0
                && list.readAllStandardOutput() == "../outside-secret.txt", "symlink is archived as a link without reading its target");
        require(tool({"l", "-slt", encrypted}, "") == (format == "zip" ? 0 : 2),
                "7z hides names while ZIP names remain listable");
        if (format == "zip") require(list.readAllStandardOutput().contains("AES-256"), "ZIP uses AES-256 rather than ZipCrypto");
        require(!operations.operations().last().toMap().value("detail").toString().contains(activePassword),
                "progress never contains a password");
        require(operations.operations().last().toMap().value("totalItems").toULongLong() == 7,
                "encrypted final counters include hidden files and empty folders");
    }
    // A name collision must create a new archive and preserve the old one.
    QFile originalArchive(secure + ".zip"); require(originalArchive.open(QIODevice::ReadOnly), "original encrypted ZIP opens");
    const QByteArray originalBytes = originalArchive.readAll(); originalArchive.close();
    finished = false;
    operations.compressEncryptedItems({fileUrl(secure)}, "zip", "Long ASCII ; $(test) 123!");
    require(until([&] { return finished; }) && success && QFileInfo::exists(secure + " (1).zip"), "collision creates a new name");
    require(originalArchive.open(QIODevice::ReadOnly) && originalArchive.readAll() == originalBytes, "existing archive remains unchanged");
    originalArchive.close();
    require(QDir().mkpath(root + "/other"), "mixed-parent fixture created");
    QFile sameName(root + "/other/--help"); require(sameName.open(QIODevice::WriteOnly), "mixed-parent file opens");
    sameName.write("second parent"); sameName.close();
    finished = false;
    operations.compressEncryptedItems({fileUrl(secure + "/nested/--help"), fileUrl(sameName.fileName())}, "7z", secret);
    const bool mixedDone = until([&] { return finished; });
    if (!mixedDone || !success) std::fprintf(stderr, "MIXED: %s\n", qPrintable(message));
    require(mixedDone && success, "encrypted mixed-parent selection succeeds");
    const QString mixedArchive = secure + "/nested/Archive.7z";
    for (const QString &member : {QString("secure/nested/--help"), QString("other/--help")}) {
        require(tool({"x", "-so", "-bso2", mixedArchive, "--", member}, secret) == 0
                && list.readAllStandardOutput() == (member.startsWith("other/") ? "second parent" : "private payload"),
                "equal mixed-parent basenames keep distinct relative paths");
    }
    for (const QString &invalid : {QString(), QString("line\nbreak"), QString("line\rbreak"), QString(1100, 'a'), QString(100, 'a'), secret}) {
        finished = false;
        const int count = operations.operations().size();
        operations.compressEncryptedItems({fileUrl(secure)}, "zip", invalid);
        require(finished && !success && operations.operations().size() == count, "invalid password fails before launching a process");
    }
    finished = false;
    operations.compressEncryptedItems({fileUrl(secure)}, "tar", secret);
    require(finished && !success, "unsupported encryption cannot silently create a plaintext archive");
    // Keep encrypted cancellation on the same native/in-app control path.
    finished = false;
    operations.compressEncryptedItems({fileUrl(source)}, "zip", "Long ASCII ; $(test) 123!");
    const int encryptedCancelId = operations.operations().last().toMap().value("id").toInt();
    require(until([&] { return operations.operations().last().toMap().value("processedBytes").toULongLong() > 0 || finished; })
            && !finished, "encrypted cancel fixture is writing");
    require(operations.toggleOperationPause(encryptedCancelId), "encrypted job pauses");
    require(operations.cancelOperation(encryptedCancelId), "paused encrypted job cancels");
    require(until([&] { return finished; }) && !success && !QFileInfo::exists(source + ".zip"), "cancelled encrypted output is not published");
    require(QDir(root).entryList({".lurviko-archive-*"}, QDir::AllEntries | QDir::Hidden).isEmpty(), "encrypted staging is removed after completion/cancellation");
    require(QFileInfo(source + "/Büyük öğe.bin").size() == 96LL * 1024 * 1024, "encrypted cancellation preserves inputs");
    std::fprintf(stderr, "PASS: localized native and integrated archive progress, live counts/bytes/speed, pause/resume, valid archives and cancellation\n");
}
