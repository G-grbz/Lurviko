#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QMouseEvent>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QSettings>
#include <QProcess>
#include <QTimer>
#include <cstdio>
#include <functional>
#include "backend_test_types.h"
static void require(bool ok, const char *message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec(); }
static bool until(const std::function<bool()> &check) { QElapsedTimer t;t.start();while(!check()&&t.elapsed()<15000)pause(10);return check(); }
static QVariant eval(QObject *item,const QString &code) { auto scope=item->findChild<MillerNavigationModel*>("millerNavigation");QQmlExpression expression(scope?qmlContext(scope):qmlContext(item),item,code);auto result=expression.evaluate();require(!expression.hasError(),qPrintable(expression.error().toString()));return result; }
static void invoke(QObject *item,const char *name,QVariant arg) { require(QMetaObject::invokeMethod(item,name,Q_ARG(QVariant,arg)),name); }
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setOrganizationName("Lurviko-QA");app.setApplicationName("Columns");QQuickStyle::setStyle("Basic");
    // REGISTER_BACKEND_TYPES
    const QString base=qEnvironmentVariable("LURVIKO_NAVIGATION_TEST_ROOT")+"/fixture";
    require(QDir().mkpath(base+"/A/nested"),"nested fixture");require(QDir().mkpath(base+"/B"),"sibling fixture");
    for(int i=0;i<3000;i++){QFile f(base+QString("/A/track-%1.txt").arg(i,4,10,QChar('0')));require(f.open(QIODevice::WriteOnly),"large fixture entry");f.write("content\n");}
    QFile hidden(base+"/.hidden");require(hidden.open(QIODevice::WriteOnly),"hidden fixture");hidden.write("hidden");hidden.close();
    QFile moved(base+"/copy-source.txt");require(moved.open(QIODevice::WriteOnly),"copy fixture");moved.write("preserve");moved.close();
    QProcess git; git.start("git", {"-C", base, "init", "-q"});
    require(git.waitForFinished() && git.exitCode() == 0, "Git integration fixture initializes");
    QQmlApplicationEngine engine;engine.addImageProvider("gfilethumb",new ThumbnailProvider);engine.addImageProvider("systemicon",new SystemIconProvider);engine.addImageProvider("bundledicon",new BundledIconProvider);engine.addImageProvider("folderart",new FolderIconProvider);
    engine.rootContext()->setContextProperty("fixturePath",base);
    engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import Lurviko.App
import Lurviko.Backend
ApplicationWindow {
 width:1400;height:900;visible:true
 LanguageManager{id:language} FavoritesModel{id:favorites} QuickAccessModel{id:quickAccess} AdminEditManager{id:admin}
 QtObject{id:auth;property string googleAccessToken:"";property string oneDriveAccessToken:""}
 QtObject{id:prefs} QtObject{id:google;property string accessToken:""} QtObject{id:one;property string accessToken:""}
 BrowsePage { objectName:"page";anchors.fill:parent;lang:language;contentIndexModel:null;musicPlayer:null;favoritesModel:favorites;quickAccessModel:quickAccess;adminEditor:admin;cloudAuth:auth;cloudIntegrationPreferences:prefs;googleDrive:google;oneDrive:one;initialLocation:fixturePath }
}
)qml");
    require(!engine.rootObjects().isEmpty(),"BrowsePage loads");auto root=engine.rootObjects().first();auto page=root->findChild<QQuickItem*>("page");
    auto pane=qobject_cast<QQuickItem*>(page->property("activePane").value<QObject*>());require(pane,"active pane");
    pane->setProperty("viewMode","columns");pause(100);
    auto navigation=pane->findChild<MillerNavigationModel*>("millerNavigation");require(navigation&&navigation->enabled(),"columns enabled");
    const bool initialReady = until([&]{return !navigation->activeDirectory()->loading()&&navigation->activeDirectory()->rowCount()>=3;});
    if (!initialReady) {
        std::fprintf(stderr,"DEBUG location=%s expected=%s count=%d loading=%d error=%s current=%s mode=%s\n",qPrintable(navigation->location()),qPrintable(base),navigation->activeDirectory()->rowCount(),navigation->activeDirectory()->loading(),qPrintable(navigation->activeDirectory()->errorString()),qPrintable(pane->property("currentLocation").toString()),qPrintable(pane->property("viewMode").toString()));
    }
    require(initialReady,"initial folder loads");
    require(until([&] { return navigation->activeDirectory()->itemAt(navigation->activeDirectory()->indexOfUrl(base+"/A")).value("gitStatus")=="untracked"; }), "directory role receives asynchronous Git status");
    std::function<bool(QQuickItem*)> findBadge = [&](QQuickItem *item) {
        if (item->objectName()=="gitStatusBadge" && item->isVisible() && item->property("status")=="untracked") return true;
        for (auto *child : item->childItems()) if (findBadge(child)) return true;
        return false;
    };
    auto hasGitBadge = [&] { return findBadge(pane); };
    require(until(hasGitBadge), "Miller displays Git badge without replacing icon");
    pane->setProperty("viewMode", "grid"); require(until(hasGitBadge), "Grid displays Git badge");
    pane->setProperty("viewMode", "list"); require(until(hasGitBadge), "List displays Git badge");
    pane->setProperty("viewMode", "columns");
    navigation->activeDirectory()->setShowHidden(false);
    require(until([&]{return !navigation->activeDirectory()->loading();}),"hidden filter settles");
    auto select=[&](const QString &url){const int row=navigation->activeDirectory()->indexOfUrl(url);require(row>=0,"selection target exists");require(QMetaObject::invokeMethod(pane,"selectIndex",Q_ARG(QVariant,QVariant(row)),Q_ARG(QVariant,QVariant(int(Qt::NoModifier)))),"select item");};
    select(base+"/A");
    require(until([&]{return navigation->rowCount()==2&&navigation->directoryAt(1)&&!navigation->directoryAt(1)->loading();}),"selecting folder opens right column asynchronously");
    require(navigation->activeColumn()==0&&navigation->directoryAt(1)->rowCount()==3001,"large child listed without moving keyboard column");
    int heartbeats=0;QTimer heartbeat;QObject::connect(&heartbeat,&QTimer::timeout,&app,[&]{++heartbeats;});heartbeat.start(5);
    eval(pane,"root.moveSelection('right',false)");
    require(navigation->activeColumn()==1&&pane->property("currentLocation").toString()==QUrl::fromLocalFile(base+"/A").toString(),"Right enters selected child");
    select(base+"/A/track-0000.txt");pause(100);
    require(eval(pane,"millerView.previewVisible").toBool(),"file selection enables bounded preview");
    eval(pane,"root.moveSelection('down',false)");require(pane->property("selectedName").toString()=="track-0001.txt","Down moves one row");
    eval(pane,"root.moveSelection('up',false)");require(pane->property("selectedName").toString()=="track-0000.txt","Up moves one row");
    const auto columnSelection=pane->property("selectedUrl");
    const auto columnModel=navigation->activeDirectory();
    pane->setProperty("viewMode","list");
    require(navigation->activeDirectory()==columnModel&&pane->property("selectedUrl")==columnSelection,"changing view retains directory and selection");
    pane->setProperty("viewMode","columns");
    eval(pane,"root.moveSelection('left',false)");require(navigation->activeColumn()==0,"Left returns to previous column");
    select(base+"/B");require(until([&]{return navigation->rowCount()==2&&navigation->locationAt(1).endsWith("/B");}),"sibling selection replaces only right branch");
    require(navigation->activeDirectory()->rowCount()==3,"hidden item excluded");
    pane->setProperty("showHiddenFiles",true);require(until([&]{return navigation->activeDirectory()->rowCount()==5;}),"hidden setting reaches columns");
    navigation->setColumnWidth(0,360,true);require(navigation->data(navigation->index(0),MillerNavigationModel::WidthRole).toInt()==360,"column resizes");
    require(QSettings().value("BrowserView/millerColumnWidth").toInt()==360,"column width persists");
    navigation->activeDirectory()->setSortMode("name");navigation->activeDirectory()->setSortAscending(false);
    require(navigation->directoryAt(1)->sortAscending()==false,"sort options propagate to child models");
    // Shared FileOperations, not a second implementation, handles column drops.
    eval(pane,"root.dropUrls(["+QString("'%1'").arg(QUrl::fromLocalFile(base+"/copy-source.txt").toString())+"], '"+QUrl::fromLocalFile(base+"/B").toString()+"', true)");
    require(until([&]{return QFileInfo::exists(base+"/B/copy-source.txt");}),"column drop routes through real KIO copy");
    require(QFileInfo::exists(base+"/copy-source.txt"),"copy preserves source");
    require(heartbeats>0,"event loop remains responsive with large column");
    auto window=qobject_cast<QQuickWindow*>(root);require(window,"test window exists");
    QFile incoming(base+"/incoming.txt");require(incoming.open(QIODevice::WriteOnly),"external drop fixture opens");incoming.write("native drop");incoming.close();
    auto targetRow=qobject_cast<QQuickItem*>(eval(pane,"millerView.currentListView.itemAtIndex(directory.indexOfUrl('"+QUrl::fromLocalFile(base+"/B").toString()+"'))").value<QObject*>());
    require(targetRow,"drop target row visible");
    const auto dropPoint=targetRow->mapToScene(QPointF(40,targetRow->height()/2));
    QMimeData mime;mime.setUrls({QUrl::fromLocalFile(incoming.fileName())});
    QDragEnterEvent enter(dropPoint.toPoint(),Qt::CopyAction,&mime,Qt::LeftButton,Qt::ControlModifier);
    QDropEvent drop(dropPoint,Qt::CopyAction,&mime,Qt::LeftButton,Qt::ControlModifier);
    app.sendEvent(window,&enter);app.sendEvent(window,&drop);
    require(until([&]{return QFileInfo::exists(base+"/B/incoming.txt");}),"native URI drop reaches Miller DropArea and KIO");
    select(base+"/copy-source.txt");pause(150);
    auto delegate=qobject_cast<QQuickItem*>(eval(pane,"millerView.currentListView.itemAtIndex(root.primaryIndex)").value<QObject*>());
    require(delegate,"visible column row exists");
    const auto scene=delegate->mapToScene(QPointF(30,delegate->height()/2));
    const QPointF global=window->mapToGlobal(scene.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,scene,global,Qt::RightButton,Qt::RightButton,Qt::NoModifier);
    QMouseEvent release(QEvent::MouseButtonRelease,scene,global,Qt::RightButton,Qt::NoButton,Qt::NoModifier);
    app.sendEvent(window,&press);app.sendEvent(window,&release);
    require(until([&]{return eval(pane,"itemContextMenu.visible").toBool();}),"right click opens shared item context menu");
    eval(pane,"root.renameSelection()");
    require(until([&]{return eval(pane,"root.inlineRenameEditor && root.inlineRenameEditor.activeFocus").toBool();}),"shared rename uses inline Miller editor");
    eval(pane,"root.commitInlineRename(root.selectedUrl,'renamed.txt')");
    require(until([&]{return QFileInfo::exists(base+"/renamed.txt")&&pane->property("selectedName").toString()=="renamed.txt";}),"KIO rename restores selection in column");
    // Restore an active column through the existing tab machinery.
    const QString rememberedLocation = pane->property("currentLocation").toString();
    require(QMetaObject::invokeMethod(page,"addTab",Q_ARG(QVariant,QVariant(base+"/A")),Q_ARG(QVariant,QVariant(true))),"new tab opens");
    require(until([&]{return page->property("currentTab").toInt()==1&&!navigation->activeDirectory()->loading()
        && pane->property("currentLocation").toString().endsWith("/A");}),"new tab uses Miller active model");
    require(QMetaObject::invokeMethod(page,"switchToTab",Q_ARG(QVariant,QVariant(0)),Q_ARG(QVariant,QVariant(false))),"switchToTab");
    const bool tabRestored=until([&]{return page->property("currentTab").toInt()==0&&!navigation->activeDirectory()->loading()
        && pane->property("currentLocation").toString()==rememberedLocation;});
    if (!tabRestored) { std::fprintf(stderr,"DEBUG tab=%d activeColumn=%d location=%s expected=%s loading=%d count=%d\n",page->property("currentTab").toInt(),navigation->activeColumn(),qPrintable(navigation->location()),qPrintable(rememberedLocation),navigation->activeDirectory()->loading(),navigation->activeDirectory()->rowCount());
        QQmlExpression tabs(qmlContext(page),page,"JSON.stringify(tabs)");std::fprintf(stderr,"TABS %s\n",qPrintable(tabs.evaluate().toString())); }
    require(tabRestored,"original tab location restored");
    page->setProperty("splitEnabled",true);
    require(until([&]{return root->findChildren<MillerNavigationModel*>().size()==2;}),"split pane owns independent Miller navigation");
    MillerNavigationModel *other=nullptr;
    for(auto candidate:root->findChildren<MillerNavigationModel*>()) if(candidate!=navigation) other=candidate;
    require(other&&other->enabled(),"new pane restores Columns preference");
    other->setLocation(base+"/B");
    require(navigation->location()==rememberedLocation&&other->location().endsWith("/B"),"split panes navigate independently");
    page->setProperty("splitEnabled",false);pause(100);
    invoke(pane,"navigateTo",base+"/A");
    require(until([&]{return !navigation->activeDirectory()->loading();}),"child column ready for search");
    eval(pane,"root.startSearch('track-2999',false)");
    require(until([&]{return navigation->activeDirectory()->searchActive()&&!navigation->activeDirectory()->loading()
        &&navigation->activeDirectory()->rowCount()==1;}),"built-in search from column completes");
    require(navigation->activeDirectory()==navigation->sourceDirectory(),"search sessions use persistent model");
    eval(pane,"root.closeSearch()");
    require(until([&]{return !navigation->activeDirectory()->loading()&&navigation->activeDirectory()->rowCount()==3001;}),"closing search restores column directory");
    pane->setProperty("viewMode","list");require(navigation->activeDirectory()==navigation->sourceDirectory(),"List restores original directory model");
    pane->setProperty("viewMode","grid");require(pane->property("gridMode").toBool(),"Grid remains available");
    pane->setProperty("viewMode","columns");require(until([&]{return QSettings().value("BrowserView/viewMode").toString()=="columns";}),"view preference saved");
    // A collection of sibling projects is not itself a Git repository.
    // The repository folder must still receive its aggregate badge from here.
    invoke(pane,"navigateTo",QFileInfo(base).absolutePath());
    require(until([&] {
        auto *model=navigation->activeDirectory();
        const int row=model->indexOfUrl(base);
        return !model->loading() && row>=0 && model->itemAt(row).value("gitStatus")=="untracked";
    }),"repository root has status when seen from its non-repository parent");
    require(until(hasGitBadge),"Miller shows sibling repository status");
    pane->setProperty("viewMode","grid");require(until(hasGitBadge),"Grid shows sibling repository status");
    pane->setProperty("viewMode","list");require(until(hasGitBadge),"List shows sibling repository status");
    std::fprintf(stderr,"PASS: Miller folder/file selection, keyboard navigation, large directory, preview, hidden/sort, resize persistence, real KIO drop and Grid/List compatibility\n");
}
