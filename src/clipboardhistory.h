#pragma once

#include <QByteArray>
#include <QFuture>
#include <QHash>
#include <QList>
#include <QObject>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

// Session-only history of file Copy/Cut payloads on the system clipboard,
// from Lurviko and from other applications (Dolphin, ...). Each Copy/Cut is
// one entry however many items it holds. Only local files are recorded:
// cloud (Google Drive, OneDrive), SMB and other remote or web URLs are not.
//
// Nothing runs while idle. Parsing a payload and checking whether recorded
// files still exist happen on a worker thread, the latter only when the
// history is shown (refresh()). Entries lose deleted items; an entry whose
// items are all gone is removed. By default the history lives in memory only
// and is gone when Lurviko exits; with clearOnExit off it is saved to
// ~/.local/share/Lurviko/clipboard-history.json (owner-only) and restored.
class ClipboardHistory : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(int count READ count NOTIFY entriesChanged)
    Q_PROPERTY(int maxEntries READ maxEntries WRITE setMaxEntries NOTIFY maxEntriesChanged)
    Q_PROPERTY(bool validating READ validating NOTIFY validatingChanged)
    Q_PROPERTY(bool clearOnExit READ clearOnExit WRITE setClearOnExit NOTIFY clearOnExitChanged)
public:
    static constexpr int defaultMaxEntries = 20;
    static constexpr int minimumMaxEntries = 5;
    static constexpr int maximumMaxEntries = 100;
    // Bounds memory for "select all" payloads: about 150-250 bytes per path.
    static constexpr int maximumStoredUrls = 100000;

    static ClipboardHistory *instance();
    explicit ClipboardHistory(QObject *parent = nullptr);

    QVariantList entries() const;
    int count() const { return int(m_entries.size()); }
    int maxEntries() const { return m_maxEntries; }
    void setMaxEntries(int value);
    bool validating() const { return m_validating; }
    bool clearOnExit() const { return m_clearOnExit; }
    void setClearOnExit(bool clear);
    static QString storagePath();

    // Re-checks which recorded files still exist (asynchronously).
    Q_INVOKABLE void refresh();
    // Puts an entry back on the system clipboard (as Copy or Cut, as it was
    // recorded) and moves it to the top. Paste then uses it.
    Q_INVOKABLE bool restore(int id);
    Q_INVOKABLE void remove(int id);
    Q_INVOKABLE void clear();

    // Records a payload directly (used by tests; the clipboard path ends here).
    void record(const QStringList &urls, bool cut);

signals:
    void entriesChanged();
    void maxEntriesChanged();
    void validatingChanged();
    void clearOnExitChanged();
    // A save started by a change has been written (tests wait for this).
    void saved();

private:
    // Display fields, computed off the GUI thread when the URL list changes.
    struct Summary {
        QStringList names;    // first few item names
        QString location;     // common folder, "~" for the home directory
        bool mixed = false;   // items come from several folders below it
    };
    struct Entry {
        int id = 0;
        QStringList urls;     // encoded file:// URLs, in clipboard order
        bool cut = false;
        qint64 timestamp = 0; // ms since epoch, last copied or restored
        QByteArray key;       // identity of (urls, cut)
        Summary summary;
    };
    struct ParsedPayload {
        QStringList urls;
        bool cut = false;
        QByteArray key;
        Summary summary;
    };

    void onClipboardChanged();
    static ParsedPayload parsePayload(const QByteArray &uriList, bool cut);
    static ParsedPayload makePayload(QStringList urls, bool cut);
    static Summary summarize(const QStringList &urls);
    static QByteArray keyFor(const QStringList &urls, bool cut);
    void applyParsed(quint64 sequence, const ParsedPayload &payload);
    void insertPayload(const ParsedPayload &payload);
    void enforceLimits();
    void setValidating(bool validating);
    void loadSaved();
    void startSave();
    void flushOnExit();

    QList<Entry> m_entries; // newest first
    int m_nextId = 1;
    int m_maxEntries = defaultMaxEntries;
    QByteArray m_currentKey;
    quint64 m_nextSequence = 0;
    quint64 m_nextApply = 0;
    // Clipboard changes before this sequence were cleared from the history.
    quint64 m_clearedBefore = 0;
    QHash<quint64, ParsedPayload> m_pendingParsed;
    quint64 m_validationGeneration = 0;
    bool m_validating = false;
    bool m_clearOnExit = true;
    QTimer m_saveTimer;
    QFuture<void> m_saveFuture;
    bool m_saveDirty = false;
};
