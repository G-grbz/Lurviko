#pragma once

#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QQuickItem>
#include <QQuickWindow>
#include <QVariantList>

// Observe window events without accepting them or taking a mouse grab.
class PopupDismissFilter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQuickWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(QObject *targetPopup READ targetPopup WRITE setTargetPopup NOTIFY targetPopupChanged)
    Q_PROPERTY(QQuickItem *openerItem READ openerItem WRITE setOpenerItem NOTIFY openerItemChanged)
    Q_PROPERTY(QVariantList relatedPopupObjects READ relatedPopupObjects WRITE setRelatedPopupObjects NOTIFY relatedPopupObjectsChanged)
    // Opt in per opener. Mouse presses lost to an unrelated popup's overlay
    // can be completed after Qt Quick has had a chance to deliver onClicked.
    Q_PROPERTY(bool watchOpenerClicks READ watchOpenerClicks WRITE setWatchOpenerClicks NOTIFY watchOpenerClicksChanged)
public:
    explicit PopupDismissFilter(QObject *parent = nullptr) : QObject(parent) {}
    QQuickWindow *window() const { return m_window; }
    QObject *targetPopup() const { return m_popup; }
    QQuickItem *openerItem() const { return m_opener; }
    QVariantList relatedPopupObjects() const { return m_related; }
    bool watchOpenerClicks() const { return m_watchOpenerClicks; }
    void setWatchOpenerClicks(bool value) {
        if (m_watchOpenerClicks == value) return;
        m_watchOpenerClicks = value;
        m_openerPressed = false;
        emit watchOpenerClicksChanged();
    }
    void setWindow(QQuickWindow *window);
    void setTargetPopup(QObject *popup);
    void setOpenerItem(QQuickItem *item);
    void setRelatedPopupObjects(const QVariantList &popups);
signals:
    void windowChanged();
    void targetPopupChanged();
    void openerItemChanged();
    void relatedPopupObjectsChanged();
    void watchOpenerClicksChanged();
    void openerPressObserved();
    void openerClickReleased();
    void outsideClickReleased(const QPointF &globalPosition);
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool contains(const QPointF &position) const;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_popup;
    QPointer<QQuickItem> m_opener;
    QVariantList m_related;
    bool m_pressedOutside = false;
    QPointF m_outsidePressGlobal;
    bool m_watchOpenerClicks = false;
    bool m_openerPressed = false;
    QPointF m_openerPressGlobal;
};
