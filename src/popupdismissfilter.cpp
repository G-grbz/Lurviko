#include "popupdismissfilter.h"

#include <QMouseEvent>
#include <QLineF>

static bool containsItem(QQuickItem *item, const QPointF &position)
{
    // Menus can live in their own Qt popup window in desktop-theme mode.
    // Compare global positions so each item uses its own window's coordinates.
    return item && item->isVisible() && item->contains(item->mapFromGlobal(position));
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
    m_openerPressed = false;
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
    m_openerPressed = false;
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
    // Closing a nested Qt Popup can emit UngrabMouse during the same click;
    // keep the pending release until it reaches this window. Cancel only when
    // the whole window deactivates.
    if (event->type() == QEvent::WindowDeactivate) {
        m_pressedOutside = false;
        m_openerPressed = false;
    }
    if (event->type() != QEvent::MouseButtonPress && event->type() != QEvent::MouseButtonRelease)
        return false;
    auto *mouse = static_cast<QMouseEvent *>(event);
    if (mouse->button() != Qt::LeftButton && mouse->button() != Qt::RightButton && mouse->button() != Qt::MiddleButton)
        return false;
    const bool visible = m_popup && m_popup->property("visible").toBool();
    const auto closePopup = [](QObject *popup) {
        if (!popup || !popup->property("visible").toBool()) return;
        const char *method = popup->metaObject()->indexOfMethod("requestClose()") >= 0
                ? "requestClose" : "close";
        QMetaObject::invokeMethod(popup, method);
    };
    const auto closeRelated = [this, &closePopup]() {
        for (const auto &related : m_related)
            closePopup(related.value<QObject *>());
    };
    if (event->type() == QEvent::MouseButtonPress) {
        m_openerPressed = m_watchOpenerClicks && mouse->button() == Qt::LeftButton
                && containsItem(m_opener, mouse->globalPosition())
                && m_opener->isEnabled();
        if (m_openerPressed) {
            m_openerPressGlobal = mouse->globalPosition();
            emit openerPressObserved();
        }
        const bool outside = visible && !contains(mouse->globalPosition());
        m_pressedOutside = outside && mouse->button() == Qt::LeftButton;
        if (m_pressedOutside)
            m_outsidePressGlobal = mouse->globalPosition();
        if (!outside) return false;
        if (m_popup && m_popup->metaObject()->indexOfMethod("prepareOutsideDismiss()") >= 0)
            QMetaObject::invokeMethod(m_popup, "prepareOutsideDismiss");
        // Do not split the close sequence across mouse press and release.
        // While the parent remains visible, Qt can still direct the toolbar's
        // release into the old popup (or reactivate a hover submenu). Close
        // the entire family before dispatching the outside press, without
        // consuming it. This also allows the same right press to open the
        // context menu for another file without a stale release callback.
        closeRelated();
        closePopup(m_popup);
    } else {
        const bool openerReleased = m_openerPressed && mouse->button() == Qt::LeftButton
                && containsItem(m_opener, mouse->globalPosition())
                && m_opener->isEnabled()
                && QLineF(m_openerPressGlobal, mouse->globalPosition()).length() < 8;
        m_openerPressed = false;
        if (openerReleased)
            emit openerClickReleased();
        // Qt's Popup.Item overlay sometimes consumes an outside click before
        // a toolbar button's onClicked handler sees it. Notify the QML owner
        // after the release; the owner defers its fallback until after normal
        // event dispatch and only opens a target that did *not* open naturally.
        const bool wasOutsideClick = m_pressedOutside && mouse->button() == Qt::LeftButton
                && QLineF(m_outsidePressGlobal, mouse->globalPosition()).length() < 8;
        m_pressedOutside = false;
        if (wasOutsideClick)
            emit outsideClickReleased(mouse->globalPosition());
    }
    return false;
}
