#include "subtitleaimanager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QHash>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>
#include <QTemporaryFile>
#include <QUrl>
#include <QtMath>

#include <algorithm>

#ifndef GFILE_SOURCE_DIR
#define GFILE_SOURCE_DIR ""
#endif

SubtitleAiManager::SubtitleAiManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &SubtitleAiManager::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &SubtitleAiManager::readStandardError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &SubtitleAiManager::processFinished);
}

SubtitleAiManager::~SubtitleAiManager()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

QString SubtitleAiManager::normalizeLocalPath(const QString &filePath) const
{
    const QString value = filePath.trimmed();
    if (value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.isLocalFile())
            return url.toLocalFile();
    }
    return value;
}

QString SubtitleAiManager::cacheRoot() const
{
    QString base = qEnvironmentVariable("GFILE_SUBTITLE_AI_CACHE_DIR").trimmed();
    if (!base.isEmpty())
        return base;
    const QString xdg = qEnvironmentVariable("XDG_CACHE_HOME").trimmed();
    return xdg.isEmpty()
               ? QDir::home().absoluteFilePath(QStringLiteral(".cache/g-file/subtitle-ai/jobs"))
               : QDir(xdg).absoluteFilePath(QStringLiteral("g-file/subtitle-ai/jobs"));
}

QString SubtitleAiManager::cacheJobDirectory(const QString &filePath,
                                             const QString &language,
                                             const QString &qualityProfile,
                                             int audioTrackIndex) const
{
    const QString inputPath = normalizeLocalPath(filePath);
    const QFileInfo info(inputPath);
    if (!info.exists() || !info.isFile())
        return {};
    QString canonical = info.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = info.absoluteFilePath();
    const qint64 mtimeMs = info.lastModified().toMSecsSinceEpoch();
    const QByteArray material = QStringLiteral("v2\n%1\n%2\n%3\n%4\n%5\n%6\n")
                                    .arg(canonical)
                                    .arg(info.size())
                                    .arg(mtimeMs)
                                    .arg(qMax(0, audioTrackIndex))
                                    .arg(language.trimmed().toLower())
                                    .arg(qualityProfile.trimmed().toLower())
                                    .toUtf8();
    const QString key = QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
    return QDir(cacheRoot()).absoluteFilePath(key);
}

QString SubtitleAiManager::subtitleCacheJobDirectory(const QString &mediaPath,
                                                     const QString &sourceKind,
                                                     const QString &subtitlePath,
                                                     int subtitleTrackIndex,
                                                     const QString &sourceLanguage) const
{
    const QFileInfo mediaInfo(normalizeLocalPath(mediaPath));
    if (!mediaInfo.exists() || !mediaInfo.isFile())
        return {};
    QString mediaCanonical = mediaInfo.canonicalFilePath();
    if (mediaCanonical.isEmpty())
        mediaCanonical = mediaInfo.absoluteFilePath();
    QStringList parts{
        QStringLiteral("existing-v3"), mediaCanonical, QString::number(mediaInfo.size()),
        QString::number(mediaInfo.lastModified().toMSecsSinceEpoch()), sourceKind.trimmed().toLower(),
        QString::number(qMax(0, subtitleTrackIndex)), sourceLanguage.trimmed().toLower().isEmpty() ? QStringLiteral("und") : sourceLanguage.trimmed().toLower()
    };
    if (sourceKind.compare(QStringLiteral("external"), Qt::CaseInsensitive) == 0) {
        const QFileInfo subtitleInfo(normalizeLocalPath(subtitlePath));
        if (!subtitleInfo.exists() || !subtitleInfo.isFile())
            return {};
        QString subtitleCanonical = subtitleInfo.canonicalFilePath();
        if (subtitleCanonical.isEmpty())
            subtitleCanonical = subtitleInfo.absoluteFilePath();
        parts << subtitleCanonical << QString::number(subtitleInfo.size())
              << QString::number(subtitleInfo.lastModified().toMSecsSinceEpoch());
    }
    const QByteArray material = (parts.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    const QString key = QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
    return QDir(cacheRoot()).absoluteFilePath(key);
}

QVariantMap SubtitleAiManager::cacheInfoFromDirectory(const QString &directory,
                                                      const QString &translateTo) const
{
    QVariantMap result{
        {QStringLiteral("sourceReady"), false},
        {QStringLiteral("detectedLanguage"), QString()},
        {QStringLiteral("translationReady"), false},
        {QStringLiteral("translationProgress"), 0},
        {QStringLiteral("translationCompletedUnits"), 0},
        {QStringLiteral("translationTotalUnits"), 0}
    };
    if (directory.isEmpty())
        return result;
    QFile stateFile(QDir(directory).absoluteFilePath(QStringLiteral("state.json")));
    if (!stateFile.open(QIODevice::ReadOnly))
        return result;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(stateFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return result;
    const QJsonObject state = document.object();
    const bool sourceReady = state.value(QStringLiteral("source_ready")).toBool(false)
                             && QFileInfo(QDir(directory).absoluteFilePath(QStringLiteral("source.srt"))).isFile();
    result[QStringLiteral("sourceReady")] = sourceReady;
    result[QStringLiteral("detectedLanguage")] = state.value(QStringLiteral("detected_language")).toString();
    const QString target = translateTo.trimmed().toLower();
    if (target.isEmpty())
        return result;
    const QJsonObject translation = state.value(QStringLiteral("translations")).toObject().value(target).toObject();
    const bool complete = translation.value(QStringLiteral("complete")).toBool(false);
    const int completedUnits = translation.value(QStringLiteral("completed_units")).toInt(0);
    const int totalUnits = translation.value(QStringLiteral("total_units")).toInt(0);
    int progress = translation.value(QStringLiteral("progress")).toInt(0);
    if (complete)
        progress = 100;
    else if (progress <= 0 && totalUnits > 0)
        progress = qBound(0, qRound(double(completedUnits) * 100.0 / double(totalUnits)), 99);
    result[QStringLiteral("translationReady")] = complete;
    result[QStringLiteral("translationProgress")] = qBound(0, progress, 100);
    result[QStringLiteral("translationCompletedUnits")] = completedUnits;
    result[QStringLiteral("translationTotalUnits")] = totalUnits;
    return result;
}

QVariantMap SubtitleAiManager::cacheInfo(const QString &filePath,
                                         const QString &language,
                                         const QString &qualityProfile,
                                         const QString &translateTo,
                                         int audioTrackIndex) const
{
    return cacheInfoFromDirectory(cacheJobDirectory(filePath, language, qualityProfile, audioTrackIndex), translateTo);
}

QVariantMap SubtitleAiManager::subtitleTranslationCacheInfo(const QString &mediaPath,
                                                            const QString &sourceKind,
                                                            const QString &subtitlePath,
                                                            int subtitleTrackIndex,
                                                            const QString &sourceLanguage,
                                                            const QString &translateTo) const
{
    return cacheInfoFromDirectory(subtitleCacheJobDirectory(mediaPath, sourceKind, subtitlePath, subtitleTrackIndex, sourceLanguage), translateTo);
}

QString SubtitleAiManager::normalizedLanguageCode(const QString &value) const
{
    QString raw = value.trimmed().toLower();
    if (raw.contains(QLatin1Char('-')))
        raw = raw.section(QLatin1Char('-'), 0, 0);
    static const QHash<QString, QString> aliases{
        {"eng","en"},{"english","en"},{"tur","tr"},{"turkish","tr"},{"deu","de"},{"ger","de"},{"german","de"},
        {"fra","fr"},{"fre","fr"},{"french","fr"},{"spa","es"},{"spanish","es"},{"ita","it"},{"italian","it"},
        {"por","pt"},{"rus","ru"},{"ukr","uk"},{"jpn","ja"},{"kor","ko"},{"zho","zh"},{"chi","zh"},
        {"ara","ar"},{"fas","fa"},{"per","fa"},{"heb","he"},{"hin","hi"},{"nld","nl"},{"dut","nl"},
        {"pol","pl"},{"swe","sv"},{"nor","no"},{"dan","da"},{"fin","fi"},{"ces","cs"},{"cze","cs"},
        {"slk","sk"},{"slo","sk"},{"hun","hu"},{"ron","ro"},{"rum","ro"},{"bul","bg"},{"srp","sr"},
        {"hrv","hr"},{"bos","bs"},{"slv","sl"},{"mkd","mk"},{"sqi","sq"},{"alb","sq"},{"ell","el"},
        {"gre","el"},{"vie","vi"},{"tha","th"},{"ind","id"},{"msa","ms"},{"may","ms"},{"cat","ca"},
        {"eus","eu"},{"baq","eu"},{"glg","gl"},{"cym","cy"},{"wel","cy"},{"aze","az"},{"hye","hy"},
        {"arm","hy"},{"kat","ka"},{"geo","ka"},{"kaz","kk"},{"uzb","uz"},{"mon","mn"},{"tam","ta"},
        {"tel","te"},{"mar","mr"},{"guj","gu"},{"pan","pa"},{"nep","ne"},{"mya","my"},{"bur","my"},
        {"ben","bn"},{"urd","ur"},{"tgl","tl"},{"fil","tl"},{"swa","sw"}
    };
    if (aliases.contains(raw))
        return aliases.value(raw);
    if (raw.size() == 2 || raw == QLatin1String("und"))
        return raw;
    return QStringLiteral("und");
}

QVariantList SubtitleAiManager::subtitleSources(const QString &filePath) const
{
    QVariantList result;
    const QString inputPath = normalizeLocalPath(filePath);
    const QFileInfo mediaInfo(inputPath);
    if (!mediaInfo.exists() || !mediaInfo.isFile())
        return result;

    const QString ffprobe = QStandardPaths::findExecutable(QStringLiteral("ffprobe"));
    if (!ffprobe.isEmpty()) {
        QProcess probe;
        probe.setProgram(ffprobe);
        probe.setArguments({QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-select_streams"), QStringLiteral("s"),
                            QStringLiteral("-show_entries"), QStringLiteral("stream=codec_name:stream_tags=language,title:stream_disposition=forced,hearing_impaired,captions"),
                            QStringLiteral("-of"), QStringLiteral("json"), mediaInfo.absoluteFilePath()});
        probe.start();
        if (probe.waitForFinished(1800) && probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0) {
            const QJsonDocument doc = QJsonDocument::fromJson(probe.readAllStandardOutput());
            const QJsonArray streams = doc.object().value(QStringLiteral("streams")).toArray();
            int subtitleOrdinal = 0;
            for (const QJsonValue &value : streams) {
                const QJsonObject stream = value.toObject();
                const QString codec = stream.value(QStringLiteral("codec_name")).toString().toLower();
                if (codec == QLatin1String("hdmv_pgs_subtitle") || codec == QLatin1String("dvd_subtitle")
                    || codec == QLatin1String("dvb_subtitle") || codec == QLatin1String("xsub")) {
                    ++subtitleOrdinal;
                    continue;
                }
                const QJsonObject tags = stream.value(QStringLiteral("tags")).toObject();
                QVariantMap item;
                item.insert(QStringLiteral("kind"), QStringLiteral("embedded"));
                item.insert(QStringLiteral("trackIndex"), subtitleOrdinal);
                item.insert(QStringLiteral("language"), normalizedLanguageCode(tags.value(QStringLiteral("language")).toString()));
                item.insert(QStringLiteral("title"), tags.value(QStringLiteral("title")).toString().trimmed());
                const QJsonObject disposition = stream.value(QStringLiteral("disposition")).toObject();
                item.insert(QStringLiteral("forced"), disposition.value(QStringLiteral("forced")).toInt() != 0);
                item.insert(QStringLiteral("hearingImpaired"), disposition.value(QStringLiteral("hearing_impaired")).toInt() != 0);
                item.insert(QStringLiteral("captions"), disposition.value(QStringLiteral("captions")).toInt() != 0);
                item.insert(QStringLiteral("codec"), codec);
                item.insert(QStringLiteral("path"), QString());
                item.insert(QStringLiteral("fileName"), QString());
                result.append(item);
                ++subtitleOrdinal;
            }
        }
    }

    const QString mediaStem = mediaInfo.completeBaseName();
    const QRegularExpression splitTokens(QStringLiteral("[\\s._\\-()\\[\\]]+"));
    QDir directory(mediaInfo.absolutePath());
    const QFileInfoList sidecars = directory.entryInfoList(
        {QStringLiteral("*.srt"), QStringLiteral("*.ass"), QStringLiteral("*.ssa"), QStringLiteral("*.vtt")},
        QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo &subtitleInfo : sidecars) {
        const QString subtitleStem = subtitleInfo.completeBaseName();
        const bool sameStem = subtitleStem.compare(mediaStem, Qt::CaseInsensitive) == 0;
        const bool prefixed = subtitleStem.startsWith(mediaStem + QLatin1Char('.'), Qt::CaseInsensitive)
                              || subtitleStem.startsWith(mediaStem + QLatin1Char('-'), Qt::CaseInsensitive)
                              || subtitleStem.startsWith(mediaStem + QLatin1Char('_'), Qt::CaseInsensitive)
                              || subtitleStem.startsWith(mediaStem + QLatin1Char(' '), Qt::CaseInsensitive);
        if (!sameStem && !prefixed)
            continue;
        QString language = QStringLiteral("und");
        QString tail = subtitleStem.mid(mediaStem.size());
        const QStringList tokens = tail.split(splitTokens, Qt::SkipEmptyParts);
        for (auto it = tokens.crbegin(); it != tokens.crend(); ++it) {
            const QString candidate = normalizedLanguageCode(*it);
            if (candidate != QLatin1String("und")) {
                language = candidate;
                break;
            }
        }
        QVariantMap item;
        item.insert(QStringLiteral("kind"), QStringLiteral("external"));
        item.insert(QStringLiteral("trackIndex"), -1);
        item.insert(QStringLiteral("language"), language);
        const QString lowerName = subtitleInfo.fileName().toLower();
        item.insert(QStringLiteral("title"), QString());
        item.insert(QStringLiteral("forced"), lowerName.contains(QStringLiteral("forced")) || lowerName.contains(QStringLiteral("zorunlu")));
        item.insert(QStringLiteral("hearingImpaired"), lowerName.contains(QStringLiteral("sdh")) || lowerName.contains(QStringLiteral("hearing")) || lowerName.contains(QStringLiteral("işitme")));
        item.insert(QStringLiteral("captions"), QRegularExpression(QStringLiteral("(^|[._\\- ])cc([._\\- ]|$)"), QRegularExpression::CaseInsensitiveOption).match(lowerName).hasMatch());
        item.insert(QStringLiteral("codec"), subtitleInfo.suffix().toLower());
        item.insert(QStringLiteral("path"), subtitleInfo.absoluteFilePath());
        item.insert(QStringLiteral("fileName"), subtitleInfo.fileName());
        result.append(item);
    }
    return result;
}

QString SubtitleAiManager::workerPath() const
{
    const QString overridePath = qEnvironmentVariable("GFILE_SUBTITLE_AI_WORKER").trimmed();
    if (!overridePath.isEmpty() && QFileInfo::exists(overridePath))
        return QFileInfo(overridePath).absoluteFilePath();
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList installedCandidates{
        QDir(appDir).absoluteFilePath(QStringLiteral("../share/g-File/subtitle-ai/worker.py")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../share/g-file/subtitle-ai/worker.py")),
        QDir(appDir).absoluteFilePath(QStringLiteral("../share/GFile/subtitle-ai/worker.py"))
    };
    for (const QString &candidate : installedCandidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    const QString sourceDir = QString::fromUtf8(GFILE_SOURCE_DIR);
    if (!sourceDir.isEmpty()) {
        const QString candidate = QDir(sourceDir).absoluteFilePath(QStringLiteral("tools/subtitle-ai/worker.py"));
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

QString SubtitleAiManager::pythonExecutable() const
{
    const QString overridePython = qEnvironmentVariable("GFILE_SUBTITLE_AI_PYTHON").trimmed();
    if (!overridePython.isEmpty())
        return overridePython;
    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    return python3.isEmpty() ? QStandardPaths::findExecutable(QStringLiteral("python")) : python3;
}

bool SubtitleAiManager::startWorker(const QStringList &arguments, const QString &initialStatus)
{
    const QString worker = workerPath();
    if (worker.isEmpty()) {
        finishWithError(tr("G-File Subtitle AI motoru bulunamadı. Kurulumdaki subtitle-ai dosyalarını kontrol edin."));
        return false;
    }
    const QString python = pythonExecutable();
    if (python.isEmpty()) {
        finishWithError(tr("Python 3 bulunamadı. G-File Subtitle AI Python 3 gerektiriyor."));
        return false;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));

    QString vendorDir = qEnvironmentVariable("GFILE_SUBTITLE_AI_VENDOR_DIR").trimmed();
    if (vendorDir.isEmpty()) {
        const QString xdgData = qEnvironmentVariable("XDG_DATA_HOME").trimmed();
        vendorDir = xdgData.isEmpty()
                        ? QDir::home().absoluteFilePath(QStringLiteral(".local/share/g-File/subtitle-ai/vendor"))
                        : QDir(xdgData).absoluteFilePath(QStringLiteral("g-File/subtitle-ai/vendor"));
    }
    if (QFileInfo(vendorDir).isDir()) {
        QString pythonPath = environment.value(QStringLiteral("PYTHONPATH"));
        pythonPath = vendorDir + (pythonPath.isEmpty() ? QString() : QDir::listSeparator() + pythonPath);
        environment.insert(QStringLiteral("PYTHONPATH"), pythonPath);
        QStringList cudaLibs;
        const QString cublas = QDir(vendorDir).absoluteFilePath(QStringLiteral("nvidia/cublas/lib"));
        const QString cudnn = QDir(vendorDir).absoluteFilePath(QStringLiteral("nvidia/cudnn/lib"));
        if (QFileInfo(cublas).isDir()) cudaLibs.append(cublas);
        if (QFileInfo(cudnn).isDir()) cudaLibs.append(cudnn);
        const QString oldLd = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
        if (!oldLd.isEmpty()) cudaLibs.append(oldLd);
        if (!cudaLibs.isEmpty()) environment.insert(QStringLiteral("LD_LIBRARY_PATH"), cudaLibs.join(QLatin1Char(':')));
    }

    m_process.setProcessEnvironment(environment);
    m_process.setProgram(python);
    m_process.setArguments(QStringList{worker} + arguments);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_cancelRequested = false;
    m_terminalEventReceived = false;
    setProgress(0);
    setStatus(initialStatus);
    m_liveTranslationActive = arguments.contains(QStringLiteral("--translate-to"));
    ++m_liveRevision;
    emit liveRevisionChanged();
    setBusy(true);
    m_process.start();
    if (!m_process.waitForStarted(1500)) {
        const QString detail = m_process.errorString();
        setBusy(false);
        finishWithError(tr("G-File Subtitle AI worker başlatılamadı: %1").arg(detail));
        return false;
    }
    return true;
}

bool SubtitleAiManager::startTranscription(const QString &filePath,
                                           const QString &language,
                                           const QString &qualityProfile,
                                           const QString &translateTo,
                                           int audioTrackIndex)
{
    if (m_busy)
        return false;
    resetResult();
    clearLiveCues();
    const QFileInfo inputInfo(normalizeLocalPath(filePath));
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        finishWithError(tr("Video dosyası bulunamadı: %1").arg(inputInfo.absoluteFilePath()));
        return false;
    }
    QStringList arguments{QStringLiteral("--input"), inputInfo.absoluteFilePath(),
                          QStringLiteral("--mode"), QStringLiteral("transcribe"),
                          QStringLiteral("--language"), language.trimmed().toLower(),
                          QStringLiteral("--quality-profile"), qualityProfile.trimmed().toLower(),
                          QStringLiteral("--audio-track-index"), QString::number(qMax(0, audioTrackIndex))};
    const QString target = translateTo.trimmed().toLower();
    if (!target.isEmpty())
        arguments << QStringLiteral("--translate-to") << target;
    return startWorker(arguments, tr("Altyazı AI hazırlanıyor…"));
}

bool SubtitleAiManager::startSubtitleTranslation(const QString &mediaPath,
                                                 const QString &sourceKind,
                                                 const QString &subtitlePath,
                                                 int subtitleTrackIndex,
                                                 const QString &sourceLanguage,
                                                 const QString &translateTo)
{
    if (m_busy)
        return false;
    resetResult();
    clearLiveCues();
    const QFileInfo mediaInfo(normalizeLocalPath(mediaPath));
    if (!mediaInfo.exists() || !mediaInfo.isFile()) {
        finishWithError(tr("Video dosyası bulunamadı: %1").arg(mediaInfo.absoluteFilePath()));
        return false;
    }
    const QString kind = sourceKind.trimmed().toLower();
    if (kind != QLatin1String("embedded") && kind != QLatin1String("external")) {
        finishWithError(tr("Geçersiz altyazı kaynağı."));
        return false;
    }
    const QString target = translateTo.trimmed().toLower();
    if (target.isEmpty()) {
        finishWithError(tr("AI çeviri için hedef dil seçilmelidir."));
        return false;
    }
    QStringList arguments{QStringLiteral("--input"), mediaInfo.absoluteFilePath(),
                          QStringLiteral("--mode"), QStringLiteral("subtitle"),
                          QStringLiteral("--language"), sourceLanguage.trimmed().toLower().isEmpty() ? QStringLiteral("und") : sourceLanguage.trimmed().toLower(),
                          QStringLiteral("--translate-to"), target,
                          QStringLiteral("--subtitle-kind"), kind,
                          QStringLiteral("--subtitle-track-index"), QString::number(qMax(0, subtitleTrackIndex))};
    if (kind == QLatin1String("external")) {
        const QFileInfo subtitleInfo(normalizeLocalPath(subtitlePath));
        if (!subtitleInfo.exists() || !subtitleInfo.isFile()) {
            finishWithError(tr("Altyazı dosyası bulunamadı: %1").arg(subtitleInfo.absoluteFilePath()));
            return false;
        }
        arguments << QStringLiteral("--subtitle-path") << subtitleInfo.absoluteFilePath();
    }
    return startWorker(arguments, tr("Mevcut altyazı AI çevirisine hazırlanıyor…"));
}

QString SubtitleAiManager::liveSubtitleAt(qint64 positionMs) const
{
    auto findCue = [positionMs](const QVector<LiveCue> &cues) -> QString {
        for (auto it = cues.crbegin(); it != cues.crend(); ++it) {
            if (positionMs >= it->startMs && positionMs <= it->endMs)
                return it->text;
            if (it->endMs + 1500 < positionMs)
                break;
        }
        return {};
    };
    const QString translated = findCue(m_liveTranslatedCues);
    // Translation gaps must stay empty rather than switching back to the
    // original language while the translated stream is still arriving.
    return m_liveTranslationActive || !translated.isEmpty()
               ? translated : findCue(m_liveSourceCues);
}


qint64 SubtitleAiManager::parseSubtitleTimestamp(const QString &value) const
{
    const QString text = value.trimmed();
    QRegularExpression re(QStringLiteral(R"(^\s*(?:(\d+):)?(\d{1,2}):(\d{2})[,.](\d{1,3})\s*$)"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return -1;
    const qint64 hours = match.captured(1).isEmpty() ? 0 : match.captured(1).toLongLong();
    const qint64 minutes = match.captured(2).toLongLong();
    const qint64 seconds = match.captured(3).toLongLong();
    QString millisText = match.captured(4);
    while (millisText.size() < 3)
        millisText.append(QLatin1Char('0'));
    if (millisText.size() > 3)
        millisText.truncate(3);
    const qint64 millis = millisText.toLongLong();
    return ((hours * 60 + minutes) * 60 + seconds) * 1000 + millis;
}

QVector<SubtitleAiManager::LiveCue> SubtitleAiManager::loadSubtitleFile(const QString &subtitlePath) const
{
    QVector<LiveCue> result;
    const QFileInfo info(normalizeLocalPath(subtitlePath));
    if (!info.exists() || !info.isFile())
        return result;

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return result;
    QString content = QString::fromUtf8(file.readAll());
    content.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    content.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    const QString suffix = info.suffix().toLower();
    if (suffix == QLatin1String("ass") || suffix == QLatin1String("ssa")) {
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (const QString &rawLine : lines) {
            const QString line = rawLine.trimmed();
            if (!line.startsWith(QStringLiteral("Dialogue:"), Qt::CaseInsensitive))
                continue;
            const QString payload = line.mid(line.indexOf(QLatin1Char(':')) + 1).trimmed();
            const QStringList fields = payload.split(QLatin1Char(','));
            if (fields.size() < 10)
                continue;
            auto parseAssTime = [](const QString &v) -> qint64 {
                QRegularExpression rx(QStringLiteral(R"(^\s*(\d+):(\d{1,2}):(\d{2})[.](\d{1,2})\s*$)"));
                const auto m = rx.match(v);
                if (!m.hasMatch()) return -1;
                const qint64 h = m.captured(1).toLongLong();
                const qint64 min = m.captured(2).toLongLong();
                const qint64 sec = m.captured(3).toLongLong();
                QString cs = m.captured(4);
                while (cs.size() < 2) cs.append(QLatin1Char('0'));
                if (cs.size() > 2) cs.truncate(2);
                return ((h * 60 + min) * 60 + sec) * 1000 + cs.toLongLong() * 10;
            };
            const qint64 start = parseAssTime(fields.at(1));
            const qint64 end = parseAssTime(fields.at(2));
            if (start < 0 || end <= start)
                continue;
            QString text = fields.mid(9).join(QStringLiteral(","));
            text.replace(QStringLiteral("\\N"), QStringLiteral("\n"), Qt::CaseInsensitive);
            text.remove(QRegularExpression(QStringLiteral(R"(\{[^}]*\})")));
            text = text.trimmed();
            if (text.isEmpty())
                continue;
            result.append({start, end, text});
        }
    } else {
        const QStringList lines = content.split(QLatin1Char('\n'));
        for (int i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            const int arrow = line.indexOf(QStringLiteral("-->"));
            if (arrow < 0)
                continue;
            const QString startText = line.left(arrow).trimmed();
            QString endText = line.mid(arrow + 3).trimmed();
            const int endSpace = endText.indexOf(QRegularExpression(QStringLiteral("\\s")));
            if (endSpace > 0)
                endText = endText.left(endSpace);
            const qint64 start = parseSubtitleTimestamp(startText);
            const qint64 end = parseSubtitleTimestamp(endText);
            if (start < 0 || end <= start)
                continue;
            QStringList textLines;
            for (++i; i < lines.size(); ++i) {
                const QString cueLine = lines.at(i);
                if (cueLine.trimmed().isEmpty())
                    break;
                textLines.append(cueLine);
            }
            QString text = textLines.join(QStringLiteral("\n")).trimmed();
            if (text.isEmpty())
                continue;
            result.append({start, end, text});
        }
    }

    std::sort(result.begin(), result.end(), [](const LiveCue &a, const LiveCue &b) {
        return a.startMs < b.startMs || (a.startMs == b.startMs && a.endMs < b.endMs);
    });
    return result;
}

bool SubtitleAiManager::selectSubtitleFile(const QString &subtitlePath)
{
    const QFileInfo info(normalizeLocalPath(subtitlePath));
    if (!info.exists() || !info.isFile())
        return false;
    const QVector<LiveCue> cues = loadSubtitleFile(info.absoluteFilePath());
    if (cues.isEmpty())
        return false;
    m_selectedSubtitleCues = cues;
    m_selectedSubtitlePath = info.absoluteFilePath();
    // Publish the new selection before releasing live translation ownership.
    clearLiveCues();
    ++m_liveRevision;
    emit liveRevisionChanged();
    emit selectedSubtitleChanged();
    return true;
}

bool SubtitleAiManager::selectEmbeddedSubtitle(const QString &mediaPath, int subtitleTrackIndex)
{
    const QFileInfo mediaInfo(normalizeLocalPath(mediaPath));
    if (!mediaInfo.exists() || !mediaInfo.isFile())
        return false;
    const QString ffmpeg = QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
    if (ffmpeg.isEmpty())
        return false;

    QProcess extractor;
    extractor.setProgram(ffmpeg);
    extractor.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                            QStringLiteral("-i"), mediaInfo.absoluteFilePath(),
                            QStringLiteral("-map"), QStringLiteral("0:s:%1").arg(qMax(0, subtitleTrackIndex)),
                            QStringLiteral("-f"), QStringLiteral("srt"), QStringLiteral("pipe:1")});
    extractor.setProcessChannelMode(QProcess::SeparateChannels);
    extractor.start();
    if (!extractor.waitForStarted(1500) || !extractor.waitForFinished(15000)) {
        extractor.kill();
        extractor.waitForFinished(500);
        return false;
    }
    if (extractor.exitStatus() != QProcess::NormalExit || extractor.exitCode() != 0)
        return false;
    const QByteArray data = extractor.readAllStandardOutput();
    if (data.trimmed().isEmpty())
        return false;

    QTemporaryFile temporary(QDir(QDir::tempPath()).absoluteFilePath(QStringLiteral("g-file-subtitle-XXXXXX.srt")));
    temporary.setAutoRemove(true);
    if (!temporary.open())
        return false;
    if (temporary.write(data) != data.size())
        return false;
    temporary.flush();
    const QVector<LiveCue> cues = loadSubtitleFile(temporary.fileName());
    if (cues.isEmpty())
        return false;

    clearLiveCues();
    m_selectedSubtitleCues = cues;
    QString canonical = mediaInfo.canonicalFilePath();
    if (canonical.isEmpty())
        canonical = mediaInfo.absoluteFilePath();
    m_selectedSubtitlePath = QStringLiteral("embedded:%1#%2").arg(canonical).arg(qMax(0, subtitleTrackIndex));
    ++m_liveRevision;
    emit liveRevisionChanged();
    emit selectedSubtitleChanged();
    return true;
}

void SubtitleAiManager::clearSelectedSubtitle()
{
    if (m_selectedSubtitleCues.isEmpty() && m_selectedSubtitlePath.isEmpty())
        return;
    m_selectedSubtitleCues.clear();
    m_selectedSubtitlePath.clear();
    ++m_liveRevision;
    emit liveRevisionChanged();
    emit selectedSubtitleChanged();
}

QString SubtitleAiManager::selectedSubtitleAt(qint64 positionMs) const
{
    for (auto it = m_selectedSubtitleCues.crbegin(); it != m_selectedSubtitleCues.crend(); ++it) {
        if (positionMs >= it->startMs && positionMs <= it->endMs)
            return it->text;
        if (it->endMs + 1500 < positionMs)
            break;
    }
    return {};
}

void SubtitleAiManager::clearLiveCues()
{
    if (m_liveSourceCues.isEmpty() && m_liveTranslatedCues.isEmpty() && !m_liveTranslationActive)
        return;
    m_liveTranslationActive = false;
    m_liveSourceCues.clear();
    m_liveTranslatedCues.clear();
    ++m_liveRevision;
    emit liveRevisionChanged();
}

void SubtitleAiManager::addLiveCues(const QJsonObject &object)
{
    const QString stage = object.value(QStringLiteral("stage")).toString();
    QVector<LiveCue> &target = stage == QLatin1String("translated") ? m_liveTranslatedCues : m_liveSourceCues;
    if (object.value(QStringLiteral("replace")).toBool(false))
        target.clear();
    const QJsonArray cues = object.value(QStringLiteral("cues")).toArray();
    bool added = false;
    for (const QJsonValue &value : cues) {
        if (!value.isObject())
            continue;
        const QJsonObject cueObject = value.toObject();
        LiveCue cue;
        cue.startMs = qMax<qint64>(0, qRound64(cueObject.value(QStringLiteral("start")).toDouble() * 1000.0));
        cue.endMs = qMax<qint64>(cue.startMs, qRound64(cueObject.value(QStringLiteral("end")).toDouble() * 1000.0));
        cue.text = cueObject.value(QStringLiteral("text")).toString().trimmed();
        if (cue.text.isEmpty() || cue.endMs <= cue.startMs)
            continue;
        target.append(cue);
        added = true;
    }
    if (!added)
        return;
    std::sort(target.begin(), target.end(), [](const LiveCue &a, const LiveCue &b) {
        return a.startMs < b.startMs || (a.startMs == b.startMs && a.endMs < b.endMs);
    });
    ++m_liveRevision;
    emit liveRevisionChanged();
}

void SubtitleAiManager::cancel()
{
    if (!m_busy || m_process.state() == QProcess::NotRunning)
        return;
    m_cancelRequested = true;
    setStatus(tr("İptal ediliyor…"));
    m_process.terminate();
    QTimer::singleShot(3000, this, [this]() {
        if (m_cancelRequested && m_process.state() != QProcess::NotRunning)
            m_process.kill();
    });
}

void SubtitleAiManager::resetResult()
{
    if (m_busy)
        return;
    clearLiveCues();
    setProgress(0);
    setStatus(QString());
    setError(QString());
    setOutputPath(QString());
    setLastLog(QString());
}

void SubtitleAiManager::readStandardOutput()
{
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
    qsizetype newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty()) consumeLine(line);
    }
}

void SubtitleAiManager::readStandardError()
{
    m_stderrBuffer.append(m_process.readAllStandardError());
    constexpr qsizetype maxTail = 16 * 1024;
    if (m_stderrBuffer.size() > maxTail)
        m_stderrBuffer = m_stderrBuffer.right(maxTail);
}

void SubtitleAiManager::consumeLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastLog(QString::fromUtf8(line));
        return;
    }
    const QJsonObject object = document.object();
    const QString event = object.value(QStringLiteral("event")).toString();
    if (event == QLatin1String("live_cues")) { addLiveCues(object); return; }
    if (event == QLatin1String("progress")) {
        setProgress(object.value(QStringLiteral("value")).toInt(m_progress));
        const QString message = object.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) setStatus(message);
        return;
    }
    if (event == QLatin1String("log") || event == QLatin1String("ready")) {
        const QString message = object.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) { setLastLog(message); setStatus(message); }
        return;
    }
    if (event == QLatin1String("completed")) {
        m_terminalEventReceived = true;
        setProgress(100);
        setOutputPath(object.value(QStringLiteral("output")).toString());
        setStatus(tr("Altyazı hazır."));
        return;
    }
    if (event == QLatin1String("cancelled")) {
        m_terminalEventReceived = true;
        m_cancelRequested = true;
        setStatus(tr("İşlem iptal edildi."));
        return;
    }
    if (event == QLatin1String("error")) {
        m_terminalEventReceived = true;
        setError(object.value(QStringLiteral("message")).toString());
        setStatus(tr("Altyazı işlemi başarısız."));
    }
}

void SubtitleAiManager::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    readStandardOutput();
    readStandardError();
    if (!m_stdoutBuffer.trimmed().isEmpty()) {
        consumeLine(m_stdoutBuffer.trimmed());
        m_stdoutBuffer.clear();
    }
    setBusy(false);
    if (m_cancelRequested) {
        clearLiveCues();
        if (!m_terminalEventReceived) setStatus(tr("İşlem iptal edildi."));
        emit cancelled();
        m_cancelRequested = false;
        return;
    }
    if (!m_error.isEmpty()) {
        clearLiveCues();
        emit failed(m_error);
        return;
    }
    if (!m_outputPath.isEmpty() && exitStatus == QProcess::NormalExit && exitCode == 0) {
        emit completed(m_outputPath);
        return;
    }
    QString detail = QString::fromUtf8(m_stderrBuffer).trimmed();
    if (detail.length() > 1200) detail = detail.right(1200);
    if (detail.isEmpty())
        detail = exitStatus == QProcess::CrashExit
                     ? tr("G-File Subtitle AI worker beklenmedik biçimde kapandı.")
                     : tr("G-File Subtitle AI worker %1 koduyla sonlandı.").arg(exitCode);
    finishWithError(detail);
}

void SubtitleAiManager::setBusy(bool value) { if (m_busy != value) { m_busy = value; emit busyChanged(); } }
void SubtitleAiManager::setProgress(int value) { value = qBound(0, value, 100); if (m_progress != value) { m_progress = value; emit progressChanged(); } }
void SubtitleAiManager::setStatus(const QString &value) { if (m_status != value) { m_status = value; emit statusChanged(); } }
void SubtitleAiManager::setError(const QString &value) { if (m_error != value) { m_error = value; emit errorChanged(); } }
void SubtitleAiManager::setOutputPath(const QString &value) { if (m_outputPath != value) { m_outputPath = value; emit outputPathChanged(); } }
void SubtitleAiManager::setLastLog(const QString &value) { if (m_lastLog != value) { m_lastLog = value; emit lastLogChanged(); } }
void SubtitleAiManager::finishWithError(const QString &message)
{
    clearLiveCues();
    setError(message);
    setStatus(tr("Altyazı işlemi başarısız."));
    emit failed(message);
}
