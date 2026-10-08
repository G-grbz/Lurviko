#pragma once

#include <QObject>
#include <QVariant>

class KeyboardShortcutManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList catalog READ catalog CONSTANT)
    Q_PROPERTY(QVariantMap bindings READ bindings NOTIFY bindingsChanged)
    Q_PROPERTY(bool editorOpen READ editorOpen WRITE setEditorOpen NOTIFY editorOpenChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
public:
    explicit KeyboardShortcutManager(QObject *parent = nullptr);
    QVariantList catalog() const { return m_catalog; }
    QVariantMap bindings() const { return m_bindings; }
    bool editorOpen() const { return m_editorOpen; }
    bool recording() const { return m_recording; }
    void setEditorOpen(bool open);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE QString displaySequence(const QString &sequence) const;
    Q_INVOKABLE QVariantMap validateBindings(const QString &action, const QStringList &sequences) const;
    Q_INVOKABLE QVariantMap assign(const QString &action, const QStringList &sequences, bool replaceConflicts = false);
    Q_INVOKABLE void resetAll();
    bool matches(const QString &action, int key, int modifiers) const;
    static QString sequenceForKey(int key, int modifiers);
signals:
    void bindingsChanged();
    void editorOpenChanged();
    void recordingChanged();
    void sequenceRecorded(const QString &sequence);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QVariantMap actionInfo(const QString &id) const;
    QStringList normalized(const QStringList &sequences, bool *ok) const;
    void persist(const QVariantMap &bindings);
    QVariantList m_catalog;
    QVariantMap m_bindings;
    bool m_editorOpen = false;
    bool m_recording = false;
};
