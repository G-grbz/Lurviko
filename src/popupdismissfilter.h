#pragma once

#include <QObject>
#include <QPointer>
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
public:
    explicit PopupDismissFilter(QObject *parent = nullptr) : QObject(parent) {}
    QQuickWindow *window() const { return m_window; }
    QObject *targetPopup() const { return m_popup; }
    QQuickItem *openerItem() const { return m_opener; }
    QVariantList relatedPopupObjects() const { return m_related; }
    void setWindow(QQuickWindow *window);
    void setTargetPopup(QObject *popup);
    void setOpenerItem(QQuickItem *item);
    void setRelatedPopupObjects(const QVariantList &popups);
signals:
    void windowChanged();
    void targetPopupChanged();
    void openerItemChanged();
    void relatedPopupObjectsChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    bool contains(const QPointF &position) const;
    QPointer<QQuickWindow> m_window;
    QPointer<QObject> m_popup;
    QPointer<QQuickItem> m_opener;
    QVariantList m_related;
    bool m_pressedOutside = false;
};
