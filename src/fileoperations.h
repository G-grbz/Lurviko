#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QPointer>
#include <QSet>
#include <QVector>
#include <QUrl>

namespace KIO { class CopyJob; }
class KJob;
class QProcess;
class QTimer;

class FileOperations : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY canPasteChanged)
    Q_PROPERTY(bool clipboardCut READ clipboardCut NOTIFY clipboardChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY canUndoChanged)
    Q_PROPERTY(bool shiftPressed READ shiftPressed NOTIFY shiftPressedChanged)
    Q_PROPERTY(bool kioProgressEnabled READ kioProgressEnabled WRITE setKioProgressEnabled NOTIFY kioProgressEnabledChanged)
    Q_PROPERTY(bool ioBusy READ ioBusy NOTIFY ioBusyChanged)
    Q_PROPERTY(QVariantList operations READ operations NOTIFY operationsChanged)
    Q_PROPERTY(QVariantList kdeConnectDevices READ kdeConnectDevices NOTIFY shareTargetsChanged)
    Q_PROPERTY(bool kdeConnectAvailable READ kdeConnectAvailable NOTIFY shareTargetsChanged)
    Q_PROPERTY(bool bluetoothAvailable READ bluetoothAvailable NOTIFY shareTargetsChanged)
    Q_PROPERTY(bool emailShareAvailable READ emailShareAvailable NOTIFY shareTargetsChanged)

public:
    explicit FileOperations(QObject *parent = nullptr);

    QString lastError() const { return m_lastError; }
    bool canPaste() const;
    bool clipboardCut() const;
    bool canUndo() const;
    bool shiftPressed() const { return m_shiftPressed; }
    bool kioProgressEnabled() const { return m_kioProgressEnabled; }
    bool ioBusy() const;
    void setKioProgressEnabled(bool enabled);
    QVariantList operations() const;
    QVariantList kdeConnectDevices() const { return m_kdeConnectDevices; }
    bool kdeConnectAvailable() const { return m_kdeConnectAvailable; }
    bool bluetoothAvailable() const { return m_bluetoothAvailable; }
    bool emailShareAvailable() const { return m_emailShareAvailable; }

    Q_INVOKABLE bool createFolder(const QString &parentLocation, const QString &folderName);
    Q_INVOKABLE bool createFile(const QString &parentLocation, const QString &fileName);
    Q_INVOKABLE bool createSymbolicLink(const QString &parentLocation, const QString &linkName, const QString &target);
    Q_INVOKABLE bool createHardLink(const QString &parentLocation, const QString &linkName, const QString &target);
    Q_INVOKABLE QVariantMap navigationTarget(const QString &target, const QString &basePath = QString()) const;
    Q_INVOKABLE void findHardLinkPeer(const QString &sourceUrl);
    Q_INVOKABLE bool openTerminal(const QString &location);
    Q_INVOKABLE bool openKFind(const QString &location);
    Q_INVOKABLE bool openFilelight(const QString &location);
    Q_INVOKABLE bool openFilelightHome();
    Q_INVOKABLE bool openFilelightDevice(const QString &location);
    Q_INVOKABLE bool runExecutable(const QString &sourceUrl);
    Q_INVOKABLE void copyToClipboard(const QStringList &sourceUrls, bool cut);
    Q_INVOKABLE void copyLocationToClipboard(const QString &location);
    Q_INVOKABLE bool toggleOperationPause(int id);
    Q_INVOKABLE bool cancelOperation(int id);
    Q_INVOKABLE void setOperationDismissPaused(int id, bool paused);
    Q_INVOKABLE void dismissOperation(int id);
    Q_INVOKABLE bool isCutUrl(const QString &sourceUrl) const;
    Q_INVOKABLE QStringList clipboardUrls() const;
    Q_INVOKABLE bool shiftModifierPressed() const;
    Q_INVOKABLE QVariantMap inspectPath(const QString &input) const;
    Q_INVOKABLE bool isSourceAlreadyInDestination(const QString &sourceUrl,
                                                   const QString &destinationLocation) const;
    Q_INVOKABLE bool pasteFromClipboard(const QString &destinationLocation);
    Q_INVOKABLE bool renameItem(const QString &itemUrl, const QString &newName);
    Q_INVOKABLE bool batchRename(const QStringList &itemUrls, const QString &newName, int startIndex = 1);
    Q_INVOKABLE bool undoLastOperation();
    Q_INVOKABLE void copyItem(const QString &sourceUrl, const QString &destinationLocation);
    Q_INVOKABLE void moveItem(const QString &sourceUrl, const QString &destinationLocation);
    Q_INVOKABLE void duplicateItem(const QString &sourceUrl);
    Q_INVOKABLE void duplicateMany(const QStringList &sourceUrls, const QString &parentLocation);
    Q_INVOKABLE void trashItem(const QString &itemUrl);
    Q_INVOKABLE void trashMany(const QStringList &itemUrls);
    Q_INVOKABLE void emptyTrash();
    Q_INVOKABLE void restoreFromTrash(const QStringList &itemUrls);
    Q_INVOKABLE void removeItem(const QString &itemUrl);
    Q_INVOKABLE void removeMany(const QStringList &itemUrls);
    Q_INVOKABLE void copyMany(const QStringList &sourceUrls, const QString &destinationLocation);
    Q_INVOKABLE void moveMany(const QStringList &sourceUrls, const QString &destinationLocation);
    Q_INVOKABLE void compressItem(const QString &sourceUrl, const QString &format);
    Q_INVOKABLE void compressItems(const QStringList &sourceUrls, const QString &format);
    Q_INVOKABLE void extractArchive(const QString &sourceUrl,
                                    const QString &destinationLocation,
                                    bool createSubfolder = false);
    Q_INVOKABLE void extractArchiveOverwrite(const QString &sourceUrl,
                                             const QString &destinationLocation,
                                             bool createSubfolder = false);
    Q_INVOKABLE void extractArkDrag(const QString &serviceName,
                                   const QString &objectPath,
                                   const QString &destinationLocation);
    Q_INVOKABLE void refreshShareTargets();
    Q_INVOKABLE bool shareViaKdeConnect(const QStringList &sourceUrls, const QString &deviceId);
    Q_INVOKABLE bool shareViaBluetooth(const QStringList &sourceUrls);
    Q_INVOKABLE bool shareViaEmail(const QStringList &sourceUrls);

signals:
    void lastErrorChanged();
    void canPasteChanged();
    void clipboardChanged();
    void canUndoChanged();
    void shiftPressedChanged();
    void kioProgressEnabledChanged();
    void ioBusyChanged();
    void operationsChanged();
    void shareTargetsChanged();
    void operationFinished(bool success, const QString &message, bool refreshNeeded,
                           bool externallyPresented = false);
    void transferItemsFinished(const QString &destinationLocation, const QStringList &resultUrls);
    void itemsRenamed(const QStringList &sourceUrls, const QStringList &resultUrls);
    void archiveOverwriteConfirmationRequired(const QString &sourceUrl,
                                              const QString &destinationLocation,
                                              bool createSubfolder,
                                              const QStringList &conflicts);
    void hardLinkPeerResolved(const QString &sourceUrl, const QString &peerUrl, const QString &error);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    static QUrl toUrl(const QString &value);
    bool launchFilelightPath(const QString &path);
    void setLastError(const QString &errorText);
    void finishJob(class KJob *job, const QString &successMessage, bool refreshNeeded = true);
    void trackTransferJob(KIO::CopyJob *job, const QList<QUrl> &sources,
                          const QUrl &destination, const QString &kind);
    int startOperation(const QString &kind, const QString &detail, KJob *job = nullptr, QProcess *process = nullptr);
    void setOperationProgress(int id, int percent);
    void setOperationDetail(int id, const QString &detail);
    void setOperationAmount(int id, int unit, qulonglong amount, bool total);
    void setOperationSpeed(int id, qulonglong bytesPerSecond);
    void setOperationPaused(int id, bool paused);
    void finishOperation(int id, bool success);
    void startOperationDismissTimer(int id);
    void configureProgressJob(class KJob *job, bool needsManualRegistration = false);
    void refreshClipboardCache();
    void setClipboardCache(const QList<QUrl> &urls, bool cut);
    void startArchiveExtraction(const QString &sourceUrl,
                                const QString &destinationLocation,
                                bool createSubfolder,
                                bool overwriteConfirmed);
    void runArchiveExtraction(const QString &archivePath,
                              const QString &outputPath,
                              const QString &executable);
    QStringList localSharePaths(const QStringList &sourceUrls, bool filesOnly, QString *errorText = nullptr) const;
    void updateShareAvailability();

    QString m_lastError;
    QList<QUrl> m_clipboardUrlCache;
    QSet<QString> m_clipboardUrlKeys;
    bool m_clipboardCutCached = false;
    bool m_settingOwnClipboard = false;
    int m_clipboardGeneration = 0;
    quint64 m_clipboardRevision = 0;
    bool m_clipboardPrepareActive = false;
    QStringList m_clipboardPrepareSources;
    bool m_clipboardPrepareCut = false;
    int m_clipboardPrepareOperationId = 0;
    QStringList m_lastClipboardSources;
    bool m_lastClipboardCut = false;
    bool m_lastClipboardSelectionValid = false;
    QByteArray m_clipboardOwnerToken;
    QSet<QString> m_activePasteKeys;
    bool m_shiftPressed = false;
    bool m_kioProgressEnabled = false;
    struct Operation {
        int id = 0;
        QString kind;
        QString detail;
        int percent = -1;
        qulonglong processedBytes = 0;
        qulonglong totalBytes = 0;
        qulonglong processedFiles = 0;
        qulonglong totalFiles = 0;
        qulonglong processedDirectories = 0;
        qulonglong totalDirectories = 0;
        qulonglong processedItems = 0;
        qulonglong totalItems = 0;
        qulonglong speed = 0;
        bool paused = false;
        bool finished = false;
        bool success = false;
        QPointer<KJob> job;
        QPointer<QProcess> process;
    };
    QVector<Operation> m_operations;
    QSet<int> m_cancelledOperations;
    QHash<int, QPointer<QTimer>> m_operationDismissTimers;
    int m_nextOperationId = 1;
    QVariantList m_kdeConnectDevices;
    bool m_kdeConnectAvailable = false;
    bool m_bluetoothAvailable = false;
    bool m_emailShareAvailable = false;
    QString m_kdeConnectExecutable;
    QString m_bluetoothExecutable;
    QString m_emailExecutable;
    QPointer<QProcess> m_kdeConnectDiscovery;
};
