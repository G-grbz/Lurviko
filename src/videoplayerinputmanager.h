#pragma once

#include <QObject>
#include <QPointer>
#include <QVector>

class VideoPlayerInputManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit VideoPlayerInputManager(QObject *parent = nullptr);

    bool active() const { return hasVisibleViewer(); }
    void setActive(bool active);
    Q_INVOKABLE void registerViewer(QObject *viewer);
    Q_INVOKABLE void unregisterViewer(QObject *viewer);

signals:
    void activeChanged();
    void escapePressed();
    void togglePlaybackPressed();
    void seekBackwardPressed();
    void seekForwardPressed();
    void volumeUpPressed();
    void volumeDownPressed();
    void mutePressed();
    void fullscreenPressed();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool hasVisibleViewer() const;
    void compactViewers();
    QVector<QPointer<QObject>> m_viewers;
};
