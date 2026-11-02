#include "filemetadata_p.h"
#include "directorysizestore_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStorageInfo>
#include <QThread>
#include <QUuid>
#include <dirent.h>
#include <fcntl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <cerrno>
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>
#include <vector>

QSet<QString> directorySizeMountBoundaries()
{
    QSet<QString> paths { "/proc", "/sys", "/dev", "/run" };
    // Qt filters some virtual filesystems out of mountedVolumes(). Linux's
    // mount table also covers bind mounts and must define our traversal limits.
    QFile mounts(QStringLiteral("/proc/self/mountinfo"));
    if (mounts.open(QIODevice::ReadOnly)) {
        const auto lines = mounts.read(1024 * 1024).split('\n');
        for (const auto& line : lines) {
            const auto fields = line.split(' ');
            if (fields.size() < 6)
                continue;
            auto path = fields.at(4);
            path.replace("\\040", " ").replace("\\011", "\t").replace("\\012", "\n").replace("\\134", "\\");
            if (path != "/")
                paths.insert(QDir::cleanPath(QFile::decodeName(path)));
        }
    } else {
        for (const auto& volume : QStorageInfo::mountedVolumes())
            if (volume.rootPath() != "/")
                paths.insert(QDir::cleanPath(volume.rootPath()));
    }
    return paths;
}

namespace {
constexpr auto columns
    = "id,parent,name,kind,bytes,files,folders,unknowns,known,device,inode,mtime,ctime,verified,scope";
struct Totals {
    qint64 bytes = 0, files = 0, folders = 0, unknowns = 0;
    Totals operator-(const Totals& v) const
    {
        return { bytes - v.bytes, files - v.files, folders - v.folders, unknowns - v.unknowns };
    }
};
struct Node {
    qint64 id = 0, parent = 0;
    QString name;
    int kind = 0;
    Totals totals;
    bool known = false;
    QString device, inode;
    qint64 modified = 0, changed = 0;
    QString verified;
    int scope = 0; // 1: independent mounted volume; 2: excluded virtual filesystem.
    Totals contribution(bool toParent = true) const
    {
        if (toParent && scope)
            return { };
        auto v = totals;
        if (kind == 1)
            ++v.folders;
        return v;
    }
};
qint64 stamp(const timespec& v) { return qint64(v.tv_sec) * 1000000000 + v.tv_nsec; }
qint64 cpuTime()
{
    timespec v { };
    ::clock_gettime(CLOCK_THREAD_CPUTIME_ID, &v);
    return stamp(v);
}
QString device(const struct stat& v) { return QString::number(qulonglong(v.st_dev)); }
QString inode(const struct stat& v) { return QString::number(qulonglong(v.st_ino)); }
bool within(const QString& path, const QString& root)
{
    return path == root || path.startsWith(root == "/" ? root : root + '/');
}

class Store {
public:
    explicit Store(const DirectorySizeWork& input)
        : work(input)
        , connection(QUuid::createUuid().toString(QUuid::Id128))
    {
    }
    void initialize(bool readOnly = false)
    {
        ownDirectory = QFileInfo(work.databasePath).absolutePath();
        if (!readOnly && !QDir().mkpath(ownDirectory))
            throw std::runtime_error("Cannot create directory-size cache");
        db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connection);
        db.setDatabaseName(work.databasePath);
        db.setConnectOptions(readOnly ? QStringLiteral("QSQLITE_OPEN_READONLY;QSQLITE_BUSY_TIMEOUT=150")
                                    : QStringLiteral("QSQLITE_BUSY_TIMEOUT=3000"));
        if (!db.open())
            fail(db.lastError().text());
        sql(readOnly ? "PRAGMA cache_size=-2048" : "PRAGMA cache_size=-8192");
        if (readOnly) {
            byId = prepared(QString("SELECT %1 FROM entries WHERE id=?").arg(columns));
            byName = prepared(QString("SELECT %1 FROM entries WHERE parent=? AND name=?").arg(columns));
            auto policy = sql("SELECT value FROM cache_info WHERE key='scope-accounting'");
            if (!policy.next() || policy.value(0).toInt() < 2)
                fail("Directory-size cache upgrade is in progress.");
            mountBoundaries = directorySizeMountBoundaries();
            return;
        }
        QFile::setPermissions(work.databasePath, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        // Independent readers must be able to restore cached child totals
        // while the serial writer is verifying a large parent tree.
        sql("PRAGMA journal_mode=WAL");
        mountBoundaries = directorySizeMountBoundaries();
        virtualRoots = { "/proc", "/sys", "/dev", "/run" };
        for (const auto& path : work.virtualRoots)
            virtualRoots.insert(QDir::cleanPath(path));
        for (const auto& path : work.mountBoundaries)
            mountBoundaries.insert(QDir::cleanPath(path));
        sql("PRAGMA temp_store=FILE");
        sql("PRAGMA synchronous=NORMAL");
        sql("CREATE TABLE IF NOT EXISTS entries(id INTEGER PRIMARY KEY,parent INTEGER,name TEXT NOT "
            "NULL,kind INTEGER NOT NULL,"
            "bytes INTEGER NOT NULL DEFAULT 0,files INTEGER NOT NULL DEFAULT 0,folders INTEGER NOT NULL "
            "DEFAULT 0,"
            "unknowns INTEGER NOT NULL DEFAULT 0,known INTEGER NOT NULL DEFAULT 0,device TEXT,inode TEXT,"
            "mtime INTEGER,ctime INTEGER,seen TEXT,verified TEXT,scope INTEGER NOT NULL DEFAULT 0,UNIQUE(parent,name))");
        ensureColumn("entries", "scope", "INTEGER NOT NULL DEFAULT 0");
        sql("CREATE INDEX IF NOT EXISTS entry_parent ON entries(parent)");
        sql("CREATE INDEX IF NOT EXISTS entry_identity ON entries(device,inode) WHERE kind=2");
        sql("CREATE INDEX IF NOT EXISTS directory_parent ON entries(parent,id) WHERE kind=1");
        sql("CREATE TABLE IF NOT EXISTS roots(path TEXT PRIMARY KEY,node INTEGER,mount TEXT,watch_session TEXT,needs_verify INTEGER NOT NULL DEFAULT 0)");
        ensureColumn("roots", "watch_session", "TEXT");
        ensureColumn("roots", "needs_verify", "INTEGER NOT NULL DEFAULT 0");
        sql("CREATE TABLE IF NOT EXISTS cache_info(key TEXT PRIMARY KEY,value TEXT)");
        sql("INSERT OR IGNORE INTO entries(id,parent,name,kind,unknowns) VALUES(1,NULL,'',1,1)");
        if (!db.transaction())
            fail(db.lastError().text());
        byId = prepared(QString("SELECT %1 FROM entries WHERE id=?").arg(columns));
        byName = prepared(QString("SELECT %1 FROM entries WHERE parent=? AND name=?").arg(columns));
        insert = prepared(
            "INSERT INTO entries(parent,name,kind,bytes,files,unknowns,known,device,inode,mtime,ctime,seen,scope) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");
        update = prepared("UPDATE entries SET device=?,inode=?,mtime=?,ctime=?,seen=?,scope=? WHERE id=?");
        if (work.invalidateAll)
            sql("UPDATE roots SET needs_verify=1");
        for (const auto& path : work.invalidatedPaths)
            sql("UPDATE roots SET needs_verify=1 WHERE path='/' OR path=? "
                "OR substr(?,1,length(path)+1)=path||'/' OR substr(path,1,?)=?",
                {path, path, path.size()+1, path+'/' });
        reconcileScopes();
        sql("INSERT INTO cache_info(key,value) VALUES('scope-accounting','2') "
            "ON CONFLICT(key) DO UPDATE SET value=excluded.value WHERE value<>excluded.value");
        epoch = QUuid::createUuid().toString(QUuid::Id128);
        budget = cpuTime();
    }
    ~Store()
    {
        byId = QSqlQuery();
        byName = QSqlQuery();
        insert = QSqlQuery();
        update = QSqlQuery();
        if (db.isOpen()) {
            db.rollback();
            db.close();
        }
        db = { };
        QSqlDatabase::removeDatabase(connection);
    }
    void fail(const QString& message) { throw std::runtime_error(message.toStdString()); }
    void ensureColumn(const QString& table, const QString& name, const QString& definition)
    {
        auto info = sql("PRAGMA table_info(" + table + ')');
        while (info.next())
            if (info.value(1).toString() == name) return;
        info.finish();
        sql("ALTER TABLE " + table + " ADD COLUMN " + name + ' ' + definition);
    }
    QSqlQuery prepared(const QString& text)
    {
        QSqlQuery q(db);
        if (!q.prepare(text))
            fail(q.lastError().text());
        return q;
    }
    void exec(QSqlQuery& q, const QVariantList& values = { })
    {
        for (int i = 0; i < values.size(); ++i)
            q.bindValue(i, values.at(i));
        if (!q.exec())
            fail(q.lastError().text());
    }
    QSqlQuery sql(const QString& text, const QVariantList& values = { })
    {
        auto q = prepared(text);
        exec(q, values);
        return q;
    }
    static Node read(const QSqlQuery& q)
    {
        Node n;
        n.id = q.value(0).toLongLong();
        n.parent = q.value(1).toLongLong();
        n.name = q.value(2).toString();
        n.kind = q.value(3).toInt();
        n.totals = { q.value(4).toLongLong(), q.value(5).toLongLong(), q.value(6).toLongLong(),
            q.value(7).toLongLong() };
        n.known = q.value(8).toBool();
        n.device = q.value(9).toString();
        n.inode = q.value(10).toString();
        n.modified = q.value(11).toLongLong();
        n.changed = q.value(12).toLongLong();
        n.verified = q.value(13).toString();
        n.scope = q.value(14).toInt();
        return n;
    }
    Node node(qint64 id)
    {
        exec(byId, { id });
        Node n;
        if (byId.next())
            n = read(byId);
        byId.finish();
        return n;
    }
    Node child(qint64 parent, const QString& name)
    {
        exec(byName, { parent, name });
        Node n;
        if (byName.next())
            n = read(byName);
        byName.finish();
        return n;
    }
    Node lookup(const QString& path)
    {
        Node n = node(1);
        for (const auto& name : path.split('/', Qt::SkipEmptyParts)) {
            n = child(n.id, name);
            if (!n.id)
                break;
        }
        return n;
    }
    int scopeFor(const QString& path, int previous = 0) const
    {
        for (const auto& root : virtualRoots)
            if (within(path, root)) return 2;
        return mountBoundaries.contains(path) ? 1 : previous;
    }
    void reconcileScopes()
    {
        auto paths = mountBoundaries.values();
        paths.append(virtualRoots.values());
        paths.removeDuplicates();
        std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
        for (const auto& path : paths) {
            Node n = lookup(path);
            const int scope = scopeFor(path, n.scope);
            if (!n.id || n.scope == scope)
                continue;
            const auto before = n.contribution();
            sql("UPDATE entries SET scope=? WHERE id=?", { scope, n.id });
            n.scope = scope;
            // Keep the volume's own records, but remove its whole contribution
            // from every enclosing local directory, including legacy caches.
            propagate(n.parent, n.contribution() - before);
        }
    }
    void propagate(qint64 id, const Totals& delta)
    {
        if (!delta.bytes && !delta.files && !delta.folders && !delta.unknowns)
            return;
        while (id) {
            const Node n = node(id);
            sql("UPDATE entries SET bytes=bytes+?,files=files+?,folders=folders+?,unknowns=unknowns+? WHERE "
                "id=?",
                { delta.bytes, delta.files, delta.folders, delta.unknowns, id });
            if (n.scope)
                break;
            id = n.parent;
        }
    }
    void erase(qint64 id)
    {
        sql("WITH RECURSIVE gone(id) AS (SELECT ? UNION ALL SELECT e.id FROM entries e JOIN gone g ON "
            "e.parent=g.id) "
            "DELETE FROM entries WHERE id IN (SELECT id FROM gone)",
            { id });
    }
    Node upsert(qint64 parent, const QString& name, const struct stat& info, bool seen = true,
        const Node* existing = nullptr, int scope = -1)
    {
        Node old = existing ? *existing : child(parent, name);
        if (scope < 0) scope = old.scope;
        const int kind = S_ISDIR(info.st_mode) ? 1 : S_ISREG(info.st_mode) ? 2 : 0;
        if (old.id
            && (old.kind != kind
                || (kind == 1 && (old.device != device(info) || old.inode != inode(info))))) {
            erase(old.id);
            old = { };
        }
        if (!old.id) {
            exec(insert,
                { parent, name, kind, kind == 2 ? qint64(info.st_size) : 0, kind == 2 ? 1 : 0,
                    kind == 1 ? 1 : 0, kind != 1, device(info), inode(info), stamp(info.st_mtim),
                    stamp(info.st_ctim), seen ? epoch : QString(), scope });
            Node n;
            n.id = insert.lastInsertId().toLongLong();
            insert.finish();
            n.parent = parent;
            n.name = name;
            n.kind = kind;
            n.scope = scope;
            n.totals = { kind == 2 ? qint64(info.st_size) : 0, kind == 2 ? 1 : 0, 0, kind == 1 ? 1 : 0 };
            n.known = kind != 1;
            n.device = device(info);
            n.inode = inode(info);
            n.modified = stamp(info.st_mtim);
            n.changed = stamp(info.st_ctim);
            return n;
        }
        exec(update,
            { device(info), inode(info), stamp(info.st_mtim), stamp(info.st_ctim), seen ? epoch : QString(), scope,
                old.id });
        update.finish();
        if (kind == 2 && old.totals.bytes != info.st_size)
            sql("UPDATE entries SET bytes=? WHERE id=?", { qint64(info.st_size), old.id });
        old.device = device(info);
        old.scope = scope;
        old.inode = inode(info);
        old.modified = stamp(info.st_mtim);
        old.changed = stamp(info.st_ctim);
        if (kind == 2)
            old.totals.bytes = info.st_size;
        return old;
    }
    Node ensureDirectory(const QString& path)
    {
        Node n = node(1);
        QString partial;
        for (const auto& name : path.split('/', Qt::SkipEmptyParts)) {
            partial += '/' + name;
            Node next = child(n.id, name);
            if (!next.id) {
                struct stat st { };
                if (::stat(QFile::encodeName(partial).constData(), &st) != 0 || !S_ISDIR(st.st_mode))
                    return { };
                next = upsert(n.id, name, st, false, &next, scopeFor(partial));
                propagate(n.id, next.contribution());
            }
            if (next.kind != 1)
                return { };
            n = next;
        }
        return n;
    }
    void recompute(qint64 id, bool known)
    {
        auto q = sql("SELECT COALESCE(SUM(CASE WHEN scope=0 THEN bytes ELSE 0 END),0),"
                     "COALESCE(SUM(CASE WHEN scope=0 THEN files ELSE 0 END),0),"
                     "COALESCE(SUM(CASE WHEN scope=0 THEN folders+CASE WHEN kind=1 THEN 1 ELSE 0 END ELSE 0 END),0),"
                     "COALESCE(SUM(CASE WHEN scope=0 THEN unknowns ELSE 0 END),0) "
                     "FROM entries WHERE parent=?",
            { id });
        q.next();
        sql("UPDATE entries SET bytes=?,files=?,folders=?,unknowns=?,known=?,verified=? WHERE id=?",
            { q.value(0), q.value(1), q.value(2), q.value(3).toLongLong() + (known ? 0 : 1), known,
                work.session, id });
    }
    void checkpoint()
    {
        if (work.cancelled->load(std::memory_order_relaxed))
            throw std::runtime_error("Cancelled");
        if (++batch < 128)
            return;
        batch = 0;
        const auto now = cpuTime();
        if (now - budget >= 4000000) {
            if (work.ioBusy && work.ioBusy->load(std::memory_order_relaxed)
                && !work.demand->load(std::memory_order_relaxed))
                QThread::msleep(20);
            budget = cpuTime();
        }
    }
    void watch(const QString&, const struct stat&) {} // watches belong only to visible subscriptions
    void scan(const QString& path, bool recursive)
    {
        if (scopeFor(path) == 2)
            return;
        Node root = ensureDirectory(path);
        if (!root.id)
            return;
        const Node before = root;
        recursive = recursive || !root.known;
        struct stat initialInfo { };
        const bool rootIdentified = ::stat(QFile::encodeName(path).constData(), &initialInfo) == 0;
        struct Frame {
            qint64 id;
            QString path;
            DIR* dir;
            bool recursive;
            std::pair<dev_t, ino_t> identity;
            bool readable = true;
            bool fresh = false;
        };
        std::vector<Frame> stack;
        std::set<std::pair<dev_t, ino_t>> ancestors;
        const auto push = [&](qint64 id, const QString& p, int fd, bool deep) {
            struct stat info { };
            if (fd < 0 || ::fstat(fd, &info) != 0 || !S_ISDIR(info.st_mode)
                || ancestors.count({ info.st_dev, info.st_ino })) {
                if (fd >= 0)
                    ::close(fd);
                sql("UPDATE entries SET unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 END,known=0 WHERE "
                    "id=?",
                    { id });
                return false;
            }
            DIR* dir = ::fdopendir(fd);
            if (!dir) {
                ::close(fd);
                sql("UPDATE entries SET unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 END,known=0 WHERE "
                    "id=?",
                    { id });
                return false;
            }
            watch(p, info);
            ancestors.insert({ info.st_dev, info.st_ino });
            auto existing = sql("SELECT 1 FROM entries WHERE parent=? LIMIT 1", { id });
            const bool fresh = !existing.next();
            stack.push_back({ id, p, dir, deep, { info.st_dev, info.st_ino }, true, fresh });
            ++result.directoryListings;
            return true;
        };
        push(root.id, path,
            ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC),
            recursive);
        try {
            while (!stack.empty()) {
                checkpoint();
                const auto frame = stack.back();
                if (frame.path == ownDirectory) {
                    ::closedir(frame.dir);
                    ancestors.erase(frame.identity);
                    stack.pop_back();
                    recompute(frame.id, true);
                    continue;
                }
                errno = 0;
                dirent* entry = ::readdir(frame.dir);
                if (!entry) {
                    const bool complete = errno == 0 && frame.readable;
                    ::closedir(frame.dir);
                    ancestors.erase(frame.identity);
                    stack.pop_back();
                    if (complete) {
                        // A vanished mount is unavailable, not an empty directory.
                        sql("UPDATE entries SET seen=?,unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 "
                            "END,known=0 "
                            "WHERE parent=? AND seen<>? AND kind=1 AND device<>?",
                            { epoch, frame.id, epoch, QString::number(qulonglong(frame.identity.first)) });
                        sql("WITH RECURSIVE gone(id) AS (SELECT id FROM entries WHERE parent=? AND (seen IS "
                            "NULL OR seen<>?) "
                            "UNION ALL SELECT e.id FROM entries e JOIN gone g ON e.parent=g.id) DELETE FROM "
                            "entries WHERE id IN (SELECT id FROM gone)",
                            { frame.id, epoch });
                    }
                    recompute(frame.id, complete);
                    continue;
                }
                if (!std::strcmp(entry->d_name, ".") || !std::strcmp(entry->d_name, ".."))
                    continue;
                struct stat info { };
                if (directoryMetadataAt(::dirfd(frame.dir), entry->d_name, &info) != 0) {
                    if (errno == ENOENT || errno == ENOTDIR)
                        continue;
                    // Preserve an existing inaccessible child; deletion is decided
                    // by a successful directory listing, never by a failed stat.
                    sql("UPDATE entries SET seen=? WHERE parent=? AND name=?",
                        { epoch, frame.id, QFile::decodeName(entry->d_name) });
                    stack.back().readable = false;
                    continue;
                }
                ++result.fileStats;
                const QString name = QFile::decodeName(entry->d_name);
                const QString childPath = QDir(frame.path).filePath(name);
                const Node old = frame.fresh ? Node { } : child(frame.id, name);
                if (old.id && old.kind == 1 && old.device != QString::number(qulonglong(frame.identity.first))
                    && device(info) == QString::number(qulonglong(frame.identity.first))) {
                    sql("UPDATE entries SET seen=?,unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 "
                        "END,known=0 WHERE id=?",
                        { epoch, old.id });
                    continue;
                }
                const Node current = upsert(frame.id, name, info, true, &old, scopeFor(childPath, old.scope));
                if (current.scope)
                    continue;
                if (current.kind == 2) {
                    work.progress->bytes.fetch_add(info.st_size, std::memory_order_relaxed);
                    work.progress->files.fetch_add(1, std::memory_order_relaxed);
                } else if (current.kind == 1) {
                    work.progress->folders.fetch_add(1, std::memory_order_relaxed);
                    const bool sameIdentity = old.device == device(info) && old.inode == inode(info);
                    if (old.known && sameIdentity && old.verified == work.session
                        && old.modified == stamp(info.st_mtim) && old.changed == stamp(info.st_ctim))
                        continue;
                    if (frame.recursive || !old.known || old.device != device(info)
                        || old.inode != inode(info) || old.modified != stamp(info.st_mtim))
                        push(current.id, childPath,
                            ::openat(::dirfd(frame.dir), entry->d_name,
                                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC),
                            frame.recursive || !old.known);
                }
            }
        } catch (...) {
            for (const auto& frame : stack)
                ::closedir(frame.dir);
            throw;
        }
        root = node(root.id);
        struct stat rootInfo { };
        if (::stat(QFile::encodeName(path).constData(), &rootInfo) == 0) {
            if (rootIdentified
                && (rootInfo.st_dev != initialInfo.st_dev || rootInfo.st_ino != initialInfo.st_ino)) {
                sql("UPDATE entries SET known=0,unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 "
                    "END,verified='' WHERE id=?",
                    { root.id });
                result.changedDirectories.push_back(path);
            } else
                sql("UPDATE entries SET device=?,inode=?,mtime=?,ctime=? WHERE id=?",
                    { device(rootInfo), inode(rootInfo), stamp(rootInfo.st_mtim), stamp(rootInfo.st_ctim),
                        root.id });
        }
        root = node(root.id);
        propagate(root.parent, root.contribution() - before.contribution());
    }
    bool accessibleRoot(const QString& path, const Node& n)
    {
        struct stat st { };
        if (::stat(QFile::encodeName(path).constData(), &st) != 0 || !S_ISDIR(st.st_mode))
            return false;
        if (n.scope == 1 && !n.device.isEmpty() && n.device != device(st))
            return false;
        auto q = sql("SELECT mount FROM roots WHERE node=? LIMIT 1", { n.id });
        if (q.next() && !q.value(0).toString().isEmpty()) {
            const auto oldMount = q.value(0).toString();
            if (QStorageInfo(path).rootPath() != oldMount)
                return false;
        }
        return true;
    }
    void verifyTree(const QString& path)
    {
        if (scopeFor(path) == 2)
            return;
        // Stream existing records in small batches. Unchanged files need one
        // stat, no per-file SELECT/UPDATE and no readdir of their directory.
        // Directory timestamps alone cannot detect offline file-size changes.
        struct Frame {
            QString path;
            Node root;
            int fd = -1;
            qint64 cursor = 0;
            QList<Node> children;
            qsizetype next = 0;
            bool listed = false;
        };
        std::vector<Frame> stack;
        const auto push = [&](const QString& currentPath) {
            Node root = lookup(currentPath);
            struct stat info { };
            const int fd = ::open(QFile::encodeName(currentPath).constData(),
                O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
            if (fd < 0 || ::fstat(fd, &info) != 0 || !root.known
                || root.device != device(info) || root.inode != inode(info)) {
                if (fd >= 0) ::close(fd);
                scan(currentPath, true);
                return;
            }
            watch(currentPath, info);
            const bool changed = root.modified != stamp(info.st_mtim) || root.changed != stamp(info.st_ctim);
            if (changed) {
                scan(currentPath, false);
                root = lookup(currentPath);
            }
            stack.push_back({ currentPath, root, fd, 0, { }, 0, changed });
        };
        push(path);
        try {
            while (!stack.empty()) {
                checkpoint();
                auto& frame = stack.back();
                if (frame.next >= frame.children.size()) {
                    frame.children.clear(); frame.next = 0;
                    auto rows = sql(QString("SELECT %1 FROM entries WHERE parent=? AND id>? ORDER BY id LIMIT 64").arg(columns),
                        { frame.root.id, frame.cursor });
                    while (rows.next()) frame.children.push_back(read(rows));
                    rows.finish();
                    if (frame.children.isEmpty()) {
                        struct stat info { };
                        if (::fstat(frame.fd, &info) == 0
                            && (frame.root.modified != stamp(info.st_mtim) || frame.root.changed != stamp(info.st_ctim)))
                            result.changedDirectories.push_back(frame.path);
                        sql("UPDATE entries SET verified=? WHERE id=?", { work.session, frame.root.id });
                        ::close(frame.fd);
                        stack.pop_back();
                        continue;
                    }
                    frame.cursor = frame.children.last().id;
                }
                const Node child = frame.children.at(frame.next++);
                if (child.kind == 1) {
                    const QString childPath = QDir(frame.path).filePath(child.name);
                    if (childPath != ownDirectory && !child.scope) push(childPath);
                    continue;
                }
                if (frame.listed || child.kind != 2)
                    continue;
                struct stat info { };
                ++result.fileStats;
                if (directoryMetadataAt(frame.fd, QFile::encodeName(child.name).constData(), &info) != 0
                    || !S_ISREG(info.st_mode) || child.device.toULongLong() != quint64(info.st_dev)
                    || child.inode.toULongLong() != quint64(info.st_ino)
                    || child.modified != stamp(info.st_mtim) || child.changed != stamp(info.st_ctim)
                    || child.totals.bytes != info.st_size)
                    updatePath(QDir(frame.path).filePath(child.name));
            }
        } catch (...) {
            for (const auto& frame : stack) ::close(frame.fd);
            throw;
        }
    }
    bool needsVerification(const QString& path)
    {
        auto rows = sql("SELECT path FROM roots WHERE needs_verify=1");
        while (rows.next())
            if (within(path, rows.value(0).toString())) return true;
        return false;
    }
    void updatePath(const QString& path)
    {
        if (scopeFor(path) == 2)
            return;
        if (path == "/") {
            const auto root = lookup(path);
            struct stat st {};
            if (::lstat("/", &st) == 0 && (root.modified != stamp(st.st_mtim) || root.changed != stamp(st.st_ctim)))
                scan(path, false);
            return;
        }
        const QString parentPath = QFileInfo(path).absolutePath();
        Node parent = lookup(parentPath);
        if (!parent.id || parent.kind != 1)
            return;
        struct stat parentInfo { };
        if (::stat(QFile::encodeName(parentPath).constData(), &parentInfo) != 0
            || !S_ISDIR(parentInfo.st_mode))
            return;
        if (parent.device != device(parentInfo) && parent.device != node(parent.parent).device) {
            sql("UPDATE entries SET unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 END,known=0 WHERE id=?",
                { parent.id });
            if (parent.known)
                propagate(parent.parent, { 0, 0, 0, 1 });
            return;
        }
        if (parent.device != device(parentInfo) || parent.inode != inode(parentInfo)) {
            scan(parentPath, true);
            return;
        }
        const QString name = QFileInfo(path).fileName();
        const Node old = child(parent.id, name);
        struct stat info { };
        if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
            if (errno != ENOENT && errno != ENOTDIR)
                return;
            // An unmounted volume can lose its mountpoint altogether. Its
            // independent cached total must not be treated as deleted contents.
            if (old.scope == 1)
                return;
            if (old.id) {
                erase(old.id);
                propagate(parent.id, Totals { } - old.contribution());
            }
            return;
        }
        ++result.fileStats;
        if (old.id && old.kind == 1 && old.device != parent.device && device(info) == parent.device) {
            sql("UPDATE entries SET unknowns=unknowns+CASE WHEN known=1 THEN 1 ELSE 0 END,known=0 WHERE id=?",
                { old.id });
            if (old.known && !old.scope)
                propagate(parent.id, { 0, 0, 0, 1 });
            return;
        }
        if (old.id && old.device == device(info) && old.inode == inode(info)
            && old.modified == stamp(info.st_mtim) && old.changed == stamp(info.st_ctim)
            && (old.kind != 2 || old.totals.bytes == info.st_size))
            return;
        const Node next = upsert(parent.id, name, info, false, &old, scopeFor(path, old.scope));
        propagate(parent.id, next.contribution() - old.contribution());
        if (next.kind == 1 && (!next.scope || work.requested.contains(path)))
            scan(path, !old.known || old.inode != next.inode || old.device != next.device);
        // Hard-link writes change every indexed directory entry referring to
        // this inode, including peers outside the directory that notified us.
        if (next.kind == 2)
            updatePeers(next);
    }
    void updatePeers(const Node& next)
    {
        auto peers = sql(
            QString("SELECT %1 FROM entries WHERE kind=2 AND device=? AND inode=? AND id<>?").arg(columns),
            { next.device, next.inode, next.id });
        QList<Node> changed;
        while (peers.next()) {
            const Node peer = read(peers);
            if (peer.totals.bytes != next.totals.bytes)
                changed.push_back(peer);
        }
        peers.finish();
        for (const auto& peer : changed) {
            sql("UPDATE entries SET bytes=?,mtime=?,ctime=? WHERE id=?",
                { next.totals.bytes, next.modified, next.changed, peer.id });
            if (!peer.scope) propagate(peer.parent, { next.totals.bytes - peer.totals.bytes, 0, 0, 0 });
        }
    }
    void move(const QString& source, const QString& destination)
    {
        const Node old = lookup(source);
        if (!old.id) {
            updatePath(destination);
            return;
        }
        struct stat st { };
        if (::lstat(QFile::encodeName(destination).constData(), &st) != 0 || old.device != device(st)
            || old.inode != inode(st)) {
            updatePath(source);
            updatePath(destination);
            return;
        }
        const Node parent = lookup(QFileInfo(destination).absolutePath());
        if (!parent.id || within(QFileInfo(destination).absolutePath(), source)) {
            updatePath(source);
            return;
        }
        const Node replaced = child(parent.id, QFileInfo(destination).fileName());
        if (replaced.id && replaced.id != old.id) {
            erase(replaced.id);
            propagate(parent.id, Totals { } - replaced.contribution());
        }
        sql("UPDATE entries SET parent=?,name=?,ctime=? WHERE id=?",
            { parent.id, QFileInfo(destination).fileName(), stamp(st.st_ctim), old.id });
        propagate(old.parent, Totals { } - old.contribution());
        propagate(parent.id, old.contribution());
    }
    DirectorySizeSnapshot snapshot(const QString& requested)
    {
        DirectorySizeSnapshot s;
        s.path = requested;
        s.canonicalPath = canonical.value(requested, requested);
        // Entries exist for every indexed descendant, even if that directory
        // has never owned a footer/root record before this process restart.
        const Node n = lookup(canonical.value(requested, requested));
        if (!n.id) {
            s.state = "unavailable";
            s.error = "Location is unavailable.";
            return s;
        }
        if (n.scope == 2) {
            s.state = "excluded";
            s.error = "Virtual filesystems are excluded from size accounting.";
            return s;
        }
        // An ancestor created while indexing a child is a lower bound, not a
        // completed cached total for the entire ancestor (notably "/").
        s.available = n.known;
        s.totals.bytes = n.totals.bytes;
        s.totals.files = int(n.totals.files);
        s.totals.folders = int(n.totals.folders);
        s.canonicalPath = canonical.value(requested, requested);
        s.complete = n.known && n.totals.unknowns == 0;
        s.state = n.verified == work.session && s.complete ? "ready" : "cached";
        if (work.cachedRoots.contains(requested) && !work.verify.contains(requested)) s.state = "cached";
        const auto filesystem = QStorageInfo(s.canonicalPath).fileSystemType();
        if (filesystem.startsWith("nfs") || filesystem == "cifs" || filesystem == "smbfs"
            || filesystem.startsWith("fuse"))
            s.state = "cached";
        int monitored = 0;
        {
            QMutexLocker lock(&work.watches->mutex);
            for (const auto& path : work.watches->names)
                if (within(path, s.canonicalPath))
                    ++monitored;
        }
        const auto expectedMonitors = n.totals.folders + 1 - (within(ownDirectory, s.canonicalPath) ? 1 : 0);
        if (monitored < expectedMonitors)
            s.state = "cached";
        if (!accessibleRoot(s.canonicalPath, n)) {
            s.complete = false;
            s.state = "unavailable";
            s.error = "Location is unavailable or its mounted filesystem changed.";
        } else if (!s.complete)
            s.error = "Some directory contents could not be read.";
        s.checkedAt = QDateTime::currentDateTimeUtc();
        auto children
            = sql("SELECT name,kind,CASE WHEN scope=2 THEN 0 ELSE bytes END,"
                  "CASE WHEN scope=2 THEN 0 ELSE files END,CASE WHEN scope=2 THEN 0 ELSE folders END "
                  "FROM entries WHERE parent=? ORDER BY id LIMIT 512",
                { n.id });
        int childBudget = 64 * 1024;
        while (children.next()) {
            const auto childPath = QDir(requested).filePath(children.value(0).toString());
            childBudget -= childPath.size() * 2 + 2048;
            if (childBudget < 0) break;
            s.totals.children.insert(childPath,
                QVariantMap { { "directory", children.value(1).toInt() == 1 }, { "bytes", children.value(2) },
                    { "files", children.value(3) },
                    { "folders",
                        children.value(4).toLongLong() + (children.value(1).toInt() == 1 ? 1 : 0) } });
        }
        return s;
    }
    DirectorySizeWorkResult run()
    {
        initialize();
        QStringList scanned;
        for (const auto& path : work.requested) {
            work.progress->activeRequest.store(work.requestIds.value(path), std::memory_order_relaxed);
            QString actual = QFileInfo(path).canonicalFilePath();
            if (actual.isEmpty())
                actual = path;
            canonical[path] = actual;
            Node root = lookup(actual);
            if (!root.id)
                root = ensureDirectory(actual);
            if (!root.id)
                continue;
            if (root.scope == 2)
                continue;
            if (!accessibleRoot(actual, root))
                continue;
            if (root.known && work.peek.contains(path)) {
                struct stat info {};
                if (::lstat(QFile::encodeName(actual).constData(), &info) == 0
                    && (root.device != device(info) || root.inode != inode(info)
                        || root.modified != stamp(info.st_mtim) || root.changed != stamp(info.st_ctim)))
                    result.changedDirectories.push_back(actual);
            }
            sql("INSERT INTO roots(path,node,mount) VALUES(?,?,?) ON CONFLICT(path) DO UPDATE SET "
                "node=excluded.node",
                { path, root.id, QStorageInfo(actual).rootPath() });
            const bool force = work.verify.contains(path) || needsVerification(path);
            const bool needsScan = !root.known || force;
            auto watched = sql("SELECT watch_session FROM roots WHERE path=?", { path });
            const bool needsWatches = !watched.next() || watched.value(0).toString() != work.session;
            watched.finish();
            if ((needsScan || needsWatches) && work.peek.contains(path)) {
                result.recheck.push_back(path);
                if (force) result.verifyAgain.push_back(path);
            }
            else if (needsScan && std::none_of(scanned.cbegin(), scanned.cend(), [&](const auto& parent) {
                         return within(actual, parent);
                     })) {
                if (root.known) verifyTree(actual);
                else scan(actual, true);
                scanned.push_back(actual);
                sql("UPDATE roots SET needs_verify=0,watch_session=? WHERE path=? OR substr(path,1,?)=?",
                    { work.session, path, path == "/" ? 1 : path.size() + 1, path == "/" ? path : path + '/' });
            }
            else if (needsWatches && !work.peek.contains(path)) {
                // A cached lookup never enumerates or watches descendants.
                sql("UPDATE roots SET watch_session=? WHERE path=?", { work.session, path });
            }
        }
        work.progress->activeRequest.store(0, std::memory_order_relaxed);
        const auto actualHint = [&](const QString& path) {
            QString mapped = path;
            int longest = -1;
            for (auto it = canonical.cbegin(); it != canonical.cend(); ++it)
                if (it.key().size() > longest && within(path, it.key())) {
                    mapped = it.value() + path.mid(it.key().size());
                    longest = it.key().size();
                }
            return mapped;
        };
        for (const auto& pair : work.moves) {
            checkpoint();
            move(actualHint(pair.first), actualHint(pair.second));
        }
        for (const auto& path : work.paths) {
            checkpoint();
            updatePath(actualHint(path));
        }
        for (const auto& path : work.directories) {
            checkpoint();
            const auto actual = actualHint(path);
            if (scopeFor(actual) != 2 && lookup(actual).id)
                scan(actual, false);
        }
        for (auto request = work.selections.cbegin(); request != work.selections.cend(); ++request) {
            auto paths = request.value();
            for (auto& path : paths)
                path = actualHint(path);
            std::sort(
                paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.size() < b.size(); });
            QSet<QString> selectedDirectories;
            DirectorySizeSnapshot selected;
            selected.available = selected.complete = true;
            selected.state = "ready";
            for (const auto& path : paths) {
                checkpoint();
                auto parentPath = QFileInfo(path).absolutePath();
                bool covered = false;
                for (auto ancestor = parentPath;; ancestor = QFileInfo(ancestor).absolutePath()) {
                    if (selectedDirectories.contains(ancestor)) {
                        covered = true;
                        break;
                    }
                    if (ancestor == "/")
                        break;
                }
                if (covered)
                    continue;
                struct stat info { };
                if (::lstat(QFile::encodeName(path).constData(), &info) != 0) {
                    selected.complete = false;
                    selected.state = "unavailable";
                    selected.error = "A selected item is unavailable.";
                    continue;
                }
                Node n;
                if (S_ISREG(info.st_mode) && within(path, ownDirectory)) {
                    selectedOwnFiles[request.key()].push_back(path);
                    continue;
                }
                if (S_ISDIR(info.st_mode)) {
                    n = ensureDirectory(path);
                    if (!n.id) {
                        selected.complete = false;
                        continue;
                    }
                    if (n.scope == 2) {
                        selected.complete = false;
                        selected.state = "excluded";
                        continue;
                    }
                    if (!n.known || needsVerification(path))
                        verifyTree(path);
                    else if (n.verified != work.session)
                        selected.state = "cached";
                    n = lookup(path);
                    selectedDirectories.insert(path);
                } else if (S_ISREG(info.st_mode)) {
                    const auto parent = ensureDirectory(parentPath);
                    if (!parent.id) {
                        selected.complete = false;
                        continue;
                    }
                    const auto old = child(parent.id, QFileInfo(path).fileName());
                    n = upsert(parent.id, QFileInfo(path).fileName(), info, false, &old);
                    propagate(parent.id, n.contribution() - old.contribution());
                    updatePeers(n);
                    ++result.fileStats;
                } else
                    continue;
                const auto amount = n.contribution(false);
                selected.totals.bytes += amount.bytes;
                selected.totals.files += int(amount.files);
                selected.totals.folders += int(amount.folders);
                if (amount.unknowns) {
                    selected.complete = false;
                    selected.state = "cached";
                }
            }
            selected.checkedAt = QDateTime::currentDateTimeUtc();
            result.selections.insert(request.key(), selected);
            selectionDirectories[request.key()] = selectedDirectories;
        }
        if (work.cancelled->load())
            throw std::runtime_error("Cancelled");
        if (!db.commit())
            fail(db.lastError().text());
        for (const auto& path : work.requested)
            result.snapshots.insert(path, snapshot(path));
        closeQueries();
        accountOwnFiles();
        return result;
    }
    DirectorySizeWorkResult peek()
    {
        initialize(true);
        if (!db.transaction())
            fail(db.lastError().text());
        for (const auto& path : work.requested) {
            const auto actual = QFileInfo(path).canonicalFilePath();
            canonical[path] = actual.isEmpty() ? path : actual;
            result.snapshots.insert(path, snapshot(path));
        }
        db.commit();
        closeQueries();
        accountOwnFiles();
        return result;
    }
    void closeQueries()
    {
        byId = QSqlQuery(); byName = QSqlQuery(); insert = QSqlQuery(); update = QSqlQuery();
        db.close();
    }
    void accountOwnFiles()
    {
        // Index files are not recursively indexed: accounting for their actual
        // size at publication avoids an index-write -> size-change feedback loop.
        const auto artifacts
            = QDir(ownDirectory).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
        qint64 ownBytes = 0;
        int ownFiles = 0;
        for (const auto& file : artifacts) {
            ownBytes += file.size();
            ++ownFiles;
        }
        for (auto it = result.snapshots.begin(); it != result.snapshots.end(); ++it) {
            if (it->state == "excluded" || !within(ownDirectory, it->canonicalPath)
                || crossesMount(it->canonicalPath, ownDirectory))
                continue;
            it->totals.bytes += ownBytes;
            it->totals.files += ownFiles;
            for (auto child = it->totals.children.begin(); child != it->totals.children.end(); ++child) {
                const auto actualChild = QDir(it->canonicalPath).filePath(QFileInfo(child.key()).fileName());
                if (!within(ownDirectory, actualChild))
                    continue;
                auto values = child->toMap();
                values["bytes"] = values.value("bytes").toLongLong() + ownBytes;
                values["files"] = values.value("files").toInt() + ownFiles;
                *child = values;
            }
        }
        for (auto it = result.selections.begin(); it != result.selections.end(); ++it) {
            bool includesCache = false;
            for (const auto& root : selectionDirectories.value(it.key()))
                includesCache = includesCache || (within(ownDirectory, root) && !crossesMount(root, ownDirectory));
            if (includesCache) {
                it->totals.bytes += ownBytes;
                it->totals.files += ownFiles;
            } else
                for (const auto& path : selectedOwnFiles.value(it.key())) {
                    const QFileInfo file(path);
                    if (file.exists()) {
                        it->totals.bytes += file.size();
                        ++it->totals.files;
                    }
                }
        }
    }
    bool crossesMount(const QString& root, const QString& path) const
    {
        for (const auto& boundary : mountBoundaries)
            if (boundary != root && within(boundary, root) && within(path, boundary)) return true;
        return false;
    }
    DirectorySizeWork work;
    DirectorySizeWorkResult result;
    QString connection, ownDirectory, epoch;
    QSqlDatabase db;
    QSqlQuery byId, byName, insert, update;
    QHash<QString, QString> canonical;
    QSet<QString> mountBoundaries;
    QSet<QString> virtualRoots;
    QHash<quint64, QSet<QString>> selectionDirectories;
    QHash<quint64, QStringList> selectedOwnFiles;
    qint64 budget = 0;
    int batch = 0;
};
}

DirectorySizeWorkResult runDirectorySizeWork(const DirectorySizeWork& work)
{
    try {
        Store store(work);
        return store.run();
    } catch (const std::exception& error) {
        DirectorySizeWorkResult result;
        result.error = QString::fromUtf8(error.what());
        // This file contains reconstructible metadata only. Keep a corrupt copy
        // for diagnosis and rebuild, without removing any user file or total.
        if (result.error.contains("not a database", Qt::CaseInsensitive)
            || result.error.contains("malformed", Qt::CaseInsensitive)) {
            const auto backup = work.databasePath + ".corrupt-" + QUuid::createUuid().toString(QUuid::Id128);
            if (QFile::rename(work.databasePath, backup)) {
                try {
                    Store replacement(work);
                    return replacement.run();
                } catch (const std::exception& retry) {
                    result.error = QString::fromUtf8(retry.what());
                }
            }
        }
        return result;
    }
}

DirectorySizeWorkResult readDirectorySizeSnapshots(const DirectorySizeWork& work)
{
    if (!QFileInfo::exists(work.databasePath))
        return { };
    try {
        Store store(work);
        return store.peek();
    } catch (const std::exception& error) {
        DirectorySizeWorkResult result;
        result.error = QString::fromUtf8(error.what());
        return result;
    }
}
