#pragma once

#include <QObject>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>
#include <memory>

// Read-only status badges remain in GitStatusCache. This facade owns explicit
// user operations; every Git process and filesystem check runs in a bounded pool.
class GitOperations : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QVariantMap info READ info NOTIFY changed)
    Q_PROPERTY(QVariantList changes READ changes NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY detailsChanged)
    Q_PROPERTY(QVariantList upstreamTargets READ upstreamTargets NOTIFY detailsChanged)
    Q_PROPERTY(QString detailText READ detailText NOTIFY detailsChanged)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString progress READ progress NOTIFY changed)
    Q_PROPERTY(QString lastCommit READ lastCommit NOTIFY changed)
public:
    explicit GitOperations(QObject* parent = nullptr);
    GitOperations(const QString& gitProgram, QObject* parent);
    ~GitOperations() override;
    bool available() const;
    bool loading() const;
    bool busy() const;
    QVariantMap info() const;
    QVariantList changes() const;
    QVariantList history() const;
    QVariantList upstreamTargets() const;
    QString detailText() const;
    QString error() const;
    QString progress() const;
    QString lastCommit() const;
    Q_INVOKABLE void setContext(const QString& location, bool directory = true);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadChanges();
    Q_INVOKABLE void loadHistory();
    Q_INVOKABLE void loadDiff(bool compareUpstream = false);
    Q_INVOKABLE void loadUpstreams();
    Q_INVOKABLE void synchronize();
    Q_INVOKABLE void fetch();
    Q_INVOKABLE void pull();
    Q_INVOKABLE void push();
    Q_INVOKABLE void commit(const QStringList& selectedIds, const QString& message, bool pushAfter = false);
    Q_INVOKABLE void setUpstream(const QString& remoteRef);
    Q_INVOKABLE void openRemote();
    Q_INVOKABLE void cancel();
    static QUrl remoteWebUrl(const QString& remote);
signals:
    void changed();
    void detailsChanged();
    void operationFinished(const QString& operation, bool success, const QString& message);
    void repositoryMutated(const QString& root, const QString& commonDirectory, const QStringList& paths);
private:
    struct Private;
    std::unique_ptr<Private> d;
    void read(const QString& kind, bool force = false);
    void startRead();
    void write(const QString& kind, const QVariantMap& parameters = {});
};
