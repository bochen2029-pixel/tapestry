// =====================================================================================================
// TAPESTRY · src/tools/tapectl.cpp · the tape's command line
//
//   tapectl gen    --dir D --entries N [--seg-bytes B] [--t0 NS] [--step NS] [--batch K]
//   tapectl verify --dir D [--expect-prev HEX]
//   tapectl head   --dir D
//   tapectl dump   --dir D [--from P] [--to P] [--kind K]
//   tapectl digest --dir D
//
// `gen` is the deterministic tape generator R0 owes: given the same arguments it writes the same
// bytes, so "two cold replays bit-identical" is checkable with a digest and not with a promise.
// `verify` is the falsifier's verifier: an expected prev is required, a fork is a break, and there is
// no seam concession (QC-1 F6 — "a tool that accepts either answer is not a falsifier").
// =====================================================================================================

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <algorithm>
#include "../core/blake2b.h"
#include "../core/bytes.h"
#include "../core/json.h"
#include "../core/fileio.h"
#include "../tape/entry.h"
#include "../tape/tape.h"

using namespace tapestry;

static const char* USAGE =
"tapectl — the TAPESTRY tape\n"
"  tapectl gen    --dir D --entries N [--seg-bytes B] [--t0 NS] [--step NS] [--batch K] [--no-sync]\n"
"  tapectl verify --dir D [--expect-prev HEX]\n"
"  tapectl head   --dir D\n"
"  tapectl status --dir D                  what a reopen would find, without writing\n"
"  tapectl dump   --dir D [--from P] [--to P] [--kind K]\n"
"  tapectl digest --dir D\n"
"  tapectl bench  --dir D --entries N [--batch K] [--no-sync]\n";

struct Args {
    std::string dir, expect_prev, kind;
    uint64_t entries = 0, seg_bytes = 64ull << 20, t0 = 1757000000000000000ull, step = 1000000ull;
    uint64_t batch = 16, from = 0, to = ~0ull;
    bool no_sync = false;
};

static bool parse(int argc, char** argv, Args* a) {
    for (int i = 2; i < argc; ++i) {
        const std::string k = argv[i];
        auto next = [&](uint64_t* out) {
            if (i + 1 >= argc) return false;
            *out = std::strtoull(argv[++i], nullptr, 10); return true;
        };
        if (k == "--dir" && i + 1 < argc)              a->dir = argv[++i];
        else if (k == "--expect-prev" && i + 1 < argc) a->expect_prev = argv[++i];
        else if (k == "--kind" && i + 1 < argc)        a->kind = argv[++i];
        else if (k == "--entries")                     { if (!next(&a->entries)) return false; }
        else if (k == "--seg-bytes")                   { if (!next(&a->seg_bytes)) return false; }
        else if (k == "--t0")                          { if (!next(&a->t0)) return false; }
        else if (k == "--step")                        { if (!next(&a->step)) return false; }
        else if (k == "--batch")                       { if (!next(&a->batch)) return false; }
        else if (k == "--from")                        { if (!next(&a->from)) return false; }
        else if (k == "--to")                          { if (!next(&a->to)) return false; }
        else if (k == "--no-sync")                     a->no_sync = true;
        else { std::fprintf(stderr, "unknown argument: %s\n", k.c_str()); return false; }
    }
    if (a->dir.empty()) { std::fprintf(stderr, "--dir is required\n"); return false; }
    return true;
}

// The generated wire: a plausible order book, written from a pure function of the entry index, so
// the only inputs to the bytes are the arguments on the command line.
static json::Value gen_body(uint64_t i) {
    json::Value f = json::Value::object();
    f.set("table", json::Value::str("orders"));
    f.set("key", json::Value::str("o-" + std::to_string(i % 9973)));
    f.set("op", json::Value::str(i % 5 == 0 ? "insert" : "update"));
    f.set("source_pos", json::Value::u64(1000000 + i));
    json::Value after = json::Value::object();
    after.set("status", json::Value::str(i % 3 == 0 ? "open" : (i % 3 == 1 ? "picked" : "shipped")));
    after.set("amount_cents", json::Value::u64((i * 977) % 1000000));
    f.set("after", after);
    json::Value facts = json::Value::array(); facts.add(f);
    json::Value b = json::Value::object();
    b.set("facts", facts);
    b.set("basis_pos", json::Value::u64(i));
    return b;
}

static int cmd_gen(const Args& a) {
    FixedClock clk(a.t0, a.step);
    TapeConfig cfg;
    cfg.dir = a.dir; cfg.segment_bytes = a.seg_bytes; cfg.sync_on_commit = !a.no_sync;
    Tape tape; std::string err;
    if (!tape.open(cfg, &clk, &err)) { std::fprintf(stderr, "open: %s\n", err.c_str()); return 2; }
    for (uint64_t i = 0; i < a.entries; ++i) {
        EntryHdr hd; hd.k = kind::TX; hd.by = by::seat("gen");
        hd.cell = 1 + (i % 50000); hd.cls = (uint32_t)(i % 11);
        Staged st; std::string why;
        if (!tape.stage(hd, gen_body(i), json::WriteOpts(), &st, &why)) {
            std::fprintf(stderr, "stage @%llu: %s\n", (unsigned long long)i, why.c_str()); return 2;
        }
        if (a.batch && (i % a.batch) == a.batch - 1) {
            if (!tape.commit(&err)) { std::fprintf(stderr, "commit: %s\n", err.c_str()); return 2; }
        }
    }
    if (!tape.commit(&err)) { std::fprintf(stderr, "commit: %s\n", err.c_str()); return 2; }
    std::printf("gen dir=%s entries=%llu committed_pos=%llu head=%s segments=%llu\n",
                a.dir.c_str(), (unsigned long long)a.entries,
                (unsigned long long)tape.committed_pos(), tape.head().c_str(),
                (unsigned long long)list_segments(a.dir).size());
    tape.close();
    return 0;
}

static int cmd_verify(const Args& a) {
    VerifyReport rep;
    verify_dir(a.dir, &rep);
    if (!a.expect_prev.empty()) {
        // The head the caller says this tape must end on. A verifier that will accept either answer
        // is not a falsifier, so this is a break, not a note.
        if (rep.head != a.expect_prev) {
            rep.problems.push_back("HEAD MISMATCH have " + rep.head + " want " + a.expect_prev);
            ++rep.breaks;
        }
    }
    for (const std::string& p : rep.problems) std::printf("  %s\n", p.c_str());
    std::printf("%s: segments=%llu rows=%llu pos=[%llu..%llu] head=%s breaks=%llu\n",
                a.dir.c_str(), (unsigned long long)rep.segments, (unsigned long long)rep.rows,
                (unsigned long long)rep.first_pos, (unsigned long long)rep.last_pos,
                rep.head.c_str(), (unsigned long long)rep.breaks);
    std::printf(rep.ok() ? "CHAIN OK\n" : "CHAIN BROKEN\n");
    return rep.ok() ? 0 : 1;
}

static int cmd_head(const Args& a) {
    const std::vector<uint64_t> segs = list_segments(a.dir);
    if (segs.empty()) { std::printf("no segments in %s\n", a.dir.c_str()); return 1; }
    const std::string path = path_join(a.dir, seg_name(segs.back()));
    const HeadScan hs = scan_head(path);
    if (!hs.found) { std::fprintf(stderr, "no recoverable head in %s (%s)\n", path.c_str(), hs.reason.c_str()); return 2; }
    std::printf("segment=%llu pos=%llu t_epoch_ns=%llu k=%s h=%s torn_bytes=%llu\n",
                (unsigned long long)segs.back(), (unsigned long long)hs.row.hdr.pos,
                (unsigned long long)hs.row.hdr.t_epoch_ns, hs.row.hdr.k.c_str(),
                hs.row.h.c_str(), (unsigned long long)hs.torn_bytes);
    return 0;
}

// What a reopen WOULD find, without touching a byte. `head` fails on a tape whose last segment is the
// ghost of an uncommitted roll; that is not a broken tape, it is the ordinary shape after a kill, and
// a tool that cannot tell the two apart makes every kill look like corruption.
static int cmd_status(const Args& a) {
    const std::vector<uint64_t> segs = list_segments(a.dir);
    if (segs.empty()) { std::printf("segments=0 state=empty\n"); return 0; }
    uint64_t ghosts = 0, ghost_bytes = 0;
    size_t idx = segs.size();
    HeadScan hs;
    while (idx > 0) {
        const std::string p = path_join(a.dir, seg_name(segs[idx - 1]));
        hs = scan_head(p);
        if (hs.found) break;
        uint64_t gsz = 0; file_size_of(p, gsz);
        ++ghosts; ghost_bytes += gsz;
        --idx;
    }
    if (idx == 0) {
        std::printf("segments=%llu ghost_segments=%llu ghost_bytes=%llu state=no_recoverable_head\n",
                    (unsigned long long)segs.size(), (unsigned long long)ghosts, (unsigned long long)ghost_bytes);
        return 2;
    }
    std::printf("segments=%llu head_segment=%llu ghost_segments=%llu ghost_bytes=%llu "
                "last_pos=%llu torn_bytes=%llu head=%s\n",
                (unsigned long long)segs.size(), (unsigned long long)segs[idx - 1],
                (unsigned long long)ghosts, (unsigned long long)ghost_bytes,
                (unsigned long long)hs.row.hdr.pos, (unsigned long long)hs.torn_bytes, hs.row.h.c_str());
    return 0;
}

static int cmd_dump(const Args& a) {
    uint64_t shown = 0;
    for (uint64_t sp : list_segments(a.dir)) {
        std::string data;
        if (!read_whole(path_join(a.dir, seg_name(sp)), data)) continue;
        size_t off = 0;
        while (off < data.size()) {
            const size_t nl = data.find('\n', off);
            if (nl == std::string::npos) break;
            const std::string line = data.substr(off, nl - off);
            off = nl + 1;
            ScannedRow r; std::string why;
            if (!scan_row(line, &r, &why, true)) { std::printf("!! %s\n", why.c_str()); continue; }
            if (r.hdr.pos < a.from || r.hdr.pos > a.to) continue;
            if (!a.kind.empty() && r.hdr.k != a.kind) continue;
            std::printf("%s\n", line.c_str());
            ++shown;
        }
    }
    std::fprintf(stderr, "dumped %llu rows\n", (unsigned long long)shown);
    return 0;
}

// The durability number, measured rather than promised. §4.1's target is a LATENCY SLO — p99 commit
// under 10 ms — not a throughput headline, "because the measured failure mode on this estate is
// latency-to-notice". The `--no-sync` arm is what fusord's writer was actually timing when it called
// fflush durable, and printing both side by side is the point.
static int cmd_bench(const Args& a) {
    FixedClock clk(a.t0, a.step);
    TapeConfig cfg;
    cfg.dir = a.dir; cfg.segment_bytes = a.seg_bytes; cfg.sync_on_commit = !a.no_sync;
    Tape tape; std::string err;
    if (!tape.open(cfg, &clk, &err)) { std::fprintf(stderr, "open: %s\n", err.c_str()); return 2; }

    std::vector<double> ms;
    ms.reserve(a.entries / (a.batch ? a.batch : 1) + 2);
    const auto t_start = std::chrono::steady_clock::now();
    uint64_t staged_in_batch = 0;
    for (uint64_t i = 0; i < a.entries; ++i) {
        EntryHdr hd; hd.k = kind::TX; hd.by = by::seat("bench");
        hd.cell = 1 + (i % 50000); hd.cls = (uint32_t)(i % 11);
        Staged st; std::string why;
        if (!tape.stage(hd, gen_body(i), json::WriteOpts(), &st, &why)) {
            std::fprintf(stderr, "stage @%llu: %s\n", (unsigned long long)i, why.c_str()); return 2;
        }
        ++staged_in_batch;
        if (a.batch && staged_in_batch >= a.batch) {
            const auto t0 = std::chrono::steady_clock::now();
            if (!tape.commit(&err)) { std::fprintf(stderr, "commit: %s\n", err.c_str()); return 2; }
            const auto t1 = std::chrono::steady_clock::now();
            ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            staged_in_batch = 0;
        }
    }
    if (staged_in_batch) {
        const auto t0 = std::chrono::steady_clock::now();
        if (!tape.commit(&err)) { std::fprintf(stderr, "commit: %s\n", err.c_str()); return 2; }
        const auto t1 = std::chrono::steady_clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    const auto t_end = std::chrono::steady_clock::now();
    const double total_s = std::chrono::duration<double>(t_end - t_start).count();

    std::sort(ms.begin(), ms.end());
    auto pct = [&](double p) -> double {
        if (ms.empty()) return 0.0;
        size_t k = (size_t)(p * (double)(ms.size() - 1) + 0.5);
        return ms[k];
    };
    std::printf("bench dir=%s entries=%llu batch=%llu sync=%s commits=%llu\n",
                a.dir.c_str(), (unsigned long long)a.entries, (unsigned long long)a.batch,
                cfg.sync_on_commit ? "yes" : "NO", (unsigned long long)ms.size());
    std::printf("  commit_ms p50=%.3f p95=%.3f p99=%.3f max=%.3f\n", pct(0.50), pct(0.95), pct(0.99),
                ms.empty() ? 0.0 : ms.back());
    std::printf("  entries_per_s=%.0f  seconds=%.3f  head=%s\n",
                total_s > 0 ? (double)a.entries / total_s : 0.0, total_s, tape.head().c_str());
    tape.close();
    return 0;
}

static int cmd_digest(const Args& a) {
    Blake2b b; b.init(32);
    uint64_t bytes = 0, rows = 0;
    for (uint64_t sp : list_segments(a.dir)) {
        std::string data;
        read_whole(path_join(a.dir, seg_name(sp)), data);
        b.update((const uint8_t*)data.data(), data.size());
        bytes += data.size();
        for (char c : data) if (c == '\n') ++rows;
    }
    uint8_t out[32]; b.finish(out);
    std::printf("digest=%s bytes=%llu rows=%llu segments=%llu\n",
                hex_of(out, 32).c_str(), (unsigned long long)bytes, (unsigned long long)rows,
                (unsigned long long)list_segments(a.dir).size());
    return 0;
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fputs(USAGE, stderr); return 2; }
    Args a;
    if (!parse(argc, argv, &a)) { std::fputs(USAGE, stderr); return 2; }
    const std::string cmd = argv[1];
    if (cmd == "gen")    return cmd_gen(a);
    if (cmd == "verify") return cmd_verify(a);
    if (cmd == "head")   return cmd_head(a);
    if (cmd == "status") return cmd_status(a);
    if (cmd == "dump")   return cmd_dump(a);
    if (cmd == "digest") return cmd_digest(a);
    if (cmd == "bench")  return cmd_bench(a);
    std::fputs(USAGE, stderr);
    return 2;
}
