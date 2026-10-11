#pragma once

#include <QObject>
#include <QColor>
#include <QString>
#include <QVariant>

class SubtitleStyleManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QColor subtitleColor READ subtitleColor WRITE setSubtitleColor NOTIFY styleChanged)
    Q_PROPERTY(QString subtitleFontFamily READ subtitleFontFamily WRITE setSubtitleFontFamily NOTIFY styleChanged)
    Q_PROPERTY(int subtitleFontSize READ subtitleFontSize WRITE setSubtitleFontSize NOTIFY styleChanged)
    Q_PROPERTY(int subtitleShadowMode READ subtitleShadowMode WRITE setSubtitleShadowMode NOTIFY styleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY styleChanged)
    Q_PROPERTY(int backgroundRadius READ backgroundRadius WRITE setBackgroundRadius NOTIFY styleChanged)
    Q_PROPERTY(qreal backgroundOpacity READ backgroundOpacity WRITE setBackgroundOpacity NOTIFY styleChanged)
    Q_PROPERTY(int backgroundBlur READ backgroundBlur WRITE setBackgroundBlur NOTIFY styleChanged)
    Q_PROPERTY(QColor sdhColor READ sdhColor WRITE setSdhColor NOTIFY styleChanged)
    Q_PROPERTY(QString sdhFontFamily READ sdhFontFamily WRITE setSdhFontFamily NOTIFY styleChanged)
    Q_PROPERTY(int sdhFontSize READ sdhFontSize WRITE setSdhFontSize NOTIFY styleChanged)
    Q_PROPERTY(int sdhShadowMode READ sdhShadowMode WRITE setSdhShadowMode NOTIFY styleChanged)
    Q_PROPERTY(QColor forcedColor READ forcedColor WRITE setForcedColor NOTIFY styleChanged)
    Q_PROPERTY(QString forcedFontFamily READ forcedFontFamily WRITE setForcedFontFamily NOTIFY styleChanged)
    Q_PROPERTY(int forcedFontSize READ forcedFontSize WRITE setForcedFontSize NOTIFY styleChanged)
    Q_PROPERTY(int forcedShadowMode READ forcedShadowMode WRITE setForcedShadowMode NOTIFY styleChanged)

public:
    explicit SubtitleStyleManager(QObject *parent = nullptr);

    QColor subtitleColor() const { return m_subtitleColor; }
    QString subtitleFontFamily() const { return m_subtitleFontFamily; }
    int subtitleFontSize() const { return m_subtitleFontSize; }
    int subtitleShadowMode() const { return m_subtitleShadowMode; }
    QColor backgroundColor() const { return m_backgroundColor; }
    int backgroundRadius() const { return m_backgroundRadius; }
    qreal backgroundOpacity() const { return m_backgroundOpacity; }
    int backgroundBlur() const { return m_backgroundBlur; }
    QColor sdhColor() const { return m_sdhColor; }
    QString sdhFontFamily() const { return m_sdhFontFamily; }
    int sdhFontSize() const { return m_sdhFontSize; }
    int sdhShadowMode() const { return m_sdhShadowMode; }
    QColor forcedColor() const { return m_forcedColor; }
    QString forcedFontFamily() const { return m_forcedFontFamily; }
    int forcedFontSize() const { return m_forcedFontSize; }
    int forcedShadowMode() const { return m_forcedShadowMode; }

    void setSubtitleColor(const QColor &value);
    void setSubtitleFontFamily(const QString &value);
    void setSubtitleFontSize(int value);
    void setSubtitleShadowMode(int value);
    void setBackgroundColor(const QColor &value);
    void setBackgroundRadius(int value);
    void setBackgroundOpacity(qreal value);
    void setBackgroundBlur(int value);
    void setSdhColor(const QColor &value);
    void setSdhFontFamily(const QString &value);
    void setSdhFontSize(int value);
    void setSdhShadowMode(int value);
    void setForcedColor(const QColor &value);
    void setForcedFontFamily(const QString &value);
    void setForcedFontSize(int value);
    void setForcedShadowMode(int value);

    Q_INVOKABLE void resetDefaults();

signals:
    void styleChanged();

private:
    void load();
    void saveValue(const QString &key, const QVariant &value);
    QString defaultFontFamily() const;

    QColor m_subtitleColor;
    QString m_subtitleFontFamily;
    int m_subtitleFontSize = 30;
    int m_subtitleShadowMode = 1;
    QColor m_backgroundColor;
    int m_backgroundRadius = 8;
    qreal m_backgroundOpacity = 0.68;
    int m_backgroundBlur = 0;
    QColor m_sdhColor;
    QString m_sdhFontFamily;
    int m_sdhFontSize = 30;
    int m_sdhShadowMode = 1;
    QColor m_forcedColor;
    QString m_forcedFontFamily;
    int m_forcedFontSize = 30;
    int m_forcedShadowMode = 1;
};
