#include <QCoreApplication>
#include <QEventLoop>
#include <QCryptographicHash>
#include <QFile>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QStorageInfo>
#include <QSaveFile>
#include <QUrl>
#include <QPointer>
#include <QDateTime>
#include <QtConcurrent>
#include <sys/resource.h>
#if defined(Q_OS_LINUX)
#include <sys/xattr.h>
#endif
#include <cstdio>
#define private public
#include "contentindexmodel.h"
#undef private
#include "index_source.cpp"

static void makeFile(const QString &path, int bytes = 32768) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray(bytes, 'x')) != bytes)
        qFatal("Cannot create fixture");
}
// Writes the desktop's shared recent list (as GTK/Chrome/KDE apps do), using
// an atomic replace like real writers. Each entry: path + age in days.
static void writeRecentlyUsed(const QList<QPair<QString, double>> &entries) {
    QString xml = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<xbel version=\"1.0\" xmlns:bookmark=\"http://www.freedesktop.org/standards/desktop-bookmarks\" "
        "xmlns:mime=\"http://www.freedesktop.org/standards/shared-mime-info\">\n");
    const QDateTime now = QDateTime::currentDateTimeUtc();
    for (const auto &entry : entries) {
        const QString when = now.addMSecs(qint64(-entry.second * 86400000.0)).toString(Qt::ISODateWithMs);
        xml += QStringLiteral("  <bookmark href=\"%1\" added=\"%2\" modified=\"%2\" visited=\"%2\">"
                              "<info><metadata owner=\"http://freedesktop.org\"><mime:mime-type type=\"application/octet-stream\"/>"
                              "<bookmark:applications><bookmark:application name=\"Test\" exec=\"&apos;test %u&apos;\" "
                              "modified=\"%2\" count=\"1\"/></bookmark:applications></metadata></info></bookmark>\n")
                   .arg(QString::fromUtf8(QUrl::fromLocalFile(entry.first).toEncoded()).toHtmlEscaped(), when);
    }
    xml += QStringLiteral("</xbel>\n");
    const QString path = ContentIndexModel::recentlyUsedPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(xml.toUtf8()) < 0 || !file.commit())
        qFatal("Cannot write recently-used.xbel fixture");
}
static double cpuSeconds() {
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6
        + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
#ifndef BASELINE
    // Synthetic mount boundaries: home-wide categories must stay on the home
    // device, whereas explicit roots on a removable filesystem are allowed.
    if (categoryRootPermitsMount("/home/test", "/home/test/USB")) return 34;
    if (!categoryRootPermitsMount("/home/test/USB", "/home/test/USB")) return 35;
    if (!categoryRootPermitsMount("/home/test/USB/Music", "/home/test/USB")) return 36;
    if (categoryRootPermitsMount("/home/test/USB-old", "/home/test/USB")) return 37;
#endif
    if (argc > 1) {
        // Read real preferences and scan without creating a model, registering
        // watchers or touching the user's settings/cache.
        QSettings settings(QString::fromLocal8Bit(argv[1]), QSettings::IniFormat);
        QHash<QString, QStringList> roots, ignores;
        for (const QString &key : allCategoryKeys()) {
            roots[key] = settings.value("contentIndex/mediaRoots/" + key, QStringList{QDir::homePath()}).toStringList();
            ignores[key] = settings.value("contentIndex/ignoreRules/" + key, defaultIgnoreRulesForCategory(key)).toStringList();
        }
        const QStringList hidden = settings.value("contentIndex/indexHiddenCategories").toStringList();
        const QSet<QString> hiddenSet(hidden.begin(), hidden.end());
        QElapsedTimer clock; clock.start(); double cpu = cpuSeconds();
        auto cancel = std::make_shared<std::atomic_bool>(false);
        const auto result = ContentIndexModel::scanTree(QDir::homePath(), hiddenSet, roots, ignores, {},
            [&](int, int, int, int, const QHash<QString, int>&) { if (clock.elapsed() > 60000) cancel->store(true); }, cancel);
        std::fprintf(stderr, "Real home: %d files, %d directories, %lld ms, %.3f CPU seconds\n",
            int(result.files.size()), int(result.directories.size()), clock.elapsed(), cpuSeconds() - cpu);
        for (auto it = result.counts.constBegin(); it != result.counts.constEnd(); ++it)
            std::fprintf(stderr, "  %s: %d\n", qPrintable(it.key()), it.value());
        QStringList paths = result.files.keys(); paths.sort();
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const QString &path : paths) {
            const auto item = result.files.value(path);
            hash.addData((path + QLatin1Char('\0') + item.category + QLatin1Char('\0')
                          + QString::number(item.size) + QLatin1Char('\0')
                          + QString::number(item.modifiedMs) + QLatin1Char('\n')).toUtf8());
        }
        std::fprintf(stderr, "  Result digest: %s\n", hash.result().toHex().constData());
        return cancel->load() ? 1 : 0;
    }
    // Recent Files mirror the desktop's shared recently-used.xbel: anything an
    // application (browser, KDE Connect, editors) saved or opened, filtered by
    // Recent's roots, ignore rules, hidden policy, file type and time window.
    {
        QTemporaryDir recentRoot;
        const QString base = recentRoot.path();
        QTemporaryDir elsewhere;
        const QString recentDoc = base + "/Belgeler/VÜCUT ÖLÇÜM #1.pdf";
        const QString oldDoc = base + "/old.pdf";
        const QString code = base + "/script.py";
        const QString hidden = base + "/.secret/hidden.pdf";
        const QString outside = elsewhere.path() + "/outside.pdf";
        const QString deleted = base + "/deleted.pdf";
        const QString ignored = base + "/build/ignored.pdf";
        const QString newer = base + "/photo.jpg";
        for (const QString &path : {recentDoc, oldDoc, code, hidden, outside, ignored, newer})
            makeFile(path);
        writeRecentlyUsed({{recentDoc, 1}, {oldDoc, 40}, {code, 1}, {hidden, 1}, {outside, 1},
                           {deleted, 1}, {ignored, 1}});
        app.setOrganizationName("Lurviko-QA-Recent");
        app.setApplicationName("Index");
        QSettings settings;
        for (const QString &key : allCategoryKeys())
            settings.setValue("contentIndex/mediaRoots/" + key, key == "recent" ? QStringList{base} : QStringList{});
        settings.setValue("contentIndex/ignoreRules/recent", QStringList{"path:build"});
        settings.setValue("contentIndex/recentWindowDays", 30);
        settings.sync();
        ContentIndexModel model;
        const auto waitRecent = [&](int expected) {
            QElapsedTimer clock; clock.start();
            while (model.countForCategory("recent") != expected && clock.elapsed() < 5000) {
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
                QThread::msleep(10);
            }
            return model.countForCategory("recent") == expected;
        };
        if (!waitRecent(1) || !model.m_recentFiles.contains(recentDoc)) return 38;
        // Refreshing Recent re-reads the list; it never walks directories.
        model.refreshCategory("recent");
        if (model.indexing() || model.m_scanRunning) return 39;
        // Another application records a newer file: picked up without a rescan.
        writeRecentlyUsed({{recentDoc, 1}, {newer, 0.01}, {oldDoc, 40}});
        if (!waitRecent(2) || !model.m_recentFiles.contains(newer)) return 40;
        const QVariantList ordered = model.filesForCategory("recent");
        if (ordered.isEmpty() || ordered.first().toMap().value("path").toString() != newer) return 41;
        // A wider window includes the 40-day-old entry.
        model.setRecentWindowDays(90);
        if (!waitRecent(3) || !model.m_recentFiles.contains(oldDoc)) return 42;
        // Files deleted after being recorded disappear on the next read.
        if (!QFile::remove(newer)) return 43;
        model.refreshCategory("recent");
        if (!waitRecent(2)) return 44;
        // Writers cap the shared list; Lurviko's history keeps evicted entries.
        writeRecentlyUsed({{recentDoc, 1}});
        model.refreshCategory("recent");
        QThread::msleep(300);
        if (!waitRecent(2) || !model.m_recentFiles.contains(oldDoc)) return 45;
        // "Today" only shows today's use.
        model.setRecentWindowDays(1);
        if (!waitRecent(0)) return 46;
        model.setRecentWindowDays(90);
        if (!waitRecent(2)) return 47;
        // Remove from Recent hides the entry without touching the file.
        model.removeFromRecent({QUrl::fromLocalFile(recentDoc).toString()});
        if (model.countForCategory("recent") != 1 || !QFileInfo::exists(recentDoc)) return 48;
        QThread::msleep(300);
        model.refreshCategory("recent");
        QThread::msleep(300);
        if (!waitRecent(1) || model.m_recentFiles.contains(recentDoc)) return 49;
        {
            ContentIndexModel restarted; // the hide survives a restart
            QElapsedTimer clock; clock.start();
            while (restarted.countForCategory("recent") != 1 && clock.elapsed() < 5000)
                QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (restarted.countForCategory("recent") != 1 || restarted.m_recentFiles.contains(recentDoc)) return 50;
        }
        // Using the file again brings it back.
        QThread::msleep(20);
        writeRecentlyUsed({{recentDoc, 0}});
        if (!waitRecent(2) || !model.m_recentFiles.contains(recentDoc)) return 51;
        app.setOrganizationName("Lurviko-QA");
        app.setApplicationName("Index");
    }
    QTemporaryDir temp;
    const QString base = temp.path();
    // A broad visible documents category and hidden archives category coexist
    // with narrower music/images roots, like the reported real configuration.
    QHash<QString, QStringList> roots{
        {"documents", {base}}, {"archives", {base}}, {"images", {base + "/.sync/Dcim"}},
        {"music", {base + "/music"}}, {"videos", {base + "/videos"}}, {"appimage", {base + "/apps"}}};
    QHash<QString, QStringList> ignores;
    for (const QString &key : allCategoryKeys()) ignores[key] = defaultIgnoreRulesForCategory(key);
    // Also exercise extension/name, case-insensitive and wildcard path rules.
    ignores["images"] << "EXT:.JPG" << "name:omit";
    ignores["documents"] << "*.pdf" << "name:skip";
    const QSet<QString> hidden{"archives", "appimage", "images", "music", "videos"};
    for (int i = 0; i < 1200; ++i) {
        const QString dir = base + QString("/.projects/npm/package-%1").arg(i);
        makeFile(dir + "/readme.txt");
        makeFile(dir + "/bundle.zip");
        makeFile(dir + "/picture.png");
        makeFile(dir + "/source.js");
    }
    // node_modules is excluded even when nested in a visible folder.
    makeFile(base + "/public/node_modules/package/ignore.zip");
    makeFile(base + "/public/node_modules/package/ignore.txt");
    makeFile(base + "/readme.txt");
    makeFile(base + "/skip.txt");
    makeFile(base + "/manual.pdf");
    makeFile(base + "/folder.zip");
    makeFile(base + "/.hidden/archive.zip");
    makeFile(base + "/.hidden/document.txt");
    makeFile(base + "/music/song.mp3");
    makeFile(base + "/music/.hidden/song.flac");
    makeFile(base + "/elsewhere/song.mp3");
    makeFile(base + "/.sync/Dcim/photo.png");
    makeFile(base + "/.sync/Dcim/omit.png");
    makeFile(base + "/.sync/Dcim/photo.JPG");
    makeFile(base + "/.sync/Dcim/ThumbNails/ignored.png");
    makeFile(base + "/.sync/Dcim/tiny.png", 100);
    makeFile(base + "/videos/movie.mkv", 1048576);
    makeFile(base + "/videos/assets/ignored.mkv", 1048576);
    makeFile(base + "/apps/tool.AppImage");
    QFile::link(base + "/readme.txt", base + "/linked.txt");
    const QSet<QString> expected{base + "/readme.txt", base + "/folder.zip", base + "/.hidden/archive.zip",
        base + "/music/song.mp3", base + "/music/.hidden/song.flac", base + "/.sync/Dcim/photo.png",
        base + "/videos/movie.mkv", base + "/apps/tool.AppImage"};
    QElapsedTimer clock; clock.start(); double cpu = cpuSeconds();
    const auto result = ContentIndexModel::scanTree(base, hidden, roots, ignores);
    const auto keys = result.files.keys(); const QSet<QString> actual(keys.begin(), keys.end());
    if (actual != expected) {
        for (const QString &path : actual - expected) std::fprintf(stderr, "Unexpected: %s\n", qPrintable(path));
        for (const QString &path : expected - actual) std::fprintf(stderr, "Missing: %s\n", qPrintable(path));
        return 2;
    }
    std::fprintf(stderr, "Mixed roots: %d files, %d directories, %lld ms, %.3f CPU seconds\n",
        int(result.files.size()), int(result.directories.size()), clock.elapsed(), cpuSeconds() - cpu);
#ifndef BASELINE
    for (const QString &path : result.directories) {
        if (path.contains("/npm") || path.contains("/node_modules")) return 3;
    }
#endif
    // When only the images category scans, unrelated directories must not be
    // visited; path boundaries must distinguish Dcim from Dcim-extra.
    makeFile(base + "/.sync/Dcim-extra/no.png");
    const auto images = ContentIndexModel::scanTree(base, hidden, {{"images", roots["images"]}},
                                                   {{"images", ignores["images"]}}, {"images"});
    if (images.files.size() != 1 || !images.files.contains(base + "/.sync/Dcim/photo.png")) return 4;
#ifndef BASELINE
    if (images.directories.size() != 3) return 5; // base, .sync, Dcim
#endif
    std::fprintf(stderr, "Image-only scan: %d directories; filtering and root boundaries passed\n", int(images.directories.size()));
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Index");
    QSettings settings;
    settings.setValue("contentIndex/indexHiddenCategories", hidden.values());
    for (const QString &key : allCategoryKeys()) {
        settings.setValue("contentIndex/mediaRoots/" + key, roots.value(key));
        settings.setValue("contentIndex/ignoreRules/" + key, ignores.value(key));
    }
    settings.sync();
    const auto waitFor = [&](const std::function<bool()> &predicate) {
        QElapsedTimer timer; timer.start();
        while (!predicate() && timer.elapsed() < 10000) {
            QEventLoop loop; QTimer::singleShot(15, &loop, &QEventLoop::quit); loop.exec();
        }
        return predicate();
    };
    {
        ContentIndexModel model;
        DirectorySizeIndex::instance()->setWatchedDirectories(&model, {base + "/.sync/Dcim"});
        model.refresh();
        if (!waitFor([&] { return !model.indexing() && model.totalIndexed() == expected.size(); })) return 6;
        { QEventLoop loop; QTimer::singleShot(150, &loop, &QEventLoop::quit); loop.exec(); }
        makeFile(base + "/.sync/Dcim/new.png");
        if (!waitFor([&] { return !model.indexing() && model.countForCategory("images") == 2; })) return 7;
        if (!QFile::remove(base + "/.sync/Dcim/new.png")) return 8;
        if (!waitFor([&] { return !model.indexing() && model.countForCategory("images") == 1; })) return 9;
        cpu = cpuSeconds();
        { QEventLoop loop; QTimer::singleShot(2000, &loop, &QEventLoop::quit); loop.exec(); }
        const double idleCpu = cpuSeconds() - cpu;
        std::fprintf(stderr, "Automatic add/delete updates passed; 2s idle used %.4f CPU seconds\n", idleCpu);
        if (idleCpu > 0.1) return 10;
    }

    // Separate roots reproduce the per-root progress that used to overwrite
    // every Home card with zeros, even after an unchanged scan had finished.
    app.setApplicationName("IndexCounts");
    QTemporaryDir separate;
    QSettings countSettings;
    const QHash<QString, QString> suffixes{{"images", ".png"}, {"videos", ".mkv"},
        {"music", ".mp3"}, {"documents", ".txt"}, {"archives", ".zip"}, {"appimage", ".AppImage"}};
    for (auto it = suffixes.constBegin(); it != suffixes.constEnd(); ++it) {
        const QString root = separate.path() + "/" + it.key();
        makeFile(root + "/sample" + it.value(), it.key() == "videos" ? 1048576 : 32768);
        countSettings.setValue("contentIndex/mediaRoots/" + it.key(), QStringList{root});
    }
    // The Recent category defaults to the real home directory if no root is
    // configured. That makes the otherwise hermetic count test depend on
    // downloads the developer has just made (and can turn 0 into 1). Keep it
    // inside our temporary fixture, with one positive Recent membership
    // recorded in the (isolated) recently-used.xbel.
    // Keep this root outside `separate`: later directory-size assertions
    // intentionally count only the six primary-category fixtures there.
    QTemporaryDir recentSeparate;
    const QString recentRoot = recentSeparate.path();
    const QString recentFixture = recentRoot + "/recent-download.pdf";
    makeFile(recentFixture);
    countSettings.setValue("contentIndex/mediaRoots/recent", QStringList{recentRoot});
    // The fixture lives beneath /tmp, which the production Recent rules
    // normally ignore. Disable only this category's ignore rules for the test.
    countSettings.setValue("contentIndex/ignoreRules/recent", QStringList{});
    writeRecentlyUsed({{recentFixture, 0.1}});
    const int expectedRecentCount = 1;
    countSettings.sync();
    ContentIndexModel counts;
    counts.refresh();
    if (!waitFor([&] { return !counts.indexing() && counts.totalIndexed() == 6; })) return 11;
    const auto stableCounts = [&] {
        for (int row = 0; row < counts.rowCount(); ++row) {
            const auto item = counts.index(row);
            const QString key = counts.data(item, ContentIndexModel::KeyRole).toString();
            const int expectedCount = key == "recent" ? expectedRecentCount : (key == "private" ? 0 : 1);
            const int committed = counts.countForCategory(key);
            const int cardCount = counts.data(item, ContentIndexModel::CountRole).toInt();
            if (committed != expectedCount || cardCount != expectedCount)
                qFatal("Committed category count changed during an unchanged scan: %s (expected=%d, model=%d, card=%d)",
                       qPrintable(key), expectedCount, committed, cardCount);
        }
    };
    // Verify the *initial* committed state, not just intermediate progress.
    stableCounts();
    int progressUpdates = 0, countNotifications = 0, fileNotifications = 0;
    auto progress = QObject::connect(&counts, &ContentIndexModel::scanProgressChanged, [&] {
        if (counts.scanProgressVisible()) { ++progressUpdates; stableCounts(); }
    });
    QObject::connect(&counts, &ContentIndexModel::dataChanged, [&](const QModelIndex &, const QModelIndex &, const QList<int> &roles) {
        if (roles.contains(ContentIndexModel::CountRole)) ++countNotifications;
    });
    QSet<QString> changedCategories;
    QObject::connect(&counts, &ContentIndexModel::categoryFilesChanged, [&](const QStringList &keys) {
        ++fileNotifications;
        for (const auto &key : keys) changedCategories.insert(key);
    });
    counts.refresh();
    if (!waitFor([&] { return !counts.indexing(); })) return 12;
    stableCounts();
    if (progressUpdates < 6 || countNotifications != 0 || fileNotifications != 0) return 13;
    progressUpdates = 0;
    counts.refreshCategory("images");
    if (!waitFor([&] { return !counts.indexing(); })) return 14;
    stableCounts();
    if (progressUpdates == 0 || countNotifications != 0 || fileNotifications != 0) return 15;
    QObject::disconnect(progress);
    // Deletion is reconciled through the existing directory watcher; unrelated
    // category views must not be torn down or have their counts cleared.
    DirectorySizeIndex::instance()->setWatchedDirectories(&counts, {separate.path() + "/images"});
    if (!waitFor([&] { return DirectorySizeIndex::instance()->activeWatchCount() > 0; })) return 16;
    if (!QFile::remove(separate.path() + "/images/sample.png")) return 17;
    if (!waitFor([&] { return !counts.indexing() && counts.countForCategory("images") == 0; })) return 18;
    if (changedCategories != QSet<QString>{"images"}) return 19;
    for (const auto &key : suffixes.keys())
        if (key != "images" && counts.countForCategory(key) != 1) return 20;
#ifndef BASELINE
    // An unrelated watched volume must not schedule a category scan. A change
    // to an ancestor of explicitly configured roots must target those roots,
    // rather than attempting a recursive scan of the ancestor volume.
    const auto pendingBefore = counts.m_pendingRoots;
    DirectorySizeIndex::instance()->directoryChanged(base + "/unrelated-volume");
    if (counts.m_pendingRoots != pendingBefore) return 30;
    DirectorySizeIndex::instance()->directoryChanged(separate.path());
    for (const auto &key : suffixes.keys()) {
        if (!counts.m_pendingRoots.contains(separate.path() + "/" + key)) return 31;
    }
    if (counts.m_pendingRoots.contains(separate.path())) return 32;
    if (!waitFor([&] { return !counts.indexing() && counts.countForCategory("music") == 1; })) return 33;
#endif
    std::fprintf(stderr, "Committed multi-root counts, unchanged refreshes and category-local deletion passed\n");
#ifndef BASELINE
    // A category preference change removes memberships, not filesystem bytes.
    // Verify the real producer feeds hints to the shared size service without
    // converting filtered-out documents into nonexistent files.
    const QString excludedRoot = separate.path() + "/empty-documents";
    QDir().mkpath(excludedRoot);
    auto *sizes = DirectorySizeIndex::instance();
    QObject sizeReader; sizes->retain(&sizeReader, separate.path());
    sizes->request(separate.path());
    qint64 expectedBytes = 0;
    for (auto it = suffixes.constBegin(); it != suffixes.constEnd(); ++it) {
        const QFileInfo file(separate.path() + "/" + it.key() + "/sample" + it.value());
        if (file.isFile()) expectedBytes += file.size();
    }
    if (!waitFor([&] { return !sizes->busy() && sizes->snapshot(separate.path()).complete; })) return 21;
    if (sizes->snapshot(separate.path()).totals.bytes != expectedBytes) return 22;
    int sizeUpdates = 0;
    QObject::connect(sizes, &DirectorySizeIndex::workCompleted, &app, [&](qint64, qint64) { ++sizeUpdates; });
    if (!counts.setCategoryIndexSettings("documents", {excludedRoot}, false, {})) return 23;
    if (!waitFor([&] { return !counts.indexing() && counts.countForCategory("documents") == 0
                && !sizes->busy() && sizeUpdates > 0; })) return 24;
    if (sizes->snapshot(separate.path()).totals.bytes != expectedBytes) return 25;
    const QString unindexed = separate.path() + "/documents/unindexed.opaque";
    sizes->setWatchedDirectories(&sizeReader, {separate.path() + "/documents"});
    if (!waitFor([&] { return sizes->activeWatchCount() >= 2; })) return 29;
    makeFile(unindexed, 19);
    if (!waitFor([&] { return !sizes->busy() && sizes->snapshot(separate.path()).totals.bytes == expectedBytes + 19; })) return 26;
    if (!QFile::remove(unindexed)) return 27;
    if (!waitFor([&] { return !sizes->busy() && sizes->snapshot(separate.path()).totals.bytes == expectedBytes; })) return 28;
    std::fprintf(stderr, "Shared size updates, category-only removal safety and files outside the category index passed\n");
#endif
    return 0;
}
