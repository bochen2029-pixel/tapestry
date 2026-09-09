// =====================================================================================================
// TAPESTRY · src/tx/cells.h · the hot row and the transactor's own fold of it
//
// Blueprint §3.2. 128 bytes, two cache lines, because three clocks must sit on it and 64 bytes hold
// one. The struct below is §3.2's, field for field, with one change taken out of `reserved`:
//
//   int64_t amount_minor — money in MINOR UNITS (cents), the authoritative quantity.
//
// §3.2 gives `float amount`, inherited from osv's `Commitment`. A float cannot be the quantity an
// exposure cap sums: §4.7 evaluates caps "as a fold over committed facts", §5 claims integer folds are
// "exact and portable ... across host and device, cards and compilers", and QC-1/QC-2 measured 22
// percent FMA divergence between contraction settings. A cap fold over floats is therefore not a fold
// by this store's own definition. `amount` stays for the judge's rendering; `amount_minor` is what the
// writ counts, and it costs 8 of the 24 reserved bytes, so the row is still 128. Recorded as a delta.
//
// The cold side holds `(source, table, key)` in full so a hashed-id collision is DETECTED rather than
// silently merging two obligations into one row (§3.2, refusal `id_collision`).
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include "../core/blake2b.h"
#include "../core/bytes.h"

namespace tapestry {

// The one byte each of state, flags and verb, carried over from `osv_core.cuh`'s vocabulary, which
// §12 keeps. `F_BLOCKED` in particular is preserved by name: a cell whose dependency is unmet must
// hold, and acting on it is a lie.
enum CState : uint8_t {
    C_OPEN       = 0,   // outstanding, nothing decided this cycle
    C_HELD       = 1,   // judged, deliberately not acted on — the hold is a record, never a silence
    C_DISPATCHED = 2,   // handed to a worker, in flight
    C_ESCALATED  = 3,   // handed to a named human with a brief
    C_CLOSED     = 4    // discharged; leaves the cell table at the next compaction
};
enum CFlag : uint8_t {
    F_WARRANT   = 1u << 0,  // irreversible: a human signs it. A boundary condition, never relaxed.
    F_EXOGENOUS = 1u << 1,  // waiting on a counterparty we do not control
    F_BLOCKED   = 1u << 2,  // a dependency is unmet
    F_SHADOW    = 1u << 3,  // in the randomised shadow stratum: judged, never published
    F_EXPLORE   = 1u << 4,  // exploration quota
    F_CONTENT   = 1u << 5,  // discharging it requires words
    F_DIRTY     = 1u << 6,  // touched since the last sweep
    F_PINNED    = 1u << 7   // ingest says this is authoritative; the solver may not move it
};
enum Verb : uint8_t {
    V_HOLD = 0, V_ACT = 1, V_WORK = 2, V_FETCH = 3, V_ESCALATE = 4
};

struct Cell {                     // 128 bytes, resident in HBM and in the transactor's fold
    uint64_t id;                  // blake2b-128 of (source ‖ 0x1f ‖ table ‖ 0x1f ‖ key), low 64 bits; 0 reserved
    uint64_t opened_ns;           // t_epoch_ns
    uint64_t due_ns;              // t_epoch_ns; 0 = no deadline
    uint64_t blocked_by;          // cell id or 0
    uint64_t pos_last;            // tape position of the last entry of ANY kind on this cell
    uint64_t src_pos_last;        // the source system's revision that last touched it: the world's clock
    uint64_t pos_judged;          // the peer's applied position when the current margin was produced
    float    amount;              // the judge's rendering value
    float    margin;              // last judged
    float    clearance;           // signed distance of the margin from the nearest gate threshold
    uint32_t cls, seg, seat;
    uint8_t  state, flags, verb, gear;
    uint64_t root_pin;            // the root the margin was judged under
    int64_t  amount_minor;        // AUTHORITATIVE money, minor units — what the caps fold sums
    uint8_t  reserved[24];
};
// §3.2 writes this same assertion — and the struct AS PRINTED THERE fails it. Counting: 56 bytes of
// clocks, 12 of floats, 12 of uint32, 4 of uint8, then `root_pin` needs 8-byte alignment, so four
// bytes of padding appear after `gear` that the blueprint's arithmetic does not account for. §3.2's
// Cell is 120 bytes, not 128, and its static_assert does not compile. `amount_minor` occupies exactly
// the eight bytes that were missing, so the row is now the two cache lines the blueprint claims.
// A second delta for the receipt, found by compiling the specification.
static_assert(sizeof(Cell) == 128, "the hot row is two cache lines");

// §3.2: blake2b-128 of (source ‖ 0x1f ‖ table ‖ 0x1f ‖ key) truncated to 64 bits; 0 is reserved.
inline uint64_t cell_id_of(const std::string& source, const std::string& table, const std::string& key) {
    Blake2b h; h.init(16);
    h.update(source); h.update((const uint8_t*)"\x1f", 1);
    h.update(table);  h.update((const uint8_t*)"\x1f", 1);
    h.update(key);
    uint8_t out[16]; h.finish(out);
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = (v << 8) | (uint64_t)out[i];   // low 8 bytes, little-endian
    return v ? v : 1;                                              // 0 is reserved, never issued
}

// The cold record: what the hot row hashed away, kept so a collision is a refusal and not a merge.
struct ColdRec {
    std::string source, table, key;
    uint64_t    first_pos = 0;
};

// ---- the constraint-visible row --------------------------------------------------------------------
// The columns a check expression may read, in a fixed order that is part of every program's hash.
// `now_ns` is the ENTRY's epoch stamp, never a host clock: §3.4, "every fold reads `now` as the
// `t_epoch_ns` of the entry being folded".
enum CellCol : int {
    COL_ID = 0, COL_OPENED_NS, COL_DUE_NS, COL_BLOCKED_BY, COL_POS_LAST, COL_SRC_POS_LAST,
    COL_POS_JUDGED, COL_AMOUNT_MINOR, COL_CLS, COL_SEG, COL_SEAT, COL_STATE, COL_FLAGS,
    COL_VERB, COL_GEAR, COL_NOW_NS, COL_COUNT
};
inline const char* const CELL_COLUMN_NAMES[COL_COUNT] = {
    "id", "opened_ns", "due_ns", "blocked_by", "pos_last", "src_pos_last",
    "pos_judged", "amount_minor", "cls", "seg", "seat", "state", "flags",
    "verb", "gear", "now_ns"
};

inline void cell_to_row(const Cell& c, uint64_t now_ns, int64_t* row) {
    row[COL_ID]           = (int64_t)c.id;
    row[COL_OPENED_NS]    = (int64_t)c.opened_ns;
    row[COL_DUE_NS]       = (int64_t)c.due_ns;
    row[COL_BLOCKED_BY]   = (int64_t)c.blocked_by;
    row[COL_POS_LAST]     = (int64_t)c.pos_last;
    row[COL_SRC_POS_LAST] = (int64_t)c.src_pos_last;
    row[COL_POS_JUDGED]   = (int64_t)c.pos_judged;
    row[COL_AMOUNT_MINOR] = c.amount_minor;
    row[COL_CLS]          = (int64_t)c.cls;
    row[COL_SEG]          = (int64_t)c.seg;
    row[COL_SEAT]         = (int64_t)c.seat;
    row[COL_STATE]        = (int64_t)c.state;
    row[COL_FLAGS]        = (int64_t)c.flags;
    row[COL_VERB]         = (int64_t)c.verb;
    row[COL_GEAR]         = (int64_t)c.gear;
    row[COL_NOW_NS]       = (int64_t)now_ns;
}

// ---- the table -------------------------------------------------------------------------------------
// One writer per cell through the transactor's serialization (§3.2), so no lock lives here.
class CellTable {
public:
    Cell* find(uint64_t id) {
        auto it = idx_.find(id);
        return it == idx_.end() ? nullptr : &cells_[it->second];
    }
    const Cell* find(uint64_t id) const {
        auto it = idx_.find(id);
        return it == idx_.end() ? nullptr : &cells_[it->second];
    }
    const ColdRec* cold(uint64_t id) const {
        auto it = idx_.find(id);
        return it == idx_.end() ? nullptr : &cold_[it->second];
    }
    Cell& create(uint64_t id, const ColdRec& rec) {
        idx_[id] = cells_.size();
        Cell c{};
        c.id = id;
        cells_.push_back(c);
        cold_.push_back(rec);
        return cells_.back();
    }
    size_t size() const { return cells_.size(); }
    const std::vector<Cell>& all() const { return cells_; }

    // A digest of the whole table, for falsifier 11 (two folds, one answer) and for verify_fold.
    // Order-independent: each row's digest is mixed in by XOR, so the table's iteration order — an
    // implementation detail of a hash map — cannot change the answer.
    std::string digest() const {
        uint8_t acc[32] = {0};
        for (size_t i = 0; i < cells_.size(); ++i) {
            Blake2b h; h.init(32);
            h.update((const uint8_t*)&cells_[i], sizeof(Cell));
            uint8_t out[32]; h.finish(out);
            for (int k = 0; k < 32; ++k) acc[k] ^= out[k];
        }
        Blake2b f; f.init(32);
        f.update(acc, 32);
        const uint64_t n = (uint64_t)cells_.size();
        uint8_t le[8]; for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((n >> (8 * k)) & 0xff);
        f.update(le, 8);                                   // the count, so a lost row cannot cancel out
        uint8_t out[32]; f.finish(out);
        return hex_of(out, 32);
    }

private:
    std::vector<Cell>    cells_;
    std::vector<ColdRec> cold_;
    std::unordered_map<uint64_t, size_t> idx_;
};

} // namespace tapestry
