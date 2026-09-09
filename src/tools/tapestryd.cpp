// =====================================================================================================
// TAPESTRY · src/tools/tapestryd.cpp · the transactor as a process
//
//   tapestryd --dir D [--port P] [--seg-bytes B] [--cap N] [--check "EXPR"] [--portfile F]
//
// One writer, one socket, one connection served at a time — because law 3 is not a threading detail.
// The environment variable TAPESTRY_FAULT arms the injector (see core/fileio.h), so a parent process
// can tell this one exactly where to die: mid-write, before a flush, or just after one returns with
// the acknowledgement still in flight. That last case is the interesting one, and it is the reason
// this exists as a separate process at all: an acknowledgement only means something if the thing that
// records it can outlive the thing that sends it.
// =====================================================================================================

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include "../core/fileio.h"
#include "../tape/tape.h"
#include "../writ/classmap.h"
#include "../tx/transactor.h"
#include "../net/frames.h"
#include "../net/protocol.h"

using namespace tapestry;

int main(int argc, char** argv) {
    std::string dir, check_src, portfile;
    uint16_t port = 0;
    uint64_t seg_bytes = 64ull << 20;
    int64_t  cap = -1;

    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if      (k == "--dir"       && i + 1 < argc) dir = argv[++i];
        else if (k == "--port"      && i + 1 < argc) port = (uint16_t)std::strtoul(argv[++i], nullptr, 10);
        else if (k == "--seg-bytes" && i + 1 < argc) seg_bytes = std::strtoull(argv[++i], nullptr, 10);
        else if (k == "--cap"       && i + 1 < argc) cap = (int64_t)std::strtoll(argv[++i], nullptr, 10);
        else if (k == "--check"     && i + 1 < argc) check_src = argv[++i];
        else if (k == "--portfile"  && i + 1 < argc) portfile = argv[++i];
        else { std::fprintf(stderr, "unknown argument: %s\n", k.c_str()); return 2; }
    }
    if (dir.empty()) { std::fprintf(stderr, "--dir is required\n"); return 2; }

    faults_from_env();

    SystemClock clk;
    Tape tape;
    TapeConfig tc; tc.dir = dir; tc.segment_bytes = seg_bytes;
    std::string err;
    if (!tape.open(tc, &clk, &err)) { std::fprintf(stderr, "tape: %s\n", err.c_str()); return 3; }

    writ::ClassMap map;
    writ::ClassDef d;
    d.cls = 1; d.name = "orders"; d.cap_minor_per_period = cap;
    writ::RevRule rr; rr.table = "*"; rr.op = "*"; rr.rev = writ::Rev::ReversibleByInverse;
    d.rev.push_back(rr);
    std::vector<std::pair<std::string, std::string>> checks;
    if (!check_src.empty()) checks.push_back({"c1", check_src});
    std::string why;
    if (!map.add(d, checks, &why)) { std::fprintf(stderr, "classmap: %s\n", why.c_str()); return 3; }

    Transactor tx;
    TxConfig cfg; cfg.seam_code_hash = "abc123";
    if (!tx.open(&tape, &map, cfg, dir, &err)) { std::fprintf(stderr, "transactor: %s\n", err.c_str()); return 3; }

    net::Listener ln;
    if (!ln.listen_on(port, &err)) { std::fprintf(stderr, "listen: %s\n", err.c_str()); return 3; }

    // The port goes to a file, fsynced, before anything is served: a parent that spawned us with
    // --port 0 must be able to find us without racing, and without parsing our stdout.
    if (!portfile.empty()) {
        if (!write_atomic(portfile, std::to_string((unsigned)ln.port) + "\n", &err)) {
            std::fprintf(stderr, "portfile: %s\n", err.c_str()); return 3;
        }
    }
    std::printf("tapestryd dir=%s port=%u committed_pos=%llu head=%s\n",
                dir.c_str(), (unsigned)ln.port,
                (unsigned long long)tape.committed_pos(), tape.head().c_str());
    std::fflush(stdout);

    uint64_t too_large_faults = 0, malformed_faults = 0;
    while (true) {
        net::Conn c;
        if (!ln.accept_one(&c)) break;
        while (true) {
            std::string frame; bool too_large = false;
            if (!c.recv_frame(&frame, &too_large)) {
                if (too_large) ++too_large_faults;      // counted, and the connection is dropped
                break;
            }
            net::Request req;
            net::Response res;
            if (!net::dec_request(frame, &req)) {
                ++malformed_faults;
                res.fault = fault::MALFORMED;
                c.send_frame(net::enc_response(res));
                continue;
            }
            if (req.call == "write") {
                WriteReq w;
                std::string rev;
                if (!read_tx_body(req.body, &w.client_id, &w.request_id, &w.basis_pos, &rev, &w.facts)) {
                    ++malformed_faults;
                    res.fault = fault::MALFORMED;
                } else {
                    w.by = req.by;
                    w.declared_reversibility = (rev == "n/a") ? std::string() : rev;
                    const WriteRes r = tx.write(w);
                    res.ok = r.committed; res.pos = r.pos; res.h = r.h;
                    res.refusal = r.refusal; res.constraint_id = r.constraint_id; res.fault = r.fault;
                }
            } else if (req.call == "tick") {
                std::string e2;
                res.ok = tx.tick(req.arg.empty() ? "cadence" : req.arg, &e2);
                if (!res.ok) res.fault = e2;
                res.pos = tape.committed_pos();
            } else if (req.call == "health") {
                const Health& h = tx.health();
                char b[512];
                std::snprintf(b, sizeof(b),
                    "committed_pos=%llu applied_pos=%llu writes_committed=%llu writes_refused=%llu "
                    "refusal_entries=%llu cells=%llu too_large=%llu malformed=%llu",
                    (unsigned long long)h.committed_pos, (unsigned long long)tx.applied_pos(),
                    (unsigned long long)h.writes_committed, (unsigned long long)h.writes_refused,
                    (unsigned long long)h.refusal_entries, (unsigned long long)h.cells,
                    (unsigned long long)too_large_faults, (unsigned long long)malformed_faults);
                res.ok = true; res.info = b; res.pos = h.committed_pos;
            } else if (req.call == "head") {
                res.ok = true; res.pos = tape.committed_pos(); res.h = tape.head();
            } else {
                res.fault = fault::MALFORMED;
                ++malformed_faults;
            }
            if (!c.send_frame(net::enc_response(res))) break;
        }
        c.close_();
    }
    std::string e3;
    tx.flush_refusals(&e3);
    tape.close();
    return 0;
}
