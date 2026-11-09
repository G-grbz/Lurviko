#pragma once

#include <QObject>
#include <QString>

// Read-only view of the persistent directory size index for one folder, used
// by Home shortcut cards. It never walks the folder itself: the totals come
// from the index, which keeps them current through its own change tracking.
class FolderStats : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool available READ available NOTIFY statsChanged)
    Q_PROPERTY(int files READ files NOTIFY statsChanged)
    Q_PROPERTY(int folders READ folders NOTIFY statsChanged)
    Q_PROPERTY(qint64 bytes READ bytes NOTIFY statsChanged)
public:
    explicit FolderStats(QObject* parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString& path);
    bool available() const { return m_available; }
    int files() const { return m_files; }
    int folders() const { return m_folders; }
    qint64 bytes() const { return m_bytes; }

signals:
    void pathChanged();
    void statsChanged();

private:
    void reload();

    QString m_path;      // local path being shown, empty when not covered
    bool m_available = false;
    int m_files = 0;
    int m_folders = 0;
    qint64 m_bytes = 0;
};
