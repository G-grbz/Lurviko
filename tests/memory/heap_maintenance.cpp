#include <QtTest>
#include <QFile>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include "heapmaintenance.h"

static qint64 residentKiB()
{
    QFile status("/proc/self/status");
    if (!status.open(QIODevice::ReadOnly)) return 0;
    for (const auto &line : status.readAll().split('\n'))
        if (line.startsWith("VmRSS:")) return line.simplified().split(' ').value(1).toLongLong();
    return 0;
}

class MemoryTests : public QObject
{
    Q_OBJECT
private slots:
    void reclaimFreePagesPreservesLiveData()
    {
#ifdef __GLIBC__
#if __GLIBC_PREREQ(2, 33)
        using Allocation = std::unique_ptr<void, decltype(&std::free)>;
        std::vector<Allocation> temporary, live;
        // Interleave live guards to stop brk from shrinking automatically.
        // This models free holes left by listings and worker image decoding.
        for (int i = 0; i < 2048; ++i) {
            Allocation block(std::malloc(65536), &std::free);
            Allocation guard(std::malloc(128), &std::free);
            QVERIFY(block && guard);
            std::memset(block.get(), 0x5a, 65536);
            std::memset(guard.get(), 0x37, 128);
            temporary.push_back(std::move(block)); live.push_back(std::move(guard));
        }
        temporary.clear();
        const qint64 before = residentKiB();
        const auto liveBytes = ::mallinfo2().uordblks;
        // The actual application performs reclamation on a worker too.
        auto result = QtConcurrent::run([] { return HeapMaintenance::releaseUnusedHeap(); });
        QFutureWatcher<bool> watcher;
        QSignalSpy finished(&watcher, &QFutureWatcher<bool>::finished);
        watcher.setFuture(result);
        QVERIFY(finished.wait(10000));
        QVERIFY(result.result());
        const qint64 after = residentKiB();
        QVERIFY2(before - after > 32 * 1024, "free resident pages must actually return to the OS");
        QVERIFY(::mallinfo2().uordblks < liveBytes + 2 * 1024 * 1024);
        for (const auto &guard : live)
            for (int i = 0; i < 128; ++i) QCOMPARE(static_cast<const unsigned char *>(guard.get())[i], 0x37);
        qInfo("RSS before/after: %lld / %lld KiB; live guards preserved", (long long)before, (long long)after);
        return;
#endif
#endif
        QSKIP("Idle heap reclamation is available on glibc 2.33 and newer");
    }
};
QTEST_GUILESS_MAIN(MemoryTests)
#include "heap_maintenance.moc"
