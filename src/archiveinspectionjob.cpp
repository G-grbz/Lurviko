#include "archiveinspectionjob.h"
#include "archivelisting.h"
#include <QFutureWatcher>
#include <QProcess>
#include <QTimer>
#include <QSettings>
#include <QLocale>
#include <QtConcurrent>

ArchiveInspectionJob::ArchiveInspectionJob(QString executable, QString archive, QString destination,
                                           QString title, QObject *parent)
    : KJob(parent), m_executable(std::move(executable)), m_archive(std::move(archive)),
      m_destination(std::move(destination)), m_title(std::move(title))
{ setCapabilities(KJob::Killable); }
ArchiveInspectionJob::~ArchiveInspectionJob() { m_cancelled->store(true); }
bool ArchiveInspectionJob::doKill() { m_cancelled->store(true); return true; }
void ArchiveInspectionJob::start()
{
    startElapsedTimer();
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher] {
        m_result = watcher->result(); watcher->deleteLater();
        if (m_cancelled->load()) return;
        if (!m_result.valid) {
            const QString fallback = QLocale::system().language() == QLocale::Turkish ? "tr" : "en";
            m_result.error = QSettings().value("ui/language", fallback).toString() == "tr"
                ? QStringLiteral("Arşiv bilgileri aşırı uzun bir girdi içeriyor. Çıkarma başlatılmadı.")
                : QStringLiteral("Archive metadata contains an excessively long entry. Extraction was not started.");
        }
        if (!m_result.error.isEmpty() || !m_result.valid) {
            setError(KJob::UserDefinedError); setErrorText(m_result.error);
        } else setPercent(100);
        emitResult();
    });
    // Announce on the next event turn, after callers attach progress listeners.
    QTimer::singleShot(0, this, [this] { if (!m_cancelled->load()) emit description(this, m_title, {}, {}); });
    watcher->setFuture(QtConcurrent::run([executable = m_executable, archive = m_archive,
                                        destination = m_destination, cancelled = m_cancelled] {
        Result result;
        if (cancelled->load()) return result;
        ArchiveListing listing(destination);
        QProcess process;
        process.start(executable, {QStringLiteral("l"), QStringLiteral("-slt"), archive});
        if (!process.waitForStarted(5000)) { result.error = process.errorString(); return result; }
        process.closeWriteChannel(); // No unattended password/console prompts.
        QByteArray diagnostics, errors;
        const auto read = [&] {
            process.setReadChannel(QProcess::StandardOutput);
            while (process.bytesAvailable() > 0 && !cancelled->load() && listing.valid()) {
                const QByteArray chunk = process.read(65536);
                listing.append(chunk);
                diagnostics = (std::move(diagnostics) + chunk).right(8192);
            }
            errors = (std::move(errors) + process.readAllStandardError()).right(8192);
        };
        while (process.state() != QProcess::NotRunning) {
            if (cancelled->load() || !listing.valid()) {
                process.kill(); process.waitForFinished(5000); break;
            }
            process.waitForReadyRead(100);
            read();
        }
        read(); listing.finish();
        result.valid = listing.valid();
        result.conflicts = listing.conflicts();
        if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
            result.error = QString::fromLocal8Bit(errors.isEmpty() ? diagnostics : errors).trimmed();
            if (result.error.isEmpty()) result.error = process.errorString();
        }
        return result;
    }));
}
