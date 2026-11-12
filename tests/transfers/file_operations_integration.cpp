#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVirtualObject>
#include <QSettings>
#include <QTimer>
#include <KJob>
#include <KIO/CopyJob>
#include <KIO/Global>
#include <cstdio>
#include <functional>
#include <unistd.h>
#include "fileoperations.h"
#include "clipboardhistory.h"
#include "directorysizeindex.h"
#include "archiveinspectionjob.h"
#include "archiveoutput.h"

static void require(bool ok, const char *message) { if (!ok) { std::fprintf(stderr,"FAIL: %s\n",message); std::exit(1); } }
static void pause(int ms) { QEventLoop loop; QTimer::singleShot(ms,&loop,&QEventLoop::quit); loop.exec(); }
static bool until(const std::function<bool()> &check, int timeout=15000) {
    QElapsedTimer timer; timer.start(); while (!check() && timer.elapsed()<timeout) pause(10); return check();
}
static void write(const QString &path, const QByteArray &data) { QFile file(path); require(file.open(QIODevice::WriteOnly),"fixture opens"); require(file.write(data)==data.size(),"fixture writes"); }
static QByteArray read(const QString &path) { QFile file(path); require(file.open(QIODevice::ReadOnly),"result opens"); return file.readAll(); }
static QString url(const QString &path) { return QUrl::fromLocalFile(path).toString(); }
struct FileOperationsTestAccess {
    static KJob *active(FileOperations &ops) { return ops.m_operations.last().job; }
    static int track(FileOperations &ops,KJob *job) { const int id=ops.trackOperationJob(job,"copy","fixture",10); ops.finishJob(job,"done"); return id; }
    static void copy(FileOperations &ops,KIO::CopyJob *job,const QUrl &source,const QUrl &destination) { ops.trackTransferJob(job,{source},destination,"copy"); ops.finishJob(job,"done"); }
};
class ControlledJob : public KJob {
public:
    ControlledJob() { setCapabilities(KJob::Killable|KJob::Suspendable); setAutoDelete(false); }
    void start() override {}
    void progress() { setTotalAmount(KJob::Bytes,10000); setProcessedAmount(KJob::Bytes,2000); setProcessedAmount(KJob::Items,2); emitSpeed(1000); setPercent(100); }
    void complete(int error=0) { setError(error); setErrorText(error ? QString("controlled failure %1").arg(error) : QString()); emitResult(); }
    QString errorString() const override { return errorText(); }
protected:
    bool doKill() override { return true; } bool doSuspend() override { return true; } bool doResume() override { return true; }
};
class ArkRequestService : public QDBusVirtualObject {
public:
    bool requested = false;
    QString introspect(const QString &) const override {
        return QStringLiteral("<interface name=\"org.kde.ark.DndExtract\"><method name=\"extractSelectedFilesTo\">"
                              "<arg type=\"s\" direction=\"in\"/></method></interface>");
    }
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override {
        if (message.interface() != "org.kde.ark.DndExtract" || message.member() != "extractSelectedFilesTo") return false;
        requested = true;
        // Like Ark, acknowledge the request before the extraction job ends.
        connection.send(message.createReply());
        return true;
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Transfers"); QSettings().setValue("ui/language","en");
    const QString root=qEnvironmentVariable("LURVIKO_ARCHIVE_TEST_ROOT");
    FileOperations ops; ops.setKioProgressEnabled(false);
    bool finished=false,success=false,refresh=false; int results=0; QStringList resultUrls; QString lastMessage;
    QObject::connect(&ops,&FileOperations::operationFinished,&app,[&](bool ok,const QString &message,bool needsRefresh,bool){finished=true;success=ok;refresh=needsRefresh;lastMessage=message;++results;});
    QObject::connect(&ops,&FileOperations::transferItemsFinished,&app,[&](const QString &,const QStringList &urls){resultUrls=urls;});
    auto state=[&](int id) { for(const auto &v:ops.operations()) if(v.toMap().value("id").toInt()==id) return v.toMap(); return QVariantMap{}; };
    {
        ArkRequestService ark;
        auto bus = QDBusConnection::sessionBus();
        require(bus.registerVirtualObject("/DndExtract/Test", &ark), "Ark D-Bus fixture registers");
        const QString arkDestination = root + "/ark-idle";
        require(QDir().mkpath(arkDestination + "/test"), "Ark merge destination creates");
        int observations = 0;
        QStringList observedUrls;
        auto observedConnection = QObject::connect(&ops, &FileOperations::archiveDropItemsChanged, &app,
            [&](const QString &, const QStringList &urls, bool first) { if (first) ++observations; observedUrls = urls; });
        const int previousResults = results;
        ops.extractArkDrag(bus.baseService(), "/DndExtract/Test", url(arkDestination));
        require(until([&] { return ark.requested; }), "Ark extraction is requested asynchronously");
        pause(100);
        require(results == previousResults && ops.operations().isEmpty() && observations == 0,
                "Ark accepting a request never reports a completed extraction or fake progress");
        write(arkDestination + "/test/actual.txt", "observed contents");
        require(until([&] { return observations == 1; }) && observedUrls.contains(url(arkDestination + "/test")),
                "actual writes inside an existing directory report the merged result");
        require(results == previousResults && ops.operations().isEmpty(),
                "observing contents does not invent completion of Ark's external job");
        QObject::disconnect(observedConnection);
        bus.unregisterObject("/DndExtract/Test");
        ops.extractArkDrag(bus.baseService(), "/DndExtract/Missing", url(root));
        require(until([&] { return results > previousResults; }) && !success && !ops.lastError().isEmpty(),
                "unavailable Ark extraction reports an actual error");
    }
    for(int error:{0,int(KIO::ERR_DISK_FULL),int(KIO::ERR_ACCESS_DENIED),int(KIO::ERR_CONNECTION_BROKEN)}) {
        ControlledJob job; finished=false; const int id=FileOperationsTestAccess::track(ops,&job); job.progress();
        require(!state(id).value("finished").toBool() && state(id).value("percent").toInt()==99,"100 percent is not treated as success before result");
        require(state(id).value("speed").toInt()==1000,"real speed reaches progress state");
        require(ops.toggleOperationPause(id)&&state(id).value("paused").toBool(),"job suspends");
        require(ops.toggleOperationPause(id)&&!state(id).value("paused").toBool(),"job resumes");
        job.complete(error);
        require(finished&&success==(error==0)&&refresh,"real error/result and refresh propagate, including partial failures");
        require(state(id).value("processedBytes").toInt()==2000&&state(id).value("processedItems").toInt()==2,"completion never fabricates copied bytes/items");
        require(state(id).value("percent").toInt()==(error?99:100),"only confirmed success finishes at 100 percent");
        if(error) require(!ops.lastError().isEmpty(),"error detail retained");
    }
    ControlledJob cancel; const int cancelId=FileOperationsTestAccess::track(ops,&cancel); cancel.progress(); finished=false;
    require(ops.cancelOperation(cancelId)&&finished&&!success&&state(cancelId).value("cancelled").toBool(),"cancellation awaits EmitResult and is distinct from failure");
    require(!ops.cancelOperation(cancelId),"completed operation cannot cancel again");
    auto *lost=new ControlledJob; const int lostId=FileOperationsTestAccess::track(ops,lost); finished=false; delete lost;
    require(finished&&!success&&state(lostId).value("finished").toBool(),"job destroyed without result fails instead of hanging or assuming success");
    {
        // Going to Discover destroys the browser page and its FileOperations
        // while KIO jobs keep running: the app-wide instance takes them over.
        FileOperations app; app.setAdoptsOrphanedOperations(true);
        bool adoptedFinished=false, adoptedSuccess=false;
        QObject::connect(&app,&FileOperations::operationFinished,[&](bool ok,const QString&,bool,bool){adoptedFinished=true;adoptedSuccess=ok;});
        ControlledJob orphan;
        auto *page=new FileOperations;
        FileOperationsTestAccess::track(*page,&orphan); orphan.progress();
        require(app.activeOperationCountAll()>=1,"running jobs are counted across all instances");
        delete page;
        require(app.operations().size()==1,"app-wide instance adopts the running job of a destroyed page");
        const auto adopted=app.operations().first().toMap();
        require(!adopted.value("finished").toBool()&&adopted.value("processedBytes").toInt()==2000,"adopted job keeps its progress");
        const int adoptedId=adopted.value("id").toInt();
        require(app.toggleOperationPause(adoptedId)&&app.toggleOperationPause(adoptedId),"adopted job stays controllable");
        require(app.activeOperationCount()==1,"adopted job counts as active");
        orphan.complete(0);
        require(adoptedFinished&&adoptedSuccess&&app.operations().first().toMap().value("finished").toBool(),"adopted job completes in the app-wide instance");
        require(app.activeOperationCount()==0,"completed adopted job is no longer active");
        app.setAdoptsOrphanedOperations(false);
    }
    // Actual local KIO paths use the same tracker as remote jobs.
    QDir().mkpath(root+"/source"); QDir().mkpath(root+"/destination"); QDir().mkpath(root+"/moved");
    const QString source=root+"/source/item.txt", destination=root+"/destination"; write(source,"source data");
    auto *sizes = DirectorySizeIndex::instance();
    QObject sourceReader, destinationReader, movedReader;
    sizes->retain(&sourceReader, root+"/source");
    sizes->retain(&destinationReader, destination);
    sizes->retain(&movedReader, root+"/moved");
    sizes->setWatchedDirectories(&sourceReader,{root+"/source"});
    sizes->setWatchedDirectories(&destinationReader,{destination});
    sizes->setWatchedDirectories(&movedReader,{root+"/moved"});
    require(until([&]{return sizes->activeWatchCount()==3;}),"three visible roots share the central watcher");
    const auto exactSize = [&](const QString &path, qint64 expected) {
        const bool matched = until([&] {
            const auto snapshot = sizes->snapshot(path);
            return !sizes->busy() && snapshot.complete && !snapshot.calculating
                && snapshot.totals.bytes == expected;
        });
        if (!matched) { const auto s=sizes->snapshot(path); std::fprintf(stderr,"SIZE: %s expected=%lld actual=%lld state=%s complete=%d calc=%d busy=%d error=%s\n",qPrintable(path),(long long)expected,(long long)s.totals.bytes,qPrintable(s.state),s.complete,s.calculating,sizes->busy(),qPrintable(s.error)); }
        require(matched, "shared size index matches actual KIO results");
    };
    sizes->request(root+"/source"); sizes->request(destination); sizes->request(root+"/moved");
    exactSize(root+"/source", 11); exactSize(destination, 0); exactSize(root+"/moved", 0);
    finished=false; resultUrls.clear(); ops.copyItem(url(source),url(destination));
    require(ops.ioBusy()&&!finished,"copy starts asynchronously");
    require(until([&]{return finished;})&&success,"real KIO copy completes");
    require(read(destination+"/item.txt")=="source data"&&QFile::exists(source),"copy preserves source and content");
    require(resultUrls==QStringList{url(destination+"/item.txt")},"selection results contain actual copied destination");
    exactSize(destination, 11); exactSize(root+"/source", 11);
    write(destination+"/item.txt","keep existing"); finished=false; resultUrls.clear();
    auto *skip=KIO::copy(QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination),KIO::HideProgressInfo); skip->setAutoSkip(true);
    FileOperationsTestAccess::copy(ops,skip,QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination));
    require(until([&]{return finished;})&&success&&read(destination+"/item.txt")=="keep existing"&&resultUrls.isEmpty(),"Skip preserves existing file and never reports a guessed destination");
    exactSize(destination, 13);
    finished=false;
    auto *overwrite=KIO::copy(QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination),KIO::HideProgressInfo|KIO::Overwrite);
    FileOperationsTestAccess::copy(ops,overwrite,QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination));
    require(until([&]{return finished;})&&success&&read(destination+"/item.txt")=="source data"&&read(source)=="source data","confirmed overwrite replaces destination, preserves source");
    exactSize(destination, 11);
    finished=false; ops.moveItem(url(destination+"/item.txt"),url(root+"/moved"));
    require(until([&]{return finished;})&&success&&QFile::exists(root+"/moved/item.txt")&&!QFile::exists(destination+"/item.txt"),"real KIO move completes");
    exactSize(destination, 0); exactSize(root+"/moved", 11);
    finished=false; ops.duplicateItem(url(source));
    require(until([&]{return finished;})&&success&&resultUrls.size()==1&&read(QUrl(resultUrls.first()).toLocalFile())=="source data","duplicate uses actual auto-renamed destination");
    exactSize(root+"/source", 22);
    finished=false; ops.copyItem(url(source),url(destination));
    const int realCancel=ops.operations().last().toMap().value("id").toInt();
    require(ops.cancelOperation(realCancel),"real KIO copy cancels before writing");
    require(until([&]{return finished;})&&!success&&read(source)=="source data","cancelled copy preserves source");
    finished=false; ops.copyItem(url(root+"/source/missing"),url(destination));
    require(until([&]{return finished;})&&!success&&!ops.lastError().isEmpty(),"real missing-source error is reported");
    // A real permission failure, without a root requirement.
    const QString denied=root+"/denied"; QDir().mkpath(denied); QFile::setPermissions(denied,QFile::ReadOwner|QFile::ExeOwner);
    if (::geteuid() != 0) {
        finished=false; ops.copyItem(url(source),url(denied));
        require(until([&]{return finished;})&&!success&&read(source)=="source data","permission-denied copy leaves source intact");
    } else std::fprintf(stderr,"SKIP: real permission-denied fixture requires a non-root user; controlled KIO error path is still tested\n");
    QFile::setPermissions(denied,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    const QString trashed=root+"/source/trash-me.txt"; write(trashed,"restore me");
    finished=false; ops.trashItem(url(trashed));
    QString trashUrl;
    QObject::connect(qobject_cast<KIO::CopyJob*>(FileOperationsTestAccess::active(ops)),&KIO::CopyJob::copyingDone,&app,
        [&](KIO::Job *,const QUrl &from,const QUrl &to,const QDateTime &,bool,bool) { if(from==QUrl::fromLocalFile(trashed)) trashUrl=to.toString(); });
    const bool trashDone=until([&]{return finished;});
    if(!trashDone||!success) std::fprintf(stderr,"TRASH RESULT: finished=%d success=%d %s\n",finished,success,qPrintable(lastMessage));
    require(trashDone&&success&&!QFile::exists(trashed),"actual trash succeeds");
    exactSize(root+"/source", 22);
    require(!trashUrl.isEmpty(),"KIO reports actual trash URL including its volume id");
    finished=false; ops.restoreFromTrash({trashUrl});
    require(ops.ioBusy(),"restore is tracked immediately");
    const bool restored=until([&]{return finished;});
    if(!restored||!success) std::fprintf(stderr,"RESTORE RESULT: %s %s\n",qPrintable(trashUrl),qPrintable(lastMessage));
    require(restored&&success&&read(trashed)=="restore me","actual trash restore succeeds and is tracked");
    exactSize(root+"/source", 32);
    finished=false; ops.removeItem(url(trashed));
    require(ops.ioBusy()&&ops.operations().last().toMap().value("kind")=="delete","permanent deletion is tracked");
    require(until([&]{return finished;})&&success&&!QFile::exists(trashed),"actual permanent delete succeeds");
    exactSize(root+"/source", 22);
    std::fprintf(stderr,"PASS: shared size totals follow real KIO copy/move, skip/overwrite, duplicate, trash/restore and deletion without forced scans\n");
    // Archive publication is exclusive, including a name that appeared while writing.
    const QString collision=root+"/collision.zip"; QString staging;
    { ArchiveOutput output(root,"collision",".zip"); require(output.valid(),"exclusive archive staging creates"); staging=output.path();
      write(staging,"new archive"); write(collision,"existing archive"); QString error;
      require(output.publish(&error)==root+"/collision (1).zip"&&read(collision)=="existing archive","publication never overwrites a racing destination"); }
    require(!QFile::exists(staging)&&read(root+"/collision (1).zip")=="new archive","staging cleanup preserves published archive");
    { ArchiveOutput cancelled(root,"cancelled",".zip"); staging=cancelled.path(); write(staging,"incomplete"); }
    require(!QFile::exists(staging)&&!QFile::exists(root+"/cancelled.zip"),"failed/cancelled staging cannot leave an invalid published archive");
    // Slow archive metadata/conflict work is performed in its worker, with responsive GUI and cancellation.
    const QString probe=root+"/slow-probe"; write(probe,"#!/bin/sh\nexec sleep 20\n");
    QFile::setPermissions(probe,QFile::ReadOwner|QFile::WriteOwner|QFile::ExeOwner);
    int ticks=0; QTimer heartbeat; heartbeat.setInterval(5); QObject::connect(&heartbeat,&QTimer::timeout,&app,[&]{++ticks;}); heartbeat.start();
    auto *inspection=new ArchiveInspectionJob(probe,source,destination,"Inspecting archive",&ops);
    const int inspectionId=FileOperationsTestAccess::track(ops,inspection); inspection->start(); pause(200);
    require(ticks>10&&!state(inspectionId).value("finished").toBool(),"slow archive inspection never blocks GUI event loop");
    require(ops.cancelOperation(inspectionId)&&state(inspectionId).value("cancelled").toBool(),"inspection can be cancelled before any extraction write");
    pause(200); require(read(source)=="source data","inspection cancellation never writes to source/destination");
    {
        // The clipboard holds only the latest Copy/Cut: a new one replaces the
        // previous card (in any pane) instead of stacking a second toast, and a
        // superseded in-flight preparation is never shown as failed.
        auto clipboardCards=[](const FileOperations &instance) {
            QVariantList cards;
            for(const auto &v:instance.operations()) if(v.toMap().value("kind").toString()=="clipboard") cards.push_back(v);
            return cards; };
        FileOperations otherPane; otherPane.setKioProgressEnabled(false);
        ops.copyToClipboard({url(root+"/clip-a.txt")},false);
        require(until([&]{ const auto c=clipboardCards(ops); return c.size()==1&&c.first().toMap().value("finished").toBool(); }),"first copy shows one clipboard card");
        ops.copyToClipboard({url(root+"/clip-b.txt")},true);
        require(until([&]{ const auto c=clipboardCards(ops); return c.size()==1&&c.first().toMap().value("finished").toBool(); }),"second cut replaces the first card");
        require(clipboardCards(ops).first().toMap().value("success").toBool(),"replacement card reports success");
        // Two requests back to back: the first is still being prepared.
        ops.copyToClipboard({url(root+"/clip-c.txt")},false);
        ops.copyToClipboard({url(root+"/clip-d.txt")},false);
        require(clipboardCards(ops).size()==1,"a superseded preparation leaves no extra card");
        require(until([&]{ const auto c=clipboardCards(ops); return c.size()==1&&c.first().toMap().value("finished").toBool(); }),"latest preparation finishes");
        require(clipboardCards(ops).first().toMap().value("success").toBool(),"superseded preparation is not shown as failed");
        require(ops.clipboardUrls()==QStringList{url(root+"/clip-d.txt")},"clipboard holds the latest selection");
        otherPane.copyToClipboard({url(root+"/clip-e.txt")},false);
        require(until([&]{ const auto c=clipboardCards(otherPane); return c.size()==1&&c.first().toMap().value("finished").toBool(); }),"other pane shows its copy");
        require(clipboardCards(ops).isEmpty(),"a copy in another pane replaces this pane's clipboard card");
    }
    {
        // Clipboard history: one entry per Copy/Cut, recorded from the system
        // clipboard; deleted items drop out; restoring keeps Copy vs Cut.
        ClipboardHistory &history=*ClipboardHistory::instance();
        history.clear();
        const int savedLimit=history.maxEntries();
        auto top=[&]{ return history.entries().value(0).toMap(); };
        const QString dir=root+"/history"; require(QDir().mkpath(dir+"/sub"),"history fixture folder");
        for(const char *name:{"a.txt","b.txt","c.txt"}) write(dir+"/"+name,"x");
        write(dir+"/sub/d.txt","x");
        const QStringList three={url(dir+"/a.txt"),url(dir+"/b.txt"),url(dir+"/c.txt")};
        ops.copyToClipboard(three,false);
        require(until([&]{ return history.count()==1&&top().value("count").toInt()==3; }),"a Copy of three files is one history entry");
        require(!top().value("cut").toBool()&&top().value("current").toBool(),"entry is a current Copy");
        const QString shownDir=dir.startsWith(QDir::homePath()+"/")?"~"+dir.mid(QDir::homePath().size()):dir;
        require(top().value("location").toString()==shownDir&&!top().value("mixed").toBool(),"entry shows where it was copied from, with ~ for home");
        ops.copyToClipboard({url(dir+"/a.txt")},true);
        require(until([&]{ return history.count()==2&&top().value("cut").toBool(); }),"a Cut is a separate entry");
        ops.copyToClipboard({url(dir+"/a.txt"),url(dir+"/sub/d.txt")},false);
        require(until([&]{ return history.count()==3&&top().value("mixed").toBool(); }),"items from several folders are marked");
        ops.copyToClipboard(three,false);
        require(until([&]{ return history.count()==3&&top().value("count").toInt()==3; }),"copying the same selection again moves its entry to the top");
        // Restoring an older entry makes Paste use it, as Copy or as Cut.
        int cutId=0; for(const auto &v:history.entries()) if(v.toMap().value("cut").toBool()) cutId=v.toMap().value("id").toInt();
        require(cutId>0&&history.restore(cutId),"restore a Cut entry");
        require(until([&]{ return ops.clipboardCut()&&ops.clipboardUrls()==QStringList{url(dir+"/a.txt")}; }),"restored Cut is on the clipboard as a Cut");
        require(top().value("id").toInt()==cutId&&history.count()==3,"restored entry moves to the top without duplicating");
        // Deleted files drop out; an entry with nothing left is removed.
        require(QFile::remove(dir+"/b.txt"),"delete one copied file");
        history.refresh();
        require(until([&]{ for(const auto &v:history.entries()) if(!v.toMap().value("cut").toBool()&&!v.toMap().value("mixed").toBool()) return v.toMap().value("count").toInt()==2; return false; }),"a deleted file drops out of its entry");
        require(QFile::remove(dir+"/a.txt"),"delete the cut file");
        history.refresh();
        require(until([&]{ for(const auto &v:history.entries()) if(v.toMap().value("cut").toBool()) return false; return !history.validating(); }),"an entry whose files are all gone is removed");
        require(QDir(dir).removeRecursively(),"delete the source folder");
        history.refresh();
        require(until([&]{ return history.count()==0; }),"deleting the folder removes its entries");
        // Remote and web URLs are not recorded; the size limit applies.
        history.record({"smb://server/share/file.txt","gdrive:/file","https://example.com/page"},false);
        require(history.count()==0,"remote and web URLs are not recorded");
        history.setMaxEntries(5);
        for(int i=0;i<7;++i) history.record({url(root+QString("/limit-%1.txt").arg(i))},false);
        require(history.count()==5&&top().value("names").toStringList()==QStringList{"limit-6.txt"},"oldest entries drop beyond the limit");
        history.setMaxEntries(1); require(history.maxEntries()==5,"limit is at least 5");
        // Optional persistence: kept on disk (owner-only) only with
        // clearOnExit off, restored by the next instance, removed when on.
        require(history.clearOnExit(),"history is cleared on exit by default");
        const QString saved=ClipboardHistory::storagePath();
        QFile::remove(saved);
        history.setClearOnExit(false);
        history.record({url(root+"/kept.txt")},true);
        require(until([&]{ return QFile::exists(saved)&&read(saved).contains("kept.txt"); }),"history is saved when kept");
        require(QFileInfo(saved).permissions()==(QFileDevice::ReadOwner|QFileDevice::WriteOwner|QFileDevice::ReadUser|QFileDevice::WriteUser),
                "saved history is readable by its owner only");
        {
            // Whatever is on the clipboard at startup is the newest entry; the
            // saved ones follow in their saved order.
            auto names=[](const QVariantList &entries) {
                QStringList list; for(const auto &v:entries) list.push_back(v.toMap().value("names").toStringList().join(",")+(v.toMap().value("cut").toBool()?"[cut]":""));
                return list; };
            const QStringList expected=names(history.entries());
            ClipboardHistory restored;
            require(until([&]{ return names(restored.entries()).contains("kept.txt[cut]"); }),"next start restores the saved history");
            const QStringList got=names(restored.entries());
            const qsizetype start=got.indexOf("kept.txt[cut]");
            require(got.mid(start)==expected.mid(0,got.size()-start),"restored entries keep their order and Cut");
        }
        history.setClearOnExit(true);
        require(!QFile::exists(saved),"turning clearing back on removes the saved history");
        history.setMaxEntries(savedLimit);
        history.clear();
    }
    std::fprintf(stderr,"PASS: asynchronous KIO copy/move/duplicate/trash/restore/delete, skip/overwrite, errors, cancellation, counters, exclusive archive output, responsive inspection, a single clipboard card and clipboard history\n");
}
