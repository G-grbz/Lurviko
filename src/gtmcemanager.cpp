#include "gtmcemanager.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QtMath>

#include <algorithm>

#ifndef LURVIKO_SOURCE_DIR
#define LURVIKO_SOURCE_DIR ""
#endif

GtmceManager::GtmceManager(QObject *parent)
    : QObject(parent)
{
    connect(&m_process, &QProcess::readyReadStandardOutput,
            this, &GtmceManager::readStandardOutput);
    connect(&m_process, &QProcess::readyReadStandardError,
            this, &GtmceManager::readStandardError);
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &GtmceManager::processFinished);
}

GtmceManager::~GtmceManager()
{
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}

QString GtmceManager::normalizeLocalPath(const QString &filePath) const
{
    const QString value = filePath.trimmed();
    if (value.startsWith(QStringLiteral("file:"), Qt::CaseInsensitive)) {
        const QUrl url(value);
        if (url.isLocalFile())
            return url.toLocalFile();
    }
    return value;
}


QString GtmceManager::cacheJobDirectory(const QString &filePath,
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
    const QString key = QString::fromLatin1(
        QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());

    QString base = qEnvironmentVariable("LURVIKO_GTMCE_CACHE_DIR").trimmed();
    if (base.isEmpty()) {
        const QString xdg = qEnvironmentVariable("XDG_CACHE_HOME").trimmed();
        base = xdg.isEmpty()
                   ? QDir::home().absoluteFilePath(QStringLiteral(".cache/Lurviko/gtmce/jobs"))
                   : QDir(xdg).absoluteFilePath(QStringLiteral("Lurviko/gtmce/jobs"));
    }
    return QDir(base).absoluteFilePath(key);
}

QVariantMap GtmceManager::cacheInfo(const QString &filePath,
                                    const QString &language,
                                    const QString &qualityProfile,
                                    const QString &translateTo,
                                    int audioTrackIndex) const
{
    QVariantMap result;
    result.insert(QStringLiteral("sourceReady"), false);
    result.insert(QStringLiteral("detectedLanguage"), QString());
    result.insert(QStringLiteral("translationReady"), false);
    result.insert(QStringLiteral("translationProgress"), 0);
    result.insert(QStringLiteral("translationCompletedUnits"), 0);
    result.insert(QStringLiteral("translationTotalUnits"), 0);

    const QString directory = cacheJobDirectory(
        filePath, language, qualityProfile, audioTrackIndex);
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
    result[QStringLiteral("detectedLanguage")] =
        state.value(QStringLiteral("detected_language")).toString();

    const QString target = translateTo.trimmed().toLower();
    if (target.isEmpty())
        return result;

    const QJsonObject translations = state.value(QStringLiteral("translations")).toObject();
    const QJsonObject translation = translations.value(target).toObject();
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

QString GtmceManager::workerPath() const
{
    const QString overridePath = qEnvironmentVariable("LURVIKO_GTMCE_WORKER").trimmed();
    if (!overridePath.isEmpty() && QFileInfo::exists(overridePath))
        return QFileInfo(overridePath).absoluteFilePath();

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList installedCandidates = {
        QDir(appDir).absoluteFilePath(QStringLiteral("../share/Lurviko/gtmce/worker.py")),
    };
    for (const QString &candidate : installedCandidates) {
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }

    const QString sourceDir = QString::fromUtf8(LURVIKO_SOURCE_DIR);
    if (!sourceDir.isEmpty()) {
        const QString candidate = QDir(sourceDir).absoluteFilePath(QStringLiteral("tools/gtmce/worker.py"));
        if (QFileInfo::exists(candidate))
            return QFileInfo(candidate).absoluteFilePath();
    }
    return {};
}

QString GtmceManager::pythonExecutable() const
{
    const QString overridePython = qEnvironmentVariable("LURVIKO_GTMCE_PYTHON").trimmed();
    if (!overridePython.isEmpty())
        return overridePython;

    const QString python3 = QStandardPaths::findExecutable(QStringLiteral("python3"));
    if (!python3.isEmpty())
        return python3;
    return QStandardPaths::findExecutable(QStringLiteral("python"));
}

bool GtmceManager::startTranscription(const QString &filePath,
                                      const QString &language,
                                      const QString &qualityProfile,
                                      const QString &translateTo,
                                      int audioTrackIndex)
{
    if (m_busy)
        return false;

    resetResult();
    clearLiveCues();
    const QString inputPath = normalizeLocalPath(filePath);
    const QFileInfo inputInfo(inputPath);
    if (!inputInfo.exists() || !inputInfo.isFile()) {
        finishWithError(tr("Video dosyası bulunamadı: %1").arg(inputPath));
        return false;
    }

    const QString worker = workerPath();
    if (worker.isEmpty()) {
        finishWithError(tr("G-TMCE motoru bulunamadı. Lurviko'ın gtmce worker dosyaları kurulmamış."));
        return false;
    }

    const QString python = pythonExecutable();
    if (python.isEmpty()) {
        finishWithError(tr("Python 3 bulunamadı. G-TMCE altyazı motoru Python 3 gerektiriyor."));
        return false;
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("PYTHONUNBUFFERED"), QStringLiteral("1"));

    // Reuse G-TMCE's app-local Python/CUDA runtime when the normal Linux
    // installation is present. The bundled engine itself remains inside
    // Lurviko; only third-party wheels/libraries are shared.
    const QString vendorDir = qEnvironmentVariable("GTMCE_VENDOR_DIR",
                                                    QStringLiteral("/opt/G-TMCE/vendor"));
    if (QFileInfo(vendorDir).isDir()) {
        QString pythonPath = environment.value(QStringLiteral("PYTHONPATH"));
        pythonPath = vendorDir + (pythonPath.isEmpty() ? QString() : QDir::listSeparator() + pythonPath);
        environment.insert(QStringLiteral("PYTHONPATH"), pythonPath);

        QStringList cudaLibs;
        const QString cublas = QDir(vendorDir).absoluteFilePath(QStringLiteral("nvidia/cublas/lib"));
        const QString cudnn = QDir(vendorDir).absoluteFilePath(QStringLiteral("nvidia/cudnn/lib"));
        if (QFileInfo(cublas).isDir())
            cudaLibs.append(cublas);
        if (QFileInfo(cudnn).isDir())
            cudaLibs.append(cudnn);
        const QString oldLd = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
        if (!oldLd.isEmpty())
            cudaLibs.append(oldLd);
        if (!cudaLibs.isEmpty())
            environment.insert(QStringLiteral("LD_LIBRARY_PATH"), cudaLibs.join(QLatin1Char(':')));
    }

    m_process.setProcessEnvironment(environment);
    m_process.setProgram(python);
    QStringList arguments{worker,
                          QStringLiteral("--input"), inputInfo.absoluteFilePath(),
                          QStringLiteral("--language"), language.trimmed().toLower(),
                          QStringLiteral("--quality-profile"), qualityProfile.trimmed().toLower(),
                          QStringLiteral("--audio-track-index"), QString::number(qMax(0, audioTrackIndex))};
    const QString target = translateTo.trimmed().toLower();
    if (!target.isEmpty())
        arguments << QStringLiteral("--translate-to") << target;
    m_process.setArguments(arguments);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    m_stdoutBuffer.clear();
    m_stderrBuffer.clear();
    m_cancelRequested = false;
    m_terminalEventReceived = false;
    setProgress(0);
    setStatus(tr("G-TMCE hazırlanıyor…"));
    setBusy(true);
    m_process.start();
    if (!m_process.waitForStarted(1500)) {
        const QString detail = m_process.errorString();
        setBusy(false);
        finishWithError(tr("G-TMCE worker başlatılamadı: %1").arg(detail));
        return false;
    }
    return true;
}

QString GtmceManager::liveSubtitleAt(qint64 positionMs) const
{
    auto findCue = [positionMs](const QVector<LiveCue> &cues) -> QString {
        // New cues normally arrive in chronological order. Walk backwards so
        // translated replacements and the newest boundary corrections win.
        for (auto it = cues.crbegin(); it != cues.crend(); ++it) {
            if (positionMs >= it->startMs && positionMs <= it->endMs)
                return it->text;
            if (it->endMs + 1500 < positionMs)
                break;
        }
        return {};
    };

    const QString translated = findCue(m_liveTranslatedCues);
    return translated.isEmpty() ? findCue(m_liveSourceCues) : translated;
}

void GtmceManager::clearLiveCues()
{
    if (m_liveSourceCues.isEmpty() && m_liveTranslatedCues.isEmpty())
        return;
    m_liveSourceCues.clear();
    m_liveTranslatedCues.clear();
    ++m_liveRevision;
    emit liveRevisionChanged();
}

void GtmceManager::addLiveCues(const QJsonObject &object)
{
    const QString stage = object.value(QStringLiteral("stage")).toString();
    QVector<LiveCue> &target = stage == QLatin1String("translated")
                                   ? m_liveTranslatedCues
                                   : m_liveSourceCues;
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

void GtmceManager::cancel()
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

void GtmceManager::resetResult()
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

void GtmceManager::readStandardOutput()
{
    m_stdoutBuffer.append(m_process.readAllStandardOutput());
    qsizetype newline = -1;
    while ((newline = m_stdoutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuffer.left(newline).trimmed();
        m_stdoutBuffer.remove(0, newline + 1);
        if (!line.isEmpty())
            consumeLine(line);
    }
}

void GtmceManager::readStandardError()
{
    m_stderrBuffer.append(m_process.readAllStandardError());
    // Keep only a bounded diagnostic tail. Python/native libraries can be noisy.
    constexpr qsizetype maxTail = 16 * 1024;
    if (m_stderrBuffer.size() > maxTail)
        m_stderrBuffer = m_stderrBuffer.right(maxTail);
}

void GtmceManager::consumeLine(const QByteArray &line)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setLastLog(QString::fromUtf8(line));
        return;
    }

    const QJsonObject object = document.object();
    const QString event = object.value(QStringLiteral("event")).toString();
    if (event == QLatin1String("live_cues")) {
        addLiveCues(object);
        return;
    }
    if (event == QLatin1String("progress")) {
        setProgress(object.value(QStringLiteral("value")).toInt(m_progress));
        const QString message = object.value(QStringLiteral("message")).toString();
        if (!message.isEmpty())
            setStatus(message);
        return;
    }
    if (event == QLatin1String("log") || event == QLatin1String("ready")) {
        const QString message = object.value(QStringLiteral("message")).toString();
        if (!message.isEmpty()) {
            setLastLog(message);
            setStatus(message);
        }
        return;
    }
    if (event == QLatin1String("completed")) {
        m_terminalEventReceived = true;
        setProgress(100);
        setOutputPath(object.value(QStringLiteral("output")).toString());
        setStatus(tr("Altyazı oluşturuldu."));
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
        setStatus(tr("Altyazı oluşturulamadı."));
    }
}

void GtmceManager::processFinished(int exitCode, QProcess::ExitStatus exitStatus)
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
        if (!m_terminalEventReceived)
            setStatus(tr("İşlem iptal edildi."));
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
    if (detail.length() > 1200)
        detail = detail.right(1200);
    if (detail.isEmpty()) {
        detail = exitStatus == QProcess::CrashExit
                     ? tr("G-TMCE worker beklenmedik biçimde kapandı.")
                     : tr("G-TMCE worker %1 koduyla sonlandı.").arg(exitCode);
    }
    finishWithError(detail);
}

void GtmceManager::setBusy(bool value)
{
    if (m_busy == value)
        return;
    m_busy = value;
    emit busyChanged();
}

void GtmceManager::setProgress(int value)
{
    value = qBound(0, value, 100);
    if (m_progress == value)
        return;
    m_progress = value;
    emit progressChanged();
}

void GtmceManager::setStatus(const QString &value)
{
    if (m_status == value)
        return;
    m_status = value;
    emit statusChanged();
}

void GtmceManager::setError(const QString &value)
{
    if (m_error == value)
        return;
    m_error = value;
    emit errorChanged();
}

void GtmceManager::setOutputPath(const QString &value)
{
    if (m_outputPath == value)
        return;
    m_outputPath = value;
    emit outputPathChanged();
}

void GtmceManager::setLastLog(const QString &value)
{
    if (m_lastLog == value)
        return;
    m_lastLog = value;
    emit lastLogChanged();
}

void GtmceManager::finishWithError(const QString &message)
{
    clearLiveCues();
    setError(message);
    setStatus(tr("Altyazı oluşturulamadı."));
    emit failed(message);
}
