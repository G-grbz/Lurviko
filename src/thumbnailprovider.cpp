#include "thumbnailprovider.h"
#include "svgpreviewidentity.h"

#include <QCryptographicHash>
#include <QCache>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImage>
#include <QImageReader>
#include <QFile>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QPainterPath>
#include <QIcon>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QRegularExpression>
#include <QPixmap>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QThreadPool>
#include <QUrl>
#include <QtConcurrent>
#include <QtEndian>

#include <atomic>
#include <memory>


namespace {

struct ThumbnailPoolHolder {
    QThreadPool pool;

    ThumbnailPoolHolder()
    {
        // Thumbnail generation can involve image decoding, ID3 parsing and
        // PNG encoding. Keeping it off QtConcurrent's global pool prevents a
        // fast scroll through a media-heavy folder from saturating every CPU
        // worker and starving the QML render/input threads.
        // Four workers keep the currently visible row responsive even when a
        // couple of video previews are waiting on external decoders. Stale
        // delegate requests are cancelled below, so fast scrolling no longer
        // leaves a long queue of off-screen work behind it.
        pool.setMaxThreadCount(4);
        pool.setExpiryTimeout(3000);
    }
};

QThreadPool *thumbnailPool()
{
    static ThumbnailPoolHolder holder;
    return &holder.pool;
}

struct ThumbnailMemoryCache {
    QMutex mutex;
    // QCache costs are KiB here. Keep final, view-sized images instead of the
    // larger 384/512 px source cache so repeated directory visits stay cheap.
    QCache<QString, QImage> images{64 * 1024};
};

ThumbnailMemoryCache &thumbnailMemoryCache()
{
    static ThumbnailMemoryCache cache;
    return cache;
}

QImage cachedMemoryThumbnail(const QString &key)
{
    ThumbnailMemoryCache &cache = thumbnailMemoryCache();
    QMutexLocker locker(&cache.mutex);
    if (const QImage *image = cache.images.object(key))
        return *image;
    return {};
}

void storeMemoryThumbnail(const QString &key, const QImage &image)
{
    if (image.isNull())
        return;
    ThumbnailMemoryCache &cache = thumbnailMemoryCache();
    const int costKiB = qMax<qsizetype>(1, image.sizeInBytes() / 1024);
    QMutexLocker locker(&cache.mutex);
    cache.images.insert(key, new QImage(image), costKiB);
}

bool waitForThumbnailProcess(QProcess &process,
                             const std::shared_ptr<std::atomic_bool> &cancelled,
                             int timeoutMs)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (process.state() != QProcess::NotRunning) {
        if (cancelled->load(std::memory_order_relaxed)) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        const int remaining = timeoutMs - int(elapsed.elapsed());
        if (remaining <= 0) {
            process.kill();
            process.waitForFinished(1000);
            return false;
        }
        process.waitForFinished(qMin(100, remaining));
    }
    return true;
}

QImage safeSvgThumbnail(const QString &path, const QSize &maximumSize)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    // SVG filters can request enormous intermediate surfaces even when the
    // final thumbnail is small. For file-manager thumbnails, render a safe
    // copy without filters; the original file is never modified.
    QByteArray data = file.read(8 * 1024 * 1024 + 1);
    if (data.isEmpty() || data.size() > 8 * 1024 * 1024)
        return {};

    QString document = QString::fromUtf8(data);
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

    QByteArray safeData = document.toUtf8();
    QBuffer buffer(&safeData);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};

    QImageReader reader(&buffer, QByteArrayLiteral("svg"));
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    const QSize bounded = source.isValid()
        ? source.scaled(maximumSize, Qt::KeepAspectRatio)
        : maximumSize;
    reader.setScaledSize(QSize(qBound(1, bounded.width(), maximumSize.width()),
                               qBound(1, bounded.height(), maximumSize.height())));
    return reader.read();
}

QImage scaledVideoCover(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    QImage cover = reader.read();
    if (!cover.isNull() && (cover.width() > 512 || cover.height() > 512))
        cover = cover.scaled(QSize(512, 512), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return cover;
}

QImage roundedThumbnail(const QImage &image)
{
    if (image.isNull())
        return {};

    QImage rounded(image.size(), QImage::Format_ARGB32_Premultiplied);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    const qreal radius = qBound<qreal>(4.0, qMin(image.width(), image.height()) / 18.0, 18.0);
    QRectF bounds = rounded.rect();
    bounds.adjust(0.5, 0.5, -0.5, -0.5);
    QPainterPath clip;
    clip.addRoundedRect(bounds, radius, radius);
    painter.setClipPath(clip);
    painter.drawImage(0, 0, image);
    return rounded;
}

bool validSquashfsSuperblock(QFile &file, qint64 offset)
{
    if (offset < 0 || offset + 48 > file.size())
        return false;
    const qint64 resumeAt = file.pos();
    if (!file.seek(offset))
        return false;
    const QByteArray header = file.read(48);
    file.seek(resumeAt);
    if (header.size() < 48 || header.first(4) != QByteArrayLiteral("hsqs"))
        return false;

    const auto *bytes = reinterpret_cast<const uchar *>(header.constData());
    const quint32 blockSize = qFromLittleEndian<quint32>(bytes + 12);
    const quint16 blockLog = qFromLittleEndian<quint16>(bytes + 22);
    const quint16 major = qFromLittleEndian<quint16>(bytes + 28);
    const quint16 minor = qFromLittleEndian<quint16>(bytes + 30);
    const quint64 bytesUsed = qFromLittleEndian<quint64>(bytes + 40);
    return major == 4 && minor == 0
        && blockSize >= 4096 && blockSize <= 1024 * 1024
        && (blockSize & (blockSize - 1)) == 0
        && blockLog >= 12 && blockLog <= 20
        && bytesUsed >= 96 && bytesUsed <= quint64(file.size() - offset);
}

qint64 appImageSquashfsOffset(const QString &path,
                              const std::shared_ptr<std::atomic_bool> &cancelled)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return -1;

    constexpr qsizetype chunkSize = 512 * 1024;
    QByteArray overlap;
    qint64 bytesRead = 0;
    while (!file.atEnd() && !cancelled->load(std::memory_order_relaxed)) {
        const QByteArray fresh = file.read(chunkSize);
        if (fresh.isEmpty())
            break;
        const QByteArray block = overlap + fresh;
        const qint64 blockOffset = bytesRead - overlap.size();
        qsizetype searchFrom = 0;
        while ((searchFrom = block.indexOf(QByteArrayLiteral("hsqs"), searchFrom)) >= 0) {
            const qint64 candidate = blockOffset + searchFrom;
            if (validSquashfsSuperblock(file, candidate))
                return candidate;
            ++searchFrom;
        }
        bytesRead += fresh.size();
        overlap = block.right(3);
    }
    return -1;
}

QImage embeddedAppImageIcon(const QString &path,
                            const std::shared_ptr<std::atomic_bool> &cancelled)
{
    const QString unsquashfs = QStandardPaths::findExecutable(QStringLiteral("unsquashfs"));
    if (unsquashfs.isEmpty())
        return {};

    const qint64 offset = appImageSquashfsOffset(path, cancelled);
    if (offset < 0 || cancelled->load(std::memory_order_relaxed))
        return {};

    // AppDir/AppImage defines .DirIcon as the application icon. unsquashfs
    // follows its usual symlink to the real PNG/SVG without executing any
    // code from the AppImage.
    QProcess extract;
    extract.start(unsquashfs,
                  {QStringLiteral("-o"), QString::number(offset),
                   QStringLiteral("-cat"), path, QStringLiteral(".DirIcon")});
    if (!waitForThumbnailProcess(extract, cancelled, 8000)
            || extract.exitStatus() != QProcess::NormalExit || extract.exitCode() != 0)
        return {};

    QByteArray iconData = extract.readAllStandardOutput();
    if (iconData.isEmpty() || iconData.size() > 24 * 1024 * 1024)
        return {};
    QBuffer buffer(&iconData);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer);
    reader.setAutoTransform(true);
    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()
            && (sourceSize.width() > 512 || sourceSize.height() > 512)) {
        reader.setScaledSize(sourceSize.scaled(QSize(512, 512), Qt::KeepAspectRatio));
    }
    QImage icon = reader.read();
    if (!icon.isNull() && (icon.width() > 512 || icon.height() > 512))
        icon = icon.scaled(QSize(512, 512), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return icon;
}

QImage embeddedVideoCover(const QString &path,
                          const std::shared_ptr<std::atomic_bool> &cancelled)
{
    if (cancelled->load(std::memory_order_relaxed))
        return {};

    // mkvextract reads the Matroska attachment table directly and does not
    // scan/decode a multi-gigabyte movie. Prefer it when available; this keeps
    // several simultaneous thumbnail jobs from hitting the FFmpeg timeout and
    // incorrectly falling back to a video frame.
    if (QFileInfo(path).suffix().compare(QStringLiteral("mkv"), Qt::CaseInsensitive) == 0) {
        const QString mkvmerge = QStandardPaths::findExecutable(QStringLiteral("mkvmerge"));
        const QString mkvextract = QStandardPaths::findExecutable(QStringLiteral("mkvextract"));
        if (!mkvmerge.isEmpty() && !mkvextract.isEmpty()) {
            QProcess identify;
            identify.start(mkvmerge, {QStringLiteral("-J"), path});
            if (waitForThumbnailProcess(identify, cancelled, 5000)
                && identify.exitStatus() == QProcess::NormalExit && identify.exitCode() == 0) {
                QJsonParseError error;
                const QJsonDocument details = QJsonDocument::fromJson(identify.readAllStandardOutput(), &error);
                int attachmentId = -1;
                qint64 bestAttachmentScore = -1;
                QString attachmentName;
                if (error.error == QJsonParseError::NoError && details.isObject()) {
                    const QJsonArray attachments = details.object().value(QStringLiteral("attachments")).toArray();
                    for (const QJsonValue &value : attachments) {
                        const QJsonObject attachment = value.toObject();
                        const QString mime = attachment.value(QStringLiteral("content_type")).toString().toLower();
                        const QString filename = attachment.value(QStringLiteral("file_name")).toString();
                        const QString suffix = QFileInfo(filename).suffix().toLower();
                        if (!mime.startsWith(QStringLiteral("image/"))
                            && !QStringList{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                                            QStringLiteral("webp"), QStringLiteral("bmp")}.contains(suffix)) {
                            continue;
                        }
                        qint64 score = attachment.value(QStringLiteral("size")).toInteger(1);
                        if (filename.contains(QStringLiteral("small"), Qt::CaseInsensitive))
                            score /= 4;
                        if (score > bestAttachmentScore) {
                            bestAttachmentScore = score;
                            attachmentId = attachment.value(QStringLiteral("id")).toInt(-1);
                            attachmentName = filename;
                        }
                    }
                }

                if (attachmentId >= 0 && !cancelled->load(std::memory_order_relaxed)) {
                    QString extension = QFileInfo(attachmentName).suffix().toLower();
                    if (extension.isEmpty())
                        extension = QStringLiteral("jpg");
                    QTemporaryFile extracted(QDir::tempPath()
                                             + QStringLiteral("/gfile-mkv-cover-XXXXXX.") + extension);
                    extracted.setAutoRemove(true);
                    if (extracted.open()) {
                        const QString extractedPath = extracted.fileName();
                        extracted.close();
                        QProcess extract;
                        extract.start(mkvextract,
                                      {path, QStringLiteral("attachments"),
                                       QString::number(attachmentId) + QLatin1Char(':') + extractedPath});
                        if (waitForThumbnailProcess(extract, cancelled, 12000)
                            && extract.exitStatus() == QProcess::NormalExit && extract.exitCode() == 0) {
                            const QImage cover = scaledVideoCover(extractedPath);
                            if (!cover.isNull())
                                return cover;
                        }
                    }
                }
            }
        }
    }

    // Matroska cover art is exposed by FFmpeg as an attached picture stream.
    // Probe first so the normal video stream is never mistaken for a cover and
    // choose the largest embedded image when both cover/small_cover variants
    // are present.
    QProcess probe;
    probe.start(QStringLiteral("ffprobe"),
                {QStringLiteral("-v"), QStringLiteral("error"),
                 QStringLiteral("-show_entries"),
                 QStringLiteral("stream=index,codec_type,codec_name,width,height:stream_disposition=attached_pic:stream_tags=filename,mimetype,title"),
                 QStringLiteral("-of"), QStringLiteral("json"), path});
    if (!waitForThumbnailProcess(probe, cancelled, 4000)
        || probe.exitStatus() != QProcess::NormalExit || probe.exitCode() != 0) {
        return {};
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(probe.readAllStandardOutput(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return {};

    int bestStream = -1;
    qint64 bestScore = -1;
    QString bestCodec;
    QString bestFilename;
    const QJsonArray streams = document.object().value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &value : streams) {
        const QJsonObject stream = value.toObject();
        const QJsonObject disposition = stream.value(QStringLiteral("disposition")).toObject();
        const QJsonObject tags = stream.value(QStringLiteral("tags")).toObject();
        const QString type = stream.value(QStringLiteral("codec_type")).toString().toLower();
        const QString codec = stream.value(QStringLiteral("codec_name")).toString().toLower();
        const QString mime = tags.value(QStringLiteral("mimetype")).toString().toLower();
        const QString filename = tags.value(QStringLiteral("filename")).toString();
        const QString fileSuffix = QFileInfo(filename).suffix().toLower();
        const bool attachedPicture = disposition.value(QStringLiteral("attached_pic")).toInt() == 1;
        const bool imageAttachment = type == QStringLiteral("attachment")
            && (mime.startsWith(QStringLiteral("image/"))
                || QStringList{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"),
                               QStringLiteral("webp"), QStringLiteral("bmp")}.contains(fileSuffix));
        if (!attachedPicture && !imageAttachment)
            continue;

        const qint64 width = stream.value(QStringLiteral("width")).toInteger();
        const qint64 height = stream.value(QStringLiteral("height")).toInteger();
        qint64 score = qMax<qint64>(1, width * height);
        if (filename.contains(QStringLiteral("small"), Qt::CaseInsensitive))
            score /= 4;
        if (score > bestScore) {
            bestScore = score;
            bestStream = stream.value(QStringLiteral("index")).toInt(-1);
            bestCodec = codec;
            bestFilename = filename;
        }
    }

    if (bestStream < 0 || cancelled->load(std::memory_order_relaxed))
        return {};

    QString extension = QFileInfo(bestFilename).suffix().toLower();
    if (extension.isEmpty()) {
        if (bestCodec == QStringLiteral("png"))
            extension = QStringLiteral("png");
        else if (bestCodec == QStringLiteral("webp"))
            extension = QStringLiteral("webp");
        else
            extension = QStringLiteral("jpg");
    }

    QTemporaryFile extracted(QDir::tempPath()
                             + QStringLiteral("/gfile-cover-XXXXXX.") + extension);
    extracted.setAutoRemove(true);
    if (!extracted.open())
        return {};
    const QString extractedPath = extracted.fileName();
    extracted.close();

    // Stream copy avoids decoding the whole video and extracts even a cover
    // stored near the end of a very large MKV in a few milliseconds.
    QProcess extract;
    extract.start(QStringLiteral("ffmpeg"),
                  {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                   QStringLiteral("-i"), path,
                   QStringLiteral("-map"), QStringLiteral("0:") + QString::number(bestStream),
                   QStringLiteral("-c"), QStringLiteral("copy"),
                   QStringLiteral("-frames:v"), QStringLiteral("1"), extractedPath});
    if (!waitForThumbnailProcess(extract, cancelled, 5000)
        || extract.exitStatus() != QProcess::NormalExit || extract.exitCode() != 0) {
        return {};
    }

    return scaledVideoCover(extractedPath);
}

quint32 synchsafe32(const uchar *p)
{
    return (quint32(p[0] & 0x7f) << 21)
        | (quint32(p[1] & 0x7f) << 14)
        | (quint32(p[2] & 0x7f) << 7)
        | quint32(p[3] & 0x7f);
}

quint32 bigEndian32(const uchar *p)
{
    return (quint32(p[0]) << 24) | (quint32(p[1]) << 16)
        | (quint32(p[2]) << 8) | quint32(p[3]);
}

QByteArray deUnsynchronise(QByteArray data)
{
    QByteArray clean;
    clean.reserve(data.size());
    for (qsizetype i = 0; i < data.size(); ++i) {
        const char c = data.at(i);
        clean.append(c);
        if (uchar(c) == 0xff && i + 1 < data.size() && data.at(i + 1) == '\0')
            ++i;
    }
    return clean;
}

qsizetype encodedStringEnd(const QByteArray &data, qsizetype start, uchar encoding)
{
    if (start >= data.size())
        return data.size();

    // ID3 encodings 1/2 are UTF-16 and terminate with two zero bytes.
    if (encoding == 1 || encoding == 2) {
        for (qsizetype i = start; i + 1 < data.size(); i += 2) {
            if (data.at(i) == '\0' && data.at(i + 1) == '\0')
                return i + 2;
        }
        return data.size();
    }

    const qsizetype pos = data.indexOf('\0', start);
    return pos >= 0 ? pos + 1 : data.size();
}

QImage imageFromApicPayload(QByteArray payload, bool unsynchronised)
{
    if (payload.size() < 8)
        return {};

    const uchar encoding = uchar(payload.at(0));
    const qsizetype mimeEnd = payload.indexOf('\0', 1);
    if (mimeEnd < 0 || mimeEnd + 2 >= payload.size())
        return {};

    // MIME string is followed by one picture-type byte and then a terminated
    // description string whose encoding is specified by the first byte.
    const qsizetype descriptionStart = mimeEnd + 2;
    const qsizetype imageStart = encodedStringEnd(payload, descriptionStart, encoding);
    if (imageStart >= payload.size())
        return {};

    QByteArray bytes = payload.mid(imageStart);
    if (unsynchronised)
        bytes = deUnsynchronise(bytes);
    if (bytes.size() > 32 * 1024 * 1024)
        return {};
    return QImage::fromData(bytes);
}

QImage imageFromPicPayload(QByteArray payload, bool unsynchronised)
{
    // ID3v2.2 PIC: encoding(1), format(3), picture type(1), description, data.
    if (payload.size() < 8)
        return {};
    const uchar encoding = uchar(payload.at(0));
    const qsizetype imageStart = encodedStringEnd(payload, 5, encoding);
    if (imageStart >= payload.size())
        return {};
    QByteArray bytes = payload.mid(imageStart);
    if (unsynchronised)
        bytes = deUnsynchronise(bytes);
    return QImage::fromData(bytes);
}

QImage embeddedMp3Cover(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    const QByteArray header = file.read(10);
    if (header.size() != 10 || header.left(3) != QByteArrayLiteral("ID3"))
        return {};

    const int version = uchar(header.at(3));
    if (version < 2 || version > 4)
        return {};

    const bool unsynchronised = (uchar(header.at(5)) & 0x80) != 0;
    const quint32 tagSize = synchsafe32(reinterpret_cast<const uchar *>(header.constData() + 6));
    if (tagSize == 0 || tagSize > 64u * 1024u * 1024u)
        return {};

    QByteArray tag = file.read(tagSize);
    if (tag.isEmpty())
        return {};

    qsizetype pos = 0;
    const bool extendedHeader = (uchar(header.at(5)) & 0x40) != 0;
    if (extendedHeader && version >= 3 && tag.size() >= 4) {
        const auto *p = reinterpret_cast<const uchar *>(tag.constData());
        const quint32 extSize = version == 4 ? synchsafe32(p) : bigEndian32(p);
        // v2.3 excludes the four-byte size field; v2.4 includes it.
        pos = version == 3 ? qsizetype(4u + extSize) : qsizetype(extSize);
        if (pos < 0 || pos >= tag.size())
            pos = 0;
    }

    while (pos < tag.size()) {
        if (version == 2) {
            if (pos + 6 > tag.size())
                break;
            const QByteArray id = tag.mid(pos, 3);
            if (id == QByteArray(3, '\0'))
                break;
            const auto *p = reinterpret_cast<const uchar *>(tag.constData() + pos + 3);
            const quint32 frameSize = (quint32(p[0]) << 16) | (quint32(p[1]) << 8) | quint32(p[2]);
            pos += 6;
            if (frameSize == 0 || pos + frameSize > tag.size())
                break;
            if (id == QByteArrayLiteral("PIC")) {
                QImage image = imageFromPicPayload(tag.mid(pos, frameSize), unsynchronised);
                if (!image.isNull())
                    return image;
            }
            pos += frameSize;
            continue;
        }

        if (pos + 10 > tag.size())
            break;
        const QByteArray id = tag.mid(pos, 4);
        if (id == QByteArray(4, '\0'))
            break;
        const auto *p = reinterpret_cast<const uchar *>(tag.constData() + pos + 4);
        const quint32 frameSize = version == 4 ? synchsafe32(p) : bigEndian32(p);
        const uchar formatFlags = uchar(tag.at(pos + 9));
        const bool frameUnsynchronised = version == 4 && (formatFlags & 0x02) != 0;
        pos += 10;
        if (frameSize == 0 || pos + frameSize > tag.size())
            break;
        if (id == QByteArrayLiteral("APIC")) {
            QImage image = imageFromApicPayload(tag.mid(pos, frameSize), unsynchronised || frameUnsynchronised);
            if (!image.isNull())
                return image;
        }
        pos += frameSize;
    }

    return {};
}

QString textPreview(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};

    QByteArray bytes = file.read(48 * 1024);
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);

    QString text = QString::fromUtf8(bytes);
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    text.replace(QLatin1Char('\t'), QStringLiteral("    "));

    // Binary-ish content should not be rendered as a text document thumbnail.
    if (text.contains(QChar::ReplacementCharacter) && bytes.contains('\0'))
        return {};

    const QStringList inputLines = text.split(QLatin1Char('\n'));
    QStringList lines;
    lines.reserve(qMin(36, inputLines.size()));
    for (const QString &raw : inputLines) {
        QString line = raw.trimmed();
        if (line.isEmpty() && (lines.isEmpty() || lines.constLast().isEmpty()))
            continue;
        if (line.size() > 180)
            line = line.left(177) + QStringLiteral("…");
        lines.append(line);
        if (lines.size() >= 36)
            break;
    }
    return lines.join(QLatin1Char('\n')).trimmed();
}

QImage renderTextThumbnail(const QString &path, const QSize &target)
{
    const QString text = textPreview(path);
    if (text.isEmpty())
        return {};

    const int h = qMax(160, target.height());
    const int w = qMax(124, qMin(target.width(), qRound(h * 0.82)));
    QImage image(QSize(w, h), QImage::Format_ARGB32_Premultiplied);
    image.fill(QColor(250, 250, 248));

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QPen(QColor(190, 193, 198), qMax(1, h / 180)));
    painter.drawRoundedRect(image.rect().adjusted(1, 1, -2, -2), qMax(3, h / 65), qMax(3, h / 65));

    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    font.setPixelSize(qBound(8, h / 24, 18));
    painter.setFont(font);
    painter.setPen(QColor(45, 48, 52));

    const int margin = qMax(9, w / 14);
    const QRect textRect = image.rect().adjusted(margin, margin, -margin, -margin);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text);
    return image;
}

} // namespace

class ThumbnailResponse final : public QQuickImageResponse
{
public:
    ThumbnailResponse(QString path, QSize requestedSize)
        : m_path(std::move(path)), m_requestedSize(requestedSize),
          m_cancelled(std::make_shared<std::atomic_bool>(false))
    {
        connect(&m_watcher, &QFutureWatcher<QImage>::finished, this, [this]() {
            m_image = m_watcher.result();
            emit finished();
        });
        m_watcher.setFuture(QtConcurrent::run(thumbnailPool(),
            [path = m_path, size = m_requestedSize, cancelled = m_cancelled]() {
            if (cancelled->load(std::memory_order_relaxed))
                return QImage();

            QSize target = size.isValid() ? size : QSize(256, 256);
            target.setWidth(qBound(96, target.width(), 512));
            target.setHeight(qBound(96, target.height(), 512));

            const QFileInfo info(path);
            if (!info.exists() || info.isDir())
                return QImage();

            if (cancelled->load(std::memory_order_relaxed))
                return QImage();

            const QString suffix = info.suffix().toLower();
            const QStringList imageTypes = {"png", "jpg", "jpeg", "webp", "bmp", "gif", "svg"};
            const QStringList videoTypes = {"mp4", "mkv", "avi", "mov", "webm", "m4v", "ts", "mpeg", "mpg"};
            const QStringList audioTypes = {"mp3", "flac", "m4a", "aac", "ogg", "opus", "wav", "wma", "alac"};
            const QStringList textTypes = {"srt", "lrc", "lrclib", "vtt", "ass", "ssa", "txt", "md", "log", "nfo", "json", "xml", "yaml", "yml", "toml", "ini", "conf", "cfg", "desktop", "service", "csv", "tsv", "m3u", "m3u8", "pls"};

            const QString cacheBase = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
            QDir().mkpath(cacheBase);
            const QString cached = ThumbnailProvider::cachedFilePath(path);
            const QString hash = QFileInfo(cached).completeBaseName();
            const QString missing = cacheBase + "/" + hash + ".miss";
            const QString memoryKey = hash + QLatin1Char('|')
                + QString::number(target.width()) + QLatin1Char('x')
                + QString::number(target.height());

            QImage image = cachedMemoryThumbnail(memoryKey);
            if (!image.isNull())
                return image;

            image = QImage(cached);
            if (!image.isNull()) {
                QImage result = roundedThumbnail(image.scaled(target, Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation));
                storeMemoryThumbnail(memoryKey, result);
                return result;
            }
            if (QFileInfo::exists(missing))
                return QImage();

            if (cancelled->load(std::memory_order_relaxed))
                return QImage();

            bool previewAttempted = false;
            QImage generated;
            if (!QFileInfo::exists(cached)) {
                if (imageTypes.contains(suffix)) {
                    previewAttempted = true;
                    QImage decoded;
                    if (suffix == QStringLiteral("svg")) {
                        decoded = safeSvgThumbnail(path, QSize(512, 512));
                    } else {
                        QImageReader reader(path);
                        reader.setAutoTransform(true);
                        const QSize source = reader.size();
                        if (source.isValid())
                            reader.setScaledSize(source.scaled(QSize(512, 512), Qt::KeepAspectRatio));
                        decoded = reader.read();
                    }
                    if (!decoded.isNull()) {
                        if (decoded.width() > 512 || decoded.height() > 512)
                            decoded = decoded.scaled(QSize(512, 512), Qt::KeepAspectRatio,
                                                     Qt::SmoothTransformation);
                        decoded.save(cached, "PNG");
                        generated = decoded;
                    }
                } else if (videoTypes.contains(suffix)) {
                    previewAttempted = true;
                    if (cancelled->load(std::memory_order_relaxed))
                        return QImage();
                    QImage cover = embeddedVideoCover(path, cancelled);
                    if (!cover.isNull()) {
                        cover.save(cached, "PNG");
                        generated = cover;
                    } else if (!cancelled->load(std::memory_order_relaxed)) {
                        QProcess proc;
                        proc.start(QStringLiteral("ffmpegthumbnailer"),
                                   {QStringLiteral("-i"), path,
                                    QStringLiteral("-o"), cached,
                                    QStringLiteral("-s"), QStringLiteral("512"),
                                    QStringLiteral("-q"), QStringLiteral("8")});
                        waitForThumbnailProcess(proc, cancelled, 15000);
                    }
                } else if (suffix == QStringLiteral("pdf")) {
                    previewAttempted = true;
                    if (cancelled->load(std::memory_order_relaxed))
                        return QImage();
                    const QString base = cacheBase + "/" + hash;
                    QProcess proc;
                    proc.start(QStringLiteral("pdftoppm"),
                               {QStringLiteral("-f"), QStringLiteral("1"),
                                QStringLiteral("-singlefile"),
                                QStringLiteral("-scale-to"), QStringLiteral("512"),
                                QStringLiteral("-png"), path, base});
                    waitForThumbnailProcess(proc, cancelled, 15000);
                } else if (audioTypes.contains(suffix)) {
                    previewAttempted = true;
                    // MP3 APIC extraction remains the fastest path. Other common
                    // audio containers expose artwork as an attached-picture stream,
                    // which the generic FFmpeg cover extractor can read without
                    // decoding the audio payload.
                    QImage cover = suffix == QStringLiteral("mp3")
                        ? embeddedMp3Cover(path)
                        : embeddedVideoCover(path, cancelled);
                    if (!cover.isNull()) {
                        if (cover.width() > 384 || cover.height() > 384)
                            cover = cover.scaled(QSize(384, 384), Qt::KeepAspectRatio, Qt::SmoothTransformation);
                        cover.save(cached, "PNG");
                        generated = cover;
                    }
                } else if (suffix == QStringLiteral("appimage")) {
                    previewAttempted = true;
                    QImage icon = embeddedAppImageIcon(path, cancelled);
                    if (!icon.isNull()) {
                        icon.save(cached, "PNG");
                        generated = icon;
                    }
                } else if (textTypes.contains(suffix)) {
                    previewAttempted = true;
                    // Use a stable cache resolution instead of tying expensive
                    // document rendering to whichever delegate (list/grid)
                    // happened to request the file first.
                    const QImage document = renderTextThumbnail(path, QSize(384, 384));
                    if (!document.isNull()) {
                        document.save(cached, "PNG");
                        generated = document;
                    }
                }
            }

            if (cancelled->load(std::memory_order_relaxed))
                return QImage();

            image = generated.isNull() ? QImage(cached) : generated;
            if (!image.isNull()) {
                QImage result = roundedThumbnail(image.scaled(target, Qt::KeepAspectRatio,
                                                               Qt::SmoothTransformation));
                storeMemoryThumbnail(memoryKey, result);
                return result;
            }
            if (previewAttempted && !cancelled->load(std::memory_order_relaxed)) {
                QFile marker(missing);
                if (marker.open(QIODevice::WriteOnly))
                    marker.close();
            }
            return QImage();
        }));
    }

    void cancel() override
    {
        // Delegates are recycled aggressively while scrolling. Mark queued
        // work as stale so it exits as soon as a worker becomes available.
        m_cancelled->store(true, std::memory_order_relaxed);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        // Qt accepts a texture factory created from a null QImage as a
        // successful load (Image.Ready), even though it draws no pixels.
        // Report the missing preview as an error so the QML file icon remains
        // visible for MP3s without cover art and other unpreviewable files.
        if (m_image.isNull())
            return nullptr;
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("No thumbnail available") : QString();
    }

private:
    QString m_path;
    QSize m_requestedSize;
    std::shared_ptr<std::atomic_bool> m_cancelled;
    QFutureWatcher<QImage> m_watcher;
    QImage m_image;
};

QString ThumbnailProvider::cachedFilePath(const QString &path)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QStringList videoTypes = {"mp4", "mkv", "avi", "mov", "webm", "m4v", "ts", "mpeg", "mpg"};
    const QStringList audioTypes = {"mp3", "flac", "m4a", "aac", "ogg", "opus", "wav", "wma", "alac"};
    const QString version = videoTypes.contains(suffix) ? QStringLiteral("v4|")
        : audioTypes.contains(suffix) ? QStringLiteral("audio-v1|")
        : suffix == QStringLiteral("appimage") ? QStringLiteral("appimage-v1|")
        : QStringLiteral("v2|");
    const QByteArray key = (version + path + QLatin1Char('|')
        + QString::number(info.lastModified().toMSecsSinceEpoch()) + QLatin1Char('|')
        + QString::number(info.size())
        + (suffix == QStringLiteral("svg") ? QLatin1Char('|') + svgPreviewIdentity(path) : QString())).toUtf8();
    const QString hash = QString::fromLatin1(QCryptographicHash::hash(key, QCryptographicHash::Sha256).toHex());
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/thumbnails/") + hash + QStringLiteral(".png");
}

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id, const QSize &requestedSize)
{
    // QML appends a view/model revision after '|'. It intentionally changes
    // the image:// source after overwrite/delete+replace so the scene graph does
    // not reuse a stale pixmap for the same file name. The provider itself only
    // needs the real path; its on-disk video/PDF cache is still keyed by file
    // metadata below.
    // The image-provider id is still percent-encoded here. Qt percent-encodes
    // the revision separator ('|') as %7C, so decode the whole id first and
    // only then strip the final revision suffix. Splitting before decoding
    // would leave "|revision" attached to the filesystem path and every
    // thumbnail lookup would fail.
    const QString decodedId = QUrl::fromPercentEncoding(id.toUtf8());
    const qsizetype separator = decodedId.lastIndexOf(QLatin1Char('|'));
    const QString path = separator >= 0 ? decodedId.left(separator) : decodedId;
    return new ThumbnailResponse(path, requestedSize);
}
