#pragma once

#include <QObject>
#include <QDateTime>

class FilePropertiesManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY propertiesChanged)
    Q_PROPERTY(QString name READ name NOTIFY propertiesChanged)
    Q_PROPERTY(bool directory READ directory NOTIFY propertiesChanged)
    Q_PROPERTY(qint64 totalSize READ totalSize NOTIFY propertiesChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY propertiesChanged)
    Q_PROPERTY(int folderCount READ folderCount NOTIFY propertiesChanged)
    Q_PROPERTY(QDateTime created READ created NOTIFY propertiesChanged)
    Q_PROPERTY(QDateTime modified READ modified NOTIFY propertiesChanged)
    Q_PROPERTY(QDateTime accessed READ accessed NOTIFY propertiesChanged)
    Q_PROPERTY(QString permissionsText READ permissionsText NOTIFY propertiesChanged)
    Q_PROPERTY(QString iconName READ iconName NOTIFY propertiesChanged)
    Q_PROPERTY(QString linkType READ linkType NOTIFY propertiesChanged)
    Q_PROPERTY(QString linkTarget READ linkTarget NOTIFY propertiesChanged)
    Q_PROPERTY(bool linkTargetExists READ linkTargetExists NOTIFY propertiesChanged)
    Q_PROPERTY(qulonglong inode READ inode NOTIFY propertiesChanged)
    Q_PROPERTY(int linkCount READ linkCount NOTIFY propertiesChanged)
    Q_PROPERTY(bool calculating READ calculating NOTIFY calculatingChanged)

public:
    explicit FilePropertiesManager(QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString name() const { return m_name; }
    bool directory() const { return m_directory; }
    qint64 totalSize() const { return m_totalSize; }
    int fileCount() const { return m_fileCount; }
    int folderCount() const { return m_folderCount; }
    QDateTime created() const { return m_created; }
    QDateTime modified() const { return m_modified; }
    QDateTime accessed() const { return m_accessed; }
    QString permissionsText() const { return m_permissionsText; }
    QString iconName() const { return m_iconName; }
    QString linkType() const { return m_linkType; }
    QString linkTarget() const { return m_linkTarget; }
    bool linkTargetExists() const { return m_linkTargetExists; }
    qulonglong inode() const { return m_inode; }
    int linkCount() const { return m_linkCount; }
    bool calculating() const { return m_calculating; }

    Q_INVOKABLE void inspect(const QString &urlOrPath);
    Q_INVOKABLE bool setFolderIcon(const QString &iconName);
    Q_INVOKABLE bool setLinkTarget(const QString &target);
    Q_INVOKABLE QString formatBytes(qint64 bytes) const;

signals:
    void propertiesChanged();
    void calculatingChanged();
    void folderIconChanged(const QString &path, const QString &iconName);
    void error(const QString &message);

private:
    void setCalculating(bool value);
    static QString readFolderIcon(const QString &path);

    QString m_path;
    QString m_name;
    bool m_directory = false;
    qint64 m_totalSize = 0;
    int m_fileCount = 0;
    int m_folderCount = 0;
    QDateTime m_created;
    QDateTime m_modified;
    QDateTime m_accessed;
    QString m_permissionsText;
    QString m_iconName;
    QString m_linkType;
    QString m_linkTarget;
    bool m_linkTargetExists = false;
    qulonglong m_inode = 0;
    int m_linkCount = 1;
    bool m_calculating = false;
    int m_generation = 0;
};
