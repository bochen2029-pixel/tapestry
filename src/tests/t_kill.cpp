// =====================================================================================================
// TAPESTRY · src/tests/t_kill.cpp · R0's durability gate, driven from a process that survives
//
// §9's R0 gate: "1,000 kills under load lose no acknowledged entry and keep no unacknowledged one;
// a redelivered suffix does not double-count; a retried write applies once."
//
// WHO HOLDS THE RECORD MATTERS. An acknowledgement is only evidence if the thing recording it outlives
// the thing that sent it, so the client is THIS process and the transactor is a child. The parent
// remembers every (request_id → pos, h) it was told, kills the child — often from inside a write, by
// the fault injector, which two hundred real kills in R0.1 never once managed — and then checks the
// tape it left behind against what it was promised.
//
// AND ONE HONEST CORRECTION TO THE GATE. "Keep no unacknowledged one" is not achievable under a
// PROCESS kill and never was: the operating system holds written-but-unflushed bytes and lands them
// after the process is gone, so a batch that was written and not yet flushed survives as entries
// nobody was told about. That is not a defect, it is what at-least-once means one layer down. The
// achievable and equally strong property, measured here:
//
//     no acknowledged entry is ever lost, and every unacknowledged survivor is deduplicated by the
//     reply cache when the client retries — so the world never sees the write twice.
//
// This was flagged in the R0.1 receipt before the harness existed, rather than discovered at the gate.
// =====================================================================================================

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include <thread>
#include "../core/fileio.h"
#include "../core/json.h"
#include "../tape/tape.h"
#include "../tx/transactor.h"
#include "../net/frames.h"
#include "../net/protocol.h"

#if defined(_WIN32)
#  include <windows.h>
#endif

using namespace tapestry;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}

// ---- the child ---------------------------------------------------------------------------------
struct Child {
#if defined(_WIN32)
    PROCESS_INFORMATION pi{};
    bool alive() const { return pi.hProcess != nullptr; }
    void kill_hard() {
        if (pi.hProcess) { TerminateProcess(pi.hProcess, 9); WaitForSingleObject(pi.hProcess, 5000); }
    }
    void wait_and_close() {
        if (pi.hProcess) {
            WaitForSingleObject(pi.hProcess, 10000);
            CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
            pi.hProcess = nullptr;
        }
    }
#endif
};

static std::string g_exe;      // path to tapestryd.exe
static std::string g_root;

static bool spawn_daemon(const std::string& dir, const std::string& portfile,
                         const std::string& fault, Child* ch, uint16_t* port) {
#if defined(_WIN32)
    remove_file(portfile);
    _putenv_s("TAPESTRY_FAULT", fault.c_str());
    std::string cmd = "\"" + g_exe + "\" --dir \"" + dir + "\" --port 0 --portfile \"" + portfile +
                      "\" --seg-bytes 262144";
    std::vector<char> buf(cmd.begin(), cmd.end());
    buf.push_back('\0');
    STARTUPINFOA si{}; si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    ZeroMemory(&ch->pi, sizeof(ch->pi));
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &ch->pi)) return false;
    // The daemon writes its port atomically before it serves anything, so there is no race to lose.
    for (int i = 0; i < 400; ++i) {
        std::string s;
        if (read_whole(portfile, s) && s.size() > 1) {
            *port = (uint16_t)std::strtoul(s.c_str(), nullptr, 10);
            if (*port) return true;
        }
        if (WaitForSingleObject(ch->pi.hProcess, 0) == WAIT_OBJECT_0) return false;   // died at boot
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
#else
    (void)dir; (void)portfile; (void)fault; (void)ch; (void)port;
    return false;
#endif
}

// ---- the client --------------------------------------------------------------------------------
static std::string write_frame_body(const std::string& client_id, const std::string& request_id,
                                    const std::string& key, uint64_t source_pos, int64_t amount) {
    WriteReq w;
    w.client_id = client_id; w.request_id = request_id;
    Fact f;
    f.source = "olist"; f.table = "orders"; f.key = key; f.op = "update";
    f.source_pos = source_pos; f.cls = 1; f.amount_minor = amount; f.has_inverse = true;
    w.facts.push_back(f);
    std::string out; std::string why;
    json::WriteOpts opts; opts.allow_f64 = false;
    json::write(tx_body(w, "n/a"), out, opts, &why);
    return out;
}

struct Acked { uint64_t pos; std::string h; };

// Every position the tape actually holds, with its hash and the request that made it.
struct TapeIndex {
    std::map<uint64_t, std::string> pos_to_h;
    std::map<std::string, uint64_t> request_to_pos;
    uint64_t rows = 0, breaks = 0, torn_tail = 0;
};
static TapeIndex index_tape(const std::string& dir) {
    TapeIndex ix;
    VerifyReport rep; verify_dir(dir, &rep);
    ix.breaks = rep.breaks;              // a torn tail is NOT one: see VerifyReport
    ix.torn_tail = rep.torn_tail_bytes;
    std::string err;
    for_each_row(dir, [&](const ScannedRow& r) {
        ix.pos_to_h[r.hdr.pos] = r.h;
        ++ix.rows;
        if (r.hdr.k == kind::TX) {
            std::string cid, rid, rev; uint64_t basis = 0; std::vector<Fact> facts;
            if (read_tx_body(r.body, &cid, &rid, &basis, &rev, &facts))
                ix.request_to_pos[cid + "\x1f" + rid] = r.hdr.pos;
        }
        return true;
    }, &err);
    return ix;
}

// =====================================================================================================
// The redelivered suffix — no process needed, and the cheapest of the three gates to get wrong
// =====================================================================================================
static void t_redelivered_suffix() {
    const std::string dir = path_join(g_root, "redeliver");
    make_dir(dir);
#if defined(_WIN32)
    WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA(path_join(dir, "*").c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do { if (fd.cFileName[0] != '.') DeleteFileA(path_join(dir, fd.cFileName).c_str()); }
        while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }
#endif
    FixedClock clk;
    Tape tape; TapeConfig tc; tc.dir = dir;
    std::string err;
    check(tape.open(tc, &clk, &err), "suffix.tape", err);
    writ::ClassMap map;
    writ::ClassDef d; d.cls = 1; d.name = "orders";
    // A CAP, on purpose. The cell table alone cannot catch a double-apply — setting the same amount
    // twice lands on the same value — so the oracle would pass for the wrong reason. An accumulating
    // fold is where redelivery shows up, and it shows up as money.
    d.cap_minor_per_period = 100000;
    writ::RevRule rr; rr.table = "*"; rr.op = "*"; rr.rev = writ::Rev::ReversibleByInverse;
    d.rev.push_back(rr);
    std::string why; check(map.add(d, {}, &why), "suffix.map", why);
    Transactor tx; TxConfig cfg; cfg.seam_code_hash = "abc123";
    check(tx.open(&tape, &map, cfg, dir, &err), "suffix.open", err);

    // Three cells, each touched four times with a RISING source revision and a rising amount. The
    // repetition matters: a suffix of identical facts is already idempotent through the ingest law,
    // so a redelivery test built on one would pass with no position guard at all. A suffix that
    // REGRESSES a cell's revision is the case only the position guard catches.
    for (int i = 0; i < 12; ++i) {
        WriteReq w;
        w.client_id = "c"; w.request_id = "r" + std::to_string(i); w.by = by::seat("s");
        Fact f;
        f.source = "olist"; f.table = "orders"; f.key = "o-" + std::to_string(i % 3);
        f.op = "update"; f.source_pos = 1 + (uint64_t)i; f.cls = 1;
        f.amount_minor = 100 * (i + 1); f.has_inverse = true;
        w.facts.push_back(f);
        const WriteRes r = tx.write(w);
        check(r.committed, "suffix.write", r.refusal + r.fault);
    }
    const std::string digest_once = tx.cells().digest();
    const uint64_t applied_once = tx.applied_pos();
    const int64_t  cap_once = tx.cap_used(1);
    check(cap_once > 0, "suffix.cap_accumulated", std::to_string(cap_once));

    // Deliver the last six rows a second time, exactly as an at-least-once subscriber would.
    std::vector<ScannedRow> rows;
    std::string werr;
    for_each_row(dir, [&](const ScannedRow& r) { rows.push_back(r); return true; }, &werr);
    size_t from = rows.size() > 6 ? rows.size() - 6 : 0;
    for (size_t i = from; i < rows.size(); ++i) tx.deliver(rows[i]);

    check(tx.cells().digest() == digest_once, "suffix.no_double_count");
    check(tx.cap_used(1) == cap_once, "suffix.the_cap_did_not_move",
          std::to_string(tx.cap_used(1)) + " was " + std::to_string(cap_once));
    check(tx.applied_pos() == applied_once, "suffix.applied_pos_unmoved",
          std::to_string(tx.applied_pos()) + " was " + std::to_string(applied_once));
    check(tx.redelivered() == rows.size() - from, "suffix.redelivery_counted",
          std::to_string(tx.redelivered()));

    // Lie arm: a fold with no position guard is one whose applied position sits below the suffix.
    // Feed it the same six rows and the exposure cap moves — the same money counted twice — because
    // each redelivered row regresses its cell's revision and the next one re-adds the increase. The
    // ingest law does not catch this: it compares revisions for EQUALITY, and these are not equal.
    Transactor naive;
    check(naive.open(&tape, &map, cfg, dir, &err), "suffix.naive_open", err);
    const int64_t naive_before = naive.cap_used(1);
    naive.rewind_applied_for_test(rows[from].hdr.pos - 1);
    for (size_t i = from; i < rows.size(); ++i) naive.deliver(rows[i]);
    check(naive.cap_used(1) > naive_before, "suffix.lie.without_the_guard_the_cap_moves",
          std::to_string(naive_before) + " -> " + std::to_string(naive.cap_used(1)));
    // And the cell table comes back to the SAME digest even without the guard, because replaying a
    // suffix ends on the same last write per cell. That is falsifier 5's scoping exactly — "state-
    // bearing folds return to their digest; accumulating folds gain exactly the rows the transaction
    // produced" — and it is why a redelivery test that watches only the cell table sees nothing. The
    // damage lives in the accumulator, which is where the money is.
    check(naive.cells().digest() == digest_once, "suffix.lie.the_cell_table_cannot_detect_it");

    std::string e2; tx.flush_refusals(&e2);
    tape.close();
}

// =====================================================================================================
// The wire: what a length-prefixed frame must refuse
// =====================================================================================================
static void t_protocol() {
    const std::string dir = path_join(g_root, "proto");
    make_dir(dir);
    const std::string portfile = path_join(dir, "port.txt");
    Child ch; uint16_t port = 0;
    if (!spawn_daemon(dir, portfile, "", &ch, &port)) { check(false, "proto.spawn"); return; }

    net::Conn c; std::string err;
    check(net::connect_to(port, &c, &err), "proto.connect", err);

    auto call = [&](const net::Request& rq, net::Response* rs) {
        if (!c.send_frame(net::enc_request(rq))) return false;
        std::string resp; bool tl = false;
        if (!c.recv_frame(&resp, &tl)) return false;
        return net::dec_response(resp, rs);
    };

    net::Request rq; net::Response rs;
    rq.call = "health";
    check(call(rq, &rs) && rs.ok && rs.info.find("committed_pos=") != std::string::npos,
          "proto.health", rs.info);

    // A malformed body is a COUNTER-class fault and the connection survives it: protocol noise does
    // not become a claim on the tape, and it does not cost the caller its session either.
    rq = net::Request(); rq.call = "write"; rq.by = by::seat("x"); rq.body = "{\"not\":\"a tx body\"}";
    rs = net::Response();
    check(call(rq, &rs) && !rs.ok && rs.fault == fault::MALFORMED, "proto.malformed_body_is_a_fault", rs.fault);
    rq = net::Request(); rq.call = "health"; rs = net::Response();
    check(call(rq, &rs) && rs.ok, "proto.connection_survives_a_malformed_frame");

    // An unknown call is the same: named, counted, survivable.
    rq = net::Request(); rq.call = "subscribe"; rs = net::Response();
    check(call(rq, &rs) && !rs.ok && rs.fault == fault::MALFORMED, "proto.unknown_call_is_a_fault", rs.fault);

    // A write that IS well formed goes through, so the refusals above are not the wire refusing
    // everything.
    rq = net::Request(); rq.call = "write"; rq.by = by::seat("x");
    rq.body = write_frame_body("pc", "p1", "o-1", 1, 500);
    rs = net::Response();
    check(call(rq, &rs) && rs.ok && rs.pos > 0 && rs.h.size() == 64, "proto.write_round_trip",
          rs.refusal + rs.fault);

    // And a retry over the wire returns the same position: idempotency is a property of the store,
    // not of the client's memory.
    const uint64_t first_pos = rs.pos;
    rs = net::Response();
    check(call(rq, &rs) && rs.pos == first_pos, "proto.retry_over_the_wire_is_the_same_position");

    // A declared length above the cap is `too_large`: the header is read, the body never is, and no
    // allocation happens on a stranger's say-so. The server drops the connection.
    const unsigned char big[4] = { 0x7f, 0xff, 0xff, 0xff };            // ~2 GiB declared
    char hdr[4];
    for (int k = 0; k < 4; ++k) hdr[k] = (char)big[k];
    check(c.send_all(hdr, 4), "proto.sent_an_oversized_header");
    std::string resp; bool tl = false;
    check(!c.recv_frame(&resp, &tl), "proto.too_large_drops_the_connection");
    c.close_();

    // The server is still up, and its health call reports the fault it counted.
    net::Conn c2;
    check(net::connect_to(port, &c2, &err), "proto.reconnect", err);
    net::Request hq; hq.call = "health";
    check(c2.send_frame(net::enc_request(hq)), "proto.health2.send");
    std::string h2; bool tl2 = false;
    net::Response hr;
    check(c2.recv_frame(&h2, &tl2) && net::dec_response(h2, &hr) && hr.ok, "proto.health2");
    check(hr.info.find("too_large=1") != std::string::npos, "proto.too_large_was_counted", hr.info);
    check(hr.info.find("malformed=2") != std::string::npos, "proto.malformed_was_counted", hr.info);
    c2.close_();

    ch.kill_hard();
    ch.wait_and_close();

    VerifyReport rep; verify_dir(dir, &rep);
    check(rep.ok(), "proto.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// =====================================================================================================
// The kill loop
// =====================================================================================================
struct KillStats {
    int kills = 0, boots = 0;
    int died_from_fault = 0, killed_by_parent = 0;
    uint64_t acked = 0, acked_lost = 0, acked_mismatch = 0;
    uint64_t unacked_sent = 0, unacked_survivors = 0, survivors_deduped = 0;
    uint64_t retry_dupes = 0, chain_breaks = 0;
    uint64_t torn_tails = 0, torn_left_after_recovery = 0;
};

static void kill_loop(int iterations, KillStats* st) {
    const char* faults[] = { "torn_write", "die_before_sync", "die_after_sync", "" };
    unsigned seed = 20260908u;
    auto rnd = [&](unsigned hi) { seed = seed * 1664525u + 1013904223u; return (seed >> 16) % hi; };

    for (int it = 1; it <= iterations; ++it) {
        const std::string dir = path_join(g_root, "k" + std::to_string(it));
        make_dir(dir);
        const std::string portfile = path_join(dir, "port.txt");

        // Arm a fault somewhere in the middle of the run, or none at all, and kill by hand instead.
        const char* what = faults[rnd(4)];
        const unsigned at = 3 + rnd(40);
        std::string fault;
        if (*what) {
            fault = std::string(what) + ":" + std::to_string(at);
            if (std::string(what) == "torn_write") fault += ":" + std::to_string(20 + rnd(200));
        }

        Child ch; uint16_t port = 0;
        if (!spawn_daemon(dir, portfile, fault, &ch, &port)) { ch.wait_and_close(); continue; }
        ++st->boots;

        net::Conn c; std::string err;
        if (!net::connect_to(port, &c, &err)) { ch.kill_hard(); ch.wait_and_close(); continue; }

        std::map<std::string, Acked> acked;
        std::vector<std::string> sent_unacked;
        const int nwrites = 20 + (int)rnd(60);
        bool server_gone = false;
        for (int i = 0; i < nwrites && !server_gone; ++i) {
            const std::string rid = "r" + std::to_string(i);
            net::Request rq;
            rq.call = "write"; rq.by = by::seat("client");
            rq.body = write_frame_body("c1", rid, "o-" + std::to_string(i % 17), 1 + (uint64_t)i, 100 + i);
            if (!c.send_frame(net::enc_request(rq))) { server_gone = true; sent_unacked.push_back(rid); break; }
            std::string resp; bool too_large = false;
            if (!c.recv_frame(&resp, &too_large)) { server_gone = true; sent_unacked.push_back(rid); break; }
            net::Response rs;
            if (!net::dec_response(resp, &rs)) { server_gone = true; sent_unacked.push_back(rid); break; }
            if (rs.ok) { acked[rid] = Acked{rs.pos, rs.h}; ++st->acked; }
        }
        st->unacked_sent += sent_unacked.size();
        if (server_gone) ++st->died_from_fault; else { ch.kill_hard(); ++st->killed_by_parent; }
        ++st->kills;
        c.close_();
        ch.wait_and_close();

        // What the crash left behind, judged against what the client was promised.
        const TapeIndex ix = index_tape(dir);
        st->chain_breaks += ix.breaks;
        if (ix.torn_tail) ++st->torn_tails;   // the injector landed inside a write: the rare case
        for (const auto& kv : acked) {
            auto f = ix.pos_to_h.find(kv.second.pos);
            if (f == ix.pos_to_h.end()) ++st->acked_lost;
            else if (f->second != kv.second.h) ++st->acked_mismatch;
        }
        for (const std::string& rid : sent_unacked) {
            if (ix.request_to_pos.count("c1\x1f" + rid)) ++st->unacked_survivors;
        }

        // Restart clean, with no fault armed, and retry everything the client is unsure about.
        Child ch2; uint16_t port2 = 0;
        if (!spawn_daemon(dir, portfile, "", &ch2, &port2)) { ch2.wait_and_close(); continue; }
        ++st->boots;
        net::Conn c2;
        if (!net::connect_to(port2, &c2, &err)) { ch2.kill_hard(); ch2.wait_and_close(); continue; }

        auto retry = [&](const std::string& rid, uint64_t* out_pos, bool* out_ok) {
            net::Request rq;
            rq.call = "write"; rq.by = by::seat("client");
            const int i = std::atoi(rid.c_str() + 1);
            rq.body = write_frame_body("c1", rid, "o-" + std::to_string(i % 17), 1 + (uint64_t)i, 100 + i);
            if (!c2.send_frame(net::enc_request(rq))) return false;
            std::string resp; bool tl = false;
            if (!c2.recv_frame(&resp, &tl)) return false;
            net::Response rs;
            if (!net::dec_response(resp, &rs)) return false;
            *out_pos = rs.pos; *out_ok = rs.ok;
            return true;
        };

        // An acknowledged write, retried after the crash, must come back at the SAME position — the
        // reply cache rebuilt from the tape, not a second commit.
        int checked = 0;
        for (auto itr = acked.rbegin(); itr != acked.rend() && checked < 5; ++itr, ++checked) {
            uint64_t pos = 0; bool ok = false;
            if (!retry(itr->first, &pos, &ok)) break;
            if (pos != itr->second.pos) ++st->retry_dupes;
        }
        // An UNACKNOWLEDGED write that happens to have survived must also come back at its original
        // position: the client never learns whether it landed, and must not be able to double it.
        for (const std::string& rid : sent_unacked) {
            const auto f = ix.request_to_pos.find("c1\x1f" + rid);
            if (f == ix.request_to_pos.end()) continue;
            uint64_t pos = 0; bool ok = false;
            if (!retry(rid, &pos, &ok)) break;
            if (pos == f->second) ++st->survivors_deduped; else ++st->retry_dupes;
        }

        c2.close_();
        ch2.kill_hard();
        ch2.wait_and_close();

        // After a clean reopen the tape must be whole: the torn bytes truncated, the warn row written,
        // and nothing the client was promised missing.
        const TapeIndex ix2 = index_tape(dir);
        st->chain_breaks += ix2.breaks;
        st->torn_left_after_recovery += (ix2.torn_tail ? 1 : 0);
        for (const auto& kv : acked) {
            auto f = ix2.pos_to_h.find(kv.second.pos);
            if (f == ix2.pos_to_h.end()) ++st->acked_lost;
            else if (f->second != kv.second.h) ++st->acked_mismatch;
        }

        // Tapes are large; keep the last few for inspection and drop the rest.
        if (it > 3) {
#if defined(_WIN32)
            WIN32_FIND_DATAA fd; HANDLE hf = FindFirstFileA(path_join(dir, "*").c_str(), &fd);
            if (hf != INVALID_HANDLE_VALUE) {
                do { if (fd.cFileName[0] != '.') DeleteFileA(path_join(dir, fd.cFileName).c_str()); }
                while (FindNextFileA(hf, &fd));
                FindClose(hf);
            }
            RemoveDirectoryA(dir.c_str());
#endif
        }
        if (it % 25 == 0) { std::printf("   ... %d kills\n", it); std::fflush(stdout); }
    }
}

int main(int argc, char** argv) {
    int iterations = 100;
    g_exe  = "C:/TAPESTRY/bin/tapestryd.exe";
    g_root = "C:/TAPESTRY/scratch/t_kill";
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if      (k == "--n"    && i + 1 < argc) iterations = std::atoi(argv[++i]);
        else if (k == "--exe"  && i + 1 < argc) g_exe = argv[++i];
        else if (k == "--root" && i + 1 < argc) g_root = argv[++i];
    }
    make_dir("C:/TAPESTRY/scratch");
    make_dir(g_root);
    std::printf("== TAPESTRY R0.3 durability gate ==  kills=%d root=%s\n", iterations, g_root.c_str());

    t_redelivered_suffix();
    t_protocol();

    KillStats st;
    kill_loop(iterations, &st);

    std::printf("\n-- after %d kills (%d boots) --\n", st.kills, st.boots);
    std::printf("   died from an injected fault: %d   killed by the parent: %d\n",
                st.died_from_fault, st.killed_by_parent);
    std::printf("   acknowledged writes: %llu\n", (unsigned long long)st.acked);
    std::printf("   acknowledged and LOST: %llu    acknowledged and CHANGED: %llu\n",
                (unsigned long long)st.acked_lost, (unsigned long long)st.acked_mismatch);
    std::printf("   sent without an answer: %llu   of those, survived on the tape: %llu\n",
                (unsigned long long)st.unacked_sent, (unsigned long long)st.unacked_survivors);
    std::printf("   survivors deduplicated on retry: %llu   retries that made a SECOND entry: %llu\n",
                (unsigned long long)st.survivors_deduped, (unsigned long long)st.retry_dupes);
    std::printf("   killed INSIDE a write, leaving a torn row: %llu   still torn after recovery: %llu\n",
                (unsigned long long)st.torn_tails, (unsigned long long)st.torn_left_after_recovery);
    std::printf("   chain breaks across every reopened tape: %llu\n\n", (unsigned long long)st.chain_breaks);

    check(st.kills == iterations, "kill.ran", std::to_string(st.kills));
    check(st.acked > 0, "kill.wrote_something", std::to_string(st.acked));
    check(st.acked_lost == 0, "kill.no_acknowledged_entry_is_lost", std::to_string(st.acked_lost));
    check(st.acked_mismatch == 0, "kill.no_acknowledged_entry_changed", std::to_string(st.acked_mismatch));
    check(st.chain_breaks == 0, "kill.every_recovered_tape_verifies", std::to_string(st.chain_breaks));
    check(st.retry_dupes == 0, "kill.a_retried_write_applies_once", std::to_string(st.retry_dupes));
    check(st.survivors_deduped == st.unacked_survivors, "kill.every_survivor_is_deduplicated",
          std::to_string(st.survivors_deduped) + " of " + std::to_string(st.unacked_survivors));
    check(st.died_from_fault > 0, "kill.the_injector_actually_fired", std::to_string(st.died_from_fault));
    check(st.torn_tails > 0, "kill.a_kill_landed_inside_a_write", std::to_string(st.torn_tails));
    check(st.torn_left_after_recovery == 0, "kill.recovery_truncated_every_torn_row",
          std::to_string(st.torn_left_after_recovery));

    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
