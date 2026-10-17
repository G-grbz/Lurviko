#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QTimer>
#include <KJob>
#include <KIO/CopyJob>
#include <KIO/Global>
#include <cstdio>
#include <functional>
#include <unistd.h>
#include "fileoperations.h"
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
int main(int argc,char **argv) {
    QApplication app(argc,argv); app.setOrganizationName("Lurviko-QA"); app.setApplicationName("Transfers"); QSettings().setValue("ui/language","en");
    const QString root=qEnvironmentVariable("LURVIKO_ARCHIVE_TEST_ROOT");
    FileOperations ops; ops.setKioProgressEnabled(false);
    bool finished=false,success=false,refresh=false; int results=0; QStringList resultUrls; QString lastMessage;
    QObject::connect(&ops,&FileOperations::operationFinished,&app,[&](bool ok,const QString &message,bool needsRefresh,bool){finished=true;success=ok;refresh=needsRefresh;lastMessage=message;++results;});
    QObject::connect(&ops,&FileOperations::transferItemsFinished,&app,[&](const QString &,const QStringList &urls){resultUrls=urls;});
    auto state=[&](int id) { for(const auto &v:ops.operations()) if(v.toMap().value("id").toInt()==id) return v.toMap(); return QVariantMap{}; };
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
    // Actual local KIO paths use the same tracker as remote jobs.
    QDir().mkpath(root+"/source"); QDir().mkpath(root+"/destination"); QDir().mkpath(root+"/moved");
    const QString source=root+"/source/item.txt", destination=root+"/destination"; write(source,"source data");
    finished=false; resultUrls.clear(); ops.copyItem(url(source),url(destination));
    require(ops.ioBusy()&&!finished,"copy starts asynchronously");
    require(until([&]{return finished;})&&success,"real KIO copy completes");
    require(read(destination+"/item.txt")=="source data"&&QFile::exists(source),"copy preserves source and content");
    require(resultUrls==QStringList{url(destination+"/item.txt")},"selection results contain actual copied destination");
    write(destination+"/item.txt","keep existing"); finished=false; resultUrls.clear();
    auto *skip=KIO::copy(QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination),KIO::HideProgressInfo); skip->setAutoSkip(true);
    FileOperationsTestAccess::copy(ops,skip,QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination));
    require(until([&]{return finished;})&&success&&read(destination+"/item.txt")=="keep existing"&&resultUrls.isEmpty(),"Skip preserves existing file and never reports a guessed destination");
    finished=false;
    auto *overwrite=KIO::copy(QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination),KIO::HideProgressInfo|KIO::Overwrite);
    FileOperationsTestAccess::copy(ops,overwrite,QUrl::fromLocalFile(source),QUrl::fromLocalFile(destination));
    require(until([&]{return finished;})&&success&&read(destination+"/item.txt")=="source data"&&read(source)=="source data","confirmed overwrite replaces destination, preserves source");
    finished=false; ops.moveItem(url(destination+"/item.txt"),url(root+"/moved"));
    require(until([&]{return finished;})&&success&&QFile::exists(root+"/moved/item.txt")&&!QFile::exists(destination+"/item.txt"),"real KIO move completes");
    finished=false; ops.duplicateItem(url(source));
    require(until([&]{return finished;})&&success&&resultUrls.size()==1&&read(QUrl(resultUrls.first()).toLocalFile())=="source data","duplicate uses actual auto-renamed destination");
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
    require(!trashUrl.isEmpty(),"KIO reports actual trash URL including its volume id");
    finished=false; ops.restoreFromTrash({trashUrl});
    require(ops.ioBusy(),"restore is tracked immediately");
    const bool restored=until([&]{return finished;});
    if(!restored||!success) std::fprintf(stderr,"RESTORE RESULT: %s %s\n",qPrintable(trashUrl),qPrintable(lastMessage));
    require(restored&&success&&read(trashed)=="restore me","actual trash restore succeeds and is tracked");
    finished=false; ops.removeItem(url(trashed));
    require(ops.ioBusy()&&ops.operations().last().toMap().value("kind")=="delete","permanent deletion is tracked");
    require(until([&]{return finished;})&&success&&!QFile::exists(trashed),"actual permanent delete succeeds");
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
    std::fprintf(stderr,"PASS: asynchronous KIO copy/move/duplicate/trash/restore/delete, skip/overwrite, errors, cancellation, counters, exclusive archive output and responsive inspection\n");
}
