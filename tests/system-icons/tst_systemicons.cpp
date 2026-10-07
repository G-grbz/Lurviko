#include "thumbnailprovider.h"

#include <KCompressionDevice>
#include <KIconLoader>
#include <QGuiApplication>
#include <QFile>
#include <QImage>
#include <QPalette>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <memory>
#include <vector>

class SystemIconTests : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_directory;
    SystemIconProvider m_provider;

    QString write(const QString &name, const QByteArray &data)
    {
        const QString path = m_directory.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
            return {};
        return path;
    }

    QImage load(const QString &path, QSize size = QSize(64, 64))
    {
        std::unique_ptr<QQuickImageResponse> response(m_provider.requestImageResponse(
            QString::fromLatin1(QUrl::toPercentEncoding(path)) + QStringLiteral("%7C64"), size));
        QSignalSpy finished(response.get(), &QQuickImageResponse::finished);
        if (finished.isEmpty() && !finished.wait(10000))
            return {};
        std::unique_ptr<QQuickTextureFactory> texture(response->textureFactory());
        return texture ? texture->image() : QImage();
    }

    static QByteArray svg(const QByteArray &color)
    {
        return "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
               "<rect x=\"8\" y=\"8\" width=\"48\" height=\"48\" fill=\"" + color + "\"/></svg>";
    }

private slots:
    void svgResolutionAndTransparency()
    {
        const auto path = write("large.svg", svg("#bada55"));
        const QImage image = load(path, QSize(334, 334));
        QCOMPARE(image.size(), QSize(334, 334));
        QCOMPARE(image.pixelColor(167, 167), QColor("#bada55"));
        QCOMPARE(image.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(load(path, QSize(334, 334)), image);
    }

    void symbolicPaletteChanges()
    {
        const QByteArray symbolic =
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"64\" height=\"64\">"
            "<defs><style type=\"text/css\" id=\"current-color-scheme\">.ColorScheme-Text {color:#ffffff;}</style></defs>"
            "<path d=\"M0 0H64V64H0Z\" class=\"ColorScheme-Text\" style=\"fill:currentColor\"/></svg>";
        const auto path = write("symbolic.svg", symbolic);
        const QPalette previous = QGuiApplication::palette();
        QPalette palette = previous;
        palette.setColor(QPalette::WindowText, QColor("#abcdef"));
        QGuiApplication::setPalette(palette);
        QCOMPARE(load(path).pixelColor(32, 32), QColor("#abcdef"));
        palette.setColor(QPalette::WindowText, QColor("#123456"));
        QGuiApplication::setPalette(palette);
        QCOMPARE(load(path).pixelColor(32, 32), QColor("#123456"));
        QGuiApplication::setPalette(previous);
    }

    void compressedSvg()
    {
        const QString path = m_directory.filePath("compressed.svgz");
        KCompressionDevice file(path, KCompressionDevice::GZip);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto data = svg("#fe9876");
        QCOMPARE(file.write(data), data.size());
        file.close();
        QCOMPARE(load(path).pixelColor(32, 32), QColor("#fe9876"));
    }

    void rasterAspectRatio()
    {
        const QString path = m_directory.filePath("wide.png");
        QImage original(80, 40, QImage::Format_ARGB32);
        original.fill(QColor("#badbad"));
        QVERIFY(original.save(path));
        const auto result = load(path);
        QCOMPARE(result.size(), QSize(64, 32));
        QCOMPARE(result.pixelColor(32, 16), QColor("#badbad"));
    }

    void replacedFileInvalidatesCache()
    {
        const auto path = write("replace.svg", svg("red"));
        QCOMPARE(load(path).pixelColor(32, 32), QColor("red"));
        write("replace.svg", svg("#123456"));
        QCOMPARE(load(path).pixelColor(32, 32), QColor("#123456"));
    }

    void concurrentRequestsAndCancellation()
    {
        const auto path = write("concurrent.svg", svg("#deadbe"));
        const QString id = QString::fromLatin1(QUrl::toPercentEncoding(path)) + QStringLiteral("%7C64");
        std::vector<std::unique_ptr<QQuickImageResponse>> responses;
        std::vector<std::unique_ptr<QSignalSpy>> completed;
        for (int i = 0; i < 32; ++i) {
            responses.emplace_back(m_provider.requestImageResponse(id, QSize(256, 256)));
            completed.emplace_back(std::make_unique<QSignalSpy>(responses.back().get(), &QQuickImageResponse::finished));
            if (i % 2 == 0)
                responses.back()->cancel();
        }
        for (int i = 0; i < 32; ++i) {
            QTRY_VERIFY_WITH_TIMEOUT(!completed[i]->isEmpty(), 10000);
            if (i % 2 != 0) {
                std::unique_ptr<QQuickTextureFactory> texture(responses[i]->textureFactory());
                QVERIFY(texture);
                QCOMPARE(texture->image().pixelColor(128, 128), QColor("#deadbe"));
            }
        }
    }

    void missingIconFallback()
    {
        QVERIFY(!load("folder-gfile-no-such-icon").isNull());
        QVERIFY(!load("gfile-no-such-icon").isNull());
        QVERIFY(!load(m_directory.filePath("missing.svg")).isNull());
    }

    void closeEngineDuringLoading()
    {
        // Opening/closing a tab or the application can destroy the engine
        // while resolution callbacks or SVG workers are still outstanding.
        for (int i = 0; i < 6; ++i) {
            QQmlEngine engine;
            engine.addImageProvider("systemicon", new SystemIconProvider);
            QQmlComponent component(&engine);
            component.setData("import QtQuick\nImage { asynchronous: true; width: 300; height: 300; "
                              "sourceSize: Qt.size(300, 300); source: 'image://systemicon/folder' }", QUrl());
            std::unique_ptr<QObject> image(component.create());
            QVERIFY2(image, qPrintable(component.errorString()));
            QCoreApplication::processEvents();
            QCoreApplication::processEvents();
        }
        QTest::qWait(50);
    }
};

QTEST_MAIN(SystemIconTests)
#include "tst_systemicons.moc"
