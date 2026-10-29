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
static double cpuSeconds() {
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    return usage.ru_utime.tv_sec + usage.ru_utime.tv_usec / 1e6
        + usage.ru_stime.tv_sec + usage.ru_stime.tv_usec / 1e6;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
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
    countSettings.sync();
    ContentIndexModel counts;
    counts.refresh();
    if (!waitFor([&] { return !counts.indexing() && counts.totalIndexed() == 6; })) return 11;
    const auto stableCounts = [&] {
        for (int row = 0; row < counts.rowCount(); ++row) {
            const auto item = counts.index(row);
            const QString key = counts.data(item, ContentIndexModel::KeyRole).toString();
            const int expectedCount = key == "private" ? 0 : 1;
            if (counts.countForCategory(key) != expectedCount
                    || counts.data(item, ContentIndexModel::CountRole).toInt() != expectedCount)
                qFatal("Committed category count changed during an unchanged scan: %s", qPrintable(key));
        }
    };
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
    if (!waitFor([&] { return counts.m_watcher.directories().contains(separate.path() + "/images"); })) return 16;
    if (!QFile::remove(separate.path() + "/images/sample.png")) return 17;
    if (!waitFor([&] { return !counts.indexing() && counts.countForCategory("images") == 0; })) return 18;
    if (changedCategories != QSet<QString>{"images"}) return 19;
    for (const auto &key : suffixes.keys())
        if (key != "images" && counts.countForCategory(key) != 1) return 20;
    std::fprintf(stderr, "Committed multi-root counts, unchanged refreshes and category-local deletion passed\n");
    return 0;
}
