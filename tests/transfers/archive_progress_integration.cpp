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
    std::fprintf(stderr, "PASS: localized native and integrated archive progress, live counts/bytes/speed, pause/resume, valid archives and cancellation\n");
}
