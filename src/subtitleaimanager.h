#pragma once

#include <QObject>
#include <QProcess>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QVariantList>
#include <QVariantMap>

class SubtitleAiManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(int progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY outputPathChanged)
    Q_PROPERTY(QString lastLog READ lastLog NOTIFY lastLogChanged)
    Q_PROPERTY(int liveRevision READ liveRevision NOTIFY liveRevisionChanged)
    Q_PROPERTY(bool liveTranslationActive READ liveTranslationActive NOTIFY liveRevisionChanged)
    Q_PROPERTY(QString selectedSubtitlePath READ selectedSubtitlePath NOTIFY selectedSubtitleChanged)

public:
    explicit SubtitleAiManager(QObject *parent = nullptr);
    ~SubtitleAiManager() override;

    bool busy() const { return m_busy; }
    int progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString error() const { return m_error; }
    QString outputPath() const { return m_outputPath; }
    QString lastLog() const { return m_lastLog; }
    int liveRevision() const { return m_liveRevision; }
    bool liveTranslationActive() const { return m_liveTranslationActive; }
    QString selectedSubtitlePath() const { return m_selectedSubtitlePath; }

    Q_INVOKABLE bool startTranscription(const QString &filePath,
                                        const QString &language = QStringLiteral("tr"),
                                        const QString &qualityProfile = QStringLiteral("slow"),
                                        const QString &translateTo = QString(),
                                        int audioTrackIndex = 0);
    Q_INVOKABLE bool startSubtitleTranslation(const QString &mediaPath,
                                              const QString &sourceKind,
                                              const QString &subtitlePath,
                                              int subtitleTrackIndex,
                                              const QString &sourceLanguage,
                                              const QString &translateTo);
    Q_INVOKABLE QVariantList subtitleSources(const QString &filePath) const;
    Q_INVOKABLE QString liveSubtitleAt(qint64 positionMs) const;
    Q_INVOKABLE bool selectSubtitleFile(const QString &subtitlePath);
    Q_INVOKABLE bool selectEmbeddedSubtitle(const QString &mediaPath, int subtitleTrackIndex);
    Q_INVOKABLE void clearSelectedSubtitle();
    Q_INVOKABLE QString selectedSubtitleAt(qint64 positionMs) const;
    Q_INVOKABLE QVariantMap cacheInfo(const QString &filePath,
                                      const QString &language,
                                      const QString &qualityProfile,
                                      const QString &translateTo,
                                      int audioTrackIndex = 0) const;
    Q_INVOKABLE QVariantMap subtitleTranslationCacheInfo(const QString &mediaPath,
                                                         const QString &sourceKind,
                                                         const QString &subtitlePath,
                                                         int subtitleTrackIndex,
                                                         const QString &sourceLanguage,
                                                         const QString &translateTo) const;
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void resetResult();

signals:
    void busyChanged();
    void progressChanged();
    void statusChanged();
    void errorChanged();
    void outputPathChanged();
    void lastLogChanged();
    void liveRevisionChanged();
    void selectedSubtitleChanged();
    void completed(const QString &outputPath);
    void failed(const QString &message);
    void cancelled();

private slots:
    void readStandardOutput();
    void readStandardError();
    void processFinished(int exitCode, QProcess::ExitStatus exitStatus);

private:
    struct LiveCue {
        qint64 startMs = 0;
        qint64 endMs = 0;
        QString text;
    };

    QString workerPath() const;
    QString pythonExecutable() const;
    QString normalizeLocalPath(const QString &filePath) const;
    QString cacheRoot() const;
    QString cacheJobDirectory(const QString &filePath,
                              const QString &language,
                              const QString &qualityProfile,
                              int audioTrackIndex) const;
    QString subtitleCacheJobDirectory(const QString &mediaPath,
                                      const QString &sourceKind,
                                      const QString &subtitlePath,
                                      int subtitleTrackIndex,
                                      const QString &sourceLanguage) const;
    QVariantMap cacheInfoFromDirectory(const QString &directory,
                                       const QString &translateTo) const;
    QString normalizedLanguageCode(const QString &value) const;
    bool startWorker(const QStringList &arguments, const QString &initialStatus);
    void consumeLine(const QByteArray &line);
    void setBusy(bool value);
    void setProgress(int value);
    void setStatus(const QString &value);
    void setError(const QString &value);
    void setOutputPath(const QString &value);
    void setLastLog(const QString &value);
    void finishWithError(const QString &message);
    void clearLiveCues();
    void addLiveCues(const QJsonObject &object);
    QVector<LiveCue> loadSubtitleFile(const QString &subtitlePath) const;
    qint64 parseSubtitleTimestamp(const QString &value) const;

    QProcess m_process;
    QByteArray m_stdoutBuffer;
    QByteArray m_stderrBuffer;
    bool m_busy = false;
    bool m_cancelRequested = false;
    bool m_terminalEventReceived = false;
    int m_progress = 0;
    QString m_status;
    QString m_error;
    QString m_outputPath;
    QString m_lastLog;
    QVector<LiveCue> m_liveSourceCues;
    QVector<LiveCue> m_liveTranslatedCues;
    bool m_liveTranslationActive = false;
    QVector<LiveCue> m_selectedSubtitleCues;
    QString m_selectedSubtitlePath;
    int m_liveRevision = 0;
};
