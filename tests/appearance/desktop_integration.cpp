#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QProxyStyle>
#include <QStyleFactory>
#include <QStyleOption>
#include <QPainter>
#include <QSettings>
#include <QTimer>
#include <QEventLoop>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include "backend_test_types.h"
#include <QtGui/qguiapplication_platform.h>
#if QT_CONFIG(xcb)
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#endif

static void require(bool ok, const char *message)
{
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static void pause(int ms)
{
    QEventLoop loop; QTimer::singleShot(ms, &loop, &QEventLoop::quit); loop.exec();
}
static void verifyNativeBlurRegion(QWindow *window, bool enabled)
{
#if QT_CONFIG(xcb)
    // Qt's offscreen plugin can expose this interface with a null Display.
    // X11 properties are only meaningful on the actual xcb platform.
    if (QGuiApplication::platformName() != QLatin1String("xcb")) return;
    const auto native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>();
    require(native && native->display(), "xcb platform exposes a live native display");
    auto *display = native->display();
    const Atom atom = XInternAtom(display, "_KDE_NET_WM_BLUR_BEHIND_REGION", False);
    Atom type; int format; unsigned long count, remaining; unsigned char *data = nullptr;
    XSync(display, False);
    require(XGetWindowProperty(display, window->winId(), atom, 0, 4096, False, XA_CARDINAL,
                &type, &format, &count, &remaining, &data) == Success, "native blur property readable");
    if (enabled) {
        require(type == XA_CARDINAL && format == 32 && count == 4 && remaining == 0,
                "KWin receives an explicit client rectangle instead of an empty whole-window sentinel");
        const auto values = reinterpret_cast<unsigned long *>(data);
        require(values[0] == 0 && values[1] == 0
                && values[2] == static_cast<unsigned long>(std::ceil(window->width() * qGuiApp->devicePixelRatio()))
                && values[3] == static_cast<unsigned long>(std::ceil(window->height() * qGuiApp->devicePixelRatio())),
                "native blur region covers the tab strip and complete client area at the current scale");
    } else {
        require(type == None, "native blur property is removed when Qt theme is disabled");
    }
    if (data) XFree(data);
#else
    Q_UNUSED(window); Q_UNUSED(enabled);
#endif
}
static QColor panelReference(QColor content, QColor base, QColor tint)
{
    QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
    image.fill(content);
    QPainter painter(&image);
    painter.fillRect(image.rect(), base);
    painter.fillRect(image.rect(), tint);
    painter.end();
    return image.pixelColor(0, 0);
}
class NativeTestStyle : public QProxyStyle
{
public:
    NativeTestStyle() : QProxyStyle(QStyleFactory::create("Fusion")) {}
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option,
                       QPainter *painter, const QWidget *widget = nullptr) const override
    {
        if (element == PE_PanelMenu) {
            painter->fillRect(option->rect, QColor(203, 220, 235, 204));
        } else if (element == PE_Widget) {
            QColor color = option->palette.color(QPalette::Window);
            color.setAlpha(153);
            painter->fillRect(option->rect, color);
        } else {
            QProxyStyle::drawPrimitive(element, option, painter, widget);
        }
    }
};
static void verifyPixels(QQuickWindow *window, QColor expected)
{
    const QImage frame = window->grabWindow();
    require(!frame.isNull(), "actual window frame available");
    const QColor pixel = frame.pixelColor(2, 2);
    std::fprintf(stderr, "FRAME: expected=%s actual=%s\n", qPrintable(expected.name(QColor::HexArgb)),
                 qPrintable(pixel.name(QColor::HexArgb)));
    require(qAbs(pixel.alpha() - expected.alpha()) <= 2, "window retains native alpha");
    require(qAbs(pixel.red() - expected.red()) <= 2 && qAbs(pixel.green() - expected.green()) <= 2
                && qAbs(pixel.blue() - expected.blue()) <= 2,
            "window retains native RGB without unpremultiplied clear-color overflow");
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("Lurviko-QA"); app.setApplicationName("DesktopAppearance");
    app.setQuitOnLastWindowClosed(false);
    QApplication::setStyle(new NativeTestStyle);
    QPalette palette = QApplication::palette();
    palette.setColor(QPalette::Window, QColor("#e4e7ec"));
    palette.setColor(QPalette::Base, QColor("#e4e7ec"));
    palette.setColor(QPalette::Text, QColor("#20252c"));
    palette.setColor(QPalette::WindowText, QColor("#20252c"));
    // Deliberately harsh bevel colors, as in some real Kvantum themes.
    palette.setColor(QPalette::Mid, Qt::black); palette.setColor(QPalette::Dark, Qt::black);
    QApplication::setPalette(palette);
    QQuickWindow::setDefaultAlphaBuffer(true); QQuickStyle::setStyle("Basic");
    const bool startCustom = app.arguments().contains(QStringLiteral("--start-custom"));
    QSettings settings; settings.setValue("Appearance/qtTheme", !startCustom); settings.sync();
    // REGISTER_BACKEND_TYPES
    QQmlApplicationEngine engine;
    engine.addImageProvider("gfilethumb", new ThumbnailProvider);
    engine.addImageProvider("systemicon", new SystemIconProvider);
    engine.addImageProvider("bundledicon", new BundledIconProvider);
    engine.addImageProvider("folderart", new FolderIconProvider);
    engine.loadFromModule("Lurviko.App", "Main");
    require(!engine.rootObjects().isEmpty(), "production Main loads");
    auto *root = engine.rootObjects().first();
    auto *window = qobject_cast<QQuickWindow *>(root);
    require(window, "production application window exists");
    pause(250);
    auto *chrome = root->findChild<QQuickItem *>("browserChromeHost");
    require(chrome && !chrome->isVisible(), "Discover starts without browser tabs or navigation chrome");
    auto *pageStack = root->findChild<QQuickItem *>("pageStack");
    require(pageStack, "production navigation stack exists");
    auto *homePage = qobject_cast<QQuickItem *>(pageStack->property("currentItem").value<QObject *>());
    auto *theme = engine.singletonInstance<QObject *>(qmlTypeId("Lurviko.App", 1, 0, "AppTheme"));
    require(theme, "application theme available");
    if (startCustom) {
        verifyPixels(window, QColor("#e9edf4"));
        require(QMetaObject::invokeMethod(theme, "setQtTheme", Q_ARG(QVariant, true)), "native mode enabled after opaque startup");
        pause(100);
    }
    require(theme->property("useQtTheme").toBool(), "native theme restored or enabled");
    const QColor nativeBackground = theme->property("background").value<QColor>();
    require(nativeBackground.alpha() == 153, "native style opacity read");
    require(window->color().alpha() == 0, "native window clears transparently");
    verifyPixels(window, nativeBackground);
    // Exercise the actual browser's menu, not a transparent rectangle fixture.
    require(QMetaObject::invokeMethod(root, "browse", Q_ARG(QVariant, QDir::homePath())), "browse action invoked");
    require(!pageStack->property("busy").toBool(),
            "leaving Discover settles the header layout without animating the outgoing page");
    pause(20);
    require(homePage && !homePage->isVisible(), "Discover is hidden before the browser layout is presented");
    pause(250);
    require(chrome->isVisible() && chrome->height() >= 76,
            "browser chrome appears above the workspace on first navigation");
    auto *unified = root->findChild<QQuickItem *>("unifiedBrowserSurface");
    auto *workspace = root->findChild<QQuickItem *>("workspaceShell");
    auto *sidebarPanel = root->findChild<QQuickItem *>("sidebarSharedPanel");
    require(unified && unified->isVisible() && workspace && sidebarPanel,
            "production browser has one shared navigation/sidebar/workspace surface");
    require(workspace->property("color").value<QColor>().alpha() == 0
                && sidebarPanel->property("color").value<QColor>().alpha() == 0,
            "embedded sidebar and workspace do not paint disconnected panels");
    auto *tabs = root->findChild<QQuickItem *>("browserTabsBar");
    auto *navigation = root->findChild<QQuickItem *>("browserNavigationHost");
    auto *commands = root->findChild<QQuickItem *>("browserCommandBar");
    require(tabs && navigation && commands && tabs->isVisible() && navigation->isVisible(),
            "production browser has visible tabs, navigation and commands");
    require(qAbs(unified->mapToScene(QPointF()).y() - navigation->mapToScene(QPointF()).y()) < 1,
            "common background starts exactly at the tab/navigation join");
    require(qAbs(tabs->width() - chrome->width()) < 1,
            "tabs span both sidebar and workspace");
    require(commands->mapToScene(QPointF()).y() >= navigation->mapToScene(QPointF()).y() + navigation->height(),
            "file commands sit below the full-width navigation row");
    QQuickItem *tabSurface = nullptr;
    const auto findTab = [&](const auto &self, QQuickItem *item) -> void {
        if (item->objectName() == QLatin1String("fluidTabSurface")) tabSurface = item;
        for (auto *child : item->childItems()) self(self, child);
    };
    findTab(findTab, tabs);
    require(tabSurface && tabSurface->property("selected").toBool(), "actual native active tab found");
    require(tabSurface->mapToScene(QPointF()).y() + tabSurface->property("capTop").toReal() <= 8,
            "tab faces start close to the top without excess header padding");
    const QColor tabFill = tabSurface->property("fillColor").value<QColor>();
    require(tabFill.alphaF() > 0.5 && tabFill.alphaF() < 0.8,
            "native active tabs have a stronger theme tint while retaining compositor translucency");
    require(tabSurface->property("outlineColor").value<QColor>().alphaF() > 0.6,
            "native active tab is distinguished by the desktop highlight outline");
    auto *effects = root->findChild<DesktopWindowEffects *>();
    require(effects && effects->window() == window && effects->enabled(), "KWin blur is requested for the complete native window including tabs");
    require(!tabSurface->findChild<QObject *>("fluidTabGlow"), "native tabs do not allocate a separate blur effect");
    verifyNativeBlurRegion(window, true);
    const auto originalSize = window->size();
    window->resize(originalSize + QSize(32, 16)); pause(60);
    verifyNativeBlurRegion(window, true);
    window->resize(originalSize); pause(60);
    require(QMetaObject::invokeMethod(root, "openContentCategory", Q_ARG(QVariant, QStringLiteral("archives")),
                Q_ARG(QVariant, QStringLiteral("Archives")), Q_ARG(QVariant, QStringLiteral("archive.svg"))),
            "category navigation opens the same browser chrome");
    pause(30);
    int visibleHeaders = 0;
    for (auto *header : root->findChildren<QQuickItem *>("browserHeader"))
        visibleHeaders += header->isVisible();
    require(visibleHeaders == 1, "only the current page header is visible during a stack transition");
    pause(220);
    require(QMetaObject::invokeMethod(root, "showHome"), "Discover navigation returns home");
    require(!pageStack->property("busy").toBool(),
            "returning to Discover changes chrome in one layout, without a resized outgoing browser");
    pause(200);
    require(!chrome->isVisible(), "Discover hides the shared header after browser navigation");
    require(homePage->isVisible() && homePage->opacity() == 1 && qAbs(homePage->x()) < 1,
            "Discover returns fully visible at its final position");
    require(QMetaObject::invokeMethod(root, "browse", Q_ARG(QVariant, QDir::homePath())), "browser reopens");
    pause(250);
    auto *menu = root->findChild<QObject *>("toolbarViewMenu");
    require(menu && QMetaObject::invokeMethod(menu, "open"), "actual View menu opens");
    pause(250);
    auto *menuSurface = qobject_cast<QQuickItem *>(menu->property("background").value<QObject *>());
    require(menuSurface && menuSurface->window() == window,
            "menu remains in its host window for stable placement");
    const QColor nativeMenu = menuSurface->property("nativeColor").value<QColor>();
    require(qAbs(nativeMenu.alpha() - 204) <= 2 && qAbs(nativeMenu.red() - 203) <= 2,
            "menu uses native menu color rather than toolbar tint");
    require(menuSurface->property("nativeBaseColor").value<QColor>() == nativeBackground,
            "embedded menu has its own sampled native window base");
    const bool rhi = qEnvironmentVariable("QT_QUICK_BACKEND") != QStringLiteral("software");
    require(menuSurface->property("blurActive").toBool() == rhi,
            "popup backdrop is blurred on GPU or solid on software rendering");
    // Under llvmpipe the first render can be slower than the animation's
    // nominal duration. Measure the settled panel, not a mid-fade frame.
    for (int i = 0; i < 150
         && (menuSurface->property("sceneOpacity").toDouble() < 0.999
             || qAbs(menu->property("scale").toDouble() - 1) > 0.001); ++i)
        pause(20);
    require(menuSurface->property("sceneOpacity").toDouble() >= 0.999,
            "menu opening animation completes before opacity is measured");
    const QPointF sample = menuSurface->mapToScene(QPointF(menuSurface->width() / 2, 4));
    const auto menuFrame = window->grabWindow();
    const auto menuPixel = menuFrame.pixelColor(qRound(sample.x() * window->devicePixelRatio()),
                                               qRound(sample.y() * window->devicePixelRatio()));
    require(menuPixel.alpha() > 200 && menuPixel.blue() > 180,
            "actual menu has a readable painted background");
    std::fprintf(stderr, "MENU: base=%s tint=%s actual=%s reference=%s\n",
                 qPrintable(nativeBackground.name(QColor::HexArgb)), qPrintable(nativeMenu.name(QColor::HexArgb)),
                 qPrintable(menuPixel.name(QColor::HexArgb)),
                 qPrintable(panelReference(Qt::transparent, nativeBackground, nativeMenu).name(QColor::HexArgb)));
    require(menuPixel.alpha() >= panelReference(Qt::transparent, nativeBackground, nativeMenu).alpha() - 2,
            "real menu opacity includes its native base rather than only the translucent menu tint");
    require(QMetaObject::invokeMethod(menu, "dismiss"), "native menu dismisses");
    pause(100);
    require(!menu->property("visible").toBool(), "menu actually closes");
    // A bounded checkerboard makes a broken/recursive or absent blur obvious.
    // Both source and surface are siblings; only the small surface is captured.
    QQmlComponent backdropFixture(&engine);
    backdropFixture.setData(R"qml(
import QtQuick
import Lurviko.App
Item {
    width: 160; height: 80; z: 100000
    // The application background is outside the captured content layer.
    // Transparent gaps between glyphs must not expose its sharp original.
    Rectangle { anchors.fill: parent; color: "white" }
    Item {
        id: stripes
        anchors.fill: parent
        Repeater {
            model: 80
            Rectangle { required property int index; x: index * 2; width: 2; height: 80; color: index % 2 ? "transparent" : "black" }
        }
    }
    GOverlaySurface {
        objectName: "blurFixture"; anchors.fill: parent; sourceItem: stripes
        nativeColor: Qt.rgba(0.5, 0.5, 0.5, 0.3)
    }
})qml", QUrl());
    auto *fixture = qobject_cast<QQuickItem *>(backdropFixture.create());
    require(fixture, "overlay pixel fixture loads");
    fixture->setParentItem(window->contentItem());
    auto *surface = fixture->findChild<QObject *>("blurFixture");
    pause(250);
    require(surface->property("blurActive").toBool() == rhi, "effect enabled only on supported renderer");
    const auto frame = window->grabWindow();
    const auto first = frame.pixelColor(qRound(70 * window->devicePixelRatio()), qRound(40 * window->devicePixelRatio()));
    const auto second = frame.pixelColor(qRound(72 * window->devicePixelRatio()), qRound(40 * window->devicePixelRatio()));
    require(qAbs(first.red() - second.red()) < 15,
            "surface actually blurs underlying high contrast stripes instead of exposing sharp text");
    if (rhi) {
        const QColor expected = panelReference(QColor(0, 0, 0, 128), nativeBackground,
                                               QColor(128, 128, 128, 77));
        require(qAbs(first.red() - expected.red()) < 8 && qAbs(first.alpha() - expected.alpha()) < 8,
                "blurred transparent content includes both sampled theme layers with Qt source-over color");
    }
    delete fixture;
    QQmlComponent movingFixture(&engine);
    movingFixture.setData(R"qml(
import QtQuick
import Lurviko.App
Item {
    x: 37; y: 29; width: 520; height: 330; z: 100000
    Item {
        id: tiles; objectName: "tiles"; width: 512; height: 300
        Rectangle { width: 256; height: 150; color: "red" }
        Rectangle { x: 256; width: 256; height: 150; color: "lime" }
        Rectangle { y: 150; width: 256; height: 150; color: "blue" }
        Rectangle { x: 256; y: 150; width: 256; height: 150; color: "yellow" }
    }
    Item {
        objectName: "overlayMount"; x: 40; y: 20; width: 100; height: 80
        transformOrigin: Item.TopLeft
        GOverlaySurface {
            objectName: "movingGlass"; x: 10; y: 10; width: 80; height: 60
            sourceItem: tiles; nativeColor: Qt.rgba(0.5, 0.5, 0.5, 0.3)
        }
    }
})qml", QUrl());
    auto *moving = qobject_cast<QQuickItem *>(movingFixture.create());
    require(moving, "moving overlay fixture loads");
    moving->setParentItem(window->contentItem());
    auto *mount = moving->findChild<QQuickItem *>("overlayMount");
    auto *glass = moving->findChild<QQuickItem *>("movingGlass");
    auto *tiles = moving->findChild<QQuickItem *>("tiles");
    auto verifyTile = [&](QColor content) {
        pause(100);
        if (!rhi) return;
        const auto image = window->grabWindow();
        const auto center = glass->mapToScene(QPointF(glass->width() / 2, glass->height() / 2));
        const auto pixel = image.pixelColor(qRound(center.x() * window->devicePixelRatio()),
                                           qRound(center.y() * window->devicePixelRatio()));
        const auto expected = panelReference(content, nativeBackground, QColor(128, 128, 128, 77));
        require(qAbs(pixel.red() - expected.red()) < 12 && qAbs(pixel.green() - expected.green()) < 12
                    && qAbs(pixel.blue() - expected.blue()) < 12,
                "blur samples the moved target and subdues opaque icons using the native panel base");
    };
    verifyTile(Qt::red);
    mount->setX(300); verifyTile(Qt::green);
    mount->setY(200); verifyTile(Qt::yellow);
    tiles->setX(200); verifyTile(Qt::blue);
    mount->setScale(0.75); pause(100);
    if (rhi) {
        const auto rect = glass->property("captureRect").toRectF();
        require(qAbs(rect.width() - glass->width() * 0.75) < 0.1
                    && qAbs(rect.height() - glass->height() * 0.75) < 0.1,
                "capture follows ancestor scale as well as translation");
    }
    delete moving;

    QQmlComponent alphaFixture(&engine);
    alphaFixture.setData(R"qml(
import QtQuick
import Lurviko.App
Item {
    width: 160; height: 80; z: 100000
    Rectangle { anchors.fill: parent; color: "white" }
    Item { id: empty; anchors.fill: parent }
    GOverlaySurface { objectName: "alphaSurface"; anchors.fill: parent; sourceItem: empty; nativeColor: Qt.rgba(0.5, 0.5, 0.5, 0.3) }
})qml", QUrl());
    auto *alpha = qobject_cast<QQuickItem *>(alphaFixture.create());
    require(alpha, "transparent backdrop fixture loads");
    alpha->setParentItem(window->contentItem());
    pause(150);
    auto verifyAlpha = [&](QColor base) {
        if (!rhi) return;
        const auto image = window->grabWindow();
        const auto pixel = image.pixelColor(qRound(70 * window->devicePixelRatio()), qRound(40 * window->devicePixelRatio()));
        const auto expected = panelReference(Qt::transparent, base, QColor(128, 128, 128, 77));
        require(qAbs(pixel.alpha() - expected.alpha()) <= 3 && pixel.alpha() < 255,
                "combined theme opacity is retained without forcing an opaque panel");
        require(qAbs(pixel.red() - expected.red()) <= 3 && qAbs(pixel.green() - expected.green()) <= 3
                    && qAbs(pixel.blue() - expected.blue()) <= 3,
                "panel color matches QPainter for light and dark native bases");
    };
    verifyAlpha(nativeBackground);
    auto *alphaSurface = alpha->findChild<QObject *>("alphaSurface");
    require(alphaSurface, "alpha surface available");
    const QColor darkBase(20, 25, 30, 153);
    alphaSurface->setProperty("nativeBaseColor", darkBase);
    pause(100);
    verifyAlpha(darkBase);
    delete alpha;
    require(theme->property("cardSurface").value<QColor>().alphaF() < 0.1
                && theme->property("panelSurface").value<QColor>().alphaF() < 0.1,
            "cards and panels do not hide the compositor backdrop");
    require(theme->property("border").value<QColor>().alphaF() < 0.2,
            "bevel palette colors are not used as opaque outlines");
    QQuickItem *player = nullptr;
    for (auto *item : root->findChildren<QQuickItem *>())
        if (item->metaObject()->indexOfProperty("sourceLibrary") >= 0) player = item;
    require(player, "shared music player found");
    player->setProperty("opened", true);
    require(QMetaObject::invokeMethod(root, "openDetachedMusic"), "detach action invoked");
    pause(200);
    QQuickWindow *detached = nullptr;
    for (auto *candidate : root->findChildren<QQuickWindow *>())
        if (candidate->isVisible()) detached = candidate;
    require(detached && detached->color().alpha() == 0, "detached window clears transparently");
    verifyPixels(detached, nativeBackground);
    require(QMetaObject::invokeMethod(theme, "setQtTheme", Q_ARG(QVariant, false)), "custom theme restored");
    pause(100);
    verifyPixels(window, QColor("#e9edf4"));
    verifyNativeBlurRegion(window, false);
    require(QMetaObject::invokeMethod(theme, "setQtTheme", Q_ARG(QVariant, true)), "native theme enabled live");
    pause(100);
    verifyPixels(window, nativeBackground);
    verifyNativeBlurRegion(window, true);
    std::fprintf(stderr, "PASS: real Main and detached native backdrop pixels, translucent cards, soft borders and live switching\n");
}
