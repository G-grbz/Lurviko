#include <QApplication>
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
static QImage thumbnail(ThumbnailProvider &provider, const QString &path, const QString &revision) {
    std::unique_ptr<QQuickImageResponse> response(provider.requestImageResponse(
        QUrl::toPercentEncoding(path) + '|' + revision, QSize(96, 96)));
    bool finished = false;
    QObject::connect(response.get(), &QQuickImageResponse::finished, [&] { finished = true; });
    require(until([&] { return finished; }), "asynchronous SVG thumbnail finishes");
    require(response->errorString().isEmpty(), "SVG thumbnail decodes");
    std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
    require(bool(texture), "SVG texture exists");
    return texture->image();
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
    return 0;
}
