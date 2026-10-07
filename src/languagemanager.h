#pragma once

#include <QObject>
#include <QHash>

class LanguageManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)

public:
    explicit LanguageManager(QObject *parent = nullptr);

    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    Q_INVOKABLE QString t(const QString &key) const;
    Q_INVOKABLE QString localizeMessage(const QString &message) const;

signals:
    void languageChanged();

private:
    void buildDictionary();
    QString m_language;
    QHash<QString, QHash<QString, QString>> m_dictionary;
};
