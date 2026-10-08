#include "musicmetadatamanager.h"

#include <algorithm>
#include <utility>
#include <QDateTime>
#include <QFutureWatcher>
#include <QtConcurrent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QUrl>

namespace {
QString tagValue(const QJsonObject &tags, const QStringList &wanted)
{
    for (auto it = tags.begin(); it != tags.end(); ++it) {
        const QString key = it.key().trimmed().toLower();
        for (const QString &candidate : wanted) {
            if (key == candidate.toLower())
                return it.value().toVariant().toString().trimmed();
        }
    }
    return {};
}

QString lyricTagValue(const QJsonObject &tags)
{
    static const QStringList preferred = {
        QStringLiteral("syncedlyrics"), QStringLiteral("lyrics"),
        QStringLiteral("unsyncedlyrics"), QStringLiteral("unsynced lyrics")
    };
    const QString direct = tagValue(tags, preferred);
    if (!direct.isEmpty())
        return direct;

    for (auto it = tags.begin(); it != tags.end(); ++it) {
        if (it.key().toLower().contains(QStringLiteral("lyric"))) {
            const QString value = it.value().toVariant().toString().trimmed();
            if (!value.isEmpty())
                return value;
        }
    }
    return {};
}

void mergeTags(QJsonObject &target, const QJsonObject &source)
{
    for (auto it = source.begin(); it != source.end(); ++it) {
        if (!target.contains(it.key()))
            target.insert(it.key(), it.value());
    }
}
}

MusicMetadataManager::MusicMetadataManager(QObject *parent)
    : QObject(parent)
{
}

QString MusicMetadataManager::localPathForUrl(const QString &url)
{
    const QUrl parsed(url);
    if (parsed.isLocalFile())
        return parsed.toLocalFile();
    if (parsed.scheme().isEmpty())
        return url;
    return {};
}

QString MusicMetadataManager::readTextFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    constexpr qint64 limit = 1024 * 1024;
    if (file.size() > limit) return {};
    QByteArray bytes = file.read(limit);
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    return QString::fromUtf8(bytes).replace(QStringLiteral("\r\n"), QStringLiteral("\n"))
                                    .replace(QLatin1Char('\r'), QLatin1Char('\n'));
}

QVariantMap MusicMetadataManager::parseLyrics(const QString &text)
{
    QVariantMap result;
    QVariantList timedLines;
    QStringList plainLines;

    // Supports [mm:ss], [mm:ss.xx], [mm:ss.xxx] and multiple timestamps on one line.
    const QRegularExpression timeRx(QStringLiteral(R"(\[(\d{1,3}):(\d{2})(?:[\.:](\d{1,3}))?\])"));
    const QRegularExpression metadataRx(QStringLiteral(R"(^\[(ar|ti|al|by|offset|re|ve|length):.*\]$)"),
                                         QRegularExpression::CaseInsensitiveOption);
    const QRegularExpression bracketRx(QStringLiteral(R"(^\[[^\]]+\]$)"));
    const QStringList lines = text.split(QLatin1Char('\n'));

    for (const QString &rawLine : lines) {
        QString line = rawLine.trimmed();
        if (line.isEmpty()) {
            plainLines.push_back(QString());
            continue;
        }

        // Ignore standard LRC metadata headers.
        if (metadataRx.match(line).hasMatch())
            continue;

        QRegularExpressionMatchIterator it = timeRx.globalMatch(line);
        QList<qint64> timestamps;
        int lastEnd = 0;
        while (it.hasNext()) {
            const auto match = it.next();
            const int min = match.captured(1).toInt();
            const int sec = match.captured(2).toInt();
            const QString fraction = match.captured(3);
            int ms = 0;
            if (!fraction.isEmpty()) {
                if (fraction.size() == 1) ms = fraction.toInt() * 100;
                else if (fraction.size() == 2) ms = fraction.toInt() * 10;
                else ms = fraction.left(3).toInt();
            }
            timestamps.push_back((qint64(min) * 60 + sec) * 1000 + ms);
            lastEnd = match.capturedEnd();
        }

        QString lyric = timestamps.isEmpty() ? line : line.mid(lastEnd).trimmed();
        if (timestamps.isEmpty()) {
            // Strip other bracket-only metadata rather than showing it as a lyric.
            if (!bracketRx.match(lyric).hasMatch())
                plainLines.push_back(lyric);
            continue;
        }

        for (qint64 timestamp : timestamps) {
            if (timedLines.size() >= 10000) break;
            QVariantMap entry;
            entry.insert(QStringLiteral("time"), timestamp);
            entry.insert(QStringLiteral("text"), lyric);
            timedLines.push_back(entry);
        }
        plainLines.push_back(lyric);
    }

    std::sort(timedLines.begin(), timedLines.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("time")).toLongLong()
             < b.toMap().value(QStringLiteral("time")).toLongLong();
    });

    QString plain = plainLines.join(QLatin1Char('\n')).trimmed();
    result.insert(QStringLiteral("synced"), !timedLines.isEmpty());
    result.insert(QStringLiteral("lines"), timedLines);
    result.insert(QStringLiteral("text"), plain);
    return result;
}

QVariantMap MusicMetadataManager::ffprobeMetadata(const QString &path)
{
    QVariantMap result;
    if (path.isEmpty())
        return result;

    QProcess process;
    process.start(QStringLiteral("ffprobe"), {
        QStringLiteral("-v"), QStringLiteral("quiet"),
        QStringLiteral("-print_format"), QStringLiteral("json"),
        QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
        path
    });
    QByteArray output;
    QElapsedTimer elapsed;
    elapsed.start();
    if (!process.waitForStarted(1200)) return result;
    while (process.state() != QProcess::NotRunning && elapsed.elapsed() < 3500) {
        process.waitForFinished(50);
        output += process.readAllStandardOutput();
        process.readAllStandardError();
        if (output.size() > 4 * 1024 * 1024) break;
    }
    output += process.readAllStandardOutput();
    if (process.state() != QProcess::NotRunning || output.size() > 4 * 1024 * 1024) {
        process.kill();
        process.waitForFinished(500);
        return result;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return result;

    QJsonObject tags;
    const QJsonObject root = doc.object();
    mergeTags(tags, root.value(QStringLiteral("format")).toObject().value(QStringLiteral("tags")).toObject());
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const QJsonValue &stream : streams)
        mergeTags(tags, stream.toObject().value(QStringLiteral("tags")).toObject());

    const QString title = tagValue(tags, {QStringLiteral("title")});
    const QString artist = tagValue(tags, {QStringLiteral("artist"), QStringLiteral("album_artist"), QStringLiteral("albumartist")});
    const QString album = tagValue(tags, {QStringLiteral("album")});
    const QString genre = tagValue(tags, {QStringLiteral("genre")});
    QString year = tagValue(tags, {QStringLiteral("date"), QStringLiteral("year")});
    const QRegularExpression yearRx(QStringLiteral(R"((19|20)\d{2})"));
    const auto yearMatch = yearRx.match(year);
    if (yearMatch.hasMatch())
        year = yearMatch.captured(0);

    result.insert(QStringLiteral("title"), title);
    result.insert(QStringLiteral("artist"), artist);
    result.insert(QStringLiteral("album"), album);
    result.insert(QStringLiteral("year"), year);
    result.insert(QStringLiteral("genre"), genre);
    result.insert(QStringLiteral("embeddedLyrics"), lyricTagValue(tags));
    return result;
}

QVariantMap MusicMetadataManager::load(const QString &url) const
{
    QVariantMap result;
    result.insert(QStringLiteral("title"), QString());
    result.insert(QStringLiteral("artist"), QString());
    result.insert(QStringLiteral("album"), QString());
    result.insert(QStringLiteral("year"), QString());
    result.insert(QStringLiteral("genre"), QString());
    result.insert(QStringLiteral("lyricsSource"), QString());
    result.insert(QStringLiteral("lyricsPath"), QString());
    result.insert(QStringLiteral("lyricsText"), QString());
    result.insert(QStringLiteral("lyricsSynced"), false);
    result.insert(QStringLiteral("lyricsLines"), QVariantList());

    const QString path = localPathForUrl(url);
    if (path.isEmpty())
        return result;

    const QVariantMap probe = ffprobeMetadata(path);
    for (auto it = probe.begin(); it != probe.end(); ++it)
        result.insert(it.key(), it.value());

    const QFileInfo audioInfo(path);
    const QString base = audioInfo.absolutePath() + QLatin1Char('/') + audioInfo.completeBaseName();
    QString lyricText;
    QString lyricPath;
    QString lyricSource;

    // Explicit user preference: sidecar .lrc first, then .lyrics, then embedded tags.
    const QString lrcPath = base + QStringLiteral(".lrc");
    const QString lyricsPath = base + QStringLiteral(".lyrics");
    if (QFileInfo::exists(lrcPath)) {
        lyricPath = lrcPath;
        lyricText = readTextFile(lrcPath);
        lyricSource = QStringLiteral("lrc");
    } else if (QFileInfo::exists(lyricsPath)) {
        lyricPath = lyricsPath;
        lyricText = readTextFile(lyricsPath);
        lyricSource = QStringLiteral("lyrics");
    } else {
        lyricText = probe.value(QStringLiteral("embeddedLyrics")).toString();
        if (!lyricText.trimmed().isEmpty())
            lyricSource = QStringLiteral("embedded");
    }

    if (lyricText.size() <= 1024 * 1024 && !lyricText.trimmed().isEmpty()) {
        const QVariantMap parsed = parseLyrics(lyricText);
        result.insert(QStringLiteral("lyricsSource"), lyricSource);
        result.insert(QStringLiteral("lyricsPath"), lyricPath);
        result.insert(QStringLiteral("lyricsText"), parsed.value(QStringLiteral("text")));
        result.insert(QStringLiteral("lyricsSynced"), parsed.value(QStringLiteral("synced")));
        result.insert(QStringLiteral("lyricsLines"), parsed.value(QStringLiteral("lines")));
    }
    result.remove(QStringLiteral("embeddedLyrics"));
    return result;
}

QString MusicMetadataManager::fingerprint(const QString &url)
{
    const QFileInfo audio(localPathForUrl(url));
    const QString base = audio.absolutePath() + '/' + audio.completeBaseName();
    QString key = url;
    for (const QFileInfo &info : {audio, QFileInfo(base + ".lrc"), QFileInfo(base + ".lyrics")})
        key += '|' + QString::number(info.size()) + ':' + QString::number(info.lastModified().toMSecsSinceEpoch());
    return key;
}

void MusicMetadataManager::request(const QString &url)
{
    ++m_generation;
    m_pendingUrl = url;
    startPending();
}

void MusicMetadataManager::clear()
{
    ++m_generation;
    m_pendingUrl.clear();
    m_cache.clear();
}

void MusicMetadataManager::startPending()
{
    if (m_workerActive || m_pendingUrl.isEmpty()) return;
    const QString url = std::exchange(m_pendingUrl, {});
    const QString key = fingerprint(url);
    if (const auto *cached = m_cache.object(key)) {
        emit metadataReady(url, *cached);
        return;
    }
    const quint64 generation = m_generation;
    m_workerActive = true;
    auto *watcher = new QFutureWatcher<QVariantMap>(this);
    connect(watcher, &QFutureWatcher<QVariantMap>::finished, this, [this, watcher, url, key, generation] {
        const auto result = watcher->result();
        watcher->deleteLater();
        m_workerActive = false;
        if (generation == m_generation) {
            const int cost = qMax(1, int(QJsonDocument::fromVariant(result).toJson(QJsonDocument::Compact).size() * 3 / 1024) + 1);
            m_cache.insert(key, new QVariantMap(result), cost);
            emit metadataReady(url, result);
        }
        startPending();
    });
    // The worker owns its reader; it never accesses the UI object's lifetime.
    watcher->setFuture(QtConcurrent::run([url] {
        MusicMetadataManager reader;
        return reader.load(url);
    }));
}
