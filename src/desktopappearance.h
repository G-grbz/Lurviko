#pragma once

#include <QColor>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QVariantMap>
#include <QWindow>

// Read the QApplication palette, never a QML window's overridden palette.
class DesktopAppearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QVariantMap colors READ colors NOTIFY changed)
    Q_PROPERTY(QColor windowBackground READ windowBackground NOTIFY changed)
    Q_PROPERTY(QColor menuBackground READ menuBackground NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)
    Q_PROPERTY(QString styleName READ styleName NOTIFY changed)
public:
    explicit DesktopAppearance(QObject *parent = nullptr);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QVariantMap colors() const { return m_colors; }
    QColor windowBackground() const { return m_background; }
    QColor menuBackground() const { return m_menuBackground; }
    bool dark() const;
    QString styleName() const { return m_styleName; }
    Q_INVOKABLE void refresh();
signals:
    void enabledChanged();
    void changed();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    QVariantMap m_colors;
    QColor m_background;
    QColor m_menuBackground;
    QString m_styleName;
    QTimer m_refreshTimer;
    bool m_enabled = false;
    bool m_refreshing = false;
};

// Per-window lifetime and native-surface tracking; no polling or window opacity
// changes. KWin's own opacity rules continue to be applied by the compositor.
class DesktopWindowEffects : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QWindow *window READ window WRITE setWindow NOTIFY windowChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(qreal cornerRadius READ cornerRadius WRITE setCornerRadius NOTIFY cornerRadiusChanged)
public:
    explicit DesktopWindowEffects(QObject *parent = nullptr);
    ~DesktopWindowEffects() override;
    QWindow *window() const { return m_window; }
    void setWindow(QWindow *window);
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    qreal cornerRadius() const { return m_cornerRadius; }
    void setCornerRadius(qreal radius);
signals:
    void windowChanged();
    void enabledChanged();
    void cornerRadiusChanged();
protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
private:
    void update();
    QPointer<QWindow> m_window;
    bool m_enabled = false;
    qreal m_cornerRadius = 0;
};
