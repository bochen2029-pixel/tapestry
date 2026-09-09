// =====================================================================================================
// TAPESTRY · src/tests/t_tape.cpp · R0.1 module oracles, EACH WITH ITS LIE ARM
//
// QC-7 F8/F9/F13: twelve of the estate's fifteen oracles had no lie arm, and an oracle that cannot
// fail is a decoration. So every oracle here has two arms:
//     the truth arm — the property must HOLD on correct input;
//     the lie  arm  — the same check must FAIL on input carrying a planted defect.
// A run is green only when both arms report as expected, so a check that has silently become a
// tautology turns the suite red rather than staying quiet.
//
// The lies planted here are the ones the estate actually shipped:
//   · fusord's `jesc` deletes '\r' and passes invalid UTF-8            (blueprint §3.1: "it is replaced")
//   · fusord's `Tape::open` restarts a rolled segment at genesis       (QC-1 F6)
//   · fusord's `Tape::open` newline-terminates a torn row              (§4.2: it is truncated)
//   · the estate's verifier treats a fork as cleaner than a seam       (QC-1 F6, probe3.py)
// =====================================================================================================

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <limits>
#include "../core/blake2b.h"
#include "../core/bytes.h"
#include "../core/json.h"
#include "../core/fileio.h"
#include "../tape/entry.h"
#include "../tape/tape.h"

using namespace tapestry;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}

// A scratch directory per oracle, wiped first so a run is a cold run.
static std::string g_root;
static std::string fresh_dir(const char* name) {
    const std::string d = path_join(g_root, name);
#if defined(_WIN32)
    // Remove any prior contents; the tape refuses to reuse a segment file, and that is correct.
    const std::string pat = path_join(d, "*");
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA(pat.c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do {
            if (fd.cFileName[0] == '.') continue;
            DeleteFileA(path_join(d, fd.cFileName).c_str());
        } while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }
#endif
    make_dir(d);
    return d;
}

// The escaper the blueprint retires, reproduced verbatim from fusord.cpp:177-186, so the lie arm is
// the estate's real behaviour and not a strawman.
static std::string jesc_fusord(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\";
        else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t"; else if (c == '\r') {}
        else if ((unsigned char)c >= 0x20 || c < 0) o += c;
    }
    return o;
}

// =====================================================================================================
// 1 · BLAKE2b-256 against RFC 7693's vectors
// =====================================================================================================
static void t_blake2b() {
    uint8_t out[32];
    blake2b256_raw("", 0, out);
    const std::string empty = hex_of(out, 32);
    blake2b256_raw("abc", 3, out);
    const std::string abc = hex_of(out, 32);
    check(empty == "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8",
          "blake2b256.empty", empty);
    check(abc == "bddd813c634239723171ef3fee98579b94964e3bb1cb3e427262c8c068d52319",
          "blake2b256.abc", abc);
    // Lie arm: one flipped bit must move the digest.
    blake2b256_raw("abd", 3, out);
    check(hex_of(out, 32) != abc, "blake2b256.abc.lie");
    // Streaming in two pieces must equal the one-shot (the chain feeds prev then body separately).
    Blake2b b; b.init(32); b.update((const uint8_t*)"a", 1); b.update((const uint8_t*)"bc", 2);
    uint8_t o2[32]; b.finish(o2);
    check(hex_of(o2, 32) == abc, "blake2b256.streaming");
}

// =====================================================================================================
// 2 · The chain hash is the estate's: h = blake2b256(prev_hex ‖ literal prefix)
// =====================================================================================================
static void t_chain() {
    const std::string prefix = "{\"pos\":0,\"k\":\"tick\"";
    const std::string h = chain_hash(GENESIS, prefix);
    Blake2b b; b.init(32);
    b.update((const uint8_t*)GENESIS, 64);
    b.update((const uint8_t*)prefix.data(), prefix.size());
    uint8_t out[32]; b.finish(out);
    check(h == hex_of(out, 32), "chain.definition");
    // Lie arm: hashing the prefix without the prev (a chain that does not chain) must differ.
    Blake2b c; c.init(32); c.update((const uint8_t*)prefix.data(), prefix.size());
    uint8_t o2[32]; c.finish(o2);
    check(h != hex_of(o2, 32), "chain.definition.lie");
}

// =====================================================================================================
// 3 · The writer is lossless: every byte a world system can hand us survives the round trip
// =====================================================================================================
static void t_lossless() {
    std::vector<std::string> nasty;
    nasty.push_back(std::string("carriage\rreturn"));
    nasty.push_back(std::string("nul\0inside", 10));
    nasty.push_back(std::string("del\x7f end"));
    nasty.push_back(std::string("quote\" backslash\\ slash/"));
    nasty.push_back(std::string("tab\tnl\nvt\x0b bell\x07"));
    nasty.push_back(std::string(",\"prev\":\"deadbeef"));          // the verifier's own marker, as data
    nasty.push_back(std::string("\xE2\x9C\x93 unicode check"));
    for (int c = 0; c < 32; ++c) nasty.push_back(std::string("c0-") + (char)c + "-end");

    bool all_ok = true; std::string first_bad;
    for (const std::string& s : nasty) {
        json::Value body = json::Value::object();
        body.set("v", json::Value::str(s));
        EntryHdr hd; hd.pos = 7; hd.k = kind::TX; hd.by = by::SYSTEM; hd.t_epoch_ns = 1;
        BuiltRow br; std::string why;
        if (!build_row(hd, body, json::WriteOpts(), GENESIS, &br, &why)) { all_ok = false; first_bad = "build: " + why; break; }
        ScannedRow r;
        if (!scan_row(br.line.substr(0, br.line.size() - 1), &r, &why, true)) { all_ok = false; first_bad = "scan: " + why; break; }
        // Pull the one string back out of the body text and compare byte for byte.
        size_t i = 0; std::string back;
        if (!detail::eat_lit(r.body, i, "{\"v\":") || !detail::eat_string(r.body, i, back)) {
            all_ok = false; first_bad = "unescape failed for " + b64_encode(s); break;
        }
        if (back != s) { all_ok = false; first_bad = "round trip differs: " + b64_encode(s) + " -> " + b64_encode(back); break; }
    }
    check(all_ok, "writer.lossless", first_bad);

    // Lie arm: the retired escaper drops the carriage return, so the round trip fails.
    const std::string cr = "carriage\rreturn";
    check(jesc_fusord(cr).find('\r') == std::string::npos && jesc_fusord(cr) != cr,
          "writer.lossless.lie.fusord_jesc_drops_cr");

    // A row is one line: nothing the writer emits may contain a raw newline.
    json::Value body = json::Value::object();
    body.set("v", json::Value::str("a\nb\rc"));
    EntryHdr hd; hd.pos = 1; hd.k = kind::TX; hd.by = by::SYSTEM; hd.t_epoch_ns = 1;
    BuiltRow br; std::string why;
    const bool built = build_row(hd, body, json::WriteOpts(), GENESIS, &br, &why);
    check(built && br.prefix.find('\n') == std::string::npos && br.prefix.find('\r') == std::string::npos,
          "writer.one_line");
}

// =====================================================================================================
// 4 · UTF-8 at the boundary: validated, and invalid bytes carried under a type tag, never dropped
// =====================================================================================================
static void t_utf8() {
    struct C { const char* bytes; size_t n; bool valid; const char* what; };
    const std::string overlong("\xC0\xAF", 2);           // overlong '/'
    const std::string surrogate("\xED\xA0\x80", 3);      // U+D800
    const std::string too_big("\xF5\x80\x80\x80", 4);    // > U+10FFFF
    const std::string truncated("\xE2\x9C", 2);          // cut short
    const std::string stray("\x80", 1);                  // stray continuation
    const std::string good("\xF0\x9F\x9A\x80", 4);       // U+1F680

    check(!utf8_valid(overlong),  "utf8.overlong_refused");
    check(!utf8_valid(surrogate), "utf8.surrogate_refused");
    check(!utf8_valid(too_big),   "utf8.above_10FFFF_refused");
    check(!utf8_valid(truncated), "utf8.truncated_refused");
    check(!utf8_valid(stray),     "utf8.stray_continuation_refused");
    check(utf8_valid(good),       "utf8.astral_accepted");

    // Lie arm: the permissive check the estate shipped (>= 0x20 or negative) accepts all of them.
    auto jesc_would_pass = [](const std::string& s) {
        for (char c : s) if (!((unsigned char)c >= 0x20 || c < 0)) return false;
        return true;
    };
    check(jesc_would_pass(overlong) && jesc_would_pass(surrogate) && jesc_would_pass(too_big),
          "utf8.lie.fusord_passes_invalid");

    // Carried, not dropped: an invalid value becomes {"$b64":"..."} and decodes to the same bytes.
    json::Value body = json::Value::object();
    body.set("v", json::Value::str(overlong));
    EntryHdr hd; hd.pos = 2; hd.k = kind::TX; hd.by = by::SYSTEM; hd.t_epoch_ns = 1;
    BuiltRow br; std::string why;
    const bool built = build_row(hd, body, json::WriteOpts(), GENESIS, &br, &why);
    bool carried = false;
    if (built) {
        const size_t at = br.prefix.find("\"body\":");
        if (at != std::string::npos) {
            const std::string bodytext = br.prefix.substr(at + 7);
            size_t j = 0; std::string b64, dec;
            if (detail::eat_lit(bodytext, j, "{\"v\":{\"$b64\":") && detail::eat_string(bodytext, j, b64)
                && b64_decode(b64, dec) && dec == overlong) carried = true;
        }
    }
    check(carried, "utf8.invalid_carried_as_base64", why);

    // And the strict policy refuses instead, with a typed reason.
    json::WriteOpts strict; strict.utf8 = json::WriteOpts::RefuseInvalid;
    std::string why2;
    const bool refused = !build_row(hd, body, strict, GENESIS, &br, &why2);
    check(refused && why2 == json::R_INVALID_UTF8, "utf8.strict_policy_refuses", why2);
}

// =====================================================================================================
// 5 · base64 round trip
// =====================================================================================================
static void t_base64() {
    bool ok = true;
    for (size_t len = 0; len <= 8; ++len) {
        std::string s;
        for (size_t i = 0; i < len; ++i) s += (char)(i * 37 + 11);
        std::string dec;
        if (!b64_decode(b64_encode(s), dec) || dec != s) { ok = false; break; }
    }
    std::string all;
    for (int i = 0; i < 256; ++i) all += (char)i;
    std::string dec;
    ok = ok && b64_decode(b64_encode(all), dec) && dec == all;
    check(ok, "base64.round_trip");
    // Lie arm: a truncated or mis-padded encoding must not decode.
    check(!b64_decode("AAA", dec) && !b64_decode("A=AA", dec), "base64.round_trip.lie");
}

// =====================================================================================================
// 6 · Wire order is asserted literally, not parsed permissively
// =====================================================================================================
static void t_wire_order() {
    EntryHdr hd; hd.pos = 42; hd.term = 0; hd.t_epoch_ns = 1757000000000000000ull;
    hd.k = kind::TICK; hd.cell = 9; hd.cls = 3; hd.by = by::SYSTEM;
    json::Value body = json::Value::object();
    body.set("cadence_rule", json::Value::str("r1"));
    BuiltRow br; std::string why;
    check(build_row(hd, body, json::WriteOpts(), GENESIS, &br, &why), "wire.build", why);

    const std::string want_prefix =
        "{\"pos\":42,\"term\":0,\"t_epoch_ns\":1757000000000000000,\"k\":\"tick\","
        "\"cell\":9,\"cls\":3,\"by\":\"system\",\"body\":{\"cadence_rule\":\"r1\"}";
    check(br.prefix == want_prefix, "wire.exact_bytes", br.prefix);
    check(br.line == want_prefix + ",\"prev\":\"" + GENESIS + "\",\"h\":\"" + br.h + "\"}\n", "wire.exact_row");

    ScannedRow r;
    check(scan_row(br.line.substr(0, br.line.size() - 1), &r, &why, true), "wire.scan", why);
    check(r.hdr.pos == 42 && r.hdr.cell == 9 && r.hdr.cls == 3 && r.hdr.k == "tick" && r.hdr.by == "system",
          "wire.scan_fields");

    // Lie arm 1: fields out of order (cell before k) must be refused, though a JSON parser accepts it.
    std::string swapped =
        "{\"pos\":42,\"term\":0,\"t_epoch_ns\":1,\"cell\":9,\"k\":\"tick\",\"cls\":3,\"by\":\"system\",\"body\":{}";
    const std::string sh = chain_hash(GENESIS, swapped);
    const std::string swapped_row = swapped + ",\"prev\":\"" + GENESIS + "\",\"h\":\"" + sh + "\"}";
    check(!scan_row(swapped_row, &r, &why, true), "wire.order.lie", why);

    // Lie arm 2: one flipped byte inside the prefix must break the hash.
    std::string tampered = br.line.substr(0, br.line.size() - 1);
    const size_t at = tampered.find("\"r1\"");
    tampered[at + 2] = '2';
    check(!scan_row(tampered, &r, &why, true) && why == "hash_mismatch", "wire.tamper.lie", why);

    // A body carrying the verifier's own marker as data must still scan (rfind takes the last one).
    json::Value tricky = json::Value::object();
    tricky.set("a", json::Value::u64(1));
    tricky.set("prev", json::Value::str(GENESIS));
    BuiltRow br2;
    check(build_row(hd, tricky, json::WriteOpts(), GENESIS, &br2, &why), "wire.marker_in_body.build", why);
    check(scan_row(br2.line.substr(0, br2.line.size() - 1), &r, &why, true), "wire.marker_in_body.scan", why);
}

// =====================================================================================================
// 7 · Positions, the epoch stamp, and what publication means
// =====================================================================================================
static void t_positions() {
    const std::string d = fresh_dir("positions");
    FixedClock clk(1000, 10);
    TapeConfig cfg; cfg.dir = d;
    Tape tape; std::string err;
    check(tape.open(cfg, &clk, &err), "pos.open", err);
    check(tape.committed_pos() == 0 && tape.has_committed(), "pos.genesis_seg_committed");

    // Stage three entries; nothing is published until commit returns.
    for (int i = 0; i < 3; ++i) {
        json::Value b = json::Value::object();
        b.set("i", json::Value::u64((uint64_t)i));
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why;
        check(tape.stage(hd, b, json::WriteOpts(), &st, &why), "pos.stage", why);
        check(st.pos == (uint64_t)(i + 1), "pos.assigned_in_order");
    }
    check(tape.committed_pos() == 0, "pos.not_published_before_commit");
    check(tape.commit(&err), "pos.commit", err);
    check(tape.committed_pos() == 3, "pos.published_after_commit");
    tape.close();

    VerifyReport rep; verify_dir(d, &rep);
    check(rep.ok() && rep.rows == 4 && rep.first_pos == 0 && rep.last_pos == 3,
          "pos.verify", rep.problems.empty() ? "" : rep.problems[0]);

    // Reopen: positions continue, they are never reused.
    Tape t2;
    check(t2.open(cfg, &clk, &err), "pos.reopen", err);
    check(t2.next_pos() == 4, "pos.continue_after_reopen");
    json::Value b = json::Value::object(); b.set("i", json::Value::u64(99));
    EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
    Staged st; std::string why;
    check(t2.stage(hd, b, json::WriteOpts(), &st, &why) && st.pos == 4, "pos.reopen_next_pos", why);
    check(t2.commit(&err), "pos.reopen_commit", err);
    t2.close();
    VerifyReport rep2; verify_dir(d, &rep2);
    check(rep2.ok() && rep2.rows == 5, "pos.verify_after_reopen", rep2.problems.empty() ? "" : rep2.problems[0]);

    // Lie arm: a duplicated position on the tape must be reported.
    const std::string p = path_join(d, seg_name(0));
    std::string data; read_whole(p, data);
    const size_t nl = data.find('\n');
    const std::string dup = data + data.substr(0, nl + 1);   // the genesis seg row, appended twice
    const std::string dl = fresh_dir("positions_lie");
    { File f; std::string e2; f.open_append(path_join(dl, seg_name(0)), &e2); f.truncate_to(0, &e2); f.write_all(dup, &e2); f.sync(&e2); }
    VerifyReport rep3; verify_dir(dl, &rep3);
    check(!rep3.ok(), "pos.duplicate.lie");
}

// =====================================================================================================
// 8 · The epoch stamp is monotone even when the wall clock is not
// =====================================================================================================
struct BackwardsClock : Clock {
    uint64_t v = 5000;
    uint64_t now_ns() override { const uint64_t x = v; if (v > 100) v -= 100; return x; }
};
static void t_epoch_monotone() {
    const std::string d = fresh_dir("epoch");
    BackwardsClock clk;
    TapeConfig cfg; cfg.dir = d;
    Tape tape; std::string err;
    check(tape.open(cfg, &clk, &err), "epoch.open", err);
    for (int i = 0; i < 5; ++i) {
        json::Value b = json::Value::object(); b.set("i", json::Value::u64((uint64_t)i));
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why;
        tape.stage(hd, b, json::WriteOpts(), &st, &why);
    }
    check(tape.commit(&err), "epoch.commit", err);
    tape.close();
    VerifyReport rep; verify_dir(d, &rep);
    check(rep.ok(), "epoch.monotone_on_a_backwards_clock", rep.problems.empty() ? "" : rep.problems[0]);

    // Lie arm: a tape whose stamps go backwards must be caught by the same check.
    const std::string dl = fresh_dir("epoch_lie");
    std::string prev = GENESIS, out;
    uint64_t stamps[3] = {900, 800, 700};
    for (int i = 0; i < 3; ++i) {
        EntryHdr hd; hd.pos = (uint64_t)i; hd.k = (i == 0 ? kind::SEG : kind::TICK); hd.by = by::SYSTEM;
        hd.t_epoch_ns = stamps[i];
        json::Value b = (i == 0) ? seg_body(0, GENESIS) : json::Value::object();
        BuiltRow br; std::string why;
        build_row(hd, b, json::WriteOpts(), prev, &br, &why);
        out += br.line; prev = br.h;
    }
    { File f; std::string e2; f.open_append(path_join(dl, seg_name(0)), &e2); f.truncate_to(0, &e2); f.write_all(out, &e2); f.sync(&e2); }
    VerifyReport rl; verify_dir(dl, &rl);
    check(!rl.ok(), "epoch.monotone.lie");
}

// =====================================================================================================
// 9 · The chain across a roll (falsifier 22) — and the fork the estate's verifier calls clean
// =====================================================================================================
static void t_roll() {
    const std::string d = fresh_dir("roll");
    FixedClock clk;
    TapeConfig cfg; cfg.dir = d; cfg.segment_bytes = 2048;   // roll early, on purpose
    Tape tape; std::string err;
    check(tape.open(cfg, &clk, &err), "roll.open", err);
    for (int i = 0; i < 60; ++i) {
        json::Value b = json::Value::object();
        b.set("i", json::Value::u64((uint64_t)i));
        b.set("pad", json::Value::str(std::string(64, 'x')));
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why;
        if (!tape.stage(hd, b, json::WriteOpts(), &st, &why)) { check(false, "roll.stage", why); break; }
        if (i % 7 == 0) tape.commit(&err);
    }
    check(tape.commit(&err), "roll.commit", err);
    const uint64_t segs_now = list_segments(d).size();
    tape.close();
    check(segs_now >= 3, "roll.rolled", std::to_string(segs_now) + " segments");

    VerifyReport rep; verify_dir(d, &rep);
    check(rep.ok() && rep.rows == 61 + segs_now - 1, "roll.zero_seams",
          rep.problems.empty() ? ("rows=" + std::to_string(rep.rows)) : rep.problems[0]);

    // Reopen after a roll and keep appending: the head must come from the LAST segment.
    Tape t2;
    check(t2.open(cfg, &clk, &err), "roll.reopen", err);
    json::Value b = json::Value::object(); b.set("i", json::Value::u64(1000));
    EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
    Staged st; std::string why;
    check(t2.stage(hd, b, json::WriteOpts(), &st, &why), "roll.reopen_stage", why);
    check(t2.commit(&err), "roll.reopen_commit", err);
    t2.close();
    VerifyReport rep2; verify_dir(d, &rep2);
    check(rep2.ok(), "roll.zero_seams_after_reopen", rep2.problems.empty() ? "" : rep2.problems[0]);

    // Lie arm: the failure fusord shipped — a rolled segment that restarts at genesis. The estate's
    // python verifier calls this CLEANER than the correct tape (QC-1 F6, probe3.py); ours must not.
    const std::string dl = fresh_dir("roll_lie");
    {
        std::string prev = GENESIS, out0;
        for (int i = 0; i < 2; ++i) {
            EntryHdr hd2; hd2.pos = (uint64_t)i; hd2.k = (i == 0 ? kind::SEG : kind::TICK);
            hd2.by = by::SYSTEM; hd2.t_epoch_ns = 100 + (uint64_t)i;
            json::Value bb = (i == 0) ? seg_body(0, GENESIS) : json::Value::object();
            BuiltRow br; std::string w; build_row(hd2, bb, json::WriteOpts(), prev, &br, &w);
            out0 += br.line; prev = br.h;
        }
        File f; std::string e2; f.open_append(path_join(dl, seg_name(0)), &e2); f.truncate_to(0, &e2);
        f.write_all(out0, &e2); f.sync(&e2);
        // segment 2 forks: its header claims genesis instead of continuing.
        std::string prev2 = GENESIS, out2;
        for (int i = 0; i < 2; ++i) {
            EntryHdr hd2; hd2.pos = (uint64_t)(2 + i); hd2.k = (i == 0 ? kind::SEG : kind::TICK);
            hd2.by = by::SYSTEM; hd2.t_epoch_ns = 200 + (uint64_t)i;
            json::Value bb = (i == 0) ? seg_body(2, GENESIS) : json::Value::object();
            BuiltRow br; std::string w; build_row(hd2, bb, json::WriteOpts(), prev2, &br, &w);
            out2 += br.line; prev2 = br.h;
        }
        File f2; f2.open_append(path_join(dl, seg_name(2)), &e2); f2.truncate_to(0, &e2);
        f2.write_all(out2, &e2); f2.sync(&e2);
    }
    VerifyReport rl; verify_dir(dl, &rl);
    bool named_the_fork = false;
    for (const std::string& p : rl.problems) if (p.find("FORKED") != std::string::npos) named_the_fork = true;
    check(!rl.ok() && named_the_fork, "roll.fork.lie");

    // And the writer refuses to open that tape at all, rather than continuing a forked chain.
    Tape t3; std::string e3; TapeConfig c3; c3.dir = dl;
    check(!t3.open(c3, &clk, &e3), "roll.fork.open_refused", e3);
}

// =====================================================================================================
// 10 · A torn tail is truncated, counted and warned — never newline-terminated
// =====================================================================================================
static void t_torn() {
    const std::string d = fresh_dir("torn");
    FixedClock clk;
    TapeConfig cfg; cfg.dir = d;
    Tape tape; std::string err;
    check(tape.open(cfg, &clk, &err), "torn.open", err);
    for (int i = 0; i < 4; ++i) {
        json::Value b = json::Value::object(); b.set("i", json::Value::u64((uint64_t)i));
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why; tape.stage(hd, b, json::WriteOpts(), &st, &why);
    }
    tape.commit(&err);
    tape.close();

    // The process dies inside a write: half a row, no newline.
    const std::string p = path_join(d, seg_name(0));
    const std::string half = "{\"pos\":5,\"term\":0,\"t_epoch_ns\":9,\"k\":\"tx\",\"cell\":0,\"cls\":0,\"by\":\"sys";
    { File f; std::string e2; f.open_append(p, &e2); f.write_all(half, &e2); f.sync(&e2); }

    Tape t2;
    check(t2.open(cfg, &clk, &err), "torn.reopen", err);
    check(t2.torn_at_open() == half.size(), "torn.counted", std::to_string(t2.torn_at_open()));
    check(t2.next_pos() == 6, "torn.warn_took_a_position");   // 0..4 entries, 5 = the warn row
    t2.close();

    VerifyReport rep; verify_dir(d, &rep);
    check(rep.ok(), "torn.verifies_after_truncation", rep.problems.empty() ? "" : rep.problems[0]);
    std::string data; read_whole(p, data);
    check(data.find(half) == std::string::npos, "torn.bytes_gone");
    check(data.find("torn_row_discarded") != std::string::npos, "torn.warn_entry_written");

    // Lie arm: fusord's repair — terminate the half row with a newline — leaves a row that scans as
    // malformed, which is exactly the failure mode a truncation avoids.
    const std::string dl = fresh_dir("torn_lie");
    { File f; std::string e2; f.open_append(path_join(dl, seg_name(0)), &e2); f.truncate_to(0, &e2);
      f.write_all(data.substr(0, data.find('\n') + 1) + half + "\n", &e2); f.sync(&e2); }
    VerifyReport rl; verify_dir(dl, &rl);
    check(!rl.ok(), "torn.newline_terminated.lie");
}

// =====================================================================================================
// 11 · Bytes with no recoverable head are FATAL, never genesis
// =====================================================================================================
static void t_no_head() {
    const std::string d = fresh_dir("nohead");
    { File f; std::string e2; f.open_append(path_join(d, seg_name(0)), &e2); f.truncate_to(0, &e2);
      f.write_all(std::string(300, 'Z'), &e2); f.sync(&e2); }
    FixedClock clk; TapeConfig cfg; cfg.dir = d;
    Tape tape; std::string err;
    const bool opened = tape.open(cfg, &clk, &err);
    check(!opened, "nohead.fatal", err);
    check(err.find("no recoverable head") != std::string::npos, "nohead.reason_named", err);

    // Lie arm: fusord's open would have silently continued from GENESIS, appending to a forked chain.
    // We assert the file is untouched — nothing was written on top of the unreadable bytes.
    uint64_t sz = 0; file_size_of(path_join(d, seg_name(0)), sz);
    check(sz == 300, "nohead.no_write_on_top.lie", std::to_string(sz));
}

// =====================================================================================================
// 12 · Group commit: unpublished entries do not exist
// =====================================================================================================
static void t_group_commit() {
    const std::string d = fresh_dir("group");
    FixedClock clk; TapeConfig cfg; cfg.dir = d;
    {
        Tape tape; std::string err;
        check(tape.open(cfg, &clk, &err), "group.open", err);
        for (int i = 0; i < 10; ++i) {
            json::Value b = json::Value::object(); b.set("i", json::Value::u64((uint64_t)i));
            EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
            Staged st; std::string why; tape.stage(hd, b, json::WriteOpts(), &st, &why);
        }
        check(tape.pending_count() == 10, "group.batched");
        check(tape.committed_pos() == 0, "group.nothing_published_yet");
        // The process dies here: the batch was never committed.
        tape.close();
    }
    VerifyReport rep; verify_dir(d, &rep);
    check(rep.ok() && rep.rows == 1, "group.uncommitted_batch_is_not_on_the_tape",
          "rows=" + std::to_string(rep.rows));

    // One flush per batch, and the batch lands whole.
    Tape t2; std::string err;
    check(t2.open(cfg, &clk, &err), "group.reopen", err);
    for (int i = 0; i < 10; ++i) {
        json::Value b = json::Value::object(); b.set("i", json::Value::u64((uint64_t)i));
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why; t2.stage(hd, b, json::WriteOpts(), &st, &why);
    }
    check(t2.commit(&err), "group.commit", err);
    check(t2.committed_pos() == 10 && t2.pending_count() == 0, "group.published_whole");
    t2.close();
    VerifyReport rep2; verify_dir(d, &rep2);
    check(rep2.ok() && rep2.rows == 11, "group.verify", "rows=" + std::to_string(rep2.rows));
}

// =====================================================================================================
// 13 · Two cold writes of the same script are bit-identical (falsifier 1a, the writer's side)
// =====================================================================================================
static std::string gen_tape(const std::string& dir, uint64_t n) {
    FixedClock clk(1757000000000000000ull, 1000000ull);
    TapeConfig cfg; cfg.dir = dir; cfg.segment_bytes = 4096;
    Tape tape; std::string err;
    if (!tape.open(cfg, &clk, &err)) return "open failed: " + err;
    for (uint64_t i = 0; i < n; ++i) {
        json::Value f = json::Value::object();
        f.set("table", json::Value::str("orders"));
        f.set("key", json::Value::str("o-" + std::to_string(i % 97)));
        f.set("op", json::Value::str(i % 3 == 0 ? "insert" : "update"));
        f.set("source_pos", json::Value::u64(1000 + i));
        json::Value facts = json::Value::array(); facts.add(f);
        json::Value b = json::Value::object();
        b.set("facts", facts);
        b.set("basis_pos", json::Value::u64(i));
        EntryHdr hd; hd.k = kind::TX; hd.by = by::seat("gen"); hd.cell = 1 + (i % 50); hd.cls = (uint32_t)(i % 7);
        Staged st; std::string why;
        if (!tape.stage(hd, b, json::WriteOpts(), &st, &why)) return "stage failed: " + why;
        if (i % 16 == 15) if (!tape.commit(&err)) return "commit failed: " + err;
    }
    if (!tape.commit(&err)) return "commit failed: " + err;
    tape.close();
    return "";
}
static std::string dir_digest(const std::string& dir, uint64_t* bytes) {
    Blake2b b; b.init(32);
    *bytes = 0;
    for (uint64_t sp : list_segments(dir)) {
        std::string data;
        read_whole(path_join(dir, seg_name(sp)), data);
        b.update((const uint8_t*)data.data(), data.size());
        *bytes += data.size();
    }
    uint8_t out[32]; b.finish(out);
    return hex_of(out, 32);
}
static void t_deterministic() {
    const std::string a = fresh_dir("det_a");
    const std::string c = fresh_dir("det_b");
    const std::string e1 = gen_tape(a, 200);
    const std::string e2 = gen_tape(c, 200);
    check(e1.empty() && e2.empty(), "det.generated", e1 + e2);
    uint64_t na = 0, nb = 0;
    const std::string da = dir_digest(a, &na), db = dir_digest(c, &nb);
    check(da == db && na == nb && na > 0, "det.two_cold_writes_bit_identical", da + " vs " + db);
    VerifyReport rep; verify_dir(a, &rep);
    check(rep.ok(), "det.verifies", rep.problems.empty() ? "" : rep.problems[0]);

    // Lie arm: one different byte anywhere in the script must move the digest.
    const std::string f = fresh_dir("det_c");
    gen_tape(f, 199);
    uint64_t nc = 0;
    check(dir_digest(f, &nc) != da, "det.digest.lie");
}

// =====================================================================================================
// 14 · Floats are refusable at the boundary that feeds folds
// =====================================================================================================
static void t_float_policy() {
    json::Value b = json::Value::object();
    b.set("amount", json::Value::f64(12.5));
    EntryHdr hd; hd.pos = 1; hd.k = kind::TX; hd.by = by::SYSTEM; hd.t_epoch_ns = 1;
    BuiltRow br; std::string why;
    json::WriteOpts strict; strict.allow_f64 = false;
    check(!build_row(hd, b, strict, GENESIS, &br, &why) && why == json::R_FLOAT_FORBIDDEN,
          "float.refused_when_forbidden", why);
    check(build_row(hd, b, json::WriteOpts(), GENESIS, &br, &why), "float.allowed_by_default", why);
    json::Value nan_body = json::Value::object();
    nan_body.set("x", json::Value::f64(std::numeric_limits<double>::infinity()));
    check(!build_row(hd, nan_body, json::WriteOpts(), GENESIS, &br, &why) && why == json::R_NONFINITE_FLOAT,
          "float.nonfinite_refused", why);
    // Lie arm: a duplicate key would make the row lossy for every reader.
    json::Value dup = json::Value::object();
    dup.set("a", json::Value::u64(1)); dup.set("a", json::Value::u64(2));
    check(!build_row(hd, dup, json::WriteOpts(), GENESIS, &br, &why) && why == json::R_DUPLICATE_KEY,
          "float.duplicate_key.lie", why);
}

// =====================================================================================================
// 15 · A segment file's NAME is not a position — the ghost of an uncommitted roll
//
// Regression for the defect the 25-kill run found on 2026-09-08: a roll opens the next segment file
// while its batch is still pending, so a kill in that window leaves a file named for a position that
// was assigned and never published. Recovery that trusts the name resumes past the last committed row
// and leaves a permanent gap in the deposit clock — nine positions, in the run that found it.
// =====================================================================================================
static void t_ghost_segment() {
    const std::string d = fresh_dir("ghost");
    FixedClock clk;
    TapeConfig cfg; cfg.dir = d; cfg.segment_bytes = 2048;
    Tape tape; std::string err;
    check(tape.open(cfg, &clk, &err), "ghost.open", err);
    for (int i = 0; i < 30; ++i) {
        json::Value b = json::Value::object();
        b.set("i", json::Value::u64((uint64_t)i));
        b.set("pad", json::Value::str(std::string(64, 'x')));
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why; tape.stage(hd, b, json::WriteOpts(), &st, &why);
    }
    check(tape.commit(&err), "ghost.commit", err);
    const uint64_t last_pos = tape.committed_pos();
    tape.close();

    // The kill's leavings, both shapes: an empty file, and one holding half a row.
    const uint64_t ghost_a = last_pos + 9;      // named for a position that was never published
    { File f; std::string e2; f.open_append(path_join(d, seg_name(ghost_a)), &e2); f.sync(&e2); }
    Tape t2;
    check(t2.open(cfg, &clk, &err), "ghost.reopen_empty", err);
    // The resumed position comes from the last complete ROW, not from the ghost's name. It advances
    // only by what open itself writes: the warn row, and a seg header if that row rolls the segment.
    check(t2.next_pos() > last_pos && t2.next_pos() <= last_pos + 3 && t2.next_pos() < ghost_a,
          "ghost.resumed_at_last_complete_row",
          "next_pos=" + std::to_string(t2.next_pos()) + " last_pos=" + std::to_string(last_pos) +
          " ghost_name=" + std::to_string(ghost_a));
    check(!file_exists(path_join(d, seg_name(ghost_a))), "ghost.file_discarded");
    t2.close();
    VerifyReport rep; verify_dir(d, &rep);
    check(rep.ok(), "ghost.verifies", rep.problems.empty() ? "" : rep.problems[0]);
    std::string all;
    for (uint64_t sp : list_segments(d)) { std::string x; read_whole(path_join(d, seg_name(sp)), x); all += x; }
    check(all.find("uncommitted_segment_discarded") != std::string::npos, "ghost.warn_entry_written");

    const uint64_t pos_b = 1 + [&]{ VerifyReport r; verify_dir(d, &r); return r.last_pos; }();
    const uint64_t ghost_b = pos_b + 40;
    { File f; std::string e2; f.open_append(path_join(d, seg_name(ghost_b)), &e2);
      f.write_all(std::string("{\"pos\":999,\"term\":0,\"t_epoch_ns\":1,\"k\":\"se"), &e2); f.sync(&e2); }
    Tape t3;
    check(t3.open(cfg, &clk, &err), "ghost.reopen_partial_row", err);
    check(t3.next_pos() >= pos_b && t3.next_pos() <= pos_b + 3 && t3.next_pos() < ghost_b,
          "ghost.partial_row_is_not_a_position",
          "next_pos=" + std::to_string(t3.next_pos()) + " pos_b=" + std::to_string(pos_b) +
          " ghost_name=" + std::to_string(ghost_b));
    t3.close();
    VerifyReport rep2; verify_dir(d, &rep2);
    check(rep2.ok(), "ghost.verifies_after_partial", rep2.problems.empty() ? "" : rep2.problems[0]);

    // Lie arm: the behaviour this replaces — resuming at the ghost's name — leaves a position gap,
    // and the verifier must call that a break rather than a cosmetic jump.
    const std::string dl = fresh_dir("ghost_lie");
    std::string prev = GENESIS, out;
    const uint64_t poses[3] = {0, 1, 11};                      // 2..10 never happened
    for (int i = 0; i < 3; ++i) {
        EntryHdr hd; hd.pos = poses[i]; hd.k = (i == 0 ? kind::SEG : kind::TICK); hd.by = by::SYSTEM;
        hd.t_epoch_ns = 100 + (uint64_t)i;
        json::Value b = (i == 0) ? seg_body(0, GENESIS) : json::Value::object();
        BuiltRow br; std::string why; build_row(hd, b, json::WriteOpts(), prev, &br, &why);
        out += br.line; prev = br.h;
    }
    { File f; std::string e2; f.open_append(path_join(dl, seg_name(0)), &e2); f.truncate_to(0, &e2);
      f.write_all(out, &e2); f.sync(&e2); }
    VerifyReport rl; verify_dir(dl, &rl);
    bool named = false;
    for (const std::string& p : rl.problems) if (p.find("POSITION BREAK") != std::string::npos) named = true;
    check(!rl.ok() && named, "ghost.position_gap.lie");
}

int main(int argc, char** argv) {
    g_root = (argc > 1) ? argv[1] : "C:/TAPESTRY/scratch/t_tape";
    make_dir("C:/TAPESTRY/scratch");
    make_dir(g_root);
    std::printf("== TAPESTRY R0.1 oracles ==  root=%s\n", g_root.c_str());
    t_blake2b();
    t_chain();
    t_lossless();
    t_utf8();
    t_base64();
    t_wire_order();
    t_positions();
    t_epoch_monotone();
    t_roll();
    t_torn();
    t_no_head();
    t_group_commit();
    t_deterministic();
    t_float_policy();
    t_ghost_segment();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
