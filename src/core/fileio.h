// =====================================================================================================
// TAPESTRY · src/core/fileio.h · files that tell the truth about durability
//
// Reused from fusord.cpp §3: `file_size_of`, `replace_file` (the 20 × 5 ms retry and the failure
// counter — the rename-while-open hazard measured on this box 2026-09-04), `write_atomic`.
// Changed, per blueprint §4.1 and §12: "`write_atomic` WITH A SYNC ADDED". fusord's version wrote
// through the C library, closed, and renamed; nothing ever reached the platter on purpose, and
// `Tape::put` called `fflush` and called it durable. Here:
//
//   * every write goes through a raw OS handle, never FILE*, so a flush is FlushFileBuffers and not
//     a library buffer drain;
//   * every call's return value is checked and carries the OS error out;
//   * `write_atomic` fsyncs the temp file BEFORE the rename and renames write-through.
//
// A `File` never partially writes without saying so: write_all loops until the byte count is met.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#else
#  include <fcntl.h>
#  include <unistd.h>
#  include <sys/stat.h>
#  include <cerrno>
#endif

namespace tapestry {

inline std::string os_err_text(unsigned long e) {
    char b[64]; std::snprintf(b, sizeof(b), "os_error=%lu", e); return b;
}

// ---- the fault injector --------------------------------------------------------------------------
// R0 owes one (§9). Two hundred real process kills in R0.1 never once landed inside `WriteFile` —
// with a flush per batch the writer is almost always parked in `FlushFileBuffers` — so the torn-row
// path, the one recovery path that matters most, was reachable only by hand-editing a file. A fault
// injector makes it reachable ON PURPOSE and repeatably: write half a row and terminate the process
// with no unwinding, which is what a crash mid-write actually looks like.
//
// Configured from the environment so a SPAWNED CHILD can be told what to do:
//     TAPESTRY_FAULT=<what>:<n>[:<bytes>]
//     write_fail   :n        the nth write returns an error
//     sync_fail    :n        the nth sync returns an error
//     die_before_sync:n      hard exit just before the nth sync — bytes written, nothing durable
//     die_after_sync :n      hard exit just after the nth sync returns — the ack never reaches anyone
//     torn_write   :n:bytes  the nth write writes only `bytes`, then hard exit
struct FaultPolicy {
    enum What { None, WriteFail, SyncFail, DieBeforeSync, DieAfterSync, TornWrite };
    What     what = None;
    uint64_t at_n = 0;
    uint32_t bytes = 0;
    uint64_t writes = 0, syncs = 0;
};
inline FaultPolicy g_faults;

inline void die_now() {
#if defined(_WIN32)
    TerminateProcess(GetCurrentProcess(), 9);   // no atexit, no flush, no unwinding: a crash
#else
    _exit(9);
#endif
}

inline void faults_from_env() {
    const char* s = std::getenv("TAPESTRY_FAULT");
    if (!s || !*s) return;
    std::string v(s);
    const size_t c1 = v.find(':');
    if (c1 == std::string::npos) return;
    const std::string what = v.substr(0, c1);
    const size_t c2 = v.find(':', c1 + 1);
    const std::string ns = v.substr(c1 + 1, (c2 == std::string::npos) ? std::string::npos : c2 - c1 - 1);
    g_faults.at_n = std::strtoull(ns.c_str(), nullptr, 10);
    if (c2 != std::string::npos) g_faults.bytes = (uint32_t)std::strtoul(v.c_str() + c2 + 1, nullptr, 10);
    if      (what == "write_fail")      g_faults.what = FaultPolicy::WriteFail;
    else if (what == "sync_fail")       g_faults.what = FaultPolicy::SyncFail;
    else if (what == "die_before_sync") g_faults.what = FaultPolicy::DieBeforeSync;
    else if (what == "die_after_sync")  g_faults.what = FaultPolicy::DieAfterSync;
    else if (what == "torn_write")      g_faults.what = FaultPolicy::TornWrite;
}

// ---- a write handle ----------------------------------------------------------------------------
struct File {
#if defined(_WIN32)
    HANDLE h = INVALID_HANDLE_VALUE;
    bool is_open() const { return h != INVALID_HANDLE_VALUE; }
#else
    int fd = -1;
    bool is_open() const { return fd >= 0; }
#endif
    std::string path;

    // Open for append (creating if absent). The handle is positioned at end of file.
    bool open_append(const std::string& p, std::string* err) {
        path = p;
#if defined(_WIN32)
        // GENERIC_WRITE, not FILE_APPEND_DATA: the truncation of a torn tail needs SetEndOfFile, and
        // one writer is a law of this store, so nothing else is appending behind us.
        h = CreateFileA(p.c_str(), GENERIC_READ | GENERIC_WRITE,
                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) { if (err) *err = "open_append " + p + " " + os_err_text(GetLastError()); return false; }
        LARGE_INTEGER z; z.QuadPart = 0;
        if (!SetFilePointerEx(h, z, nullptr, FILE_END)) { if (err) *err = "seek_end " + p + " " + os_err_text(GetLastError()); return false; }
        return true;
#else
        fd = ::open(p.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) { if (err) *err = "open_append " + p + " " + os_err_text((unsigned long)errno); return false; }
        return true;
#endif
    }

    bool write_raw(const void* p, size_t n, std::string* err) {
        const char* c = (const char*)p; size_t done = 0;
        while (done < n) {
#if defined(_WIN32)
            const DWORD chunk = (DWORD)((n - done) > 0x40000000u ? 0x40000000u : (n - done));
            DWORD got = 0;
            if (!WriteFile(h, c + done, chunk, &got, nullptr)) {
                if (err) *err = "write " + path + " " + os_err_text(GetLastError()); return false;
            }
            if (got == 0) { if (err) *err = "write " + path + " wrote_zero"; return false; }
            done += got;
#else
            const ssize_t got = ::write(fd, c + done, n - done);
            if (got < 0) { if (err) *err = "write " + path + " " + os_err_text((unsigned long)errno); return false; }
            if (got == 0) { if (err) *err = "write " + path + " wrote_zero"; return false; }
            done += (size_t)got;
#endif
        }
        return true;
    }

    bool write_all(const void* p, size_t n, std::string* err) {
        if (g_faults.what != FaultPolicy::None) {
            ++g_faults.writes;
            if (g_faults.writes == g_faults.at_n) {
                if (g_faults.what == FaultPolicy::WriteFail) {
                    if (err) *err = "write " + path + " injected_fault"; return false;
                }
                if (g_faults.what == FaultPolicy::TornWrite) {
                    const size_t partial = (g_faults.bytes && g_faults.bytes < n) ? g_faults.bytes : (n / 2);
                    std::string ignored;
                    write_raw(p, partial, &ignored);      // half a row, on its way to the platter
                    die_now();                            // and the process is gone, mid-write
                }
            }
        }
        return write_raw(p, n, err);
    }
    bool write_all(const std::string& s, std::string* err) { return write_all(s.data(), s.size(), err); }

    // The real one. This is the call the group-commit batch is paid for.
    bool sync(std::string* err) {
        if (g_faults.what != FaultPolicy::None) {
            ++g_faults.syncs;
            if (g_faults.syncs == g_faults.at_n) {
                if (g_faults.what == FaultPolicy::SyncFail) {
                    if (err) *err = "sync " + path + " injected_fault"; return false;
                }
                if (g_faults.what == FaultPolicy::DieBeforeSync) die_now();
            }
        }
#if defined(_WIN32)
        if (!FlushFileBuffers(h)) { if (err) *err = "FlushFileBuffers " + path + " " + os_err_text(GetLastError()); return false; }
#else
        if (::fsync(fd) != 0) { if (err) *err = "fsync " + path + " " + os_err_text((unsigned long)errno); return false; }
#endif
        if (g_faults.what == FaultPolicy::DieAfterSync && g_faults.syncs == g_faults.at_n) die_now();
        return true;
    }

    bool size(uint64_t* out, std::string* err) const {
#if defined(_WIN32)
        LARGE_INTEGER n;
        if (!GetFileSizeEx(h, &n)) { if (err) *err = "size " + path + " " + os_err_text(GetLastError()); return false; }
        *out = (uint64_t)n.QuadPart; return true;
#else
        struct stat st;
        if (::fstat(fd, &st) != 0) { if (err) *err = "size " + path + " " + os_err_text((unsigned long)errno); return false; }
        *out = (uint64_t)st.st_size; return true;
#endif
    }

    // Torn trailing bytes are TRUNCATED (blueprint §4.2), not newline-terminated as fusord did:
    // a half-written row is not a row, and terminating it would make a lie verifiable.
    bool truncate_to(uint64_t n, std::string* err) {
#if defined(_WIN32)
        LARGE_INTEGER li; li.QuadPart = (LONGLONG)n;
        if (!SetFilePointerEx(h, li, nullptr, FILE_BEGIN)) { if (err) *err = "seek " + path + " " + os_err_text(GetLastError()); return false; }
        if (!SetEndOfFile(h)) { if (err) *err = "SetEndOfFile " + path + " " + os_err_text(GetLastError()); return false; }
        LARGE_INTEGER z; z.QuadPart = 0;
        if (!SetFilePointerEx(h, z, nullptr, FILE_END)) { if (err) *err = "seek_end " + path + " " + os_err_text(GetLastError()); return false; }
        return true;
#else
        if (::ftruncate(fd, (off_t)n) != 0) { if (err) *err = "ftruncate " + path + " " + os_err_text((unsigned long)errno); return false; }
        return true;
#endif
    }

    void close() {
#if defined(_WIN32)
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
#else
        if (fd >= 0) { ::close(fd); fd = -1; }
#endif
    }
    ~File() { close(); }
    File() = default;
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};

// ---- reads (independent of any write handle) ---------------------------------------------------
inline bool file_size_of(const std::string& path, uint64_t& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#if defined(_WIN32)
    _fseeki64(f, 0, SEEK_END); const long long n = _ftelli64(f);
#else
    fseeko(f, 0, SEEK_END); const long long n = (long long)ftello(f);
#endif
    std::fclose(f);
    if (n < 0) return false;
    out = (uint64_t)n; return true;
}
inline bool file_exists(const std::string& path) { uint64_t n; return file_size_of(path, n); }
inline bool remove_file(const std::string& path) {
#if defined(_WIN32)
    return DeleteFileA(path.c_str()) != 0;
#else
    return ::remove(path.c_str()) == 0;
#endif
}

// Read [off, off+len) as raw bytes. Short reads at EOF are returned, not an error.
inline bool read_range(const std::string& path, uint64_t off, size_t len, std::string& out) {
    out.clear();
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
#if defined(_WIN32)
    if (_fseeki64(f, (long long)off, SEEK_SET) != 0) { std::fclose(f); return false; }
#else
    if (fseeko(f, (off_t)off, SEEK_SET) != 0) { std::fclose(f); return false; }
#endif
    out.resize(len);
    const size_t got = std::fread(&out[0], 1, len, f);
    out.resize(got);
    std::fclose(f);
    return true;
}
inline bool read_whole(const std::string& path, std::string& out) {
    uint64_t n = 0;
    if (!file_size_of(path, n)) return false;
    return read_range(path, 0, (size_t)n, out);
}

// ---- atomic replace, with the estate's measured retry -------------------------------------------
inline std::atomic<uint64_t> g_rename_failures{0};
inline std::mutex g_rename_mu;
inline std::string g_rename_last_path;
inline unsigned long g_rename_last_err = 0;

inline bool replace_file(const std::string& src, const std::string& dst, bool write_through) {
    unsigned long err = 0;
    for (int attempt = 0; attempt < 20; ++attempt) {
#if defined(_WIN32)
        const DWORD flags = MOVEFILE_REPLACE_EXISTING | (write_through ? MOVEFILE_WRITE_THROUGH : 0);
        if (MoveFileExA(src.c_str(), dst.c_str(), flags) != 0) return true;
        err = (unsigned long)GetLastError();
#else
        (void)write_through;
        if (std::rename(src.c_str(), dst.c_str()) == 0) return true;
        err = (unsigned long)errno;
#endif
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    g_rename_failures.fetch_add(1, std::memory_order_relaxed);
    { std::lock_guard<std::mutex> lk(g_rename_mu); g_rename_last_path = dst; g_rename_last_err = err; }
    return false;
}
inline uint64_t rename_failures_snapshot(std::string& path, unsigned long& err) {
    std::lock_guard<std::mutex> lk(g_rename_mu);
    path = g_rename_last_path; err = g_rename_last_err;
    return g_rename_failures.load(std::memory_order_relaxed);
}

// write_atomic WITH THE SYNC ADDED: the temp file's bytes are on the platter before the rename, and
// the rename itself is write-through. Without the sync a crash can leave the directory entry
// pointing at a file whose contents never landed — the failure the snapshot digest would then call
// corruption.
inline bool write_atomic(const std::string& path, const std::string& body, std::string* err = nullptr) {
    const std::string tmp = path + ".tmp";
    {
        File f;
        if (!f.open_append(tmp, err)) return false;
        if (!f.truncate_to(0, err)) return false;
        if (!f.write_all(body, err)) return false;
        if (!f.sync(err)) return false;
        f.close();
    }
    if (!replace_file(tmp, path, true)) { if (err) *err = "replace_file " + path; return false; }
    return true;
}

} // namespace tapestry
