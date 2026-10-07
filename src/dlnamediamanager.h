#pragma once

#include <QObject>
#include <QHash>
#include <QNetworkAccessManager>
#include <QTimer>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

class QUdpSocket;

class DlnaMediaManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList servers READ servers NOTIFY serversChanged)
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QVariantList displayEntries READ displayEntries NOTIFY displayEntriesChanged)
    Q_PROPERTY(QVariantList rootEntries READ rootEntries NOTIFY rootEntriesChanged)
    Q_PROPERTY(QString selectedServerId READ selectedServerId NOTIFY selectedServerChanged)
    Q_PROPERTY(QString selectedServerName READ selectedServerName NOTIFY selectedServerChanged)
    Q_PROPERTY(QString selectedProvider READ selectedProvider NOTIFY selectedServerChanged)
    Q_PROPERTY(bool discovering READ discovering NOTIFY discoveringChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString currentObjectId READ currentObjectId NOTIFY currentObjectIdChanged)
    Q_PROPERTY(QString errorString READ errorString NOTIFY errorStringChanged)

public:
    explicit DlnaMediaManager(QObject *parent = nullptr);
    ~DlnaMediaManager() override;

    QVariantList servers() const { return m_servers; }
    QVariantList entries() const { return m_entries; }
    QVariantList displayEntries() const { return m_displayEntries; }
    QVariantList rootEntries() const { return m_rootEntries; }
    QString selectedServerId() const { return m_selectedServerId; }
    QString selectedServerName() const;
    QString selectedProvider() const;
    bool discovering() const { return m_discovering; }
    bool loading() const { return m_loading; }
    QString currentObjectId() const { return m_currentObjectId; }
    QString errorString() const { return m_errorString; }

    Q_INVOKABLE void discover();
    Q_INVOKABLE void selectServer(const QString &serverId);
    Q_INVOKABLE void browseRoot();
    Q_INVOKABLE void browse(const QString &objectId);
    Q_INVOKABLE void openSection(const QString &sectionKey);
    Q_INVOKABLE bool hasSection(const QString &sectionKey) const;
    Q_INVOKABLE QString sectionObjectId(const QString &sectionKey) const;
    Q_INVOKABLE QString sectionTitle(const QString &sectionKey) const;
    Q_INVOKABLE void setViewOptions(const QString &searchText, const QString &sortMode, bool sortAscending);

signals:
    void serversChanged();
    void entriesChanged();
    void displayEntriesChanged();
    void rootEntriesChanged();
    void selectedServerChanged();
    void discoveringChanged();
    void loadingChanged();
    void currentObjectIdChanged();
    void errorStringChanged();
    void browseFinished(const QString &objectId);

private:
    struct Server {
        QString id;
        QString name;
        QString provider;
        QUrl location;
        QUrl baseUrl;
        QUrl controlUrl;
        QString contentDirectoryType;
        QString manufacturer;
        QString modelName;
        QString serverHeader;
    };

    void ensureSocket();
    void processSsdpDatagrams();
    void fetchDescription(const QUrl &location, const QString &serverHeader, const QString &usn);
    void addOrUpdateServer(const Server &server);
    const Server *selectedServer() const;
    QVariantMap serverToMap(const Server &server) const;
    void setDiscovering(bool value);
    void setLoading(bool value);
    void setErrorString(const QString &value);
    void performBrowse(const QString &objectId, bool rootRequest);
    void performBrowsePage(const QString &objectId,
                           bool rootRequest,
                           int startingIndex,
                           const QVariantList &accumulated,
                           quint64 generation);
    QVariantList parseDidl(const QString &xml) const;
    QUrl resolveDeviceUrl(const QString &value) const;
    QString classifyProvider(const QString &name,
                             const QString &manufacturer,
                             const QString &model,
                             const QString &serverHeader) const;
    bool entryMatchesSection(const QVariantMap &entry, const QString &sectionKey) const;
    void rebuildDisplayEntries();
    QString configFilePath() const;
    void loadSettings();
    void saveSelection() const;

    QVariantList m_servers;
    QVariantList m_entries;
    QVariantList m_displayEntries;
    QVariantList m_rootEntries;
    QHash<QString, Server> m_serverById;
    QHash<QString, QString> m_idByLocation;
    QString m_selectedServerId;
    QString m_savedServerLocation;
    QString m_currentObjectId;
    QString m_errorString;
    QString m_pendingSection;
    QString m_viewSearchText;
    QString m_viewSortMode = QStringLiteral("name");
    bool m_viewSortAscending = true;
    bool m_discovering = false;
    bool m_loading = false;
    QUdpSocket *m_socket = nullptr;
    QNetworkAccessManager m_network;
    QTimer m_discoveryTimer;
    quint64 m_browseGeneration = 0;
};
