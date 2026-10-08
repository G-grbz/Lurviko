#pragma once
#include <QObject>
#include <QCache>
#include <QProcess>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

class VideoChapterReader : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUrl source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
public:
    explicit VideoChapterReader(QObject *parent = nullptr);
    ~VideoChapterReader() override;
    QUrl source() const { return m_source; }
    void setSource(const QUrl &source);
    QVariantList chapters() const { return m_chapters; }
    bool loading() const { return m_loading; }
    QString error() const { return m_error; }
    Q_INVOKABLE int chapterAt(qint64 positionMs) const;
    static QVariantList parseChapters(const QByteArray &json);
signals:
    void sourceChanged();
    void chaptersChanged();
    void loadingChanged();
    void errorChanged();
private:
    void cancelProbe();
    void finishProbe(QProcess *process, const QString &error);
    QUrl m_source;
    QVariantList m_chapters;
    QProcess *m_process = nullptr;
    QTimer m_timeout;
    QByteArray m_output;
    QCache<QString, QVariantList> m_cache{32};
    QString m_cacheKey;
    bool m_loading = false;
    QString m_error;
};
