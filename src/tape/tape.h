// =====================================================================================================
// TAPESTRY · src/tape/tape.h · the tape store: segments, positions, the chain, group commit, recovery
//
// Blueprint §3.1, §4.1 (Durability), §4.2. What this file owes, and what each debt costs:
//
//   POSITIONS      `pos` is the deposit clock and the only ordering authority. Monotone, never reused
//                  on the tape. Assigned at stage, PUBLISHED only after the batch's flush returns.
//
//   THE CHAIN      h = blake2b256(prev_hex ‖ literal prefix bytes). Continuous across a segment roll,
//                  because the first row of every segment is a chained `seg` entry whose `prev` is the
//                  previous segment's last `h`. QC-1 F6: fusord's Tape::open recovered `prev` from the
//                  file it was opening, so a rolled segment restarted at genesis, silently, and the
//                  estate's verifier called the forked tape cleaner than the correct one.
//
//   GROUP COMMIT   Entries accumulate in a batch; one FlushFileBuffers per batch per touched segment;
//                  positions and hashes are published only after it returns. fusord's `Tape::put`
//                  called fflush and called that durable, returned the previous hash on a closed file,
//                  and checked no return value (§4.1). None of that carries.
//
//   RECOVERY       Walk back for the last complete row, doubling the window. A file with bytes and no
//                  recoverable head is FATAL — never genesis. Genesis is legal on segment 0 only.
//                  Torn trailing bytes are TRUNCATED, counted, and reported as a `warn` entry;
//                  fusord terminated them with a newline, which makes a half-row verifiable.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include <memory>
#include <cerrno>
#include "entry.h"
#include "../core/fileio.h"
#include "../core/json.h"

#if defined(_WIN32)
#  include <direct.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

namespace tapestry {

// ---- clocks --------------------------------------------------------------------------------------
// §3.1: `t_epoch_ns` is TAI or UTC nanoseconds FROM THE TRANSACTOR, enforced monotone as max of
// observed and last plus one, and it is inside the hash. A monotonic reading lives on operational
// rows only and never on the entry header, because a steady clock resets at boot (QC-1 F7).
struct Clock {
    virtual ~Clock() = default;
    virtual uint64_t now_ns() = 0;
};
struct SystemClock : Clock {
    uint64_t now_ns() override {
        using namespace std::chrono;
        return (uint64_t)duration_cast<nanoseconds>(system_clock::now().time_since_epoch()).count();
    }
};
// The generator's clock: a tape written under it is byte-identical across runs, which is what
// falsifier 1a's "two cold replays bit-identical" needs from the writer's side.
struct FixedClock : Clock {
    uint64_t t; uint64_t step;
    explicit FixedClock(uint64_t t0 = 1757000000000000000ull, uint64_t st = 1000000ull) : t(t0), step(st) {}
    uint64_t now_ns() override { const uint64_t v = t; t += step; return v; }
};

// ---- paths ---------------------------------------------------------------------------------------
inline std::string seg_name(uint64_t first_pos) {
    char b[64]; std::snprintf(b, sizeof(b), "seg-%020llu.jsonl", (unsigned long long)first_pos);
    return b;
}
inline bool seg_first_pos_of(const std::string& fname, uint64_t* out) {
    if (fname.size() != 30) return false;                       // seg-(4) + 20 digits + .jsonl(6)
    if (fname.compare(0, 4, "seg-") != 0) return false;
    if (fname.compare(24, 6, ".jsonl") != 0) return false;
    uint64_t v = 0;
    for (size_t i = 4; i < 24; ++i) {
        if (fname[i] < '0' || fname[i] > '9') return false;
        v = v * 10ull + (uint64_t)(fname[i] - '0');
    }
    *out = v; return true;
}
inline std::string path_join(const std::string& dir, const std::string& f) {
    if (dir.empty()) return f;
    const char c = dir[dir.size() - 1];
    return (c == '/' || c == '\\') ? dir + f : dir + "/" + f;
}
inline bool make_dir(const std::string& dir) {
#if defined(_WIN32)
    return _mkdir(dir.c_str()) == 0 || errno == EEXIST;
#else
    return ::mkdir(dir.c_str(), 0755) == 0 || errno == EEXIST;
#endif
}
inline std::vector<uint64_t> list_segments(const std::string& dir) {
    std::vector<uint64_t> v;
#if defined(_WIN32)
    WIN32_FIND_DATAA fd;
    const std::string pat = path_join(dir, "seg-*.jsonl");
    HANDLE hf = FindFirstFileA(pat.c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            uint64_t fp;
            if (seg_first_pos_of(fd.cFileName, &fp)) v.push_back(fp);
        } while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }
#else
    DIR* d = ::opendir(dir.c_str());
    if (d) {
        while (struct dirent* e = ::readdir(d)) {
            uint64_t fp;
            if (seg_first_pos_of(e->d_name, &fp)) v.push_back(fp);
        }
        ::closedir(d);
    }
#endif
    std::sort(v.begin(), v.end());
    return v;
}

// ---- the segment header body ---------------------------------------------------------------------
// Written and read in exactly one spelling: {"first_pos":N,"prev_of_first_entry":"<64hex>"}
inline json::Value seg_body(uint64_t first_pos, const std::string& prev_of_first) {
    json::Value b = json::Value::object();
    b.set("first_pos", json::Value::u64(first_pos));
    b.set("prev_of_first_entry", json::Value::str(prev_of_first));
    return b;
}
inline bool read_seg_body(const std::string& body_text, uint64_t* first_pos, std::string* prev_of_first) {
    size_t i = 0;
    if (!detail::eat_lit(body_text, i, "{\"first_pos\":")) return false;
    if (!detail::eat_u64(body_text, i, *first_pos)) return false;
    if (!detail::eat_lit(body_text, i, ",\"prev_of_first_entry\":")) return false;
    if (!detail::eat_string(body_text, i, *prev_of_first)) return false;
    if (!detail::eat_lit(body_text, i, "}")) return false;
    return i == body_text.size() && is_hex64(*prev_of_first);
}

// ---- recovery over one segment file ----------------------------------------------------------------
struct HeadScan {
    bool        found = false;
    ScannedRow  row;
    uint64_t    complete_bytes = 0;   // offset just past the last complete row
    uint64_t    torn_bytes = 0;       // bytes after that offset (a row the process died inside)
    std::string reason;               // set when !found
};

// Walk back from the end of `path` for the last COMPLETE row, doubling the window until one is found
// or the file's start is reached. A file with bytes and no recoverable head is fatal to the caller.
inline HeadScan scan_head(const std::string& path) {
    HeadScan hs;
    uint64_t sz = 0;
    if (!file_size_of(path, sz)) { hs.reason = "no_such_file"; return hs; }
    if (sz == 0) { hs.reason = "empty_file"; return hs; }

    // Torn tail: bytes after the last newline in the file.
    for (uint64_t win = 65536; ; win *= 2) {
        const uint64_t lo = sz > win ? sz - win : 0;
        std::string buf;
        if (!read_range(path, lo, (size_t)(sz - lo), buf)) { hs.reason = "read_failed"; return hs; }

        size_t end = buf.size();                       // one past the last complete row
        if (end > 0 && buf[end - 1] != '\n') {
            const size_t nl = buf.rfind('\n');
            if (nl == std::string::npos) {
                if (lo == 0) { hs.reason = "no_newline_in_file"; return hs; }
                if (win >= sz) { hs.reason = "no_newline_in_file"; return hs; }
                continue;                              // widen: the whole window is one torn row
            }
            hs.torn_bytes = end - (nl + 1);
            end = nl + 1;
        }
        // Walk complete rows backwards inside the window.
        while (end > 0) {
            const size_t nl = end >= 2 ? buf.rfind('\n', end - 2) : std::string::npos;
            const size_t start = (nl == std::string::npos) ? 0 : nl + 1;
            if (start == 0 && lo > 0) break;           // row may be cut by the window: widen
            const std::string line = buf.substr(start, end - start - 1);   // drop the '\n'
            std::string why;
            if (scan_row(line, &hs.row, &why, true)) {
                hs.found = true;
                hs.complete_bytes = lo + (uint64_t)end;
                return hs;
            }
            hs.reason = why;                           // a complete row that does not verify
            return hs;                                 // corruption, not tornness: fatal to the caller
        }
        if (lo == 0) { hs.reason = "no_complete_row"; return hs; }
        if (win >= sz) { hs.reason = "no_complete_row"; return hs; }
    }
}

// Read the FIRST row of a segment file (the `seg` header entry).
inline bool scan_first_row(const std::string& path, ScannedRow* out, std::string* reason) {
    std::string buf;
    if (!read_range(path, 0, 1 << 16, buf)) { if (reason) *reason = "read_failed"; return false; }
    const size_t nl = buf.find('\n');
    if (nl == std::string::npos) { if (reason) *reason = "no_first_row"; return false; }
    return scan_row(buf.substr(0, nl), out, reason, true);
}

// ---- the writer -------------------------------------------------------------------------------------
struct TapeConfig {
    std::string dir;
    uint64_t    segment_bytes  = 64ull << 20;   // §4.2: 64 MiB segments named by first position
    bool        sync_on_commit = true;          // false ONLY in the fault-injection harness's honest arm
    bool        write_open_warn = true;         // record torn bytes discarded at open as a `warn` entry
    uint32_t    term = 0;                       // v0 has no leader term
};

struct Staged {
    uint64_t    pos = 0;
    uint64_t    t_epoch_ns = 0;
    std::string h;
    uint64_t    seg_first_pos = 0;
};

class Tape {
public:
    bool open(const TapeConfig& cfg, Clock* clk, std::string* err) {
        cfg_ = cfg; clk_ = clk;
        if (!make_dir(cfg_.dir)) { if (err) *err = "mkdir " + cfg_.dir; return false; }

        std::vector<uint64_t> segs = list_segments(cfg_.dir);
        if (segs.empty()) {
            if (!start_segment(0, GENESIS, err)) return false;
            return commit(err);                        // the genesis `seg` entry is durable before we return
        }

        // ---- ghost segments ---------------------------------------------------------------------
        // A segment file is named by its FIRST POSITION, and a roll opens that file while the batch
        // that will fill it is still pending. So a kill between the roll and the flush leaves a file
        // whose NAME is a position that was assigned but never published — and §4.1 is explicit that
        // positions and hashes are published only after the flush returns. Trusting that name as a
        // position is what a 25-kill run found: the tape resumed nine positions past its last row and
        // left a permanent gap. A trailing segment holding NO COMPLETE ROW (empty, or nothing but the
        // torn remains of one) therefore carries no entry, is a ghost of an uncommitted roll, and is
        // discarded here; the head comes from the last complete row, which is the only authority.
        uint64_t ghosts = 0, ghost_bytes = 0;
        while (!segs.empty()) {
            const uint64_t lastg = segs.back();
            const std::string gp = path_join(cfg_.dir, seg_name(lastg));
            uint64_t gsz = 0;
            if (!file_size_of(gp, gsz)) { if (err) *err = "stat " + gp; return false; }
            bool has_row = false;
            if (gsz > 0) {
                std::string probe;
                read_range(gp, 0, (size_t)(gsz < (1u << 20) ? gsz : (1u << 20)), probe);
                has_row = probe.find('\n') != std::string::npos;
                if (!has_row && gsz > (1u << 20)) {     // a huge file with no newline in its first MiB
                    std::string whole;                   // is still not a row, but read it all before saying so
                    read_whole(gp, whole);
                    has_row = whole.find('\n') != std::string::npos;
                }
            }
            if (has_row) break;
            if (segs.size() == 1) {
                // Nothing before it. Segment 0 with no complete row is either a fresh, empty tape
                // (legal: write genesis) or bytes that are not a row (fatal: never genesis, QC-1 F6).
                if (lastg == 0 && gsz == 0) {
                    if (!start_segment(0, GENESIS, err)) return false;
                    return commit(err);
                }
                if (err) *err = "fatal: " + gp + " has bytes and no recoverable head (no_complete_row)";
                return false;
            }
            remove_file(gp);
            segs.pop_back();
            ++ghosts; ghost_bytes += gsz;
        }

        // The last segment carries the head.
        const uint64_t last = segs.back();
        const std::string lp = path_join(cfg_.dir, seg_name(last));

        // The previous segment's last row is where a rolled segment's `prev` must come from. Absent
        // only when this IS segment 0, or when older segments have been evicted (§4.2 compaction),
        // in which case continuity across the missing files is the verifier's problem, not open's.
        std::string prev_of_first = GENESIS;
        bool        have_prev_seg = false;
        if (segs.size() >= 2) {
            const std::string pp = path_join(cfg_.dir, seg_name(segs[segs.size() - 2]));
            const HeadScan ph = scan_head(pp);
            if (!ph.found) { if (err) *err = "fatal: previous segment " + pp + " has no recoverable head (" + ph.reason + ")"; return false; }
            if (ph.torn_bytes) { if (err) *err = "fatal: previous segment " + pp + " is torn: it is not the last segment"; return false; }
            prev_of_first = ph.row.h;
            have_prev_seg = true;
        }

        const HeadScan hs = scan_head(lp);
        if (!hs.found) { if (err) *err = "fatal: " + lp + " has bytes and no recoverable head (" + hs.reason + ")"; return false; }

        // The segment's own header must chain to the previous segment's last row; genesis is legal on
        // segment 0 only. This is QC-1 F6's fix, asserted at open rather than only in the verifier.
        ScannedRow first;
        std::string why;
        if (!scan_first_row(lp, &first, &why)) { if (err) *err = "fatal: " + lp + " first row unreadable (" + why + ")"; return false; }
        if (first.hdr.k != kind::SEG) { if (err) *err = "fatal: " + lp + " does not begin with a seg entry"; return false; }
        uint64_t fp = 0; std::string pof;
        if (!read_seg_body(first.body, &fp, &pof)) { if (err) *err = "fatal: " + lp + " seg body malformed"; return false; }
        if (fp != last || first.hdr.pos != last) { if (err) *err = "fatal: " + lp + " first_pos does not match its name"; return false; }
        if (pof != first.prev) { if (err) *err = "fatal: " + lp + " header prev_of_first_entry disagrees with its own prev"; return false; }
        if (last == 0) {
            if (first.prev != GENESIS) { if (err) *err = "fatal: segment 0 does not begin at genesis"; return false; }
        } else {
            if (first.prev == GENESIS) { if (err) *err = "fatal: segment " + std::to_string(last) + " begins at genesis (a forked chain)"; return false; }
            if (have_prev_seg && first.prev != prev_of_first) { if (err) *err = "fatal: segment " + std::to_string(last) + " does not continue the previous segment"; return false; }
        }

        next_pos_   = hs.row.hdr.pos + 1;
        prev_       = hs.row.h;
        epoch_last_ = hs.row.hdr.t_epoch_ns;
        cur_first_pos_ = last;
        torn_at_open_  = hs.torn_bytes;

        if (!open_segment_file(last, err)) return false;
        if (hs.torn_bytes) {
            if (!file_.truncate_to(hs.complete_bytes, err)) return false;
            if (!file_.sync(err)) return false;
        }
        if (!file_size_of(lp, cur_bytes_)) { if (err) *err = "stat " + lp; return false; }

        // Everything discarded at open is a row on the tape. A store whose recovery is silent cannot
        // be audited, and "the refusal that never silences" is the same law one level down.
        if (cfg_.write_open_warn && (torn_at_open_ || ghosts)) {
            if (torn_at_open_) {
                json::Value b = json::Value::object();
                b.set("reason", json::Value::str("torn_row_discarded"));
                b.set("bytes", json::Value::u64(torn_at_open_));
                b.set("segment", json::Value::u64(last));
                b.set("at_offset", json::Value::u64(hs.complete_bytes));
                EntryHdr hd; hd.k = kind::WARN; hd.by = by::SYSTEM;
                Staged st; std::string reason;
                if (!stage(hd, b, json::WriteOpts(), &st, &reason)) { if (err) *err = "warn stage: " + reason; return false; }
            }
            if (ghosts) {
                json::Value b = json::Value::object();
                b.set("reason", json::Value::str("uncommitted_segment_discarded"));
                b.set("segments", json::Value::u64(ghosts));
                b.set("bytes", json::Value::u64(ghost_bytes));
                b.set("resumed_at_pos", json::Value::u64(next_pos_));
                EntryHdr hd; hd.k = kind::WARN; hd.by = by::SYSTEM;
                Staged st; std::string reason;
                if (!stage(hd, b, json::WriteOpts(), &st, &reason)) { if (err) *err = "warn stage: " + reason; return false; }
            }
            if (!commit(err)) return false;
        }
        return true;
    }

    // Assign a position, a monotone epoch stamp, the chain link and the hash; append the row's bytes
    // to the pending batch. NOT durable: the caller may not acknowledge until commit() returns true.
    bool stage(const EntryHdr& hd_in, const json::Value& body, const json::WriteOpts& opts,
               Staged* out, std::string* reason) {
        if (!file_.is_open()) { if (reason) *reason = "tape_closed"; return false; }

        // Roll BEFORE the row that would overflow the segment, so a segment never exceeds its size and
        // the first row of the new one is its header.
        if (cur_bytes_ + pending_bytes_ >= cfg_.segment_bytes) {
            std::string err;
            if (!roll(&err)) { if (reason) *reason = "roll: " + err; return false; }
        }

        EntryHdr hd = hd_in;
        hd.pos  = next_pos_;
        hd.term = cfg_.term;
        // §3.1: the epoch stamp comes FROM THE TRANSACTOR. A caller that has already reserved one —
        // because its constraints had to read `now` before the entry existed — passes it in, and the
        // tape still enforces the monotone rule over it. Zero means "take one from the clock".
        hd.t_epoch_ns = stamp_from(hd_in.t_epoch_ns);

        BuiltRow br;
        if (!build_row(hd, body, opts, prev_, &br, reason)) return false;

        pending_.append(br.line);
        pending_bytes_ += br.line.size();
        ++pending_count_;
        prev_ = br.h;
        next_pos_ = hd.pos + 1;
        epoch_last_ = hd.t_epoch_ns;

        if (out) { out->pos = hd.pos; out->t_epoch_ns = hd.t_epoch_ns; out->h = br.h; out->seg_first_pos = cur_first_pos_; }
        return true;
    }

    // Write the batch, flush it, and only then publish. One FlushFileBuffers per batch per segment.
    bool commit(std::string* err) {
        if (pending_.empty() && closed_chunks_.empty()) return true;

        for (auto& ch : closed_chunks_) {
            File f;
            if (!f.open_append(ch.first, err)) return false;
            if (!f.write_all(ch.second, err)) return false;
            if (cfg_.sync_on_commit && !f.sync(err)) return false;
            f.close();
        }
        closed_chunks_.clear();

        if (!pending_.empty()) {
            if (!file_.write_all(pending_, err)) return false;
            if (cfg_.sync_on_commit && !file_.sync(err)) return false;
            cur_bytes_ += pending_.size();
        }

        published_pos_ = next_pos_ - 1;
        any_published_ = next_pos_ > 0;
        published_h_   = prev_;
        committed_count_ += pending_count_;
        pending_.clear(); pending_bytes_ = 0; pending_count_ = 0;
        return true;
    }

    // Published (durable) state. Nothing here moves until commit() has returned true.
    bool        has_committed()   const { return any_published_; }
    uint64_t    committed_pos()   const { return published_pos_; }
    std::string head()            const { return published_h_; }
    uint64_t    next_pos()        const { return next_pos_; }
    uint64_t    pending_bytes()   const { return pending_bytes_; }
    uint64_t    pending_count()   const { return pending_count_; }
    uint64_t    committed_count() const { return committed_count_; }
    uint64_t    torn_at_open()    const { return torn_at_open_; }
    uint64_t    current_segment() const { return cur_first_pos_; }

    // The stamp the next entry would carry. A validator that must read `now` before deciding whether
    // there will BE an entry reserves it here and hands it back to stage().
    uint64_t    reserve_stamp() { return stamp(); }

    void close() { file_.close(); }
    ~Tape() { close(); }

private:
    uint64_t stamp() {
        const uint64_t obs = clk_ ? clk_->now_ns() : 0;
        const uint64_t mono = (epoch_last_ == 0) ? obs : ((obs > epoch_last_) ? obs : epoch_last_ + 1);
        return mono;
    }
    uint64_t stamp_from(uint64_t supplied) {
        if (supplied == 0) return stamp();
        if (epoch_last_ == 0) return supplied;
        return (supplied > epoch_last_) ? supplied : epoch_last_ + 1;
    }

    bool open_segment_file(uint64_t first_pos, std::string* err) {
        file_.close();
        cur_first_pos_ = first_pos;
        return file_.open_append(path_join(cfg_.dir, seg_name(first_pos)), err);
    }

    bool write_seg_header(uint64_t first_pos, const std::string& prev_of_first, std::string* err) {
        EntryHdr hd; hd.k = kind::SEG; hd.by = by::SYSTEM;
        hd.pos = first_pos; hd.term = cfg_.term; hd.t_epoch_ns = stamp();
        BuiltRow br; std::string reason;
        if (!build_row(hd, seg_body(first_pos, prev_of_first), json::WriteOpts(), prev_of_first, &br, &reason)) {
            if (err) *err = "seg header: " + reason; return false;
        }
        pending_.append(br.line);
        pending_bytes_ += br.line.size();
        ++pending_count_;
        prev_ = br.h;
        next_pos_ = first_pos + 1;
        epoch_last_ = hd.t_epoch_ns;
        return true;
    }

    bool start_segment(uint64_t first_pos, const std::string& prev_of_first, std::string* err) {
        next_pos_ = first_pos;
        prev_ = prev_of_first;
        if (!open_segment_file(first_pos, err)) return false;
        cur_bytes_ = 0;
        return write_seg_header(first_pos, prev_of_first, err);
    }

    // A roll inside an uncommitted batch: the bytes already staged for the old segment are set aside
    // as a closed chunk and written by the same commit, so one batch may span a roll and still be one
    // flush per file.
    bool roll(std::string* err) {
        if (!pending_.empty()) {
            closed_chunks_.emplace_back(path_join(cfg_.dir, seg_name(cur_first_pos_)), pending_);
            pending_.clear(); pending_bytes_ = 0;
        }
        const uint64_t first_pos = next_pos_;
        const std::string prev_of_first = prev_;
        if (!open_segment_file(first_pos, err)) return false;
        uint64_t sz = 0;
        if (file_.size(&sz, err) && sz != 0) { if (err) *err = "roll: segment " + seg_name(first_pos) + " already exists"; return false; }
        cur_bytes_ = 0;
        return write_seg_header(first_pos, prev_of_first, err);
    }

    TapeConfig cfg_;
    Clock*     clk_ = nullptr;
    File       file_;
    std::string prev_ = GENESIS;
    std::string published_h_ = GENESIS;
    uint64_t   next_pos_ = 0;
    uint64_t   published_pos_ = 0;
    bool       any_published_ = false;
    uint64_t   epoch_last_ = 0;
    uint64_t   cur_first_pos_ = 0;
    uint64_t   cur_bytes_ = 0;
    uint64_t   torn_at_open_ = 0;
    uint64_t   committed_count_ = 0;
    std::string pending_;
    uint64_t   pending_bytes_ = 0;
    uint64_t   pending_count_ = 0;
    std::vector<std::pair<std::string, std::string>> closed_chunks_;
};

// ---- reading the tape back --------------------------------------------------------------------------
// Every fold rebuilds by replaying committed entries in position order. The callback returns false to
// stop. A row that does not scan stops the walk with `err` set: a fold may not skip a row it cannot
// read and call the result a fold.
template <typename Fn>
inline bool for_each_row(const std::string& dir, Fn&& fn, std::string* err) {
    for (uint64_t sp : list_segments(dir)) {
        const std::string path = path_join(dir, seg_name(sp));
        std::string data;
        if (!read_whole(path, data)) { if (err) *err = "unreadable: " + path; return false; }
        size_t off = 0, line_no = 0;
        while (off < data.size()) {
            const size_t nl = data.find('\n', off);
            if (nl == std::string::npos) {
                if (err) *err = path + ": torn trailing bytes";
                return false;
            }
            const std::string line = data.substr(off, nl - off);
            off = nl + 1; ++line_no;
            ScannedRow r; std::string why;
            if (!scan_row(line, &r, &why, true)) {
                if (err) *err = path + ":" + std::to_string(line_no) + ": " + why;
                return false;
            }
            if (!fn(r)) return true;
        }
    }
    return true;
}

// ---- the verifier -----------------------------------------------------------------------------------
// QC-1 F6 again: "Replace the 'seam' concession in the verifier with an explicit expected-prev
// argument: a tool that accepts either answer is not a falsifier." So: an expected prev is REQUIRED
// per segment (genesis only for segment 0), a break is a break, and there is no seam concession.
struct VerifyReport {
    uint64_t rows = 0;
    uint64_t breaks = 0;
    uint64_t segments = 0;
    uint64_t first_pos = 0;
    uint64_t last_pos = 0;
    std::string head = GENESIS;
    std::vector<std::string> problems;
    bool ok() const { return breaks == 0; }
};

inline void verify_dir(const std::string& dir, VerifyReport* rep) {
    const std::vector<uint64_t> segs = list_segments(dir);
    std::string expect_prev = GENESIS;
    uint64_t expect_pos = 0;
    uint64_t expect_epoch = 0;
    bool first_row_seen = false;

    for (size_t si = 0; si < segs.size(); ++si) {
        const uint64_t sp = segs[si];
        const std::string path = path_join(dir, seg_name(sp));
        std::string data;
        if (!read_whole(path, data)) { rep->problems.push_back(path + ": unreadable"); ++rep->breaks; continue; }
        ++rep->segments;

        size_t line_no = 0, off = 0;
        bool seg_header_checked = false;
        while (off < data.size()) {
            const size_t nl = data.find('\n', off);
            if (nl == std::string::npos) {
                rep->problems.push_back(path + ": torn trailing bytes (" + std::to_string(data.size() - off) + ")");
                ++rep->breaks; break;
            }
            const std::string line = data.substr(off, nl - off);
            off = nl + 1; ++line_no;

            ScannedRow r; std::string why;
            if (!scan_row(line, &r, &why, true)) {
                rep->problems.push_back(path + ":" + std::to_string(line_no) + ": " + why);
                ++rep->breaks; continue;
            }
            if (r.prev != expect_prev) {
                rep->problems.push_back(path + ":" + std::to_string(line_no) + ": PREV LINK BREAK have " +
                                        r.prev.substr(0, 16) + " want " + expect_prev.substr(0, 16));
                ++rep->breaks;
            }
            if (r.hdr.pos != expect_pos) {
                rep->problems.push_back(path + ":" + std::to_string(line_no) + ": POSITION BREAK have " +
                                        std::to_string(r.hdr.pos) + " want " + std::to_string(expect_pos));
                ++rep->breaks;
            }
            if (first_row_seen && r.hdr.t_epoch_ns <= expect_epoch) {
                rep->problems.push_back(path + ":" + std::to_string(line_no) + ": EPOCH NOT MONOTONE " +
                                        std::to_string(r.hdr.t_epoch_ns) + " <= " + std::to_string(expect_epoch));
                ++rep->breaks;
            }
            if (!seg_header_checked) {
                seg_header_checked = true;
                uint64_t fp = 0; std::string pof;
                if (r.hdr.k != kind::SEG) {
                    rep->problems.push_back(path + ": first row is not a seg entry"); ++rep->breaks;
                } else if (!read_seg_body(r.body, &fp, &pof)) {
                    rep->problems.push_back(path + ": seg body malformed"); ++rep->breaks;
                } else {
                    if (fp != sp) { rep->problems.push_back(path + ": first_pos " + std::to_string(fp) + " != file name"); ++rep->breaks; }
                    if (pof != r.prev) { rep->problems.push_back(path + ": prev_of_first_entry != own prev"); ++rep->breaks; }
                    if (sp == 0 && r.prev != GENESIS) { rep->problems.push_back(path + ": segment 0 not at genesis"); ++rep->breaks; }
                    if (sp != 0 && r.prev == GENESIS) { rep->problems.push_back(path + ": FORKED — segment " + std::to_string(sp) + " begins at genesis"); ++rep->breaks; }
                }
            }
            if (!first_row_seen) { rep->first_pos = r.hdr.pos; first_row_seen = true; }
            expect_prev = r.h;
            expect_pos  = r.hdr.pos + 1;
            expect_epoch = r.hdr.t_epoch_ns;
            rep->last_pos = r.hdr.pos;
            rep->head = r.h;
            ++rep->rows;
        }
    }
}

} // namespace tapestry
