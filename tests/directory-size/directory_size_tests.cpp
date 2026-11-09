#include "filemetadata_p.h"
#include "directorysizeindex.h"
#include "directorywatchsubscription.h"
#include "directorysizestore_p.h"
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/inotify.h>
#include <unistd.h>

class DirectorySizeTests : public QObject {
    Q_OBJECT
    static void write(const QString& path, int size)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || !file.resize(size))
            qFatal("Cannot create size fixture");
    }
    static bool wait(DirectorySizeIndex& index, const QString& root, qint64 bytes, int timeout = 15000)
    {
        QElapsedTimer clock;
        clock.start();
        while (clock.elapsed() < timeout) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            auto s = index.snapshot(root);
            if (!index.busy() && s.complete && !s.calculating && s.totals.bytes == bytes)
                return true;
            QThread::msleep(5);
        }
        const auto s = index.snapshot(root);
        qWarning() << "Size state" << s.path << s.state << s.totals.bytes << s.error << "busy"
                   << index.busy();
        return false;
    }
    static DirectorySizeWork workFor(const QString& database, const QStringList& paths, const QString& session)
    {
        DirectorySizeWork work;
        work.databasePath = database; work.requested = paths; work.session = session;
        work.cancelled = std::make_shared<std::atomic_bool>(false);
        work.demand = std::make_shared<std::atomic_int>(0);
        work.ioBusy = std::make_shared<std::atomic_bool>(false);
        work.progress = std::make_shared<DirectorySizeProgress>();
        work.watches = std::make_shared<DirectorySizeWatchState>();
        return work;
    }
private slots:
    void visibleDirectoryWatchWorksIndependentlyOfSizeIndex()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/external";
        QVERIFY(QDir().mkpath(root));
        DirectoryWatchSubscription visible;
        visible.addPath(root);
        auto *index = DirectorySizeIndex::instance();
        // Force the central size-index watch off. The file list still needs to
        // see files published externally even while its size-index subscription
        // has been removed or is being rearmed.
        index->setWatchedDirectories(&visible, {});
        QTRY_COMPARE_WITH_TIMEOUT(index->activeWatchCount(), 0, 5000);
        QSignalSpy changes(&visible, &DirectoryWatchSubscription::directoryChanged);
        write(root + "/mkvtoolnix-output.mkv", 37);
        QTRY_VERIFY_WITH_TIMEOUT(changes.count() > 0, 5000);
        QCOMPARE(changes.constLast().at(0).toString(), root);
    }
    void externalWriterAndAtomicRenameNotifyVisibleDirectory()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/output";
        const auto database = temp.path() + "/cache/index.sqlite";
        QVERIFY(QDir().mkpath(root));
        DirectorySizeIndex index(database);
        QObject owner;
        QSignalSpy changes(&index, &DirectorySizeIndex::directoryChanged);
        index.setWatchedDirectories(&owner, {root});
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 1, 5000);
        const auto part = root + "/muxing.mkv.part";
        const auto mkv = root + "/finished.mkv";
        write(part, 17); // external application creates its output
        QTRY_VERIFY_WITH_TIMEOUT(changes.count() >= 1, 5000);
        changes.clear();
        QVERIFY(QFile::rename(part, mkv)); // MKVToolNix-style atomic publish
        QTRY_VERIFY_WITH_TIMEOUT(changes.count() >= 1, 5000);
        bool matchingParent = false;
        for (const auto &args : changes)
            matchingParent |= args.at(0).toString() == root;
        QVERIFY(matchingParent);
    }
    void startupWarmupWorksWithoutAVisibleDirectoryAndReusesTheCache()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        write(root + "/nested/data", 17);
        // A prior Home/child scan must not be repeated while filling its parent.
        const auto child = runDirectorySizeWork(workFor(database, {root + "/nested"}, "prior-session"));
        QVERIFY(child.error.isEmpty());
        {
            DirectorySizeIndex index(database);
            QSignalSpy jobs(&index, &DirectorySizeIndex::workCompleted);
            index.warmup(root);
            QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
            QCOMPARE(jobs.size(), 1);
            QCOMPARE(jobs.first().at(1).toLongLong(), 1);
            QCOMPARE(index.activeWatchCount(), 0);
            const auto cached = readDirectorySizeSnapshots(workFor(database, {root}, "read"));
            QVERIFY2(cached.error.isEmpty(), qPrintable(cached.error));
            QCOMPARE(cached.snapshots[root].totals.bytes, 17);
            QVERIFY(cached.snapshots[root].complete);
            index.warmup(root);
            QVERIFY(!index.busy()); // At most one startup fill per service lifetime.
        }
        DirectorySizeIndex restarted(database);
        QSignalSpy jobs(&restarted, &DirectorySizeIndex::workCompleted);
        restarted.warmup(root);
        QTRY_VERIFY_WITH_TIMEOUT(!restarted.busy(), 10000);
        QCOMPARE(jobs.size(), 1);
        QCOMPARE(jobs.first().at(0).toLongLong(), 0);
        QCOMPARE(jobs.first().at(1).toLongLong(), 0);
        QCOMPARE(restarted.activeWatchCount(), 0);
    }
    void startupWarmupYieldsToNavigationWithoutLosingPartialRecords()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/warmup", next = temp.path() + "/visible";
        const auto database = temp.path() + "/cache/index.sqlite", seed = temp.path() + "/seed";
        write(seed, 1); write(next + "/data", 7);
        QDir().mkpath(root + "/a"); QDir().mkpath(root + "/b");
        for (int i = 0; i < 24000; ++i) {
            const auto file = root + (i < 12000 ? "/a/" : "/b/") + QString::number(i);
            QCOMPARE(::link(QFile::encodeName(seed).constData(), QFile::encodeName(file).constData()), 0);
        }
        DirectorySizeIndex index(database);
        QObject transfer, reader;
        index.setIoBusy(&transfer, true);
        index.warmup(root);
        QTest::qWait(450);
        QVERIFY(index.busy());
        QElapsedTimer latency; latency.start();
        index.retain(&reader, next); index.request(next);
        QTRY_VERIFY_WITH_TIMEOUT(index.snapshot(next).complete && !index.snapshot(next).calculating, 3000);
        QCOMPARE(index.snapshot(next).totals.bytes, 7);
        qInfo() << "Visible directory while startup scan yields:" << latency.elapsed() << "ms";
        const auto partial = readDirectorySizeSnapshots(workFor(database, {root + "/a", root + "/b"}, "partial"));
        QVERIFY2(partial.error.isEmpty(), qPrintable(partial.error));
        QVERIFY(partial.snapshots[root + "/a"].totals.files + partial.snapshots[root + "/b"].totals.files > 0);
        index.setIoBusy(&transfer, false);
        QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        const auto complete = readDirectorySizeSnapshots(workFor(database, {root}, "complete"));
        QVERIFY2(complete.error.isEmpty(), qPrintable(complete.error));
        QVERIFY(complete.snapshots[root].complete);
        QCOMPARE(complete.snapshots[root].totals.bytes, 24000);
        QCOMPARE(complete.snapshots[root].totals.files, 24000);
        QCOMPARE(index.activeWatchCount(), 0);
    }
    void startupWarmupKeepsMountScopesAndDoesNotForceCachedVerification()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        write(root + "/data", 5);
        write(root + "/disk/data", 100);
        write(root + "/virtual/data", 200);
        auto initial = workFor(database, {root}, "initial");
        initial.backgroundWarmup = true;
        initial.mountBoundaries = {root + "/disk"};
        initial.virtualRoots = {root + "/virtual"};
        const auto first = runDirectorySizeWork(initial);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(first.snapshots[root].totals.bytes, 5);
        QCOMPARE(first.directoryListings, 1);
        QCOMPARE(first.fileStats, 1); // Excluded mountpoints are not even statted.
        write(root + "/data", 8);
        auto restart = initial; restart.session = "restart"; restart.invalidateAll = true;
        const auto cached = runDirectorySizeWork(restart);
        QVERIFY2(cached.error.isEmpty(), qPrintable(cached.error));
        QCOMPARE(cached.snapshots[root].totals.bytes, 5);
        QCOMPARE(cached.fileStats, 0);
        QCOMPARE(cached.directoryListings, 0);
        auto refresh = initial; refresh.session = "refresh"; refresh.backgroundWarmup = false;
        const auto verified = runDirectorySizeWork(refresh);
        QVERIFY2(verified.error.isEmpty(), qPrintable(verified.error));
        QCOMPARE(verified.snapshots[root].totals.bytes, 8);
    }
    void initialScanKeepsLeafRecordsAcrossDirectoriesAndUpdates()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        qint64 expected = 0;
        int files = 0;
        for (int count : {1, 63, 64, 65, 129}) {
            const auto folder = root + "/" + QString::number(count);
            for (int i = 0; i < count; ++i) {
                write(folder + "/item-" + QString::number(i), i + 1);
                expected += i + 1;
                ++files;
            }
        }
        const auto source = root + "/64/item-63", alias = root + "/hard-link";
        QCOMPARE(::link(QFile::encodeName(source).constData(), QFile::encodeName(alias).constData()), 0);
        expected += 64; ++files;
        QCOMPARE(::symlink("64/item-63", QFile::encodeName(root + "/symbolic-link").constData()), 0);
        QCOMPARE(::mkfifo(QFile::encodeName(root + "/pipe").constData(), 0600), 0);
        auto first = runDirectorySizeWork(workFor(database, {root}, "initial"));
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error));
        QCOMPARE(first.snapshots[root].totals.bytes, expected);
        QCOMPARE(first.snapshots[root].totals.files, files);
        QCOMPARE(first.snapshots[root].totals.folders, 5);
        auto children = workFor(database, {}, "restart");
        for (int count : {1, 63, 64, 65, 129})
            children.requested.append(root + "/" + QString::number(count));
        const auto cached = runDirectorySizeWork(children);
        QVERIFY2(cached.error.isEmpty(), qPrintable(cached.error));
        QCOMPARE(cached.fileStats, 0);
        QCOMPARE(cached.directoryListings, 0);
        for (int count : {1, 63, 64, 65, 129}) {
            const auto value = cached.snapshots[root + "/" + QString::number(count)];
            QCOMPARE(value.totals.bytes, qint64(count) * (count + 1) / 2);
            QCOMPARE(value.totals.files, count);
        }
        write(source, 71);
        auto growth = workFor(database, {root}, "growth");
        growth.paths = {source};
        auto changed = runDirectorySizeWork(growth);
        QVERIFY2(changed.error.isEmpty(), qPrintable(changed.error));
        QCOMPARE(changed.snapshots[root].totals.bytes, expected + 14); // Both hard-link names grow.
        QVERIFY(QFile::remove(root + "/129/item-128"));
        auto deletion = workFor(database, {root}, "deletion");
        deletion.paths = {root + "/129/item-128"};
        auto removed = runDirectorySizeWork(deletion);
        QCOMPARE(removed.snapshots[root].totals.bytes, expected + 14 - 129);
        QCOMPARE(removed.snapshots[root].totals.files, files - 1);
    }
    void ancestorInvalidationDoesNotRescanEveryVisitedDirectory()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        for (int i = 0; i < 50; ++i)
            write(root + "/folder-" + QString::number(i) + "/deep/data", 10);
        auto initial = runDirectorySizeWork(workFor(database, {root}, "initial"));
        QVERIFY2(initial.error.isEmpty(), qPrintable(initial.error));
        QCOMPARE(initial.snapshots[root].totals.bytes, 500);
        // The legacy cache marks every ancestor dirty even for a named change.
        // That flag must not turn ordinary navigation into full verification.
        {
            const QString connection = "ancestor-invalidation";
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database); QVERIFY(db.open());
            { QSqlQuery q(db); QVERIFY(q.exec("UPDATE roots SET needs_verify=1")); }
            db.close(); db = {}; QSqlDatabase::removeDatabase(connection);
        }
        for (const auto& path : QStringList{root, root + "/folder-0", root + "/folder-1"}) {
            auto result = runDirectorySizeWork(workFor(database, {path}, "restart"));
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.directoryListings, 0);
            QCOMPARE(result.fileStats, 0);
            QVERIFY(result.snapshots[path].available);
        }
        // Preserve the specific path across an inactive period/restart and
        // update just that file, without revalidating the other 49 subtrees.
        const auto changed = root + "/folder-0/deep/data";
        write(changed, 23);
        auto hint = workFor(database, {}, "inactive");
        hint.invalidatedPaths = {changed};
        QVERIFY(runDirectorySizeWork(hint).error.isEmpty());
        auto updated = runDirectorySizeWork(workFor(database, {root}, "next"));
        QVERIFY2(updated.error.isEmpty(), qPrintable(updated.error));
        QCOMPARE(updated.snapshots[root].totals.bytes, 513);
        QCOMPARE(updated.fileStats, 1);
        QCOMPARE(updated.directoryListings, 0);
        auto revisit = runDirectorySizeWork(workFor(database, {root}, "again"));
        QCOMPARE(revisit.snapshots[root].totals.bytes, 513);
        QCOMPARE(revisit.fileStats, 0);
        QCOMPARE(revisit.directoryListings, 0);
        write(root + "/unrelated/new/data", 7);
        auto ancestorHint = workFor(database, {}, "ancestor-hint");
        ancestorHint.invalidatedPaths = {root};
        QVERIFY(runDirectorySizeWork(ancestorHint).error.isEmpty());
        const auto child = root + "/folder-1";
        auto childVisit = runDirectorySizeWork(workFor(database, {child}, "child-visit"));
        QVERIFY(childVisit.error.isEmpty());
        QCOMPARE(childVisit.snapshots[child].totals.bytes, 10);
        QCOMPARE(childVisit.directoryListings, 0); // Do not scan the dirty ancestor's siblings.
        auto parentVisit = runDirectorySizeWork(workFor(database, {root}, "parent-visit"));
        QCOMPARE(parentVisit.snapshots[root].totals.bytes, 520);
    }
    void ancestorEventLossDoesNotRescanCachedDescendantsOnEveryVisit()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", child = root + "/folder", database = temp.path() + "/cache/index.sqlite";
        write(child + "/deep/data", 10);
        auto initial = runDirectorySizeWork(workFor(database, {root}, "initial"));
        QVERIFY2(initial.error.isEmpty(), qPrintable(initial.error));
        QCOMPARE(initial.snapshots[root].totals.bytes, 10);

        {
            const QString connection = "ancestor-event-loss";
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database); QVERIFY(db.open());
            {
                QSqlQuery q(db);
                QVERIFY(q.prepare("UPDATE roots SET needs_verify=2 WHERE path=?"));
                q.addBindValue(root);
                QVERIFY(q.exec());
            }
            db.close(); db = {}; QSqlDatabase::removeDatabase(connection);
        }

        for (const auto& session : {QStringLiteral("first-visit"), QStringLiteral("second-visit")}) {
            const auto result = runDirectorySizeWork(workFor(database, {child}, session));
            QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
            QCOMPARE(result.snapshots[child].totals.bytes, 10);
            QCOMPARE(result.fileStats, 0);
            QCOMPARE(result.directoryListings, 0);
        }
        const auto stillInvalidated = runDirectorySizeWork(workFor(database, {root}, "root-visit"));
        QVERIFY2(stillInvalidated.error.isEmpty(), qPrintable(stillInvalidated.error));
        QVERIFY(stillInvalidated.fileStats > 0);
    }
    void visibleSubscriptionsAreSharedNonRecursiveAndReleased()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", other = temp.path() + "/other";
        for (int i=0; i<1001; ++i) write(root + "/sub-" + QString::number(i) + "/file", 1);
        QDir().mkpath(other);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject a, b;
        index.setWatchedDirectories(&a, {root}); index.setWatchedDirectories(&b, {root});
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 1, 10000);
        index.retain(&a, root); index.request(root);
        QVERIFY(wait(index, root, 1001));
        QCOMPARE(index.activeWatchCount(), 1); // scanning 1001 children adds zero watches
        index.setWatchedDirectories(&a, {other});
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 2, 10000);
        index.setWatchedDirectories(&b, {});
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 1, 10000);
        index.setWatchedDirectories(&a, {});
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 0, 10000);
        qInfo() << "1001 subdirectories: one shared visible watch; zero after leaving";
    }
    void visibleSymlinkAliasesShareAndReleaseTheirNativeWatch()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", alias = temp.path() + "/alias";
        write(root + "/file", 1);
        QCOMPARE(::symlink(QFile::encodeName(root).constData(), QFile::encodeName(alias).constData()), 0);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject a, b;
        index.setWatchedDirectories(&a, {root}); index.setWatchedDirectories(&b, {alias});
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 1, 10000);
        QSignalSpy changed(&index, &DirectorySizeIndex::fileChanged);
        write(root + "/file", 2);
        QTRY_VERIFY_WITH_TIMEOUT(changed.size() >= 2, 10000);
        QSet<QString> parents; for (const auto& event : changed) parents.insert(event.at(0).toString());
        QVERIFY(parents.contains(root)); QVERIFY(parents.contains(alias));
        index.setWatchedDirectories(&a, {});
        QCOMPARE(index.activeWatchCount(), 1);
        changed.clear(); write(root + "/file", 3);
        QTRY_VERIFY_WITH_TIMEOUT(!changed.isEmpty(), 10000);
        QCOMPARE(changed.last().at(0).toString(), alias);
        index.setWatchedDirectories(&b, {}); QCOMPARE(index.activeWatchCount(), 0);
    }
    void navigationCancelsOldWorkAndPrioritizesNewDirectory()
    {
        QTemporaryDir temp;
        const auto oldRoot=temp.path()+"/old", next=temp.path()+"/next";
        write(oldRoot+"/seed", 1); write(next+"/file", 7);
        for(int i=0;i<20000;++i)
            QCOMPARE(::link(QFile::encodeName(oldRoot+"/seed").constData(),
                           QFile::encodeName(oldRoot+"/item-"+QString::number(i)).constData()),0);
        DirectorySizeIndex index(temp.path()+"/cache/index.sqlite");
        QObject oldReader, newReader, transfer;
        index.setIoBusy(&transfer, true); index.retain(&oldReader, oldRoot);
        index.request(oldRoot);
        QTest::qWait(280);
        QVERIFY(index.snapshot(oldRoot).calculating);
        QElapsedTimer latency; latency.start();
        index.release(&oldReader); index.retain(&newReader,next); index.request(next);
        QVERIFY(wait(index,next,7,3000));
        QVERIFY(!index.snapshot(oldRoot).calculating);
        QCOMPARE(index.activeWatchCount(),0);
        qInfo()<<"New directory after cancellation:"<<latency.elapsed()<<"ms";
    }
    void cancellingRunningSelectionReleasesItsWorker()
    {
        QTemporaryDir temp;
        const auto slow = temp.path() + "/slow", next = temp.path() + "/next";
        write(slow + "/seed", 1); write(next + "/file", 7);
        for (int i = 0; i < 20000; ++i)
            QCOMPARE(::link(QFile::encodeName(slow + "/seed").constData(),
                           QFile::encodeName(slow + "/item-" + QString::number(i)).constData()), 0);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject transfer, reader;
        index.setIoBusy(&transfer, true);
        QSignalSpy selections(&index, &DirectorySizeIndex::selectionReady);
        const auto id = index.inspectSelection({slow});
        QTest::qWait(280); QVERIFY(index.busy());
        index.cancelSelection(id);
        index.retain(&reader, next); index.request(next);
        QVERIFY(wait(index, next, 7, 3000));
        QCOMPARE(selections.size(), 0);
    }
    void cancelledTransactionPreservesInvisibleMutationHints()
    {
        QTemporaryDir temp;
        const auto cached = temp.path() + "/cached", slow = temp.path() + "/slow", next = temp.path() + "/next";
        write(cached + "/deep/data", 10); write(slow + "/seed", 1); write(next + "/file", 7);
        for (int i = 0; i < 20000; ++i)
            QCOMPARE(::link(QFile::encodeName(slow + "/seed").constData(),
                           QFile::encodeName(slow + "/item-" + QString::number(i)).constData()), 0);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject reader, transfer;
        index.retain(&reader, cached); index.request(cached);
        QVERIFY(wait(index, cached, 10)); index.release(&reader);
        write(cached + "/deep/data", 17);
        index.observePaths({cached + "/deep/data"});
        index.setIoBusy(&transfer, true); index.retain(&reader, slow); index.request(slow);
        QTest::qWait(280);
        QVERIFY(index.snapshot(slow).calculating);
        index.retain(&reader, next); index.request(next);
        QVERIFY(wait(index, next, 7, 3000));
        index.setIoBusy(&transfer, false);
        index.retain(&reader, cached); index.request(cached);
        QVERIFY(wait(index, cached, 17));
    }
    void statxAndFallbackKeepMetadataAndLinkSemantics()
    {
        QTemporaryDir temp;
        const auto path=temp.path()+"/file", link=temp.path()+"/link";
        write(path,123);
        QCOMPARE(::symlink(QFile::encodeName(path).constData(),QFile::encodeName(link).constData()),0);
        struct stat sx{}, fallback{}, symlinkInfo{};
        QCOMPARE(directoryMetadataAt(AT_FDCWD,QFile::encodeName(path).constData(),&sx),0);
        QCOMPARE(directoryMetadataAt(AT_FDCWD,QFile::encodeName(path).constData(),&fallback,false,false),0);
        QCOMPARE(sx.st_size,fallback.st_size); QCOMPARE(sx.st_ino,fallback.st_ino);
        QCOMPARE(directoryMetadataAt(AT_FDCWD,QFile::encodeName(link).constData(),&symlinkInfo,true),0);
        QVERIFY(S_ISLNK(symlinkInfo.st_mode));
    }
    void initialAndChildReuse()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/.hidden", 7);
        write(root + "/nested/data", 11);
        const auto loop = QFile::encodeName(root + "/nested/loop"), target = QFile::encodeName(root);
        QCOMPARE(::symlink(target.constData(), loop.constData()), 0);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 18));
        const auto s = index.snapshot(root);
        QCOMPARE(s.totals.files, 2);
        QCOMPARE(s.totals.folders, 1);
        QCOMPARE(s.totals.children.value(root + "/nested").toMap().value("bytes").toLongLong(), 11);
        const int before = work.count();
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QTest::qWait(30);
        QCOMPARE(work.count(), before);
        index.request(root + "/nested");
        QVERIFY(wait(index, root + "/nested", 11));
        QCOMPARE(work.last().at(1).toLongLong(), 0);
    }
    void deltasAndDuplicateHints()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/one", 10);
        write(root + "/nested/two", 20);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 30));
        write(root + "/one", 15);
        index.observePaths({ root + "/one", root + "/one" });
        QVERIFY(wait(index, root, 35));
        QVERIFY(QFile::remove(root + "/nested/two"));
        index.observePaths({ root + "/nested/two" });
        QVERIFY(wait(index, root, 15));
        index.observePaths({ root + "/nested/two" });
        QVERIFY(wait(index, root, 15));
        write(root + "/new/sub/data", 8);
        index.observeDirectory(root);
        QVERIFY(wait(index, root, 23));
        QVERIFY(QDir(root + "/new").removeRecursively());
        index.observeDirectory(root);
        QVERIFY(wait(index, root, 15));
    }
    void directoryRenameKeepsKnownSubtree()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/left/known/deep/data", 37);
        QDir().mkpath(root + "/right");
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 37));
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        QVERIFY(QDir().rename(root + "/left/known", root + "/right/moved"));
        index.observeMove(root + "/left/known", root + "/right/moved");
        QVERIFY(wait(index, root, 37));
        index.request(root + "/right/moved");
        QVERIFY(wait(index, root + "/right/moved", 37));
        qint64 listings = 0;
        for (const auto& event : work)
            listings += event.at(1).toLongLong();
        QVERIFY2(listings <= 2, "Known moved descendants must not be recursively relisted");
        write(root + "/right/moved/deep/data", 41);
        index.observePaths({root + "/right/moved/deep/data"});
        QVERIFY(wait(index, root, 41));
    }
    void persistentSnapshotAndOfflineChanges()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        write(root + "/nested/data", 10);
        {
            DirectorySizeIndex index(database);
            index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
            QVERIFY(wait(index, root, 10));
        }
        write(root + "/nested/data", 23);
        DirectorySizeIndex index(database);
        bool restoredOld = false;
        connect(&index, &DirectorySizeIndex::snapshotChanged, this, [&](const QString& path) {
            const auto s = index.snapshot(path);
            if (s.available && s.calculating && s.totals.bytes == 10)
                restoredOld = true;
        });
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 10));
        QVERIFY(restoredOld);
        QCOMPARE(index.snapshot(root).state, QString("cached"));
        // Opening a cached root does not walk every file after each restart.
        // Explicit Refresh validates changes made while the application was off.
        index.request(root, true);
        QVERIFY(wait(index, root, 23));
    }
    void cachedDeepChildDoesNotWaitForParentVerification()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        const auto child = root + "/bulk/deep/known";
        write(child + "/data", 17);
        const auto seed = temp.path() + "/seed";
        write(seed, 1);
        for (int i = 0; i < 30000; ++i)
            QCOMPARE(::link(QFile::encodeName(seed).constData(),
                         QFile::encodeName(root + "/bulk/item-" + QString::number(i)).constData()), 0);
        {
            DirectorySizeIndex initial(database);
            QElapsedTimer firstScan;
            firstScan.start();
            initial.request(root);
            QVERIFY(wait(initial, root, 30017, 30000));
            qInfo() << "Unthrottled 30k initial scan:" << firstScan.elapsed() << "ms";
        }
        // Refuse any rewrite of an unchanged file record. Restart verification
        // must validate metadata without updating every file's generation/stamp.
        const auto connection = QStringLiteral("verification-write-guard");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery query(db);
            QVERIFY(query.exec("CREATE TRIGGER no_file_rewrites BEFORE UPDATE ON entries WHEN OLD.kind=2 "
                               "BEGIN SELECT RAISE(ABORT,'unchanged file rewritten'); END"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        DirectorySizeIndex restarted(database);
        QObject transfer;
        restarted.setIoBusy(&transfer, true); // Keep validation active while navigating.
        QSignalSpy work(&restarted, &DirectorySizeIndex::workCompleted);
        restarted.request(root, true);
        QTRY_VERIFY_WITH_TIMEOUT(restarted.snapshot(root).available && work.count() == 1, 10000);
        QTest::qWait(260);
        QVERIFY(restarted.snapshot(root).calculating);
        QElapsedTimer latency;
        latency.start();
        restarted.request(child);
        QTRY_VERIFY_WITH_TIMEOUT(restarted.snapshot(child).available, 750);
        QCOMPARE(restarted.snapshot(child).totals.bytes, 17);
        QVERIFY(restarted.snapshot(root).calculating);
        qInfo() << "Cached deep-child lookup during parent verification:" << latency.elapsed() << "ms";
        QVERIFY(wait(restarted, root, 30017, 30000));
        QVERIFY(wait(restarted, child, 17));
        for (int i = 1; i < work.count(); ++i)
            QCOMPARE(work.at(i).at(1).toLongLong(), 0);
        QVERIFY(restarted.snapshot(root).error.isEmpty());
    }
    void restartRestoresBoundedWatchesWithoutWalkingFiles()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        for (int i = 0; i < 20; ++i)
            write(root + "/dir-" + QString::number(i) + "/file", 1);
        auto initial = workFor(database, { root }, "first");
        auto result = runDirectorySizeWork(initial);
        QVERIFY(result.error.isEmpty());
        auto restarted = workFor(database, { root }, "restarted");
        restarted.watches->descriptor = ::inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
        QVERIFY(restarted.watches->descriptor >= 0);
        restarted.watches->limit = 3;
        result = runDirectorySizeWork(restarted);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.fileStats, 0);
        QCOMPARE(result.directoryListings, 0);
        QCOMPARE(result.directoryStats, 0);
        QCOMPARE(result.snapshots.value(root).totals.bytes, 20);
        QCOMPARE(result.snapshots.value(root).state, QString("cached"));
        result = runDirectorySizeWork(restarted);
        QCOMPARE(result.directoryStats, 0);
        QCOMPARE(result.fileStats, 0);
        QCOMPARE(result.directoryListings, 0);
    }

    void legacyVolumeAndVirtualTotalsAreDetachedWithoutErasingTheirRecords()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/system", external = root + "/usb", virtualPath = root + "/proc";
        const auto database = temp.path() + "/cache/index.sqlite";
        write(root + "/local", 7);
        write(external + "/nested/data", 29);
        write(virtualPath + "/memory", 11);
        auto original = workFor(database, { root }, "old");
        QVERIFY(runDirectorySizeWork(original).error.isEmpty());
        const qint64 fakeSize = 131072LL * 1024 * 1024 * 1024;
        int records = 0;
        const auto connection = QStringLiteral("legacy-scope-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery q(db);
            q.prepare("WITH RECURSIVE ancestors(id) AS (SELECT id FROM entries WHERE name='proc' "
                      "UNION ALL SELECT e.parent FROM entries e JOIN ancestors a ON e.id=a.id WHERE e.parent IS NOT NULL) "
                      "UPDATE entries SET bytes=bytes+? WHERE id IN (SELECT id FROM ancestors)");
            q.addBindValue(fakeSize - 11);
            QVERIFY(q.exec());
            QVERIFY(q.exec(QString("UPDATE entries SET bytes=%1 WHERE name='memory'").arg(fakeSize)));
            QVERIFY(q.exec("SELECT COUNT(*) FROM entries")); QVERIFY(q.next());
            records = q.value(0).toInt(); q.finish();
            // Exercise upgrading the exact schema used before scope isolation.
            QVERIFY(q.exec("ALTER TABLE entries DROP COLUMN scope"));
            QVERIFY(q.exec("ALTER TABLE roots DROP COLUMN watch_session"));
            QVERIFY(q.exec("ALTER TABLE roots DROP COLUMN needs_verify"));
            QVERIFY(q.exec("DROP TABLE cache_info"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        auto upgraded = workFor(database, { root, external, virtualPath }, "new");
        upgraded.mountBoundaries = { external };
        upgraded.virtualRoots = { virtualPath };
        auto result = runDirectorySizeWork(upgraded);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.snapshots.value(root).totals.bytes, 7);
        QCOMPARE(result.snapshots.value(external).totals.bytes, 29);
        QCOMPARE(result.snapshots.value(virtualPath).state, QString("excluded"));
        QCOMPARE(result.snapshots.value(virtualPath).totals.bytes, 0);
        QCOMPARE(result.fileStats, 0);
        QCOMPARE(result.directoryListings, 0);
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database); QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT COUNT(*) FROM entries")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toInt(), records); q.finish();
            QVERIFY(q.exec("SELECT bytes FROM entries WHERE name='memory'")); QVERIFY(q.next());
            QCOMPARE(q.value(0).toLongLong(), fakeSize); q.finish();
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        write(external + "/nested/data", 53);
        upgraded.paths = { external + "/nested/data" };
        upgraded.selections[1] = { external };
        result = runDirectorySizeWork(upgraded);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.snapshots.value(root).totals.bytes, 7);
        QCOMPARE(result.snapshots.value(external).totals.bytes, 53);
        QCOMPARE(result.selections.value(1).totals.bytes, 53);
        upgraded.selections.clear();
        const auto away = temp.path() + "/disconnected";
        QVERIFY(QDir().rename(external, away));
        upgraded.paths = { external, external + "/nested/data" };
        result = runDirectorySizeWork(upgraded);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.snapshots.value(root).totals.bytes, 7);
        QCOMPARE(result.snapshots.value(external).totals.bytes, 53);
        QCOMPARE(result.snapshots.value(external).state, QString("unavailable"));
        QVERIFY(QDir().rename(away, external));
        upgraded.paths.clear();
        result = runDirectorySizeWork(upgraded);
        QCOMPARE(result.snapshots.value(root).totals.bytes, 7);
        QCOMPARE(result.snapshots.value(external).totals.bytes, 53);
    }

    void mountedVolumesAreLazyUntilExplicitlyRequested()
    {
        const auto boundaries = directorySizeMountBoundaries();
        for (const auto& virtualPath : { "/proc", "/sys", "/dev", "/run" })
            QVERIFY(boundaries.contains(virtualPath));
        QTemporaryDir temp;
        const auto root = temp.path() + "/system", external = root + "/media/usb";
        write(root + "/local", 7);
        write(external + "/untouched/data", 29);
        DirectorySizeWork work;
        work.databasePath = temp.path() + "/cache/index.sqlite";
        work.session = "isolated-topology"; work.requested = { root };
        work.mountBoundaries = { external };
        work.cancelled = std::make_shared<std::atomic_bool>(false);
        work.demand = std::make_shared<std::atomic_int>(0);
        work.ioBusy = std::make_shared<std::atomic_bool>(false);
        work.progress = std::make_shared<DirectorySizeProgress>();
        work.watches = std::make_shared<DirectorySizeWatchState>();
        auto result = runDirectorySizeWork(work);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.snapshots.value(root).totals.bytes, 7);
        QCOMPARE(result.directoryListings, 2); // system + media; not usb or its descendants.
        work.requested = { external };
        result = runDirectorySizeWork(work);
        QVERIFY(result.error.isEmpty());
        QCOMPARE(result.snapshots.value(external).totals.bytes, 29);
        QVERIFY(result.snapshots.value(external).complete);
    }
    void selectionUsesKnownSubtreesAndDeduplicatesAncestors()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/nested/a", 11);
        write(root + "/nested/.hidden", 7);
        write(root + "/other/b", 3);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 21));
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        bool done = false;
        DirectorySizeSnapshot selected;
        connect(
            &index, &DirectorySizeIndex::selectionReady, this, [&](quint64, const DirectorySizeSnapshot& s) {
                selected = s;
                done = true;
            });
        index.inspectSelection({ root + "/nested", root + "/nested/a", root + "/other/b" });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QVERIFY(selected.complete);
        QCOMPARE(selected.totals.bytes, 21);
        QCOMPARE(selected.totals.files, 3);
        QCOMPARE(selected.totals.folders, 1);
        QCOMPARE(work.last().at(1).toLongLong(), 0);
        done = false;
        index.inspectSelection({ root + "/nested/a", root + "/other/b" });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(selected.totals.bytes, 14);
        QCOMPARE(selected.totals.files, 2);
    }
    void hardLinkGrowthAndMembershipHints()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/a", 10);
        QDir().mkpath(root + "/nested");
        QCOMPARE(::link(QFile::encodeName(root + "/a").constData(),
                     QFile::encodeName(root + "/nested/peer").constData()),
            0);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 20));
        write(root + "/a", 17);
        index.observePaths({ root + "/a" });
        QVERIFY(wait(index, root, 34));
        // Removing a category/index membership is only a hint. A file that
        // still exists on disk must keep its contribution to the total.
        index.observePaths({ root + "/nested/peer" });
        QVERIFY(wait(index, root, 34));
    }
    void unavailableRootAndRecovery()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", away = temp.path() + "/disconnected";
        write(root + "/nested/data", 19);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 19));
        QVERIFY(QDir().rename(root, away));
        index.observeDirectory(root);
        QTRY_VERIFY_WITH_TIMEOUT(!index.busy() && index.snapshot(root).state == "unavailable", 10000);
        auto snapshot = index.snapshot(root);
        QCOMPARE(snapshot.state, QString("unavailable"));
        QVERIFY(snapshot.available);
        QCOMPARE(snapshot.totals.bytes, 19);
        QVERIFY(!snapshot.complete);
        QVERIFY(QDir().rename(away, root));
        index.request(root, true);
        QVERIFY(wait(index, root, 19));
    }
    void watchLimitsAndForcedVerification()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/nested/data", 4);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite", nullptr, 0);
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 4));
        QCOMPARE(index.snapshot(root).state, QString("cached"));
        write(root + "/nested/data", 16);
        index.verifyAll();
        QVERIFY(wait(index, root, 16));
        QCOMPARE(index.snapshot(root).state, QString("cached"));
    }
    void corruptCacheRecovery()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        write(root + "/data", 23);
        write(database, 100);
        DirectorySizeIndex index(database);
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 23));
        QCOMPARE(QDir(temp.path() + "/cache").entryList({ "*.corrupt-*" }, QDir::Files).size(), 1);
    }
    void largeTreeIncrementalAndChangesDuringScan()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", seed = temp.path() + "/seed";
        write(seed, 1);
        QDir().mkpath(root + "/bulk");
        for (int i = 0; i < 30000; ++i)
            QCOMPARE(::link(QFile::encodeName(seed).constData(),
                         QFile::encodeName(root + "/bulk/item-" + QString::number(i)).constData()),
                0);
        write(root + "/target", 10);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject initialTransfer;
        index.setIoBusy(&initialTransfer, true);
        bool changed = false;
        connect(&index, &DirectorySizeIndex::snapshotChanged, this, [&](const QString& path) {
            const auto s = index.snapshot(path);
            if (!changed && s.calculating && s.totals.files > 1000) {
                changed = true;
                write(root + "/added", 64);
                QVERIFY(QFile::remove(root + "/bulk/item-0"));
                index.observePaths({root + "/bulk/item-0"});
                index.setIoBusy(&initialTransfer, false);
            }
        });
        QElapsedTimer clock;
        clock.start();
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 30073, 30000));
        QVERIFY(changed);
        qInfo() << "30k initial scan with concurrent updates:" << clock.elapsed() << "ms";
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        write(root + "/target", 14);
        index.observePaths({ root + "/target" });
        QVERIFY(wait(index, root, 30077));
        qint64 stats = 0, listings = 0;
        for (const auto& event : work) {
            stats += event.at(0).toLongLong();
            listings += event.at(1).toLongLong();
        }
        QCOMPARE(listings, 0);
        QVERIFY(stats <= 2);
        qInfo() << "Single-file growth:" << stats << "file stats," << listings << "directory listings";
        work.clear();
        write(root + "/bulk/new-file", 5);
        index.observePaths({root + "/bulk/new-file"});
        index.observePaths({root + "/bulk/new-file"});
        QVERIFY(wait(index, root, 30082));
        for (const auto& event : work)
            QCOMPARE(event.at(1).toLongLong(), 0);
        QVERIFY(QFile::remove(root + "/bulk/new-file"));
        index.observePaths({root + "/bulk/new-file"});
        QVERIFY(wait(index, root, 30077));
        QObject transfer;
        index.setIoBusy(&transfer, true);
        rusage before { }, after { };
        ::getrusage(RUSAGE_SELF, &before);
        clock.restart();
        index.request(root, true);
        QVERIFY(wait(index, root, 30077, 30000));
        const auto throttled = clock.elapsed();
        ::getrusage(RUSAGE_SELF, &after);
        const double cpu = (after.ru_utime.tv_sec - before.ru_utime.tv_sec)
            + (after.ru_utime.tv_usec - before.ru_utime.tv_usec) / 1e6
            + (after.ru_stime.tv_sec - before.ru_stime.tv_sec)
            + (after.ru_stime.tv_usec - before.ru_stime.tv_usec) / 1e6;
        QVERIFY(cpu < throttled / 1000.0 * 0.35);
        index.foregroundDemand()->fetch_add(1);
        clock.restart();
        index.request(root, true);
        QVERIFY(wait(index, root, 30077, 30000));
        QVERIFY(clock.elapsed() < throttled * 0.75 + 100);
        index.foregroundDemand()->fetch_sub(1);
        index.setIoBusy(&transfer, false);
        qInfo() << "Transfer-priority scan:" << throttled << "ms," << cpu
                << "CPU seconds; foreground boost:" << clock.elapsed() << "ms";
    }
    void explicitAliasUsesTheSameRecord()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", alias = temp.path() + "/alias";
        write(root + "/nested/data", 7);
        QCOMPARE(::symlink(QFile::encodeName(root).constData(), QFile::encodeName(alias).constData()), 0);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite", nullptr, 0);
        index.retain(this, alias);
        index.request(alias);
        QVERIFY(wait(index, alias, 7));
        write(alias + "/nested/data", 19);
        index.observePaths({ alias + "/nested/data" });
        QVERIFY(wait(index, alias, 19));
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        index.request(root + "/nested");
        QVERIFY(wait(index, root + "/nested", 19));
        QCOMPARE(work.last().at(1).toLongLong(), 0);
    }
    void replacedDirectoryPublishesCurrentContents()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", child = root + "/nested";
        write(child + "/old", 7);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 7));
        QObject childReader; index.retain(&childReader, child);
        index.setWatchedDirectories(&childReader, {child});
        index.request(child);
        QVERIFY(wait(index, child, 7));
        QVERIFY(QDir().rename(child, temp.path() + "/old-directory"));
        write(child + "/replacement", 29);
        index.observePaths({ child });
        QVERIFY(wait(index, root, 29));
        QVERIFY(wait(index, child, 29));
        QCOMPARE(index.snapshot(child).totals.files, 1);
        QTRY_COMPARE_WITH_TIMEOUT(index.activeWatchCount(), 2, 10000);
        write(child + "/replacement", 41);
        QVERIFY(wait(index, root, 41));
        QVERIFY(wait(index, child, 41));
    }
    void privateDatabaseIsCountedWithoutFeedback()
    {
        QTemporaryDir temp;
        const auto root = temp.path();
        write(root + "/data", 13);
        DirectorySizeIndex index(root + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        const auto independent = scanDirectorySize(root, std::make_shared<std::atomic_bool>(false),
            std::make_shared<std::atomic_int>(1), std::make_shared<DirectorySizeProgress>(), false);
        QCOMPARE(index.snapshot(root).totals.bytes, independent.bytes);
        QCOMPARE(index.snapshot(root).totals.files, independent.files);
        QCOMPARE(index.snapshot(root).totals.folders, independent.folders);
        bool done = false;
        DirectorySizeSnapshot selected;
        connect(
            &index, &DirectorySizeIndex::selectionReady, this, [&](quint64, const DirectorySizeSnapshot& s) {
                selected = s;
                done = true;
            });
        index.inspectSelection({ root + "/cache", root + "/data" });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10000);
        QCOMPARE(selected.totals.bytes, index.snapshot(root).totals.bytes);
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        QTest::qWait(750);
        QCOMPARE(work.count(), 0);
        QVERIFY(!index.busy());
    }
    void stoppedSelectionAndCacheErrorsFinish()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/data", 11);
        {
            DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
            QSignalSpy results(&index, &DirectorySizeIndex::selectionReady);
            const auto id = index.inspectSelection({ root });
            index.cancelSelection(id);
            QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
            QCOMPARE(results.count(), 0);
        }
        write(temp.path() + "/blocked", 1);
        DirectorySizeIndex broken(temp.path() + "/blocked/index.sqlite");
        broken.request(root);
        QTRY_VERIFY_WITH_TIMEOUT(!broken.busy(), 10000);
        QCOMPARE(broken.snapshot(root).state, QString("error"));
        QVERIFY(!broken.snapshot(root).calculating);
        QVERIFY(!broken.snapshot(root).complete);
        QVERIFY(!broken.snapshot(root).error.isEmpty());
    }
    void idleDoesNotPollOrRescan()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/data", 5);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        index.retain(this, root);
        index.setWatchedDirectories(this, {root});
        index.request(root);
        QVERIFY(wait(index, root, 5));
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        rusage before { }, after { };
        const auto diskIo = [] {
            QHash<QByteArray,qint64> values;
            QFile io("/proc/self/io");
            if (io.open(QIODevice::ReadOnly)) for (const auto& line : io.readAll().split('\n')) {
                const auto parts=line.split(':');
                if(parts.size()==2) values[parts[0]]=parts[1].trimmed().toLongLong();
            }
            return values;
        };
        const auto ioBefore=diskIo();
        ::getrusage(RUSAGE_SELF, &before);
        QTest::qWait(1000);
        ::getrusage(RUSAGE_SELF, &after);
        const double cpu = (after.ru_utime.tv_sec - before.ru_utime.tv_sec)
            + (after.ru_utime.tv_usec - before.ru_utime.tv_usec) / 1e6
            + (after.ru_stime.tv_sec - before.ru_stime.tv_sec)
            + (after.ru_stime.tv_usec - before.ru_stime.tv_usec) / 1e6;
        const auto ioAfter=diskIo();
        qInfo() << "1s idle CPU:" << cpu << "seconds; physical I/O bytes read/write:"
                << ioAfter.value("read_bytes")-ioBefore.value("read_bytes")
                << ioAfter.value("write_bytes")-ioBefore.value("write_bytes")
                << "; peak RSS KiB:" << after.ru_maxrss << "; watches:" << index.activeWatchCount();
        QVERIFY(cpu < 0.08);
        QCOMPARE(work.count(), 0);
    }
    void activeReadersSurviveCacheEviction()
    {
        QTemporaryDir temp;
        const auto pinned = temp.path() + "/root-0";
        write(pinned + "/data", 1);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject consumer;
        index.retain(&consumer, pinned);
        QVERIFY(index.request(pinned));
        QVERIFY(wait(index, pinned, 1));
        for (int start = 1; start < 70; start += 16) {
            for (int i = start; i < qMin(start + 16, 70); ++i) {
                const auto path = temp.path() + "/root-" + QString::number(i);
                write(path + "/data", 1);
                QVERIFY(index.request(path));
            }
            QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        }
        QVERIFY(index.snapshot(pinned).available);
        write(pinned + "/data", 9);
        index.observePaths({ pinned + "/data" });
        QVERIFY(wait(index, pinned, 9));
        // A directory evicted from the GUI cache remains in the disk index.
        // Hints invalidate hidden records without keeping a recursive watch alive.
        const auto evicted = temp.path() + "/root-1";
        QVERIFY(!index.snapshot(evicted).available);
        write(evicted + "/data", 23);
        index.observePaths({ evicted + "/data" });
        QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        QVERIFY(index.request(evicted));
        QVERIFY(wait(index, evicted, 23));
        qint64 checked = 0; for (const auto& event : work) checked += event.at(0).toLongLong();
        QVERIFY(checked > 0); // verify hidden data only when requested
    }
    void eventLossInvalidatesHiddenTreesWithoutScanningThem()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files";
        write(root + "/deep/data", 10);
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite");
        QObject reader;
        index.retain(&reader, root); index.request(root);
        QVERIFY(wait(index, root, 10)); index.release(&reader);
        write(root + "/deep/data", 23);
        QSignalSpy work(&index, &DirectorySizeIndex::workCompleted);
        index.verifyAll();
        QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        QVERIFY(!index.snapshot(root).complete);
        for (const auto& event : work) {
            QCOMPARE(event.at(0).toLongLong(), 0);
            QCOMPARE(event.at(1).toLongLong(), 0);
        }
        index.retain(&reader, root); index.request(root);
        QVERIFY(wait(index, root, 23));
    }
    void remountedVolumeWithNewDeviceKeepsItsCachedTree()
    {
        // FUSE/CIFS/sshfs and re-enumerated USB disks get a new st_dev on each
        // (auto)mount. That must not erase and rescan the whole volume.
        QTemporaryDir temp;
        const auto volume = temp.path() + "/mnt/disk", database = temp.path() + "/cache/index.sqlite";
        for (int i = 0; i < 20; ++i)
            write(volume + "/folder-" + QString::number(i) + "/data", 10);
        auto work = workFor(database, {volume}, "first");
        work.mountBoundaries = {volume};
        auto result = runDirectorySizeWork(work);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.snapshots.value(volume).totals.bytes, 200);
        const auto connection = QStringLiteral("remount-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("UPDATE entries SET device='987654' WHERE kind=1 AND scope=0 AND name LIKE 'folder-%'"));
            QVERIFY(q.exec("UPDATE entries SET device='987654' WHERE name='disk'"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        work.session = "after-remount";
        work.requested = {volume + "/folder-3", volume};
        result = runDirectorySizeWork(work);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.snapshots.value(volume).state == QString("unavailable"), false);
        QCOMPARE(result.snapshots.value(volume).totals.bytes, 200);
        QCOMPARE(result.snapshots.value(volume + "/folder-3").totals.bytes, 10);
        QCOMPARE(result.directoryListings, 0);
    }
    void interruptedRootReusesCompleteSubtreesFromEarlierSessions()
    {
        // A scan cancelled by navigation leaves its root incomplete. The next
        // session must list only that root, not walk every cached subtree.
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        for (int i = 0; i < 20; ++i)
            write(root + "/folder-" + QString::number(i) + "/nested/data", 3);
        auto work = workFor(database, {root}, "first");
        QVERIFY(runDirectorySizeWork(work).error.isEmpty());
        const auto connection = QStringLiteral("interrupted-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("UPDATE entries SET known=0,unknowns=unknowns+1 WHERE name='files'"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        work.session = "second";
        const auto result = runDirectorySizeWork(work);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.snapshots.value(root).totals.bytes, 60);
        QVERIFY(result.snapshots.value(root).complete);
        QCOMPARE(result.directoryListings, 1);
    }
    void interruptedVerificationResumesAcrossRestarts()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        for (int i = 0; i < 10; ++i) {
            write(root + "/done/file-" + QString::number(i), 1);
            write(root + "/todo/file-" + QString::number(i), 1);
        }
        auto work = workFor(database, {root}, "first");
        work.invalidateAll = true; // starts a verification epoch
        QVERIFY(runDirectorySizeWork(work).error.isEmpty());
        const auto connection = QStringLiteral("resume-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("SELECT value FROM cache_info WHERE key='verification-epoch'") && q.next());
            const QString epoch = q.value(0).toString();
            q.finish();
            QVERIFY(!epoch.isEmpty());
            // A previous launch finished "done" before being interrupted.
            QVERIFY(q.exec("UPDATE roots SET needs_verify=2"));
            QVERIFY(q.exec("UPDATE entries SET verified='' WHERE name IN ('files','todo')"));
            QVERIFY(q.exec(QString("UPDATE entries SET verified='tree:%1:previous-launch' WHERE name='done'").arg(epoch)));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        work.session = "next-launch";
        work.invalidateAll = false;
        const auto result = runDirectorySizeWork(work);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.snapshots.value(root).totals.bytes, 20);
        QCOMPARE(result.fileStats, 10); // only the unfinished "todo" subtree
    }
    void legacyFullVerificationFlagsDoNotWalkTrees()
    {
        QTemporaryDir temp;
        const auto root = temp.path() + "/files", database = temp.path() + "/cache/index.sqlite";
        for (int i = 0; i < 10; ++i)
            write(root + "/folder/file-" + QString::number(i), 1);
        auto work = workFor(database, {root}, "first");
        QVERIFY(runDirectorySizeWork(work).error.isEmpty());
        const auto connection = QStringLiteral("legacy-verify-fixture");
        {
            auto db = QSqlDatabase::addDatabase("QSQLITE", connection);
            db.setDatabaseName(database);
            QVERIFY(db.open());
            QSqlQuery q(db);
            QVERIFY(q.exec("DELETE FROM cache_info WHERE key='verification-epoch'"));
            QVERIFY(q.exec("UPDATE roots SET needs_verify=2"));
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
        work.session = "upgraded";
        const auto result = runDirectorySizeWork(work);
        QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.snapshots.value(root).totals.bytes, 10);
        QCOMPARE(result.fileStats, 0);
    }
    void lostEventsInvalidateEvictedRecords()
    {
        QTemporaryDir temp;
        DirectorySizeIndex index(temp.path() + "/cache/index.sqlite", nullptr, 0);
        const auto oldest = temp.path() + "/root-0";
        for (int start = 0; start < 70; start += 16) {
            for (int i = start; i < qMin(start + 16, 70); ++i) {
                const auto path = temp.path() + "/root-" + QString::number(i);
                write(path + "/data", 1);
                QVERIFY(index.request(path));
            }
            QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        }
        QVERIFY(!index.snapshot(oldest).available);
        write(oldest + "/data", 23);
        // Simulate event loss while its persistent record is outside the LRU.
        index.verifyAll();
        QTRY_VERIFY_WITH_TIMEOUT(!index.busy(), 10000);
        QVERIFY(index.request(oldest));
        QVERIFY(wait(index, oldest, 23));
    }
};
QTEST_GUILESS_MAIN(DirectorySizeTests)
#include "directory_size_tests.moc"
