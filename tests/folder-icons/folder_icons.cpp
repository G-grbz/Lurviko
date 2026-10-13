#include "bundlediconprovider.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImageReader>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <vector>

static void require(bool value, const char *message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    FolderIconProvider provider;
    QStringList names{QStringLiteral("folder.svg")};
    for (const char *color : {"blue", "green", "orange", "pink", "purple", "red", "turquoise", "yellow"})
        for (const char *kind : {"folder", "documents", "download", "music", "picture", "video"})
            names << QString::fromLatin1(color) + QLatin1Char('-') + QString::fromLatin1(kind) + QStringLiteral(".svg");
    QElapsedTimer timer;
    timer.start();
    for (const QString &name : names) {
        QSize size;
        const QImage image = provider.requestImage(name, &size, QSize(288, 288));
        require(!image.isNull() && image.size() == size && size.width() <= 288 && size.height() <= 288,
                "All 49 bundled folder variants must render at requested physical extent");
    }
    const double coldMs = timer.nsecsElapsed() / 1e6;
    timer.restart();
    for (const QString &name : names)
        require(!provider.requestImage(name, nullptr, QSize(288, 288)).isNull(), "Warm navigation must render");
    const double warmMs = timer.nsecsElapsed() / 1e6;
    // Match direct SVG rendering at a baked size, including all original colors.
    for (const QString &name : names) {
        QImageReader reader(QStringLiteral(":/qt/qml/Lurviko/App/assets/icons/") + name);
        reader.setScaledSize(reader.size().scaled(QSize(128, 128), Qt::KeepAspectRatio));
        const QImage original = reader.read().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        const QImage actual = provider.requestImage(name, nullptr, QSize(128, 128));
        require(!original.isNull() && original.size() == actual.size(), "Preserve SVG aspect ratio");
        qint64 error = 0;
        for (int y = 0; y < actual.height(); ++y) {
            const auto *a = reinterpret_cast<const QRgb *>(actual.constScanLine(y));
            const auto *b = reinterpret_cast<const QRgb *>(original.constScanLine(y));
            for (int x = 0; x < actual.width(); ++x)
                error += std::abs(qRed(a[x]) - qRed(b[x])) + std::abs(qGreen(a[x]) - qGreen(b[x]))
                    + std::abs(qBlue(a[x]) - qBlue(b[x])) + std::abs(qAlpha(a[x]) - qAlpha(b[x]));
        }
        require(error < qint64(actual.width()) * actual.height() * 4, "Keep original colors/alpha (PNG rounding only)");
        const QImage again = provider.requestImage(name, nullptr, QSize(128, 128));
        require(actual.cacheKey() == again.cacheKey(), "Warm requests reuse pixels instead of decoding again");
    }
    for (const QString &name : {QStringLiteral("../folder.svg"), QStringLiteral("blue-folder.svg|red"),
                                QStringLiteral("file:///tmp/folder.svg"), QStringLiteral("not-a-folder.svg")})
        require(provider.requestImage(name, nullptr, QSize(128, 128)).isNull(), "Reject paths and tint arguments");
    require(provider.requestImage("folder.svg", nullptr, QSize(10000, 10000)).width() <= 512, "Bound transient huge sizes");
    require(provider.requestImage("folder.svg", nullptr, QSize(1, 1)).pixelColor(0, 0).alpha() == 0,
            "Skip temporary one-pixel layout artwork");
    std::vector<std::future<QImage>> requests;
    for (int i = 0; i < 8; ++i)
        requests.emplace_back(std::async(std::launch::async, [&provider] {
            return provider.requestImage("blue-picture.svg", nullptr, QSize(257, 257));
        }));
    const QImage shared = requests.front().get();
    for (int i = 1; i < 8; ++i)
        require(requests[i].get().cacheKey() == shared.cacheKey(), "Concurrent requests share one cached render");
    std::printf("PASS: 49 original-color variants, immediate cache reuse, bounds, concurrent loading\n"
                "49 icons at 288px: cold %.2f ms; warm %.2f ms\n", coldMs, warmMs);
}
