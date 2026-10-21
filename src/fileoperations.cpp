#include "fileoperations.h"
#include "archivearguments.h"
#include "archivelisting.h"
#include "archiveinspectionjob.h"
#include "archivedropobserver.h"
#include "archiveoutput.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
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
#include <QLocale>
#include <QSettings>
#include <QHash>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTimer>
#include <QUrl>
#include <QtConcurrent>
#include <memory>
#include <atomic>
#include <utility>
#include <signal.h>
#include <cerrno>
#include <cstring>
#include <sys/stat.h>
#include <fcntl.h>
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
QString securityMessage(const char *english, const char *turkish)
{
    const QString fallback = QLocale::system().language() == QLocale::Turkish ? QStringLiteral("tr") : QStringLiteral("en");
    return QString::fromUtf8(QSettings().value(QStringLiteral("ui/language"), fallback).toString() == QStringLiteral("tr")
                                ? turkish : english);
}
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

    void trackCompression(const QStringList &sources, const QString &destination, const QString &displayDestination)
    {
        m_destination = destination;
        m_displayDestination = displayDestination;
        setProgressUnit(KJob::Items);
        m_cancelScan = std::make_shared<std::atomic_bool>(false);
        const auto cancelled = m_cancelScan;
        auto *watcher = new QFutureWatcher<qulonglong>(this);
        connect(watcher, &QFutureWatcher<qulonglong>::finished, this, [this, watcher] {
            m_totalEntries = watcher->result(); watcher->deleteLater();
            if (m_done) return;
            setTotalAmount(KJob::Items, m_totalEntries);
            publishProgress();
        });
        watcher->setFuture(QtConcurrent::run([sources, destination, cancelled] {
            qulonglong entries = 0;
            for (const auto &source : sources) {
                if (cancelled->load()) break;
                ++entries;
                const QFileInfo info(source);
                if (!info.isDir() || info.isSymLink()) continue;
                QDirIterator it(source, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                                QDirIterator::Subdirectories);
                while (!cancelled->load() && it.hasNext()) {
                    if (it.next() != destination) ++entries;
                }
            }
            return entries;
        }));
        connect(m_process, &QProcess::readyReadStandardError, this, [this] { consumeDiagnostics(); });
        m_timer = new QTimer(this);
        m_timer->setInterval(500);
        connect(m_timer, &QTimer::timeout, this, [this] {
            if (!m_process || m_process->state() != QProcess::Running || isSuspended()) return;
            // SIGUSR1 is bsdtar's documented progress signal. Check the actual
            // handler first: never send it during exec/loader initialization.
            QFile status(QStringLiteral("/proc/%1/status").arg(m_process->processId()));
            if (status.open(QIODevice::ReadOnly)) {
                const auto lines = status.readAll().split('\n');
                for (const auto &line : lines) {
                    if (!line.startsWith("SigCgt:")) continue;
                    bool ok = false;
                    const auto caught = line.mid(7).trimmed().toULongLong(&ok, 16);
                    if (ok && (caught & (qulonglong(1) << (SIGUSR1 - 1))))
                        ::kill(static_cast<pid_t>(m_process->processId()), SIGUSR1);
                    break;
                }
            }
            sampleOutput();
            publishProgress();
        });
    }

    ~ArchiveProgressJob() override { if (m_cancelScan) m_cancelScan->store(true); }

    void start() override
    {
        startElapsedTimer();
        m_clock.start();
        QTimer::singleShot(0, this, [this] {
            if (!m_process || m_done) return;
            publishDescription();
            m_process->start(m_executable, m_arguments);
            if (m_timer) m_timer->start();
        });
    }

    QString compressionDiagnostics()
    {
        consumeDiagnostics();
        if (!m_buffer.isEmpty()) { parseLine(m_buffer); m_buffer.clear(); }
        return m_errors.trimmed();
    }

    void setPublishedArchive(const QString &path) { m_destination = path; m_displayDestination = path; }

    void complete(bool success, const QString &error = QString())
    {
        m_done = true;
        if (m_timer) m_timer->stop();
        if (m_cancelScan) m_cancelScan->store(true);
        if (!success) {
            setError(KJob::UserDefinedError); setErrorText(error);
        } else {
            if (!m_destination.isEmpty()) {
                sampleOutput();
                // Successful verbose records are the actual archive entries,
                // including when sources changed while the scan was running.
                m_totalEntries = m_seenEntries;
                setTotalAmount(KJob::Items, m_seenEntries);
                setProcessedAmount(KJob::Items, m_seenEntries);
                publishDescription();
            }
            setPercent(100);
        }
        emitSpeed(0);
        emitResult();
    }

    QString errorString() const override { return errorText(); }

protected:
    bool doKill() override
    {
        m_done = true; // Also prevent a queued start after immediate cancellation.
        if (m_cancelScan) m_cancelScan->store(true);
        if (m_timer) m_timer->stop();
        if (!m_process) return false;
        if (isSuspended() && m_process->processId() > 0)
            ::kill(static_cast<pid_t>(m_process->processId()), SIGCONT);
        m_process->kill(); return true;
    }
    bool doSuspend() override
    {
        const bool stopped = m_process && m_process->processId() > 0
            && ::kill(static_cast<pid_t>(m_process->processId()), SIGSTOP) == 0;
        if (stopped) emitSpeed(0);
        return stopped;
    }
    bool doResume() override
    {
        m_lastSampleTime = m_clock.elapsed();
        return m_process && m_process->processId() > 0
            && ::kill(static_cast<pid_t>(m_process->processId()), SIGCONT) == 0;
    }

private:
    void consumeDiagnostics()
    {
        if (!m_process || m_destination.isEmpty()) return;
        m_buffer += m_process->readAllStandardError();
        qsizetype newline;
        while ((newline = m_buffer.indexOf('\n')) >= 0) {
            parseLine(m_buffer.left(newline)); m_buffer.remove(0, newline + 1);
        }
        // Bound pathological filenames/diagnostics instead of growing forever.
        if (m_buffer.size() > 65536) { m_errors += QString::fromLocal8Bit(m_buffer).right(4096); m_buffer.clear(); }
        m_errors = m_errors.right(8192);
    }
    void parseLine(const QByteArray &raw)
    {
        const QString line = QString::fromLocal8Bit(raw).trimmed();
        static const QRegularExpression report(QStringLiteral("^In: ([0-9]+) files, [0-9]+ bytes; Out: ([0-9]+) bytes, compression [0-9]+%$"));
        static const QRegularExpression current(QStringLiteral("^Current: (.*) \\(([0-9]+)/([0-9]+) bytes\\)$"));
        if (line.startsWith(QStringLiteral("a "))) {
            ++m_seenEntries; m_currentFile = line.mid(2); m_currentFraction = 0;
        } else if (const auto match = report.match(line); match.hasMatch()) {
            m_reportEntries = qMax(m_reportEntries, match.captured(1).toULongLong());
        } else if (const auto match = current.match(line); match.hasMatch()) {
            m_currentFile = match.captured(1);
            const auto size = match.captured(3).toULongLong();
            m_currentFraction = size ? qBound(0.0, double(match.captured(2).toULongLong()) / double(size), 1.0) : 0;
        } else if (!line.isEmpty()) {
            m_errors += line + QLatin1Char('\n');
        }
    }
    void sampleOutput()
    {
        const auto written = qulonglong(qMax<qint64>(0, QFileInfo(m_destination).size()));
        const qint64 now = m_clock.elapsed(), elapsed = now - m_lastSampleTime;
        setProcessedAmount(KJob::Bytes, written); // Compressed output, not input bytes.
        if (elapsed > 0) emitSpeed(static_cast<unsigned long>((written >= m_lastWritten ? written - m_lastWritten : 0) * 1000 / elapsed));
        m_lastWritten = written; m_lastSampleTime = now;
    }
    void publishProgress()
    {
        if (m_done) return;
        const auto announced = qMax(m_seenEntries, m_reportEntries);
        const auto completed = announced > 0 ? announced - 1 : 0;
        setProcessedAmount(KJob::Items, m_totalEntries ? qMin(completed, m_totalEntries - 1) : completed);
        if (m_totalEntries)
            setPercent(qMax(percent(), static_cast<unsigned long>(qBound(0.0, (double(completed) + m_currentFraction) * 100 / double(m_totalEntries), 99.0))));
        publishDescription();
    }
    void publishDescription()
    {
        if (m_destination.isEmpty()) {
            emit description(this, m_title, {securityMessage("File", "Dosya"), m_detail}); return;
        }
        QString destination = m_displayDestination;
        if (m_totalEntries)
            destination += QStringLiteral(" · ") + securityMessage("%1 / %2 entries", "%1 / %2 öğe")
                .arg(processedAmount(KJob::Items)).arg(m_totalEntries);
        emit description(this, m_title,
                         {securityMessage("Source", "Kaynak"), m_currentFile.isEmpty() ? m_detail : m_currentFile},
                         {securityMessage("Archive", "Arşiv"), destination});
    }
    QPointer<QProcess> m_process;
    QString m_executable;
    QStringList m_arguments;
    QString m_title, m_detail, m_destination, m_displayDestination, m_currentFile, m_errors;
    QByteArray m_buffer;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_clock;
    qint64 m_lastSampleTime = 0;
    qulonglong m_lastWritten = 0, m_totalEntries = 0, m_seenEntries = 0, m_reportEntries = 0;
    double m_currentFraction = 0;
    bool m_done = false;
    std::shared_ptr<std::atomic_bool> m_cancelScan;
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
            // A failed/cancelled batch can already have changed some entries.
            // Refresh remote views too; local watchers are not sufficient.
            emit operationFinished(false, job->errorString(), refreshNeeded, resultAlreadyPresented);
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
        // Clipboard preparation and trash feedback stay visible in-app even
        // when Plasma handles other transfer progress.
        if (m_kioProgressEnabled && operation.kind != QStringLiteral("clipboard")
            && operation.kind != QStringLiteral("trash"))
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
        item.insert(QStringLiteral("canPause"), operation.job
            ? operation.job->capabilities().testFlag(KJob::Suspendable) : bool(operation.process));
        item.insert(QStringLiteral("canCancel"), operation.job
            ? operation.job->capabilities().testFlag(KJob::Killable) : bool(operation.process));
        item.insert(QStringLiteral("finished"), operation.finished);
        item.insert(QStringLiteral("success"), operation.success);
        item.insert(QStringLiteral("cancelled"), operation.cancelled);
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
            if (enabled && !operation.job->property("lurvikoIntegratedProgress").toBool())
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
    if (m_kioProgressEnabled && !job->property("lurvikoIntegratedProgress").toBool()) {
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
        // Only the result signal can confirm completion, even if a worker
        // reports 100% before a final rename/metadata write fails.
        operation.percent = qBound(-1, percent, 99);
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

void FileOperations::finishOperation(int id, bool success, bool cancelled)
{
    const bool wasBusy = ioBusy();
    for (Operation &operation : m_operations) {
        if (operation.id != id || operation.finished)
            continue;

        operation.finished = true;
        operation.success = success;
        operation.cancelled = cancelled;
        operation.paused = false;
        operation.speed = 0;
        if (success) {
            operation.percent = 100;
            // Preserve measured counters. Success may include Skip/Skip All;
            // requested totals are not proof that those bytes/items were copied.
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
        m_cancelledOperations.remove(id);
        return false;
    }
    return false;
}

int FileOperations::trackOperationJob(KJob *job, const QString &kind, const QString &detail, qulonglong items)
{
    const int operationId = startOperation(kind, detail, job);
    setOperationAmount(operationId, int(KJob::Items), items, true);
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
    connect(job, &KJob::suspended, this, [this, operationId] { setOperationPaused(operationId, true); });
    connect(job, &KJob::resumed, this, [this, operationId] { setOperationPaused(operationId, false); });
    connect(job, &KJob::result, this, [this, job, operationId] {
        if (job->error())
            setOperationDetail(operationId, job->errorString());
        const bool cancelled = job->error() == KJob::KilledJobError || m_cancelledOperations.remove(operationId);
        m_cancelledOperations.remove(operationId);
        finishOperation(operationId, !job->error(), cancelled);
    });
    connect(job, &QObject::destroyed, this, [this, operationId] {
        for (const auto &operation : std::as_const(m_operations)) {
            if (operation.id != operationId || operation.finished) continue;
            const QString error = securityMessage("The operation ended without a completion result. Some items may have changed.",
                                                  "İşlem sonuç bildirmeden sonlandı. Bazı öğeler değişmiş olabilir.");
            setOperationDetail(operationId, error);
            finishOperation(operationId, false);
            m_cancelledOperations.remove(operationId);
            setLastError(error);
            emit operationFinished(false, error, true, true);
            break;
        }
    });
    return operationId;
}

void FileOperations::trackTransferJob(KIO::CopyJob *job, const QList<QUrl> &sources,
                                      const QUrl &destination, const QString &kind)
{
    const QString detail = sources.size() == 1 ? sources.constFirst().fileName()
                          : securityMessage("%1 items", "%1 öğe").arg(sources.size());
    const int operationId = trackOperationJob(job, kind, detail, qulonglong(sources.size()));
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
    auto completed = std::make_shared<QHash<QString, QUrl>>();
    auto sourceKeys = std::make_shared<QSet<QString>>();
    for (const QUrl &source : sources)
        sourceKeys->insert(source.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded));

    connect(job, &KIO::CopyJob::copyingDone, this,
            [this, operationId, kind, completed, sourceKeys](KIO::Job *, const QUrl &from, const QUrl &to,
                                    const QDateTime &, bool, bool) {
        const QString key = from.adjusted(QUrl::StripTrailingSlash).toString(QUrl::FullyEncoded);
        if (sourceKeys->contains(key)) {
            completed->insert(key, to);
            setOperationAmount(operationId, int(KJob::Items), completed->size(), false);
        }
    });
    // Trashing is a rename into KDE's trash, not a copy into the current view.
    // Keep its real completion count without selecting trash URLs in a tab.
    if (kind == QStringLiteral("trash"))
        return;
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
    trackOperationJob(job, QStringLiteral("rename"), securityMessage("%1 items", "%1 öğe").arg(urls.size()), urls.size());
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
    trashMany({itemUrl});
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
    // Show preparation immediately, including the first cold KIO folder-size
    // scan. Preserve KDE's trash limits, metadata and Undo support.
    job->setProperty("lurvikoIntegratedProgress", true);
    KIO::FileUndoManager::self()->recordJob(KIO::FileUndoManager::Trash, urls,
                                            QUrl(QStringLiteral("trash:/")), job);
    trackTransferJob(job, urls, QUrl(QStringLiteral("trash:/")), QStringLiteral("trash"));
    finishJob(job, QStringLiteral("Moved to trash"));
}

void FileOperations::emptyTrash()
{
    auto *job = KIO::emptyTrash();
    trackOperationJob(job, QStringLiteral("delete"), securityMessage("Emptying Trash", "Çöp boşaltılıyor"));
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
    trackOperationJob(job, QStringLiteral("restore"), securityMessage("Restoring %1 items", "%1 öğe geri yükleniyor").arg(urls.size()), urls.size());
    finishJob(job, QStringLiteral("Restored from trash"));
}

void FileOperations::removeItem(const QString &itemUrl)
{
    auto *job = KIO::del(toUrl(itemUrl));
    trackOperationJob(job, QStringLiteral("delete"), toUrl(itemUrl).fileName(), 1);
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
    trackOperationJob(job, QStringLiteral("delete"), securityMessage("%1 items", "%1 öğe").arg(urls.size()), urls.size());
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

    const QString plannedArchivePath = parent.filePath(baseName + extension);
    auto output = std::make_shared<ArchiveOutput>(parent.absolutePath(), baseName, extension);
    if (!output->valid()) {
        const QString error = securityMessage("Could not create an archive staging file. Check free space and directory permissions.",
                                              "Geçici arşiv dosyası oluşturulamadı. Boş alanı ve dizin izinlerini kontrol et.");
        setLastError(error); emit operationFinished(false, error, false); return;
    }

    QStringList args;
    // bsdtar's auto-compress mode selects both the archive format (Zip/7z/Tar)
    // and the compression filter (gzip/bzip2/xz/zstd) from the filename suffix.
    // This keeps all formats on one code path and preserves the per-source -C
    // handling below for category views containing mixed parent directories.
    args << QStringLiteral("-a")
         << QStringLiteral("-cf")
         << output->path();
    args << ArchiveArguments::creationPathOptions();

    QStringList sourcePaths;
    args << QStringLiteral("-v");
    for (const QFileInfo &info : std::as_const(sources)) {
        // -C before each operand avoids embedding absolute paths even when a
        // category selection contains files from different directories.
        args << ArchiveArguments::sourceOperand(info);
        sourcePaths.append(info.absoluteFilePath());
    }

    const QString detail = sources.size() == 1
        ? sources.constFirst().fileName()
        : securityMessage("%1 items", "%1 öğe").arg(sources.size());

    auto *process = new QProcess(this);
    process->setWorkingDirectory(parent.absolutePath());
    auto *job = new ArchiveProgressJob(process, executable, args, securityMessage("Creating archive", "Arşiv oluşturuluyor"),
                                       detail, this);
    job->trackCompression(sourcePaths, output->path(), plannedArchivePath);
    QPointer<ArchiveProgressJob> trackedJob(job);
    const int operationId = startOperation(QStringLiteral("compress"), detail, job, process);
    setOperationAmount(operationId, int(KJob::Items), qulonglong(sources.size()), true);

    connect(job, &KJob::result, this, [this, process, output, operationId](KJob *result) {
        if (result->error() == KJob::KilledJobError) {
            m_cancelledOperations.insert(operationId);
            if (process->state() == QProcess::NotRunning) {
                m_cancelledOperations.remove(operationId);
                process->deleteLater();
                finishOperation(operationId, false, true);
                emit operationFinished(false, securityMessage("Cancelled", "İptal edildi"), false, true);
            }
        }
    });
    connect(job, &KJob::suspended, this, [this, operationId] { setOperationPaused(operationId, true); });
    connect(job, &KJob::resumed, this, [this, operationId] { setOperationPaused(operationId, false); });

    connect(job, &KJob::percentChanged, this, [this, operationId](KJob *, unsigned long value) {
        setOperationProgress(operationId, int(value));
    });
    connect(job, &KJob::totalAmountChanged, this, [this, operationId](KJob *, KJob::Unit unit, qulonglong value) {
        setOperationAmount(operationId, int(unit), value, true);
    });
    connect(job, &KJob::processedAmountChanged, this, [this, operationId](KJob *, KJob::Unit unit, qulonglong value) {
        setOperationAmount(operationId, int(unit), value, false);
    });
    connect(job, &KJob::speed, this, [this, operationId](KJob *, unsigned long value) { setOperationSpeed(operationId, value); });
    connect(job, &KJob::description, this, [this, operationId](KJob *, const QString &, const QPair<QString, QString> &source, const QPair<QString, QString> &) {
        setOperationDetail(operationId, source.second);
    });

    connect(process, &QProcess::finished, this,
            [this, process, output, operationId, trackedJob](int exitCode, QProcess::ExitStatus status) {
        const bool cancelled = m_cancelledOperations.remove(operationId);
        bool ok = !cancelled && status == QProcess::NormalExit && exitCode == 0;
        QString publishError, archivePath;
        if (ok) { archivePath = output->publish(&publishError); ok = !archivePath.isEmpty(); }
        if (ok && trackedJob) trackedJob->setPublishedArchive(archivePath);
        const QString processError = !publishError.isEmpty() ? publishError
            : trackedJob ? trackedJob->compressionDiagnostics()
                         : QString::fromLocal8Bit(process->readAllStandardError()).trimmed();
        const QString error = cancelled ? securityMessage("Cancelled", "İptal edildi")
            : processError.isEmpty() ? securityMessage("Compression failed.", "Sıkıştırma başarısız.") : processError;
        if (!ok) setOperationDetail(operationId, error);
        if (trackedJob && !cancelled)
            trackedJob->complete(ok, processError.isEmpty() ? securityMessage("Compression failed.", "Sıkıştırma başarısız.") : processError);
        finishOperation(operationId, ok, cancelled);
        if (ok) {
            setLastError(QString());
            emit transferItemsFinished(QUrl::fromLocalFile(QFileInfo(archivePath).absolutePath()).toString(),
                                       {QUrl::fromLocalFile(archivePath).toString()});
            emit operationFinished(true, QStringLiteral("Archive created: %1").arg(QFileInfo(archivePath).fileName()), true,
                                   true);
        } else {
            setLastError(error);
            emit operationFinished(false, error, false, true);
        }
        process->deleteLater();
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, operationId, trackedJob](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        setOperationDetail(operationId, process->errorString());
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

    // The user explicitly selected this root; resolve its alias once. Derived
    // archive subfolders are opened with O_NOFOLLOW below, never through links.
    QString outputPath = QFileInfo(destination.toLocalFile()).canonicalFilePath();
    if (outputPath.isEmpty()) {
        const QString error = securityMessage("Choose an existing extraction destination directory.",
                                              "Çıkarma hedefi olarak mevcut bir dizin seç.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }
    if (createSubfolder) {
        outputPath = QDir(outputPath).filePath(ArchiveArguments::extractionFolderName(archiveInfo.fileName()));
    }

    const QString executable = QStandardPaths::findExecutable(QStringLiteral("7z"));
    if (executable.isEmpty()) {
        const QString error = QStringLiteral("7-Zip was not found.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    const QString extractor = QStandardPaths::findExecutable(QStringLiteral("bsdtar"));
    if (extractor.isEmpty()) {
        const QString error = securityMessage("Safe archive extraction requires bsdtar (libarchive).",
                                              "Güvenli arşiv çıkarma için bsdtar (libarchive) gerekiyor.");
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
        runArchiveExtraction(archiveInfo.absoluteFilePath(), outputPath, extractor);
        return;
    }

    auto *probe = new ArchiveInspectionJob(executable, archiveInfo.absoluteFilePath(), outputPath,
                                            securityMessage("Inspecting archive", "Arşiv inceleniyor"), this);
    const int operationId = trackOperationJob(probe, QStringLiteral("inspect"), archiveInfo.fileName());
    connect(probe, &KJob::result, this,
            [this, probe, operationId, sourceUrl, destinationLocation, createSubfolder,
             archivePath = archiveInfo.absoluteFilePath(), outputPath, extractor] {
        const auto &listing = probe->inspection();
        if (!listing.valid) {
            const QString error = securityMessage("Archive metadata contains an excessively long entry. Extraction was not started.",
                                                  "Arşiv bilgileri aşırı uzun bir girdi içeriyor. Çıkarma başlatılmadı.");
            setOperationDetail(operationId, error); setLastError(error);
            emit operationFinished(false, error, false, true);
            return;
        }
        if (probe->error()) {
            const QString error = probe->error() == KJob::KilledJobError
                ? securityMessage("Cancelled", "İptal edildi") : probe->errorString();
            setOperationDetail(operationId, error); setLastError(error);
            emit operationFinished(false, error, false, true);
            return;
        }
        // Continue into extraction (or its existing overwrite confirmation)
        // without leaving a second completed preparation card on screen.
        dismissOperation(operationId);
        if (!listing.conflicts.isEmpty()) {
            emit archiveOverwriteConfirmationRequired(sourceUrl, destinationLocation,
                                                       createSubfolder, listing.conflicts);
            return;
        }
        if (!QDir().mkpath(outputPath)) {
            const QString error = securityMessage("Could not create extraction folder: %1", "Çıkarma klasörü oluşturulamadı: %1").arg(outputPath);
            setLastError(error); emit operationFinished(false, error, false);
            return;
        }
        runArchiveExtraction(archivePath, outputPath, extractor);
    });
    configureProgressJob(probe, true);
    probe->start();
}

void FileOperations::runArchiveExtraction(const QString &archivePath,
                                          const QString &outputPath,
                                          const QString &executable)
{
    const int directory = ::open(QFile::encodeName(outputPath).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0) {
        const QString error = securityMessage("Could not safely open the extraction directory. A derived subfolder must not be a symbolic link.",
                                              "Çıkarma dizini güvenli şekilde açılamadı. Arşiv için oluşturulan alt dizin sembolik bağ olmamalı.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }
    auto *process = new QProcess(this);
    // Keep this parent-process descriptor alive until the child exits. bsdtar
    // chdirs through it, pinning its root against asynchronous path replacement.
    connect(process, &QObject::destroyed, [directory] { ::close(directory); });
    const QString pinnedOutput = QStringLiteral("/proc/%1/fd/%2").arg(QCoreApplication::applicationPid()).arg(directory);
    const QStringList args = ArchiveArguments::extraction(archivePath, pinnedOutput);
    auto *job = new ArchiveProgressJob(process, executable, args, securityMessage("Extracting archive", "Arşiv çıkarılıyor"),
                                       QFileInfo(archivePath).fileName(), this);
    QPointer<ArchiveProgressJob> trackedJob(job);
    const int operationId = startOperation(QStringLiteral("extract"), QFileInfo(archivePath).fileName(), job, process);
    connect(job, &KJob::result, this, [this, process, operationId](KJob *result) {
        if (result->error() == KJob::KilledJobError) {
            m_cancelledOperations.insert(operationId);
            if (process->state() == QProcess::NotRunning) {
                m_cancelledOperations.remove(operationId);
                finishOperation(operationId, false, true);
                process->deleteLater();
                emit operationFinished(false, securityMessage("Cancelled", "İptal edildi"), true, true);
            }
        }
    });
    connect(job, &KJob::suspended, this, [this, operationId] { setOperationPaused(operationId, true); });
    connect(job, &KJob::resumed, this, [this, operationId] { setOperationPaused(operationId, false); });
    auto lastOutput = std::make_shared<QString>();
    auto errors = std::make_shared<QByteArray>();
    const auto readErrors = [process, errors] {
        *errors = (std::move(*errors) + process->readAllStandardError()).right(8192);
    };
    connect(process, &QProcess::readyReadStandardError, this, readErrors);
    connect(process, &QProcess::readyReadStandardOutput, this, [process, lastOutput] {
        const QString chunk = QString::fromLocal8Bit(process->readAllStandardOutput());
        if (!chunk.isEmpty())
            *lastOutput = (*lastOutput + chunk).right(2048);
    });
    connect(process, &QProcess::finished, this,
            [this, process, outputPath, operationId, lastOutput, errors, readErrors, trackedJob](int exitCode, QProcess::ExitStatus status) {
        const bool cancelled = m_cancelledOperations.remove(operationId);
        const bool ok = !cancelled && status == QProcess::NormalExit && exitCode == 0;
        readErrors();
        const QString processError = QString::fromLocal8Bit(*errors).trimmed();
        QString error = cancelled ? securityMessage("Cancelled", "İptal edildi") : processError;
        if (error.isEmpty()) error = lastOutput->trimmed();
        if (error.isEmpty()) error = securityMessage("Archive extraction failed.", "Arşiv çıkarılamadı.");
        if (!ok) setOperationDetail(operationId, error);
        if (trackedJob && !cancelled)
            trackedJob->complete(ok, processError.isEmpty() ? QStringLiteral("Archive extraction failed.") : processError);
        finishOperation(operationId, ok, cancelled);
        if (ok) {
            setLastError(QString());
            emit operationFinished(true,
                                   QStringLiteral("Extracted to: %1").arg(QDir::toNativeSeparators(outputPath)), true,
                                   true);
        } else {
            setLastError(error);
            // Earlier entries may already be extracted. Keep them and refresh
            // the destination rather than deleting data to imitate rollback.
            emit operationFinished(false, error, true, true);
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

    // Overwrite consent does not authorize following links outside the target.
    // Keep libarchive's path guards enabled even after confirmation. Progress
    // stays indeterminate until completion; pause/resume/cancel remain available.
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

    auto *interface = new QDBusInterface(serviceName, objectPath,
                             QStringLiteral("org.kde.ark.DndExtract"),
                             QDBusConnection::sessionBus(), this);
    if (!interface->isValid()) {
        interface->deleteLater();
        const QString error = QStringLiteral("Could not connect to Ark's extraction service.");
        setLastError(error);
        emit operationFinished(false, error, false);
        return;
    }

    // Install temporary destination/child watches BEFORE calling Ark: a tiny
    // archive can finish before its D-Bus acknowledgement is delivered. Ark
    // still owns the real KIO job; these signals report observed contents,
    // never inferred progress, success or completion of that external job.
    auto *observer = new ArchiveDropObserver(destination.toLocalFile(), this);
    connect(observer, &ArchiveDropObserver::itemsChanged, this,
            [this, destination](const QStringList &urls, bool first) {
        emit archiveDropItemsChanged(destination.toString(), urls, first);
    });
    QPointer<ArchiveDropObserver> guardedObserver(observer);
    const auto request = [this, interface, destination, guardedObserver] {
        auto *watcher = new QDBusPendingCallWatcher(
            interface->asyncCall(QStringLiteral("extractSelectedFilesTo"), destination.toLocalFile()), this);
        interface->deleteLater();
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, guardedObserver](QDBusPendingCallWatcher *finishedWatcher) {
            const QDBusPendingReply<> reply = *finishedWatcher;
            if (reply.isError()) {
                if (guardedObserver) guardedObserver->deleteLater();
                const QString error = reply.error().message();
                setLastError(error);
                emit operationFinished(false, error, false, !m_kioProgressEnabled);
            } else {
                setLastError(QString());
            }
            finishedWatcher->deleteLater();
        });
    };
    connect(observer, &ArchiveDropObserver::ready, this, request);
    // A watch quota/permission failure must not prevent Ark extracting files.
    connect(observer, &ArchiveDropObserver::unavailable, this, request);
    observer->start();
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
