#include "subtitlestylemanager.h"

#include <QGuiApplication>
#include <QFont>
#include <QSettings>
#include <QtGlobal>

namespace {
constexpr auto kGroup = "video/subtitles/style/";

QColor readColor(QSettings &settings, const QString &key, const QColor &fallback)
{
    const QColor value(settings.value(QString::fromLatin1(kGroup) + key, fallback.name(QColor::HexArgb)).toString());
    return value.isValid() ? value : fallback;
}
}

SubtitleStyleManager::SubtitleStyleManager(QObject *parent)
    : QObject(parent)
{
    load();
}

QString SubtitleStyleManager::defaultFontFamily() const
{
    return QGuiApplication::font().family();
}

void SubtitleStyleManager::load()
{
    QSettings settings;
    const QString font = defaultFontFamily();
    m_subtitleColor = readColor(settings, QStringLiteral("subtitleColor"), QColor(QStringLiteral("#FFFFFFFF")));
    m_subtitleFontFamily = settings.value(QString::fromLatin1(kGroup) + QStringLiteral("subtitleFontFamily"), font).toString();
    m_subtitleFontSize = qBound(12, settings.value(QString::fromLatin1(kGroup) + QStringLiteral("subtitleFontSize"), 30).toInt(), 72);
    m_backgroundColor = readColor(settings, QStringLiteral("backgroundColor"), QColor(QStringLiteral("#FF000000")));
    m_backgroundRadius = qBound(0, settings.value(QString::fromLatin1(kGroup) + QStringLiteral("backgroundRadius"), 8).toInt(), 32);
    m_backgroundOpacity = qBound<qreal>(0.0, settings.value(QString::fromLatin1(kGroup) + QStringLiteral("backgroundOpacity"), 0.68).toDouble(), 1.0);
    m_backgroundBlur = qBound(0, settings.value(QString::fromLatin1(kGroup) + QStringLiteral("backgroundBlur"), 0).toInt(), 48);
    m_sdhColor = readColor(settings, QStringLiteral("sdhColor"), QColor(QStringLiteral("#FFFFD166")));
    m_sdhFontFamily = settings.value(QString::fromLatin1(kGroup) + QStringLiteral("sdhFontFamily"), font).toString();
    m_sdhFontSize = qBound(12, settings.value(QString::fromLatin1(kGroup) + QStringLiteral("sdhFontSize"), 30).toInt(), 72);
    m_forcedColor = readColor(settings, QStringLiteral("forcedColor"), QColor(QStringLiteral("#FF7FDBFF")));
    m_forcedFontFamily = settings.value(QString::fromLatin1(kGroup) + QStringLiteral("forcedFontFamily"), font).toString();
    m_forcedFontSize = qBound(12, settings.value(QString::fromLatin1(kGroup) + QStringLiteral("forcedFontSize"), 30).toInt(), 72);
}

void SubtitleStyleManager::saveValue(const QString &key, const QVariant &value)
{
    QSettings settings;
    settings.setValue(QString::fromLatin1(kGroup) + key, value);
    settings.sync();
}

void SubtitleStyleManager::setSubtitleColor(const QColor &value)
{
    if (!value.isValid() || value == m_subtitleColor) return;
    m_subtitleColor = value; saveValue(QStringLiteral("subtitleColor"), value.name(QColor::HexArgb)); emit styleChanged();
}
void SubtitleStyleManager::setSubtitleFontFamily(const QString &value)
{
    const QString v = value.trimmed().isEmpty() ? defaultFontFamily() : value.trimmed();
    if (v == m_subtitleFontFamily) return;
    m_subtitleFontFamily = v; saveValue(QStringLiteral("subtitleFontFamily"), v); emit styleChanged();
}
void SubtitleStyleManager::setSubtitleFontSize(int value)
{
    value = qBound(12, value, 72); if (value == m_subtitleFontSize) return;
    m_subtitleFontSize = value; saveValue(QStringLiteral("subtitleFontSize"), value); emit styleChanged();
}
void SubtitleStyleManager::setBackgroundColor(const QColor &value)
{
    if (!value.isValid() || value == m_backgroundColor) return;
    m_backgroundColor = value; saveValue(QStringLiteral("backgroundColor"), value.name(QColor::HexArgb)); emit styleChanged();
}
void SubtitleStyleManager::setBackgroundRadius(int value)
{
    value = qBound(0, value, 32); if (value == m_backgroundRadius) return;
    m_backgroundRadius = value; saveValue(QStringLiteral("backgroundRadius"), value); emit styleChanged();
}
void SubtitleStyleManager::setBackgroundOpacity(qreal value)
{
    value = qBound<qreal>(0.0, value, 1.0); if (qFuzzyCompare(value, m_backgroundOpacity)) return;
    m_backgroundOpacity = value; saveValue(QStringLiteral("backgroundOpacity"), value); emit styleChanged();
}
void SubtitleStyleManager::setBackgroundBlur(int value)
{
    value = qBound(0, value, 48); if (value == m_backgroundBlur) return;
    m_backgroundBlur = value; saveValue(QStringLiteral("backgroundBlur"), value); emit styleChanged();
}
void SubtitleStyleManager::setSdhColor(const QColor &value)
{
    if (!value.isValid() || value == m_sdhColor) return;
    m_sdhColor = value; saveValue(QStringLiteral("sdhColor"), value.name(QColor::HexArgb)); emit styleChanged();
}
void SubtitleStyleManager::setSdhFontFamily(const QString &value)
{
    const QString v = value.trimmed().isEmpty() ? defaultFontFamily() : value.trimmed();
    if (v == m_sdhFontFamily) return;
    m_sdhFontFamily = v; saveValue(QStringLiteral("sdhFontFamily"), v); emit styleChanged();
}
void SubtitleStyleManager::setSdhFontSize(int value)
{
    value = qBound(12, value, 72); if (value == m_sdhFontSize) return;
    m_sdhFontSize = value; saveValue(QStringLiteral("sdhFontSize"), value); emit styleChanged();
}
void SubtitleStyleManager::setForcedColor(const QColor &value)
{
    if (!value.isValid() || value == m_forcedColor) return;
    m_forcedColor = value; saveValue(QStringLiteral("forcedColor"), value.name(QColor::HexArgb)); emit styleChanged();
}
void SubtitleStyleManager::setForcedFontFamily(const QString &value)
{
    const QString v = value.trimmed().isEmpty() ? defaultFontFamily() : value.trimmed();
    if (v == m_forcedFontFamily) return;
    m_forcedFontFamily = v; saveValue(QStringLiteral("forcedFontFamily"), v); emit styleChanged();
}

void SubtitleStyleManager::setForcedFontSize(int value)
{
    value = qBound(12, value, 72); if (value == m_forcedFontSize) return;
    m_forcedFontSize = value; saveValue(QStringLiteral("forcedFontSize"), value); emit styleChanged();
}

void SubtitleStyleManager::resetDefaults()
{
    QSettings settings;
    settings.remove(QString::fromLatin1(kGroup).left(QString::fromLatin1(kGroup).size() - 1));
    settings.sync();
    load();
    emit styleChanged();
}
