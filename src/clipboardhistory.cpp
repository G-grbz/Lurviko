#include "clipboardhistory.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QGuiApplication>
#include <QMimeData>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent>

namespace {
constexpr auto kUriList = "text/uri-list";
constexpr auto kCutSelection = "application/x-kde-cutselection";
constexpr auto kMaxEntriesKey = "ClipboardHistory/maxEntries";
constexpr auto kClearOnExitKey = "ClipboardHistory/clearOnExit";
constexpr int kSaveDelayMs = 1000;

struct SavedEntry {
    QStringList urls;
    bool cut = false;
    qint64 timestamp = 0;
};

// Written owner-only: the file lists which files the user copied and from where.
void writeHistoryFile(const QString &path, const QList<SavedEntry> &entries)
{
    QJsonArray array;
    for (const SavedEntry &entry : entries) {
        QJsonObject item;
        item.insert(QStringLiteral("urls"), QJsonArray::fromStringList(entry.urls));
        item.insert(QStringLiteral("cut"), entry.cut);
        item.insert(QStringLiteral("timestamp"), double(entry.timestamp));
        array.push_back(item);
    }
    QJsonObject root;
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("entries"), array);
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (file.commit())
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}
constexpr int kSummaryNames = 3;

QString parentPath(const QString &path)
{
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    return slash <= 0 ? QStringLiteral("/") : path.left(slash);
}

// Longest common folder of two absolute folder paths, on "/" boundaries.
QString commonFolder(const QString &a, const QString &b)
{
    if (a == b)
        return a;
    const QStringList left = a.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QStringList right = b.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    QStringList common;
    for (qsizetype i = 0; i < left.size() && i < right.size() && left.at(i) == right.at(i); ++i)
        common.push_back(left.at(i));
    return QLatin1Char('/') + common.join(QLatin1Char('/'));
}

QString displayPath(const QString &path)
{
    const QString home = QDir::homePath();
    if (path == home)
        return QStringLiteral("~");
    if (path.startsWith(home + QLatin1Char('/')))
        return QLatin1Char('~') + path.mid(home.size());
    return path;
}
} // namespace

ClipboardHistory *ClipboardHistory::instance()
{
    static ClipboardHistory *history = new ClipboardHistory(QCoreApplication::instance());
    return history;
}

QString ClipboardHistory::storagePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return QDir(base).filePath(QStringLiteral("Lurviko/clipboard-history.json"));
}

ClipboardHistory::ClipboardHistory(QObject *parent) : QObject(parent)
{
    QSettings settings;
    m_maxEntries = qBound(minimumMaxEntries,
                          settings.value(QLatin1String(kMaxEntriesKey), defaultMaxEntries).toInt(),
                          maximumMaxEntries);
    m_clearOnExit = settings.value(QLatin1String(kClearOnExitKey), true).toBool();
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(kSaveDelayMs);
    connect(&m_saveTimer, &QTimer::timeout, this, &ClipboardHistory::startSave);
    // Coalesce bursts of changes into one write, only when the history is kept.
    connect(this, &ClipboardHistory::entriesChanged, this, [this]() {
        if (!m_clearOnExit)
            m_saveTimer.start();
    });
    if (auto *application = QCoreApplication::instance())
        connect(application, &QCoreApplication::aboutToQuit, this, &ClipboardHistory::flushOnExit);
    if (!m_clearOnExit)
        loadSaved();
    if (auto *clipboard = QGuiApplication::clipboard()) {
        connect(clipboard, &QClipboard::dataChanged, this, &ClipboardHistory::onClipboardChanged);
        // A file list already on the clipboard when Lurviko starts.
        onClipboardChanged();
    }
}

QVariantList ClipboardHistory::entries() const
{
    QVariantList result;
    result.reserve(m_entries.size());
    for (const Entry &entry : m_entries) {
        QVariantMap item;
        item.insert(QStringLiteral("id"), entry.id);
        item.insert(QStringLiteral("cut"), entry.cut);
        item.insert(QStringLiteral("count"), int(entry.urls.size()));
        item.insert(QStringLiteral("names"), entry.summary.names);
        item.insert(QStringLiteral("location"), entry.summary.location);
        item.insert(QStringLiteral("mixed"), entry.summary.mixed);
        item.insert(QStringLiteral("timestamp"), entry.timestamp);
        item.insert(QStringLiteral("current"), !m_currentKey.isEmpty() && entry.key == m_currentKey);
        result.push_back(item);
    }
    return result;
}

void ClipboardHistory::setMaxEntries(int value)
{
    value = qBound(minimumMaxEntries, value, maximumMaxEntries);
    if (value == m_maxEntries)
        return;
    m_maxEntries = value;
    QSettings().setValue(QLatin1String(kMaxEntriesKey), value);
    emit maxEntriesChanged();
    const qsizetype before = m_entries.size();
    enforceLimits();
    if (m_entries.size() != before)
        emit entriesChanged();
}

QByteArray ClipboardHistory::keyFor(const QStringList &urls, bool cut)
{
    QCryptographicHash hash(QCryptographicHash::Sha1);
    hash.addData(cut ? QByteArrayView("1") : QByteArrayView("0"));
    for (const QString &url : urls) {
        hash.addData(url.toUtf8());
        hash.addData(QByteArrayView("\n"));
    }
    return hash.result();
}

ClipboardHistory::Summary ClipboardHistory::summarize(const QStringList &urls)
{
    Summary summary;
    QString firstParent;
    QString common;
    for (const QString &value : urls) {
        const QString path = QUrl(value).toLocalFile();
        if (path.isEmpty())
            continue;
        if (summary.names.size() < kSummaryNames)
            summary.names.push_back(QFileInfo(path).fileName());
        const QString parent = parentPath(path);
        if (firstParent.isNull()) {
            firstParent = parent;
            common = parent;
        } else if (parent != firstParent) {
            summary.mixed = true;
            common = commonFolder(common, parent);
        }
    }
    summary.location = displayPath(common);
    return summary;
}

ClipboardHistory::ParsedPayload ClipboardHistory::makePayload(QStringList urls, bool cut)
{
    ParsedPayload payload;
    payload.cut = cut;
    payload.key = keyFor(urls, cut);
    payload.summary = summarize(urls);
    payload.urls = std::move(urls);
    return payload;
}

ClipboardHistory::ParsedPayload ClipboardHistory::parsePayload(const QByteArray &uriList, bool cut)
{
    QStringList urls;
    const QList<QByteArray> lines = uriList.split('\n');
    urls.reserve(lines.size());
    for (const QByteArray &raw : lines) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty() || line.startsWith('#'))
            continue;
        const QUrl url = QUrl::fromEncoded(line);
        // Local files only. This also leaves out web links, which browsers
        // put on the clipboard in the same format, and remote locations.
        if (!url.isValid() || !url.isLocalFile() || url.toLocalFile().isEmpty())
            continue;
        urls.push_back(QString::fromLatin1(url.toEncoded(QUrl::FullyEncoded)));
    }
    if (urls.isEmpty())
        return {};
    return makePayload(std::move(urls), cut);
}

void ClipboardHistory::onClipboardChanged()
{
    const quint64 sequence = m_nextSequence++;
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    if (!mime || !mime->hasFormat(QLatin1String(kUriList))) {
        applyParsed(sequence, {});
        return;
    }
    // QMimeData is GUI-thread only; parse the bytes on a worker thread.
    const QByteArray uriList = mime->data(QLatin1String(kUriList));
    const bool cut = mime->data(QLatin1String(kCutSelection)) == QByteArrayLiteral("1");
    auto *watcher = new QFutureWatcher<ParsedPayload>(this);
    connect(watcher, &QFutureWatcher<ParsedPayload>::finished, this, [this, watcher, sequence]() {
        watcher->deleteLater();
        applyParsed(sequence, watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([uriList, cut]() { return parsePayload(uriList, cut); }));
}

void ClipboardHistory::applyParsed(quint64 sequence, const ParsedPayload &payload)
{
    // Workers can finish out of order; apply clipboard changes in the order
    // they happened so the newest Copy/Cut always ends up on top.
    m_pendingParsed.insert(sequence, payload);
    while (m_pendingParsed.contains(m_nextApply)) {
        const quint64 applied = m_nextApply++;
        const ParsedPayload next = m_pendingParsed.take(applied);
        // Clear also forgets Copy/Cut still being parsed when it was pressed.
        if (applied < m_clearedBefore)
            continue;
        if (next.urls.isEmpty()) {
            // Text or nothing on the clipboard: no entry is current any more.
            if (!m_currentKey.isEmpty()) {
                m_currentKey.clear();
                emit entriesChanged();
            }
            continue;
        }
        insertPayload(next);
    }
}

void ClipboardHistory::record(const QStringList &urls, bool cut)
{
    QStringList local;
    for (const QString &value : urls) {
        const QUrl url(value);
        if (url.isValid() && url.isLocalFile() && !url.toLocalFile().isEmpty())
            local.push_back(QString::fromLatin1(url.toEncoded(QUrl::FullyEncoded)));
    }
    if (!local.isEmpty())
        insertPayload(makePayload(std::move(local), cut));
}

void ClipboardHistory::insertPayload(const ParsedPayload &payload)
{
    Entry entry;
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).key == payload.key) {
            // The same Copy/Cut again (or restored from the history): keep
            // one entry and move it to the top.
            entry = m_entries.takeAt(index);
            break;
        }
    }
    if (entry.id == 0) {
        entry.id = m_nextId++;
        entry.urls = payload.urls;
        entry.cut = payload.cut;
        entry.key = payload.key;
        entry.summary = payload.summary;
    }
    entry.timestamp = QDateTime::currentMSecsSinceEpoch();
    m_entries.prepend(entry);
    m_currentKey = entry.key;
    enforceLimits();
    emit entriesChanged();
}

void ClipboardHistory::enforceLimits()
{
    while (m_entries.size() > m_maxEntries)
        m_entries.removeLast();
    qsizetype stored = 0;
    for (const Entry &entry : std::as_const(m_entries))
        stored += entry.urls.size();
    // Always keep the newest entry, even when it alone exceeds the budget.
    while (m_entries.size() > 1 && stored > maximumStoredUrls) {
        stored -= m_entries.last().urls.size();
        m_entries.removeLast();
    }
}

bool ClipboardHistory::restore(int id)
{
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).id != id)
            continue;
        Entry entry = m_entries.takeAt(index);
        entry.timestamp = QDateTime::currentMSecsSinceEpoch();
        m_entries.prepend(entry);
        m_currentKey = entry.key;
        emit entriesChanged();

        QByteArray uriList;
        for (const QString &url : std::as_const(entry.urls)) {
            uriList += url.toLatin1();
            uriList += "\r\n";
        }
        auto *mime = new QMimeData;
        mime->setData(QLatin1String(kUriList), uriList);
        mime->setData(QLatin1String(kCutSelection), entry.cut ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));
        // The resulting dataChanged is recorded like any Copy/Cut and only
        // confirms this entry as the newest one.
        QGuiApplication::clipboard()->setMimeData(mime);
        return true;
    }
    return false;
}

void ClipboardHistory::remove(int id)
{
    for (qsizetype index = 0; index < m_entries.size(); ++index) {
        if (m_entries.at(index).id == id) {
            m_entries.removeAt(index);
            emit entriesChanged();
            return;
        }
    }
}

void ClipboardHistory::clear()
{
    m_clearedBefore = m_nextSequence;
    if (m_entries.isEmpty())
        return;
    m_entries.clear();
    emit entriesChanged();
}

void ClipboardHistory::setValidating(bool validating)
{
    if (m_validating == validating)
        return;
    m_validating = validating;
    emit validatingChanged();
}

void ClipboardHistory::refresh()
{
    if (m_entries.isEmpty())
        return;
    struct Snapshot { int id; QStringList urls; };
    struct Checked { int id; bool changed; ParsedPayload payload; };
    QList<Snapshot> snapshot;
    snapshot.reserve(m_entries.size());
    for (const Entry &entry : std::as_const(m_entries))
        snapshot.push_back({entry.id, entry.urls});

    const quint64 generation = ++m_validationGeneration;
    setValidating(true);
    auto *watcher = new QFutureWatcher<QList<Checked>>(this);
    connect(watcher, &QFutureWatcher<QList<Checked>>::finished, this, [this, watcher, generation]() {
        watcher->deleteLater();
        if (generation != m_validationGeneration)
            return;
        setValidating(false);
        bool changed = false;
        for (const Checked &checked : watcher->result()) {
            if (!checked.changed)
                continue;
            for (qsizetype index = 0; index < m_entries.size(); ++index) {
                Entry &entry = m_entries[index];
                if (entry.id != checked.id)
                    continue;
                changed = true;
                if (checked.payload.urls.isEmpty()) {
                    // Every item (or the folder holding them) was deleted.
                    m_entries.removeAt(index);
                    break;
                }
                const QByteArray key = keyFor(checked.payload.urls, entry.cut);
                if (m_currentKey == entry.key)
                    m_currentKey = key;
                entry.urls = checked.payload.urls;
                entry.summary = checked.payload.summary;
                entry.key = key;
                break;
            }
        }
        if (changed)
            emit entriesChanged();
    });
    watcher->setFuture(QtConcurrent::run([snapshot]() {
        QList<Checked> results;
        results.reserve(snapshot.size());
        for (const Snapshot &entry : snapshot) {
            QStringList existing;
            existing.reserve(entry.urls.size());
            for (const QString &url : entry.urls) {
                const QFileInfo info(QUrl(url).toLocalFile());
                if (info.exists() || info.isSymLink())
                    existing.push_back(url);
            }
            Checked checked{entry.id, existing.size() != entry.urls.size(), {}};
            if (checked.changed) {
                checked.payload.summary = summarize(existing);
                checked.payload.urls = std::move(existing);
            }
            results.push_back(std::move(checked));
        }
        return results;
    }));
}

void ClipboardHistory::setClearOnExit(bool clear)
{
    if (clear == m_clearOnExit)
        return;
    m_clearOnExit = clear;
    QSettings().setValue(QLatin1String(kClearOnExitKey), clear);
    emit clearOnExitChanged();
    if (clear) {
        // Nothing is kept on disk any more; this session's list stays.
        m_saveTimer.stop();
        m_saveDirty = false;
        m_saveFuture.waitForFinished();
        QFile::remove(storagePath());
    } else {
        startSave();
    }
}

void ClipboardHistory::startSave()
{
    if (m_clearOnExit)
        return;
    if (m_saveFuture.isRunning()) {
        // One write at a time; write the newest state after this one.
        m_saveDirty = true;
        return;
    }
    QList<SavedEntry> snapshot;
    snapshot.reserve(m_entries.size());
    for (const Entry &entry : std::as_const(m_entries))
        snapshot.push_back({entry.urls, entry.cut, entry.timestamp});
    const QString path = storagePath();
    m_saveFuture = QtConcurrent::run([path, snapshot]() { writeHistoryFile(path, snapshot); });
    auto *watcher = new QFutureWatcher<void>(this);
    connect(watcher, &QFutureWatcher<void>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        if (m_saveDirty) {
            m_saveDirty = false;
            startSave();
            return;
        }
        emit saved();
    });
    watcher->setFuture(m_saveFuture);
}

void ClipboardHistory::flushOnExit()
{
    m_saveTimer.stop();
    m_saveFuture.waitForFinished();
    if (m_clearOnExit) {
        QFile::remove(storagePath());
        return;
    }
    QList<SavedEntry> snapshot;
    snapshot.reserve(m_entries.size());
    for (const Entry &entry : std::as_const(m_entries))
        snapshot.push_back({entry.urls, entry.cut, entry.timestamp});
    writeHistoryFile(storagePath(), snapshot);
}

void ClipboardHistory::loadSaved()
{
    struct Loaded { ParsedPayload payload; qint64 timestamp; };
    const QString path = storagePath();
    auto *watcher = new QFutureWatcher<QList<Loaded>>(this);
    connect(watcher, &QFutureWatcher<QList<Loaded>>::finished, this, [this, watcher]() {
        watcher->deleteLater();
        bool changed = false;
        // Saved entries are older than anything copied since startup.
        for (const Loaded &loaded : watcher->result()) {
            bool known = false;
            for (const Entry &entry : std::as_const(m_entries))
                known = known || entry.key == loaded.payload.key;
            if (known)
                continue;
            Entry entry;
            entry.id = m_nextId++;
            entry.urls = loaded.payload.urls;
            entry.cut = loaded.payload.cut;
            entry.key = loaded.payload.key;
            entry.summary = loaded.payload.summary;
            entry.timestamp = loaded.timestamp;
            m_entries.push_back(entry);
            changed = true;
        }
        if (changed) {
            enforceLimits();
            emit entriesChanged();
        }
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        QList<Loaded> result;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return result;
        const QJsonArray array = QJsonDocument::fromJson(file.readAll()).object()
                                     .value(QStringLiteral("entries")).toArray();
        for (const QJsonValue &value : array) {
            const QJsonObject item = value.toObject();
            QStringList urls;
            for (const QJsonValue &url : item.value(QStringLiteral("urls")).toArray()) {
                const QUrl parsed(url.toString());
                if (parsed.isValid() && parsed.isLocalFile() && !parsed.toLocalFile().isEmpty())
                    urls.push_back(url.toString());
            }
            if (urls.isEmpty())
                continue;
            result.push_back({makePayload(std::move(urls), item.value(QStringLiteral("cut")).toBool()),
                              qint64(item.value(QStringLiteral("timestamp")).toDouble())});
        }
        return result;
    }));
}
