#include "fileoperations.h"
#include "archivearguments.h"

#include <QClipboard>
#include <QDir>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QFileInfo>
#include <QEvent>
#include <QKeyEvent>
#include <QGuiApplication>
#include <QFutureWatcher>
#include <QMimeData>
#include <QMimeDatabase>
#include <QProcess>
#include <QRegularExpression>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <memory>
#include <utility>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include <KIO/CopyJob>
#include <KIO/BatchRenameJob>
#include <KIO/DeleteJob>
#include <KIO/EmptyTrashJob>
#include <KIO/RestoreJob>
#include <KIO/FileUndoManager>
#include <KIO/JobTracker>
#include <KIO/MkpathJob>
#include <KIO/SimpleJob>
#include <KIO/StoredTransferJob>
#include <KTerminalLauncherJob>
#include <KJob>
#include <KJobTrackerInterface>

namespace {
struct PreparedClipboardData {
    QList<QUrl> urls;
    QByteArray uriList;
};

QString clipboardUrlKey(const QUrl &url)
{
    return url.adjusted(QUrl::NormalizePathSegments | QUrl::StripTrailingSlash)
        .toString(QUrl::FullyEncoded);
}
}

// Archive tools run outside KIO.  Expose their QProcess as a KJob so Plasma's
// job tracker can show the same notification and controls as file transfers.
class ArchiveProgressJob final : public KJob
{
public:
    ArchiveProgressJob(QProcess *process, QString executable, QStringList arguments,
                       QString title, QString detail, QObject *parent)
        : KJob(parent), m_process(process), m_executable(std::move(executable)),
          m_arguments(std::move(arguments)), m_title(std::move(title)), m_detail(std::move(detail))
    {
        setCapabilities(KJob::Killable | KJob::Suspendable);
    }

    void start() override
    {
        startElapsedTimer();
        QTimer::singleShot(0, this, [this] {
            if (!m_process)
                return;
            emit description(this, m_title, {QStringLiteral("File"), m_detail});
            m_process->start(m_executable, m_arguments);
        });
    }

    void updateProgress(int percent) { setPercent(static_cast<unsigned long>(qBound(0, percent, 100))); }

    void complete(bool success, const QString &error = QString())
    {
        if (!success) {
            setError(KJob::UserDefinedError);
            setErrorText(error);
        } else {
            setPercent(100);
        }
        emitResult();
    }

    QString errorString() const override { return errorText(); }

protected:
    bool doKill() override
    {
        if (!m_process)
            return false;
        if (isSuspended() && m_process->processId() > 0)
            ::kill(static_cast<pid_t>(m_process->processId()), SIGCONT);
        m_process->kill();
        return true;
    }

    bool doSuspend() override
    {
        return m_process && m_process->processId() > 0
            && ::kill(static_cast<pid_t>(m_process->processId()), SIGSTOP) == 0;
    }

    bool doResume() override
    {
        return m_process && m_process->processId() > 0
            && ::kill(static_cast<pid_t>(m_process->processId()), SIGCONT) == 0;
    }

private:
    QPointer<QProcess> m_process;
    QString m_executable;
    QStringList m_arguments;
    QString m_title;
    QString m_detail;
};

FileOperations::FileOperations(QObject *parent)
    : QObject(parent)
{
    connect(QGuiApplication::clipboard(), &QClipboard::dataChanged, this, [this]() {
        const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
        const QByteArray ownerToken = mime
            ? mime->data(QStringLiteral("application/x-gfile-clipboard-owner"))
            : QByteArray{};
        const bool ownPreparedClipboard = !m_clipboardOwnerToken.isEmpty()
            && ownerToken == m_clipboardOwnerToken;

        if (!m_settingOwnClipboard && !ownPreparedClipboard) {
            // Any clipboard change not produced by our own prepared payload
            // invalidates the completed-selection dedup state.
            m_lastClipboardSources.clear();
            m_lastClipboardSelectionValid = false;
            m_clipboardOwnerToken.clear();
            refreshClipboardCache();
        }
        emit canPasteChanged();
        emit clipboardChanged();
    });
    refreshClipboardCache();
    connect(KIO::FileUndoManager::self(), &KIO::FileUndoManager::undoAvailable,
            this, [this](bool) { emit canUndoChanged(); });

    // Track modifier keys from the application event stream itself.  Reading
    // QGuiApplication::keyboardModifiers() from QML only causes bindings to
    // refresh when some unrelated QML event (often mouse movement) happens,
    // which made Shift+Trash feel delayed and erratic.
    if (auto *application = QGuiApplication::instance())
        application->installEventFilter(this);

    refreshShareTargets();
}

bool FileOperations::canPaste() const
{
    return !m_clipboardUrlCache.isEmpty();
}

bool FileOperations::clipboardCut() const
{
    return !m_clipboardUrlCache.isEmpty() && m_clipboardCutCached;
}

QStringList FileOperations::clipboardUrls() const
{
    QStringList values;
    values.reserve(m_clipboardUrlCache.size());
    for (const QUrl &url : m_clipboardUrlCache)
        values.push_back(url.toString(QUrl::FullyEncoded));
    return values;
}

bool FileOperations::ioBusy() const
{
    for (const Operation &operation : m_operations) {
        // Clipboard preparation only serializes URL strings in a worker. It is
        // not disk I/O and must not freeze thumbnail/search refresh policy.
        if (!operation.finished && operation.kind != QStringLiteral("clipboard"))
            return true;
    }
    return false;
}

void FileOperations::setClipboardCache(const QList<QUrl> &urls, bool cut)
{
    const bool normalizedCut = cut && !urls.isEmpty();
    if (m_clipboardUrlCache == urls && m_clipboardCutCached == normalizedCut)
        return;

    m_clipboardUrlCache = urls;
    m_clipboardUrlKeys.clear();
    m_clipboardUrlKeys.reserve(urls.size());
    for (const QUrl &url : urls)
        m_clipboardUrlKeys.insert(clipboardUrlKey(url));
    m_clipboardCutCached = normalizedCut;
    ++m_clipboardRevision;
}

void FileOperations::refreshClipboardCache()
{
    const QMimeData *mimeData = QGuiApplication::clipboard()->mimeData();
    if (!mimeData || !mimeData->hasUrls()) {
        setClipboardCache({}, false);
        return;
    }
    const QList<QUrl> urls = mimeData->urls();
    const bool cut = mimeData->data(QStringLiteral("application/x-kde-cutselection")) == QByteArrayLiteral("1");
    setClipboardCache(urls, cut);
}

bool FileOperations::canUndo() const
{
    return KIO::FileUndoManager::self()->isUndoAvailable();
}

bool FileOperations::shiftModifierPressed() const
{
    return m_shiftPressed;
}

QVariantMap FileOperations::inspectPath(const QString &input) const
{
    QVariantMap result;
    const QUrl url = toUrl(input);
    result.insert(QStringLiteral("url"), url.toString(QUrl::FullyEncoded));
    result.insert(QStringLiteral("exists"), false);
    result.insert(QStringLiteral("isDir"), false);

    if (!url.isLocalFile())
        return result;

    const QFileInfo info(url.toLocalFile());
    if (!info.exists())
        return result;

    result.insert(QStringLiteral("exists"), true);
    result.insert(QStringLiteral("isDir"), info.isDir());
    result.insert(QStringLiteral("name"), info.fileName());
    result.insert(QStringLiteral("localPath"), info.absoluteFilePath());

    if (!info.isDir()) {
        QMimeDatabase database;
        const QMimeType mime = database.mimeTypeForFile(info, QMimeDatabase::MatchExtension);
        result.insert(QStringLiteral("mimeType"), mime.name());
    }
    return result;
}

bool FileOperations::isSourceAlreadyInDestination(const QString &sourceUrl,
                                                        const QString &destinationLocation) const
{
    const QUrl source = toUrl(sourceUrl).adjusted(QUrl::NormalizePathSegments);
    const QUrl destination = toUrl(destinationLocation).adjusted(QUrl::NormalizePathSegments
                                                                   | QUrl::StripTrailingSlash);
    if (!source.isValid() || !destination.isValid())
        return false;

    // For local paths, compare canonical directories when possible so harmless
    // spelling differences (file:///..., /..., symlinks, dot segments) do not
    // turn a same-folder drop into a copy/move onto itself.
    if (source.isLocalFile() && destination.isLocalFile()) {
        const QFileInfo sourceInfo(source.toLocalFile());
        const QFileInfo destinationInfo(destination.toLocalFile());

        QString sourceParent = sourceInfo.absoluteDir().canonicalPath();
        if (sourceParent.isEmpty())
            sourceParent = sourceInfo.absoluteDir().absolutePath();

        QString destinationPath = destinationInfo.canonicalFilePath();
        if (destinationPath.isEmpty())
            destinationPath = destinationInfo.absoluteFilePath();

        return QDir::cleanPath(sourceParent) == QDir::cleanPath(destinationPath);
    }

    const QUrl sourceParent = source.adjusted(QUrl::RemoveFilename
                                               | QUrl::StripTrailingSlash
                                               | QUrl::NormalizePathSegments);
    return sourceParent == destination;
}

bool FileOperations::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    bool nextShift = m_shiftPressed;
    switch (event->type()) {
    case QEvent::KeyPress: {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Shift)
            nextShift = true;
        else
            nextShift = keyEvent->modifiers().testFlag(Qt::ShiftModifier);
        break;
    }
    case QEvent::KeyRelease: {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Shift)
            nextShift = false;
        else
            nextShift = keyEvent->modifiers().testFlag(Qt::ShiftModifier);
        break;
    }
    case QEvent::ApplicationDeactivate:
        nextShift = false;
        break;
    default:
        break;
    }

    if (nextShift != m_shiftPressed) {
        m_shiftPressed = nextShift;
        emit shiftPressedChanged();
    }

    return QObject::eventFilter(watched, event);
}

bool FileOperations::isCutUrl(const QString &sourceUrl) const
{
    if (!clipboardCut())
        return false;
    return m_clipboardUrlKeys.contains(clipboardUrlKey(toUrl(sourceUrl)));
}

QUrl FileOperations::toUrl(const QString &value)
{
    const QString trimmed = value.trimmed();
    const QUrl candidate(trimmed);
    // KIO schemes such as trash:/ are valid URLs even though they don't use
    // the :// spelling.  Preserve all explicit schemes, including admin:/.
    if (candidate.isValid() && !candidate.scheme().isEmpty())
        return candidate;
    QString localPath = trimmed;
    if (localPath == QStringLiteral("~"))
        localPath = QDir::homePath();
    else if (localPath.startsWith(QStringLiteral("~/")))
        localPath = QDir::homePath() + localPath.mid(1);
    return QUrl::fromLocalFile(QDir(localPath).absolutePath());
}

void FileOperations::setLastError(const QString &errorText)
{
    if (m_lastError == errorText)
        return;
    m_lastError = errorText;
    emit lastErrorChanged();
}

void FileOperations::finishJob(KJob *job, const QString &successMessage, bool refreshNeeded)
{
    configureProgressJob(job);
    connect(job, &KJob::result, this, [this, job, successMessage, refreshNeeded]() {
        bool trackedOperation = false;
        for (const Operation &operation : std::as_const(m_operations)) {
            if (operation.job == job) {
                trackedOperation = true;
                break;
            }
        }
        const bool resultAlreadyPresented = m_kioProgressEnabled || trackedOperation;
        if (job->error()) {
            setLastError(job->errorString());
            emit operationFinished(false, job->errorString(), false, resultAlreadyPresented);
        } else {
            setLastError(QString());
            emit operationFinished(true, successMessage, refreshNeeded, resultAlreadyPresented);
        }
    });
}

QVariantList FileOperations::operations() const
{
    QVariantList items;
    for (const Operation &operation : m_operations) {
        // Clipboard preparation is not a KIO job, so keep its lightweight
        // in-app status visible even when Plasma handles KIO transfer progress.
        if (m_kioProgressEnabled && operation.kind != QStringLiteral("clipboard"))
            continue;
        QVariantMap item;
        item.insert(QStringLiteral("id"), operation.id);
        item.insert(QStringLiteral("kind"), operation.kind);
        item.insert(QStringLiteral("detail"), operation.detail);
        item.insert(QStringLiteral("percent"), operation.percent);
        item.insert(QStringLiteral("processedBytes"), QVariant::fromValue(operation.processedBytes));
        item.insert(QStringLiteral("totalBytes"), QVariant::fromValue(operation.totalBytes));
        const qulonglong processedItems = qMax(operation.processedItems,
                                               operation.processedFiles + operation.processedDirectories);
        const qulonglong totalItems = qMax(operation.totalItems,
                                           operation.totalFiles + operation.totalDirectories);
        item.insert(QStringLiteral("processedItems"), QVariant::fromValue(processedItems));
        item.insert(QStringLiteral("totalItems"), QVariant::fromValue(totalItems));
        item.insert(QStringLiteral("speed"), QVariant::fromValue(operation.speed));
        item.insert(QStringLiteral("paused"), operation.paused);
        item.insert(QStringLiteral("controllable"), operation.job || operation.process);
        item.insert(QStringLiteral("finished"), operation.finished);
        item.insert(QStringLiteral("success"), operation.success);
        items.push_back(item);
    }
    return items;
}

void FileOperations::setKioProgressEnabled(bool enabled)
{
    if (m_kioProgressEnabled == enabled)
        return;
    m_kioProgressEnabled = enabled;

    // Move active operations to the selected presentation immediately.
    for (const Operation &operation : std::as_const(m_operations)) {
        if (!operation.finished && operation.job) {
            if (enabled)
                KIO::getJobTracker()->registerJob(operation.job);
            else
                KIO::getJobTracker()->unregisterJob(operation.job);
        }
    }
    emit kioProgressEnabledChanged();
    emit operationsChanged();
}

void FileOperations::configureProgressJob(KJob *job, bool needsManualRegistration)
{
    if (!job)
        return;
    if (m_kioProgressEnabled) {
        if (needsManualRegistration)
            KIO::getJobTracker()->registerJob(job);
    } else {
        // The overwrite/rename UI delegate remains attached. Only Plasma's
        // progress presentation is removed in integrated-card mode.
        KIO::getJobTracker()->unregisterJob(job);
    }
}

int FileOperations::startOperation(const QString &kind, const QString &detail, KJob *job, QProcess *process)
{
    const bool wasBusy = ioBusy();
    Operation operation;
    operation.id = m_nextOperationId++;
    operation.kind = kind;
    operation.detail = detail;
    operation.job = job;
    operation.process = process;
    m_operations.push_back(operation);
    emit operationsChanged();
    if (wasBusy != ioBusy())
        emit ioBusyChanged();
    return operation.id;
}

void FileOperations::setOperationProgress(int id, int percent)
{
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished || operation.percent == percent)
            continue;
        operation.percent = qBound(-1, percent, 100);
        emit operationsChanged();
        return;
    }
}

void FileOperations::setOperationDetail(int id, const QString &detail)
{
    if (detail.isEmpty())
        return;
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished || operation.detail == detail)
            continue;
        operation.detail = detail;
        emit operationsChanged();
        return;
    }
}

void FileOperations::setOperationAmount(int id, int unit, qulonglong amount, bool total)
{
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished)
            continue;
        qulonglong *target = nullptr;
        switch (static_cast<KJob::Unit>(unit)) {
        case KJob::Bytes:
            target = total ? &operation.totalBytes : &operation.processedBytes;
            break;
        case KJob::Files:
            target = total ? &operation.totalFiles : &operation.processedFiles;
            break;
        case KJob::Directories:
            target = total ? &operation.totalDirectories : &operation.processedDirectories;
            break;
        case KJob::Items:
            target = total ? &operation.totalItems : &operation.processedItems;
            break;
        default:
            return;
        }
        if (*target == amount)
            return;
        *target = amount;
        emit operationsChanged();
        return;
    }
}

void FileOperations::setOperationSpeed(int id, qulonglong bytesPerSecond)
{
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished || operation.speed == bytesPerSecond)
            continue;
        operation.speed = operation.speed > 0 && bytesPerSecond > 0
            ? (operation.speed * 3 + bytesPerSecond) / 4
            : bytesPerSecond;
        emit operationsChanged();
        return;
    }
}

void FileOperations::setOperationPaused(int id, bool paused)
{
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished || operation.paused == paused)
            continue;
        operation.paused = paused;
        emit operationsChanged();
        return;
    }
}

void FileOperations::finishOperation(int id, bool success)
{
    const bool wasBusy = ioBusy();
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished)
            continue;

        operation.finished = true;
        operation.success = success;
        operation.paused = false;
        operation.speed = 0;
        if (success) {
            operation.percent = 100;
            // Some KIO workers only publish a total item count. On successful
            // completion it is safe to show that all requested top-level items
            // were processed, while retaining file/directory counters when
            // workers supplied them.
            if (operation.totalItems > 0 && operation.processedItems < operation.totalItems)
                operation.processedItems = operation.totalItems;
            if (operation.totalBytes > 0 && operation.processedBytes < operation.totalBytes)
                operation.processedBytes = operation.totalBytes;
        }
        // Start the dismiss clock before publishing the finished state. If the
        // pointer is already over the card, QML can stop this timer immediately
        // from the same operationsChanged notification without racing a later
        // timer start.
        startOperationDismissTimer(id);
        emit operationsChanged();
        if (wasBusy != ioBusy())
            emit ioBusyChanged();
        return;
    }
}

void FileOperations::startOperationDismissTimer(int id)
{
    QTimer *timer = m_operationDismissTimers.value(id);
    if (!timer) {
        timer = new QTimer(this);
        timer->setSingleShot(true);
        timer->setInterval(5000);
        m_operationDismissTimers.insert(id, timer);
        connect(timer, &QTimer::timeout, this, [this, id] {
            dismissOperation(id);
        });
    }
    timer->start(5000);
}

void FileOperations::setOperationDismissPaused(int id, bool paused)
{
    bool finished = false;
    for (const Operation &operation : std::as_const(m_operations)) {
        if (operation.id == id) {
            finished = operation.finished;
            break;
        }
    }
    if (!finished)
        return;

    QTimer *timer = m_operationDismissTimers.value(id);
    if (paused) {
        if (timer)
            timer->stop();
        return;
    }

    // Leaving hover always grants a fresh full reading period.
    startOperationDismissTimer(id);
}

void FileOperations::dismissOperation(int id)
{
    if (QTimer *timer = m_operationDismissTimers.take(id)) {
        timer->stop();
        timer->deleteLater();
    }

    for (qsizetype index = 0; index < m_operations.size(); ++index) {
        if (m_operations.at(index).id != id || !m_operations.at(index).finished)
            continue;
        m_operations.removeAt(index);
        emit operationsChanged();
        return;
    }
}

bool FileOperations::toggleOperationPause(int id)
{
    for (const Operation &operation : std::as_const(m_operations)) {
        if (operation.id != id || operation.finished)
            continue;
        if (operation.job)
            return operation.paused ? operation.job->resume() : operation.job->suspend();
        if (operation.process && operation.process->processId() > 0) {
            const bool nextPaused = !operation.paused;
            if (::kill(static_cast<pid_t>(operation.process->processId()), nextPaused ? SIGSTOP : SIGCONT) == 0) {
                setOperationPaused(id, nextPaused);
                return true;
            }
        }
        return false;
    }
    return false;
}

bool FileOperations::cancelOperation(int id)
{
    for (const Operation &operation : std::as_const(m_operations)) {
        if (operation.id != id || operation.finished)
            continue;
        m_cancelledOperations.insert(id);
        if (operation.job) {
            const bool killed = operation.job->kill(KJob::EmitResult);
            if (!killed)
                m_cancelledOperations.remove(id);
            return killed;
        }
        if (operation.process) {
            // A paused process must be resumed before SIGKILL can be reaped.
            if (operation.paused && operation.process->processId() > 0)
                ::kill(static_cast<pid_t>(operation.process->processId()), SIGCONT);
            operation.process->kill();
            return true;
        }
        return false;
    }
    return false;
}

void FileOperations::trackTransferJob(KIO::CopyJob *job, const QList<QUrl> &sources,
                                      const QUrl &destination, const QString &kind)
{
    const QString detail = sources.size() == 1 ? sources.constFirst().fileName()
                          : QStringLiteral("%1 items").arg(sources.size());
    const int operationId = startOperation(kind, detail, job);
    setOperationAmount(operationId, int(KJob::Items), qulonglong(sources.size()), true);
    connect(job, &KJob::percentChanged, this, [this, operationId](KJob *, unsigned long percent) {
        setOperationProgress(operationId, int(percent));
    });
    connect(job, &KJob::totalAmountChanged, this,
            [this, operationId](KJob *, KJob::Unit unit, qulonglong amount) {
        setOperationAmount(operationId, int(unit), amount, true);
    });
    connect(job, &KJob::processedAmountChanged, this,
            [this, operationId](KJob *, KJob::Unit unit, qulonglong amount) {
        setOperationAmount(operationId, int(unit), amount, false);
    });
    connect(job, &KJob::speed, this, [this, operationId](KJob *, unsigned long speed) {
        setOperationSpeed(operationId, qulonglong(speed));
    });
    // CopyJob also exposes legacy file/directory counters. Keep them as a
    // fallback for workers that do not publish the newer KJob units.
    connect(job, &KIO::CopyJob::processedFiles, this,
            [this, operationId](KIO::Job *, unsigned long files) {
        setOperationAmount(operationId, int(KJob::Files), qulonglong(files), false);
    });
    connect(job, &KIO::CopyJob::processedDirs, this,
            [this, operationId](KIO::Job *, unsigned long directories) {
        setOperationAmount(operationId, int(KJob::Directories), qulonglong(directories), false);
    });
    connect(job, &KIO::CopyJob::copying, this,
            [this, operationId](KIO::Job *, const QUrl &source, const QUrl &) {
        setOperationDetail(operationId, source.fileName());
    });
    connect(job, &KIO::CopyJob::moving, this,
            [this, operationId](KIO::Job *, const QUrl &source, const QUrl &) {
        setOperationDetail(operationId, source.fileName());
    });
    connect(job, &KIO::CopyJob::creatingDir, this,
            [this, operationId](KIO::Job *, const QUrl &directory) {
        setOperationDetail(operationId, directory.fileName());
    });
    connect(job, &KJob::suspended, this, [this, operationId] { setOperationPaused(operationId, true); });
    connect(job, &KJob::resumed, this, [this, operationId] { setOperationPaused(operationId, false); });
    connect(job, &KJob::result, this, [this, job, operationId] {
        finishOperation(operationId, !job->error());
        m_cancelledOperations.remove(operationId);
    });
    auto completed = std::make_shared<QHash<QString, QUrl>>();
    auto sourceKeys = std::make_shared<QSet<QString>>();
    for (const QUrl &source : sources)
        sourceKeys->insert(source.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded));

    connect(job, &KIO::CopyJob::copyingDone, this,
            [completed, sourceKeys](KIO::Job *, const QUrl &from, const QUrl &to,
                                    const QDateTime &, bool, bool) {
        const QString key = from.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded);
        if (sourceKeys->contains(key))
            completed->insert(key, to);
    });
    connect(job, &KJob::result, this, [this, job, sources, destination, completed] {
        if (job->error())
            return;
        QStringList resultUrls;
        for (const QUrl &source : sources) {
            const QString key = source.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded);
            const QUrl target = completed->value(key);
            // Do not guess a destination when KIO never emitted copyingDone for
            // this top-level source. In particular, a conflict answered with
            // Skip/Skip All must not later appear as a newly selected result.
            if (!target.isEmpty() && !resultUrls.contains(target.toString()))
                resultUrls.push_back(target.toString());
        }
        emit transferItemsFinished(destination.toString(), resultUrls);
    });
}

bool FileOperations::createFolder(const QString &parentLocation, const QString &folderName)
{
    const QString trimmed = folderName.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("Folder name cannot be empty."));
        return false;
    }

    QUrl destination = toUrl(parentLocation);
    QString path = destination.path();
    if (!path.endsWith('/'))
        path += '/';
    path += trimmed;
    destination.setPath(path);

    auto *job = KIO::mkpath(destination);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Mkpath, {}, destination, job);
    finishJob(job, QStringLiteral("Folder created"));
    return true;
}

bool FileOperations::createFile(const QString &parentLocation, const QString &fileName)
{
    const QString trimmed = fileName.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("File name cannot be empty."));
        return false;
    }

    QUrl destination = toUrl(parentLocation);
    QString path = destination.path();
    if (!path.endsWith('/'))
        path += '/';
    path += trimmed;
    destination.setPath(path);

    auto *job = KIO::storedPut(QByteArray(), destination, -1);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Put, {}, destination, job);
    finishJob(job, QStringLiteral("File created"));
    return true;
}


bool FileOperations::createSymbolicLink(const QString &parentLocation, const QString &linkName, const QString &target)
{
    const QString name = linkName.trimmed();
    const QString rawTarget = target.trimmed();
    const QUrl parentUrl = toUrl(parentLocation);
    if (!parentUrl.isLocalFile()) {
        setLastError(QStringLiteral("Symbolic links can currently be created only in local folders."));
        return false;
    }
    if (name.isEmpty() || name.contains(QLatin1Char('/'))) {
        setLastError(QStringLiteral("Link name is invalid."));
        return false;
    }
    if (rawTarget.isEmpty()) {
        setLastError(QStringLiteral("Symbolic link target cannot be empty."));
        return false;
    }

    QString linkValue = rawTarget;
    const QUrl candidate(rawTarget);
    if (candidate.isValid() && candidate.isLocalFile())
        linkValue = candidate.toLocalFile();
    else if (candidate.isValid() && !candidate.scheme().isEmpty()) {
        setLastError(QStringLiteral("A symbolic link target must be a local path."));
        return false;
    }

    const QString destinationPath = QDir(parentUrl.toLocalFile()).filePath(name);
    const QFileInfo destinationInfo(destinationPath);
    if (destinationInfo.exists() || destinationInfo.isSymLink()) {
        setLastError(QStringLiteral("An item with this name already exists."));
        return false;
    }

    const QByteArray nativeTarget = QFile::encodeName(linkValue);
    const QByteArray nativeDestination = QFile::encodeName(destinationPath);
    if (::symlink(nativeTarget.constData(), nativeDestination.constData()) != 0) {
        setLastError(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    setLastError(QString());
    emit operationFinished(true, QStringLiteral("Symbolic link created"), true);
    return true;
}

bool FileOperations::createHardLink(const QString &parentLocation, const QString &linkName, const QString &target)
{
    const QString name = linkName.trimmed();
    const QString rawTarget = target.trimmed();
    const QUrl parentUrl = toUrl(parentLocation);
    if (!parentUrl.isLocalFile()) {
        setLastError(QStringLiteral("Hard links can currently be created only in local folders."));
        return false;
    }
    if (name.isEmpty() || name.contains(QLatin1Char('/'))) {
        setLastError(QStringLiteral("Link name is invalid."));
        return false;
    }
    if (rawTarget.isEmpty()) {
        setLastError(QStringLiteral("Hard link target cannot be empty."));
        return false;
    }

    QString targetPath;
    const QUrl candidate(rawTarget);
    if (candidate.isValid() && candidate.isLocalFile())
        targetPath = candidate.toLocalFile();
    else if (candidate.isValid() && !candidate.scheme().isEmpty()) {
        setLastError(QStringLiteral("A hard link target must be a local file."));
        return false;
    } else {
        targetPath = QDir(parentUrl.toLocalFile()).absoluteFilePath(rawTarget);
    }
    targetPath = QDir::cleanPath(targetPath);

    const QFileInfo targetInfo(targetPath);
    if (!targetInfo.exists() || !targetInfo.isFile() || targetInfo.isSymLink()) {
        setLastError(QStringLiteral("Hard link target must be an existing regular file."));
        return false;
    }

    const QString destinationPath = QDir(parentUrl.toLocalFile()).filePath(name);
    const QFileInfo destinationInfo(destinationPath);
    if (destinationInfo.exists() || destinationInfo.isSymLink()) {
        setLastError(QStringLiteral("An item with this name already exists."));
        return false;
    }

    const QByteArray nativeTarget = QFile::encodeName(targetPath);
    const QByteArray nativeDestination = QFile::encodeName(destinationPath);
    if (::link(nativeTarget.constData(), nativeDestination.constData()) != 0) {
        setLastError(QString::fromLocal8Bit(std::strerror(errno)));
        return false;
    }

    setLastError(QString());
    emit operationFinished(true, QStringLiteral("Hard link created"), true);
    return true;
}

QVariantMap FileOperations::navigationTarget(const QString &target, const QString &basePath) const
{
    QVariantMap result;
    const QString raw = target.trimmed();
    if (raw.isEmpty())
        return result;

    QUrl url(raw);
    if (url.isValid() && !url.scheme().isEmpty()) {
        if (url.isLocalFile()) {
            const QString path = QDir::cleanPath(url.toLocalFile());
            const QFileInfo info(path);
            result.insert(QStringLiteral("itemUrl"), QUrl::fromLocalFile(path).toString());
            result.insert(QStringLiteral("parentLocation"), info.absolutePath());
            result.insert(QStringLiteral("localPath"), path);
            result.insert(QStringLiteral("exists"), info.exists() || info.isSymLink());
            return result;
        }
        QUrl parentUrl = url;
        QString path = parentUrl.path();
        while (path.length() > 1 && path.endsWith(QLatin1Char('/')))
            path.chop(1);
        const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
        parentUrl.setPath(slash <= 0 ? QStringLiteral("/") : path.left(slash));
        result.insert(QStringLiteral("itemUrl"), url.toString());
        result.insert(QStringLiteral("parentLocation"), parentUrl.toString());
        result.insert(QStringLiteral("exists"), true);
        return result;
    }

    QString baseDirectory;
    if (!basePath.trimmed().isEmpty()) {
        const QFileInfo baseInfo(basePath);
        baseDirectory = baseInfo.isDir() ? baseInfo.absoluteFilePath() : baseInfo.absolutePath();
    }
    if (baseDirectory.isEmpty())
        baseDirectory = QDir::currentPath();
    const QString path = QDir::cleanPath(QDir(baseDirectory).absoluteFilePath(raw));
    const QFileInfo info(path);
    result.insert(QStringLiteral("itemUrl"), QUrl::fromLocalFile(path).toString());
    result.insert(QStringLiteral("parentLocation"), info.absolutePath());
    result.insert(QStringLiteral("localPath"), path);
    result.insert(QStringLiteral("exists"), info.exists() || info.isSymLink());
    return result;
}

void FileOperations::findHardLinkPeer(const QString &sourceUrl)
{
    const QUrl url = toUrl(sourceUrl);
    if (!url.isLocalFile()) {
        emit hardLinkPeerResolved(sourceUrl, QString(), QStringLiteral("Hard links are local filesystem entries."));
        return;
    }
    const QString sourcePath = QDir::cleanPath(url.toLocalFile());
    struct stat sourceStat {};
    const QByteArray nativeSource = QFile::encodeName(sourcePath);
    if (::lstat(nativeSource.constData(), &sourceStat) != 0 || !S_ISREG(sourceStat.st_mode) || sourceStat.st_nlink < 2) {
        emit hardLinkPeerResolved(sourceUrl, QString(), QStringLiteral("No hard-link peer was found."));
        return;
    }

    // Cheap first pass: most hard links are created next to one another.
    const QDir parent(QFileInfo(sourcePath).absolutePath());
    const QFileInfoList siblings = parent.entryInfoList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &candidate : siblings) {
        const QString path = QDir::cleanPath(candidate.absoluteFilePath());
        if (path == sourcePath)
            continue;
        struct stat st {};
        const QByteArray nativeCandidate = QFile::encodeName(path);
        if (::lstat(nativeCandidate.constData(), &st) == 0
                && st.st_dev == sourceStat.st_dev && st.st_ino == sourceStat.st_ino) {
            emit hardLinkPeerResolved(sourceUrl, QUrl::fromLocalFile(path).toString(), QString());
            return;
        }
    }

    const QString findExecutable = QStandardPaths::findExecutable(QStringLiteral("find"));
    if (findExecutable.isEmpty()) {
        emit hardLinkPeerResolved(sourceUrl, QString(), QStringLiteral("The find utility is not available."));
        return;
    }

    QStorageInfo storage(sourcePath);
    storage.refresh();
    QString searchRoot = storage.isValid() ? storage.rootPath() : QString();
    if (searchRoot.isEmpty())
        searchRoot = QStringLiteral("/");

    auto *process = new QProcess(this);
    connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
            [this, process, sourceUrl](int exitCode, QProcess::ExitStatus status) {
        const QString output = QString::fromLocal8Bit(process->readAllStandardOutput()).trimmed();
        const QString errorOutput = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        process->deleteLater();
        if (!output.isEmpty()) {
            const QString peerPath = output.section(QLatin1Char('\n'), 0, 0).trimmed();
            emit hardLinkPeerResolved(sourceUrl, QUrl::fromLocalFile(peerPath).toString(), QString());
            return;
        }
        if (status != QProcess::NormalExit || exitCode != 0 || output.isEmpty()) {
            emit hardLinkPeerResolved(sourceUrl, QString(),
                                      errorOutput.isEmpty() ? QStringLiteral("No hard-link peer was found.") : errorOutput);
            return;
        }
    });
    process->start(findExecutable, {searchRoot, QStringLiteral("-xdev"), QStringLiteral("-samefile"), sourcePath,
                                    QStringLiteral("!"), QStringLiteral("-path"), sourcePath,
                                    QStringLiteral("-print"), QStringLiteral("-quit")});
}

bool FileOperations::openTerminal(const QString &location)
{
    const QUrl url = toUrl(location);
    if (!url.isLocalFile() || !QFileInfo(url.toLocalFile()).isDir()) {
        const QString error = QStringLiteral("A terminal can only be opened in a local folder.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    auto *job = new KTerminalLauncherJob(QString(), this);
    job->setWorkingDirectory(url.toLocalFile());
    connect(job, &KJob::result, this, [this, job]() {
        if (job->error()) {
            setLastError(job->errorString());
            emit operationFinished(false, job->errorString(), false);
        } else {
            setLastError(QString());
        }
    });
    job->start();
    return true;
}


bool FileOperations::openKFind(const QString &location)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("kfind"));
    if (executable.isEmpty()) {
        const QString error = QStringLiteral("KFind is not installed. Install the 'kfind' package to use advanced search.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    const QUrl url = toUrl(location);
    const QString searchPath = url.isLocalFile() ? url.toLocalFile() : url.toString();
    if (!QProcess::startDetached(executable, {searchPath})) {
        const QString error = QStringLiteral("KFind could not be started.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    setLastError(QString());
    return true;
}

bool FileOperations::launchFilelightPath(const QString &path)
{
    const QString executable = QStandardPaths::findExecutable(QStringLiteral("filelight"));
    if (executable.isEmpty()) {
        const QString error = QStringLiteral("Filelight is not installed. Install the 'filelight' package to analyze disk usage.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    const QFileInfo info(path);
    if (!info.exists() || !info.isDir()) {
        const QString error = QStringLiteral("Filelight can only open an existing local folder.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    const QString cleanPath = QDir::cleanPath(info.absoluteFilePath());
    if (!QProcess::startDetached(executable, {cleanPath})) {
        const QString error = QStringLiteral("Filelight could not be started.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    setLastError(QString());
    return true;
}

bool FileOperations::openFilelight(const QString &location)
{
    const QUrl url = toUrl(location);
    if (!url.isLocalFile()) {
        const QString error = QStringLiteral("Filelight can only analyze local folders.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }
    return launchFilelightPath(url.toLocalFile());
}

bool FileOperations::openFilelightHome()
{
    return launchFilelightPath(QDir::homePath());
}

bool FileOperations::openFilelightDevice(const QString &location)
{
    const QUrl url = toUrl(location);
    if (!url.isLocalFile()) {
        const QString error = QStringLiteral("Filelight can only analyze local devices.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    const QFileInfo info(url.toLocalFile());
    const QString localPath = info.isDir() ? info.absoluteFilePath() : info.absolutePath();
    QStorageInfo storage(localPath);
    storage.refresh();
    if (!storage.isValid() || !storage.isReady() || storage.rootPath().isEmpty()) {
        const QString error = QStringLiteral("The storage device for this location could not be determined.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    return launchFilelightPath(storage.rootPath());
}

bool FileOperations::runExecutable(const QString &sourceUrl)
{
    const QUrl source = toUrl(sourceUrl);
    if (!source.isLocalFile()) {
        const QString error = QStringLiteral("Only local executables can be started.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    const QFileInfo info(source.toLocalFile());
    if (!info.isFile()) {
        const QString error = QStringLiteral("Executable file does not exist.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    if (!info.isExecutable()) {
        QFile file(info.absoluteFilePath());
        const QFileDevice::Permissions permissions = file.permissions()
            | QFileDevice::ExeOwner;
        if (!file.setPermissions(permissions)) {
            const QString error = QStringLiteral("Could not grant execute permission to the file.");
            setLastError(error);
            emit operationFinished(false, error, false);
            return false;
        }
    }

    if (!QProcess::startDetached(info.absoluteFilePath(), {}, info.absolutePath())) {
        const QString error = QStringLiteral("Could not start executable.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    setLastError(QString());
    emit operationFinished(true, QStringLiteral("Application started."), false);
    return true;
}

void FileOperations::copyToClipboard(const QStringList &sourceUrls, bool cut)
{
    if (sourceUrls.isEmpty())
        return;

    // Keyboard repeat / muscle-memory Ctrl+C should be idempotent for the
    // exact same selection. This covers both an in-flight preparation and the
    // common case where the worker already finished before the next keypress.
    if (m_clipboardPrepareActive
            && m_clipboardPrepareCut == cut
            && m_clipboardPrepareSources == sourceUrls) {
        return;
    }
    if (!m_clipboardPrepareActive
            && m_lastClipboardSelectionValid
            && m_lastClipboardCut == cut
            && !m_clipboardUrlCache.isEmpty()
            && m_lastClipboardSources == sourceUrls) {
        return;
    }

    if (m_clipboardPrepareActive && m_clipboardPrepareOperationId > 0)
        finishOperation(m_clipboardPrepareOperationId, false);

    const int generation = ++m_clipboardGeneration;
    m_clipboardPrepareActive = true;
    m_clipboardPrepareSources = sourceUrls;
    m_clipboardPrepareCut = cut;
    // Do not allow Paste to use an older clipboard payload while the newly
    // selected 10k+ item list is still being prepared.
    setClipboardCache({}, false);
    emit canPasteChanged();
    emit clipboardChanged();
    const int operationId = startOperation(QStringLiteral("clipboard"),
                                           QStringLiteral("%1 items").arg(sourceUrls.size()));
    m_clipboardPrepareOperationId = operationId;
    setOperationAmount(operationId, int(KJob::Items), qulonglong(sourceUrls.size()), true);

    auto *watcher = new QFutureWatcher<PreparedClipboardData>(this);
    connect(watcher, &QFutureWatcher<PreparedClipboardData>::finished, this,
            [this, watcher, generation, operationId, cut, sourceUrls]() {
        const PreparedClipboardData prepared = watcher->result();
        watcher->deleteLater();
        if (generation != m_clipboardGeneration) {
            finishOperation(operationId, false);
            return;
        }

        m_clipboardPrepareActive = false;
        m_clipboardPrepareSources.clear();
        m_clipboardPrepareOperationId = 0;

        if (prepared.urls.isEmpty()) {
            refreshClipboardCache();
            emit canPasteChanged();
            emit clipboardChanged();
            finishOperation(operationId, false);
            const QString error = QStringLiteral("The clipboard does not contain files.");
            setLastError(error);
            emit operationFinished(false, error, false, true);
            return;
        }

        setClipboardCache(prepared.urls, cut);
        m_lastClipboardSources = sourceUrls;
        m_lastClipboardCut = cut;
        m_lastClipboardSelectionValid = true;
        auto *mimeData = new QMimeData;
        mimeData->setData(QStringLiteral("text/uri-list"), prepared.uriList);
        mimeData->setData(QStringLiteral("application/x-kde-cutselection"),
                          cut ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));
        m_clipboardOwnerToken = QByteArray::number(generation);
        mimeData->setData(QStringLiteral("application/x-gfile-clipboard-owner"),
                          m_clipboardOwnerToken);
        m_settingOwnClipboard = true;
        QGuiApplication::clipboard()->setMimeData(mimeData);
        m_settingOwnClipboard = false;
        emit canPasteChanged();
        emit clipboardChanged();

        setOperationAmount(operationId, int(KJob::Items), qulonglong(prepared.urls.size()), false);
        setLastError(QString());
        finishOperation(operationId, true);
        emit operationFinished(true, cut ? QStringLiteral("Ready to move")
                                         : QStringLiteral("Copied to clipboard"),
                               false, true);
    });

    watcher->setFuture(QtConcurrent::run([sourceUrls]() {
        PreparedClipboardData prepared;
        prepared.urls.reserve(sourceUrls.size());
        QByteArray uriList;
        uriList.reserve(sourceUrls.size() * 64);
        for (const QString &source : sourceUrls) {
            const QUrl url = FileOperations::toUrl(source);
            if (!url.isValid())
                continue;
            prepared.urls.push_back(url);
            uriList += url.toEncoded(QUrl::FullyEncoded);
            uriList += "\r\n";
        }
        prepared.uriList = std::move(uriList);
        return prepared;
    }));
}

void FileOperations::copyLocationToClipboard(const QString &location)
{
    const QUrl url = toUrl(location);
    QGuiApplication::clipboard()->setText(url.isLocalFile() ? url.toLocalFile() : location);
}

bool FileOperations::pasteFromClipboard(const QString &destinationLocation)
{
    if (m_clipboardUrlCache.isEmpty())
        refreshClipboardCache();
    if (m_clipboardUrlCache.isEmpty()) {
        const QString error = QStringLiteral("The clipboard does not contain files.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    const QUrl destination = toUrl(destinationLocation).adjusted(QUrl::NormalizePathSegments
                                                                 | QUrl::StripTrailingSlash);
    const QString pasteKey = QString::number(m_clipboardRevision)
        + QLatin1Char('|') + destination.toString(QUrl::FullyEncoded);
    if (m_activePasteKeys.contains(pasteKey))
        return false;

    const QList<QUrl> urls = m_clipboardUrlCache;
    const bool cut = m_clipboardCutCached;
    m_activePasteKeys.insert(pasteKey);

    KIO::CopyJob *job = cut
        ? KIO::move(urls, destination)
        : KIO::copy(urls, destination);
    connect(job, &KJob::result, this, [this, pasteKey] {
        m_activePasteKeys.remove(pasteKey);
    });
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, urls, destination, cut ? QStringLiteral("move") : QStringLiteral("copy"));
    if (cut) {
        connect(job, &KJob::result, this, [job]() {
            if (!job->error())
                QGuiApplication::clipboard()->clear();
        });
    }
    finishJob(job, cut ? QStringLiteral("Moved") : QStringLiteral("Pasted"));
    return true;
}

bool FileOperations::renameItem(const QString &itemUrl, const QString &newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("New name cannot be empty."));
        return false;
    }

    const QUrl source = toUrl(itemUrl);
    QUrl destination = source;
    QString path = destination.path();
    const int slash = path.lastIndexOf('/');
    path = (slash >= 0 ? path.left(slash + 1) : QStringLiteral("/")) + trimmed;
    destination.setPath(path);

    auto *job = KIO::moveAs(source, destination);
    const auto renamedDestination = std::make_shared<QUrl>();
    connect(job, &KIO::CopyJob::copyingDone, this,
            [source, renamedDestination](KIO::Job *, const QUrl &from, const QUrl &to, const QDateTime &, bool, bool) {
        if (from.adjusted(QUrl::StripTrailingSlash) == source.adjusted(QUrl::StripTrailingSlash))
            *renamedDestination = to;
    });
    connect(job, &KJob::result, this, [this, job, source, renamedDestination]() {
        if (!job->error()) {
            QStringList results;
            if (!renamedDestination->isEmpty())
                results.push_back(renamedDestination->toString());
            emit itemsRenamed({source.toString()}, results);
        }
    });
    const QUrl parent = source.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Rename, {source}, parent, job);
    finishJob(job, QStringLiteral("Renamed"));
    return true;
}

bool FileOperations::batchRename(const QStringList &itemUrls, const QString &newName, int startIndex)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty()) {
        setLastError(QStringLiteral("New name cannot be empty."));
        return false;
    }

    QList<QUrl> urls;
    urls.reserve(itemUrls.size());
    for (const QString &value : itemUrls) {
        const QUrl url = toUrl(value);
        if (url.isValid())
            urls.push_back(url);
    }
    if (urls.size() < 2) {
        setLastError(QStringLiteral("Select at least two items for batch rename."));
        return false;
    }

    auto *job = KIO::batchRename(urls, trimmed, qMax(0, startIndex), QLatin1Char('#'));
    const auto renamed = std::make_shared<QHash<QUrl, QUrl>>();
    connect(job, &KIO::BatchRenameJob::fileRenamed, this,
            [renamed](const QUrl &source, const QUrl &destination) { renamed->insert(source, destination); });
    connect(job, &KJob::result, this, [this, job, urls, renamed]() {
        if (job->error())
            return;
        QStringList sources;
        QStringList results;
        for (const QUrl &source : urls) {
            if (!renamed->contains(source))
                continue;
            sources.push_back(source.toString());
            results.push_back(renamed->value(source).toString());
        }
        emit itemsRenamed(sources, results);
    });
    const QUrl parent = urls.constFirst().adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::BatchRename, urls, parent, job);
    finishJob(job, QStringLiteral("Renamed"));
    return true;
}

bool FileOperations::undoLastOperation()
{
    auto *manager = KIO::FileUndoManager::self();
    if (!manager->isUndoAvailable()) {
        setLastError(QStringLiteral("There is no operation to undo."));
        return false;
    }

    setLastError(QString());
    connect(manager, &KIO::FileUndoManager::undoJobFinished, this, [this]() {
        emit operationFinished(true, QStringLiteral("Operation undone"), true);
    }, Qt::SingleShotConnection);
    manager->undo();
    return true;
}

void FileOperations::copyItem(const QString &sourceUrl, const QString &destinationLocation)
{
    if (isSourceAlreadyInDestination(sourceUrl, destinationLocation))
        return;
    const QUrl source = toUrl(sourceUrl);
    const QUrl destination = toUrl(destinationLocation);
    auto *job = KIO::copy(source, destination);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, {source}, destination, QStringLiteral("copy"));
    finishJob(job, QStringLiteral("Copied"));
}

void FileOperations::moveItem(const QString &sourceUrl, const QString &destinationLocation)
{
    if (isSourceAlreadyInDestination(sourceUrl, destinationLocation))
        return;
    const QUrl source = toUrl(sourceUrl);
    const QUrl destination = toUrl(destinationLocation);
    auto *job = KIO::move(source, destination);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, {source}, destination, QStringLiteral("move"));
    finishJob(job, QStringLiteral("Moved"));
}

void FileOperations::duplicateItem(const QString &sourceUrl)
{
    const QUrl source = toUrl(sourceUrl);
    QUrl parent = source.adjusted(QUrl::RemoveFilename | QUrl::StripTrailingSlash);
    auto *job = KIO::copy(source, parent);
    job->setAutoRename(true);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, {source}, parent, QStringLiteral("duplicate"));
    finishJob(job, QStringLiteral("Duplicated"));
}

void FileOperations::duplicateMany(const QStringList &sourceUrls, const QString &parentLocation)
{
    QList<QUrl> urls;
    urls.reserve(sourceUrls.size());
    for (const QString &value : sourceUrls)
        urls.push_back(toUrl(value));
    if (urls.isEmpty())
        return;
    const QUrl destination = toUrl(parentLocation);
    auto *job = KIO::copy(urls, destination);
    job->setAutoRename(true);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, urls, destination, QStringLiteral("duplicate"));
    finishJob(job, QStringLiteral("Duplicated"));
}

void FileOperations::trashItem(const QString &itemUrl)
{
    const QUrl source = toUrl(itemUrl);
    auto *job = KIO::trash(source);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, {source},
                                            QUrl(QStringLiteral("trash:/")), job);
    finishJob(job, QStringLiteral("Moved to trash"));
}

void FileOperations::trashMany(const QStringList &itemUrls)
{
    QList<QUrl> urls;
    urls.reserve(itemUrls.size());
    for (const QString &value : itemUrls)
        urls.push_back(toUrl(value));
    if (urls.isEmpty())
        return;
    auto *job = KIO::trash(urls);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, urls,
                                            QUrl(QStringLiteral("trash:/")), job);
    finishJob(job, QStringLiteral("Moved to trash"));
}

void FileOperations::emptyTrash()
{
    auto *job = KIO::emptyTrash();
    finishJob(job, QStringLiteral("Trash emptied"));
}

void FileOperations::restoreFromTrash(const QStringList &itemUrls)
{
    QList<QUrl> urls;
    urls.reserve(itemUrls.size());
    for (const QString &value : itemUrls) {
        const QUrl url = toUrl(value);
        if (url.isValid() && url.scheme() == QStringLiteral("trash"))
            urls.push_back(url);
    }
    if (urls.isEmpty())
        return;

    // RestoreJob reads the original destination from the trash metadata. Keep
    // its normal KIO UI delegate alive: if the original path already contains
    // an item with the same name, KDE presents its overwrite/rename/skip flow.
    auto *job = KIO::restoreFromTrash(urls);
    finishJob(job, QStringLiteral("Restored from trash"));
}

void FileOperations::removeItem(const QString &itemUrl)
{
    auto *job = KIO::del(toUrl(itemUrl));
    finishJob(job, QStringLiteral("Deleted"));
}

void FileOperations::removeMany(const QStringList &itemUrls)
{
    QList<QUrl> urls;
    urls.reserve(itemUrls.size());
    for (const QString &value : itemUrls)
        urls.push_back(toUrl(value));
    if (urls.isEmpty())
        return;
    auto *job = KIO::del(urls);
    finishJob(job, QStringLiteral("Deleted"));
}

void FileOperations::copyMany(const QStringList &sourceUrls, const QString &destinationLocation)
{
    QList<QUrl> urls;
    urls.reserve(sourceUrls.size());
    for (const QString &value : sourceUrls) {
        if (!isSourceAlreadyInDestination(value, destinationLocation))
            urls.push_back(toUrl(value));
    }
    if (urls.isEmpty())
        return;
    const QUrl destination = toUrl(destinationLocation);
    auto *job = KIO::copy(urls, destination);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, urls, destination, QStringLiteral("copy"));
    finishJob(job, QStringLiteral("Copied"));
}

void FileOperations::moveMany(const QStringList &sourceUrls, const QString &destinationLocation)
{
    QList<QUrl> urls;
    urls.reserve(sourceUrls.size());
    for (const QString &value : sourceUrls) {
        if (!isSourceAlreadyInDestination(value, destinationLocation))
            urls.push_back(toUrl(value));
    }
    if (urls.isEmpty())
        return;
    const QUrl destination = toUrl(destinationLocation);
    auto *job = KIO::move(urls, destination);
    KIO::FileUndoManager::self()->recordCopyJob(job);
    trackTransferJob(job, urls, destination, QStringLiteral("move"));
    finishJob(job, QStringLiteral("Moved"));
}


void FileOperations::compressItem(const QString &sourceUrl, const QString &format)
{
    compressItems(QStringList{sourceUrl}, format);
}

void FileOperations::compressItems(const QStringList &sourceUrls, const QString &format)
{
    QList<QFileInfo> sources;
    QSet<QString> seenPaths;
    sources.reserve(sourceUrls.size());

    for (const QString &sourceUrl : sourceUrls) {
        const QUrl source = toUrl(sourceUrl);
        if (!source.isLocalFile()) {
            const QString error = QStringLiteral("Compression currently requires local files or folders.");
            setLastError(error);
            emit operationFinished(false, error, false);
            return;
        }

        const QFileInfo info(source.toLocalFile());
        if (!info.exists()) {
            const QString error = QStringLiteral("Source does not exist: %1").arg(info.fileName());
            setLastError(error);
            emit operationFinished(false, error, false);
            return;
        }

        const QString absolutePath = info.absoluteFilePath();
        if (seenPaths.contains(absolutePath))
            continue;
        seenPaths.insert(absolutePath);
        sources.push_back(info);
    }

    if (sources.isEmpty())
        return;

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (executable.isEmpty()) {
        const QString error = QStringLiteral("bsdtar was not found. Install the libarchive package.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    const QString normalizedFormat = format.trimmed().toLower();
    QString extension;
    if (normalizedFormat == QStringLiteral("zip")) {
        extension = QStringLiteral(".zip");
    } else if (normalizedFormat == QStringLiteral("7z")) {
        extension = QStringLiteral(".7z");
    } else if (normalizedFormat == QStringLiteral("tar")) {
        extension = QStringLiteral(".tar");
    } else if (normalizedFormat == QStringLiteral("tar.gz")) {
        extension = QStringLiteral(".tar.gz");
    } else if (normalizedFormat == QStringLiteral("tar.bz2")) {
        extension = QStringLiteral(".tar.bz2");
    } else if (normalizedFormat == QStringLiteral("tar.xz")) {
        extension = QStringLiteral(".tar.xz");
    } else if (normalizedFormat == QStringLiteral("tar.zst")) {
        extension = QStringLiteral(".tar.zst");
    } else {
        const QString error = QStringLiteral("Unsupported archive format: %1").arg(format);
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    // Keep the archive next to the selection.  Normal directory views select
    // siblings, while aggregate/category views may contain different parents;
    // in that case the first selected item's parent is the least surprising
    // writable destination and -C keeps every source path relative in archive.
    QDir parent = sources.constFirst().dir();
    QString baseName;
    if (sources.size() == 1) {
        const QFileInfo &info = sources.constFirst();
        baseName = info.isDir() ? info.fileName() : info.completeBaseName();
        if (baseName.isEmpty())
            baseName = info.fileName();
    } else {
        baseName = QStringLiteral("Archive");
    }

    QString archivePath = parent.filePath(baseName + extension);
    int counter = 1;
    while (QFileInfo::exists(archivePath)) {
        archivePath = parent.filePath(QStringLiteral("%1 (%2)%3")
                                      .arg(baseName).arg(counter++).arg(extension));
    }

    QStringList args;
    // bsdtar's auto-compress mode selects both the archive format (Zip/7z/Tar)
    // and the compression filter (gzip/bzip2/xz/zstd) from the filename suffix.
    // This keeps all formats on one code path and preserves the per-source -C
    // handling below for category views containing mixed parent directories.
    args << QStringLiteral("-a")
         << QStringLiteral("-cf")
         << archivePath;

    qulonglong inputBytes = 0;
    for (const QFileInfo &info : std::as_const(sources)) {
        // -C before each operand avoids embedding absolute paths even when a
        // category selection contains files from different directories.
        args << ArchiveArguments::sourceOperand(info);
        if (info.isFile() && info.size() > 0)
            inputBytes += qulonglong(info.size());
    }

    const QString detail = sources.size() == 1
        ? sources.constFirst().fileName()
        : QStringLiteral("%1 items").arg(sources.size());

    auto *process = new QProcess(this);
    process->setWorkingDirectory(parent.absolutePath());
    auto *job = new ArchiveProgressJob(process, executable, args, QStringLiteral("Creating archive"),
                                       detail, this);
    QPointer<ArchiveProgressJob> trackedJob(job);
    const int operationId = startOperation(QStringLiteral("compress"), detail, job, process);
    setOperationAmount(operationId, int(KJob::Items), qulonglong(sources.size()), true);

    connect(job, &KJob::result, this, [this, process, archivePath, operationId](KJob *result) {
        if (result->error() == KJob::KilledJobError) {
            m_cancelledOperations.insert(operationId);
            if (process->state() == QProcess::NotRunning) {
                m_cancelledOperations.remove(operationId);
                QFile::remove(archivePath);
                finishOperation(operationId, false);
                emit operationFinished(false, QStringLiteral("Cancelled"), false, true);
            }
        }
    });
    connect(job, &KJob::suspended, this, [this, operationId] { setOperationPaused(operationId, true); });
    connect(job, &KJob::resumed, this, [this, operationId] { setOperationPaused(operationId, false); });

    if (inputBytes > 0) {
        auto *progressTimer = new QTimer(process);
        progressTimer->setInterval(180);
        connect(progressTimer, &QTimer::timeout, this,
                [this, operationId, archivePath, inputBytes, trackedJob] {
            const qulonglong written = qulonglong(qMax<qint64>(0, QFileInfo(archivePath).size()));
            const int percent = int(qMin<qulonglong>(95, written * 100 / inputBytes));
            setOperationProgress(operationId, percent);
            if (trackedJob)
                trackedJob->updateProgress(percent);
        });
        progressTimer->start();
    }

    connect(process, &QProcess::finished, this,
            [this, process, archivePath, operationId, trackedJob](int exitCode, QProcess::ExitStatus status) {
        const bool cancelled = m_cancelledOperations.remove(operationId);
        const bool ok = !cancelled && status == QProcess::NormalExit && exitCode == 0;
        const QString processError = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (trackedJob && !cancelled)
            trackedJob->complete(ok, processError.isEmpty() ? QStringLiteral("Compression failed.") : processError);
        finishOperation(operationId, ok);
        if (cancelled)
            QFile::remove(archivePath);
        if (ok) {
            setLastError(QString());
            emit transferItemsFinished(QUrl::fromLocalFile(QFileInfo(archivePath).absolutePath()).toString(),
                                       {QUrl::fromLocalFile(archivePath).toString()});
            emit operationFinished(true, QStringLiteral("Archive created: %1").arg(QFileInfo(archivePath).fileName()), true,
                                   true);
        } else {
            QString error = cancelled ? QStringLiteral("Cancelled") : processError;
            if (error.isEmpty())
                error = QStringLiteral("Compression failed.");
            setLastError(error);
            emit operationFinished(false, error, false, true);
        }
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, operationId, trackedJob](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (trackedJob)
            trackedJob->complete(false, process->errorString());
        finishOperation(operationId, false);
        setLastError(process->errorString());
        emit operationFinished(false, process->errorString(), false, true);
        process->deleteLater();
    });

    configureProgressJob(job, true);
    job->start();
}

void FileOperations::extractArchive(const QString &sourceUrl,
                                    const QString &destinationLocation,
                                    bool createSubfolder)
{
    startArchiveExtraction(sourceUrl, destinationLocation, createSubfolder, false);
}

void FileOperations::extractArchiveOverwrite(const QString &sourceUrl,
                                             const QString &destinationLocation,
                                             bool createSubfolder)
{
    startArchiveExtraction(sourceUrl, destinationLocation, createSubfolder, true);
}

void FileOperations::startArchiveExtraction(const QString &sourceUrl,
                                            const QString &destinationLocation,
                                            bool createSubfolder,
                                            bool overwriteConfirmed)
{
    const QUrl source = toUrl(sourceUrl);
    const QUrl destination = toUrl(destinationLocation);
    if (!source.isLocalFile() || !destination.isLocalFile()) {
        const QString error = QStringLiteral("Archive extraction currently requires local files and folders.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    const QFileInfo archiveInfo(source.toLocalFile());
    if (!archiveInfo.isFile()) {
        const QString error = QStringLiteral("Archive does not exist.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    QString outputPath = destination.toLocalFile();
    if (createSubfolder) {
        QString folderName = archiveInfo.fileName();
        const QStringList suffixes = {
            QStringLiteral(".tar.gz"), QStringLiteral(".tar.bz2"), QStringLiteral(".tar.xz"),
            QStringLiteral(".tar.zst"), QStringLiteral(".tgz"), QStringLiteral(".tbz2"),
            QStringLiteral(".txz"), QStringLiteral(".zip"), QStringLiteral(".rar"),
            QStringLiteral(".7z"), QStringLiteral(".tar"), QStringLiteral(".gz"),
            QStringLiteral(".bz2"), QStringLiteral(".xz"), QStringLiteral(".zst")
        };
        for (const QString &suffix : suffixes) {
            if (folderName.endsWith(suffix, Qt::CaseInsensitive)) {
                folderName.chop(suffix.size());
                break;
            }
        }
        if (folderName.isEmpty())
            folderName = archiveInfo.completeBaseName();
        outputPath = QDir(outputPath).filePath(folderName);
    }

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (executable.isEmpty()) {
        const QString error = QStringLiteral("7-Zip was not found.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    if (overwriteConfirmed) {
        if (!QDir().mkpath(outputPath)) {
            const QString error = QStringLiteral("Could not create extraction folder: %1").arg(outputPath);
            setLastError(error);
            emit operationFinished(false, error, false);
            return;
        }
        runArchiveExtraction(archiveInfo.absoluteFilePath(), outputPath, executable);
        return;
    }

    // Ask before overwriting just like Ark drag-and-drop does.  7-Zip's own
    // interactive overwrite prompt cannot be used from a detached QProcess, so
    // first inspect the archive and compare its file entries with the target.
    auto *probe = new QProcess(this);
    connect(probe, &QProcess::finished, this,
            [this, probe, sourceUrl, destinationLocation, createSubfolder,
             archivePath = archiveInfo.absoluteFilePath(), outputPath, executable]
            (int exitCode, QProcess::ExitStatus status) {
        const QByteArray stdoutData = probe->readAllStandardOutput();
        const QByteArray stderrData = probe->readAllStandardError();
        probe->deleteLater();

        if (status != QProcess::NormalExit || exitCode != 0) {
            QString error = QString::fromLocal8Bit(stderrData).trimmed();
            if (error.isEmpty())
                error = QString::fromLocal8Bit(stdoutData).trimmed();
            if (error.isEmpty())
                error = QStringLiteral("Could not inspect archive contents.");
            setLastError(error);
            emit operationFinished(false, error, false);
            return;
        }

        QStringList conflicts;
        bool entriesStarted = false;
        QString entryPath;
        bool entryIsFolder = false;

        auto finishRecord = [&]() {
            if (entryPath.isEmpty())
                return;
            QString relative = QDir::cleanPath(entryPath);
            entryPath.clear();
            const bool isFolder = entryIsFolder;
            entryIsFolder = false;

            if (relative.isEmpty() || relative == QStringLiteral(".")
                    || QDir::isAbsolutePath(relative)
                    || relative == QStringLiteral("..")
                    || relative.startsWith(QStringLiteral("../")))
                return;

            const QFileInfo target(QDir(outputPath).filePath(relative));
            // Existing directories can be merged safely.  Ask only for entries
            // that would replace a file/symlink or change a directory into a file.
            if (!target.exists() && !target.isSymLink())
                return;
            if (isFolder && target.isDir())
                return;
            if (conflicts.size() < 100)
                conflicts.push_back(relative);
        };

        const QString listing = QString::fromLocal8Bit(stdoutData);
        const QStringList lines = listing.split(QLatin1Char('\n'));
        for (QString line : lines) {
            if (line.endsWith(QLatin1Char('\r')))
                line.chop(1);
            if (!entriesStarted) {
                if (line.trimmed() == QStringLiteral("----------"))
                    entriesStarted = true;
                continue;
            }
            if (line.isEmpty()) {
                finishRecord();
                continue;
            }
            if (line.startsWith(QStringLiteral("Path = "))) {
                finishRecord();
                entryPath = line.mid(7);
            } else if (line.startsWith(QStringLiteral("Folder = "))) {
                entryIsFolder = line.mid(9).trimmed() == QStringLiteral("+");
            }
        }
        finishRecord();

        if (!conflicts.isEmpty()) {
            emit archiveOverwriteConfirmationRequired(sourceUrl, destinationLocation,
                                                       createSubfolder, conflicts);
            return;
        }

        if (!QDir().mkpath(outputPath)) {
            const QString error = QStringLiteral("Could not create extraction folder: %1").arg(outputPath);
            setLastError(error);
            emit operationFinished(false, error, false);
            return;
        }
        runArchiveExtraction(archivePath, outputPath, executable);
    });

    probe->start(executable, {QStringLiteral("l"), QStringLiteral("-slt"),
                              archiveInfo.absoluteFilePath()});
}

void FileOperations::runArchiveExtraction(const QString &archivePath,
                                          const QString &outputPath,
                                          const QString &executable)
{
    auto *process = new QProcess(this);
    const QStringList args = {
        QStringLiteral("x"), archivePath,
        QStringLiteral("-o%1").arg(outputPath), QStringLiteral("-aoa"), QStringLiteral("-y"),
        QStringLiteral("-bsp1")
    };
    auto *job = new ArchiveProgressJob(process, executable, args, QStringLiteral("Extracting archive"),
                                       QFileInfo(archivePath).fileName(), this);
    QPointer<ArchiveProgressJob> trackedJob(job);
    const int operationId = startOperation(QStringLiteral("extract"), QFileInfo(archivePath).fileName(), job, process);
    connect(job, &KJob::result, this, [this, process, operationId](KJob *result) {
        if (result->error() == KJob::KilledJobError) {
            m_cancelledOperations.insert(operationId);
            if (process->state() == QProcess::NotRunning) {
                m_cancelledOperations.remove(operationId);
                finishOperation(operationId, false);
                emit operationFinished(false, QStringLiteral("Cancelled"), false, true);
            }
        }
    });
    connect(job, &KJob::suspended, this, [this, operationId] { setOperationPaused(operationId, true); });
    connect(job, &KJob::resumed, this, [this, operationId] { setOperationPaused(operationId, false); });
    auto lastOutput = std::make_shared<QString>();
    connect(process, &QProcess::readyReadStandardOutput, this, [this, process, operationId, lastOutput, trackedJob] {
        const QString chunk = QString::fromLocal8Bit(process->readAllStandardOutput());
        if (!chunk.isEmpty())
            *lastOutput = (*lastOutput + chunk).right(2048);
        static const QRegularExpression percentPattern(QStringLiteral(R"((\d{1,3})%)"));
        QRegularExpressionMatchIterator matches = percentPattern.globalMatch(*lastOutput);
        int latest = -1;
        while (matches.hasNext())
            latest = matches.next().captured(1).toInt();
        if (latest >= 0) {
            setOperationProgress(operationId, qMin(95, latest));
            if (trackedJob)
                trackedJob->updateProgress(qMin(95, latest));
        }
    });
    connect(process, &QProcess::finished, this,
            [this, process, outputPath, operationId, lastOutput, trackedJob](int exitCode, QProcess::ExitStatus status) {
        const bool cancelled = m_cancelledOperations.remove(operationId);
        const bool ok = !cancelled && status == QProcess::NormalExit && exitCode == 0;
        const QString processError = QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        if (trackedJob && !cancelled)
            trackedJob->complete(ok, processError.isEmpty() ? QStringLiteral("Archive extraction failed.") : processError);
        finishOperation(operationId, ok);
        if (ok) {
            setLastError(QString());
            emit operationFinished(true,
                                   QStringLiteral("Extracted to: %1").arg(QDir::toNativeSeparators(outputPath)), true,
                                   true);
        } else {
            QString error = cancelled ? QStringLiteral("Cancelled") : processError;
            if (error.isEmpty())
                error = lastOutput->trimmed();
            if (error.isEmpty())
                error = QStringLiteral("Archive extraction failed.");
            setLastError(error);
            emit operationFinished(false, error, false, true);
        }
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, operationId, trackedJob](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (trackedJob)
            trackedJob->complete(false, process->errorString());
        finishOperation(operationId, false);
        setLastError(process->errorString());
        emit operationFinished(false, process->errorString(), false, true);
        process->deleteLater();
    });

    // Conflicts have already been checked (or explicitly accepted), so -aoa is
    // now safe and keeps the extraction non-interactive.
    configureProgressJob(job, true);
    job->start();
}

void FileOperations::extractArkDrag(const QString &serviceName,
                                    const QString &objectPath,
                                    const QString &destinationLocation)
{
    const QUrl destination = toUrl(destinationLocation);
    if (!destination.isLocalFile() || !QFileInfo(destination.toLocalFile()).isDir()
            || serviceName.isEmpty() || !objectPath.startsWith(QLatin1Char('/'))) {
        const QString error = QStringLiteral("Invalid Ark drag and drop destination.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    QDBusInterface interface(serviceName, objectPath,
                             QStringLiteral("org.kde.ark.DndExtract"),
                             QDBusConnection::sessionBus());
    if (!interface.isValid()) {
        const QString error = QStringLiteral("Could not connect to Ark's extraction service.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    // Ark's D-Bus drag-and-drop API does not expose a byte/file percentage,
    // but its async reply is delivered when the extraction request finishes.
    // Track that lifetime in the integrated operation card so users still get
    // immediate feedback instead of a misleading "started" toast at the end.
    const QString destinationDetail = QStringLiteral("Ark → %1")
        .arg(QDir::toNativeSeparators(destination.toLocalFile()));
    const int operationId = startOperation(QStringLiteral("extract"), destinationDetail);

    auto *watcher = new QDBusPendingCallWatcher(
        interface.asyncCall(QStringLiteral("extractSelectedFilesTo"), destination.toLocalFile()), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, operationId](QDBusPendingCallWatcher *finishedWatcher) {
        const QDBusPendingReply<> reply = *finishedWatcher;
        if (reply.isError()) {
            const QString error = reply.error().message();
            setLastError(error);
            finishOperation(operationId, false);
            emit operationFinished(false, error, false, !m_kioProgressEnabled);
        } else {
            setLastError(QString());
            finishOperation(operationId, true);
            emit operationFinished(true, QStringLiteral("Archive extraction completed."), true,
                                   !m_kioProgressEnabled);
        }
        finishedWatcher->deleteLater();
    });
}


QStringList FileOperations::localSharePaths(const QStringList &sourceUrls, bool filesOnly, QString *errorText) const
{
    QStringList paths;
    paths.reserve(sourceUrls.size());

    for (const QString &source : sourceUrls) {
        const QUrl url = toUrl(source);
        if (!url.isLocalFile()) {
            if (errorText)
                *errorText = QStringLiteral("Only local files can be shared with this target.");
            return {};
        }

        const QFileInfo info(url.toLocalFile());
        if (!info.exists()) {
            if (errorText)
                *errorText = QStringLiteral("A selected file no longer exists: %1").arg(info.fileName());
            return {};
        }
        if (filesOnly && info.isDir()) {
            if (errorText)
                *errorText = QStringLiteral("Folders cannot be sent with this sharing method.");
            return {};
        }
        paths.append(info.absoluteFilePath());
    }

    if (paths.isEmpty() && errorText)
        *errorText = QStringLiteral("No local files selected.");
    return paths;
}

void FileOperations::updateShareAvailability()
{
    const bool oldKdeConnect = m_kdeConnectAvailable;
    const bool oldBluetooth = m_bluetoothAvailable;
    const bool oldEmail = m_emailShareAvailable;

    m_kdeConnectExecutable = QStandardPaths::findExecutable(QStringLiteral("kdeconnect-cli"));
    m_kdeConnectAvailable = !m_kdeConnectExecutable.isEmpty();

    m_bluetoothExecutable = QStandardPaths::findExecutable(QStringLiteral("bluedevil-sendfile"));
    if (m_bluetoothExecutable.isEmpty())
        m_bluetoothExecutable = QStandardPaths::findExecutable(QStringLiteral("bluetooth-sendto"));
    if (m_bluetoothExecutable.isEmpty())
        m_bluetoothExecutable = QStandardPaths::findExecutable(QStringLiteral("blueman-sendto"));

    const QDir bluetoothClass(QStringLiteral("/sys/class/bluetooth"));
    const bool adapterPresent = bluetoothClass.exists()
        && !bluetoothClass.entryList(QStringList{QStringLiteral("hci*")}, QDir::Dirs | QDir::NoDotAndDotDot).isEmpty();
    m_bluetoothAvailable = !m_bluetoothExecutable.isEmpty() && adapterPresent;

    m_emailExecutable = QStandardPaths::findExecutable(QStringLiteral("xdg-email"));
    m_emailShareAvailable = !m_emailExecutable.isEmpty();

    if (oldKdeConnect != m_kdeConnectAvailable
            || oldBluetooth != m_bluetoothAvailable
            || oldEmail != m_emailShareAvailable) {
        emit shareTargetsChanged();
    }
}

void FileOperations::refreshShareTargets()
{
    updateShareAvailability();

    if (!m_kdeConnectAvailable) {
        if (!m_kdeConnectDevices.isEmpty()) {
            m_kdeConnectDevices.clear();
            emit shareTargetsChanged();
        }
        return;
    }

    if (m_kdeConnectDiscovery && m_kdeConnectDiscovery->state() != QProcess::NotRunning)
        return;

    auto *process = new QProcess(this);
    m_kdeConnectDiscovery = process;
    process->setProgram(m_kdeConnectExecutable);
    process->setArguments({QStringLiteral("--list-available"), QStringLiteral("--id-name-only")});

    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        QVariantList devices;
        if (status == QProcess::NormalExit && exitCode == 0) {
            const QString output = QString::fromUtf8(process->readAllStandardOutput());
            const QStringList lines = output.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            for (const QString &rawLine : lines) {
                const QString line = rawLine.trimmed();
                const int separator = line.indexOf(QLatin1Char(' '));
                if (separator <= 0)
                    continue;
                const QString id = line.left(separator).trimmed();
                const QString name = line.mid(separator + 1).trimmed();
                if (id.isEmpty() || name.isEmpty())
                    continue;
                QVariantMap device;
                device.insert(QStringLiteral("id"), id);
                device.insert(QStringLiteral("name"), name);
                devices.append(device);
            }
        }

        if (devices != m_kdeConnectDevices) {
            m_kdeConnectDevices = devices;
            emit shareTargetsChanged();
        }
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        if (!m_kdeConnectDevices.isEmpty()) {
            m_kdeConnectDevices.clear();
            emit shareTargetsChanged();
        }
        process->deleteLater();
    });
    process->start();
}

bool FileOperations::shareViaKdeConnect(const QStringList &sourceUrls, const QString &deviceId)
{
    updateShareAvailability();
    if (!m_kdeConnectAvailable || deviceId.trimmed().isEmpty()) {
        const QString error = QStringLiteral("KDE Connect is not available or the device is invalid.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    QString error;
    const QStringList paths = localSharePaths(sourceUrls, false, &error);
    if (paths.isEmpty()) {
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    QStringList args{QStringLiteral("--device"), deviceId.trimmed(), QStringLiteral("--share"), paths.constFirst()};
    for (int i = 1; i < paths.size(); ++i)
        args.append(paths.at(i));

    auto *process = new QProcess(this);
    process->setProgram(m_kdeConnectExecutable);
    process->setArguments(args);
    connect(process, &QProcess::finished, this,
            [this, process](int exitCode, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && exitCode == 0;
        if (ok) {
            setLastError(QString());
            emit operationFinished(true, QStringLiteral("Sent with KDE Connect."), false);
        } else {
            QString message = QString::fromUtf8(process->readAllStandardError()).trimmed();
            if (message.isEmpty())
                message = QStringLiteral("KDE Connect sharing failed.");
            setLastError(message);
            emit operationFinished(false, message, false);
        }
        process->deleteLater();
        refreshShareTargets();
    });
    connect(process, &QProcess::errorOccurred, this,
            [this, process](QProcess::ProcessError processError) {
        if (processError != QProcess::FailedToStart)
            return;
        const QString message = process->errorString();
        setLastError(message);
        emit operationFinished(false, message, false);
        process->deleteLater();
    });
    process->start();
    return true;
}

bool FileOperations::shareViaBluetooth(const QStringList &sourceUrls)
{
    updateShareAvailability();
    if (!m_bluetoothAvailable) {
        const QString error = QStringLiteral("Bluetooth file transfer is not available.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    QString error;
    const QStringList paths = localSharePaths(sourceUrls, true, &error);
    if (paths.isEmpty()) {
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    QStringList args;
    const QString executableName = QFileInfo(m_bluetoothExecutable).fileName();
    if (executableName == QStringLiteral("bluedevil-sendfile")) {
        for (const QString &path : paths)
            args << QStringLiteral("--files") << path;
    } else {
        args = paths;
    }

    const bool started = QProcess::startDetached(m_bluetoothExecutable, args);
    if (!started) {
        const QString message = QStringLiteral("Could not open Bluetooth file transfer.");
        setLastError(message);
        emit operationFinished(false, message, false);
        return false;
    }

    setLastError(QString());
    emit operationFinished(true, QStringLiteral("Bluetooth sharing opened."), false);
    return true;
}

bool FileOperations::shareViaEmail(const QStringList &sourceUrls)
{
    updateShareAvailability();
    if (!m_emailShareAvailable) {
        const QString error = QStringLiteral("Email sharing is not available.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    QString error;
    const QStringList paths = localSharePaths(sourceUrls, true, &error);
    if (paths.isEmpty()) {
        setLastError(error);
        emit operationFinished(false, error, false);
        return false;
    }

    QStringList args;
    for (const QString &path : paths)
        args << QStringLiteral("--attach") << path;

    const bool started = QProcess::startDetached(m_emailExecutable, args);
    if (!started) {
        const QString message = QStringLiteral("Could not open the email application.");
        setLastError(message);
        emit operationFinished(false, message, false);
        return false;
    }

    setLastError(QString());
    emit operationFinished(true, QStringLiteral("Email sharing opened."), false);
    return true;
}
