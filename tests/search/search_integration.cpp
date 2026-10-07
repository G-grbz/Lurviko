
#include "admineditmanager.h"
#include "cloudauthmanager.h"
#include "contentindexmodel.h"
#include "directorymodel.h"
#include "favoritesmodel.h"
#include "filemanager1interface.h"
#include "fileoperations.h"
#include "filepropertiesmanager.h"
#include "googledrivemanager.h"
#include "iconpickermanager.h"
#include "languagemanager.h"
#include "onedrivemanager.h"
#include "openwithmodel.h"
#include "quickaccessmodel.h"
#include "servicemenumodel.h"
#include "storagemodel.h"
#include "thumbnailprovider.h"
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSet>
#include <QStandardPaths>
#include <QTimer>
#include <QtQml>
#include <cstdio>
#include <functional>

static void pause(int ms) {
  QEventLoop loop;
  QTimer::singleShot(ms, &loop, &QEventLoop::quit);
  loop.exec();
}
static bool until(std::function<bool()> check, int timeout = 30000) {
  QElapsedTimer time;
  time.start();
  while (!check() && time.elapsed() < timeout)
    pause(10);
  return check();
}
int main(int argc, char **argv) {
  QApplication app(argc, argv);
  app.setOrganizationName("g-File-QA");
  app.setApplicationName("Search");
  QQuickStyle::setStyle("Basic");
  const QString base =
      QString::fromLocal8Bit(qgetenv("GFILE_SEARCH_TEST_ROOT")) + "/fixture";
  const int expected = 20000;
  for (int d = 0; d < 100; d++) {
    const QString folder = base + QString("/folder-%1").arg(d);
    QDir().mkpath(folder);
    for (int i = 0; i < 200; i++) {
      const QString path = folder + QString("/match-%1.dat").arg(i);
      if (QFile::exists(path))
        continue;
      QFile f(path);
      if (!f.open(QIODevice::WriteOnly))
        return 1;
      f.write("fixture");
    }
  }
  std::fprintf(stderr, "FIXTURE_READY\n");
  qmlRegisterType<StorageModel>("GFile.Backend", 1, 0, "StorageModel");
  qmlRegisterType<DirectoryModel>("GFile.Backend", 1, 0, "DirectoryModel");
  qmlRegisterType<FileOperations>("GFile.Backend", 1, 0, "FileOperations");
  qmlRegisterType<FavoritesModel>("GFile.Backend", 1, 0, "FavoritesModel");
  qmlRegisterType<QuickAccessModel>("GFile.Backend", 1, 0, "QuickAccessModel");
  qmlRegisterType<ContentIndexModel>("GFile.Backend", 1, 0,
                                     "ContentIndexModel");
  qmlRegisterType<LanguageManager>("GFile.Backend", 1, 0, "LanguageManager");
  qmlRegisterType<CloudAuthManager>("GFile.Backend", 1, 0, "CloudAuthManager");
  qmlRegisterType<AdminEditManager>("GFile.Backend", 1, 0, "AdminEditManager");
  qmlRegisterType<OpenWithModel>("GFile.Backend", 1, 0, "OpenWithModel");
  qmlRegisterType<ServiceMenuModel>("GFile.Backend", 1, 0, "ServiceMenuModel");
  qmlRegisterType<GoogleDriveManager>("GFile.Backend", 1, 0,
                                      "GoogleDriveManager");
  qmlRegisterType<OneDriveManager>("GFile.Backend", 1, 0, "OneDriveManager");
  qmlRegisterType<IconPickerManager>("GFile.Backend", 1, 0,
                                     "IconPickerManager");
  qmlRegisterType<FilePropertiesManager>("GFile.Backend", 1, 0,
                                         "FilePropertiesManager");

  QQmlApplicationEngine engine;
  engine.addImageProvider("gfilethumb", new ThumbnailProvider);
  engine.addImageProvider("systemicon", new SystemIconProvider);
  engine.rootContext()->setContextProperty("testBase", base);
  std::fprintf(stderr, "QML_LOAD_START\n");
  engine.loadData(R"qml(
import QtQuick
import QtQuick.Controls
import GFile.App
import GFile.Backend
ApplicationWindow {
 width:1400;height:900;visible:true;title:"g-File büyük arama testi"
 LanguageManager {id: language}
 FavoritesModel {id: favorites}
 AdminEditManager {id: admin}
 QuickAccessModel {id: quickAccess}
 QtObject {id: auth;property string googleAccessToken:"";property string oneDriveAccessToken:""}
 QtObject {id: prefs}
 QtObject {id: google}
 QtObject {id: one}
 BrowsePage {
  objectName:"page";anchors.fill:parent;lang:language;favoritesModel:favorites;adminEditor:admin
  cloudAuth:auth;cloudIntegrationPreferences:prefs;googleDrive:google;oneDrive:one
  contentIndexModel:null;quickAccessModel:quickAccess;initialLocation:testBase
 }
}
)qml");
  std::fprintf(stderr, "QML_LOADED\n");
  if (engine.rootObjects().isEmpty())
    return 2;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects()[0]);
  auto page = window->findChild<QQuickItem *>("page");
  auto pane = qobject_cast<QQuickItem *>(
      page->property("activePane").value<QObject *>());
  if (!pane)
    return 15;
  DirectoryModel *model = nullptr;
  for (auto candidate : window->findChildren<DirectoryModel *>())
    if (candidate->location() == QUrl::fromLocalFile(base).toString())
      model = candidate;
  if (!model || !until([&] { return !model->loading(); }))
    return 3;
  pane->setProperty("folderPreviewsEnabled", false);
  pause(300);
  std::fprintf(stderr, "DIRECTORY_READY\n");
  int heartbeats = 0;
  int insertions = 0;
  qint64 maxGap = 0, maxInsertMs = 0, firstResult = -1;
  QElapsedTimer run, lastBeat, insert;
  run.start();
  lastBeat.start();
  QTimer heartbeat;
  heartbeat.setInterval(16);
  QObject::connect(&heartbeat, &QTimer::timeout, &app, [&] {
    maxGap = qMax(maxGap, lastBeat.restart());
    heartbeats++;
  });
  heartbeat.start();
  QObject::connect(model, &QAbstractItemModel::rowsAboutToBeInserted, &app,
                   [&] { insert.start(); });
  QObject::connect(model, &QAbstractItemModel::rowsInserted, &app, [&] {
    maxInsertMs = qMax(maxInsertMs, insert.elapsed());
    insertions++;
    if (firstResult < 0)
      firstResult = run.elapsed();
  });
  std::fprintf(stderr, "SEARCH_START\n");
  QMetaObject::invokeMethod(pane, "startSearch",
                            Q_ARG(QVariant, QVariant("match")),
                            Q_ARG(QVariant, QVariant(false)));
  bool selectedWhileScanning = false;
  QTimer userAction;
  userAction.setInterval(25);
  QObject::connect(&userAction, &QTimer::timeout, &app, [&] {
    if (selectedWhileScanning || model->rowCount() < 512 || !model->loading())
      return;
    QMetaObject::invokeMethod(pane, "selectIndex", Q_ARG(QVariant, QVariant(0)),
                              Q_ARG(QVariant, QVariant(0)));
    selectedWhileScanning = pane->property("selectedCount").toInt() == 1;
  });
  userAction.start();
  if (!until(
          [&] {
            return model->searchActive() && model->rowCount() == expected &&
                   !model->loading();
          },
          60000))
    return 4;
  std::fprintf(
      stderr,
      "SEARCH_RESULTS %d TOTAL_MS %lld FIRST_RESULT_MS %lld HEARTBEATS %d "
      "MAX_GAP_MS %lld INSERTIONS %d MAX_INSERT_MS %lld\n",
      model->rowCount(), run.elapsed(), firstResult, heartbeats, maxGap,
      insertions, maxInsertMs);
  if (!selectedWhileScanning || heartbeats < 10 || maxGap > 500)
    return 5;
  const auto urls = model->allItemUrls();
  QSet<QString> unique;
  for (const auto &url : urls)
    unique.insert(url.toString());
  if (unique.size() != expected)
    return 6;
  const QStringList reversed{urls.at(100).toString(), urls.at(0).toString()};
  const auto selectedItems = model->itemsForUrls(reversed);
  if (selectedItems.size() != 2 ||
      selectedItems[0].toMap().value("itemUrl").toString() !=
          urls.at(0).toString() ||
      selectedItems[1].toMap().value("itemUrl").toString() !=
          urls.at(100).toString())
    return 7;
  userAction.stop();
  QMetaObject::invokeMethod(pane, "clearSelection");
  // Exercise real tab state capture/restoration, not just the model cache.
  auto checkTabScroll = [&](bool gridMode, const QString &target) {
    pane->setProperty("gridMode", gridMode);
    pause(60);
    const char *name = gridMode ? "fileGridView" : "fileListView";
    auto view = pane->findChild<QQuickItem *>(name);
    QMetaObject::invokeMethod(view, "forceLayout");
    view->setProperty("contentY", view->property("originY").toReal() + 1800);
    pause(40);
    const double offset = view->property("contentY").toReal() -
                          view->property("originY").toReal();
    if (offset < 1000)
      return false;
    const QString session = model->searchSessionId();
    QMetaObject::invokeMethod(page, "addTab",
                              Q_ARG(QVariant, QVariant(target)),
                              Q_ARG(QVariant, QVariant(true)));
    pause(100);
    std::fprintf(stderr, "TAB_AWAY tab=%d active=%d loading=%d location=%s\n",
                 page->property("currentTab").toInt(), model->searchActive(),
                 model->loading(), qPrintable(model->location()));
    if (!until([&] { return !model->searchActive() && !model->loading(); }))
      return false;
    QMetaObject::invokeMethod(page, "switchToTab", Q_ARG(QVariant, QVariant(0)),
                              Q_ARG(QVariant, QVariant(false)));
    pause(100);
    std::fprintf(
        stderr,
        "TAB_RETURN tab=%d active=%d loading=%d session=%s expected=%s\n",
        page->property("currentTab").toInt(), model->searchActive(),
        model->loading(), qPrintable(model->searchSessionId()),
        qPrintable(session));
    if (!until([&] { return model->searchSessionId() == session; }))
      return false;
    pause(100);
    const double restored = view->property("contentY").toReal() -
                            view->property("originY").toReal();
    std::fprintf(stderr,
                 "TAB_SCROLL mode=%s streaming=%d saved=%.1f restored=%.1f\n",
                 name, model->loading(), offset, restored);
    return qAbs(offset - restored) < 1;
  };
  if (!checkTabScroll(true, base + "/folder-0"))
    return 16;
  if (!checkTabScroll(false, base))
    return 17;
  // Cancel a producer while its bounded mailbox is full. No queued rows may
  // reappear after cancellation, and the next search must still complete.
  model->cancelSearchForInput();
  model->search("match");
  if (!until([&] { return model->rowCount() >= 512 && model->loading(); }))
    return 8;
  QElapsedTimer cancellation;
  cancellation.start();
  model->cancelSearchForInput();
  if (cancellation.elapsed() > 200 || model->loading() || model->searchActive())
    return 9;
  pause(120);
  if (model->rowCount() != 0)
    return 10;
  // An inactive tab continues caching results. Reattach it after an ordinary
  // directory load, then verify completeness and absence of stale folder rows.
  model->search("match");
  if (!until([&] { return model->rowCount() >= 512; }))
    return 11;
  if (!checkTabScroll(true, base + "/folder-1"))
    return 18;
  const QString session = model->detachSearchSession();
  model->setLocation(base + "/folder-0");
  if (!until([&] { return !model->loading(); }))
    return 12;
  pause(200);
  if (!model->restoreSearchSession(session))
    return 13;
  if (!until(
          [&] { return model->rowCount() == expected && !model->loading(); }))
    return 14;
  model->releaseSearchSession(session);
  // Destruction must wake a blocked producer so closing a searching tab cannot
  // deadlock QThreadPool shutdown.
  {
    DirectoryModel closing;
    closing.setLocation(base);
    closing.search("match");
    pause(100);
  }
  std::fprintf(stderr,
               "PASS: complete results, responsive selection, "
               "cancellation, session restore, tab scroll, worker shutdown\n");
  return 0;
}
