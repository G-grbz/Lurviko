#pragma once

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

// Dolphin-style file preview preferences shared by every file view.
//
// QML asks allows() before requesting an image://gfilethumb/ source, so a
// disabled type or an oversized file never reaches the thumbnail workers.
// kindFor() is also used by ThumbnailProvider to pick the generator; it is
// thread-safe and depends only on the file name suffix and MIME type.
class PreviewSettings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    // Upper file size, in KiB, for files on the system disk ("/" and its
    // internal system mounts) and for every other mount (USB, network, /mnt,
    // /run/media…). 0 means no limit.
    Q_PROPERTY(int systemMaxKiB READ systemMaxKiB WRITE setSystemMaxKiB NOTIFY changed)
    Q_PROPERTY(int externalMaxKiB READ externalMaxKiB WRITE setExternalMaxKiB NOTIFY changed)
    Q_PROPERTY(QStringList kinds READ kinds CONSTANT)
    // Bumped on every change so QML bindings that call allows() re-evaluate.
    Q_PROPERTY(int revision READ revision NOTIFY changed)

public:
    explicit PreviewSettings(QObject *parent = nullptr);
    static PreviewSettings *instance();

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    int systemMaxKiB() const { return m_systemMaxKiB; }
    void setSystemMaxKiB(int kib);
    int externalMaxKiB() const { return m_externalMaxKiB; }
    void setExternalMaxKiB(int kib);
    QStringList kinds() const;
    int revision() const { return m_revision; }

    Q_INVOKABLE bool kindEnabled(const QString &kind) const;
    Q_INVOKABLE void setKindEnabled(const QString &kind, bool enabled);
    // ignoreSizeLimit: media category pages (images, music, videos) exist to
    // show artwork; covers and frames do not cost more for larger files.
    Q_INVOKABLE bool allows(const QString &localPath, const QString &suffix,
                            const QString &mimeType, qint64 size,
                            bool ignoreSizeLimit = false) const;
    Q_INVOKABLE bool isExternalPath(const QString &localPath) const;
    Q_INVOKABLE QString previewKind(const QString &suffix, const QString &mimeType) const
    { return kindFor(suffix, mimeType); }

    // Thread-safe classification. Unknown/extension-less files are reported
    // as "text" candidates; the thumbnail worker confirms them by content.
    static QString kindFor(const QString &suffix, const QString &mimeType);
    static bool needsContentSniffing(const QString &mimeType);

signals:
    void changed();

private:
    void save();
    void bump();

    bool m_enabled = true;
    int m_systemMaxKiB = 0;
    int m_externalMaxKiB = 0;
    QSet<QString> m_disabledKinds;
    int m_revision = 0;
    mutable QHash<QString, bool> m_externalByDirectory;
    // Mount points, longest first. QStorageInfo(path) re-reads the system
    // mount table on every construction, which dominated large categories.
    mutable QStringList m_mountRoots;
    mutable qint64 m_mountRootsLoadedMs = 0;
};
