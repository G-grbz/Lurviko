#include "previewsettings.h"

#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QMutex>
#include <QMutexLocker>
#include <QSettings>
#include <QStorageInfo>

namespace {

const QStringList &allKinds()
{
    static const QStringList kinds = {
        QStringLiteral("images"), QStringLiteral("svg"), QStringLiteral("video"),
        QStringLiteral("audio"), QStringLiteral("pdf"), QStringLiteral("print"),
        QStringLiteral("office"), QStringLiteral("ebooks"), QStringLiteral("comics"),
        QStringLiteral("fonts"), QStringLiteral("text"), QStringLiteral("appimage")};
    return kinds;
}

QSet<QString> toLowerSet(const QList<QByteArray> &values)
{
    QSet<QString> result;
    for (const QByteArray &value : values)
        result.insert(QString::fromLatin1(value).toLower());
    return result;
}

const QSet<QString> &imageSuffixes()
{
    // Includes every format the installed Qt/KImageFormats plugins decode:
    // HEIC, AVIF, JXL, TIFF, PSD, KRA, XCF, EXR, camera RAW…
    static const QSet<QString> suffixes = [] {
        QSet<QString> result = toLowerSet(QImageReader::supportedImageFormats());
        for (const char *handledElsewhere : {"svg", "svgz", "pdf", "eps", "epsf", "epsi", "ps"})
            result.remove(QString::fromLatin1(handledElsewhere));
        result.insert(QStringLiteral("ora"));
        return result;
    }();
    return suffixes;
}

const QSet<QString> &imageMimeTypes()
{
    static const QSet<QString> types = toLowerSet(QImageReader::supportedMimeTypes());
    return types;
}

} // namespace

PreviewSettings::PreviewSettings(QObject *parent)
    : QObject(parent)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Previews"));
    m_enabled = settings.value(QStringLiteral("enabled"), true).toBool();
    m_systemMaxKiB = qMax(0, settings.value(QStringLiteral("systemMaxKiB"), 0).toInt());
    m_externalMaxKiB = qMax(0, settings.value(QStringLiteral("externalMaxKiB"), 0).toInt());
    const QStringList disabled = settings.value(QStringLiteral("disabledKinds")).toStringList();
    m_disabledKinds = QSet<QString>(disabled.cbegin(), disabled.cend());
}

PreviewSettings *PreviewSettings::instance()
{
    static PreviewSettings settings;
    return &settings;
}

void PreviewSettings::save()
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("Previews"));
    settings.setValue(QStringLiteral("enabled"), m_enabled);
    settings.setValue(QStringLiteral("systemMaxKiB"), m_systemMaxKiB);
    settings.setValue(QStringLiteral("externalMaxKiB"), m_externalMaxKiB);
    QStringList disabled(m_disabledKinds.cbegin(), m_disabledKinds.cend());
    disabled.sort();
    settings.setValue(QStringLiteral("disabledKinds"), disabled);
}

void PreviewSettings::bump()
{
    ++m_revision;
    save();
    emit changed();
}

void PreviewSettings::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    bump();
}

void PreviewSettings::setSystemMaxKiB(int kib)
{
    kib = qMax(0, kib);
    if (m_systemMaxKiB == kib)
        return;
    m_systemMaxKiB = kib;
    bump();
}

void PreviewSettings::setExternalMaxKiB(int kib)
{
    kib = qMax(0, kib);
    if (m_externalMaxKiB == kib)
        return;
    m_externalMaxKiB = kib;
    bump();
}

QStringList PreviewSettings::kinds() const
{
    return allKinds();
}

bool PreviewSettings::kindEnabled(const QString &kind) const
{
    return !m_disabledKinds.contains(kind);
}

void PreviewSettings::setKindEnabled(const QString &kind, bool enabled)
{
    if (!allKinds().contains(kind) || kindEnabled(kind) == enabled)
        return;
    if (enabled)
        m_disabledKinds.remove(kind);
    else
        m_disabledKinds.insert(kind);
    bump();
}

bool PreviewSettings::isExternalPath(const QString &localPath) const
{
    const QString directory = QFileInfo(localPath).absolutePath();
    const auto cached = m_externalByDirectory.constFind(directory);
    if (cached != m_externalByDirectory.cend())
        return cached.value();

    bool external = false;
    const QStorageInfo storage(directory);
    if (storage.isValid()) {
        // "/" and the internal mounts a system usually splits off count as
        // the system disk. Everything else (USB sticks, other disks under
        // /run/media or /mnt, network and FUSE mounts) uses the other limit.
        static const QSet<QString> systemMounts = {
            QStringLiteral("/"), QStringLiteral("/home"), QStringLiteral("/boot"),
            QStringLiteral("/boot/efi"), QStringLiteral("/efi"), QStringLiteral("/usr"),
            QStringLiteral("/var"), QStringLiteral("/opt"), QStringLiteral("/srv"),
            QStringLiteral("/tmp"), QStringLiteral("/root")};
        external = !systemMounts.contains(storage.rootPath());
    }
    if (m_externalByDirectory.size() > 4096)
        m_externalByDirectory.clear();
    m_externalByDirectory.insert(directory, external);
    return external;
}

bool PreviewSettings::allows(const QString &localPath, const QString &suffix,
                             const QString &mimeType, qint64 size) const
{
    if (!m_enabled || localPath.isEmpty())
        return false;
    const QString kind = kindFor(suffix, mimeType);
    if (kind.isEmpty() || !kindEnabled(kind))
        return false;
    if (size <= 0 && kind == QStringLiteral("text"))
        return false;
    const int limit = isExternalPath(localPath) ? m_externalMaxKiB : m_systemMaxKiB;
    return limit <= 0 || size <= qint64(limit) * 1024;
}

bool PreviewSettings::needsContentSniffing(const QString &mimeType)
{
    return mimeType.isEmpty() || mimeType == QStringLiteral("application/octet-stream");
}

QString PreviewSettings::kindFor(const QString &suffixValue, const QString &mimeValue)
{
    const QString suffix = suffixValue.toLower();
    const QString mime = mimeValue.toLower();
    const QString key = suffix + QLatin1Char('|') + mime;

    static QMutex mutex;
    static QHash<QString, QString> cache;
    {
        QMutexLocker lock(&mutex);
        const auto found = cache.constFind(key);
        if (found != cache.cend())
            return found.value();
    }

    static const QSet<QString> video = {
        "mp4", "m4v", "mkv", "webm", "avi", "mov", "qt", "wmv", "asf", "flv", "f4v", "mpeg", "mpg",
        "mpe", "m2v", "m1v", "mpv", "mts", "m2ts", "vob", "ogv", "3gp", "3g2", "divx", "xvid",
        "rm", "rmvb", "mxf", "dv", "y4m", "ivf", "nut", "dvr-ms", "wtv", "amv"};
    static const QSet<QString> audio = {
        "mp3", "mp2", "flac", "wav", "wave", "m4a", "m4b", "m4r", "aac", "ogg", "oga", "opus",
        "spx", "wma", "alac", "aiff", "aif", "aifc", "ape", "wv", "mka", "mpc", "dsf", "dff",
        "tta", "weba", "tak"};
    static const QSet<QString> print = {"ps", "eps", "epsf", "epsi", "djvu", "djv"};
    static const QSet<QString> office = {
        "odt", "ods", "odp", "odg", "odf", "ott", "ots", "otp", "otg", "fodt",
        "docx", "docm", "dotx", "xlsx", "xlsm", "xltx", "pptx", "pptm", "potx", "ppsx"};
    static const QSet<QString> fonts = {"ttf", "otf", "ttc", "otc", "woff", "woff2"};

    QString kind;
    if (suffix == QStringLiteral("svg") || suffix == QStringLiteral("svgz")
            || mime.startsWith(QStringLiteral("image/svg+xml"))) {
        kind = QStringLiteral("svg");
    } else if (suffix == QStringLiteral("pdf") || mime == QStringLiteral("application/pdf")) {
        kind = QStringLiteral("pdf");
    } else if (print.contains(suffix) || mime == QStringLiteral("application/postscript")
               || mime == QStringLiteral("image/x-eps") || mime == QStringLiteral("image/vnd.djvu")) {
        kind = QStringLiteral("print");
    } else if (suffix == QStringLiteral("appimage") || mime.contains(QStringLiteral("appimage"))) {
        kind = QStringLiteral("appimage");
    } else if (office.contains(suffix)) {
        kind = QStringLiteral("office");
    } else if (suffix == QStringLiteral("epub") || mime == QStringLiteral("application/epub+zip")) {
        kind = QStringLiteral("ebooks");
    } else if (suffix == QStringLiteral("cbz") || mime == QStringLiteral("application/vnd.comicbook+zip")) {
        kind = QStringLiteral("comics");
    } else if (fonts.contains(suffix) || mime.startsWith(QStringLiteral("font/"))
               || mime == QStringLiteral("application/x-font-ttf")
               || mime == QStringLiteral("application/x-font-otf")) {
        kind = QStringLiteral("fonts");
    } else if (suffix == QStringLiteral("ts")) {
        // ".ts" is also TypeScript and Qt Linguist.
        kind = mime.isEmpty() || mime.startsWith(QStringLiteral("video/")) ? QStringLiteral("video") : QString();
    } else if (video.contains(suffix) || mime.startsWith(QStringLiteral("video/"))) {
        kind = QStringLiteral("video");
    } else if (audio.contains(suffix)
               || (mime.startsWith(QStringLiteral("audio/")) && !mime.contains(QStringLiteral("mpegurl"))
                   && !mime.contains(QStringLiteral("scpls")))) {
        kind = QStringLiteral("audio");
    } else if (imageSuffixes().contains(suffix) || imageMimeTypes().contains(mime)) {
        kind = QStringLiteral("images");
    }

    if (kind.isEmpty()) {
        if (needsContentSniffing(mime)) {
            // Extension-less or unknown names (".gitignore", "README",
            // "claude"…) are confirmed as text from their content later.
            kind = QStringLiteral("text");
        } else {
            static const QMimeDatabase database;
            const QMimeType type = database.mimeTypeForName(mime);
            if (type.isValid() && type.inherits(QStringLiteral("text/plain")))
                kind = QStringLiteral("text");
        }
    }

    QMutexLocker lock(&mutex);
    if (cache.size() > 2048)
        cache.clear();
    cache.insert(key, kind);
    return kind;
}
