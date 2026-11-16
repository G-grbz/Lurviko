#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QTimer>
#include <QQuickTextureFactory>
#include <QUrl>
#include <fcntl.h>
#include <cstdio>
#include <functional>
#include <memory>
#include "backend_test_types.h"

static void require(bool ok, const char *message) {
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms) {
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
static bool until(const std::function<bool()> &check) {
    QElapsedTimer timer; timer.start();
    while (!check() && timer.elapsed() < 8000) pause(10);
    return check();
}
static void write(const QString &path, const QByteArray &bytes) {
    QFile file(path); require(file.open(QIODevice::WriteOnly), "SVG fixture opens");
    require(file.write(bytes) == bytes.size(), "SVG fixture writes");
    file.close();
    const timespec times[] = {{1700000000, 0}, {1700000000, 0}};
    require(::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0) == 0,
            "fixture timestamp is preserved");
}
static QImage thumbnail(ThumbnailProvider &provider, const QString &path, const QString &revision, bool required = true) {
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(
        QUrl::toPercentEncoding(path) + '|' + revision, QSize(96, 96)));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, [&] { finished = true; });
    require(until([&] { return finished; }), "asynchronous SVG thumbnail finishes");
    if (required) {
        if (!response->errorString().isEmpty()) std::fprintf(stderr,"THUMBNAIL: %s: %s\n",qPrintable(path),qPrintable(response->errorString()));
        require(response->errorString().isEmpty(), "SVG thumbnail decodes");
    }
    std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
    if (required) require(bool(texture), "SVG texture exists");
    return texture ? texture->image() : QImage();
}

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Thumbnails");
    const QString base = QString::fromLocal8Bit(qgetenv("LURVIKO_THUMBNAIL_TEST_ROOT")) + "/files";
    QDir().mkpath(base);
    const QString left = base + "/left.svg", right = base + "/right.svg", temporary = base + "/swap.svg";
    write(left, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"96\"><path fill=\"#ff0000\" d=\"M10 48L80 10L80 86Z\"/></svg>");
    write(right, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"96\"><path fill=\"#0000ff\" d=\"M86 48L16 10L16 86Z\"/></svg>");
    require(QFileInfo(left).size() == QFileInfo(right).size()
            && QFileInfo(left).lastModified() == QFileInfo(right).lastModified(), "colliding SVG metadata fixture");
    DirectoryModel model; model.setLocation(base);
    const auto row = [&](const QString &path) { return model.indexOfUrl(QUrl::fromLocalFile(path).toString()); };
    require(until([&] { return !model.loading() && model.rowCount() == 2; }), "directory loads");
    const auto revision = [&](const QString &path) {
        return model.data(model.index(row(path), 0), DirectoryModel::PreviewRevisionRole).toString();
    };
    const QString beforeLeft = revision(left), beforeRight = revision(right);
    ThumbnailProvider provider;
    const QImage red = thumbnail(provider, left, beforeLeft);
    const QImage blue = thumbnail(provider, right, beforeRight);
    require(red.pixelColor(48, 48).red() > 240 && blue.pixelColor(48, 48).blue() > 240, "initial arrows differ");
    require(thumbnail(provider, left, beforeLeft) == red, "unchanged SVG reuses cached image");
    require(QFile::rename(left, temporary) && QFile::rename(right, left) && QFile::rename(temporary, right), "swap names");
    require(until([&] { return row(left) >= 0 && row(right) >= 0
                             && revision(left) != beforeLeft && revision(right) != beforeRight; }),
            "watcher updates both SVG revisions after swapping equal-size files");
    require(thumbnail(provider, left, revision(left)).pixelColor(48, 48).blue() > 240,
            "memory/disk cache follows renamed file content");
    require(thumbnail(provider, right, revision(right)).pixelColor(48, 48).red() > 240,
            "both renamed files render their actual content");
    const QString oldRevision = revision(left);
    write(left, "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"96\" height=\"96\"><path fill=\"#00ff00\" d=\"M86 48L16 10L16 86Z\"/></svg>");
    require(until([&] { return revision(left) != oldRevision; }), "same-size SVG edit updates its revision with preserved mtime");
    require(thumbnail(provider, left, revision(left)).pixelColor(48, 48).green() > 240,
            "same-size SVG edit invalidates cached pixels");
    std::puts("PASS: SVG name swaps and preserved-metadata edits invalidate previews; unchanged files reuse cache");
    const QString compressed = QFileInfo(base).absolutePath() + "/compressed-fixtures";
    const QString zippedLeft = compressed + "/left.svgz", zippedRight = compressed + "/right.SVGZ";
    model.setLocation(compressed);
    require(until([&] { return !model.loading() && model.rowCount() == 4; }), "compressed SVG directory loads");
    const QString beforeZipped = revision(zippedLeft);
    const QImage zippedRed = thumbnail(provider, zippedLeft, beforeZipped);
    require(!zippedRed.isNull() && zippedRed.pixelColor(48,48).red() > 240, "SVGZ decompresses and unsafe filters are stripped");
    require(thumbnail(provider,zippedLeft,beforeZipped)==zippedRed,"unchanged SVGZ reuses cache");
    require(thumbnail(provider,zippedRight,revision(zippedRight)).pixelColor(48,48).blue()>240,"uppercase SVGZ renders");
    const QString swap = compressed + "/temporary.svgz";
    require(QFile::rename(zippedLeft,swap) && QFile::rename(zippedRight,zippedLeft) && QFile::rename(swap,zippedRight),"SVGZ swap succeeds");
    require(until([&] { return revision(zippedLeft) != beforeZipped; }),"SVGZ swap changes preview identity");
    require(thumbnail(provider,zippedLeft,revision(zippedLeft)).pixelColor(48,48).blue()>240,"SVGZ cache follows swapped contents");
    QFile green(QFileInfo(base).absolutePath()+"/green.svgz");require(green.open(QIODevice::ReadOnly),"green SVGZ fixture opens");
    const QString oldZipped = revision(zippedLeft); const qint64 oldSize = QFileInfo(zippedLeft).size();
    write(zippedLeft,green.readAll());
    require(QFileInfo(zippedLeft).size()==oldSize,"SVGZ edit preserves size and mtime");
    require(until([&] { return revision(zippedLeft) != oldZipped; }),"SVGZ in-place edit invalidates revision");
    require(thumbnail(provider,zippedLeft,revision(zippedLeft)).pixelColor(48,48).green()>240,"SVGZ in-place edit invalidates disk and memory cache");
    require(thumbnail(provider,compressed+"/oversized.svgz","large",false).isNull(),"decompressed SVGZ is bounded to 8 MiB");
    require(thumbnail(provider,compressed+"/invalid.svgz","invalid",false).isNull(),"malformed SVGZ is rejected");
    std::puts("PASS: SVGZ previews, filter safety, cache reuse/invalidation, compressed-data bounds and malformed input");

    const QString formats = QFileInfo(base).absolutePath() + "/formats";
    const auto pixel = [&](const QString &name, int x = 48, int y = 48) {
        const QImage image = thumbnail(provider, formats + "/" + name, "formats");
        return image.pixelColor(qMin(x, image.width() - 1), qMin(y, image.height() - 1));
    };
    const auto isRed = [](const QColor &c) { return c.red() > 200 && c.green() < 70 && c.blue() < 70; };
    const auto missing = [&](const QString &name) {
        return thumbnail(provider, formats + "/" + name, "formats", false).isNull();
    };
    require(isRed(pixel("signed.apk")), "APK behind a signing block shows its highest-density launcher icon");
    require(isRed(pixel("bundle.xapk")), "XAPK shows its bundled icon");
    require(missing("broken.apk"), "malformed APK has no preview");
    require(isRed(pixel("absolute.desktop")), "desktop launcher with an absolute Icon= path");
    require(isRed(pixel("themed.desktop")), "desktop launcher Icon= name resolves through the icon theme");
    require(isRed(pixel("legacy.blend", 48, 15)) && pixel("legacy.blend", 48, 80).blue() > 200,
            "Blender thumbnail rows are flipped upright");
    require(missing("truncated.blend"), "truncated Blender thumbnail is rejected");
    require(isRed(pixel("model.3mf")), "3MF uses the thumbnail named by its package relationship");
    require(isRed(pixel("book.fb2")) && isRed(pixel("book.fb2.zip")), "FictionBook shows its cover page image");
    require(isRed(pixel("book.azw3")), "Mobipocket shows the EXTH cover record");
    require(isRed(pixel("comic.cbt")), "CBT comic shows its first page in natural order");
    if (qEnvironmentVariableIsSet("LURVIKO_TEST_HAS_7Z")) {
        require(isRed(pixel("comic.cb7")), "CB7 comic shows its first page");
        require(isRed(pixel("mislabelled.cbr")), "comic with a wrong extension falls back to an external extractor");
    }
    if (qEnvironmentVariableIsSet("LURVIKO_TEST_HAS_RAR"))
        require(isRed(pixel("comic.cbr")), "CBR comic is read through an installed RAR extractor");
    std::puts("PASS: APK/XAPK, launcher, Blender, 3MF, FB2, Mobipocket and comic archive previews");

    // freedesktop.org shared repository: written by Lurviko, reused from
    // other applications, ignored when stale.
    const QString rasters = QFileInfo(base).absolutePath() + "/rasters";
    QDir().mkpath(rasters);
    const auto solid = [](QColor color) { QImage image(64, 64, QImage::Format_RGB32); image.fill(color); return image; };
    const auto rasterFixture = [&](const QString &name, QColor color) {
        const QString path = rasters + "/" + name;
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        solid(color).save(&buffer, "PNG");
        write(path, bytes);
        return path;
    };
    const auto seedShared = [&](const QString &path, QColor color, const QString &mtime) {
        QImage image = solid(color);
        image.setText("Thumb::URI", QUrl::fromLocalFile(path).toEncoded());
        image.setText("Thumb::MTime", mtime);
        const QString file = ThumbnailCache::sharedFilePath(path);
        QDir().mkpath(QFileInfo(file).absolutePath());
        require(image.save(file, "PNG"), "shared thumbnail fixture is written");
    };
    const auto colorOf = [&](const QString &path) { return thumbnail(provider, path, "raster").pixelColor(32, 32); };

    const QString photo = rasterFixture("photo.png", Qt::red);
    require(isRed(colorOf(photo)), "raster image preview");
    const QImage stored(ThumbnailCache::sharedFilePath(photo));
    require(!stored.isNull(), "raster preview is stored in the shared x-large repository");
    require(stored.text("Thumb::MTime") == "1700000000"
            && stored.text("Thumb::URI") == QString::fromLatin1(QUrl::fromLocalFile(photo).toEncoded()),
            "shared thumbnail carries Thumb::URI and Thumb::MTime");
    const auto permissions = QFileInfo(ThumbnailCache::sharedFilePath(photo)).permissions();
    require(!(permissions & (QFileDevice::ReadGroup | QFileDevice::ReadOther)), "shared thumbnail is private (0600)");
    require(!QFileInfo::exists(ThumbnailProvider::cachedFilePath(photo)), "raster preview has no private duplicate");

    const QString foreign = rasterFixture("foreign.png", Qt::red);
    seedShared(foreign, Qt::blue, "1700000000");
    require(colorOf(foreign).blue() > 200, "a thumbnail made by another file manager is reused");

    const QString stale = rasterFixture("stale.png", Qt::red);
    seedShared(stale, Qt::green, "1");
    require(isRed(colorOf(stale)), "a shared thumbnail with an old Thumb::MTime is ignored");
    require(QImage(ThumbnailCache::sharedFilePath(stale)).text("Thumb::MTime") == "1700000000",
            "the stale shared thumbnail is replaced");

    const QString legacy = rasterFixture("legacy.png", Qt::red);
    QDir().mkpath(ThumbnailCache::privateDirectory());
    require(solid(Qt::yellow).save(ThumbnailProvider::cachedFilePath(legacy), "PNG"), "legacy private preview");
    const QColor migrated = colorOf(legacy);
    require(migrated.red() > 200 && migrated.green() > 200, "an existing private preview is still used");
    require(QFileInfo::exists(ThumbnailCache::sharedFilePath(legacy))
            && !QFileInfo::exists(ThumbnailProvider::cachedFilePath(legacy)),
            "an existing private raster preview moves to the shared repository");
    std::puts("PASS: shared thumbnail repository write, reuse, staleness and migration");

    // Files inside either repository are never previewed: that would write
    // into the folder being shown and re-trigger its listing forever.
    const QString insidePrivate = ThumbnailCache::privateDirectory() + "/0123.png";
    const QString insideShared = ThumbnailCache::sharedDirectory() + "/x-large/4567.png";
    for (const QString &path : {insidePrivate, insideShared}) {
        QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
        solid(Qt::red).save(&buffer, "PNG");
        write(path, bytes);
    }
    const auto entries = [] {
        return QDir(ThumbnailCache::privateDirectory()).count() + QDir(ThumbnailCache::sharedDirectory() + "/x-large").count();
    };
    const auto before = entries();
    PreviewSettings *settings = PreviewSettings::instance();
    require(settings->allows(photo, "png", "image/png", QFileInfo(photo).size()), "ordinary image may be previewed");
    for (const QString &path : {insidePrivate, insideShared}) {
        require(!settings->allows(path, "png", "image/png", QFileInfo(path).size()),
                "QML never requests previews inside a thumbnail repository");
        require(thumbnail(provider, path, "repository", false).isNull(),
                "the provider refuses files inside a thumbnail repository");
    }
    require(entries() == before, "previewing a repository folder writes nothing into it");
    std::puts("PASS: thumbnail repositories are excluded from previews");

    // Private cache maintenance: expiry, leftovers and the size cap (LRU).
    const QString pruned = QFileInfo(base).absolutePath() + "/prune";
    QDir().mkpath(pruned);
    const qint64 now = 1800000000;
    const auto aged = [&](const QString &name, int bytes, qint64 ageSecs) {
        const QString path = pruned + "/" + name;
        QFile file(path); require(file.open(QIODevice::WriteOnly), "maintenance fixture opens");
        file.write(QByteArray(bytes, 'x')); file.close();
        const timespec times[] = {{time_t(now - ageSecs), 0}, {time_t(now - ageSecs), 0}};
        require(::utimensat(AT_FDCWD, QFile::encodeName(path).constData(), times, 0) == 0, "maintenance fixture age");
        return path;
    };
    const qint64 day = 24 * 3600;
    const QString fresh = aged("fresh.png", 1024, 60);
    const QString expired = aged("expired.png", 1024, 100 * day);
    const QString leftover = aged("render-abc.ppm", 1024, 2 * 3600);
    const QString rendering = aged("render-new.ppm", 1024, 60);
    QStringList byAge;
    for (int age = 1; age <= 10; ++age)
        byAge << aged(QString("day%1.png").arg(age), 100 * 1024, age * day);
    const auto result = ThumbnailCache::maintain(pruned, 600 * 1024, 90, now);
    require(!QFileInfo::exists(expired), "previews unused for longer than the expiry are removed");
    require(!QFileInfo::exists(leftover) && QFileInfo::exists(rendering),
            "only abandoned render files are removed");
    require(QFileInfo::exists(fresh), "recently used previews survive the size cap");
    for (int age = 1; age <= 10; ++age)
        require(QFileInfo::exists(byAge[age - 1]) == (age <= 4), "least recently used previews go first");
    require(result.keptBytes <= 600 * 1024 / 10 * 8 && result.removedFiles == 8, "cache is pruned below 80 % of its cap");
    std::puts("PASS: private preview cache expiry, leftovers and size cap");

    // A mass delete in a large folder (what the first cache cleanup does to
    // the thumbnails folder) is applied as one reset, not row by row; a single
    // change still updates in place.
    const QString bulk = QFileInfo(base).absolutePath() + "/bulk";
    QDir().mkpath(bulk);
    for (int i = 0; i < 4000; ++i) {
        QFile file(QString("%1/%2.png").arg(bulk).arg(i, 5, 10, QChar('0')));
        require(file.open(QIODevice::WriteOnly), "bulk fixture");
    }
    DirectoryModel large; large.setLocation(bulk);
    require(until([&] { return !large.loading() && large.rowCount() == 4000; }), "large folder loads");
    int bulkResets = 0, removals = 0;
    QObject::connect(&large, &QAbstractItemModel::modelReset, [&] { ++bulkResets; });
    QObject::connect(&large, &QAbstractItemModel::rowsRemoved, [&] { ++removals; });
    for (int i = 100; i < 4000; ++i)
        QFile::remove(QString("%1/%2.png").arg(bulk).arg(i, 5, 10, QChar('0')));
    require(until([&] { return large.rowCount() == 100; }), "mass delete reaches the model");
    require(bulkResets >= 1 && removals == 0, "mass delete is one reset, not thousands of row removals");
    bulkResets = 0;
    QFile::remove(bulk + "/00050.png");
    require(until([&] { return large.rowCount() == 99; }), "single delete reaches the model");
    require(bulkResets == 0 && removals == 1, "a single delete is still an in-place row removal");
    std::puts("PASS: bulk folder changes reset once; small changes stay incremental");
    return 0;
}
