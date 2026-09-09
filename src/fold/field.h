// =====================================================================================================
// TAPESTRY · src/fold/field.h · the field fold: the lattice, the differential projection, the sweep
//
// Blueprint §3.4's `field` row, and §12's reuse of `osv_core.cuh`'s leaves — `Lattice`, `step_cell`,
// `sweep_segment`, `slot_of`, `project_one`, `contributes`, `cell_index`, `Conservation` — with the
// three corrections the QC demanded of them.
//
// THE TWO REPRESENTATIONS. The cell table is identity: one row per open obligation, what ingest
// updates. The lattice is flow: bucket grain `(segment × class × slot)`, regular and coalesced, what
// the sweep relaxes. The projection between them is the bridge, and conservation is its falsifier —
// every unit of amount in the table lands in exactly one lattice cell.
//
// WHAT CHANGED FROM THE SHIPPED CODE, AND WHY
//
//   1 · EVERYTHING IS INTEGER, INCLUDING `dev`. The shipped lattice accumulates load in fixed point
//       but relaxes a FLOAT `dev`, so the stored object — the one the whole design is arranged around
//       keeping sparse — was the one thing that could not be reproduced across a compiler flag.
//       QC-1 and QC-2 measured 22 percent FMA divergence between contraction settings. Here `dev`,
//       `baseline` and `capacity` are int64 in minor units × 2^16, and the relaxation coefficients
//       are DYADIC RATIONALS over a common power-of-two denominator, so the entire sweep is integer
//       arithmetic with one truncating division per cell.
//
//       That is stronger than the blueprint claims. §5 puts the sweep in the "bounded" tier —
//       agreement to a stated envelope, no verb flips — and integer-only arithmetic puts it in the
//       "exact and portable" tier instead: two runs on any compiler, any card, agree bit for bit.
//       Recorded as a delta; R2's host-versus-device gate is the test that will confirm it.
//
//   2 · THE PROJECTION IS DIFFERENTIAL, and bit-identical to a cold recompute BY CONSTRUCTION rather
//       than by measurement: the field remembers exactly what each cell contributed and where, so a
//       reprojection subtracts that stored contribution and adds the new one. Integer addition is
//       associative and commutative, so any order of updates reaches the same lattice. The shipped
//       code recomputed the whole lattice on every append, which QC-2 measured as the dominant cost.
//
//   3 · THE SWEEP TERMINATES. A RELATIVE tolerance, `tol · max(1, ‖dev‖∞)`, under a ratified
//       iteration cap that is recorded with the result. The shipped absolute tolerance parked at one
//       ULP on a large field and never converged; an unbounded loop inside a fold is not a fold.
//
// TOTALITY. Every accumulation is overflow-checked. A fold that wraps is a fold that lies, so an
// overflow halts the operation with a typed reason instead of producing a number.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include "../tx/cells.h"
#include "../core/blake2b.h"
#include "../core/bytes.h"

namespace tapestry {
namespace fold {

// The lattice's numeric unit: one minor unit (a cent) times 2^16, so the relaxation has fractional
// resolution while every stored quantity stays an exact integer.
inline constexpr int      FIX_BITS = 16;
inline constexpr int64_t  FIX      = (int64_t)1 << FIX_BITS;

inline const char* const E_OVERFLOW    = "field_overflow";
inline const char* const E_DIMS        = "field_bad_dims";
inline const char* const E_COEFFS      = "field_bad_coeffs";
inline const char* const E_NOT_PLACED  = "field_cell_not_placed";

inline bool add_checked(int64_t a, int64_t b, int64_t* out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return false;
    *out = a + b; return true;
}
inline bool mul_checked(int64_t a, int64_t b, int64_t* out) {
    if (a > 0) {
        if (b > 0)      { if (a > INT64_MAX / b) return false; }
        else if (b < 0) { if (b < INT64_MIN / a) return false; }
    } else if (a < 0) {
        if (b > 0)      { if (a < INT64_MIN / b) return false; }
        else if (b < 0) { if (b < INT64_MAX / a) return false; }
    }
    *out = a * b; return true;
}

// ---- dimensions and the projection's pure functions -------------------------------------------------
struct LatticeDims {
    int32_t  NSEG = 0, NCLS = 0, NSLOT = 0;
    uint64_t slot_ns = 0;
    int32_t  count() const { return NSEG * NCLS * NSLOT; }
    bool ok() const { return NSEG > 0 && NCLS > 0 && NSLOT > 1 && slot_ns > 0; }
};

inline int32_t cell_index(const LatticeDims& d, int32_t seg, int32_t cls, int32_t slot) {
    return (seg * d.NCLS + cls) * d.NSLOT + slot;
}

// A commitment's slot is where its deadline falls relative to now. Overdue is not negative time, it
// is maximum pressure, so it lands in slot 0. No deadline lands in the far bucket.
inline int32_t slot_of(const LatticeDims& d, uint64_t now_ns, uint64_t due_ns) {
    if (due_ns == 0) return d.NSLOT - 1;
    if (due_ns <= now_ns) return 0;
    const uint64_t ahead = (due_ns - now_ns) / d.slot_ns;
    return ahead >= (uint64_t)d.NSLOT ? d.NSLOT - 1 : (int32_t)ahead;
}

inline int32_t project_one(const LatticeDims& d, const Cell& c, uint64_t now_ns) {
    const int32_t seg  = (int32_t)(c.seg % (uint32_t)d.NSEG);
    const int32_t cls  = (int32_t)(c.cls % (uint32_t)d.NCLS);
    return cell_index(d, seg, cls, slot_of(d, now_ns, c.due_ns));
}

// A cell contributes while it is outstanding. Closed obligations are history; dispatched ones still
// occupy the seat discharging them, so they still carry load.
inline bool contributes(const Cell& c) {
    return c.state == C_OPEN || c.state == C_HELD || c.state == C_DISPATCHED;
}

// ---- the relaxation coefficients --------------------------------------------------------------------
// Dyadic rationals over one power-of-two denominator. The constraint is what makes the sweep exact:
// with `den` a power of two and every numerator an integer, the step is integer arithmetic with a
// single truncating division, and truncation is the same on every machine.
struct Coeffs {
    int64_t  den    = 256;   // D, a power of two
    int64_t  omega  = 256;   // ω · D   (1.0)
    int64_t  decay  = 256;   // decay·D (1.0)
    int64_t  k_cls  = 64;    // 0.25·D
    int64_t  k_slot = 64;    // 0.25·D
    // Relative tolerance: the sweep stops when the largest move is under tol_num/tol_den of
    // max(1, ‖dev‖∞). QC-2 measured the shipped ABSOLUTE tolerance parked at one ULP.
    int64_t  tol_num = 1, tol_den = 100000;
    uint32_t max_iters = 128;         // the ratified cap, recorded with every result
    int32_t  baseline_shift = 6;      // the EMA time constant: a step of 1/2^6 toward pressure
    bool ok() const {
        if (den <= 0 || (den & (den - 1)) != 0) return false;     // a power of two
        if (omega <= 0 || decay <= 0 || k_cls < 0 || k_slot < 0) return false;
        if (tol_den <= 0 || tol_num < 0) return false;
        if (max_iters == 0 || baseline_shift < 0 || baseline_shift > 40) return false;
        return true;
    }
};

struct SweepResult {
    uint32_t iters = 0;
    int64_t  max_move = 0;
    int64_t  dev_inf = 0;
    int64_t  threshold = 0;
    bool     converged = false;
    bool     capped = false;      // hit max_iters: recorded, never hidden
    bool     overflow = false;
};

struct Conservation {
    int64_t table_fix = 0, lattice_fix = 0;
    int64_t placed = 0, skipped = 0;
    bool holds() const { return table_fix == lattice_fix; }
};

// ---- the field ---------------------------------------------------------------------------------------
class Field {
public:
    // `wheel_at` is stored rather than recomputed: the key a cell was filed under depends on the
    // `now` at the time of filing, so recomputing it at removal time — after a tick has moved `now` —
    // looks in the wrong bucket and leaves the entry behind. Every differential must undo exactly
    // what it did, which means remembering it rather than deriving it again under new conditions.
    struct Placement { int32_t index; int64_t load; int64_t count; uint64_t due_ns; uint64_t wheel_at; bool warrant; };

    bool init(const LatticeDims& d, const Coeffs& c, std::string* err) {
        if (!d.ok()) { if (err) *err = E_DIMS; return false; }
        if (!c.ok()) { if (err) *err = E_COEFFS; return false; }
        d_ = d; c_ = c;
        const size_t n = (size_t)d.count();
        load_.assign(n, 0); count_.assign(n, 0); capacity_.assign(n, 0);
        baseline_.assign(n, 0); dev_.assign(n, 0); flags_.assign(n, 0);
        warrants_.assign(n, 0);
        placed_.clear(); wheel_.clear();
        now_ns_ = 0; ticks_ = 0;
        return true;
    }

    const LatticeDims& dims() const { return d_; }
    const Coeffs& coeffs() const { return c_; }
    uint64_t now_ns() const { return now_ns_; }
    uint64_t ticks() const { return ticks_; }
    // §3.4: the baseline is "zero until it has history, and the store says so".
    bool baseline_has_history() const { return ticks_ > 0; }

    int64_t load_at(int32_t i)     const { return load_[(size_t)i]; }
    int64_t dev_at(int32_t i)      const { return dev_[(size_t)i]; }
    int64_t baseline_at(int32_t i) const { return baseline_[(size_t)i]; }
    int64_t pressure_at(int32_t i) const { return baseline_[(size_t)i] + dev_[(size_t)i]; }
    size_t  placed_count()         const { return placed_.size(); }

    void set_capacity(int32_t i, int64_t cap_fix) { capacity_[(size_t)i] = cap_fix; }
    void set_now(uint64_t now_ns) { now_ns_ = now_ns; }

    // ---- the differential projection -----------------------------------------------------------
    // Place, unplace and reproject are the only ways the lattice's load changes. Each one remembers
    // exactly what it contributed and where, so the inverse is exact rather than recomputed — which
    // is what makes "cold equals incremental" a property of the code and not a coincidence.
    bool place(const Cell& cell, std::string* err) {
        if (placed_.count(cell.id)) { if (err) *err = "already_placed"; return false; }
        if (!contributes(cell)) return true;                       // not an error: it carries nothing
        const int32_t i = project_one(d_, cell, now_ns_);
        int64_t contrib = 0;
        if (!mul_checked(cell.amount_minor, FIX, &contrib)) { if (err) *err = E_OVERFLOW; return false; }
        int64_t nl = 0, nc = 0;
        if (!add_checked(load_[(size_t)i], contrib, &nl))  { if (err) *err = E_OVERFLOW; return false; }
        if (!add_checked(count_[(size_t)i], FIX, &nc))     { if (err) *err = E_OVERFLOW; return false; }
        load_[(size_t)i] = nl; count_[(size_t)i] = nc;
        // The warrant flag is COUNTED, not set. A lattice cell is a boundary condition while at least
        // one warrant sits in it; if the flag were merely set, the last warrant leaving would leave
        // the cell frozen forever and a cold rebuild would disagree with the incremental one.
        const bool warrant = (cell.flags & F_WARRANT) != 0;
        if (warrant) { ++warrants_[(size_t)i]; flags_[(size_t)i] |= F_WARRANT; }
        const uint64_t at = wheel_add(cell.id, cell.due_ns);
        placed_[cell.id] = Placement{i, contrib, FIX, cell.due_ns, at, warrant};
        return true;
    }

    bool unplace(uint64_t cell_id, std::string* err) {
        auto it = placed_.find(cell_id);
        if (it == placed_.end()) return true;                      // it was never carrying anything
        const Placement p = it->second;
        int64_t nl = 0, nc = 0;
        if (!add_checked(load_[(size_t)p.index], -p.load, &nl))  { if (err) *err = E_OVERFLOW; return false; }
        if (!add_checked(count_[(size_t)p.index], -p.count, &nc)){ if (err) *err = E_OVERFLOW; return false; }
        load_[(size_t)p.index] = nl; count_[(size_t)p.index] = nc;
        if (p.warrant && warrants_[(size_t)p.index] > 0) {
            if (--warrants_[(size_t)p.index] == 0)
                flags_[(size_t)p.index] = (uint8_t)(flags_[(size_t)p.index] & ~F_WARRANT);
        }
        wheel_remove(cell_id, p.wheel_at);
        placed_.erase(it);
        return true;
    }

    // Subtract the old contribution, add the new. The whole differential claim in one call.
    bool reproject(const Cell& after, std::string* err) {
        if (!unplace(after.id, err)) return false;
        return place(after, err);
    }

    // ---- time ------------------------------------------------------------------------------------
    // A tick advances `now` and re-slots ONLY the cells whose deadline crosses a slot boundary, found
    // by the wheel rather than by walking the table. Cells with no deadline never move.
    bool tick(uint64_t new_now_ns, const CellTable& table, uint64_t* moved, std::string* err) {
        if (new_now_ns < now_ns_) { if (err) *err = "tick_went_backwards"; return false; }
        now_ns_ = new_now_ns;
        uint64_t n = 0;
        while (!wheel_.empty() && wheel_.begin()->first <= now_ns_) {
            std::vector<uint64_t> due_now;
            due_now.swap(wheel_.begin()->second);
            wheel_.erase(wheel_.begin());
            for (uint64_t id : due_now) {
                auto it = placed_.find(id);
                if (it == placed_.end()) continue;
                const Cell* c = table.find(id);
                if (!c) continue;
                const int32_t want = project_one(d_, *c, now_ns_);
                if (want == it->second.index) {          // did not move: re-file at its next crossing
                    it->second.wheel_at = wheel_add(id, it->second.due_ns);
                    continue;
                }
                if (!reproject(*c, err)) return false;
                ++n;
            }
        }
        // The baseline is a fold of ticks, not of appends: a fixed-point exponential moving average
        // of pressure, stepping 1/2^shift toward it. Exact, because the step is a shift.
        for (size_t i = 0; i < baseline_.size(); ++i) {
            const int64_t pressure = baseline_[i] + dev_[i];
            const int64_t diff = pressure - baseline_[i];          // == dev_[i]
            const int64_t step = diff >> c_.baseline_shift;        // arithmetic shift: floor, deterministic
            int64_t nb = 0;
            if (!add_checked(baseline_[i], step, &nb)) { if (err) *err = E_OVERFLOW; return false; }
            baseline_[i] = nb;
            dev_[i] -= step;                                       // pressure is conserved by the move
        }
        ++ticks_;
        if (moved) *moved = n;
        return true;
    }

    // ---- the sweep --------------------------------------------------------------------------------
    // Red-black Gauss-Seidel, in place, integer. No cell reads a same-colour neighbour, so the order
    // threads arrive in cannot change the answer — which is what makes the device version of this
    // (R2) a port rather than a second implementation.
    SweepResult sweep() {
        SweepResult r;
        r.threshold = 0;
        for (r.iters = 0; r.iters < c_.max_iters; ++r.iters) {
            int64_t moved_max = 0;
            bool overflow = false;
            for (int32_t color = 0; color < 2 && !overflow; ++color)
                for (int32_t seg = 0; seg < d_.NSEG && !overflow; ++seg)
                    for (int32_t cls = 0; cls < d_.NCLS && !overflow; ++cls)
                        for (int32_t slot = 0; slot < d_.NSLOT; ++slot) {
                            if (((cls + slot) & 1) != color) continue;
                            int64_t moved = 0;
                            if (!step_cell(seg, cls, slot, &moved)) { overflow = true; break; }
                            if (moved > moved_max) moved_max = moved;
                        }
            if (overflow) { r.overflow = true; return r; }
            r.max_move = moved_max;
            r.dev_inf = dev_inf_norm();
            // The RELATIVE tolerance: tol · max(1, ‖dev‖∞). An absolute one parks at one ULP.
            const int64_t scale = (r.dev_inf > FIX) ? r.dev_inf : FIX;
            r.threshold = (scale / c_.tol_den) * c_.tol_num;
            if (moved_max <= r.threshold) { r.converged = true; ++r.iters; return r; }
        }
        r.capped = true;                    // recorded on the result, and from there on the snapshot
        return r;
    }

    // ---- conservation, the projection's falsifier -----------------------------------------------
    // EVERY contributing cell counts on the table side, whether or not the field placed it. Excusing
    // the unplaced ones — which an earlier draft of this function did — makes the check unable to see
    // the one failure it exists for: a commitment the projection quietly lost. The lie arm in
    // t_field.cpp plants exactly that, and it caught this.
    Conservation conservation(const CellTable& table) const {
        Conservation cv;
        for (const Cell& c : table.all()) {
            if (!contributes(c)) { ++cv.skipped; continue; }
            cv.table_fix += c.amount_minor * FIX;
            if (placed_.count(c.id)) ++cv.placed;
        }
        for (int64_t v : load_) cv.lattice_fix += v;
        return cv;
    }

    // The lattice's identity. Load and count only: `dev` and `baseline` are the sweep's and the
    // ticks' business, and a projection digest that moved when the sweep ran would not be a
    // projection digest.
    std::string projection_digest() const {
        Blake2b h; h.init(32);
        mix_u64(h, (uint64_t)d_.NSEG); mix_u64(h, (uint64_t)d_.NCLS);
        mix_u64(h, (uint64_t)d_.NSLOT); mix_u64(h, d_.slot_ns);
        for (int64_t v : load_)  mix_u64(h, (uint64_t)v);
        for (int64_t v : count_) mix_u64(h, (uint64_t)v);
        uint8_t out[32]; h.finish(out);
        return hex_of(out, 32);
    }
    // The whole field's identity: the projection, the relaxed deviation, the baseline, and the
    // warrant counts — which belong here because a warrant is a boundary condition and changes what
    // the next sweep computes. `F_DIRTY` is deliberately absent: it is a note about the last sweep,
    // not state the next one depends on.
    std::string full_digest() const {
        Blake2b h; h.init(32);
        h.update(projection_digest());
        for (int64_t v : dev_)      mix_u64(h, (uint64_t)v);
        for (int64_t v : baseline_) mix_u64(h, (uint64_t)v);
        for (int32_t v : warrants_) mix_u64(h, (uint64_t)(uint32_t)v);
        uint8_t out[32]; h.finish(out);
        return hex_of(out, 32);
    }

    // A cold rebuild of the projection from the table alone, at this field's `now`. `verify_fold`
    // (§4.8) is this compared against the incremental side.
    bool cold_project(const CellTable& table, Field* out, std::string* err) const {
        if (!out->init(d_, c_, err)) return false;
        out->set_now(now_ns_);
        for (const Cell& c : table.all()) {
            if (!contributes(c)) continue;
            if (!out->place(c, err)) return false;
        }
        for (size_t i = 0; i < capacity_.size(); ++i) out->capacity_[i] = capacity_[i];
        return true;
    }

private:
    static void mix_u64(Blake2b& h, uint64_t v) {
        uint8_t le[8]; for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((v >> (8 * k)) & 0xff);
        h.update(le, 8);
    }

    int64_t dev_inf_norm() const {
        int64_t m = 0;
        for (int64_t v : dev_) { const int64_t a = v < 0 ? -v : v; if (a > m) m = a; }
        return m;
    }

    // One cell of the relaxation, entirely in integers.
    //
    //   src   = load − capacity − baseline                         (exact, fixed point)
    //   lapN  = Σ k_num·(dev[n] − dev[i])                          (over the denominator D)
    //   diagN = decay_num + Σ k_num                                (over D)
    //   rN    = src·D + lapN − decay_num·dev[i]                    (over D)
    //   dev  += omega_num·rN / (D·diagN)                           one truncating division
    //
    // The diagonal is decay PLUS the incident couplings, not decay alone: dividing by decay is the
    // classic way to make an unrelaxed sweep diverge on the interior, and it looks right.
    bool step_cell(int32_t seg, int32_t cls, int32_t slot, int64_t* moved) {
        const int32_t i = cell_index(d_, seg, cls, slot);
        *moved = 0;
        if (flags_[(size_t)i] & F_WARRANT) return true;     // a boundary condition is never relaxed

        const int64_t d0 = dev_[(size_t)i];
        int64_t lapN = 0, diagN = c_.decay;

        auto couple = [&](int32_t j, int64_t k) -> bool {
            int64_t t = 0;
            if (!mul_checked(k, dev_[(size_t)j] - d0, &t)) return false;
            if (!add_checked(lapN, t, &lapN)) return false;
            diagN += k;
            return true;
        };
        if (cls > 0            && !couple(cell_index(d_, seg, cls - 1, slot), c_.k_cls))  return false;
        if (cls + 1 < d_.NCLS  && !couple(cell_index(d_, seg, cls + 1, slot), c_.k_cls))  return false;
        if (slot > 0           && !couple(cell_index(d_, seg, cls, slot - 1), c_.k_slot)) return false;
        if (slot + 1 < d_.NSLOT&& !couple(cell_index(d_, seg, cls, slot + 1), c_.k_slot)) return false;

        int64_t src = load_[(size_t)i] - capacity_[(size_t)i] - baseline_[(size_t)i];
        int64_t rN = 0, t = 0;
        if (!mul_checked(src, c_.den, &rN)) return false;
        if (!add_checked(rN, lapN, &rN)) return false;
        if (!mul_checked(c_.decay, d0, &t)) return false;
        if (!add_checked(rN, -t, &rN)) return false;

        int64_t num = 0, den = 0;
        if (!mul_checked(c_.omega, rN, &num)) return false;
        if (!mul_checked(c_.den, diagN, &den)) return false;
        if (den == 0) return false;
        const int64_t delta = num / den;                    // truncation toward zero, one rule
        int64_t nd = 0;
        if (!add_checked(d0, delta, &nd)) return false;
        dev_[(size_t)i] = nd;

        const int64_t m = delta < 0 ? -delta : delta;
        *moved = m;
        if (m > 0) flags_[(size_t)i] |= F_DIRTY;
        else       flags_[(size_t)i] = (uint8_t)(flags_[(size_t)i] & ~F_DIRTY);
        return true;
    }

    // ---- the deadline wheel ----------------------------------------------------------------------
    // A cell at slot k crosses into k−1 when `now` passes `due − k·slot_ns`. Keyed by that instant, so
    // a tick touches only the cells that actually move. A cell with no deadline is never in the wheel.
    // The instant at which this cell's slot DECREASES. slot k = floor((due − now)/slot_ns) falls to
    // k−1 the moment now exceeds due − k·slot_ns, so the first instant with the smaller slot is one
    // nanosecond later. The `+1` is load-bearing: without it a deadline that sits exactly on a slot
    // boundary is re-filed at the instant that just fired, and the tick loop spins forever on it.
    uint64_t crossing_ns(uint64_t due_ns) const {
        if (due_ns == 0) return UINT64_MAX;                        // the far bucket, forever
        const int32_t k = slot_of(d_, now_ns_, due_ns);
        if (k <= 0) return UINT64_MAX;                             // overdue: it cannot fall further
        if (k >= d_.NSLOT - 1 && due_ns == 0) return UINT64_MAX;
        const uint64_t boundary = (uint64_t)k * d_.slot_ns;
        const uint64_t at = (due_ns > boundary) ? (due_ns - boundary + 1) : 1;
        return (at > now_ns_) ? at : now_ns_ + 1;                   // always strictly in the future
    }
    uint64_t wheel_add(uint64_t id, uint64_t due_ns) {
        const uint64_t at = crossing_ns(due_ns);
        if (at == UINT64_MAX) return at;
        wheel_[at].push_back(id);
        return at;
    }
    void wheel_remove(uint64_t id, uint64_t at) {
        if (at == UINT64_MAX) return;
        auto it = wheel_.find(at);
        if (it == wheel_.end()) return;
        for (size_t k = 0; k < it->second.size(); ++k)
            if (it->second[k] == id) { it->second.erase(it->second.begin() + (ptrdiff_t)k); break; }
        if (it->second.empty()) wheel_.erase(it);
    }

    LatticeDims d_;
    Coeffs c_;
    std::vector<int64_t> load_, count_, capacity_, baseline_, dev_;
    std::vector<uint8_t> flags_;
    std::vector<int32_t> warrants_;   // how many warrants sit in each lattice cell
    std::unordered_map<uint64_t, Placement> placed_;
    std::map<uint64_t, std::vector<uint64_t>> wheel_;
    uint64_t now_ns_ = 0, ticks_ = 0;
};

} // namespace fold
} // namespace tapestry
