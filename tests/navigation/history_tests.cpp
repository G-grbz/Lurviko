#include <QtTest>
#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <unistd.h>
#include "quickaccessmodel.h"

class HistoryTests : public QObject
{
    Q_OBJECT
    QTemporaryDir m_config;
private slots:
    void initTestCase()
    {
        QVERIFY(m_config.isValid());
        QCoreApplication::setOrganizationName("Lurviko-QA");
        QCoreApplication::setApplicationName("History");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_config.path());
    }
    void init() { QSettings().clear(); }

    void unavailablePathsSurviveMenuNavigationAndRestart()
    {
        QTemporaryDir fixture;
        const QString disk = fixture.filePath("disk"), offline = fixture.filePath("offline");
        const QString one = disk + "/one", two = disk + "/two", local = fixture.filePath("local");
        QVERIFY(QDir().mkpath(one)); QVERIFY(QDir().mkpath(two)); QVERIFY(QDir().mkpath(local));
        QuickAccessModel model;
        model.setLastLocation(one); model.setLastLocation(two);
        const auto before = model.recentLocations(); QCOMPARE(before.size(), 2);
        // Temporarily remove access to the same paths as a disconnected
        // mount or replacement of an extracted folder would do.
        QVERIFY(QDir().rename(disk, offline));
        QSignalSpy changed(&model, &QuickAccessModel::recentLocationsChanged);
        QVERIFY(!model.pruneRecentLocations());
        QCOMPARE(model.recentLocations(), before); QCOMPARE(changed.count(), 0);
        model.setLastLocation(local); // Every normal navigation used to prune.
        QCOMPARE(model.recentLocations().size(), 3);
        QSettings().sync();
        QuickAccessModel restored;
        QCOMPARE(restored.recentLocations(), model.recentLocations());
        QVERIFY(QDir().rename(offline, disk));
        restored.setLastLocation(two);
        QCOMPARE(restored.recentLocations().size(), 3);
        QCOMPARE(restored.lastLocation(), QUrl::fromLocalFile(two).toString());
    }

    void permissionFailureDoesNotEraseHistory()
    {
        if (::geteuid() == 0) QSKIP("Root bypasses directory access permissions");
        QTemporaryDir fixture;
        const QString parent = fixture.filePath("parent"), child = parent + "/child";
        QVERIFY(QDir().mkpath(child));
        QuickAccessModel model; model.setLastLocation(child);
        const auto before = model.recentLocations(); QCOMPARE(before.size(), 1);
        const auto permissions = QFile::permissions(parent);
        const auto restore = qScopeGuard([&] { QFile::setPermissions(parent, permissions); });
        QVERIFY(QFile::setPermissions(parent, {}));
        QVERIFY(!QFileInfo(child).exists());
        QVERIFY(!model.pruneRecentLocations());
        QCOMPARE(model.recentLocations(), before);
        QuickAccessModel restored; QCOMPARE(restored.recentLocations(), before);
    }

    void uniqueTenLocationsPersist()
    {
        QTemporaryDir fixture;
        QuickAccessModel model;
        for (int i = 0; i < 12; ++i) {
            const auto path = fixture.filePath(QString::number(i));
            QVERIFY(QDir().mkpath(path)); model.setLastLocation(path);
        }
        QCOMPARE(model.recentLocations().size(), 10);
        const QString path = fixture.filePath("5");
        model.setLastLocation(path);
        const auto before = model.recentLocations();
        model.setLastLocation(path + "/");
        model.setLastLocation(QUrl::fromLocalFile(path + "/").toString());
        model.setLastLocation(path + "/../5");
        QCOMPARE(model.recentLocations(), before);
        QCOMPARE(model.lastLocation(), QUrl::fromLocalFile(path).toString());
        model.setLastLocation(QDir::homePath()); model.setLastLocation("category:/images");
        model.setLastLocation(fixture.filePath("never-visited"));
        QCOMPARE(model.recentLocations(), before);
        QSettings().sync();
        QuickAccessModel restored; QCOMPARE(restored.recentLocations(), before);
    }
};
QTEST_GUILESS_MAIN(HistoryTests)
#include "history_tests.moc"
