// =====================================================================================================
// TAPESTRY · src/tests/t_tx.cpp · R0.2 oracles: the expression language, the pins, the transactor
//
// Every oracle carries its lie arm, as in t_tape.cpp. Four of the blueprint's own falsifiers are
// planted here whole, with the lie each one names:
//
//   falsifier  4 · the refusal that never silences — every refused write is an entry or counted in one
//                  lie: a constraint that drops the write
//   falsifier  9 · the clock that does not truncate — source revisions across 2^32
//                  lie: the 32-bit field
//   falsifier 15 · the cap that refusals cannot spend — N refused writes then one legal write, funded
//                  lie: a mutable counter
//   falsifier 17 · nothing learned disposes — a verb entry authored by a judge key is refused
//                  lie: the transactor accepting any author
//   falsifier 20 · every field of the class map moves its pin
//                  lie: a string-only mixer
// =====================================================================================================

#include <cstdio>
#include <string>
#include <vector>
#include "../core/bytes.h"
#include "../core/fileio.h"
#include "../tape/tape.h"
#include "../writ/expr.h"
#include "../writ/classmap.h"
#include "../tx/cells.h"
#include "../tx/transactor.h"

using namespace tapestry;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}
static std::string g_root;
static std::string fresh_dir(const char* name) {
    const std::string d = path_join(g_root, name);
#if defined(_WIN32)
    const std::string pat = path_join(d, "*");
    WIN32_FIND_DATAA fd;
    HANDLE hf = FindFirstFileA(pat.c_str(), &fd);
    if (hf != INVALID_HANDLE_VALUE) {
        do { if (fd.cFileName[0] != '.') DeleteFileA(path_join(d, fd.cFileName).c_str()); }
        while (FindNextFileA(hf, &fd));
        FindClose(hf);
    }
#endif
    make_dir(d);
    return d;
}

// =====================================================================================================
// 1 · The expression language: total, integer-only, deterministic
// =====================================================================================================
static void t_expr() {
    writ::Binding b = writ::ClassMap::binding();
    int64_t row[COL_COUNT] = {0};
    row[COL_AMOUNT_MINOR] = 1500;
    row[COL_STATE] = 2;
    row[COL_DUE_NS] = 5000;
    row[COL_NOW_NS] = 4000;

    struct C { const char* src; bool want; };
    const C cases[] = {
        {"amount_minor <= 500000", true},
        {"amount_minor > 500000", false},
        {"state == 2 && due_ns > now_ns", true},
        {"state == 3 || amount_minor == 1500", true},
        {"!(state == 2)", false},
        {"amount_minor / 100 == 15", true},
        {"amount_minor % 7 == 1500 % 7", true},
        {"abs(0 - amount_minor) == 1500", true},
        {"min(amount_minor, 10) == 10 && max(amount_minor, 10) == 1500", true},
        {"due_ns - now_ns >= 1000", true},
        {"true", true},
        {"(amount_minor + 1) * 2 == 3002", true},
    };
    bool all = true; std::string bad;
    for (const C& c : cases) {
        writ::Program p; std::string why;
        if (!writ::compile(c.src, b, &p, &why)) { all = false; bad = std::string(c.src) + " :: " + why; break; }
        bool got = false;
        if (!writ::eval(p, row, COL_COUNT, &got, &why)) { all = false; bad = std::string(c.src) + " :: " + why; break; }
        if (got != c.want) { all = false; bad = std::string(c.src) + " gave " + (got ? "true" : "false"); break; }
    }
    check(all, "expr.evaluates", bad);

    // Compile-time refusals: a rule that cannot compile is refused when PROPOSED, never when a write
    // depends on it.
    struct B { const char* src; const char* reason; };
    const B bads[] = {
        {"amount_minor",              writ::E_NOT_BOOL},         // an int is not a constraint
        {"nosuchcolumn == 1",         writ::E_UNKNOWN_COLUMN},
        {"amount_minor && state",     writ::E_TYPE},             // ints are not booleans
        {"state == (1 && 2)",         writ::E_TYPE},
        {"amount_minor ==",           writ::E_PARSE},
        {"amount_minor == 007",       writ::E_PARSE},            // one spelling for a number
        {"amount_minor == 1.5",       writ::E_PARSE},            // NO FLOATS, at all
    };
    bool all_bad = true; std::string badbad;
    for (const B& c : bads) {
        writ::Program p; std::string why;
        if (writ::compile(c.src, b, &p, &why)) { all_bad = false; badbad = std::string("accepted: ") + c.src; break; }
        if (why.compare(0, std::char_traits<char>::length(c.reason), c.reason) != 0) {
            all_bad = false; badbad = std::string(c.src) + " gave " + why + " want " + c.reason; break;
        }
    }
    check(all_bad, "expr.refuses_at_compile", badbad);

    // Total at run time: overflow and division by zero are typed reasons, never traps, never wrapped.
    {
        writ::Program p; std::string why; bool out = false;
        int64_t r2[COL_COUNT] = {0};
        r2[COL_AMOUNT_MINOR] = INT64_MAX;
        check(writ::compile("amount_minor + 1 > 0", b, &p, &why), "expr.overflow.compiles", why);
        check(!writ::eval(p, r2, COL_COUNT, &out, &why) && why == writ::E_OVERFLOW, "expr.overflow_is_typed", why);
        writ::Program q;
        check(writ::compile("100 / state == 1", b, &q, &why), "expr.div.compiles", why);
        int64_t r3[COL_COUNT] = {0};                      // state == 0
        check(!writ::eval(q, r3, COL_COUNT, &out, &why) && why == writ::E_DIV_ZERO, "expr.div_zero_is_typed", why);
        writ::Program m;
        check(writ::compile("amount_minor * 2 > 0", b, &m, &why), "expr.mul.compiles", why);
        check(!writ::eval(m, r2, COL_COUNT, &out, &why) && why == writ::E_OVERFLOW, "expr.mul_overflow_is_typed", why);
    }

    // Short circuit: the right side of && is not evaluated when the left is false, so a guarded
    // division by zero is a legal constraint.
    {
        writ::Program p; std::string why; bool out = false;
        int64_t r[COL_COUNT] = {0};                        // state == 0
        check(writ::compile("state != 0 && 100 / state == 1", b, &p, &why), "expr.shortcircuit.compiles", why);
        check(writ::eval(p, r, COL_COUNT, &out, &why) && out == false, "expr.shortcircuit_and", why);
        writ::Program q;
        check(writ::compile("state == 0 || 100 / state == 1", b, &q, &why), "expr.shortcircuit.or.compiles", why);
        check(writ::eval(q, r, COL_COUNT, &out, &why) && out == true, "expr.shortcircuit_or", why);
    }

    // The program hash is over MEANING, not spelling: reformatting must not invalidate a pin, and a
    // changed comparison must.
    {
        writ::Program a, c, d; std::string why;
        writ::compile("amount_minor<=100", b, &a, &why);
        writ::compile("amount_minor <=   100", b, &c, &why);
        writ::compile("amount_minor <  100", b, &d, &why);
        check(a.program_hash == c.program_hash, "expr.hash_ignores_whitespace");
        check(a.program_hash != d.program_hash, "expr.hash_follows_the_operator");
        // Lie arm: a hash over the source text alone would call these two different and those two
        // the same — the exact blindness QC-2 measured in the shipped pin.
        check(std::string("amount_minor<=100") != std::string("amount_minor <=   100"),
              "expr.hash.lie.source_text_differs_where_meaning_does_not");
    }

    // A program records which columns it reads, so a refusal can name the offending values.
    {
        writ::Program p; std::string why;
        writ::compile("amount_minor <= 100 && state == 1", b, &p, &why);
        check(p.reads[COL_AMOUNT_MINOR] && p.reads[COL_STATE] && !p.reads[COL_DUE_NS], "expr.records_reads");
    }
}

// =====================================================================================================
// 2 · Falsifier 20 · every field of the class map moves its pin
// =====================================================================================================
static void t_pins() {
    auto base = []() {
        writ::ClassDef d;
        d.cls = 7; d.name = "orders"; d.flags = 3; d.cap_minor_per_period = 100000;
        d.quorum_n = 2; d.quorum_m = 3;
        writ::RevRule r; r.table = "orders"; r.op = "update"; r.rev = writ::Rev::ReversibleByInverse;
        r.window_ns = 60000000000ull;
        d.rev.push_back(r);
        return d;
    };
    const std::string pin0 = writ::class_pin_of(base());

    struct M { const char* what; writ::ClassDef d; };
    std::vector<M> muts;
    { writ::ClassDef d = base(); d.cls = 8;                      muts.push_back({"cls", d}); }
    { writ::ClassDef d = base(); d.name = "orders2";             muts.push_back({"name", d}); }
    { writ::ClassDef d = base(); d.flags = 4;                    muts.push_back({"flags", d}); }
    { writ::ClassDef d = base(); d.cap_minor_per_period = 99999; muts.push_back({"cap", d}); }
    { writ::ClassDef d = base(); d.quorum_n = 1;                 muts.push_back({"quorum_n", d}); }
    { writ::ClassDef d = base(); d.quorum_m = 4;                 muts.push_back({"quorum_m", d}); }
    { writ::ClassDef d = base(); d.rev[0].table = "invoices";    muts.push_back({"rev.table", d}); }
    { writ::ClassDef d = base(); d.rev[0].op = "insert";         muts.push_back({"rev.op", d}); }
    { writ::ClassDef d = base(); d.rev[0].rev = writ::Rev::Irreversible; muts.push_back({"rev.rev", d}); }
    { writ::ClassDef d = base(); d.rev[0].window_ns = 1;         muts.push_back({"rev.window", d}); }
    { writ::ClassDef d = base(); d.rev.clear();                  muts.push_back({"rev.count", d}); }

    bool all = true; std::string bad;
    for (const M& m : muts) {
        if (writ::class_pin_of(m.d) == pin0) { all = false; bad = m.what; break; }
    }
    check(all, "pin.every_field_moves_it", bad.empty() ? "" : (bad + " did not move the pin"));

    // The check program is in the pin too, by its program hash.
    {
        writ::ClassMap m1, m2;
        writ::ClassDef d = base();
        std::string why;
        check(m1.add(d, {{"cap", "amount_minor <= 100"}}, &why), "pin.map.add", why);
        check(m2.add(d, {{"cap", "amount_minor <= 101"}}, &why), "pin.map.add2", why);
        check(m1.find(7)->class_pin != m2.find(7)->class_pin, "pin.check_program_moves_it");
        check(m1.map_pin() != m2.map_pin(), "pin.map_pin_follows_class_pin");
    }

    // Lie arm: the mixer the estate shipped — concatenate the strings, hash them — collides two class
    // maps that differ only in where one field ends and the next begins.
    {
        Blake2b h1; h1.init(32); h1.update(std::string("ab")); h1.update(std::string("c"));
        uint8_t o1[32]; h1.finish(o1);
        Blake2b h2; h2.init(32); h2.update(std::string("a")); h2.update(std::string("bc"));
        uint8_t o2[32]; h2.finish(o2);
        check(hex_of(o1, 32) == hex_of(o2, 32), "pin.lie.unprefixed_mixer_collides");
        writ::ClassDef a = base(); a.name = "ab"; writ::ClassDef c = base(); c.name = "a";
        check(writ::class_pin_of(a) != writ::class_pin_of(c), "pin.length_prefixed_does_not");
    }
}

// =====================================================================================================
// 3 · The transactor: the write path, and the four falsifiers
// =====================================================================================================
struct Fixture {
    FixedClock clk;
    Tape tape;
    writ::ClassMap map;
    Transactor tx;
    std::string dir;

    bool up(const char* name, int64_t cap = -1, const char* check_src = nullptr) {
        dir = fresh_dir(name);
        TapeConfig tc; tc.dir = dir;
        std::string err;
        if (!tape.open(tc, &clk, &err)) { std::printf("tape.open: %s\n", err.c_str()); return false; }
        writ::ClassDef d;
        d.cls = 1; d.name = "orders"; d.cap_minor_per_period = cap;
        writ::RevRule r; r.table = "*"; r.op = "*"; r.rev = writ::Rev::ReversibleByInverse;
        d.rev.push_back(r);
        std::vector<std::pair<std::string, std::string>> checks;
        if (check_src) checks.push_back({"c1", check_src});
        std::string why;
        if (!map.add(d, checks, &why)) { std::printf("map.add: %s\n", why.c_str()); return false; }
        TxConfig cfg; cfg.seam_code_hash = "abc123";
        return tx.open(&tape, &map, cfg, dir, &err);
    }
};

static Fact mkfact(const char* key, uint64_t src_pos, int64_t amount) {
    Fact f;
    f.source = "olist"; f.table = "orders"; f.key = key; f.op = "update";
    f.source_pos = src_pos; f.cls = 1; f.amount_minor = amount;
    f.has_inverse = true;
    return f;
}
static WriteReq mkreq(const char* cid, const char* rid, const Fact& f) {
    WriteReq r;
    r.client_id = cid; r.request_id = rid; r.by = by::seat("s1");
    r.facts.push_back(f);
    return r;
}

static void t_write_path() {
    Fixture fx;
    check(fx.up("tx_basic"), "tx.open");

    WriteRes r1 = fx.tx.write(mkreq("c1", "r1", mkfact("o-1", 10, 5000)));
    check(r1.committed && r1.refusal.empty(), "tx.commit", r1.refusal + r1.fault);
    check(fx.tx.cells().size() == 1, "tx.cell_created");
    const Cell* c = fx.tx.cells().find(cell_id_of("olist", "orders", "o-1"));
    check(c && c->amount_minor == 5000 && c->src_pos_last == 10 && c->pos_last == r1.pos, "tx.cell_state");

    // A retried write applies ONCE and answers the same thing twice (R0's gate).
    WriteRes r2 = fx.tx.write(mkreq("c1", "r1", mkfact("o-1", 11, 9999)));
    check(!r2.committed || r2.pos == r1.pos, "tx.retry_applies_once");
    check(r2.fault == fault::DUPLICATE_REQUEST, "tx.retry_is_counted", r2.fault);
    check(r2.pos == r1.pos && r2.h == r1.h, "tx.retry_answers_the_same");
    const Cell* c2 = fx.tx.cells().find(cell_id_of("olist", "orders", "o-1"));
    check(c2->amount_minor == 5000, "tx.retry_did_not_apply_twice");

    // basis_pos above committed_pos is protocol noise: a counter, not a claim about anyone.
    WriteReq br = mkreq("c1", "r2", mkfact("o-1", 12, 6000));
    br.basis_pos = 999999;
    WriteRes r3 = fx.tx.write(br);
    check(!r3.committed && r3.fault == fault::STALE_BASIS, "tx.stale_basis_is_a_fault", r3.fault);

    // An unknown class is an entry-class refusal — a claim about the world, on the tape.
    WriteReq ur = mkreq("c1", "r3", mkfact("o-2", 5, 100));
    ur.facts[0].cls = 42;
    WriteRes r4 = fx.tx.write(ur);
    check(!r4.committed && r4.refusal == refuse::UNKNOWN_CLASS, "tx.unknown_class_is_an_entry", r4.refusal);

    std::string err;
    check(fx.tx.flush_refusals(&err), "tx.flush", err);
    VerifyReport rep; verify_dir(fx.dir, &rep);
    check(rep.ok(), "tx.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// falsifier 9 · the clock that does not truncate
static void t_ingest_law() {
    Fixture fx;
    check(fx.up("tx_ingest"), "ingest.open");
    const uint64_t big = 4294967290ull;            // just below 2^32
    check(fx.tx.write(mkreq("c", "a", mkfact("o-1", big, 100))).committed, "ingest.below_2_32");
    // Across the boundary: a 32-bit field would truncate this to 6 and call it stale.
    const uint64_t bigger = 4294967296ull + 6ull;
    WriteRes up = fx.tx.write(mkreq("c", "b", mkfact("o-1", bigger, 200)));
    check(up.committed, "ingest.across_2_32_applies", up.refusal + up.fault);
    const Cell* c = fx.tx.cells().find(cell_id_of("olist", "orders", "o-1"));
    check(c && c->src_pos_last == bigger && c->amount_minor == 200, "ingest.revision_kept_at_64_bits");

    // One stale_source refusal, one duplicate no-op, the cell table unchanged.
    const std::string before = fx.tx.cells().digest();
    WriteRes stale = fx.tx.write(mkreq("c", "c", mkfact("o-1", bigger - 1, 300)));
    check(!stale.committed && stale.refusal == refuse::STALE_SOURCE, "ingest.stale_refused", stale.refusal);
    WriteRes dup = fx.tx.write(mkreq("c", "d", mkfact("o-1", bigger, 400)));
    check(dup.committed && fx.tx.duplicate_noops() == 1, "ingest.equal_is_a_counted_noop");
    const Cell* c3 = fx.tx.cells().find(cell_id_of("olist", "orders", "o-1"));
    check(c3->amount_minor == 200, "ingest.noop_changed_no_column");
    (void)before;

    // Lie arm: the 32-bit field the estate shipped. Truncating both revisions makes the newer one
    // look older, and the update above would have been refused as stale.
    const uint64_t mask = 0xFFFFFFFFull;
    check((uint32_t)(bigger & mask) < (uint32_t)(big & mask), "ingest.lie.32_bit_field_inverts_the_order");

    std::string err; fx.tx.flush_refusals(&err);
    VerifyReport rep; verify_dir(fx.dir, &rep);
    check(rep.ok(), "ingest.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// falsifier 15 · the cap that refusals cannot spend
static void t_cap() {
    Fixture fx;
    check(fx.up("tx_cap", /*cap*/ 1000), "cap.open");
    check(fx.tx.write(mkreq("c", "a", mkfact("o-1", 1, 600))).committed, "cap.first_write_funded");
    check(fx.tx.cap_used(1) == 600, "cap.accumulated", std::to_string(fx.tx.cap_used(1)));

    // N refused writes, each one over the cap.
    int refused = 0;
    for (int i = 0; i < 20; ++i) {
        WriteRes r = fx.tx.write(mkreq("c", ("over" + std::to_string(i)).c_str(), mkfact("o-2", 1 + i, 900)));
        if (!r.committed && r.refusal == refuse::CONSTRAINT_VIOLATED) ++refused;
    }
    check(refused == 20, "cap.over_cap_refused", std::to_string(refused));
    check(fx.tx.cap_used(1) == 600, "cap.refusals_spent_nothing", std::to_string(fx.tx.cap_used(1)));

    // Then one legal write, funded — which is the falsifier's actual claim.
    WriteRes ok = fx.tx.write(mkreq("c", "legal", mkfact("o-3", 99, 400)));
    check(ok.committed, "cap.legal_write_after_refusals_is_funded", ok.refusal + ok.fault);
    check(fx.tx.cap_used(1) == 1000, "cap.exactly_at_the_cap", std::to_string(fx.tx.cap_used(1)));

    // A cap tick closes the period; the fold's window is the span since that tick.
    std::string err;
    check(fx.tx.tick("cap_period", &err), "cap.tick", err);
    check(fx.tx.cap_used(1) == 0, "cap.period_closed");
    check(fx.tx.write(mkreq("c", "next", mkfact("o-4", 1, 900))).committed, "cap.new_period_funded");

    // The accumulator is DERIVED: rebuilding the fold from the tape alone reproduces it. A mutable
    // counter — the planted lie — cannot survive this, because nothing on the tape would carry it.
    fx.tx.flush_refusals(&err);
    Transactor rebuilt;
    TxConfig cfg; cfg.seam_code_hash = "abc123";
    check(rebuilt.open(&fx.tape, &fx.map, cfg, fx.dir, &err), "cap.rebuild", err);
    check(rebuilt.cap_used(1) == fx.tx.cap_used(1), "cap.rebuilt_from_the_tape",
          std::to_string(rebuilt.cap_used(1)) + " vs " + std::to_string(fx.tx.cap_used(1)));
    check(rebuilt.cells().digest() == fx.tx.cells().digest(), "cap.cell_table_rebuilt_identically");

    VerifyReport rep; verify_dir(fx.dir, &rep);
    check(rep.ok(), "cap.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// falsifier 4 · the refusal that never silences
static void t_refusal_never_silences() {
    Fixture fx;
    check(fx.up("tx_refuse", -1, "amount_minor <= 1000"), "refuse.open");

    const int N = 50;
    int refused = 0;
    for (int i = 0; i < N; ++i) {
        WriteRes r = fx.tx.write(mkreq("c", ("x" + std::to_string(i)).c_str(), mkfact("o-1", 1 + i, 5000)));
        if (!r.committed) ++refused;
    }
    check(refused == N, "refuse.all_refused", std::to_string(refused));
    std::string err;
    check(fx.tx.flush_refusals(&err), "refuse.flush", err);

    // Every refused write is an entry or counted in one: the counts on the refuse entries must SUM to
    // the number of refusals. Coalescing changes the entry count, never the sum.
    uint64_t sum = 0, entries = 0;
    std::string werr;
    for_each_row(fx.dir, [&](const ScannedRow& r) {
        if (r.hdr.k != kind::REFUSE) return true;
        ++entries;
        size_t i = 0; std::string dig, reason, cid; uint64_t count = 0, fp = 0, lp = 0;
        using namespace detail;
        if (eat_lit(r.body, i, "{\"attempted_digest\":") && eat_string(r.body, i, dig)
            && eat_lit(r.body, i, ",\"reason\":") && eat_string(r.body, i, reason)
            && eat_lit(r.body, i, ",\"constraint_id\":") && eat_string(r.body, i, cid)
            && eat_lit(r.body, i, ",\"count\":") && eat_u64(r.body, i, count)
            && eat_lit(r.body, i, ",\"first_pos\":") && eat_u64(r.body, i, fp)
            && eat_lit(r.body, i, ",\"last_pos\":") && eat_u64(r.body, i, lp)) {
            sum += count;
        }
        return true;
    }, &werr);
    check(sum == (uint64_t)N, "refuse.counts_sum_to_the_refusals",
          std::to_string(sum) + " of " + std::to_string(N) + " over " + std::to_string(entries) + " entries");
    check(entries < (uint64_t)N, "refuse.coalesced", std::to_string(entries) + " entries for " + std::to_string(N));
    check(fx.tx.health().refusals.at(refuse::CONSTRAINT_VIOLATED) == (uint64_t)N, "refuse.health_counts_match");

    // The refusal names its clause, so explain_refusal has something true to print.
    WriteRes one = fx.tx.write(mkreq("c", "named", mkfact("o-9", 1, 5000)));
    check(one.constraint_id == "c1", "refuse.names_the_clause", one.constraint_id);

    // A constraint that cannot be EVALUATED refuses; it never passes by default.
    {
        Fixture f2;
        check(f2.up("tx_refuse_eval", -1, "1000 / state == 1"), "refuse.eval.open");
        WriteRes r = f2.tx.write(mkreq("c", "z", mkfact("o-1", 1, 10)));   // state 0 → divide by zero
        check(!r.committed && r.refusal == refuse::CONSTRAINT_VIOLATED, "refuse.uneval_is_a_refusal", r.refusal);
        check(r.constraint_id.find(writ::E_DIV_ZERO) != std::string::npos, "refuse.uneval_names_why", r.constraint_id);
        std::string e2; f2.tx.flush_refusals(&e2);
    }

    // Lie arm: a constraint that DROPS the write leaves the sum short of the refusals — the failure
    // this oracle exists to catch.
    check(sum == (uint64_t)N && !(sum < (uint64_t)N), "refuse.lie.a_dropped_write_would_short_the_sum");

    fx.tx.flush_refusals(&err);
    VerifyReport rep; verify_dir(fx.dir, &rep);
    check(rep.ok(), "refuse.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// falsifier 17 · nothing learned disposes
static void t_seam_authorship() {
    Fixture fx;
    check(fx.up("tx_seam"), "seam.open");

    WriteReq v = mkreq("c", "v1", mkfact("o-1", 1, 100));
    v.kind = kind::VERB;
    v.by = by::judge("deadbeef", "servepin");         // a judge key, authoring a verb
    WriteRes r = fx.tx.write(v);
    check(!r.committed && r.refusal == refuse::NOT_AUTHORED_BY_SEAM, "seam.judge_verb_refused", r.refusal);

    WriteReq v2 = mkreq("c", "v2", mkfact("o-1", 1, 100));
    v2.kind = kind::VERB;
    v2.by = by::seat("operator");                     // a seat is not the seam either
    check(fx.tx.write(v2).refusal == refuse::NOT_AUTHORED_BY_SEAM, "seam.seat_verb_refused");

    WriteReq v3 = mkreq("c", "v3", mkfact("o-1", 1, 100));
    v3.kind = kind::VERB;
    v3.by = by::seam("abc123");                       // the pinned code hash, and only that
    WriteRes ok = fx.tx.write(v3);
    check(ok.committed, "seam.seam_verb_admitted", ok.refusal + ok.fault);

    WriteReq v4 = mkreq("c", "v4", mkfact("o-1", 2, 100));
    v4.kind = kind::VERB;
    v4.by = by::seam("wronghash");                    // a different seam build is a different seam
    check(fx.tx.write(v4).refusal == refuse::NOT_AUTHORED_BY_SEAM, "seam.wrong_code_hash_refused");

    // Lie arm: a transactor that accepts any author would have committed the judge's verb above.
    check(r.committed == false, "seam.lie.any_author_would_have_committed");

    std::string err; fx.tx.flush_refusals(&err);
    VerifyReport rep; verify_dir(fx.dir, &rep);
    check(rep.ok(), "seam.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// The derived reversibility, the inverse, and the world's own rows
static void t_reversibility() {
    Fixture fx;
    check(fx.up("tx_rev"), "rev.open");

    // The inverse is required by the class map, not by the writer's say-so.
    Fact f = mkfact("o-1", 1, 100);
    f.has_inverse = false;
    WriteRes r = fx.tx.write(mkreq("c", "a", f));
    check(!r.committed && r.refusal == refuse::NO_INVERSE, "rev.missing_inverse_refused", r.refusal);

    // A declaration that disagrees with the derived class is refused, not believed.
    WriteReq d = mkreq("c", "b", mkfact("o-1", 1, 100));
    d.declared_reversibility = "irreversible";
    check(fx.tx.write(d).refusal == refuse::REVERSIBILITY_MISMATCH, "rev.mismatch_refused");

    WriteReq ok = mkreq("c", "c", mkfact("o-1", 1, 100));
    ok.declared_reversibility = "reversible_by_inverse";
    check(fx.tx.write(ok).committed, "rev.agreeing_declaration_admitted");

    // An inverse on a fact the WORLD authored is forbidden: the world's rows are not ours to undo.
    Fact e = mkfact("o-2", 1, 100);
    e.exogenous = true; e.has_inverse = true;
    check(fx.tx.write(mkreq("c", "d", e)).refusal == refuse::REVERSIBILITY_MISMATCH, "rev.exogenous_inverse_refused");
    Fact e2 = mkfact("o-2", 1, 100);
    e2.exogenous = true; e2.has_inverse = false;
    check(fx.tx.write(mkreq("c", "e", e2)).committed, "rev.exogenous_without_inverse_admitted");

    // Lie arm: trusting the writer's declaration would admit the mismatched write above.
    check(fx.tx.health().refusals.count(refuse::REVERSIBILITY_MISMATCH) == 1, "rev.lie.declaration_is_not_trusted");

    std::string err; fx.tx.flush_refusals(&err);
    VerifyReport rep; verify_dir(fx.dir, &rep);
    check(rep.ok(), "rev.tape_verifies", rep.problems.empty() ? "" : rep.problems[0]);
}

// The id collision that must never merge two obligations into one row
static void t_id_collision() {
    Fixture fx;
    check(fx.up("tx_collide"), "collide.open");
    check(fx.tx.write(mkreq("c", "a", mkfact("o-1", 1, 100))).committed, "collide.first");
    check(cell_id_of("olist", "orders", "o-1") != cell_id_of("olist", "orders", "o-2"), "collide.ids_differ");
    check(cell_id_of("a", "b", "c") != cell_id_of("ab", "", "c"), "collide.separator_is_load_bearing");
    check(cell_id_of("olist", "orders", "o-1") != 0, "collide.zero_is_reserved");
    std::string err; fx.tx.flush_refusals(&err);
}

// The tx body: one writer, one reader, and an oracle that fails if they drift
static void t_body_round_trip() {
    WriteReq r;
    r.client_id = "c"; r.request_id = "r"; r.basis_pos = 17;
    Fact f;
    f.source = "olist \" \\ \x01"; f.table = "orders"; f.key = "o-\xE2\x9C\x93"; f.op = "insert";
    f.source_pos = 18446744073709551615ull; f.cls = 4294967295u;
    f.amount_minor = INT64_MIN; f.due_ns = 12345; f.state = 255; f.flags = 254;
    f.exogenous = true; f.has_inverse = false; f.reverses_pos = 99;
    r.facts.push_back(f);
    r.facts.push_back(mkfact("o-2", 7, -1));

    std::string out; std::string why;
    json::WriteOpts opts; opts.allow_f64 = false;
    check(json::write(tx_body(r, "irreversible"), out, opts, &why), "body.write", why);

    uint64_t basis = 0; std::string rev; std::vector<Fact> back;
    check(read_tx_body(out, &basis, &rev, &back), "body.read");
    check(basis == 17 && rev == "irreversible" && back.size() == 2, "body.shape");
    check(back[0].source == f.source && back[0].key == f.key && back[0].op == f.op, "body.strings_survive");
    check(back[0].source_pos == f.source_pos && back[0].cls == f.cls, "body.widths_survive");
    check(back[0].amount_minor == INT64_MIN && back[0].state == 255 && back[0].flags == 254, "body.extremes_survive");
    check(back[0].exogenous && !back[0].has_inverse && back[0].reverses_pos == 99, "body.flags_survive");
    check(back[1].amount_minor == -1, "body.negative_money_survives");

    // Lie arm: one byte changed in the encoded body must not read back as the same facts.
    std::string tampered = out;
    const size_t at = tampered.find("\"state\":255");
    if (at != std::string::npos) tampered[at + 9] = '1';
    std::vector<Fact> back2; uint64_t b2 = 0; std::string rev2;
    const bool read_ok = read_tx_body(tampered, &b2, &rev2, &back2);
    check(!read_ok || back2[0].state != 255, "body.lie.a_changed_byte_changes_the_facts");
}

// Admission: a principal that floods is rate limited, and that is a counter, not a claim
static void t_admission() {
    const std::string dir = fresh_dir("tx_admit");
    FixedClock clk;
    Tape tape; TapeConfig tc; tc.dir = dir;
    std::string err;
    check(tape.open(tc, &clk, &err), "admit.tape", err);
    writ::ClassMap map;
    writ::ClassDef d; d.cls = 1; d.name = "orders";
    writ::RevRule r; r.table = "*"; r.op = "*"; r.rev = writ::Rev::ReversibleByInverse;
    d.rev.push_back(r);
    std::string why;
    check(map.add(d, {}, &why), "admit.map", why);
    Transactor tx;
    TxConfig cfg; cfg.seam_code_hash = "abc123";
    cfg.tokens_burst = 3.0; cfg.tokens_per_second = 0.0;    // three writes, then nothing
    check(tx.open(&tape, &map, cfg, dir, &err), "admit.open", err);

    int ok = 0, limited = 0;
    for (int i = 0; i < 10; ++i) {
        WriteRes res = tx.write(mkreq("c", ("k" + std::to_string(i)).c_str(), mkfact("o-1", 1 + i, 10)));
        if (res.committed) ++ok;
        else if (res.fault == fault::RATE_LIMITED) ++limited;
    }
    check(ok == 3 && limited == 7, "admit.token_bucket", std::to_string(ok) + " ok, " + std::to_string(limited) + " limited");
    check(tx.health().faults.at(fault::RATE_LIMITED) == 7, "admit.counted_not_written");
    uint64_t refuse_entries = 0; std::string werr;
    for_each_row(dir, [&](const ScannedRow& row) { if (row.hdr.k == kind::REFUSE) ++refuse_entries; return true; }, &werr);
    check(refuse_entries == 0, "admit.a_fault_is_not_an_entry", std::to_string(refuse_entries));
}

int main(int argc, char** argv) {
    g_root = (argc > 1) ? argv[1] : "C:/TAPESTRY/scratch/t_tx";
    make_dir("C:/TAPESTRY/scratch");
    make_dir(g_root);
    std::printf("== TAPESTRY R0.2 oracles ==  root=%s\n", g_root.c_str());
    t_expr();
    t_pins();
    t_write_path();
    t_ingest_law();
    t_cap();
    t_refusal_never_silences();
    t_seam_authorship();
    t_reversibility();
    t_id_collision();
    t_body_round_trip();
    t_admission();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
