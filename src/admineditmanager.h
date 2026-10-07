#pragma once

#include <QObject>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QHash>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

class AdminEditManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)

public:
    explicit AdminEditManager(QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    Q_INVOKABLE void openUrl(const QString &urlString);

signals:
    void busyChanged();
    void opened();
    void saved();
    void error(const QString &message);

private:
    struct Fingerprint {
        qint64 modifiedMs = -1;
        qint64 size = -1;
        QByteArray sha256;

        bool operator==(const Fingerprint &other) const
        {
            return modifiedMs == other.modifiedMs && size == other.size && sha256 == other.sha256;
        }
        bool operator!=(const Fingerprint &other) const { return !(*this == other); }
    };

    static Fingerprint fingerprintFor(const QString &path);
    static QString safeFileName(const QUrl &url);
    QString localPathFor(const QUrl &adminUrl) const;

    void setBusy(bool busy);
    void trackFile(const QString &localPath, const QUrl &adminUrl);
    void markDirty(const QString &localPath);
    void rearmWatch(const QString &localPath);
    void scanTrackedFiles();
    void syncDirtyFiles();
    void syncOne(const QString &localPath);

    QFileSystemWatcher m_watcher;
    QTemporaryDir m_tempDir;
    QTimer m_syncTimer;
    QTimer m_pollTimer;
    QHash<QString, QUrl> m_adminUrls;
    QHash<QString, Fingerprint> m_lastSynced;
    QSet<QString> m_dirty;
    QSet<QString> m_syncing;
    bool m_busy = false;
};
