#include "videoplayerinputmanager.h"
#include "keyboardshortcutmanager.h"

#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QVariant>
#include <utility>

VideoPlayerInputManager::VideoPlayerInputManager(QObject *parent)
    : QObject(parent)
{
    if (QCoreApplication::instance())
        QCoreApplication::instance()->installEventFilter(this);
}

void VideoPlayerInputManager::setActive(bool active)
{
    Q_UNUSED(active)
    // Kept for source compatibility with older QML. Runtime activation is now
    // derived from the registered VideoViewer objects themselves, so a missed
    // Popup.onOpened signal can no longer disable keyboard input.
    emit activeChanged();
}

void VideoPlayerInputManager::compactViewers()
{
    for (qsizetype i = m_viewers.size() - 1; i >= 0; --i) {
        if (m_viewers.at(i).isNull())
            m_viewers.removeAt(i);
    }
}

void VideoPlayerInputManager::registerViewer(QObject *viewer)
{
    if (!viewer)
        return;
    compactViewers();
    for (const auto &existing : std::as_const(m_viewers)) {
        if (existing.data() == viewer)
            return;
    }
    m_viewers.push_back(QPointer<QObject>(viewer));
    connect(viewer, &QObject::destroyed, this, [this]() {
        compactViewers();
        emit activeChanged();
    });
    emit activeChanged();
}

void VideoPlayerInputManager::unregisterViewer(QObject *viewer)
{
    for (qsizetype i = m_viewers.size() - 1; i >= 0; --i) {
        if (m_viewers.at(i).isNull() || m_viewers.at(i).data() == viewer)
            m_viewers.removeAt(i);
    }
    emit activeChanged();
}

bool VideoPlayerInputManager::hasVisibleViewer() const
{
    for (const auto &viewer : m_viewers) {
        if (!viewer.isNull() && viewer->property("visible").toBool())
            return true;
    }
    return false;
}

void VideoPlayerInputManager::setShortcutManager(QObject *manager)
{
    m_shortcuts = qobject_cast<KeyboardShortcutManager *>(manager);
}

bool VideoPlayerInputManager::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched)

    if ((event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride)
            || !hasVisibleViewer())
        return QObject::eventFilter(watched, event);

    auto *keyEvent = static_cast<QKeyEvent *>(event);
    if (m_shortcuts) {
        if (m_shortcuts->editorOpen()) return QObject::eventFilter(watched, event);
        const QString actions[] = {"video_close", "video_play", "video_back", "video_forward",
            "video_volume_up", "video_volume_down", "video_mute", "fullscreen"};
        int matched = -1;
        for (int i = 0; i < 8; ++i)
            if (m_shortcuts->matches(actions[i], keyEvent->key(), keyEvent->modifiers())) { matched = i; break; }
        if (matched < 0) return QObject::eventFilter(watched, event);
        keyEvent->accept();
        if (event->type() == QEvent::ShortcutOverride) return true;
        if (keyEvent->isAutoRepeat() && (matched < 2 || matched > 5)) return true;
        switch (matched) {
        case 0: emit escapePressed(); break;
        case 1: emit togglePlaybackPressed(); break;
        case 2: emit seekBackwardPressed(); break;
        case 3: emit seekForwardPressed(); break;
        case 4: emit volumeUpPressed(); break;
        case 5: emit volumeDownPressed(); break;
        case 6: emit mutePressed(); break;
        case 7: emit fullscreenPressed(); break;
        }
        return true;
    }
    const Qt::KeyboardModifiers modifiers = keyEvent->modifiers();
    if (modifiers.testFlag(Qt::ControlModifier)
            || modifiers.testFlag(Qt::AltModifier)
            || modifiers.testFlag(Qt::MetaModifier))
        return QObject::eventFilter(watched, event);

    // Claim playback keys before QML's window shortcuts. The viewer moves to
    // a separate window in fullscreen; shortcut matching there can otherwise
    // intercept Escape before a KeyPress reaches the player.
    if (event->type() == QEvent::ShortcutOverride) {
        switch (keyEvent->key()) {
        case Qt::Key_Escape:
        case Qt::Key_Space:
        case Qt::Key_Left:
        case Qt::Key_Right:
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_M:
        case Qt::Key_F11:
            keyEvent->accept();
            return true;
        default:
            return QObject::eventFilter(watched, event);
        }
    }

    switch (keyEvent->key()) {
    case Qt::Key_Escape:
        if (!keyEvent->isAutoRepeat()) emit escapePressed();
        return true;
    case Qt::Key_Space:
        if (!keyEvent->isAutoRepeat()) emit togglePlaybackPressed();
        return true;
    case Qt::Key_Left:
        emit seekBackwardPressed();
        return true;
    case Qt::Key_Right:
        emit seekForwardPressed();
        return true;
    case Qt::Key_Up:
        emit volumeUpPressed();
        return true;
    case Qt::Key_Down:
        emit volumeDownPressed();
        return true;
    case Qt::Key_M:
        if (!keyEvent->isAutoRepeat()) emit mutePressed();
        return true;
    case Qt::Key_F11:
        if (!keyEvent->isAutoRepeat()) emit fullscreenPressed();
        return true;
    default:
        return QObject::eventFilter(watched, event);
    }
}
