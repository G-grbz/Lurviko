#include "checksumcalculator.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QUrl>
#include <QtConcurrent/QtConcurrentRun>

#include <array>
#include <optional>

#ifdef Q_OS_LINUX
#include <fcntl.h>
#endif

namespace {

QString message(const char *en, const char *tr)
{
    const auto fallback = QLocale::system().language() == QLocale::Turkish ? "tr" : "en";
    return QString::fromUtf8(QSettings().value("ui/language", fallback).toString() == "tr" ? tr : en);
}

std::optional<QCryptographicHash::Algorithm> cryptoAlgorithm(const QString &name)
{
    if (name == QLatin1String("MD5")) return QCryptographicHash::Md5;
    if (name == QLatin1String("SHA1")) return QCryptographicHash::Sha1;
    if (name == QLatin1String("SHA224")) return QCryptographicHash::Sha224;
    if (name == QLatin1String("SHA256")) return QCryptographicHash::Sha256;
    if (name == QLatin1String("SHA384")) return QCryptographicHash::Sha384;
    if (name == QLatin1String("SHA512")) return QCryptographicHash::Sha512;
    if (name == QLatin1String("SHA3-256")) return QCryptographicHash::Sha3_256;
    if (name == QLatin1String("SHA3-512")) return QCryptographicHash::Sha3_512;
    if (name == QLatin1String("BLAKE2b-512")) return QCryptographicHash::Blake2b_512;
    if (name == QLatin1String("BLAKE2s-256")) return QCryptographicHash::Blake2s_256;
    return std::nullopt;
}

// zlib-compatible CRC-32 (the value of .sfv files and ZIP entries).
struct Crc32 {
    std::array<quint32, 256> table {};
    quint32 value = 0xFFFFFFFFu;
    Crc32()
    {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
    }
    void add(const char *data, qsizetype size)
    {
        for (qsizetype i = 0; i < size; ++i)
            value = table[(value ^ quint8(data[i])) & 0xFFu] ^ (value >> 8);
    }
    QByteArray hex() const { return QByteArray::number(value ^ 0xFFFFFFFFu, 16).rightJustified(8, '0'); }
};

struct Outcome {
    QVariantMap results;
    QString error;
    bool cancelled = false;
};

QString algorithmFromName(QString name)
{
    name = name.toUpper().remove('-').remove('_');
    if (name == QLatin1String("MD5")) return QStringLiteral("MD5");
    if (name == QLatin1String("SHA1")) return QStringLiteral("SHA1");
    if (name == QLatin1String("SHA224")) return QStringLiteral("SHA224");
    if (name == QLatin1String("SHA256")) return QStringLiteral("SHA256");
    if (name == QLatin1String("SHA384")) return QStringLiteral("SHA384");
    if (name == QLatin1String("SHA512")) return QStringLiteral("SHA512");
    if (name == QLatin1String("SHA3256")) return QStringLiteral("SHA3-256");
    if (name == QLatin1String("SHA3512")) return QStringLiteral("SHA3-512");
    if (name == QLatin1String("BLAKE2B") || name == QLatin1String("BLAKE2B512") || name == QLatin1String("B2"))
        return QStringLiteral("BLAKE2b-512");
    if (name == QLatin1String("BLAKE2S") || name == QLatin1String("BLAKE2S256")) return QStringLiteral("BLAKE2s-256");
    if (name == QLatin1String("CRC32") || name == QLatin1String("SFV")) return QStringLiteral("CRC32");
    return {};
}

QStringList candidatesForLength(int hexLength)
{
    switch (hexLength) {
    case 8: return {QStringLiteral("CRC32")};
    case 32: return {QStringLiteral("MD5")};
    case 40: return {QStringLiteral("SHA1")};
    case 56: return {QStringLiteral("SHA224")};
    case 64: return {QStringLiteral("SHA256"), QStringLiteral("SHA3-256"), QStringLiteral("BLAKE2s-256")};
    case 96: return {QStringLiteral("SHA384")};
    case 128: return {QStringLiteral("SHA512"), QStringLiteral("SHA3-512"), QStringLiteral("BLAKE2b-512")};
    default: return {};
    }
}

// Verification files beside the file: name.sha256 and friends, and the
// SUMS files that list several names.
QVariantList scanSidecars(const QString &file)
{
    QVariantList found;
    const QFileInfo info(file);
    const QDir dir = info.dir();
    const QString name = info.fileName();
    const QStringList extensions{"md5", "sha1", "sha224", "sha256", "sha384", "sha512", "sha3", "b2", "sfv",
                                 "md5sum", "sha1sum", "sha256sum", "sha512sum", "digest", "DIGESTS", "asc.sha256"};
    QStringList candidates;
    for (const auto &ext : extensions)
        candidates << name + '.' + ext << name + '.' + ext.toUpper();
    const QStringList sums{"SHA256SUMS", "SHA512SUMS", "SHA1SUMS", "MD5SUMS", "B2SUMS", "CHECKSUMS",
                           "sha256sums.txt", "sha512sums.txt", "md5sums.txt", "checksums.txt",
                           "SHA256SUMS.txt", "CHECKSUM", "sha256sum.txt"};
    candidates << sums;
    candidates.removeDuplicates();
    for (const auto &candidate : candidates) {
        const QString path = dir.filePath(candidate);
        const QFileInfo sidecar(path);
        if (!sidecar.isFile() || sidecar.size() > 4 * 1024 * 1024 || path == file)
            continue;
        QFile handle(path);
        if (!handle.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        const auto lines = QString::fromUtf8(handle.read(4 * 1024 * 1024)).split('\n');
        // "name.sha256sum" -> SHA256, "SHA512SUMS" / "sha512sums.txt" -> SHA512.
        const bool single = candidate.startsWith(name + '.');
        QString stem = single ? QFileInfo(candidate).suffix() : QFileInfo(candidate).completeBaseName();
        stem.remove(QRegularExpression(QStringLiteral("sums?$"), QRegularExpression::CaseInsensitiveOption));
        const QString hint = algorithmFromName(stem);
        for (const auto &line : lines) {
            const auto trimmed = line.trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith('#') || trimmed.startsWith(';'))
                continue;
            const auto parsed = ChecksumCalculator::identify(trimmed);
            if (!parsed.value("valid").toBool())
                continue;
            const QString listed = parsed.value("fileName").toString();
            // A SUMS file must name this file; a sidecar may omit the name.
            if (!single && QFileInfo(listed).fileName() != name)
                continue;
            if (single && !listed.isEmpty() && QFileInfo(listed).fileName() != name)
                continue;
            QStringList algorithms = parsed.value("candidates").toStringList();
            if (!hint.isEmpty() && algorithms.contains(hint))
                algorithms = {hint};
            found.append(QVariantMap{{"path", path}, {"fileName", candidate},
                                     {"algorithm", algorithms.value(0)}, {"candidates", algorithms},
                                     {"expected", parsed.value("hex").toString()}});
            break;
        }
    }
    return found;
}

} // namespace

ChecksumCalculator::ChecksumCalculator(QObject *parent)
    : QObject(parent)
{
}

ChecksumCalculator::~ChecksumCalculator()
{
    if (m_cancel)
        m_cancel->store(true);
}

QStringList ChecksumCalculator::algorithms()
{
    return {"MD5", "SHA1", "SHA224", "SHA256", "SHA384", "SHA512",
            "SHA3-256", "SHA3-512", "BLAKE2b-512", "BLAKE2s-256", "CRC32"};
}

QStringList ChecksumCalculator::defaultAlgorithms()
{
    return {"MD5", "SHA1", "SHA256", "SHA512"};
}

QString ChecksumCalculator::localFile() const
{
    const QUrl url(m_path);
    const QString path = url.isLocalFile() ? url.toLocalFile()
        : (url.scheme().isEmpty() && QDir::isAbsolutePath(m_path) ? m_path : QString());
    return path.isEmpty() ? QString() : QDir::cleanPath(path);
}

void ChecksumCalculator::setPath(const QString &value)
{
    if (m_path == value)
        return;
    cancel();
    m_path = value;
    m_results.clear();
    m_error.clear();
    m_sidecars.clear();
    m_done = m_total = 0;
    m_speed = 0;
    emit pathChanged();
    emit resultsChanged();
    emit sidecarsChanged();
    emit progressChanged();
}

void ChecksumCalculator::cancel()
{
    ++m_generation;
    if (m_cancel)
        m_cancel->store(true);
    m_cancel.reset();
    if (m_busy) {
        m_busy = false;
        m_pending.clear();
        emit progressChanged();
    }
}

void ChecksumCalculator::compute(const QStringList &requested)
{
    QStringList wanted;
    for (const auto &name : requested)
        if (algorithms().contains(name) && !m_results.contains(name) && !wanted.contains(name)
                && !(m_busy && m_pending.contains(name)))
            wanted << name;
    if (wanted.isEmpty())
        return;
    const QString file = localFile();
    if (file.isEmpty() || !QFileInfo(file).isFile()) {
        m_error = message("Checksums need a local file.", "Sağlama toplamı için yerel bir dosya gerekir.");
        emit resultsChanged();
        return;
    }
    // A running pass is restarted with the union, so one read serves all.
    if (m_busy) {
        wanted = m_pending + wanted;
        wanted.removeDuplicates();
        cancel();
    }
    m_error.clear();
    m_busy = true;
    m_pending = wanted;
    m_done = 0;
    m_total = QFileInfo(file).size();
    m_speed = 0;
    emit progressChanged();
    emit resultsChanged();

    auto cancelled = std::make_shared<std::atomic_bool>(false);
    m_cancel = cancelled;
    const quint64 generation = m_generation;
    QPointer<ChecksumCalculator> self(this);
    auto *watcher = new QFutureWatcher<Outcome>(this);
    connect(watcher, &QFutureWatcher<Outcome>::finished, this, [this, watcher, generation] {
        const Outcome outcome = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        m_busy = false;
        m_pending.clear();
        m_cancel.reset();
        if (!outcome.error.isEmpty())
            m_error = outcome.error;
        for (auto it = outcome.results.cbegin(); it != outcome.results.cend(); ++it)
            m_results.insert(it.key(), it.value());
        if (outcome.error.isEmpty())
            m_done = m_total;
        emit progressChanged();
        emit resultsChanged();
        emit finished(outcome.error.isEmpty() && !outcome.cancelled);
    });
    watcher->setFuture(QtConcurrent::run([file, wanted, cancelled, self, generation]() {
        Outcome outcome;
        QFile handle(file);
        if (!handle.open(QIODevice::ReadOnly)) {
            outcome.error = handle.errorString();
            return outcome;
        }
#ifdef Q_OS_LINUX
        ::posix_fadvise(handle.handle(), 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
        std::vector<std::pair<QString, std::unique_ptr<QCryptographicHash>>> hashes;
        std::optional<Crc32> crc;
        for (const auto &name : wanted) {
            if (name == QLatin1String("CRC32"))
                crc.emplace();
            else if (const auto algorithm = cryptoAlgorithm(name))
                hashes.emplace_back(name, std::make_unique<QCryptographicHash>(*algorithm));
        }
        QByteArray buffer(1024 * 1024, Qt::Uninitialized);
        QElapsedTimer clock, report;
        clock.start();
        report.start();
        qint64 done = 0;
        for (;;) {
            if (cancelled->load()) {
                outcome.cancelled = true;
                return outcome;
            }
            const qint64 read = handle.read(buffer.data(), buffer.size());
            if (read < 0) {
                outcome.error = handle.errorString();
                return outcome;
            }
            if (read == 0)
                break;
            const QByteArrayView chunk(buffer.constData(), read);
            for (auto &entry : hashes)
                entry.second->addData(chunk);
            if (crc)
                crc->add(buffer.constData(), read);
            done += read;
            if (report.elapsed() >= 80) {
                report.restart();
                const double speed = clock.elapsed() > 0 ? done * 1000.0 / clock.elapsed() : 0;
                QMetaObject::invokeMethod(self.data(), [self, generation, done, speed] {
                    if (!self || self->m_generation != generation || !self->m_busy)
                        return;
                    self->m_done = done;
                    self->m_speed = speed;
                    emit self->progressChanged();
                }, Qt::QueuedConnection);
            }
        }
        for (auto &entry : hashes)
            outcome.results.insert(entry.first, QString::fromLatin1(entry.second->result().toHex()));
        if (crc)
            outcome.results.insert(QStringLiteral("CRC32"), QString::fromLatin1(crc->hex()));
        return outcome;
    }));
}

QVariantMap ChecksumCalculator::identify(const QString &expected)
{
    QVariantMap result{{"valid", false}, {"hex", QString()}, {"candidates", QStringList()}, {"fileName", QString()}};
    QString text = expected.trimmed();
    if (text.isEmpty() || text.size() > 4096)
        return result;
    text = text.section('\n', 0, 0).trimmed();
    QString hint;
    QString fileName;

    // BSD style: "SHA256 (file.iso) = hex"
    static const QRegularExpression bsd(QStringLiteral("^([A-Za-z0-9-]+)\\s*\\((.*)\\)\\s*=\\s*([0-9A-Fa-f]+)$"));
    // SRI: "sha256-base64" / "sha512-base64"
    static const QRegularExpression sri(QStringLiteral("^(sha256|sha384|sha512)-([A-Za-z0-9+/=]+)$"),
                                        QRegularExpression::CaseInsensitiveOption);
    // "SHA256:hex" / "sha256=hex"
    static const QRegularExpression prefixed(QStringLiteral("^([A-Za-z0-9-]+)[:=]\\s*([0-9A-Fa-f]+)$"));
    // GNU coreutils: "hex  name" or "hex *name"; SFV: "name crc32"
    static const QRegularExpression gnu(QStringLiteral("^\\\\?([0-9A-Fa-f]{8,128})\\s+\\*?(.*)$"));
    static const QRegularExpression sfv(QStringLiteral("^(.+?)\\s+([0-9A-Fa-f]{8})$"));
    static const QRegularExpression hexOnly(QStringLiteral("^[0-9A-Fa-f]+$"));

    QString hex;
    if (const auto m = bsd.match(text); m.hasMatch()) {
        hint = algorithmFromName(m.captured(1));
        fileName = m.captured(2);
        hex = m.captured(3);
    } else if (const auto m = sri.match(text); m.hasMatch()) {
        hint = algorithmFromName(m.captured(1));
        hex = QString::fromLatin1(QByteArray::fromBase64(m.captured(2).toLatin1()).toHex());
    } else if (const auto m = prefixed.match(text); m.hasMatch()) {
        hint = algorithmFromName(m.captured(1));
        hex = m.captured(2);
    } else if (const auto m = gnu.match(text); m.hasMatch()) {
        hex = m.captured(1);
        fileName = m.captured(2).trimmed();
    } else if (const auto m = sfv.match(text); m.hasMatch()) {
        hint = QStringLiteral("CRC32");
        fileName = m.captured(1).trimmed();
        hex = m.captured(2);
    } else {
        hex = QString(text).remove(QRegularExpression(QStringLiteral("\\s")));
    }
    if (!hexOnly.match(hex).hasMatch())
        return result;
    hex = hex.toLower();
    QStringList candidates = candidatesForLength(hex.size());
    if (!hint.isEmpty()) {
        if (!candidates.contains(hint))
            return result;
        candidates = {hint};
    }
    if (candidates.isEmpty())
        return result;
    result.insert("valid", true);
    result.insert("hex", hex);
    result.insert("candidates", candidates);
    result.insert("fileName", fileName);
    return result;
}

QString ChecksumCalculator::matchingAlgorithm(const QString &expected) const
{
    const auto parsed = identify(expected);
    if (!parsed.value("valid").toBool())
        return {};
    const QString hex = parsed.value("hex").toString();
    for (const auto &name : parsed.value("candidates").toStringList())
        if (m_results.value(name).toString() == hex)
            return name;
    return {};
}

QString ChecksumCalculator::compare(const QString &expected) const
{
    const auto parsed = identify(expected);
    if (!parsed.value("valid").toBool())
        return {};
    if (!matchingAlgorithm(expected).isEmpty())
        return QStringLiteral("match");
    for (const auto &name : parsed.value("candidates").toStringList())
        if (!m_results.contains(name))
            return {};
    return QStringLiteral("mismatch");
}

void ChecksumCalculator::findSidecars()
{
    const QString file = localFile();
    if (file.isEmpty())
        return;
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher, file] {
        const auto found = watcher->result();
        watcher->deleteLater();
        if (file != localFile())
            return;
        m_sidecars = found;
        emit sidecarsChanged();
    });
    watcher->setFuture(QtConcurrent::run(scanSidecars, file));
}

QString ChecksumCalculator::extensionFor(const QString &algorithm)
{
    if (algorithm == QLatin1String("BLAKE2b-512")) return QStringLiteral("b2");
    if (algorithm == QLatin1String("CRC32")) return QStringLiteral("sfv");
    return algorithm.toLower().remove('-');
}

QString ChecksumCalculator::writeSidecar(const QString &algorithm)
{
    const QString file = localFile();
    const QString hash = m_results.value(algorithm).toString();
    if (file.isEmpty() || hash.isEmpty())
        return message("Compute the checksum first.", "Önce sağlama toplamını hesapla.");
    const QFileInfo info(file);
    const QString target = info.dir().filePath(info.fileName() + '.' + extensionFor(algorithm));
    QFile out(target);
    // NewOnly: an existing verification file is never replaced.
    if (!out.open(QIODevice::WriteOnly | QIODevice::NewOnly | QIODevice::Text))
        return out.exists() ? message("%1 already exists.", "%1 zaten var.").arg(QFileInfo(target).fileName())
                            : out.errorString();
    const QByteArray line = algorithm == QLatin1String("CRC32")
        ? info.fileName().toUtf8() + ' ' + hash.toLatin1() + '\n'
        : hash.toLatin1() + "  " + info.fileName().toUtf8() + '\n';
    if (out.write(line) != line.size())
        return out.errorString();
    out.close();
    findSidecars();
    return {};
}

QString ChecksumCalculator::summaryText() const
{
    const QString name = QFileInfo(localFile()).fileName();
    QStringList lines;
    for (const auto &algorithm : algorithms())
        if (m_results.contains(algorithm))
            lines << algorithm + QStringLiteral(" (") + name + QStringLiteral(") = ") + m_results.value(algorithm).toString();
    return lines.join('\n');
}

QByteArray ChecksumCalculator::hashFile(const QString &path, const QString &algorithm)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    if (algorithm == QLatin1String("CRC32")) {
        Crc32 crc;
        const QByteArray data = file.readAll();
        crc.add(data.constData(), data.size());
        return crc.hex();
    }
    const auto kind = cryptoAlgorithm(algorithm);
    if (!kind)
        return {};
    QCryptographicHash hash(*kind);
    hash.addData(&file);
    return hash.result().toHex();
}
