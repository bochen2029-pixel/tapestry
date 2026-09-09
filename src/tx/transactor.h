// =====================================================================================================
// TAPESTRY · src/tx/transactor.h · the one writer
//
// Blueprint §4.1, §4.7. One logical writer holding its own CPU-resident fold of exactly the state its
// constraints read — the cell table's constrained columns and the per-class cap accumulators — so that
// validation never crosses to the card. Per proposed write, in §4.1's order:
//
//    1  leader check                          → fault not_leader
//    2  admission, per-principal token bucket  → fault rate_limited
//    3  idempotency, on (client_id, request_id)→ fault duplicate_request, and the ORIGINAL answer
//    4  constraint validation per touched cell → an ENTRY-class refusal
//    5  build, batch, commit, acknowledge, publish
//
// TWO CLASSES OF REFUSAL, AND WHY. §4.1: judgments about the world are ENTRIES; protocol faults are
// COUNTERS on health(). The distinction is not cosmetic — an entry is a permanent claim about a
// counterparty's behaviour and belongs on the tape; a malformed frame is noise and belongs in a
// counter. Falsifier 4 ("the refusal that never silences") holds over both: every refused write is
// either an entry or counted in one, and the oracle sums them.
//
// COALESCING. §4.1 coalesces identical entry-class refusals per principal, constraint and cell within
// a window, because QC-5 F11 showed an entry per refusal is a denial of service against the tape
// itself. The first refusal in a window writes its entry immediately with count 1; the rest increment
// a pending count that is flushed as a second entry carrying the total and the span. At no instant is
// a refusal neither an entry nor counted in one — which is the property, not the entry count.
//
// THE CAP THAT REFUSALS CANNOT SPEND. §4.7 evaluates exposure caps "as a fold over committed facts
// since the last cap tick". So the accumulator here is derived, never a mutable counter incremented at
// validation time: a refused write contributes nothing because it never becomes a committed fact, and
// a crash re-derives the period by replaying the tape from the last cap tick. Falsifier 15.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "cells.h"
#include "../writ/classmap.h"
#include "../writ/expr.h"
#include "../tape/tape.h"
#include "../core/json.h"

namespace tapestry {

// ---- refusals: entry class, judgments about the world -----------------------------------------------
namespace refuse {
inline const char* const CONSTRAINT_VIOLATED    = "constraint_violated";
inline const char* const QUORUM_INSUFFICIENT    = "quorum_insufficient";
inline const char* const LICENSE_EXCEEDED       = "license_exceeded";
inline const char* const PIN_MISMATCH           = "pin_mismatch";
inline const char* const NO_INVERSE             = "no_inverse";
inline const char* const STALE_SOURCE           = "stale_source";
inline const char* const STALE_JUDGMENT         = "stale_judgment";
inline const char* const REVERSIBILITY_MISMATCH = "reversibility_mismatch";
inline const char* const ID_COLLISION           = "id_collision";
inline const char* const UNKNOWN_CELL           = "unknown_cell";
inline const char* const UNKNOWN_CLASS          = "unknown_class";
// DELTA against §4.1's list: falsifier 17 ("a verb entry authored by a judge key is refused") has no
// typed reason in the blueprint. Overloading `constraint_violated` would hide the one refusal the
// fourth law exists for, so it gets its own name.
inline const char* const NOT_AUTHORED_BY_SEAM   = "not_authored_by_seam";
}

// ---- faults: counter class, protocol noise ------------------------------------------------------------
namespace fault {
inline const char* const MALFORMED         = "malformed";
inline const char* const NOT_LEADER        = "not_leader";
inline const char* const NO_QUORUM         = "no_quorum";
inline const char* const RATE_LIMITED      = "rate_limited";
inline const char* const DUPLICATE_REQUEST = "duplicate_request";
inline const char* const STALE_BASIS       = "stale_basis";
inline const char* const TOO_LARGE         = "too_large";
}

// ---- the request ----------------------------------------------------------------------------------
struct Fact {
    std::string source, table, key, op;
    uint64_t    source_pos = 0;      // the world's clock for this row: 64 bits (falsifier 9)
    uint32_t    cls = 0;
    int64_t     amount_minor = 0;    // the after-state
    uint64_t    due_ns = 0;
    uint8_t     state = 0, flags = 0;
    bool        exogenous = false;   // a fact the world authored: an inverse is FORBIDDEN on it
    bool        has_inverse = false;
    uint64_t    reverses_pos = 0;
};

struct WriteReq {
    std::string client_id, request_id;
    std::string by = "seat:unknown";      // the principal
    uint64_t    basis_pos = 0;
    std::vector<Fact> facts;
    std::string declared_reversibility;   // redundant assertion; a mismatch is refused and warned
    std::string kind = kind::TX;          // `verb` and `hold` take the seam-authorship path
    uint64_t    margin_pos_read = 0;      // for a verb: the position the margin was judged against
};

struct WriteRes {
    bool        committed = false;
    uint64_t    pos = 0;
    std::string h;
    std::string refusal;        // entry-class reason, "" if none
    std::string constraint_id;  // which clause, for explain_refusal
    std::string fault;          // counter-class reason, "" if none
    uint64_t    refusal_pos = 0;
};

struct TxConfig {
    uint64_t refusal_window_ns   = 1000000000ull;   // 1 s
    double   tokens_burst        = 100000.0;        // per principal; the oracle sets a small one
    double   tokens_per_second   = 100000.0;
    size_t   max_facts_per_write = 1000;
    std::string seam_code_hash;                     // `by` must equal seam:<this> to author a verb
};

struct Health {
    uint64_t committed_pos = 0;
    uint64_t writes_committed = 0;
    uint64_t writes_refused = 0;       // entry class
    std::map<std::string, uint64_t> refusals;   // entry-class reason → count
    std::map<std::string, uint64_t> faults;     // counter-class reason → count
    uint64_t refusal_entries = 0;
    uint64_t cells = 0;
};

// ---- the tx body: one writer, one reader, side by side ------------------------------------------------
// Fixed order, every field always present. Written and read in this one place so the two cannot drift;
// the oracle `body.round_trip` fails if they do. No permissive parser touches the trust path.
inline json::Value fact_body(const Fact& f) {
    json::Value o = json::Value::object();
    o.set("source", json::Value::str(f.source));
    o.set("table",  json::Value::str(f.table));
    o.set("key",    json::Value::str(f.key));
    o.set("op",     json::Value::str(f.op));
    o.set("source_pos", json::Value::u64(f.source_pos));
    o.set("cls",    json::Value::u64(f.cls));
    o.set("exogenous",   json::Value::boolean(f.exogenous));
    o.set("has_inverse", json::Value::boolean(f.has_inverse));
    o.set("reverses_pos", json::Value::u64(f.reverses_pos));
    json::Value after = json::Value::object();
    after.set("amount_minor", json::Value::i64(f.amount_minor));
    after.set("due_ns", json::Value::u64(f.due_ns));
    after.set("state",  json::Value::u64(f.state));
    after.set("flags",  json::Value::u64(f.flags));
    o.set("after", after);
    return o;
}
inline json::Value tx_body(const WriteReq& r, const std::string& derived_rev) {
    json::Value b = json::Value::object();
    // The idempotency key is ON THE TAPE. §4.2: "the client reply cache is part of the replicated
    // state machine" — so it is a fold, not a side table, and a transactor that has just recovered
    // from a kill can still tell a retry from a new write. Without these two fields the gate "a
    // retried write applies once" holds only until the first crash, which is the case it is for.
    b.set("client_id", json::Value::str(r.client_id));
    b.set("request_id", json::Value::str(r.request_id));
    b.set("basis_pos", json::Value::u64(r.basis_pos));
    b.set("reversibility", json::Value::str(derived_rev));
    json::Value fs = json::Value::array();
    for (const Fact& f : r.facts) fs.add(fact_body(f));
    b.set("facts", fs);
    return b;
}
inline bool read_fact(const std::string& s, size_t& i, Fact* f) {
    using namespace detail;
    uint64_t u = 0;
    if (!eat_lit(s, i, "{\"source\":") || !eat_string(s, i, f->source)) return false;
    if (!eat_lit(s, i, ",\"table\":")  || !eat_string(s, i, f->table))  return false;
    if (!eat_lit(s, i, ",\"key\":")    || !eat_string(s, i, f->key))    return false;
    if (!eat_lit(s, i, ",\"op\":")     || !eat_string(s, i, f->op))     return false;
    if (!eat_lit(s, i, ",\"source_pos\":") || !eat_u64(s, i, f->source_pos)) return false;
    if (!eat_lit(s, i, ",\"cls\":")    || !eat_u64(s, i, u) || u > 0xFFFFFFFFull) return false;
    f->cls = (uint32_t)u;
    if (!eat_lit(s, i, ",\"exogenous\":")   || !eat_bool(s, i, f->exogenous))   return false;
    if (!eat_lit(s, i, ",\"has_inverse\":") || !eat_bool(s, i, f->has_inverse)) return false;
    if (!eat_lit(s, i, ",\"reverses_pos\":")|| !eat_u64(s, i, f->reverses_pos)) return false;
    if (!eat_lit(s, i, ",\"after\":{\"amount_minor\":") || !eat_i64(s, i, f->amount_minor)) return false;
    if (!eat_lit(s, i, ",\"due_ns\":") || !eat_u64(s, i, f->due_ns)) return false;
    if (!eat_lit(s, i, ",\"state\":")  || !eat_u64(s, i, u) || u > 255) return false;
    f->state = (uint8_t)u;
    if (!eat_lit(s, i, ",\"flags\":")  || !eat_u64(s, i, u) || u > 255) return false;
    f->flags = (uint8_t)u;
    if (!eat_lit(s, i, "}}")) return false;
    return true;
}
inline bool read_tx_body(const std::string& body, std::string* client_id, std::string* request_id,
                         uint64_t* basis_pos, std::string* rev, std::vector<Fact>* facts) {
    using namespace detail;
    size_t i = 0;
    if (!eat_lit(body, i, "{\"client_id\":") || !eat_string(body, i, *client_id)) return false;
    if (!eat_lit(body, i, ",\"request_id\":") || !eat_string(body, i, *request_id)) return false;
    if (!eat_lit(body, i, ",\"basis_pos\":") || !eat_u64(body, i, *basis_pos)) return false;
    if (!eat_lit(body, i, ",\"reversibility\":") || !eat_string(body, i, *rev)) return false;
    if (!eat_lit(body, i, ",\"facts\":[")) return false;
    facts->clear();
    if (body.compare(i, 1, "]") == 0) { ++i; return eat_lit(body, i, "}") && i == body.size(); }
    while (true) {
        Fact f;
        if (!read_fact(body, i, &f)) return false;
        facts->push_back(f);
        if (body.compare(i, 1, ",") == 0) { ++i; continue; }
        break;
    }
    if (!eat_lit(body, i, "]")) return false;
    if (!eat_lit(body, i, "}")) return false;
    return i == body.size();
}
inline json::Value tick_body(const std::string& cadence_rule, uint64_t wheel_pos) {
    json::Value b = json::Value::object();
    b.set("cadence_rule", json::Value::str(cadence_rule));
    b.set("deadline_wheel_pos", json::Value::u64(wheel_pos));
    return b;
}
inline bool read_tick_body(const std::string& body, std::string* cadence_rule, uint64_t* wheel_pos) {
    using namespace detail;
    size_t i = 0;
    if (!eat_lit(body, i, "{\"cadence_rule\":") || !eat_string(body, i, *cadence_rule)) return false;
    if (!eat_lit(body, i, ",\"deadline_wheel_pos\":") || !eat_u64(body, i, *wheel_pos)) return false;
    return eat_lit(body, i, "}") && i == body.size();
}

// ---- the transactor ----------------------------------------------------------------------------------
class Transactor {
public:
    // Opening rebuilds the fold by replaying the tape: §4.1's "its own CPU-resident fold ... maintained
    // by the same reducers the peer runs". R0 replays from the beginning; a snapshot base is R1.
    bool open(Tape* tape, const writ::ClassMap* map, const TxConfig& cfg, const std::string& dir,
              std::string* err) {
        tape_ = tape; map_ = map; cfg_ = cfg; dir_ = dir;
        cells_ = CellTable();
        cap_used_.clear();
        replies_.clear();
        applied_pos_ = 0; applied_any_ = false; redelivered_ = 0;
        std::string werr;
        const bool ok = for_each_row(dir_, [&](const ScannedRow& r) { apply_committed(r); return true; }, &werr);
        if (!ok) { if (err) *err = "rebuild: " + werr; return false; }
        health_.committed_pos = tape_->committed_pos();
        health_.cells = cells_.size();
        return true;
    }

    WriteRes write(const WriteReq& req) {
        WriteRes res;

        // 1 · leader. v0 has one node and is always the leader; the counter exists so the shape of the
        //     answer does not change when R6 adds Raft.
        if (!leader_) { res.fault = fault::NOT_LEADER; ++health_.faults[res.fault]; return res; }

        // The stamp this write's entry will carry, reserved BEFORE validation because a check
        // expression reads `now` and §3.4 says `now` is the stamp of the entry being folded — not a
        // host clock read somewhere in the middle of deciding.
        const uint64_t now = tape_->reserve_stamp();

        // 2 · admission. A per-principal token bucket, refilled against the tape's epoch stamp — never
        //     a host clock, so admission is a function of the tape like everything else.
        if (!admit(req.by, now)) { res.fault = fault::RATE_LIMITED; ++health_.faults[res.fault]; return res; }

        if (req.facts.size() > cfg_.max_facts_per_write) {
            res.fault = fault::TOO_LARGE; ++health_.faults[res.fault]; return res;
        }
        if (req.client_id.empty() || req.request_id.empty()) {
            res.fault = fault::MALFORMED; ++health_.faults[res.fault]; return res;
        }

        // 3 · idempotency. A retried write applies ONCE and answers the same thing twice.
        const std::string rk = req.client_id + "\x1f" + req.request_id;
        auto it = replies_.find(rk);
        if (it != replies_.end()) {
            WriteRes cached = it->second;
            cached.fault = fault::DUPLICATE_REQUEST;
            ++health_.faults[fault::DUPLICATE_REQUEST];
            return cached;
        }

        // basis_pos ≤ committed_pos (§4.1). A stale basis is protocol noise, not a claim about anyone.
        if (tape_->has_committed() && req.basis_pos > tape_->committed_pos()) {
            res.fault = fault::STALE_BASIS; ++health_.faults[res.fault]; return res;
        }

        // 4 · constraint validation, per touched cell.
        std::string constraint_id;
        std::string reason = validate(req, now, &constraint_id);
        if (!reason.empty()) {
            res.refusal = reason;
            res.constraint_id = constraint_id;
            ++health_.writes_refused;
            ++health_.refusals[reason];
            record_refusal(req, now, reason, constraint_id, &res);
            replies_[rk] = res;                  // a retried refusal answers the same refusal
            return res;
        }

        // 5 · build, batch, commit, acknowledge, publish.
        const writ::ClassDef* cd = map_->find(req.facts.empty() ? 0u : req.facts[0].cls);
        std::string derived = "n/a";
        if (cd && !req.facts.empty()) {
            uint64_t w = 0;
            derived = writ::rev_name(cd->reversibility_of(req.facts[0].table, req.facts[0].op, &w));
        }
        EntryHdr hd;
        hd.k = req.kind;
        hd.by = req.by;
        hd.t_epoch_ns = now;
        hd.cell = req.facts.empty() ? 0 : cell_id_of(req.facts[0].source, req.facts[0].table, req.facts[0].key);
        hd.cls = req.facts.empty() ? 0 : req.facts[0].cls;
        Staged st; std::string why;
        json::WriteOpts opts; opts.allow_f64 = false;   // no float ever reaches a fold's input
        if (!tape_->stage(hd, tx_body(req, derived), opts, &st, &why)) {
            res.fault = fault::MALFORMED; ++health_.faults[res.fault]; return res;
        }
        std::string cerr;
        if (!tape_->commit(&cerr)) { res.fault = fault::MALFORMED; ++health_.faults[res.fault]; return res; }

        // Applied only after the flush returned: the fold never holds state the tape does not. And the
        // applied position moves with it, so a subscriber replaying this same suffix later — after a
        // reconnect, a leader change, or an effector's at-least-once redelivery — is a no-op rather
        // than a second helping.
        apply_facts(req.facts, st.pos, st.t_epoch_ns);
        applied_pos_ = st.pos; applied_any_ = true;
        last_stamp_ = st.t_epoch_ns;
        res.committed = true; res.pos = st.pos; res.h = st.h;
        ++health_.writes_committed;
        health_.committed_pos = tape_->committed_pos();
        health_.cells = cells_.size();
        replies_[rk] = res;
        return res;
    }

    // Time as a row (§3.1's `tick`). `cap_period` closes an exposure period: the cap fold's window is
    // the span since the last such tick, which is why a crash cannot reset a period and a refusal
    // cannot spend one.
    bool tick(const std::string& cadence_rule, std::string* err) {
        EntryHdr hd; hd.k = kind::TICK; hd.by = by::SYSTEM;
        Staged st; std::string why;
        json::WriteOpts opts; opts.allow_f64 = false;
        if (!tape_->stage(hd, tick_body(cadence_rule, wheel_pos_++), opts, &st, &why)) {
            if (err) *err = why; return false;
        }
        if (!tape_->commit(err)) return false;
        last_stamp_ = st.t_epoch_ns;
        applied_pos_ = st.pos; applied_any_ = true;
        if (cadence_rule == "cap_period") cap_used_.clear();
        health_.committed_pos = tape_->committed_pos();
        return true;
    }

    // Close every open coalescing window, so nothing is left counted-but-unwritten at shutdown.
    bool flush_refusals(std::string* err) {
        for (auto& kv : windows_) {
            if (kv.second.pending == 0) continue;
            if (!write_refusal_entry(kv.second, kv.second.pending, err)) return false;
            kv.second.pending = 0;
        }
        windows_.clear();
        return tape_->commit(err);
    }

    const Health&    health() const { return health_; }
    const CellTable& cells()  const { return cells_; }
    int64_t cap_used(uint32_t cls) const {
        auto it = cap_used_.find(cls);
        return it == cap_used_.end() ? 0 : it->second;
    }
    void set_leader(bool v) { leader_ = v; }

private:
    struct Bucket { double tokens; uint64_t last_ns; };
    struct Window {
        std::string principal, reason, constraint_id, digest;
        uint64_t cell = 0, opened_ns = 0, first_pos = 0, last_pos = 0, pending = 0;
    };

    bool admit(const std::string& principal, uint64_t now_ns) {
        Bucket& b = buckets_[principal];
        if (b.last_ns == 0) { b.tokens = cfg_.tokens_burst; b.last_ns = now_ns; }
        if (now_ns > b.last_ns) {
            const double dt = (double)(now_ns - b.last_ns) / 1e9;
            b.tokens += dt * cfg_.tokens_per_second;
            if (b.tokens > cfg_.tokens_burst) b.tokens = cfg_.tokens_burst;
            b.last_ns = now_ns;
        }
        if (b.tokens < 1.0) return false;
        b.tokens -= 1.0;
        return true;
    }

    // The whole writ, in the order §4.1 states it. Returns "" to admit, or an entry-class reason.
    std::string validate(const WriteReq& req, uint64_t now, std::string* constraint_id) {
        constraint_id->clear();

        // The fourth law, enforced where it can be: a verb is authored by the seam or by nobody.
        // Falsifier 17 plants a verb signed with a judge key.
        if (req.kind == kind::VERB) {
            const std::string want = "seam:" + cfg_.seam_code_hash;
            if (cfg_.seam_code_hash.empty() || req.by != want) return refuse::NOT_AUTHORED_BY_SEAM;
        }

        for (const Fact& f : req.facts) {
            const writ::ClassDef* cd = map_->find(f.cls);
            if (!cd) return refuse::UNKNOWN_CLASS;

            const uint64_t id = cell_id_of(f.source, f.table, f.key);
            const Cell* cur = cells_.find(id);
            const ColdRec* cold = cells_.cold(id);

            // A hashed id that lands on a different (source, table, key) is a COLLISION, and merging
            // two obligations into one row is the one failure this store must never have (§3.2).
            if (cold && (cold->source != f.source || cold->table != f.table || cold->key != f.key))
                return refuse::ID_COLLISION;

            // The ingest law (§4.7): a fact whose source_pos is not above the cell's src_pos_last is
            // refused stale_source; an equal one is a counted duplicate no-op. 64 bits, so a source
            // that crosses 2^32 does not truncate into the past (falsifier 9).
            if (cur) {
                if (f.source_pos < cur->src_pos_last) return refuse::STALE_SOURCE;
                if (f.source_pos == cur->src_pos_last) {
                    ++dup_noops_;                              // counted, applied to nothing
                    continue;
                }
            }

            // Reversibility is DERIVED from the pinned map, never declared (§3.3, QC-4 F2).
            uint64_t window_ns = 0;
            const writ::Rev rev = cd->reversibility_of(f.table, f.op, &window_ns);
            if (!req.declared_reversibility.empty()) {
                writ::Rev declared;
                if (!writ::rev_from_name(req.declared_reversibility, &declared) || declared != rev)
                    return refuse::REVERSIBILITY_MISMATCH;
            }
            // The inverse must be present exactly when required, and is forbidden on exogenous facts:
            // the world's own rows are not ours to undo.
            if (f.exogenous && f.has_inverse) return refuse::REVERSIBILITY_MISMATCH;
            if (!f.exogenous && rev == writ::Rev::ReversibleByInverse && !f.has_inverse)
                return refuse::NO_INVERSE;

            // Check expressions, on the AFTER row, with `now` the entry's stamp.
            Cell after = cur ? *cur : Cell{};
            after.id = id;
            apply_fact_to(&after, f, /*pos*/ 0, now);
            int64_t row[COL_COUNT];
            cell_to_row(after, now, row);
            for (const writ::Check& k : cd->checks) {
                bool pass = false; std::string why;
                if (!writ::eval(k.prog, row, COL_COUNT, &pass, &why)) {
                    // A constraint that cannot be evaluated REFUSES. It never passes by default.
                    *constraint_id = k.id + ":" + why;
                    return refuse::CONSTRAINT_VIOLATED;
                }
                if (!pass) { *constraint_id = k.id; return refuse::CONSTRAINT_VIOLATED; }
            }

            // Exposure cap per class per period, read from the committed fold.
            if (cd->cap_minor_per_period >= 0) {
                const int64_t before = cur ? cur->amount_minor : 0;
                const int64_t added = (f.amount_minor > before) ? (f.amount_minor - before) : 0;
                if (cap_used(f.cls) + added > cd->cap_minor_per_period) {
                    *constraint_id = "cap:" + cd->name;
                    return refuse::CONSTRAINT_VIOLATED;
                }
            }
        }
        return "";
    }

    static void apply_fact_to(Cell* c, const Fact& f, uint64_t pos, uint64_t now_ns) {
        if (c->opened_ns == 0) c->opened_ns = now_ns;
        c->cls          = f.cls;
        c->amount_minor = f.amount_minor;
        c->amount       = (float)((double)f.amount_minor / 100.0);   // rendering only, never summed
        c->due_ns       = f.due_ns;
        c->state        = f.state;
        c->flags        = f.flags;
        c->src_pos_last = f.source_pos;
        if (pos) c->pos_last = pos;
    }

    void apply_facts(const std::vector<Fact>& facts, uint64_t pos, uint64_t now_ns) {
        for (const Fact& f : facts) {
            const uint64_t id = cell_id_of(f.source, f.table, f.key);
            Cell* c = cells_.find(id);
            if (!c) {
                ColdRec rec; rec.source = f.source; rec.table = f.table; rec.key = f.key; rec.first_pos = pos;
                c = &cells_.create(id, rec);
            }
            if (f.source_pos == c->src_pos_last && c->pos_last != 0) { c->pos_last = pos; continue; }
            const int64_t before = c->amount_minor;
            apply_fact_to(c, f, pos, now_ns);
            const int64_t added = (f.amount_minor > before) ? (f.amount_minor - before) : 0;
            cap_used_[f.cls] += added;
        }
    }

    // Replay: the fold is a function of the committed tape and of nothing else.
    //
    // IDEMPOTENT BY POSITION. A delivery is at-least-once everywhere in this design — §4.9's effector,
    // §4.8's subscribe, and a peer resuming after a leader change all re-read a suffix they may have
    // already applied. A fold that adds a fact twice because the tape handed it over twice is not a
    // fold, and an exposure cap is exactly the column where that shows up as money. So: nothing at or
    // below the applied position is applied again. R0's gate, "a redelivered suffix does not
    // double-count", and falsifier 2's second lie, "deliver one twice".
    void apply_committed(const ScannedRow& r) {
        if (applied_any_ && r.hdr.pos <= applied_pos_) { ++redelivered_; return; }
        applied_pos_ = r.hdr.pos;
        applied_any_ = true;
        last_stamp_ = r.hdr.t_epoch_ns;
        if (r.hdr.k == kind::TX) {
            std::string cid, rid; uint64_t basis = 0; std::string rev; std::vector<Fact> facts;
            if (read_tx_body(r.body, &cid, &rid, &basis, &rev, &facts)) {
                apply_facts(facts, r.hdr.pos, r.hdr.t_epoch_ns);
                // The reply cache, rebuilt: a retry after a crash gets the original answer, not a
                // second commit.
                if (!cid.empty() || !rid.empty()) {
                    WriteRes res;
                    res.committed = true; res.pos = r.hdr.pos; res.h = r.h;
                    replies_[cid + "\x1f" + rid] = res;
                }
            }
        } else if (r.hdr.k == kind::TICK) {
            std::string rule; uint64_t wheel = 0;
            if (read_tick_body(r.body, &rule, &wheel)) {
                wheel_pos_ = wheel + 1;
                if (rule == "cap_period") cap_used_.clear();
            }
        }
    }

    void record_refusal(const WriteReq& req, uint64_t now, const std::string& reason,
                        const std::string& constraint_id, WriteRes* res) {
        const uint64_t cell = req.facts.empty() ? 0
            : cell_id_of(req.facts[0].source, req.facts[0].table, req.facts[0].key);
        const std::string key = req.by + "\x1f" + reason + "\x1f" + constraint_id + "\x1f" + std::to_string(cell);
        Window& w = windows_[key];
        std::string err;
        if (w.opened_ns == 0 || now - w.opened_ns > cfg_.refusal_window_ns) {
            if (w.pending) { write_refusal_entry(w, w.pending, &err); w.pending = 0; }
            w.principal = req.by; w.reason = reason; w.constraint_id = constraint_id; w.cell = cell;
            w.digest = attempt_digest(req);
            w.opened_ns = now; w.first_pos = tape_->committed_pos(); w.last_pos = w.first_pos;
            if (write_refusal_entry(w, 1, &err)) { res->refusal_pos = last_refusal_pos_; }
            tape_->commit(&err);
        } else {
            ++w.pending;
            w.last_pos = tape_->committed_pos();
        }
    }

    bool write_refusal_entry(const Window& w, uint64_t count, std::string* err) {
        json::Value b = json::Value::object();
        b.set("attempted_digest", json::Value::str(w.digest));
        b.set("reason", json::Value::str(w.reason));
        b.set("constraint_id", json::Value::str(w.constraint_id));
        b.set("count", json::Value::u64(count));
        b.set("first_pos", json::Value::u64(w.first_pos));
        b.set("last_pos", json::Value::u64(w.last_pos));
        EntryHdr hd; hd.k = kind::REFUSE; hd.by = by::SYSTEM; hd.cell = w.cell;
        Staged st; std::string why;
        json::WriteOpts opts; opts.allow_f64 = false;
        if (!tape_->stage(hd, b, opts, &st, &why)) { if (err) *err = why; return false; }
        last_refusal_pos_ = st.pos;
        last_stamp_ = st.t_epoch_ns;
        applied_pos_ = st.pos; applied_any_ = true;
        ++health_.refusal_entries;
        return true;
    }

    static std::string attempt_digest(const WriteReq& req) {
        Blake2b h; h.init(32);
        h.update(req.client_id); h.update((const uint8_t*)"\x1f", 1);
        h.update(req.request_id); h.update((const uint8_t*)"\x1f", 1);
        h.update(req.by);
        for (const Fact& f : req.facts) {
            h.update((const uint8_t*)"\x1e", 1);
            h.update(f.source); h.update(f.table); h.update(f.key); h.update(f.op);
        }
        uint8_t out[32]; h.finish(out);
        return hex_of(out, 32);
    }

    Tape* tape_ = nullptr;
    const writ::ClassMap* map_ = nullptr;
    TxConfig cfg_;
    std::string dir_;
    CellTable cells_;
    std::unordered_map<uint32_t, int64_t> cap_used_;
    std::unordered_map<std::string, WriteRes> replies_;
    std::unordered_map<std::string, Bucket> buckets_;
    std::map<std::string, Window> windows_;
    Health   health_;
    uint64_t last_stamp_ = 0;
    uint64_t wheel_pos_ = 0;
    uint64_t dup_noops_ = 0;
    uint64_t last_refusal_pos_ = 0;
    uint64_t applied_pos_ = 0;
    bool     applied_any_ = false;
    uint64_t redelivered_ = 0;
    bool     leader_ = true;

public:
    uint64_t duplicate_noops() const { return dup_noops_; }
    uint64_t applied_pos()     const { return applied_pos_; }
    uint64_t redelivered()     const { return redelivered_; }

    // Feed a committed row into the fold from outside — the shape a subscriber or a peer uses, and
    // the one the redelivery oracle drives.
    void deliver(const ScannedRow& r) { apply_committed(r); }

    // FOR THE LIE ARM ONLY. Rewinding the applied position turns this fold into one with no
    // redelivery guard, which is the defect falsifier 2 plants. Nothing in the write path calls it.
    void rewind_applied_for_test(uint64_t pos) { applied_pos_ = pos; applied_any_ = true; }
};

} // namespace tapestry
