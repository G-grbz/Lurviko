#pragma once

#include <QObject>
#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QVariantList>
#include <functional>

class QProcess;

// On-demand usershare operations. No polling, directory scans or shell commands.
class SambaShareManager : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool shared READ shared NOTIFY changed)
    Q_PROPERTY(bool editable READ editable NOTIFY changed)
    Q_PROPERTY(bool readOnly READ readOnly NOTIFY changed)
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString shareName READ shareName NOTIFY changed)
    Q_PROPERTY(QString comment READ comment NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QStringList shareNames READ shareNames NOTIFY changed)
    Q_PROPERTY(QVariantList localShares READ localShares NOTIFY changed)
public:
    explicit SambaShareManager(QObject* parent = nullptr);
    SambaShareManager(const QString& netProgram, QObject* parent);
    ~SambaShareManager() override;
    bool busy() const { return m_busy; }
    bool available() const { return m_available; }
    bool shared() const { return !m_selected.isEmpty(); }
    bool editable() const;
    bool readOnly() const;
    QString path() const { return m_path; }
    QString shareName() const;
    QString comment() const;
    QString error() const { return m_error; }
    QString address() const;
    QStringList shareNames() const;
    QVariantList localShares() const;
    Q_INVOKABLE void inspect(const QString& location);
    Q_INVOKABLE void listShares();
    Q_INVOKABLE void clear();
    Q_INVOKABLE void selectShare(const QString& name);
    Q_INVOKABLE void save(const QString& name, const QString& comment, bool readOnly);
    Q_INVOKABLE void remove();
signals:
    void changed();
    void operationFinished(bool success, const QString& message);
private:
    struct Share { QString name, path, comment, acl; bool guest = false; };
    QString m_netProgram, m_path, m_selected, m_error, m_pendingLocation;
    QHash<QString, Share> m_shares;
    QPointer<QProcess> m_process;
    quint64 m_generation = 0;
    bool m_busy = false, m_available = false, m_writing = false;
    void run(const QStringList& args, bool writing,
             std::function<void(bool, const QByteArray&, const QString&)> done);
    void fail(const QString& error);
    void readShares(quint64 token);
    void mutate(const QStringList& args, const QString& successMessage);
};
