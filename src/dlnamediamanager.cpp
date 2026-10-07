#include "dlnamediamanager.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHostAddress>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QUdpSocket>
#include <QXmlStreamReader>
#include <algorithm>

namespace {
constexpr quint16 SsdpPort = 1900;
const QHostAddress SsdpAddress(QStringLiteral("239.255.255.250"));
const QString ContentDirectoryPrefix = QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:");

QString headerValue(const QByteArray &datagram, const QByteArray &wanted)
{
    const QList<QByteArray> lines = datagram.split('\n');
    const QByteArray wantedLower = wanted.toLower();
    for (QByteArray line : lines) {
        line = line.trimmed();
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        if (line.left(colon).trimmed().toLower() == wantedLower)
            return QString::fromUtf8(line.mid(colon + 1).trimmed());
    }
    return {};
}

QString xmlEscape(QString value)
{
    value.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    value.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    value.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    value.replace(QLatin1Char('"'), QStringLiteral("&quot;"));
    value.replace(QLatin1Char('\''), QStringLiteral("&apos;"));
    return value;
}

qint64 parseDurationMs(const QString &duration)
{
    if (duration.isEmpty())
        return 0;
    const QString timePart = duration.section(QLatin1Char('.'), 0, 0);
    const QStringList parts = timePart.split(QLatin1Char(':'));
    if (parts.size() != 3)
        return 0;
    bool okH = false, okM = false, okS = false;
    const qint64 h = parts.at(0).toLongLong(&okH);
    const qint64 m = parts.at(1).toLongLong(&okM);
    const qint64 s = parts.at(2).toLongLong(&okS);
    if (!okH || !okM || !okS)
        return 0;
    return ((h * 60 + m) * 60 + s) * 1000;
}
}

DlnaMediaManager::DlnaMediaManager(QObject *parent)
    : QObject(parent)
{
    m_discoveryTimer.setSingleShot(true);
    m_discoveryTimer.setInterval(2800);
    connect(&m_discoveryTimer, &QTimer::timeout, this, [this]() {
        setDiscovering(false);
        if (m_selectedServerId.isEmpty() && m_servers.size() == 1) {
            const QString id = m_servers.constFirst().toMap().value(QStringLiteral("id")).toString();
            if (!id.isEmpty())
                selectServer(id);
        }
    });
    loadSettings();
    QTimer::singleShot(250, this, &DlnaMediaManager::discover);
}

DlnaMediaManager::~DlnaMediaManager() = default;

QString DlnaMediaManager::configFilePath() const
{
    QString root = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (root.isEmpty())
        root = QDir::home().filePath(QStringLiteral(".config"));
    const QString appDir = QDir(root).filePath(QStringLiteral("g-File"));
    QDir().mkpath(appDir);
    return QDir(appDir).filePath(QStringLiteral("dlna.ini"));
}

void DlnaMediaManager::loadSettings()
{
    QSettings settings(configFilePath(), QSettings::IniFormat);
    m_savedServerLocation = settings.value(QStringLiteral("server/location")).toString();
}

void DlnaMediaManager::saveSelection() const
{
    const Server *server = selectedServer();
    QSettings settings(configFilePath(), QSettings::IniFormat);
    settings.setValue(QStringLiteral("server/location"), server ? server->location.toString() : QString());
    settings.sync();
}

void DlnaMediaManager::ensureSocket()
{
    if (m_socket)
        return;
    m_socket = new QUdpSocket(this);
    m_socket->bind(QHostAddress::AnyIPv4, 0,
                   QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(m_socket, &QUdpSocket::readyRead, this, &DlnaMediaManager::processSsdpDatagrams);
}

void DlnaMediaManager::discover()
{
    ensureSocket();
    setErrorString({});
    setDiscovering(true);
    m_discoveryTimer.start();

    const QByteArray payload = QByteArrayLiteral(
        "M-SEARCH * HTTP/1.1\r\n"
        "HOST: 239.255.255.250:1900\r\n"
        "MAN: \"ssdp:discover\"\r\n"
        "MX: 2\r\n"
        "ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"
        "USER-AGENT: G-File/0.5 UPnP/1.1\r\n"
        "\r\n");
    m_socket->writeDatagram(payload, SsdpAddress, SsdpPort);
}

void DlnaMediaManager::processSsdpDatagrams()
{
    while (m_socket && m_socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(int(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        quint16 port = 0;
        m_socket->readDatagram(datagram.data(), datagram.size(), &sender, &port);
        Q_UNUSED(sender)
        Q_UNUSED(port)
        const QString locationText = headerValue(datagram, QByteArrayLiteral("location"));
        if (locationText.isEmpty())
            continue;
        const QUrl location(locationText);
        if (!location.isValid() || location.scheme().isEmpty())
            continue;
        const QString key = location.toString();
        if (m_idByLocation.contains(key))
            continue;
        fetchDescription(location,
                         headerValue(datagram, QByteArrayLiteral("server")),
                         headerValue(datagram, QByteArrayLiteral("usn")));
    }
}

void DlnaMediaManager::fetchDescription(const QUrl &location,
                                        const QString &serverHeader,
                                        const QString &usn)
{
    QNetworkRequest request(location);
    request.setTransferTimeout(8000);
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("G-File/0.5 UPnP/1.1"));
    QNetworkReply *reply = m_network.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, location, serverHeader, usn]() {
        const QByteArray body = reply->readAll();
        const auto error = reply->error();
        reply->deleteLater();
        if (error != QNetworkReply::NoError)
            return;

        QXmlStreamReader xml(body);
        Server server;
        server.location = location;
        server.baseUrl = location;
        server.serverHeader = serverHeader;
        server.id = usn.section(QStringLiteral("::"), 0, 0).trimmed();
        QString currentServiceType;
        QString currentControlUrl;
        bool insideService = false;

        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement()) {
                const QStringView name = xml.name();
                if (name == QStringLiteral("service")) {
                    insideService = true;
                    currentServiceType.clear();
                    currentControlUrl.clear();
                } else if (insideService && name == QStringLiteral("serviceType")) {
                    currentServiceType = xml.readElementText().trimmed();
                } else if (insideService && name == QStringLiteral("controlURL")) {
                    currentControlUrl = xml.readElementText().trimmed();
                } else if (name == QStringLiteral("URLBase")) {
                    const QUrl declaredBase(xml.readElementText().trimmed());
                    if (declaredBase.isValid() && !declaredBase.scheme().isEmpty())
                        server.baseUrl = declaredBase;
                } else if (name == QStringLiteral("friendlyName")) {
                    server.name = xml.readElementText().trimmed();
                } else if (name == QStringLiteral("manufacturer")) {
                    server.manufacturer = xml.readElementText().trimmed();
                } else if (name == QStringLiteral("modelName")) {
                    server.modelName = xml.readElementText().trimmed();
                } else if (name == QStringLiteral("UDN")) {
                    const QString udn = xml.readElementText().trimmed();
                    if (!udn.isEmpty())
                        server.id = udn;
                }
            } else if (xml.isEndElement() && xml.name() == QStringLiteral("service")) {
                if (currentServiceType.startsWith(ContentDirectoryPrefix)) {
                    server.controlUrl = server.baseUrl.resolved(QUrl(currentControlUrl));
                    server.contentDirectoryType = currentServiceType;
                }
                insideService = false;
            }
        }
        if (xml.hasError() || server.controlUrl.isEmpty())
            return;
        if (server.id.isEmpty())
            server.id = location.toString();
        if (server.name.isEmpty())
            server.name = location.host();
        server.provider = classifyProvider(server.name, server.manufacturer, server.modelName, serverHeader);
        addOrUpdateServer(server);
    });
}

QString DlnaMediaManager::classifyProvider(const QString &name,
                                           const QString &manufacturer,
                                           const QString &model,
                                           const QString &serverHeader) const
{
    const QString text = (name + QLatin1Char(' ') + manufacturer + QLatin1Char(' ') + model
                          + QLatin1Char(' ') + serverHeader).toLower();
    if (text.contains(QStringLiteral("jellyfin")))
        return QStringLiteral("Jellyfin");
    if (text.contains(QStringLiteral("emby")))
        return QStringLiteral("Emby");
    if (text.contains(QStringLiteral("gig")))
        return QStringLiteral("GiG");
    return QStringLiteral("DLNA");
}

QVariantMap DlnaMediaManager::serverToMap(const Server &server) const
{
    return {
        {QStringLiteral("id"), server.id},
        {QStringLiteral("name"), server.name},
        {QStringLiteral("provider"), server.provider},
        {QStringLiteral("location"), server.location.toString()},
        {QStringLiteral("manufacturer"), server.manufacturer},
        {QStringLiteral("modelName"), server.modelName}
    };
}

void DlnaMediaManager::addOrUpdateServer(const Server &server)
{
    m_serverById.insert(server.id, server);
    m_idByLocation.insert(server.location.toString(), server.id);

    QVariantList next;
    next.reserve(m_serverById.size());
    for (auto it = m_serverById.cbegin(); it != m_serverById.cend(); ++it)
        next.append(serverToMap(it.value()));
    std::sort(next.begin(), next.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("name")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    m_servers = next;
    emit serversChanged();

    if (m_selectedServerId.isEmpty() && !m_savedServerLocation.isEmpty()
            && server.location.toString() == m_savedServerLocation) {
        selectServer(server.id);
    }
}

const DlnaMediaManager::Server *DlnaMediaManager::selectedServer() const
{
    const auto it = m_serverById.constFind(m_selectedServerId);
    return it == m_serverById.cend() ? nullptr : &it.value();
}

QString DlnaMediaManager::selectedServerName() const
{
    const Server *server = selectedServer();
    return server ? server->name : QString();
}

QString DlnaMediaManager::selectedProvider() const
{
    const Server *server = selectedServer();
    return server ? server->provider : QString();
}

void DlnaMediaManager::setViewOptions(const QString &searchText, const QString &sortMode, bool sortAscending)
{
    const QString normalizedSearch = searchText.trimmed();
    const QString normalizedSort = sortMode.trimmed().toLower();
    if (m_viewSearchText == normalizedSearch
        && m_viewSortMode == normalizedSort
        && m_viewSortAscending == sortAscending)
        return;

    m_viewSearchText = normalizedSearch;
    m_viewSortMode = normalizedSort.isEmpty() ? QStringLiteral("name") : normalizedSort;
    m_viewSortAscending = sortAscending;
    rebuildDisplayEntries();
}

void DlnaMediaManager::rebuildDisplayEntries()
{
    QVariantList filtered;
    filtered.reserve(m_entries.size());

    const QString query = m_viewSearchText.trimmed();
    for (const QVariant &value : m_entries) {
        const QVariantMap item = value.toMap();
        if (!query.isEmpty()) {
            QString haystack = item.value(QStringLiteral("title")).toString();
            haystack += QLatin1Char(' ');
            haystack += item.value(QStringLiteral("year")).toString();
            haystack += QLatin1Char(' ');
            haystack += item.value(QStringLiteral("date")).toString();
            haystack += QLatin1Char(' ');
            haystack += item.value(QStringLiteral("artist")).toString();
            haystack += QLatin1Char(' ');
            haystack += item.value(QStringLiteral("album")).toString();
            if (!haystack.contains(query, Qt::CaseInsensitive))
                continue;
        }
        filtered.append(value);
    }

    const QString mode = m_viewSortMode;
    const bool ascending = m_viewSortAscending;
    std::stable_sort(filtered.begin(), filtered.end(), [mode, ascending](const QVariant &left, const QVariant &right) {
        const QVariantMap a = left.toMap();
        const QVariantMap b = right.toMap();
        const bool aContainer = a.value(QStringLiteral("kind")).toString() == QStringLiteral("container");
        const bool bContainer = b.value(QStringLiteral("kind")).toString() == QStringLiteral("container");
        if (aContainer != bContainer)
            return aContainer;

        int cmp = 0;
        if (mode == QStringLiteral("size")) {
            const qlonglong av = a.value(QStringLiteral("size")).toLongLong();
            const qlonglong bv = b.value(QStringLiteral("size")).toLongLong();
            cmp = av < bv ? -1 : (av > bv ? 1 : 0);
        } else if (mode == QStringLiteral("date") || mode == QStringLiteral("created")
                   || mode == QStringLiteral("added") || mode == QStringLiteral("dateadded")) {
            const qlonglong av = a.value(QStringLiteral("dateMs")).toLongLong();
            const qlonglong bv = b.value(QStringLiteral("dateMs")).toLongLong();
            cmp = av < bv ? -1 : (av > bv ? 1 : 0);
            if (cmp == 0) {
                const QString as = a.value(QStringLiteral("date")).toString();
                const QString bs = b.value(QStringLiteral("date")).toString();
                cmp = QString::compare(as, bs, Qt::CaseInsensitive);
            }
            if (cmp == 0) {
                const int ay = a.value(QStringLiteral("year")).toInt();
                const int by = b.value(QStringLiteral("year")).toInt();
                cmp = ay < by ? -1 : (ay > by ? 1 : 0);
            }
        } else if (mode == QStringLiteral("type")) {
            const QString av = a.value(QStringLiteral("mediaKind")).toString()
                    + QLatin1Char('|') + a.value(QStringLiteral("className")).toString();
            const QString bv = b.value(QStringLiteral("mediaKind")).toString()
                    + QLatin1Char('|') + b.value(QStringLiteral("className")).toString();
            cmp = QString::compare(av, bv, Qt::CaseInsensitive);
        } else {
            cmp = QString::compare(a.value(QStringLiteral("title")).toString(),
                                   b.value(QStringLiteral("title")).toString(),
                                   Qt::CaseInsensitive);
        }

        if (cmp == 0)
            cmp = QString::compare(a.value(QStringLiteral("title")).toString(),
                                   b.value(QStringLiteral("title")).toString(),
                                   Qt::CaseInsensitive);
        return ascending ? cmp < 0 : cmp > 0;
    });

    m_displayEntries = filtered;
    emit displayEntriesChanged();
}

void DlnaMediaManager::selectServer(const QString &serverId)
{
    if (!m_serverById.contains(serverId))
        return;
    const bool changed = m_selectedServerId != serverId;
    m_selectedServerId = serverId;
    m_entries.clear();
    m_displayEntries.clear();
    m_rootEntries.clear();
    m_currentObjectId.clear();
    m_pendingSection.clear();
    setErrorString({});
    saveSelection();
    if (changed)
        emit selectedServerChanged();
    emit entriesChanged();
    emit displayEntriesChanged();
    emit rootEntriesChanged();
    emit currentObjectIdChanged();
}

void DlnaMediaManager::browseRoot()
{
    performBrowse(QStringLiteral("0"), true);
}

void DlnaMediaManager::browse(const QString &objectId)
{
    if (objectId.isEmpty())
        return;
    performBrowse(objectId, objectId == QStringLiteral("0"));
}

QString DlnaMediaManager::sectionObjectId(const QString &sectionKey) const
{
    const QString key = sectionKey.trimmed().toLower();
    for (const QVariant &value : m_rootEntries) {
        const QVariantMap entry = value.toMap();
        if (entryMatchesSection(entry, key))
            return entry.value(QStringLiteral("objectId")).toString();
    }
    return {};
}

QString DlnaMediaManager::sectionTitle(const QString &sectionKey) const
{
    const QString id = sectionObjectId(sectionKey);
    if (id.isEmpty())
        return {};
    for (const QVariant &value : m_rootEntries) {
        const QVariantMap entry = value.toMap();
        if (entry.value(QStringLiteral("objectId")).toString() == id)
            return entry.value(QStringLiteral("title")).toString();
    }
    return {};
}

bool DlnaMediaManager::hasSection(const QString &sectionKey) const
{
    return !sectionObjectId(sectionKey).isEmpty();
}

bool DlnaMediaManager::entryMatchesSection(const QVariantMap &entry, const QString &sectionKey) const
{
    const QString id = entry.value(QStringLiteral("objectId")).toString().toLower();
    const QString title = entry.value(QStringLiteral("title")).toString().toLower();
    if (sectionKey == QStringLiteral("movies")) {
        return id == QStringLiteral("movies")
            || title.contains(QStringLiteral("movie"))
            || title.contains(QStringLiteral("film"));
    }
    if (sectionKey == QStringLiteral("shows")) {
        return id == QStringLiteral("shows")
            || title.contains(QStringLiteral("tv show"))
            || title.contains(QStringLiteral("series"))
            || title.contains(QStringLiteral("dizi"))
            || title.contains(QStringLiteral("television"));
    }
    if (sectionKey == QStringLiteral("music")) {
        return id == QStringLiteral("music")
            || title.contains(QStringLiteral("music"))
            || title.contains(QStringLiteral("müzik"))
            || title.contains(QStringLiteral("audio"));
    }
    return false;
}

void DlnaMediaManager::openSection(const QString &sectionKey)
{
    if (m_selectedServerId.isEmpty())
        return;
    const QString key = sectionKey.trimmed().toLower();
    if (key == QStringLiteral("all")) {
        m_pendingSection.clear();
        browseRoot();
        return;
    }
    const QString objectId = sectionObjectId(key);
    if (!objectId.isEmpty()) {
        m_pendingSection.clear();
        browse(objectId);
        return;
    }
    m_pendingSection = key;
    browseRoot();
}

void DlnaMediaManager::performBrowse(const QString &objectId, bool rootRequest)
{
    const Server *server = selectedServer();
    if (!server) {
        setErrorString(QStringLiteral("No DLNA media server selected."));
        return;
    }
    Q_UNUSED(server)
    setLoading(true);
    setErrorString({});
    const quint64 generation = ++m_browseGeneration;
    performBrowsePage(objectId, rootRequest, 0, {}, generation);
}

void DlnaMediaManager::performBrowsePage(const QString &objectId,
                                         bool rootRequest,
                                         int startingIndex,
                                         const QVariantList &accumulated,
                                         quint64 generation)
{
    const Server *server = selectedServer();
    if (!server) {
        setLoading(false);
        setErrorString(QStringLiteral("No DLNA media server selected."));
        return;
    }

    const QString serviceType = server->contentDirectoryType.isEmpty()
            ? ContentDirectoryPrefix + QStringLiteral("1")
            : server->contentDirectoryType;
    const QString body = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:Browse xmlns:u=\"%1\">"
        "<ObjectID>%2</ObjectID><BrowseFlag>BrowseDirectChildren</BrowseFlag>"
        "<Filter>*</Filter><StartingIndex>%3</StartingIndex><RequestedCount>500</RequestedCount>"
        "<SortCriteria></SortCriteria></u:Browse></s:Body></s:Envelope>")
            .arg(xmlEscape(serviceType), xmlEscape(objectId), QString::number(startingIndex));

    QNetworkRequest request(server->controlUrl);
    request.setTransferTimeout(12000);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("text/xml; charset=\"utf-8\""));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("G-File/0.5 UPnP/1.1"));
    request.setRawHeader("SOAPACTION", (QStringLiteral("\"") + serviceType
                                           + QStringLiteral("#Browse\"")).toUtf8());
    QNetworkReply *reply = m_network.post(request, body.toUtf8());
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, objectId, rootRequest, startingIndex, accumulated, generation]() {
        const QByteArray payload = reply->readAll();
        const auto networkError = reply->error();
        const QString networkMessage = reply->errorString();
        reply->deleteLater();
        if (generation != m_browseGeneration)
            return;
        if (networkError != QNetworkReply::NoError) {
            setLoading(false);
            setErrorString(networkMessage);
            return;
        }

        QXmlStreamReader envelope(payload);
        QString didl;
        int numberReturned = 0;
        int totalMatches = 0;
        while (!envelope.atEnd()) {
            envelope.readNext();
            if (!envelope.isStartElement())
                continue;
            if (envelope.name() == QStringLiteral("Result"))
                didl = envelope.readElementText(QXmlStreamReader::IncludeChildElements);
            else if (envelope.name() == QStringLiteral("NumberReturned"))
                numberReturned = envelope.readElementText().trimmed().toInt();
            else if (envelope.name() == QStringLiteral("TotalMatches"))
                totalMatches = envelope.readElementText().trimmed().toInt();
        }
        if (didl.isEmpty()) {
            setLoading(false);
            setErrorString(QStringLiteral("DLNA server returned an empty ContentDirectory response."));
            return;
        }

        QVariantList all = accumulated;
        const QVariantList page = parseDidl(didl);
        for (const QVariant &entry : page)
            all.append(entry);

        const int effectiveReturned = numberReturned > 0 ? numberReturned : page.size();
        const int nextIndex = startingIndex + effectiveReturned;
        if (effectiveReturned > 0 && totalMatches > nextIndex) {
            performBrowsePage(objectId, rootRequest, nextIndex, all, generation);
            return;
        }

        setLoading(false);
        m_entries = all;
        rebuildDisplayEntries();
        m_currentObjectId = objectId;
        emit entriesChanged();
        emit currentObjectIdChanged();

        if (rootRequest) {
            m_rootEntries = all;
            emit rootEntriesChanged();
            if (!m_pendingSection.isEmpty()) {
                const QString pending = m_pendingSection;
                m_pendingSection.clear();
                const QString target = sectionObjectId(pending);
                if (!target.isEmpty()) {
                    browse(target);
                    return;
                }
            }
        }
        emit browseFinished(objectId);
    });
}

QUrl DlnaMediaManager::resolveDeviceUrl(const QString &value) const
{
    const Server *server = selectedServer();
    if (!server)
        return QUrl(value);
    return server->baseUrl.resolved(QUrl(value));
}

QVariantList DlnaMediaManager::parseDidl(const QString &xmlText) const
{
    QVariantList result;
    QXmlStreamReader xml(xmlText);
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement())
            continue;
        const QStringView elementName = xml.name();
        if (elementName != QStringLiteral("item") && elementName != QStringLiteral("container"))
            continue;

        const bool container = elementName == QStringLiteral("container");
        const auto attrs = xml.attributes();
        QVariantMap entry;
        entry.insert(QStringLiteral("kind"), container ? QStringLiteral("container") : QStringLiteral("item"));
        entry.insert(QStringLiteral("objectId"), attrs.value(QStringLiteral("id")).toString());
        entry.insert(QStringLiteral("parentId"), attrs.value(QStringLiteral("parentID")).toString());
        entry.insert(QStringLiteral("childCount"), attrs.value(QStringLiteral("childCount")).toInt());
        QString className;
        QString dateText;
        QString artUrl;
        QString resourceUrl;
        QString protocolInfo;
        QString resolution;
        qint64 size = 0;
        qint64 durationMs = 0;

        while (!(xml.isEndElement() && xml.name() == elementName) && !xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement())
                continue;
            const QStringView childName = xml.name();
            if (childName == QStringLiteral("title")) {
                entry.insert(QStringLiteral("title"), xml.readElementText().trimmed());
            } else if (childName == QStringLiteral("class")) {
                className = xml.readElementText().trimmed();
            } else if (childName == QStringLiteral("date")) {
                dateText = xml.readElementText().trimmed();
            } else if (childName == QStringLiteral("artist")) {
                entry.insert(QStringLiteral("artist"), xml.readElementText().trimmed());
            } else if (childName == QStringLiteral("album")) {
                entry.insert(QStringLiteral("album"), xml.readElementText().trimmed());
            } else if (childName == QStringLiteral("albumArtURI")) {
                artUrl = resolveDeviceUrl(xml.readElementText().trimmed()).toString();
            } else if (childName == QStringLiteral("tmdbId")) {
                entry.insert(QStringLiteral("tmdbId"), xml.readElementText().trimmed().toInt());
            } else if (childName == QStringLiteral("mediaType")) {
                entry.insert(QStringLiteral("sourceMediaType"), xml.readElementText().trimmed().toLower());
            } else if (childName == QStringLiteral("res")) {
                const auto resAttrs = xml.attributes();
                protocolInfo = resAttrs.value(QStringLiteral("protocolInfo")).toString();
                resolution = resAttrs.value(QStringLiteral("resolution")).toString();
                size = resAttrs.value(QStringLiteral("size")).toLongLong();
                durationMs = parseDurationMs(resAttrs.value(QStringLiteral("duration")).toString());
                resourceUrl = resolveDeviceUrl(xml.readElementText().trimmed()).toString();
            }
        }

        if (!entry.contains(QStringLiteral("title")))
            entry.insert(QStringLiteral("title"), entry.value(QStringLiteral("objectId")));
        entry.insert(QStringLiteral("className"), className);
        entry.insert(QStringLiteral("date"), dateText);
        const QDateTime parsedDate = QDateTime::fromString(dateText, Qt::ISODate);
        entry.insert(QStringLiteral("dateMs"), parsedDate.isValid() ? parsedDate.toMSecsSinceEpoch() : 0);

        // dc:date is the library/dateAdded timestamp for GiG and many DLNA
        // servers. It must not be confused with the production/release year
        // used for TMDB matching. GiG deliberately includes releaseYear in
        // the clean display title ("Title (2021)"), so extract it there.
        int releaseYear = 0;
        const int maximumReleaseYear = QDateTime::currentDateTimeUtc().date().year() + 3;
        const QString displayTitle = entry.value(QStringLiteral("title")).toString();
        static const QRegularExpression releaseYearRe(
            QStringLiteral(R"((?:^|[\s._\-\(\[])(18\d{2}|19\d{2}|20\d{2}|21\d{2})(?=$|[\s._\-\)\]]))"));
        QRegularExpressionMatchIterator releaseYears = releaseYearRe.globalMatch(displayTitle);
        while (releaseYears.hasNext()) {
            const int candidateYear = releaseYears.next().captured(1).toInt();
            if (candidateYear >= 1888 && candidateYear <= maximumReleaseYear)
                releaseYear = candidateYear;
        }
        // Some DLNA servers publish a plain YYYY-MM-DD release date instead
        // of putting the year in the title. GiG's dateAdded is a full ISO
        // timestamp, so this fallback does not mistake it for a release year.
        if (releaseYear == 0) {
            static const QRegularExpression plainDateRe(QStringLiteral(R"(^(19\d{2}|20\d{2}|21\d{2})-\d{2}-\d{2}$)"));
            const QRegularExpressionMatch dateMatch = plainDateRe.match(dateText);
            if (dateMatch.hasMatch())
                releaseYear = dateMatch.captured(1).toInt();
        }
        entry.insert(QStringLiteral("year"), releaseYear);
        entry.insert(QStringLiteral("artUrl"), artUrl);
        entry.insert(QStringLiteral("url"), resourceUrl);
        entry.insert(QStringLiteral("size"), size);
        entry.insert(QStringLiteral("durationMs"), durationMs);
        entry.insert(QStringLiteral("resolution"), resolution);
        entry.insert(QStringLiteral("protocolInfo"), protocolInfo);

        const QString lowerClass = className.toLower();
        const QString lowerProtocol = protocolInfo.toLower();
        QString mediaKind = QStringLiteral("container");
        if (!container) {
            if (lowerClass.contains(QStringLiteral("audioitem")) || lowerProtocol.contains(QStringLiteral("audio/")))
                mediaKind = QStringLiteral("audio");
            else if (lowerClass.contains(QStringLiteral("imageitem")) || lowerProtocol.contains(QStringLiteral("image/")))
                mediaKind = QStringLiteral("image");
            else
                mediaKind = QStringLiteral("video");
        }
        entry.insert(QStringLiteral("mediaKind"), mediaKind);
        entry.insert(QStringLiteral("playable"), !resourceUrl.isEmpty() && !container);
        result.append(entry);
    }
    return result;
}

void DlnaMediaManager::setDiscovering(bool value)
{
    if (m_discovering == value)
        return;
    m_discovering = value;
    emit discoveringChanged();
}

void DlnaMediaManager::setLoading(bool value)
{
    if (m_loading == value)
        return;
    m_loading = value;
    emit loadingChanged();
}

void DlnaMediaManager::setErrorString(const QString &value)
{
    if (m_errorString == value)
        return;
    m_errorString = value;
    emit errorStringChanged();
}
