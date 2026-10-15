#pragma once

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrent>

#ifdef __GLIBC__
#include <malloc.h>
#endif

namespace HeapMaintenance {
inline bool releaseUnusedHeap()
{
#ifdef __GLIBC__
#if __GLIBC_PREREQ(2, 33)
    // SVG decoding and large file listings leave free pages in allocator
    // arenas. Return those pages, retaining live objects and all image caches.
    const auto info = ::mallinfo2();
    if (info.fordblks >= 64u * 1024u * 1024u)
        return ::malloc_trim(0) != 0;
#endif
#endif
    return false;
}

class IdleReclaimer final : public QObject
{
public:
    explicit IdleReclaimer(QCoreApplication *application) : QObject(application)
    {
        m_idle.start();
        application->installEventFilter(this);
        m_timer.setInterval(30000);
        m_timer.setTimerType(Qt::VeryCoarseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            // Do not compete with active browsing/scrolling or repeatedly
            // scan the same arenas. Allocator locks/page reclamation run off
            // the GUI thread, at most once every two minutes while idle.
            if (m_idle.elapsed() < 5000 || m_watcher.isRunning()
                    || (m_lastAttempt.isValid() && m_lastAttempt.elapsed() < 120000))
                return;
            m_lastAttempt.start();
            m_watcher.setFuture(QtConcurrent::run([] { return releaseUnusedHeap(); }));
        });
        connect(application, &QCoreApplication::aboutToQuit, &m_timer, &QTimer::stop);
        m_timer.start();
    }

protected:
    bool eventFilter(QObject *, QEvent *event) override
    {
        switch (event->type()) {
        case QEvent::MouseMove:
        case QEvent::MouseButtonPress:
        case QEvent::Wheel:
        case QEvent::KeyPress:
        case QEvent::TouchBegin:
        case QEvent::TouchUpdate:
            m_idle.restart();
            break;
        default:
            break;
        }
        return false;
    }

private:
    QTimer m_timer;
    QElapsedTimer m_idle, m_lastAttempt;
    QFutureWatcher<bool> m_watcher;
};

inline void installIdleReclaimer(QCoreApplication *application)
{
#ifdef __GLIBC__
#if __GLIBC_PREREQ(2, 33)
    new IdleReclaimer(application);
#else
    Q_UNUSED(application);
#endif
#else
    Q_UNUSED(application);
#endif
}
}
