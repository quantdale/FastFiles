#pragma once

// durable-cold-search-index-and-hybrid-residency 4.2: test-only forwarding
// VFS. FULL is returned by a real SQLite xWrite, never by a Store mock.
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <utility>
#include "sqlite3.h"

namespace fftest {
class SqliteWriteFaultVfs {
public:
    enum class FileKind { Any, Database, Wal };
    struct File {
        sqlite3_file file{};
        int wal = 0;
        int database = 0;
    };

    static inline sqlite3* capturedDb = nullptr;
    static inline bool captureNext = false;
    static inline std::string currentSql;
    static inline bool inCommit = false;
    static inline std::function<void(bool, bool)> onWrite;
    static inline std::function<void()> onCommit;
    static inline std::function<void(sqlite3*, const char*)> onStatement;
    static inline bool armed = false;
    static inline bool fired = false;
    static inline int writes = 0;
    static inline int targetWrite = 1;
    static inline FileKind kind = FileKind::Any;
    static inline std::string sqlFilter;
    static inline std::string failureSql;
    static inline sqlite3_int64 failureOffset = 0;
    static inline int failureBytes = 0;
    static inline bool failureWal = false;

    static void Arm(int nth, FileKind fileKind, std::string filter = {}) {
        armed = true;
        fired = false;
        writes = 0;
        targetWrite = nth;
        kind = fileKind;
        sqlFilter = std::move(filter);
        failureSql.clear();
    }
    static void Disarm() { armed = false; }

    static bool Install() {
        base_ = sqlite3_vfs_find(nullptr);
        if (!base_) return false;
        vfs_ = *base_;
        vfs_.zName = "ff-test-write-full";
        vfs_.pNext = nullptr;
        static_assert(sizeof(File) % alignof(std::max_align_t) == 0);
        vfs_.szOsFile = static_cast<int>(sizeof(File)) + base_->szOsFile;
        vfs_.xOpen = Open;
        if (sqlite3_vfs_register(&vfs_, 1) != SQLITE_OK) return false;
        return sqlite3_auto_extension(reinterpret_cast<void(*)(void)>(Capture)) == SQLITE_OK;
    }
    static void Uninstall() {
        Disarm();
        onWrite = {};
        onCommit = {};
        onStatement = {};
        sqlite3_cancel_auto_extension(reinterpret_cast<void(*)(void)>(Capture));
        sqlite3_vfs_register(base_, 1);
        sqlite3_vfs_unregister(&vfs_);
    }

private:
    static inline sqlite3_vfs* base_ = nullptr;
    static inline sqlite3_vfs vfs_{};
    static File* Wrap(sqlite3_file* f) { return reinterpret_cast<File*>(f); }
    static sqlite3_file* Real(sqlite3_file* f) {
        return reinterpret_cast<sqlite3_file*>(reinterpret_cast<unsigned char*>(f) + sizeof(File));
    }
    static int Trace(unsigned event, void* context, void* statement, void*) {
        const auto* sql = sqlite3_sql(static_cast<sqlite3_stmt*>(statement));
        if (event == SQLITE_TRACE_STMT) {
            currentSql = sql ? sql : "";
            if (sql && std::strncmp(sql, "COMMIT", 6) == 0) inCommit = true;
            if (onStatement) onStatement(static_cast<sqlite3*>(context), sql);
        }
        if (event == SQLITE_TRACE_PROFILE && sql && std::strncmp(sql, "COMMIT", 6) == 0) {
            inCommit = false;
            if (onCommit) onCommit();
        }
        return 0;
    }
    static int Capture(sqlite3* db, char**, const sqlite3_api_routines*) {
        if (captureNext) {
            captureNext = false;
            capturedDb = db;
            sqlite3_trace_v2(db, SQLITE_TRACE_STMT | SQLITE_TRACE_PROFILE, Trace, db);
        }
        return SQLITE_OK;
    }
    static int Open(sqlite3_vfs*, sqlite3_filename name, sqlite3_file* f, int flags, int* outFlags) {
        auto* wrapped = Wrap(f);
        wrapped->file.pMethods = nullptr;
        wrapped->wal = (flags & SQLITE_OPEN_WAL) != 0;
        wrapped->database = (flags & SQLITE_OPEN_MAIN_DB) != 0;
        auto* real = Real(f);
        std::memset(real, 0, static_cast<size_t>(base_->szOsFile));
        const int rc = base_->xOpen(base_, name, real, flags, outFlags);
        if (rc == SQLITE_OK) wrapped->file.pMethods = &methods_;
        return rc;
    }
    static int Close(sqlite3_file* f) { return Real(f)->pMethods->xClose(Real(f)); }
    static int Read(sqlite3_file* f, void* b, int n, sqlite3_int64 o) {
        return Real(f)->pMethods->xRead(Real(f), b, n, o);
    }
    static int Write(sqlite3_file* f, const void* b, int n, sqlite3_int64 o) {
        const auto* wrapped = Wrap(f);
        if (onWrite) onWrite(wrapped->wal != 0, wrapped->database != 0);
        if (armed && (kind == FileKind::Any || (kind == FileKind::Wal && wrapped->wal) ||
                      (kind == FileKind::Database && wrapped->database)) &&
            (sqlFilter.empty() || currentSql.find(sqlFilter) != std::string::npos ||
             (sqlFilter == "COMMIT" && inCommit)) && ++writes == targetWrite) {
            armed = false;
            fired = true;
            failureSql = (inCommit ? "COMMIT / " : "") + currentSql;
            failureOffset = o;
            failureBytes = n;
            failureWal = wrapped->wal != 0;
            return SQLITE_FULL;
        }
        return Real(f)->pMethods->xWrite(Real(f), b, n, o);
    }
    static int Truncate(sqlite3_file* f, sqlite3_int64 n) { return Real(f)->pMethods->xTruncate(Real(f), n); }
    static int Sync(sqlite3_file* f, int flags) { return Real(f)->pMethods->xSync(Real(f), flags); }
    static int Size(sqlite3_file* f, sqlite3_int64* n) { return Real(f)->pMethods->xFileSize(Real(f), n); }
    static int Lock(sqlite3_file* f, int n) { return Real(f)->pMethods->xLock(Real(f), n); }
    static int Unlock(sqlite3_file* f, int n) { return Real(f)->pMethods->xUnlock(Real(f), n); }
    static int Reserved(sqlite3_file* f, int* n) { return Real(f)->pMethods->xCheckReservedLock(Real(f), n); }
    static int Control(sqlite3_file* f, int n, void* p) { return Real(f)->pMethods->xFileControl(Real(f), n, p); }
    static int Sector(sqlite3_file* f) { return Real(f)->pMethods->xSectorSize(Real(f)); }
    static int Device(sqlite3_file* f) { return Real(f)->pMethods->xDeviceCharacteristics(Real(f)); }
    static int ShmMap(sqlite3_file* f, int p, int n, int w, void volatile** v) {
        return Real(f)->pMethods->xShmMap(Real(f), p, n, w, v);
    }
    static int ShmLock(sqlite3_file* f, int o, int n, int flags) {
        return Real(f)->pMethods->xShmLock(Real(f), o, n, flags);
    }
    static void Barrier(sqlite3_file* f) { Real(f)->pMethods->xShmBarrier(Real(f)); }
    static int ShmUnmap(sqlite3_file* f, int d) { return Real(f)->pMethods->xShmUnmap(Real(f), d); }
    static int Fetch(sqlite3_file* f, sqlite3_int64 o, int n, void** p) {
        const auto* m = Real(f)->pMethods;
        if (m->iVersion >= 3 && m->xFetch) return m->xFetch(Real(f), o, n, p);
        *p = nullptr;
        return SQLITE_OK;
    }
    static int Unfetch(sqlite3_file* f, sqlite3_int64 o, void* p) {
        const auto* m = Real(f)->pMethods;
        return m->iVersion >= 3 && m->xUnfetch ? m->xUnfetch(Real(f), o, p) : SQLITE_OK;
    }
    static inline const sqlite3_io_methods methods_ = {
        3, Close, Read, Write, Truncate, Sync, Size, Lock, Unlock, Reserved, Control,
        Sector, Device, ShmMap, ShmLock, Barrier, ShmUnmap, Fetch, Unfetch
    };
};
} // namespace fftest
