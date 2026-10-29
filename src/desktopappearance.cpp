#include "desktopappearance.h"

#include <QApplication>
#include <QEvent>
#include <QImage>
#include <QMainWindow>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlatformSurfaceEvent>
#include <QStyle>
#include <QStyleOption>
#include <KWindowEffects>

DesktopAppearance::DesktopAppearance(QObject *parent) : QObject(parent)
{
    m_refreshTimer.setSingleShot(true);
    connect(&m_refreshTimer, &QTimer::timeout, this, &DesktopAppearance::refresh);
    qApp->installEventFilter(this);
    refresh();
}

bool DesktopAppearance::eventFilter(QObject *, QEvent *event)
{
    if (!m_refreshing && (event->type() == QEvent::ApplicationPaletteChange
                         || event->type() == QEvent::StyleChange))
        m_refreshTimer.start(0);
    return false;
}

void DesktopAppearance::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    refresh();
    emit enabledChanged();
}

bool DesktopAppearance::dark() const
{
    const QColor window = m_colors.value(QStringLiteral("window")).value<QColor>();
    const QColor text = m_colors.value(QStringLiteral("windowText")).value<QColor>();
    return window.lightnessF() < text.lightnessF();
}

void DesktopAppearance::refresh()
{
    if (m_refreshing)
        return;
    m_refreshing = true;
    const QPalette palette = QApplication::palette();
    QVariantMap colors;
    const auto add = [&](const char *name, QPalette::ColorRole role) {
        colors.insert(QLatin1String(name), palette.color(QPalette::Active, role));
    };
    add("window", QPalette::Window);
    add("windowText", QPalette::WindowText);
    add("base", QPalette::Base);
    add("alternateBase", QPalette::AlternateBase);
    add("text", QPalette::Text);
    add("button", QPalette::Button);
    add("buttonText", QPalette::ButtonText);
    add("highlight", QPalette::Highlight);
    add("highlightedText", QPalette::HighlightedText);
    add("mid", QPalette::Mid);
    add("dark", QPalette::Dark);
    add("placeholderText", QPalette::PlaceholderText);
    add("toolTipBase", QPalette::ToolTipBase);
    add("toolTipText", QPalette::ToolTipText);
    add("menuText", QPalette::WindowText);
    colors.insert(QStringLiteral("disabledText"), palette.color(QPalette::Disabled, QPalette::Text));
    colors.insert(QStringLiteral("menuDisabledText"), palette.color(QPalette::Disabled, QPalette::WindowText));

    QColor background = palette.color(QPalette::Active, QPalette::Window);
    QColor menuBackground = palette.color(QPalette::Active, QPalette::Window);
    if (m_enabled) {
        // Some QWidget styles (notably Kvantum) paint window translucency from
        // their SVG, while QPalette::Window stays opaque. Ask the actual style
        // to paint a tiny hidden window. No native handle, show(), theme-file
        // parsing, screenshot or per-frame rendering is needed. Styles without
        // PE_Widget painting (Fusion/Breeze) fall back to the application palette.
        QMainWindow proxy;
        proxy.resize(64, 64);
        proxy.ensurePolished();
        proxy.style()->styleHint(QStyle::SH_Widget_Animate, nullptr, &proxy);
        QStyleOption option;
        option.initFrom(&proxy);
        option.rect = QRect(0, 0, 64, 64);
        option.state |= QStyle::State_Active;
        QImage image(64, 64, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        proxy.style()->drawPrimitive(QStyle::PE_Widget, &option, &painter, &proxy);
        painter.end();
        const QColor sampled = image.pixelColor(32, 32);
        if (sampled.alpha() > 0)
            background = sampled;

        // Menus have their own native painting/opacity (not the application's
        // toolbar tint). Sample PE_PanelMenu with a real, never-shown QMenu.
        QMenu menu;
        menu.ensurePolished();
        menu.style()->styleHint(QStyle::SH_Menu_MouseTracking, nullptr, &menu);
        QStyleOption menuOption;
        menuOption.initFrom(&menu);
        menuOption.rect = image.rect();
        menuOption.state |= QStyle::State_Active;
        image.fill(Qt::transparent);
        QPainter menuPainter(&image);
        menu.style()->drawPrimitive(QStyle::PE_PanelMenu, &menuOption, &menuPainter, &menu);
        menuPainter.end();
        const QColor menuSample = image.pixelColor(32, 32);
        if (menuSample.alpha() > 0)
            menuBackground = menuSample;
        colors.insert(QStringLiteral("menuText"), menu.palette().color(QPalette::Active, QPalette::WindowText));
        colors.insert(QStringLiteral("menuDisabledText"), menu.palette().color(QPalette::Disabled, QPalette::WindowText));
    }
    const QString styleName = QApplication::style()->objectName();
    const bool different = colors != m_colors || background != m_background
        || menuBackground != m_menuBackground || styleName != m_styleName;
    m_colors = colors;
    m_background = background;
    m_menuBackground = menuBackground;
    m_styleName = styleName;
    m_refreshing = false;
    if (different)
        emit changed();
}

DesktopWindowEffects::DesktopWindowEffects(QObject *parent) : QObject(parent) {}

DesktopWindowEffects::~DesktopWindowEffects()
{
    setEnabled(false);
}

void DesktopWindowEffects::setWindow(QWindow *window)
{
    if (m_window == window)
        return;
    if (m_window) {
        const bool enabled = m_enabled;
        m_enabled = false;
        update();
        m_enabled = enabled;
        m_window->removeEventFilter(this);
    }
    m_window = window;
    if (m_window)
        m_window->installEventFilter(this);
    update();
    emit windowChanged();
}

void DesktopWindowEffects::setEnabled(bool enabled)
{
    if (m_enabled == enabled)
        return;
    m_enabled = enabled;
    update();
    emit enabledChanged();
}

void DesktopWindowEffects::setCornerRadius(qreal radius)
{
    radius = qMax(qreal(0), radius);
    if (qFuzzyCompare(m_cornerRadius, radius)) return;
    m_cornerRadius = radius;
    update();
    emit cornerRadiusChanged();
}

bool DesktopWindowEffects::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::PlatformSurface
        && static_cast<QPlatformSurfaceEvent *>(event)->surfaceEventType() == QPlatformSurfaceEvent::SurfaceCreated)
        QTimer::singleShot(0, this, &DesktopWindowEffects::update);
    else if (event->type() == QEvent::Expose || event->type() == QEvent::Resize)
        update();
    return false;
}

void DesktopWindowEffects::update()
{
    // Avoid creating a native surface early (in particular on Wayland).
    if (!m_window || !m_window->handle())
        return;
    const QString platform = QGuiApplication::platformName();
    if (platform != QLatin1String("xcb") && !platform.startsWith(QLatin1String("wayland")))
        return;
    QRegion region;
    if (m_enabled) {
        if (m_window->size().isEmpty()) return;
        // Send bounded logical client coordinates, including the tab strip.
        // An empty "whole window" region becomes an enormous rectangle on
        // ext-background-effect; avoid that sentinel for native/custom effects.
        if (m_cornerRadius > 0) {
            QPainterPath path;
            path.addRoundedRect(QRectF(QPointF(), m_window->size()), m_cornerRadius, m_cornerRadius);
            region = QRegion(path.toFillPolygon().toPolygon());
        } else {
            region = QRegion(QRect(QPoint(), m_window->size()));
        }
    }
    KWindowEffects::enableBlurBehind(m_window, m_enabled, region);
}
