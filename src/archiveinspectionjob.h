#pragma once
#include <KJob>
#include <QStringList>
#include <atomic>
#include <memory>

// Listing and destination conflict checks may touch thousands of filesystem
// entries. Keep the bounded streaming parser entirely off the GUI thread.
class ArchiveInspectionJob final : public KJob
{
public:
    struct Result { QStringList conflicts; QString error; bool valid = true; };
    ArchiveInspectionJob(QString executable, QString archive, QString destination,
                         QString title, QObject *parent = nullptr);
    ~ArchiveInspectionJob() override;
    void start() override;
    const Result &inspection() const { return m_result; }
    QString errorString() const override { return errorText(); }
protected:
    bool doKill() override;
private:
    QString m_executable, m_archive, m_destination, m_title;
    Result m_result;
    std::shared_ptr<std::atomic_bool> m_cancelled = std::make_shared<std::atomic_bool>(false);
};
