#pragma once

#include <QObject>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <atomic>
#include <memory>

// Checksums of one local file for the Properties dialog.
//
// Every requested algorithm is computed in a single read of the file, off
// the GUI thread, with progress, speed and cancellation. An expected value
// can be pasted in any common form (bare hex, "hash  name" lines, BSD
// "SHA256 (name) = hash", SRI "sha256-base64", "SHA256:hex"); its algorithm
// is recognised and compared automatically. Verification files next to the
// file (name.sha256, SHA256SUMS, …) are found and can be written.
class ChecksumCalculator : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesDone READ bytesDone NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal NOTIFY progressChanged)
    Q_PROPERTY(double bytesPerSecond READ bytesPerSecond NOTIFY progressChanged)
    // algorithm -> lowercase hex
    Q_PROPERTY(QVariantMap results READ results NOTIFY resultsChanged)
    Q_PROPERTY(QStringList pending READ pending NOTIFY progressChanged)
    Q_PROPERTY(QString error READ error NOTIFY resultsChanged)
    // [{ path, fileName, algorithm, expected }]
    Q_PROPERTY(QVariantList sidecars READ sidecars NOTIFY sidecarsChanged)
public:
    explicit ChecksumCalculator(QObject *parent = nullptr);
    ~ChecksumCalculator() override;

    QString path() const { return m_path; }
    void setPath(const QString &value);
    bool busy() const { return m_busy; }
    double progress() const { return m_total > 0 ? double(m_done) / double(m_total) : (m_busy ? 0.0 : 1.0); }
    qint64 bytesDone() const { return m_done; }
    qint64 bytesTotal() const { return m_total; }
    double bytesPerSecond() const { return m_speed; }
    QVariantMap results() const { return m_results; }
    QStringList pending() const { return m_pending; }
    QString error() const { return m_error; }
    QVariantList sidecars() const { return m_sidecars; }

    Q_INVOKABLE static QStringList algorithms();
    Q_INVOKABLE static QStringList defaultAlgorithms();
    // Computes the given algorithms that have no result yet, in one pass.
    Q_INVOKABLE void compute(const QStringList &algorithms);
    Q_INVOKABLE void cancel();
    // { valid, hex, candidates: [algorithms], fileName }
    Q_INVOKABLE static QVariantMap identify(const QString &expected);
    // "match", "mismatch" or "" (not computed / not recognised).
    Q_INVOKABLE QString compare(const QString &expected) const;
    // The algorithm whose result equals the expected value, if any.
    Q_INVOKABLE QString matchingAlgorithm(const QString &expected) const;
    Q_INVOKABLE void findSidecars();
    // Writes "<hash>  <name>" to <file>.<ext>; never replaces a file.
    Q_INVOKABLE QString writeSidecar(const QString &algorithm);
    Q_INVOKABLE static QString extensionFor(const QString &algorithm);
    Q_INVOKABLE QString summaryText() const;

    static QByteArray hashFile(const QString &path, const QString &algorithm);

signals:
    void pathChanged();
    void progressChanged();
    void resultsChanged();
    void sidecarsChanged();
    void finished(bool success);

private:
    QString m_path;
    QString m_error;
    QVariantMap m_results;
    QVariantList m_sidecars;
    QStringList m_pending;
    bool m_busy = false;
    qint64 m_done = 0;
    qint64 m_total = 0;
    double m_speed = 0;
    quint64 m_generation = 0;
    std::shared_ptr<std::atomic_bool> m_cancel;
    QString localFile() const;
};
