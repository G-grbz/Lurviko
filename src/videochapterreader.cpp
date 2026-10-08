#include "videochapterreader.h"
#include <QFileInfo>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>

namespace {
constexpr int maximumOutput = 1024 * 1024;
qint64 chapterTime(const QJsonObject &chapter, const QString &field)
{
    bool ok = false;
    const auto explicitSeconds = chapter.value(field + QStringLiteral("_time")).toVariant();
    double seconds = explicitSeconds.toDouble(&ok);
    if (!ok) {
        const auto base = chapter.value(QStringLiteral("time_base")).toString().split(QLatin1Char('/'));
        if (base.size() != 2) return -1;
        bool numeratorOk = false, denominatorOk = false;
        const double numerator = base[0].toDouble(&numeratorOk);
        const double denominator = base[1].toDouble(&denominatorOk);
        const double ticks = chapter.value(field).toVariant().toDouble(&ok);
        if (!ok || !numeratorOk || !denominatorOk || numerator <= 0 || denominator <= 0) return -1;
        seconds = ticks * numerator / denominator;
    }
    // Reject malformed timestamps before converting to an integer.
    if (!std::isfinite(seconds) || seconds < 0 || seconds > 365 * 24 * 60 * 60) return -1;
    return qRound64(seconds * 1000.0);
}
}

VideoChapterReader::VideoChapterReader(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(15000);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process) finishProbe(m_process, QStringLiteral("timeout"));
    });
}

VideoChapterReader::~VideoChapterReader() { cancelProbe(); }

void VideoChapterReader::cancelProbe()
{
    m_timeout.stop();
    if (!m_process) return;
    auto *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    // Reap a cancelled process asynchronously rather than waiting in the UI.
    process->setParent(nullptr);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), process, &QObject::deleteLater);
    if (process->state() == QProcess::NotRunning) process->deleteLater();
    else process->kill();
}

QVariantList VideoChapterReader::parseChapters(const QByteArray &json)
{
    if (json.size() > maximumOutput) return {};
    const auto doc = QJsonDocument::fromJson(json);
    if (!doc.isObject()) return {};
    QVariantList result;
    const auto chapters = doc.object().value(QStringLiteral("chapters")).toArray();
    if (chapters.size() > 4096) return {};
    for (const auto &entry : chapters) {
        const auto chapter = entry.toObject();
        const qint64 start = chapterTime(chapter, QStringLiteral("start"));
        qint64 end = chapterTime(chapter, QStringLiteral("end"));
        if (start < 0) continue;
        if (end <= start) end = -1;
        QString title;
        const auto tags = chapter.value(QStringLiteral("tags")).toObject();
        for (auto it = tags.begin(); it != tags.end(); ++it) {
            if (it.key().compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0) {
                title = it.value().toString().trimmed(); break;
            }
        }
        result.append(QVariantMap{{QStringLiteral("title"), title},
                                 {QStringLiteral("startMs"), start}, {QStringLiteral("endMs"), end}});
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.toMap().value(QStringLiteral("startMs")).toLongLong() < b.toMap().value(QStringLiteral("startMs")).toLongLong();
    });
    return result;
}

int VideoChapterReader::chapterAt(qint64 positionMs) const
{
    const auto it = std::upper_bound(m_chapters.cbegin(), m_chapters.cend(), positionMs,
        [](qint64 position, const QVariant &chapter) { return position < chapter.toMap().value(QStringLiteral("startMs")).toLongLong(); });
    if (it == m_chapters.cbegin()) return -1;
    const auto previous = it - 1;
    const qint64 end = previous->toMap().value(QStringLiteral("endMs")).toLongLong();
    return end >= 0 && positionMs >= end ? -1 : int(previous - m_chapters.cbegin());
}

void VideoChapterReader::setSource(const QUrl &source)
{
    if (m_source == source) return;
    cancelProbe();
    m_source = source;
    m_chapters.clear();
    m_error.clear();
    m_loading = false;
    m_output.clear();
    m_cacheKey.clear();
    emit sourceChanged(); emit chaptersChanged(); emit errorChanged(); emit loadingChanged();
    if (source.isEmpty()) return;
    QString input;
    if (source.isLocalFile()) {
        const QFileInfo info(source.toLocalFile());
        if (!info.isFile()) { m_error = QStringLiteral("unavailable"); emit errorChanged(); return; }
        input = info.absoluteFilePath();
        m_cacheKey = input + QLatin1Char('|') + QString::number(info.size()) + QLatin1Char('|') + QString::number(info.lastModified().toMSecsSinceEpoch());
        if (const auto *cached = m_cache.object(m_cacheKey)) {
            m_chapters = *cached; emit chaptersChanged(); return;
        }
    } else if (source.scheme() == QStringLiteral("http") || source.scheme() == QStringLiteral("https")) {
        input = source.toString(QUrl::FullyEncoded);
    } else {
        m_error = QStringLiteral("unavailable"); emit errorChanged(); return;
    }
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (executable.isEmpty()) { m_error = QStringLiteral("missing-ffprobe"); emit errorChanged(); return; }
    auto *process = new QProcess(this);
    m_process = process;
    m_loading = true; emit loadingChanged();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process] {
        if (process != m_process) return;
        m_output += process->readAllStandardOutput();
        if (m_output.size() > maximumOutput) finishProbe(process, QStringLiteral("invalid"));
    });
    // Do not accumulate arbitrary stderr output or expose local paths/URL tokens.
    connect(process, &QProcess::readyReadStandardError, this, [process] { process->readAllStandardError(); });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError) {
        finishProbe(process, QStringLiteral("unavailable"));
    });
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this, process](int code, QProcess::ExitStatus status) {
            finishProbe(process, code == 0 && status == QProcess::NormalExit ? QString() : QStringLiteral("unavailable"));
        });
    m_timeout.start();
    process->start(executable, {QStringLiteral("-v"), QStringLiteral("error"),
        QStringLiteral("-rw_timeout"), QStringLiteral("5000000"),
        QStringLiteral("-show_chapters"), QStringLiteral("-of"), QStringLiteral("json"), QStringLiteral("-i"), input});
}

void VideoChapterReader::finishProbe(QProcess *process, const QString &error)
{
    if (process != m_process) return;
    m_output += process->readAllStandardOutput();
    m_error = error;
    if (m_output.size() > maximumOutput) m_error = QStringLiteral("invalid");
    if (m_error.isEmpty()) {
        if (!QJsonDocument::fromJson(m_output).isObject()) m_error = QStringLiteral("invalid");
        else {
            m_chapters = parseChapters(m_output);
            if (!m_cacheKey.isEmpty()) m_cache.insert(m_cacheKey, new QVariantList(m_chapters));
        }
    }
    cancelProbe();
    m_output.clear();
    m_loading = false;
    emit chaptersChanged(); emit errorChanged(); emit loadingChanged();
}
