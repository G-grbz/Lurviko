#include "thumbnailextractors.h"

#include <KCompressionDevice>
#include <KZip>

#include <QBuffer>
#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QIcon>
#include <QImageReader>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QXmlStreamReader>
#include <QtEndian>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <memory>
#include <optional>

namespace {

constexpr qint64 kMaxEntryBytes = 32 * 1024 * 1024;

// ---- Shared helpers ---------------------------------------------------------

// Bounds-checked little/big-endian reads; anything outside the buffer reads
// as zero, so malformed files end parsing loops instead of reading past them.
struct Bytes {
    const uchar *p = nullptr;
    qint64 n = 0;

    explicit Bytes(const QByteArray &data)
        : p(reinterpret_cast<const uchar *>(data.constData())), n(data.size()) {}
    Bytes(const uchar *data, qint64 size) : p(data), n(size) {}

    bool has(qint64 offset, qint64 length) const
    {
        return offset >= 0 && length >= 0 && offset <= n && length <= n - offset;
    }
    quint8 u8(qint64 o) const { return has(o, 1) ? p[o] : 0; }
    quint16 u16(qint64 o) const { return has(o, 2) ? qFromLittleEndian<quint16>(p + o) : 0; }
    quint32 u32(qint64 o) const { return has(o, 4) ? qFromLittleEndian<quint32>(p + o) : 0; }
    quint16 be16(qint64 o) const { return has(o, 2) ? qFromBigEndian<quint16>(p + o) : 0; }
    quint32 be32(qint64 o) const { return has(o, 4) ? qFromBigEndian<quint32>(p + o) : 0; }
    QByteArray copy(qint64 o, qint64 length) const
    {
        return has(o, length) ? QByteArray(reinterpret_cast<const char *>(p + o), length) : QByteArray();
    }
};

QImage decodeImage(QByteArray data, const QSize &maximum = QSize(512, 512),
                   const QByteArray &format = {})
{
    if (data.isEmpty())
        return {};
    QBuffer buffer(&data);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer, format);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid() && (source.width() > maximum.width() || source.height() > maximum.height()))
        reader.setScaledSize(source.scaled(maximum, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (!image.isNull() && (image.width() > maximum.width() || image.height() > maximum.height()))
        image = image.scaled(maximum, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

// Renders SVG markup at exactly `size`, scaling small documents up.
QImage renderSvg(QByteArray svg, const QSize &size)
{
    if (svg.isEmpty() || svg.size() > 8 * 1024 * 1024)
        return {};
    QBuffer buffer(&svg);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer, QByteArrayLiteral("svg"));
    const QSize source = reader.size();
    reader.setScaledSize(source.isValid() ? source.scaled(size, Qt::KeepAspectRatio) : size);
    return reader.read();
}

QByteArray archiveEntry(const KArchiveDirectory *root, const QString &name,
                        qint64 limit = kMaxEntryBytes)
{
    const KArchiveEntry *entry = root ? root->entry(name) : nullptr;
    if (!entry || !entry->isFile())
        return {};
    const auto *file = static_cast<const KArchiveFile *>(entry);
    if (file->size() <= 0 || file->size() > limit)
        return {};
    return file->data();
}

void listArchive(const KArchiveDirectory *directory, const QString &prefix, QStringList &out)
{
    if (!directory || out.size() > 50000)
        return;
    const QStringList names = directory->entries();
    for (const QString &name : names) {
        const KArchiveEntry *entry = directory->entry(name);
        const QString path = prefix.isEmpty() ? name : prefix + QLatin1Char('/') + name;
        if (entry->isDirectory())
            listArchive(static_cast<const KArchiveDirectory *>(entry), path, out);
        else
            out.append(path);
    }
}

QImage fitInto(const QImage &image, int size)
{
    if (image.isNull())
        return {};
    QImage canvas(size, size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);
    const QImage scaled = image.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&canvas);
    painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    return canvas;
}

// ---- ZIP central directory reader -----------------------------------------

// Signed APKs keep an "APK Signing Block" between the last entry and the
// central directory, which KZip's sequential scan rejects. This reader goes
// through the central directory like Android does, and can read a stored
// (uncompressed) split APK in place inside its bundle.
class ZipReader
{
public:
    ZipReader(QIODevice *device, qint64 base, qint64 size)
        : m_device(device), m_base(base), m_size(size) {}

    bool open()
    {
        if (m_size < 22)
            return false;
        const qint64 tailLength = qMin<qint64>(m_size, 22 + 65535);
        const QByteArray tail = readAt(m_size - tailLength, tailLength);
        const qsizetype eocd = tail.lastIndexOf(QByteArrayLiteral("PK\x05\x06"));
        if (eocd < 0)
            return false;
        const Bytes t(tail);
        const quint16 count = t.u16(eocd + 10);
        const quint32 directorySize = t.u32(eocd + 12);
        const quint32 directoryOffset = t.u32(eocd + 16);
        if (directoryOffset == 0xffffffff || directorySize > 64 * 1024 * 1024
                || qint64(directoryOffset) + directorySize > m_size)
            return false; // ZIP64 or damaged.
        const QByteArray directory = readAt(directoryOffset, directorySize);
        const Bytes d(directory);
        qint64 pos = 0;
        for (int i = 0; i < count && d.u32(pos) == 0x02014b50; ++i) {
            const quint16 nameLength = d.u16(pos + 28);
            const qint64 next = pos + 46 + nameLength + d.u16(pos + 30) + d.u16(pos + 32);
            if (!d.has(pos, next - pos))
                break;
            Entry entry;
            entry.method = d.u16(pos + 10);
            entry.compressed = d.u32(pos + 20);
            entry.size = d.u32(pos + 24);
            entry.localHeader = d.u32(pos + 42);
            m_entries.insert(QString::fromUtf8(directory.constData() + pos + 46, nameLength), entry);
            pos = next;
        }
        return !m_entries.isEmpty();
    }

    QStringList names() const { return m_entries.keys(); }
    bool contains(const QString &name) const { return m_entries.contains(name); }

    // Offset of a stored entry's bytes in the underlying device, or -1.
    qint64 storedOffset(const QString &name, qint64 *size) const
    {
        const auto found = m_entries.constFind(name);
        if (found == m_entries.cend() || found->method != 0)
            return -1;
        const qint64 data = dataOffset(*found);
        if (data < 0)
            return -1;
        *size = found->size;
        return m_base + data;
    }

    QByteArray read(const QString &name, qint64 limit = kMaxEntryBytes) const
    {
        const auto found = m_entries.constFind(name);
        if (found == m_entries.cend() || found->size <= 0 || found->size > limit
                || found->compressed > limit)
            return {};
        const qint64 data = dataOffset(*found);
        if (data < 0)
            return {};
        QByteArray raw = readAt(data, found->compressed);
        if (raw.size() != found->compressed)
            return {};
        if (found->method == 0)
            return raw;
        if (found->method != 8)
            return {};
        QBuffer buffer(&raw);
        KCompressionDevice inflater(&buffer, false, KCompressionDevice::GZip);
        inflater.setSkipHeaders(); // Raw deflate, as in ZIP.
        if (!inflater.open(QIODevice::ReadOnly))
            return {};
        const QByteArray out = inflater.read(found->size);
        return out.size() == found->size ? out : QByteArray();
    }

private:
    struct Entry {
        quint16 method = 0;
        qint64 compressed = 0;
        qint64 size = 0;
        qint64 localHeader = 0;
    };

    QByteArray readAt(qint64 offset, qint64 length) const
    {
        if (offset < 0 || length < 0 || offset + length > m_size || !m_device->seek(m_base + offset))
            return {};
        return m_device->read(length);
    }

    qint64 dataOffset(const Entry &entry) const
    {
        const QByteArray local = readAt(entry.localHeader, 30);
        const Bytes l(local);
        if (l.u32(0) != 0x04034b50)
            return -1;
        const qint64 data = entry.localHeader + 30 + l.u16(26) + l.u16(28);
        return data + entry.compressed <= m_size ? data : -1;
    }

    QIODevice *m_device;
    qint64 m_base;
    qint64 m_size;
    QHash<QString, Entry> m_entries;
};

// ---- Android binary XML and resource table ---------------------------------

QString poolString(const Bytes &b, qint64 pool, quint32 index)
{
    const quint16 headerSize = b.u16(pool + 2);
    const quint32 chunkSize = b.u32(pool + 4);
    const quint32 count = b.u32(pool + 8);
    const quint32 flags = b.u32(pool + 16);
    const quint32 stringsStart = b.u32(pool + 20);
    if (index >= count || !b.has(pool, chunkSize))
        return {};
    const qint64 end = pool + chunkSize;
    qint64 s = pool + stringsStart + b.u32(pool + headerSize + qint64(index) * 4);
    if (s < pool || s >= end)
        return {};
    if (flags & 0x100) {
        // UTF-8: UTF-16 length, then byte length; each one or two bytes.
        s += (b.u8(s) & 0x80) ? 2 : 1;
        qint64 length = b.u8(s);
        if (length & 0x80) {
            length = ((length & 0x7f) << 8) | b.u8(s + 1);
            s += 2;
        } else {
            s += 1;
        }
        if (!b.has(s, length) || s + length > end)
            return {};
        return QString::fromUtf8(reinterpret_cast<const char *>(b.p + s), length);
    }
    qint64 length = b.u16(s);
    if (length & 0x8000) {
        length = ((length & 0x7fff) << 16) | b.u16(s + 2);
        s += 4;
    } else {
        s += 2;
    }
    if (!b.has(s, length * 2) || s + length * 2 > end)
        return {};
    QString out(length, Qt::Uninitialized);
    for (qint64 i = 0; i < length; ++i)
        out[i] = QChar(b.u16(s + 2 * i));
    return out;
}

enum : quint8 {
    TypeReference = 0x01,
    TypeAttribute = 0x02,
    TypeString = 0x03,
    TypeFloat = 0x04,
    TypeDimension = 0x05,
    TypeFraction = 0x06,
    TypeDynamicReference = 0x07,
    TypeIntDec = 0x10,
    TypeIntHex = 0x11,
    TypeIntBool = 0x12,
    TypeColorFirst = 0x1c,
    TypeColorLast = 0x1f,
};

bool isReference(quint8 type) { return type == TypeReference || type == TypeDynamicReference; }
bool isColor(quint8 type) { return type >= TypeColorFirst && type <= TypeColorLast; }

struct AxmlAttribute {
    quint32 resourceId = 0;
    QString name;
    quint8 type = 0;
    quint32 data = 0;
    QString string;
};

struct AxmlElement {
    QString name;
    int depth = 0;
    QList<AxmlAttribute> attributes;

    const AxmlAttribute *attribute(QLatin1StringView wanted, quint32 resourceId = 0) const
    {
        for (const AxmlAttribute &attribute : attributes) {
            if ((resourceId && attribute.resourceId == resourceId) || attribute.name == wanted)
                return &attribute;
        }
        return nullptr;
    }
};

bool isBinaryXml(const QByteArray &data)
{
    const Bytes b(data);
    return b.u16(0) == 0x0003 && b.u16(2) == 8;
}

QList<AxmlElement> parseBinaryXml(const QByteArray &data)
{
    QList<AxmlElement> elements;
    const Bytes b(data);
    if (!isBinaryXml(data))
        return elements;
    const qint64 end = qMin<qint64>(b.u32(4), b.n);
    qint64 pos = b.u16(2);
    qint64 pool = -1;
    QList<quint32> resourceMap;
    int depth = 0;
    while (pos + 8 <= end && elements.size() < 8192) {
        const quint16 type = b.u16(pos);
        const quint16 headerSize = b.u16(pos + 2);
        const quint32 size = b.u32(pos + 4);
        if (size < 8 || headerSize < 8 || headerSize > size || pos + size > end)
            break;
        if (type == 0x0001 && pool < 0) {
            pool = pos;
        } else if (type == 0x0180) {
            for (qint64 o = pos + headerSize; o + 4 <= pos + size && resourceMap.size() < 65536; o += 4)
                resourceMap.append(b.u32(o));
        } else if (type == 0x0102 && pool >= 0) {
            const qint64 ext = pos + headerSize;
            AxmlElement element;
            element.name = poolString(b, pool, b.u32(ext + 4));
            element.depth = depth++;
            const quint16 attributeStart = b.u16(ext + 8);
            const quint16 attributeSize = b.u16(ext + 10);
            const quint16 attributeCount = b.u16(ext + 12);
            if (attributeSize >= 20) {
                for (int i = 0; i < attributeCount; ++i) {
                    const qint64 a = ext + attributeStart + qint64(i) * attributeSize;
                    if (a + 20 > pos + size)
                        break;
                    AxmlAttribute attribute;
                    const quint32 nameIndex = b.u32(a + 4);
                    if (nameIndex < quint32(resourceMap.size()))
                        attribute.resourceId = resourceMap.at(nameIndex);
                    attribute.name = poolString(b, pool, nameIndex);
                    const quint32 raw = b.u32(a + 8);
                    attribute.type = b.u8(a + 15);
                    attribute.data = b.u32(a + 16);
                    if (attribute.type == TypeString)
                        attribute.string = poolString(b, pool, attribute.data);
                    else if (raw != 0xffffffff)
                        attribute.string = poolString(b, pool, raw);
                    element.attributes.append(attribute);
                }
            }
            elements.append(element);
        } else if (type == 0x0103) {
            depth = qMax(0, depth - 1);
        }
        pos += size;
    }
    return elements;
}

struct ResourceValue {
    int rank = 0;
    quint8 type = 0;
    quint32 data = 0;
    QString string;
};

// resources.arsc lookup: every configuration of one resource id, best first
// (adaptive "anydpi" XML, then the highest density bitmap). Localised and
// night-mode variants rank last.
class ResourceTable
{
public:
    explicit ResourceTable(QByteArray data) : m_data(std::move(data)) {}

    QList<ResourceValue> values(quint32 id) const
    {
        QList<ResourceValue> result;
        const Bytes b(m_data);
        if (b.u16(0) != 0x0002)
            return result;
        const quint32 packageId = id >> 24;
        const quint32 typeId = (id >> 16) & 0xff;
        const quint32 entryIndex = id & 0xffff;
        if (typeId == 0)
            return result;
        const qint64 end = qMin<qint64>(b.u32(4), b.n);
        qint64 pos = b.u16(2);
        qint64 globalPool = -1;
        while (pos + 8 <= end) {
            const quint16 type = b.u16(pos);
            const quint16 headerSize = b.u16(pos + 2);
            const quint32 size = b.u32(pos + 4);
            if (size < 8 || headerSize < 8 || headerSize > size || pos + size > end)
                break;
            if (type == 0x0001 && globalPool < 0) {
                globalPool = pos;
            } else if (type == 0x0200 && b.u32(pos + 8) == packageId) {
                const qint64 packageEnd = pos + size;
                qint64 inner = pos + headerSize;
                while (inner + 8 <= packageEnd) {
                    const quint16 innerType = b.u16(inner);
                    const quint16 innerHeader = b.u16(inner + 2);
                    const quint32 innerSize = b.u32(inner + 4);
                    if (innerSize < 8 || innerHeader < 8 || innerHeader > innerSize
                            || inner + innerSize > packageEnd)
                        break;
                    if (innerType == 0x0201 && b.u8(inner + 8) == typeId)
                        readEntry(b, inner, innerHeader, innerSize, entryIndex, globalPool, result);
                    inner += innerSize;
                }
            }
            pos += size;
        }
        std::stable_sort(result.begin(), result.end(), [](const ResourceValue &a, const ResourceValue &c) {
            return a.rank > c.rank;
        });
        return result;
    }

private:
    static void readEntry(const Bytes &b, qint64 chunk, quint16 headerSize, quint32 size,
                          quint32 entryIndex, qint64 globalPool, QList<ResourceValue> &out)
    {
        const qint64 chunkEnd = chunk + size;
        const quint8 flags = b.u8(chunk + 9);
        const quint32 entryCount = b.u32(chunk + 12);
        const quint32 entriesStart = b.u32(chunk + 16);
        const qint64 table = chunk + headerSize;
        qint64 offset = -1;
        if (flags & 0x01) {
            // Sparse: (index, offset / 4) pairs sorted by index.
            for (quint32 i = 0; i < entryCount && i < 65536; ++i) {
                const qint64 e = table + qint64(i) * 4;
                if (e + 4 > chunkEnd)
                    break;
                if (b.u16(e) == entryIndex) {
                    offset = qint64(b.u16(e + 2)) * 4;
                    break;
                }
            }
        } else if (entryIndex < entryCount) {
            if (flags & 0x02) {
                const quint16 value = b.u16(table + qint64(entryIndex) * 2);
                if (value != 0xffff)
                    offset = qint64(value) * 4;
            } else {
                const quint32 value = b.u32(table + qint64(entryIndex) * 4);
                if (value != 0xffffffff)
                    offset = value;
            }
        }
        if (offset < 0)
            return;
        const qint64 entry = chunk + entriesStart + offset;
        if (entry < chunk || entry + 8 > chunkEnd)
            return;
        const quint16 entrySize = b.u16(entry);
        const quint16 entryFlags = b.u16(entry + 2);
        ResourceValue value;
        if (entryFlags & 0x0008) {
            // Compact entry: key, flags (type in the high byte), data.
            value.type = quint8(entryFlags >> 8);
            value.data = b.u32(entry + 4);
        } else if (!(entryFlags & 0x0001) && entry + entrySize + 8 <= chunkEnd) {
            value.type = b.u8(entry + entrySize + 3);
            value.data = b.u32(entry + entrySize + 4);
        } else {
            return; // Styles and other bags are not drawables.
        }
        if (value.type == TypeString && globalPool >= 0)
            value.string = poolString(b, globalPool, value.data);

        // ResTable_config: size, imsi, locale, orientation/touch/density, …
        const qint64 config = chunk + 20;
        const quint32 configSize = b.u32(config);
        const quint16 density = configSize >= 16 ? b.u16(config + 14) : 0;
        const bool localized = configSize >= 12 && b.u16(config + 8) != 0;
        const bool night = configSize >= 30 && (b.u8(config + 29) & 0x30) == 0x20;
        value.rank = density == 0xfffe ? 100000 : density == 0xffff ? 90000 : density == 0 ? 159 : density;
        if (night)
            value.rank -= 200000;
        if (localized)
            value.rank -= 400000;
        // Same density: a current device uses the newest API-level variant
        // (older ones may have attributes stripped for old platforms).
        const quint16 sdk = configSize >= 26 ? b.u16(config + 24) : 0;
        value.rank = value.rank * 1000 + qMin<int>(sdk, 999);
        out.append(value);
    }

    QByteArray m_data;
};

float complexToFloat(quint32 data)
{
    static const float radix[] = {1.0f / (1 << 8), 1.0f / (1 << 15), 1.0f / (1 << 23), 1.0f / float(1u << 31)};
    return float(qint32(data & 0xffffff00)) * radix[(data >> 4) & 3];
}

QString svgNumber(double value)
{
    return QString::number(value, 'g', 7);
}

QString svgColor(const QColor &color)
{
    return color.name(QColor::HexRgb);
}

QString xmlEscape(QString text)
{
    return text.replace(QLatin1Char('&'), QLatin1String("&amp;"))
        .replace(QLatin1Char('<'), QLatin1String("&lt;"))
        .replace(QLatin1Char('>'), QLatin1String("&gt;"))
        .replace(QLatin1Char('"'), QLatin1String("&quot;"));
}

// Android framework attribute ids, for packages whose binary XML drops the
// attribute names (resource shrinkers and obfuscators may do that).
constexpr quint32 kAttrIcon = 0x01010002;
constexpr quint32 kAttrDrawable = 0x01010199;
constexpr quint32 kAttrRoundIcon = 0x0101052c;

struct Paint {
    QString value = QStringLiteral("none");
    qreal opacity = 1;
};

class AndroidDrawableRenderer
{
public:
    AndroidDrawableRenderer(const ZipReader &zip, const ResourceTable &table)
        : m_zip(zip), m_table(table) {}

    QImage render(quint32 id, int size, int depth)
    {
        if (depth > 8 || size <= 0)
            return {};
        const QList<ResourceValue> values = m_table.values(id);
        for (const ResourceValue &value : values) {
            QImage image = renderValue(value, size, depth);
            if (!image.isNull())
                return image;
        }
        return {};
    }

    QImage renderFile(const QString &path, int size, int depth)
    {
        if (depth > 8)
            return {};
        const QByteArray data = m_zip.read(path);
        if (data.isEmpty())
            return {};
        if (isBinaryXml(data))
            return renderXml(parseBinaryXml(data), size, depth);
        if (path.endsWith(QLatin1String(".9.png"), Qt::CaseInsensitive))
            return {};
        return fitInto(decodeImage(data, QSize(size, size)), size);
    }

private:
    QImage renderValue(const ResourceValue &value, int size, int depth)
    {
        if (isColor(value.type))
            return solid(QColor::fromRgba(value.data), size);
        if (isReference(value.type))
            return render(value.data, size, depth + 1);
        if (value.type == TypeString && !value.string.isEmpty())
            return renderFile(value.string, size, depth + 1);
        return {};
    }

    static QImage solid(const QColor &color, int size)
    {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(color);
        return image;
    }

    QImage renderAttribute(const AxmlAttribute *attribute, int size, int depth)
    {
        if (!attribute)
            return {};
        if (isColor(attribute->type))
            return solid(QColor::fromRgba(attribute->data), size);
        if (isReference(attribute->type))
            return render(attribute->data, size, depth + 1);
        return {};
    }

    // Children one level below the element at `index`.
    static QList<int> children(const QList<AxmlElement> &elements, int index)
    {
        QList<int> result;
        const int parentDepth = elements.at(index).depth;
        for (int i = index + 1; i < elements.size() && elements.at(i).depth > parentDepth; ++i) {
            if (elements.at(i).depth == parentDepth + 1)
                result.append(i);
        }
        return result;
    }

    QImage renderXml(const QList<AxmlElement> &elements, int size, int depth)
    {
        if (elements.isEmpty())
            return {};
        const AxmlElement &root = elements.first();
        const QString &name = root.name;
        if (name == QLatin1String("adaptive-icon"))
            return renderAdaptive(elements, size, depth);
        if (name == QLatin1String("vector"))
            return renderVector(elements, size, depth);
        if (name == QLatin1String("bitmap") || name == QLatin1String("nine-patch"))
            return renderAttribute(root.attribute(QLatin1StringView("src")), size, depth);
        if (name == QLatin1String("inset"))
            return renderInset(root, size, depth);
        if (name == QLatin1String("shape"))
            return renderShape(elements, size, depth);
        if (name == QLatin1String("layer-list")) {
            QImage canvas = solid(Qt::transparent, size);
            QPainter painter(&canvas);
            bool drawn = false;
            for (int child : children(elements, 0)) {
                const QImage layer = renderAttribute(
                    elements.at(child).attribute(QLatin1StringView("drawable"), kAttrDrawable), size, depth);
                if (!layer.isNull()) {
                    painter.drawImage(0, 0, layer);
                    drawn = true;
                }
            }
            return drawn ? canvas : QImage();
        }
        // Selectors, level/animation lists, animated vectors, clip/scale/
        // rotate wrappers: the first drawable they reference is the icon.
        for (const AxmlElement &element : elements) {
            const QImage image = renderAttribute(
                element.attribute(QLatin1StringView("drawable"), kAttrDrawable), size, depth);
            if (!image.isNull())
                return image;
        }
        return {};
    }

    QImage renderAdaptive(const QList<AxmlElement> &elements, int size, int depth)
    {
        // Layers are 108dp; launchers show the central 72dp under a mask.
        const int layerSize = size * 3 / 2;
        QImage canvas = solid(Qt::transparent, layerSize);
        bool drawn = false; // The foreground: a background alone is no icon.
        {
            QPainter painter(&canvas);
            for (const QLatin1StringView layerName : {QLatin1StringView("background"), QLatin1StringView("foreground")}) {
                for (int child : children(elements, 0)) {
                    if (elements.at(child).name != layerName)
                        continue;
                    const QImage layer = renderAttribute(
                        elements.at(child).attribute(QLatin1StringView("drawable"), kAttrDrawable), layerSize, depth);
                    if (!layer.isNull()) {
                        painter.drawImage(0, 0, layer);
                        drawn = layerName == QLatin1String("foreground");
                    }
                    break;
                }
            }
        }
        if (!drawn)
            return {};
        const int inset = (layerSize - size) / 2;
        const QImage visible = canvas.copy(inset, inset, size, size);
        QImage masked = solid(Qt::transparent, size);
        QPainter painter(&masked);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath mask;
        const qreal radius = size * 0.24;
        mask.addRoundedRect(QRectF(0, 0, size, size), radius, radius);
        painter.setClipPath(mask);
        painter.drawImage(0, 0, visible);
        return masked;
    }

    float number(const AxmlAttribute *attribute, float fallback, int depth = 0) const
    {
        if (!attribute)
            return fallback;
        switch (attribute->type) {
        case TypeFloat: {
            float value;
            std::memcpy(&value, &attribute->data, sizeof value);
            return std::isfinite(value) ? value : fallback;
        }
        case TypeDimension:
        case TypeFraction:
            return complexToFloat(attribute->data);
        case TypeIntDec:
        case TypeIntHex:
            return float(qint32(attribute->data));
        case TypeString: {
            bool ok = false;
            const float value = attribute->string.toFloat(&ok);
            return ok ? value : fallback;
        }
        case TypeReference:
        case TypeDynamicReference:
            if (depth < 4) {
                const QList<ResourceValue> values = m_table.values(attribute->data);
                if (!values.isEmpty()) {
                    AxmlAttribute resolved;
                    resolved.type = values.first().type;
                    resolved.data = values.first().data;
                    resolved.string = values.first().string;
                    return number(&resolved, fallback, depth + 1);
                }
            }
            return fallback;
        default:
            return fallback;
        }
    }

    // Fraction attributes (e.g. "16%") are relative to the drawable;
    // dimensions are treated as dp of a 108dp adaptive layer.
    float insetFraction(const AxmlAttribute *attribute) const
    {
        if (!attribute)
            return 0;
        if (attribute->type == TypeFraction)
            return complexToFloat(attribute->data);
        return number(attribute, 0) / 108.0f;
    }

    QImage renderInset(const AxmlElement &root, int size, int depth)
    {
        const float all = insetFraction(root.attribute(QLatin1StringView("inset")));
        const auto side = [&](const char *name) {
            const AxmlAttribute *attribute = root.attribute(QLatin1StringView(name));
            return qBound(0.0f, attribute ? insetFraction(attribute) : all, 0.45f);
        };
        const int left = qRound(size * side("insetLeft"));
        const int top = qRound(size * side("insetTop"));
        const int right = qRound(size * side("insetRight"));
        const int bottom = qRound(size * side("insetBottom"));
        const int inner = qMax(1, qMin(size - left - right, size - top - bottom));
        const QImage child = renderAttribute(
            root.attribute(QLatin1StringView("drawable"), kAttrDrawable), inner, depth);
        if (child.isNull())
            return {};
        QImage canvas = solid(Qt::transparent, size);
        QPainter painter(&canvas);
        painter.drawImage(left + (size - left - right - inner) / 2, top + (size - top - bottom - inner) / 2, child);
        return canvas;
    }

    QImage renderShape(const QList<AxmlElement> &elements, int size, int depth)
    {
        std::optional<QColor> fill;
        for (int child : children(elements, 0)) {
            if (elements.at(child).name == QLatin1String("solid"))
                fill = color(elements.at(child).attribute(QLatin1StringView("color")), depth);
        }
        if (!fill)
            return {};
        QImage canvas = solid(Qt::transparent, size);
        QPainter painter(&canvas);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(*fill);
        const AxmlAttribute *shape = elements.first().attribute(QLatin1StringView("shape"));
        if (shape && shape->data == 1)
            painter.drawEllipse(canvas.rect());
        else
            painter.drawRect(canvas.rect());
        return canvas;
    }

    std::optional<QColor> color(const AxmlAttribute *attribute, int depth) const
    {
        if (!attribute)
            return std::nullopt;
        if (isColor(attribute->type))
            return QColor::fromRgba(attribute->data);
        if (isReference(attribute->type))
            return colorResource(attribute->data, depth + 1);
        return std::nullopt;
    }

    std::optional<QColor> colorResource(quint32 id, int depth) const
    {
        if (depth > 6)
            return std::nullopt;
        for (const ResourceValue &value : m_table.values(id)) {
            if (isColor(value.type))
                return QColor::fromRgba(value.data);
            if (isReference(value.type))
                return colorResource(value.data, depth + 1);
            if (value.type == TypeString && value.string.endsWith(QLatin1String(".xml"))) {
                // Colour state list: the default (last) item wins in Android,
                // the first plain item is a close enough stand-in.
                const QList<AxmlElement> elements = parseBinaryXml(m_zip.read(value.string));
                if (elements.isEmpty() || elements.first().name != QLatin1String("selector"))
                    continue; // Gradients also have <item color>, but are not colours.
                for (const AxmlElement &element : elements) {
                    if (element.name != QLatin1String("item"))
                        continue;
                    std::optional<QColor> result = color(element.attribute(QLatin1StringView("color")), depth);
                    if (result) {
                        const AxmlAttribute *alpha = element.attribute(QLatin1StringView("alpha"));
                        if (alpha)
                            result->setAlphaF(result->alphaF() * qBound(0.0f, number(alpha, 1), 1.0f));
                        return result;
                    }
                }
            }
        }
        return std::nullopt;
    }

    QString gradientStops(const QList<AxmlElement> &elements, int depth) const
    {
        QString stops;
        for (int child : children(elements, 0)) {
            const AxmlElement &item = elements.at(child);
            if (item.name != QLatin1String("item"))
                continue;
            const std::optional<QColor> c = color(item.attribute(QLatin1StringView("color")), depth);
            if (!c)
                continue;
            stops += QStringLiteral("<stop offset=\"%1\" stop-color=\"%2\" stop-opacity=\"%3\"/>")
                         .arg(svgNumber(qBound(0.0f, number(item.attribute(QLatin1StringView("offset")), 0), 1.0f)),
                              svgColor(*c), svgNumber(c->alphaF()));
        }
        if (!stops.isEmpty())
            return stops;
        const AxmlElement &root = elements.first();
        const std::optional<QColor> start = color(root.attribute(QLatin1StringView("startColor")), depth);
        const std::optional<QColor> center = color(root.attribute(QLatin1StringView("centerColor")), depth);
        const std::optional<QColor> end = color(root.attribute(QLatin1StringView("endColor")), depth);
        const auto stop = [](double offset, const QColor &c) {
            return QStringLiteral("<stop offset=\"%1\" stop-color=\"%2\" stop-opacity=\"%3\"/>")
                .arg(svgNumber(offset), svgColor(c), svgNumber(c.alphaF()));
        };
        if (start)
            stops += stop(0, *start);
        if (center)
            stops += stop(0.5, *center);
        if (end)
            stops += stop(1, *end);
        return stops;
    }

    Paint paint(const AxmlAttribute *attribute, QString &defs, int &counter, int depth) const
    {
        Paint result;
        if (!attribute)
            return result;
        if (attribute->type == TypeAttribute) {
            // Theme attribute (?attr/colorOnSurface…): not resolvable outside
            // the device theme, use a neutral ink.
            result.value = QStringLiteral("#5f6368");
            return result;
        }
        if (const std::optional<QColor> c = color(attribute, depth)) {
            result.value = svgColor(*c);
            result.opacity = c->alphaF();
            return result;
        }
        if (!isReference(attribute->type))
            return result;
        for (const ResourceValue &value : m_table.values(attribute->data)) {
            if (value.type != TypeString || !value.string.endsWith(QLatin1String(".xml")))
                continue;
            const QList<AxmlElement> elements = parseBinaryXml(m_zip.read(value.string));
            if (elements.isEmpty() || elements.first().name != QLatin1String("gradient"))
                continue;
            const AxmlElement &g = elements.first();
            const QString stops = gradientStops(elements, depth + 1);
            if (stops.isEmpty())
                continue;
            const QString id = QStringLiteral("g%1").arg(++counter);
            const int type = int(number(g.attribute(QLatin1StringView("type")), 0));
            const int tile = int(number(g.attribute(QLatin1StringView("tileMode")), 0));
            const QString spread = tile == 2 ? QStringLiteral("repeat")
                : tile == 3 ? QStringLiteral("reflect") : QStringLiteral("pad");
            if (type == 1) {
                defs += QStringLiteral("<radialGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" cx=\"%2\" cy=\"%3\" r=\"%4\" spreadMethod=\"%5\">%6</radialGradient>")
                            .arg(id, svgNumber(number(g.attribute(QLatin1StringView("centerX")), 0)),
                                 svgNumber(number(g.attribute(QLatin1StringView("centerY")), 0)),
                                 svgNumber(number(g.attribute(QLatin1StringView("gradientRadius")), 1)),
                                 spread, stops);
            } else {
                // Sweep gradients have no SVG equivalent; a linear ramp
                // between the same colours keeps the icon recognisable.
                defs += QStringLiteral("<linearGradient id=\"%1\" gradientUnits=\"userSpaceOnUse\" x1=\"%2\" y1=\"%3\" x2=\"%4\" y2=\"%5\" spreadMethod=\"%6\">%7</linearGradient>")
                            .arg(id, svgNumber(number(g.attribute(QLatin1StringView("startX")), 0)),
                                 svgNumber(number(g.attribute(QLatin1StringView("startY")), 0)),
                                 svgNumber(number(g.attribute(QLatin1StringView("endX")), 0)),
                                 svgNumber(number(g.attribute(QLatin1StringView("endY")), 0)),
                                 spread, stops);
            }
            result.value = QStringLiteral("url(#%1)").arg(id);
            return result;
        }
        return result;
    }

    // VectorDrawable → SVG, rendered by QtSvg.
    QImage renderVector(const QList<AxmlElement> &elements, int size, int depth)
    {
        const AxmlElement &root = elements.first();
        const float viewportWidth = number(root.attribute(QLatin1StringView("viewportWidth")), 0);
        const float viewportHeight = number(root.attribute(QLatin1StringView("viewportHeight")), 0);
        if (!(viewportWidth > 0) || !(viewportHeight > 0))
            return {};
        const std::optional<QColor> tint = color(root.attribute(QLatin1StringView("tint")), depth);
        const float rootAlpha = qBound(0.0f, number(root.attribute(QLatin1StringView("alpha")), 1), 1.0f);

        QString defs;
        QString body;
        int counter = 0;
        struct Open { int depth; int closes; };
        QList<Open> open = {{root.depth, 0}};
        const auto closeTo = [&](int elementDepth) {
            while (open.size() > 1 && elementDepth <= open.last().depth) {
                body += QStringLiteral("</g>").repeated(open.last().closes);
                open.removeLast();
            }
        };
        for (int i = 1; i < elements.size(); ++i) {
            const AxmlElement &e = elements.at(i);
            closeTo(e.depth);
            if (e.name == QLatin1String("group")) {
                const float pivotX = number(e.attribute(QLatin1StringView("pivotX")), 0);
                const float pivotY = number(e.attribute(QLatin1StringView("pivotY")), 0);
                body += QStringLiteral("<g transform=\"translate(%1 %2) rotate(%3) scale(%4 %5) translate(%6 %7)\">")
                            .arg(svgNumber(number(e.attribute(QLatin1StringView("translateX")), 0) + pivotX),
                                 svgNumber(number(e.attribute(QLatin1StringView("translateY")), 0) + pivotY),
                                 svgNumber(number(e.attribute(QLatin1StringView("rotation")), 0)),
                                 svgNumber(number(e.attribute(QLatin1StringView("scaleX")), 1)),
                                 svgNumber(number(e.attribute(QLatin1StringView("scaleY")), 1)),
                                 svgNumber(-pivotX), svgNumber(-pivotY));
                open.append({e.depth, 1});
            } else if (e.name == QLatin1String("clip-path")) {
                const AxmlAttribute *data = e.attribute(QLatin1StringView("pathData"));
                if (!data || data->string.isEmpty())
                    continue;
                const QString id = QStringLiteral("c%1").arg(++counter);
                defs += QStringLiteral("<clipPath id=\"%1\"><path d=\"%2\"/></clipPath>").arg(id, xmlEscape(data->string));
                // Clips every later sibling inside the same group.
                body += QStringLiteral("<g clip-path=\"url(#%1)\">").arg(id);
                ++open.last().closes;
            } else if (e.name == QLatin1String("path")) {
                const AxmlAttribute *data = e.attribute(QLatin1StringView("pathData"));
                if (!data || data->string.isEmpty())
                    continue;
                Paint fill = paint(e.attribute(QLatin1StringView("fillColor")), defs, counter, depth);
                Paint stroke = paint(e.attribute(QLatin1StringView("strokeColor")), defs, counter, depth);
                if (tint) {
                    if (fill.value != QLatin1String("none")) {
                        fill.value = svgColor(*tint);
                        fill.opacity *= tint->alphaF();
                    }
                    if (stroke.value != QLatin1String("none")) {
                        stroke.value = svgColor(*tint);
                        stroke.opacity *= tint->alphaF();
                    }
                }
                fill.opacity *= qBound(0.0f, number(e.attribute(QLatin1StringView("fillAlpha")), 1), 1.0f);
                stroke.opacity *= qBound(0.0f, number(e.attribute(QLatin1StringView("strokeAlpha")), 1), 1.0f);
                QString path = QStringLiteral("<path d=\"%1\" fill=\"%2\" fill-opacity=\"%3\"")
                                   .arg(xmlEscape(data->string), fill.value, svgNumber(fill.opacity));
                const AxmlAttribute *fillType = e.attribute(QLatin1StringView("fillType"));
                if (fillType && fillType->data == 1)
                    path += QStringLiteral(" fill-rule=\"evenodd\"");
                const float strokeWidth = number(e.attribute(QLatin1StringView("strokeWidth")), 0);
                if (stroke.value != QLatin1String("none") && strokeWidth > 0) {
                    static const char *caps[] = {"butt", "round", "square"};
                    static const char *joins[] = {"miter", "round", "bevel"};
                    const AxmlAttribute *cap = e.attribute(QLatin1StringView("strokeLineCap"));
                    const AxmlAttribute *join = e.attribute(QLatin1StringView("strokeLineJoin"));
                    path += QStringLiteral(" stroke=\"%1\" stroke-opacity=\"%2\" stroke-width=\"%3\" stroke-linecap=\"%4\" stroke-linejoin=\"%5\" stroke-miterlimit=\"%6\"")
                                .arg(stroke.value, svgNumber(stroke.opacity), svgNumber(strokeWidth),
                                     QLatin1String(caps[cap && cap->data < 3 ? cap->data : 0]),
                                     QLatin1String(joins[join && join->data < 3 ? join->data : 0]),
                                     svgNumber(number(e.attribute(QLatin1StringView("strokeMiterLimit")), 4)));
                }
                body += path + QStringLiteral("/>");
            }
        }
        closeTo(root.depth);
        body += QStringLiteral("</g>").repeated(open.first().closes);

        const QString svg = QStringLiteral(
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%1\" height=\"%1\" viewBox=\"0 0 %2 %3\" preserveAspectRatio=\"none\">"
            "<defs>%4</defs><g opacity=\"%5\">%6</g></svg>")
                                .arg(QString::number(size), svgNumber(viewportWidth), svgNumber(viewportHeight),
                                     defs, svgNumber(rootAlpha), body);
        QImage image = renderSvg(svg.toUtf8(), QSize(size, size));
        if (!image.isNull() && image.size() != QSize(size, size))
            image = fitInto(image, size);
        return image;
    }

    const ZipReader &m_zip;
    const ResourceTable &m_table;
};

int densityRank(const QString &path)
{
    static const QList<std::pair<QLatin1StringView, int>> densities = {
        {QLatin1StringView("xxxhdpi"), 640}, {QLatin1StringView("xxhdpi"), 480},
        {QLatin1StringView("xhdpi"), 320}, {QLatin1StringView("hdpi"), 240},
        {QLatin1StringView("mdpi"), 160}, {QLatin1StringView("ldpi"), 120}};
    for (const auto &[name, value] : densities) {
        if (path.contains(name))
            return value;
    }
    return 100;
}

QImage iconFromApk(const ZipReader &apk)
{
    constexpr int size = 512;
    const QList<AxmlElement> manifest = parseBinaryXml(apk.read(QStringLiteral("AndroidManifest.xml")));
    quint32 icon = 0;
    quint32 roundIcon = 0;
    for (const AxmlElement &element : manifest) {
        if (element.name != QLatin1String("application"))
            continue;
        if (const AxmlAttribute *a = element.attribute(QLatin1StringView("icon"), kAttrIcon); a && isReference(a->type))
            icon = a->data;
        if (const AxmlAttribute *a = element.attribute(QLatin1StringView("roundIcon"), kAttrRoundIcon); a && isReference(a->type))
            roundIcon = a->data;
        break;
    }

    const ResourceTable table(apk.read(QStringLiteral("resources.arsc"), 128 * 1024 * 1024));
    AndroidDrawableRenderer renderer(apk, table);
    for (const quint32 id : {icon, roundIcon}) {
        if (!id)
            continue;
        const QImage image = renderer.render(id, size, 0);
        if (!image.isNull())
            return image;
    }

    // No (renderable) manifest icon: the conventional launcher bitmaps.
    QStringList candidates;
    const QStringList files = apk.names();
    for (const QString &file : files) {
        if (!file.startsWith(QLatin1String("res/")))
            continue;
        const QString name = QFileInfo(file).fileName().toLower();
        if ((name == QLatin1String("ic_launcher.png") || name == QLatin1String("ic_launcher.webp")
                || name == QLatin1String("ic_launcher_round.png") || name == QLatin1String("ic_launcher_round.webp")
                || name == QLatin1String("icon.png") || name == QLatin1String("app_icon.png"))
                && !file.contains(QLatin1String("night")))
            candidates.append(file);
    }
    std::stable_sort(candidates.begin(), candidates.end(), [](const QString &a, const QString &b) {
        return densityRank(a) > densityRank(b);
    });
    for (const QString &candidate : std::as_const(candidates)) {
        const QImage image = decodeImage(apk.read(candidate));
        if (!image.isNull())
            return image;
    }
    return {};
}

// ---- Icon theme lookup for desktop launchers -------------------------------

struct ThemeDirectory {
    QString path;
    int rank = 0;
};

struct IconThemeState {
    QMutex mutex;
    bool captured = false;
    QString theme;
    QStringList searchPaths;
    QHash<QString, QList<ThemeDirectory>> directories; // theme → existing dirs, best first
    QHash<QString, QStringList> inherits;
    QHash<QString, QString> resolved;
};

IconThemeState &iconThemeState()
{
    static IconThemeState state;
    return state;
}

QHash<QString, QHash<QString, QString>> readIni(const QString &path)
{
    QHash<QString, QHash<QString, QString>> groups;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 4 * 1024 * 1024)
        return groups;
    QString group;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            group = line.mid(1, line.size() - 2);
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals > 0)
            groups[group].insert(line.left(equals).trimmed(), line.mid(equals + 1).trimmed());
    }
    return groups;
}

QStringList iconSearchPaths(const IconThemeState &state)
{
    QStringList paths = state.searchPaths;
    paths.append(QDir::homePath() + QStringLiteral("/.icons"));
    for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        paths.append(data + QStringLiteral("/icons"));
    paths.removeDuplicates();
    return paths;
}

// Requires state.mutex.
void loadTheme(IconThemeState &state, const QString &theme)
{
    if (state.directories.contains(theme))
        return;
    QList<ThemeDirectory> directories;
    QStringList inherits;
    bool foundIndex = false;
    for (const QString &base : iconSearchPaths(state)) {
        const QString themeRoot = base + QLatin1Char('/') + theme;
        const QString index = themeRoot + QStringLiteral("/index.theme");
        if (!QFileInfo::exists(index))
            continue;
        const auto ini = readIni(index);
        const QHash<QString, QString> header = ini.value(QStringLiteral("Icon Theme"));
        if (!foundIndex) {
            inherits = header.value(QStringLiteral("Inherits")).split(QLatin1Char(','), Qt::SkipEmptyParts);
            foundIndex = true;
        }
        QStringList names = header.value(QStringLiteral("Directories")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        names += header.value(QStringLiteral("ScaledDirectories")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        for (QString name : std::as_const(names)) {
            name = name.trimmed();
            const QString path = themeRoot + QLatin1Char('/') + name;
            if (!QFileInfo(path).isDir())
                continue;
            const QHash<QString, QString> info = ini.value(name);
            const int dirSize = info.value(QStringLiteral("Size")).toInt();
            const int scale = qMax(1, info.value(QStringLiteral("Scale"), QStringLiteral("1")).toInt());
            const bool scalable = info.value(QStringLiteral("Type")).compare(QLatin1String("Scalable"), Qt::CaseInsensitive) == 0;
            int rank = scalable ? 100000 + qMax(dirSize, info.value(QStringLiteral("MaxSize")).toInt())
                                : dirSize * scale;
            if (name.contains(QLatin1String("symbolic")))
                rank -= 200000;
            directories.append({path, rank});
        }
    }
    std::stable_sort(directories.begin(), directories.end(), [](const ThemeDirectory &a, const ThemeDirectory &b) {
        return a.rank > b.rank;
    });
    state.directories.insert(theme, directories);
    for (QString &parent : inherits)
        parent = parent.trimmed();
    state.inherits.insert(theme, inherits);
}

QString resolveThemeIcon(const QString &name)
{
    IconThemeState &state = iconThemeState();
    QMutexLocker locker(&state.mutex);
    if (!state.captured) {
        // Not captured from the GUI thread (tests): KDE's configured theme.
        const auto kdeglobals = readIni(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                        + QStringLiteral("/kdeglobals"));
        state.theme = kdeglobals.value(QStringLiteral("Icons")).value(QStringLiteral("Theme"));
        state.captured = true;
    }
    const auto cached = state.resolved.constFind(name);
    if (cached != state.resolved.cend())
        return cached.value();

    static const QStringList extensions = {QStringLiteral(".svg"), QStringLiteral(".png"),
                                           QStringLiteral(".svgz"), QStringLiteral(".xpm")};
    QString found;
    QStringList chain;
    QStringList queue = {state.theme.isEmpty() ? QStringLiteral("breeze") : state.theme};
    while (!queue.isEmpty() && chain.size() < 12) {
        const QString theme = queue.takeFirst();
        if (theme.isEmpty() || chain.contains(theme) || theme == QLatin1String("hicolor"))
            continue;
        chain.append(theme);
        loadTheme(state, theme);
        queue += state.inherits.value(theme);
    }
    chain.append(QStringLiteral("hicolor"));
    loadTheme(state, QStringLiteral("hicolor"));

    for (const QString &theme : std::as_const(chain)) {
        for (const ThemeDirectory &directory : state.directories.value(theme)) {
            for (const QString &extension : extensions) {
                const QString candidate = directory.path + QLatin1Char('/') + name + extension;
                if (QFileInfo::exists(candidate)) {
                    found = candidate;
                    break;
                }
            }
            if (!found.isEmpty())
                break;
        }
        if (!found.isEmpty())
            break;
    }
    if (found.isEmpty()) {
        QStringList pixmaps;
        for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
            pixmaps.append(data + QStringLiteral("/pixmaps"));
        for (const QString &directory : std::as_const(pixmaps)) {
            for (const QString &extension : extensions) {
                if (QFileInfo::exists(directory + QLatin1Char('/') + name + extension)) {
                    found = directory + QLatin1Char('/') + name + extension;
                    break;
                }
            }
            if (!found.isEmpty())
                break;
        }
    }
    if (state.resolved.size() > 4096)
        state.resolved.clear();
    state.resolved.insert(name, found);
    return found;
}

QImage loadIconFile(const QString &path, int size)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("svg") || suffix == QLatin1String("svgz")) {
        std::unique_ptr<QIODevice> input;
        if (suffix == QLatin1String("svgz"))
            input = std::make_unique<KCompressionDevice>(path, KCompressionDevice::GZip);
        else
            input = std::make_unique<QFile>(path);
        if (!input->open(QIODevice::ReadOnly))
            return {};
        const QByteArray data = input->read(8 * 1024 * 1024 + 1);
        return renderSvg(data, QSize(size, size));
    }
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize source = reader.size();
    if (source.isValid() && (source.width() > size || source.height() > size))
        reader.setScaledSize(source.scaled(QSize(size, size), Qt::KeepAspectRatio));
    return reader.read();
}

} // namespace

namespace ThumbnailExtractors {

void captureIconTheme()
{
    IconThemeState &state = iconThemeState();
    QMutexLocker locker(&state.mutex);
    state.theme = QIcon::themeName();
    state.searchPaths = QIcon::themeSearchPaths();
    state.captured = true;
    state.directories.clear();
    state.inherits.clear();
    state.resolved.clear();
}

QImage androidPackageIcon(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    ZipReader zip(&file, 0, file.size());
    if (!zip.open())
        return {};
    if (zip.contains(QStringLiteral("AndroidManifest.xml")))
        return iconFromApk(zip);

    // Split-APK bundles. XAPK ships the launcher icon next to the splits;
    // APKS/APKM keep it inside the base split.
    const QImage bundled = decodeImage(zip.read(QStringLiteral("icon.png")));
    if (!bundled.isNull())
        return bundled;
    QString base;
    for (const QString &name : zip.names()) {
        const QString lower = name.toLower();
        if (lower.contains(QLatin1Char('/')) || !lower.endsWith(QLatin1String(".apk")))
            continue;
        if (lower == QLatin1String("base.apk")) {
            base = name;
            break;
        }
        if (base.isEmpty() && !lower.startsWith(QLatin1String("config."))
                && !lower.startsWith(QLatin1String("split_")))
            base = name;
    }
    if (base.isEmpty())
        return {};
    qint64 storedSize = 0;
    const qint64 stored = zip.storedOffset(base, &storedSize);
    if (stored >= 0) {
        ZipReader nested(&file, stored, storedSize);
        return nested.open() ? iconFromApk(nested) : QImage();
    }
    QByteArray inner = zip.read(base, 256 * 1024 * 1024);
    QBuffer buffer(&inner);
    if (inner.isEmpty() || !buffer.open(QIODevice::ReadOnly))
        return {};
    ZipReader nested(&buffer, 0, inner.size());
    return nested.open() ? iconFromApk(nested) : QImage();
}

QImage windowsExecutableIcon(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 512 || file.size() > qint64(4) * 1024 * 1024 * 1024)
        return {};
    uchar *mapped = file.map(0, file.size());
    if (!mapped)
        return {};
    const Bytes b(mapped, file.size());
    QImage result;
    [&] {
        if (b.u16(0) != 0x5a4d) // "MZ"
            return;
        const qint64 pe = b.u32(0x3c);
        if (b.u32(pe) != 0x00004550) // "PE\0\0"
            return;
        const quint16 sectionCount = b.u16(pe + 6);
        const quint16 optionalSize = b.u16(pe + 20);
        const qint64 optional = pe + 24;
        const quint16 magic = b.u16(optional);
        if (magic != 0x10b && magic != 0x20b)
            return;
        const bool pe32Plus = magic == 0x20b;
        if (b.u32(optional + (pe32Plus ? 108 : 92)) < 3)
            return;
        const quint32 resourceRva = b.u32(optional + (pe32Plus ? 112 : 96) + 16);
        if (!resourceRva)
            return;
        const qint64 sections = optional + optionalSize;
        const auto fileOffset = [&](quint32 rva) -> qint64 {
            for (int i = 0; i < sectionCount && i < 96; ++i) {
                const qint64 s = sections + qint64(i) * 40;
                const quint32 address = b.u32(s + 12);
                const quint32 rawSize = b.u32(s + 16);
                const quint32 span = qMax(b.u32(s + 8), rawSize);
                if (rva >= address && rva - address < span) {
                    const quint32 offset = rva - address;
                    return offset < rawSize ? qint64(b.u32(s + 20)) + offset : -1;
                }
            }
            return -1;
        };
        const qint64 resources = fileOffset(resourceRva);
        if (resources < 0)
            return;
        using Entry = std::pair<quint32, quint32>;
        const auto entries = [&](qint64 directory) {
            QList<Entry> list;
            const int count = b.u16(directory + 12) + b.u16(directory + 14);
            for (int i = 0; i < count && i < 4096; ++i) {
                const qint64 e = directory + 16 + qint64(i) * 8;
                if (!b.has(e, 8))
                    break;
                list.append({b.u32(e), b.u32(e + 4)});
            }
            return list;
        };
        const auto subdirectory = [&](quint32 type) -> qint64 {
            for (const Entry &entry : entries(resources)) {
                if (entry.first == type && (entry.second & 0x80000000))
                    return resources + (entry.second & 0x7fffffff);
            }
            return -1;
        };
        // Follows the first language (and any further levels) to a data entry.
        const auto leaf = [&](quint32 offset) -> QByteArray {
            for (int level = 0; level < 4 && (offset & 0x80000000); ++level) {
                const QList<Entry> list = entries(resources + (offset & 0x7fffffff));
                if (list.isEmpty())
                    return {};
                offset = list.first().second;
            }
            if (offset & 0x80000000)
                return {};
            const qint64 data = resources + offset;
            const qint64 location = fileOffset(b.u32(data));
            const quint32 size = b.u32(data + 4);
            if (location < 0 || size == 0 || size > 64 * 1024 * 1024)
                return {};
            return b.copy(location, size);
        };

        const qint64 groups = subdirectory(14); // RT_GROUP_ICON
        const qint64 icons = subdirectory(3);   // RT_ICON
        if (groups < 0 || icons < 0)
            return;
        const QList<Entry> groupList = entries(groups);
        if (groupList.isEmpty())
            return;
        // Windows shows the first icon group as the program icon.
        const QByteArray group = leaf(groupList.first().second);
        const Bytes g(group);
        struct Candidate { int width; int height; int colors; int planes; int bits; quint16 id; };
        QList<Candidate> candidates;
        const int count = g.u16(4);
        for (int i = 0; i < count && i < 256; ++i) {
            const qint64 e = 6 + qint64(i) * 14;
            if (!g.has(e, 14))
                break;
            candidates.append({g.u8(e) ? g.u8(e) : 256, g.u8(e + 1) ? g.u8(e + 1) : 256, g.u8(e + 2),
                               g.u16(e + 4), g.u16(e + 6), g.u16(e + 12)});
        }
        std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &c) {
            return a.width != c.width ? a.width > c.width : a.bits > c.bits;
        });
        const QList<Entry> iconList = entries(icons);
        for (const Candidate &candidate : std::as_const(candidates)) {
            QByteArray data;
            for (const Entry &entry : iconList) {
                if (entry.first == candidate.id) {
                    data = leaf(entry.second);
                    break;
                }
            }
            if (data.isEmpty())
                continue;
            QImage image;
            if (data.startsWith("\x89PNG")) {
                image = decodeImage(data);
            } else {
                // A bare DIB: wrap it in a one-image .ico for Qt's decoder.
                QByteArray ico;
                ico.reserve(22 + data.size());
                const auto put16 = [&ico](quint16 v) { ico.append(char(v & 0xff)).append(char(v >> 8)); };
                const auto put32 = [&](quint32 v) { put16(quint16(v & 0xffff)); put16(quint16(v >> 16)); };
                put16(0); put16(1); put16(1);
                ico.append(char(candidate.width >= 256 ? 0 : candidate.width));
                ico.append(char(candidate.height >= 256 ? 0 : candidate.height));
                ico.append(char(candidate.colors)).append(char(0));
                put16(quint16(candidate.planes)); put16(quint16(candidate.bits));
                put32(quint32(data.size())); put32(22);
                ico.append(data);
                image = decodeImage(ico, QSize(512, 512), QByteArrayLiteral("ico"));
            }
            if (!image.isNull()) {
                result = image;
                return;
            }
        }
    }();
    file.unmap(mapped);
    return result;
}

QImage desktopEntryIcon(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text) || file.size() > 1024 * 1024)
        return {};
    QString icon;
    bool inEntry = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            if (inEntry)
                break;
            inEntry = line == QLatin1String("[Desktop Entry]");
            continue;
        }
        if (inEntry && line.startsWith(QLatin1String("Icon"))) {
            const qsizetype equals = line.indexOf(QLatin1Char('='));
            if (equals > 0 && line.left(equals).trimmed() == QLatin1String("Icon")) {
                icon = line.mid(equals + 1).trimmed();
                break;
            }
        }
    }
    if (icon.isEmpty())
        return {};
    constexpr int size = 256;
    if (icon.startsWith(QLatin1Char('/')))
        return QFileInfo(icon).isFile() ? loadIconFile(icon, size) : QImage();
    if (icon.contains(QLatin1Char('/')))
        return {};
    // "name.png" without a path is common in hand-written launchers.
    static const QStringList knownExtensions = {QStringLiteral(".png"), QStringLiteral(".svg"),
                                                QStringLiteral(".svgz"), QStringLiteral(".xpm")};
    for (const QString &extension : knownExtensions) {
        if (icon.endsWith(extension, Qt::CaseInsensitive)) {
            icon.chop(extension.size());
            break;
        }
    }
    const QString file2 = resolveThemeIcon(icon);
    return file2.isEmpty() ? QImage() : loadIconFile(file2, size);
}

QImage blenderThumbnail(const QString &path)
{
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly))
        return {};
    const QByteArray magic = probe.peek(4);
    probe.close();
    std::unique_ptr<QIODevice> device;
    if (magic.startsWith("\x1f\x8b"))
        device = std::make_unique<KCompressionDevice>(path, KCompressionDevice::GZip);
    else if (magic == QByteArray("\x28\xb5\x2f\xfd", 4))
        device = std::make_unique<KCompressionDevice>(path, KCompressionDevice::Zstd);
    else
        device = std::make_unique<QFile>(path);
    if (!device->open(QIODevice::ReadOnly))
        return {};

    const QByteArray header = device->read(12);
    if (header.size() < 12 || !header.startsWith("BLENDER"))
        return {};
    int pointerSize = 4;
    bool little = true;
    bool large = false;
    if (std::isdigit(uchar(header.at(7))) && std::isdigit(uchar(header.at(8)))) {
        // Blender 5 header: "BLENDER" + header size + "-" + format + "v" + version.
        const int headerSize = header.mid(7, 2).toInt();
        if (headerSize < 13 || headerSize > 64)
            return {};
        const QByteArray rest = device->read(headerSize - 12);
        if (rest.size() != headerSize - 12)
            return {};
        pointerSize = 8;
        little = rest.at(0) == 'v';
        large = true;
    } else {
        pointerSize = header.at(7) == '-' ? 8 : 4;
        little = header.at(8) == 'v';
    }
    const auto i32 = [little](const char *p) {
        return little ? qFromLittleEndian<qint32>(p) : qFromBigEndian<qint32>(p);
    };
    const auto i64 = [little](const char *p) {
        return little ? qFromLittleEndian<qint64>(p) : qFromBigEndian<qint64>(p);
    };
    const int blockHeader = large ? 32 : 16 + pointerSize;
    qint64 scanned = 0;
    for (int block = 0; block < 100000 && scanned < qint64(256) * 1024 * 1024; ++block) {
        const QByteArray bh = device->read(blockHeader);
        if (bh.size() != blockHeader)
            return {};
        const QByteArray code = bh.left(4);
        const qint64 length = large ? i64(bh.constData() + 16) : i32(bh.constData() + 4);
        if (length < 0)
            return {};
        if (code == "TEST") {
            if (length < 8 || length > 8 + 1024 * 1024 * 4)
                return {};
            const QByteArray data = device->read(length);
            if (data.size() != length)
                return {};
            const qint32 width = i32(data.constData());
            const qint32 height = i32(data.constData() + 4);
            if (width <= 0 || height <= 0 || width > 1024 || height > 1024
                    || qint64(width) * height * 4 + 8 > length)
                return {};
            QImage image(width, height, QImage::Format_RGBA8888);
            const qint64 stride = qint64(width) * 4;
            for (int y = 0; y < height; ++y) // Stored bottom-up.
                std::memcpy(image.scanLine(height - 1 - y), data.constData() + 8 + y * stride, stride);
            return image;
        }
        // Blender writes the thumbnail right after the render info and
        // before the global block; a file without one ends the search here.
        if (code == "GLOB" || code == "ENDB" || code == "DNA1")
            return {};
        if (device->skip(length) != length)
            return {};
        scanned += length;
    }
    return {};
}

QImage threeMfThumbnail(const QString &path)
{
    KZip zip(path);
    if (!zip.open(QIODevice::ReadOnly))
        return {};
    const KArchiveDirectory *root = zip.directory();
    QStringList names;
    // The package relationship names the thumbnail part.
    QXmlStreamReader rels(archiveEntry(root, QStringLiteral("_rels/.rels"), 1024 * 1024));
    while (!rels.atEnd()) {
        if (rels.readNext() == QXmlStreamReader::StartElement && rels.name() == QLatin1String("Relationship")
                && rels.attributes().value(QLatin1String("Type")).endsWith(QLatin1String("/thumbnail"))) {
            QString target = rels.attributes().value(QLatin1String("Target")).toString();
            while (target.startsWith(QLatin1Char('/')))
                target.remove(0, 1);
            names.append(target);
        }
    }
    names += {QStringLiteral("Metadata/thumbnail.png"), QStringLiteral("Metadata/thumbnail.jpg"),
              QStringLiteral("Metadata/plate_1.png"), QStringLiteral("Auxiliaries/.thumbnails/thumbnail_3mf.png"),
              QStringLiteral("Auxiliaries/.thumbnails/thumbnail_middle.png")};
    for (const QString &name : std::as_const(names)) {
        const QImage image = decodeImage(archiveEntry(root, name));
        if (!image.isNull())
            return image;
    }
    return {};
}

QImage fictionBookCover(const QString &path)
{
    std::unique_ptr<QIODevice> device;
    std::unique_ptr<KZip> zip;
    QFile probe(path);
    if (!probe.open(QIODevice::ReadOnly))
        return {};
    const bool zipped = probe.peek(2) == "PK";
    probe.close();
    if (zipped) {
        zip = std::make_unique<KZip>(path);
        if (!zip->open(QIODevice::ReadOnly))
            return {};
        QStringList files;
        listArchive(zip->directory(), QString(), files);
        for (const QString &name : std::as_const(files)) {
            if (!name.endsWith(QLatin1String(".fb2"), Qt::CaseInsensitive))
                continue;
            const auto *entry = static_cast<const KArchiveFile *>(zip->directory()->entry(name));
            if (entry && entry->size() > 0 && entry->size() <= qint64(256) * 1024 * 1024)
                device.reset(entry->createDevice());
            break;
        }
    } else {
        device = std::make_unique<QFile>(path);
    }
    if (!device || (!device->isOpen() && !device->open(QIODevice::ReadOnly)))
        return {};

    QXmlStreamReader xml(device.get());
    QString coverId;
    bool inCoverPage = false;
    QByteArray firstImage;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::EndElement && xml.name() == QLatin1String("coverpage")) {
            inCoverPage = false;
        } else if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("coverpage")) {
                inCoverPage = true;
            } else if (inCoverPage && coverId.isEmpty() && xml.name() == QLatin1String("image")) {
                for (const QXmlStreamAttribute &attribute : xml.attributes()) {
                    if (attribute.name() == QLatin1String("href")) {
                        coverId = attribute.value().toString();
                        if (coverId.startsWith(QLatin1Char('#')))
                            coverId.remove(0, 1);
                    }
                }
            } else if (xml.name() == QLatin1String("binary")) {
                const QString id = xml.attributes().value(QLatin1String("id")).toString();
                const bool image = xml.attributes().value(QLatin1String("content-type")).startsWith(QLatin1String("image/"));
                const bool wanted = !coverId.isEmpty() && id == coverId;
                if (!wanted && (!firstImage.isEmpty() || !image || !coverId.isEmpty())) {
                    xml.skipCurrentElement();
                    continue;
                }
                const QByteArray data = QByteArray::fromBase64(xml.readElementText().toLatin1());
                if (wanted)
                    return decodeImage(data);
                firstImage = data;
            }
        }
    }
    return decodeImage(firstImage);
}

QImage mobipocketCover(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() < 128 || file.size() > qint64(2) * 1024 * 1024 * 1024)
        return {};
    uchar *mapped = file.map(0, file.size());
    if (!mapped)
        return {};
    const Bytes b(mapped, file.size());
    QImage result;
    [&] {
        if (std::memcmp(b.p + 60, "BOOKMOBI", 8) != 0)
            return;
        const quint16 records = b.be16(76);
        const auto recordOffset = [&](quint32 index) -> qint64 {
            return index < records ? qint64(b.be32(78 + qint64(index) * 8)) : -1;
        };
        const qint64 record0 = recordOffset(0);
        if (record0 < 0 || b.copy(record0 + 16, 4) != "MOBI")
            return;
        const quint32 mobiLength = b.be32(record0 + 20);
        const quint32 firstImage = b.be32(record0 + 0x6c);
        if (firstImage == 0xffffffff || firstImage >= records)
            return;
        QList<quint32> offsets;
        if (b.be32(record0 + 0x80) & 0x40) {
            const qint64 exth = record0 + 16 + mobiLength;
            if (b.copy(exth, 4) == "EXTH") {
                const quint32 count = b.be32(exth + 8);
                qint64 pos = exth + 12;
                for (quint32 i = 0; i < count && i < 4096; ++i) {
                    const quint32 type = b.be32(pos);
                    const quint32 length = b.be32(pos + 4);
                    if (length < 8 || !b.has(pos, length))
                        break;
                    if ((type == 201 || type == 202) && length >= 12) {
                        const quint32 value = b.be32(pos + 8);
                        if (value != 0xffffffff) {
                            if (type == 201)
                                offsets.prepend(value); // Cover before thumbnail.
                            else
                                offsets.append(value);
                        }
                    }
                    pos += length;
                }
            }
        }
        offsets.append(0);
        for (const quint32 offset : std::as_const(offsets)) {
            const quint32 index = firstImage + offset;
            const qint64 start = recordOffset(index);
            const qint64 end = index + 1 < records ? recordOffset(index + 1) : b.n;
            if (start < 0 || end <= start || !b.has(start, end - start) || end - start > kMaxEntryBytes)
                continue;
            const QImage image = decodeImage(b.copy(start, end - start));
            if (!image.isNull()) {
                result = image;
                return;
            }
        }
    }();
    file.unmap(mapped);
    return result;
}

} // namespace ThumbnailExtractors
