#include "thumbnailprovider.h"

#include <KCompressionDevice>
#include <KIconColors>
#include <KIconLoader>
#include <KIconTheme>
#include <QBuffer>
#include <QCache>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QGuiApplication>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QRegularExpression>
#include <QScreen>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrent>

#include <atomic>
#include <memory>

namespace {

// Expose KDE's own SVG stylesheet rather than inventing different symbolic
// colors. Capture it on the GUI thread; only plain data enters the workers.
class IconColors : public KIconColors
{
public:
    using KIconColors::KIconColors;
    using KIconColors::stylesheet;
};

struct ResolvedIcon {
    QString path;
    QSize pixels;
    QByteArray stylesheet;
    QString key;
};

struct IconTask {
    QFuture<QImage> future;
    std::atomic_int subscribers{0};
};

struct IconRendering {
    QThreadPool *pool = nullptr;
    QMutex mutex;
    // This cache survives delegate/image-cache eviction across navigation.
    // Costs are KiB, so large themes cannot grow memory without a bound.
    QCache<QString, QImage> images{64 * 1024};
    QHash<QString, std::shared_ptr<IconTask>> pending;

    IconRendering()
    {
        // Process-lifetime pool: do not block application shutdown waiting for
        // decorative icon renders that are no longer visible.
        pool = new QThreadPool;
        pool->setMaxThreadCount(2);
        pool->setThreadPriority(QThread::LowPriority);
        pool->setExpiryTimeout(1500);
    }
};

IconRendering &iconRendering()
{
    static IconRendering rendering;
    return rendering;
}

QImage renderIcon(const ResolvedIcon &icon)
{
    const QString suffix = QFileInfo(icon.path).suffix().toLower();
    QByteArray svg;
    if (suffix == QStringLiteral("svgz")) {
        KCompressionDevice input(icon.path, KCompressionDevice::GZip);
        if (input.open(QIODevice::ReadOnly))
            svg = input.readAll();
    } else if (suffix == QStringLiteral("svg")) {
        QFile input(icon.path);
        if (input.open(QIODevice::ReadOnly))
            svg = input.readAll();
    }

    QBuffer buffer;
    if (!svg.isEmpty()) {
        QString document = QString::fromUtf8(svg);
        static const QRegularExpression colorStyle(
            QStringLiteral("<style\\b[^>]*\\bid\\s*=\\s*[\"']current-color-scheme[\"'][^>]*>.*?</style\\s*>"),
            QRegularExpression::DotMatchesEverythingOption);
        const auto match = colorStyle.match(document);
        if (match.hasMatch() && !icon.stylesheet.isEmpty()) {
            document.replace(match.capturedStart(), match.capturedLength(),
                             QStringLiteral("<style id=\"current-color-scheme\" type=\"text/css\">")
                                 + QString::fromUtf8(icon.stylesheet) + QStringLiteral("</style>"));
        }

        // Some icon themes export complex SVG filter graphs. QtSvg may create
        // a huge intermediate surface for those even when the requested icon
        // is tiny, producing "requested buffer size is too big" warnings.
        // Shadows are decorative here, so render a safe filter-free copy.
        static const QRegularExpression filterElement(
            QStringLiteral("<filter\\b[^>]*>.*?</filter\\s*>"),
            QRegularExpression::DotMatchesEverythingOption | QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression filterAttribute(
            QStringLiteral("\\sfilter\\s*=\\s*[\\\"'][^\\\"']*[\\\"']"),
            QRegularExpression::CaseInsensitiveOption);
        static const QRegularExpression filterStyle(
            QStringLiteral("filter\\s*:\\s*url\\(#[^)]+\\)\\s*;?"),
            QRegularExpression::CaseInsensitiveOption);
        document.remove(filterElement);
        document.remove(filterAttribute);
        document.remove(filterStyle);
        buffer.setData(document.toUtf8());
        buffer.open(QIODevice::ReadOnly);
    }

    QImageReader reader;
    if (buffer.isOpen()) {
        reader.setDevice(&buffer);
        reader.setFormat("svg");
    } else {
        reader.setFileName(icon.path);
    }
    const QSize intrinsic = reader.size();
    reader.setScaledSize(intrinsic.isValid()
                             ? intrinsic.scaled(icon.pixels, Qt::KeepAspectRatio)
                             : icon.pixels);
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (!image.isNull() && image.size() != icon.pixels)
        image = image.scaled(icon.pixels, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

ResolvedIcon resolveIcon(const QString &name, int logicalSize, const QSize &pixels)
{
    // KIconLoader and the platform icon engine are GUI-thread objects. Workers
    // receive only the resolved filename and stylesheet, never a QIcon/loader.
    KIconLoader *loader = KIconLoader::global();
    const qreal scale = qMax<qreal>(1, qreal(pixels.width()) / logicalSize);
    QString path = loader->iconPath(name, -logicalSize, true, scale);
    if (path.isEmpty() || !QFileInfo(path).isFile()) {
        const bool folder = name == QStringLiteral("folder")
            || name.startsWith(QStringLiteral("folder-"))
            || name.endsWith(QStringLiteral("-folder"));
        path = loader->iconPath(folder ? QStringLiteral("folder")
                                      : QStringLiteral("text-x-generic"), -logicalSize, false, scale);
    }
    const QByteArray stylesheet = loader->theme() && loader->theme()->followsColorScheme()
        ? IconColors(QGuiApplication::palette()).stylesheet(KIconLoader::DefaultState).toUtf8()
        : QByteArray();
    const QFileInfo info(path);
    const QString key = path + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch())
        + QLatin1Char('|') + QString::number(info.size())
        + QLatin1Char('|') + QString::number(pixels.width()) + QLatin1Char('x') + QString::number(pixels.height())
        + QLatin1Char('|') + QString::fromLatin1(QCryptographicHash::hash(stylesheet, QCryptographicHash::Sha256).toHex());
    return {path, pixels, stylesheet, key};
}

class SystemIconResponse final : public QQuickImageResponse
{
public:
    SystemIconResponse(QString name, int nativeSize, QSize requestedSize)
    {
        connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, [this] {
            m_image = m_watcher.result();
            releaseTask();
            emit finished();
        });

        // requestImageResponse may run on Qt's image-loading thread. Resolve
        // through KDE on the GUI thread without blocking that thread on SVG
        // rendering, then deliver the result back to this response's thread.
        QPointer<SystemIconResponse> response(this);
        QMetaObject::invokeMethod(qApp, [response, name, nativeSize, requestedSize] {
            if (!response)
                return;
            QSize pixels = requestedSize.isValid() ? requestedSize : QSize(128, 128);
            pixels.setWidth(qBound(1, pixels.width(), 512));
            pixels.setHeight(qBound(1, pixels.height(), 512));
            const qreal dpr = QGuiApplication::primaryScreen()
                ? QGuiApplication::primaryScreen()->devicePixelRatio() : 1;
            const int logicalSize = nativeSize > 0 ? nativeSize : qMax(1, qRound(pixels.width() / dpr));
            const ResolvedIcon icon = resolveIcon(name, logicalSize, pixels);
            QMetaObject::invokeMethod(response, [response, icon] {
                if (response)
                    response->load(icon);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
    }

    ~SystemIconResponse() override { releaseTask(); }

    void cancel() override
    {
        m_cancelled.store(true, std::memory_order_relaxed);
        releaseTask();
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return m_image.isNull() ? nullptr : QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return !m_cancelled.load(std::memory_order_relaxed) && m_image.isNull()
            ? QStringLiteral("No system icon available") : QString();
    }

private:
    void load(const ResolvedIcon &icon)
    {
        QMutexLocker subscriptionLock(&m_subscriptionMutex);
        if (m_cancelled.load(std::memory_order_relaxed) || icon.path.isEmpty()) {
            subscriptionLock.unlock();
            emit finished();
            return;
        }
        IconRendering &rendering = iconRendering();
        QMutexLocker lock(&rendering.mutex);
        if (const QImage *cached = rendering.images.object(icon.key)) {
            m_image = *cached;
            lock.unlock();
            subscriptionLock.unlock();
            emit finished();
            return;
        }
        // Grid/sidebar/hidden cache delegates often ask for the same artwork
        // simultaneously. Share one render instead of queuing duplicate work.
        auto pending = rendering.pending.constFind(icon.key);
        if (pending != rendering.pending.cend()) {
            m_task = *pending;
            m_task->subscribers.fetch_add(1, std::memory_order_relaxed);
        } else {
            m_task = std::make_shared<IconTask>();
            m_task->subscribers.store(1, std::memory_order_relaxed);
            m_task->future = QtConcurrent::run(rendering.pool, [icon, task = m_task] {
                // A rapidly dragged slider can queue many obsolete sizes.
                // Skip requests nobody still needs before starting SVG work.
                {
                    IconRendering &rendering = iconRendering();
                    QMutexLocker lock(&rendering.mutex);
                    if (task->subscribers.load(std::memory_order_relaxed) == 0) {
                        rendering.pending.remove(icon.key);
                        return QImage();
                    }
                }
                const QImage image = renderIcon(icon);
                IconRendering &rendering = iconRendering();
                QMutexLocker lock(&rendering.mutex);
                if (!image.isNull())
                    rendering.images.insert(icon.key, new QImage(image), qMax<qsizetype>(1, (image.sizeInBytes() + 1023) / 1024));
                rendering.pending.remove(icon.key);
                return image;
            });
            rendering.pending.insert(icon.key, m_task);
        }
        const QFuture<QImage> future = m_task->future;
        lock.unlock();
        subscriptionLock.unlock();
        m_watcher.setFuture(future);
    }

    void releaseTask()
    {
        QMutexLocker lock(&m_subscriptionMutex);
        if (m_task) {
            m_task->subscribers.fetch_sub(1, std::memory_order_relaxed);
            m_task.reset();
        }
    }

    QFutureWatcher<QImage> m_watcher;
    QImage m_image;
    std::atomic_bool m_cancelled{false};
    QMutex m_subscriptionMutex;
    std::shared_ptr<IconTask> m_task;
};

} // namespace

QQuickImageResponse *SystemIconProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    QString name = QUrl::fromPercentEncoding(id.toUtf8());
    int nativeSize = 0;
    const qsizetype separator = name.lastIndexOf(QLatin1Char('|'));
    if (separator > 0) {
        bool ok = false;
        const int size = name.mid(separator + 1).toInt(&ok);
        if (ok && size > 0) {
            nativeSize = qBound(1, size, 512);
            name = name.left(separator);
        }
    }
    return new SystemIconResponse(name, nativeSize, requestedSize);
}
