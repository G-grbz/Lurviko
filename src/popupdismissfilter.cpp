#include "popupdismissfilter.h"

#include <QMouseEvent>

static bool containsItem(QQuickItem *item, const QPointF &position)
{
    return item && item->isVisible() && item->contains(item->mapFromScene(position));
}

static bool containsPopup(QObject *popup, const QPointF &position)
{
    if (!popup || !popup->property("visible").toBool()) return false;
    auto *content = qobject_cast<QQuickItem *>(popup->property("contentItem").value<QObject *>());
    return content && containsItem(content->parentItem(), position);
}

void PopupDismissFilter::setWindow(QQuickWindow *window)
{
    if (m_window == window) return;
    if (m_window) m_window->removeEventFilter(this);
    m_window = window;
    m_pressedOutside = false;
    if (m_window) m_window->installEventFilter(this);
    emit windowChanged();
}

void PopupDismissFilter::setTargetPopup(QObject *popup)
{
    if (m_popup == popup) return;
    m_popup = popup;
    m_pressedOutside = false;
    emit targetPopupChanged();
}

void PopupDismissFilter::setOpenerItem(QQuickItem *item)
{
    if (m_opener == item) return;
    m_opener = item;
    emit openerItemChanged();
}

void PopupDismissFilter::setRelatedPopupObjects(const QVariantList &popups)
{
    m_related = popups;
    emit relatedPopupObjectsChanged();
}

bool PopupDismissFilter::contains(const QPointF &position) const
{
    if (containsPopup(m_popup, position) || containsItem(m_opener, position)) return true;
    for (const auto &related : m_related)
        if (containsPopup(related.value<QObject *>(), position)) return true;
    return false;
}

bool PopupDismissFilter::eventFilter(QObject *watched, QEvent *event)
{
    if (watched != m_window) return false;
    if (event->type() == QEvent::WindowDeactivate || event->type() == QEvent::UngrabMouse)
        m_pressedOutside = false;
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton && mouse->button() != Qt::RightButton && mouse->button() != Qt::MiddleButton)
        return false;
    const bool visible = m_popup && m_popup->property("visible").toBool();
    if (event->type() == QEvent::MouseButtonPress) {
        m_pressedOutside = visible && !contains(mouse->scenePosition());
    } else {
        // Dismiss on release so the exit animation cannot cancel a toolbar
        // button halfway through its press. The release still reaches Qt.
        const bool dismiss = m_pressedOutside && visible && !contains(mouse->scenePosition());
        m_pressedOutside = false;
        if (dismiss) {
            const char *method = m_popup->metaObject()->indexOfMethod("requestClose()") >= 0 ? "requestClose" : "close";
            QMetaObject::invokeMethod(m_popup, method);
        }
    }
    return false;
}
