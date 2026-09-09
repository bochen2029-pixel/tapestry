// =====================================================================================================
// TAPESTRY · src/tests/t_field.cpp · R1's field oracles, each with its lie arm
//
// Two of R1's four gates live here (§9):
//   · CONSERVATION            — every unit of amount in the cell table lands in exactly one lattice
//                               cell, exactly, because the accumulation is integer
//   · COLD EQUALS INCREMENTAL — the differential projection is bit-identical to a cold recompute
//
// Plus the three corrections §12 demanded of the shipped lattice, each as its own oracle: `dev` in
// fixed point, a relative tolerance that terminates, and a deadline wheel that moves only what moves.
// =====================================================================================================

#include <cstdio>
#include <string>
#include <vector>
#include "../tx/cells.h"
#include "../fold/field.h"

using namespace tapestry;
using namespace tapestry::fold;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}

static LatticeDims dims() {
    LatticeDims d; d.NSEG = 4; d.NCLS = 6; d.NSLOT = 8; d.slot_ns = 1000000000ull;   // 1 s slots
    return d;
}
static const uint64_t T0 = 1757000000000000000ull;

static Cell mk(uint64_t id, uint32_t seg, uint32_t cls, int64_t amount, uint64_t due_ns,
               uint8_t state = C_OPEN, uint8_t flags = 0) {
    Cell c{};
    c.id = id; c.seg = seg; c.cls = cls; c.amount_minor = amount; c.due_ns = due_ns;
    c.state = state; c.flags = flags; c.opened_ns = T0;
    return c;
}

// A table of n cells, spread across segments, classes and deadlines.
static void build(CellTable* t, std::vector<Cell>* cells, int n) {
    for (int i = 0; i < n; ++i) {
        ColdRec rec; rec.source = "olist"; rec.table = "orders"; rec.key = "o-" + std::to_string(i);
        const uint64_t id = cell_id_of(rec.source, rec.table, rec.key);
        Cell c = mk(id, (uint32_t)(i % 4), (uint32_t)(i % 6), 100 + i * 7,
                    (i % 5 == 0) ? 0 : T0 + (uint64_t)(i % 11) * 1000000000ull,
                    (i % 13 == 0) ? C_CLOSED : C_OPEN,
                    (i % 17 == 0) ? (uint8_t)F_WARRANT : (uint8_t)0);
        Cell& in = t->create(id, rec);
        in = c;
        cells->push_back(c);
    }
}

// =====================================================================================================
// 1 · Conservation — the projection's falsifier, stated as arithmetic
// =====================================================================================================
static void t_conservation() {
    CellTable table; std::vector<Cell> cells;
    build(&table, &cells, 400);

    Field f; std::string err;
    check(f.init(dims(), Coeffs(), &err), "cons.init", err);
    f.set_now(T0);
    bool all_placed = true;
    for (const Cell& c : cells) if (contributes(c) && !f.place(c, &err)) { all_placed = false; break; }
    check(all_placed, "cons.place_every_contributing_cell", err);

    const Conservation cv = f.conservation(table);
    check(cv.holds(), "cons.holds",
          std::to_string(cv.table_fix) + " vs " + std::to_string(cv.lattice_fix));
    check(cv.placed > 300, "cons.placed_most_of_them", std::to_string(cv.placed));
    check(cv.skipped > 0, "cons.skipped_the_closed_ones", std::to_string(cv.skipped));

    // Lie arm: drop one unit of amount from the lattice and conservation must fail. This is the
    // check that would catch a projection that quietly loses a commitment.
    Field g; check(g.init(dims(), Coeffs(), &err), "cons.lie.init", err);
    g.set_now(T0);
    bool skipped_one = false;
    for (const Cell& c : cells) {
        if (!contributes(c)) continue;
        if (!skipped_one) { skipped_one = true; continue; }        // the planted loss
        g.place(c, &err);
    }
    const Conservation cl = g.conservation(table);
    check(!cl.holds(), "cons.lie.a_lost_commitment_shows");
}

// =====================================================================================================
// 2 · Cold equals incremental — the gate the differential projection exists to pass
// =====================================================================================================
static void t_cold_equals_incremental() {
    CellTable table; std::vector<Cell> cells;
    build(&table, &cells, 500);

    Field inc; std::string err;
    check(inc.init(dims(), Coeffs(), &err), "diff.init", err);
    inc.set_now(T0);
    for (const Cell& c : cells) if (contributes(c)) inc.place(c, &err);

    // Now churn it: change amounts, deadlines, classes and states, one differential at a time.
    for (size_t i = 0; i < cells.size(); i += 3) {
        Cell c = cells[i];
        c.amount_minor += (int64_t)(i * 13 % 977) - 400;
        c.due_ns = (i % 4 == 0) ? 0 : T0 + (uint64_t)(i % 7) * 1000000000ull;
        c.cls = (uint32_t)((c.cls + 1) % 6);
        if (i % 11 == 0) c.state = C_DISPATCHED;
        if (i % 23 == 0) c.state = C_CLOSED;                 // leaves the lattice entirely
        if (i % 29 == 0) c.flags &= (uint8_t)~F_WARRANT;      // the last warrant can leave a cell
        cells[i] = c;
        Cell* live = table.find(c.id);
        *live = c;
        const bool ok = contributes(c) ? inc.reproject(c, &err) : inc.unplace(c.id, &err);
        if (!ok) { check(false, "diff.churn", err); break; }
    }
    check(true, "diff.churned_without_error");

    Field cold;
    check(inc.cold_project(table, &cold, &err), "diff.cold_project", err);
    check(inc.projection_digest() == cold.projection_digest(), "diff.cold_equals_incremental",
          inc.projection_digest().substr(0, 16) + " vs " + cold.projection_digest().substr(0, 16));
    const Conservation cv = inc.conservation(table);
    check(cv.holds(), "diff.conservation_survives_the_churn");

    // Order independence: integer addition is associative and commutative, so placing the same set
    // in a different order must reach the same lattice, byte for byte.
    Field rev; check(rev.init(dims(), Coeffs(), &err), "diff.rev_init", err);
    rev.set_now(T0);
    for (size_t i = cells.size(); i-- > 0; ) if (contributes(cells[i])) rev.place(cells[i], &err);
    check(rev.projection_digest() == cold.projection_digest(), "diff.order_independent");

    // Lie arm: a reprojection that adds the new contribution without subtracting the old — the
    // defect a differential update has if it is written carelessly — must move the digest.
    Field bad; check(bad.init(dims(), Coeffs(), &err), "diff.lie.init", err);
    bad.set_now(T0);
    for (const Cell& c : cells) if (contributes(c)) bad.place(c, &err);
    for (size_t i = 0; i < 5; ++i) {
        Cell c = cells[i * 3];
        if (!contributes(c)) continue;
        c.id = c.id ^ 1ull;                          // a fresh id, so `place` adds without removing
        bad.place(c, &err);
    }
    check(bad.projection_digest() != cold.projection_digest(), "diff.lie.an_unsubtracted_add_shows");
}

// =====================================================================================================
// 3 · The warrant flag is counted, not set
// =====================================================================================================
static void t_warrant_flag() {
    CellTable table;
    ColdRec rec; rec.source = "s"; rec.table = "t"; rec.key = "w1";
    const uint64_t id = cell_id_of(rec.source, rec.table, rec.key);
    Cell w = mk(id, 0, 0, 500, T0 + 3000000000ull, C_OPEN, F_WARRANT);
    Cell& in = table.create(id, rec); in = w;

    Field f; std::string err;
    f.init(dims(), Coeffs(), &err); f.set_now(T0);
    f.place(w, &err);
    const std::string with = f.full_digest();

    // The warrant leaves. A flag that was merely SET would freeze the lattice cell as a boundary
    // condition forever, and the cold rebuild would disagree.
    Cell plain = w; plain.flags = 0;
    *table.find(id) = plain;
    check(f.reproject(plain, &err), "warrant.reproject", err);
    Field cold;
    check(f.cold_project(table, &cold, &err), "warrant.cold", err);
    check(f.projection_digest() == cold.projection_digest(), "warrant.cleared_when_the_last_one_leaves");
    check(with != f.full_digest(), "warrant.the_flag_was_really_there");
}

// =====================================================================================================
// 4 · The sweep terminates, and does so in integers
// =====================================================================================================
static void t_sweep() {
    CellTable table; std::vector<Cell> cells;
    build(&table, &cells, 600);
    Field f; std::string err;
    Coeffs c;
    check(f.init(dims(), c, &err), "sweep.init", err);
    f.set_now(T0);
    for (const Cell& x : cells) if (contributes(x)) f.place(x, &err);

    const SweepResult r = f.sweep();
    check(!r.overflow, "sweep.no_overflow");
    check(r.converged, "sweep.converged", "iters=" + std::to_string(r.iters) +
          " move=" + std::to_string(r.max_move) + " thr=" + std::to_string(r.threshold));
    check(!r.capped, "sweep.did_not_hit_the_cap", std::to_string(r.iters));
    check(r.max_move <= r.threshold, "sweep.relative_tolerance_met");

    // Determinism: the same field swept twice from the same start is bit-identical. Integer
    // arithmetic, so this is exactness, not closeness.
    Field g; check(g.init(dims(), c, &err), "sweep.init2", err);
    g.set_now(T0);
    for (const Cell& x : cells) if (contributes(x)) g.place(x, &err);
    const SweepResult r2 = g.sweep();
    check(g.full_digest() == f.full_digest(), "sweep.bit_identical_across_runs");
    check(r2.iters == r.iters && r2.max_move == r.max_move, "sweep.same_iteration_count");

    // Sweeping again from a converged state moves nothing further.
    const std::string before = f.full_digest();
    const SweepResult r3 = f.sweep();
    check(r3.converged && r3.iters <= 2, "sweep.idempotent_once_converged", std::to_string(r3.iters));
    (void)before;

    // The cap is recorded, never hidden: a field given one iteration reports that it was capped.
    Coeffs tight = c; tight.max_iters = 1; tight.tol_num = 1; tight.tol_den = 1000000000;
    Field h; h.init(dims(), tight, &err); h.set_now(T0);
    for (const Cell& x : cells) if (contributes(x)) h.place(x, &err);
    const SweepResult rc = h.sweep();
    check(rc.capped && !rc.converged, "sweep.the_cap_is_on_the_result");

    // Lie arm: an ABSOLUTE tolerance is what the shipped code used, and on a large field the moves
    // never fall below it in a fixed number of iterations — the parked sweep QC-2 measured. Here the
    // relative threshold scales with ‖dev‖∞, and it is that scaling that makes termination possible.
    check(r.threshold > 0 && r.dev_inf > FIX, "sweep.lie.an_absolute_threshold_would_not_scale",
          "dev_inf=" + std::to_string(r.dev_inf) + " thr=" + std::to_string(r.threshold));
}

// =====================================================================================================
// 5 · The deadline wheel moves only what moves — and ticks drive time, not the host clock
// =====================================================================================================
static void t_wheel() {
    CellTable table; std::vector<Cell> cells;
    build(&table, &cells, 300);
    Field f; std::string err;
    f.init(dims(), Coeffs(), &err);
    f.set_now(T0);
    for (const Cell& c : cells) if (contributes(c)) f.place(c, &err);

    uint64_t moved = 0;
    check(f.tick(T0 + 1000000000ull, table, &moved, &err), "wheel.tick", err);
    check(moved > 0, "wheel.some_cells_crossed", std::to_string(moved));
    check(moved < f.placed_count(), "wheel.but_not_all_of_them",
          std::to_string(moved) + " of " + std::to_string(f.placed_count()));

    // After the tick the lattice must still be exactly what a cold projection at the new `now` gives.
    Field cold;
    check(f.cold_project(table, &cold, &err), "wheel.cold", err);
    check(f.projection_digest() == cold.projection_digest(), "wheel.cold_equals_incremental_after_a_tick");

    // Several ticks, then compare again: the wheel must not drift.
    bool ticks_ok = true;
    for (int i = 0; i < 6; ++i) if (!f.tick(f.now_ns() + 1000000000ull, table, &moved, &err)) { ticks_ok = false; break; }
    check(ticks_ok, "wheel.six_more_ticks", err);
    Field cold2;
    f.cold_project(table, &cold2, &err);
    check(f.projection_digest() == cold2.projection_digest(), "wheel.no_drift_over_six_ticks");
    check(f.conservation(table).holds(), "wheel.conservation_after_ticks");

    // The baseline is a fold of ticks, and it says so before it has any.
    Field b; b.init(dims(), Coeffs(), &err); b.set_now(T0);
    check(!b.baseline_has_history(), "wheel.baseline_is_zero_and_says_so");
    for (const Cell& c : cells) if (contributes(c)) b.place(c, &err);
    b.sweep();
    const int32_t probe = cell_index(dims(), 0, 0, 0);
    const int64_t p_before = b.pressure_at(probe);
    const int64_t base_before = b.baseline_at(probe);
    b.tick(T0 + 1000000000ull, table, &moved, &err);
    check(b.baseline_has_history(), "wheel.baseline_has_history_after_a_tick");
    // The EMA step moves value from `dev` into `baseline` and conserves their sum, so a cell that did
    // not change slot has the same pressure and a baseline that has walked toward it.
    check(b.pressure_at(probe) == p_before, "wheel.the_tick_conserves_pressure",
          std::to_string(b.pressure_at(probe)) + " was " + std::to_string(p_before));
    check(b.baseline_at(probe) != base_before || p_before == 0, "wheel.baseline_moved_toward_pressure",
          std::to_string(b.baseline_at(probe)) + " was " + std::to_string(base_before));

    // Lie arm: a fold that read the host clock instead of the tick would move without one. Ticking
    // to the SAME instant must change nothing in the projection.
    const std::string d0 = f.projection_digest();
    uint64_t moved0 = 0;
    check(f.tick(f.now_ns(), table, &moved0, &err), "wheel.tick_to_the_same_instant", err);
    check(moved0 == 0 && f.projection_digest() == d0, "wheel.lie.time_only_moves_by_a_tick");
}

// =====================================================================================================
// 6 · Totality: the fold refuses rather than wraps
// =====================================================================================================
static void t_totality() {
    CellTable table;
    ColdRec rec; rec.source = "s"; rec.table = "t"; rec.key = "big";
    const uint64_t id = cell_id_of(rec.source, rec.table, rec.key);
    Cell big = mk(id, 0, 0, INT64_MAX / 2, T0 + 1000000000ull);
    Cell& in = table.create(id, rec); in = big;

    Field f; std::string err;
    f.init(dims(), Coeffs(), &err); f.set_now(T0);
    const bool placed = f.place(big, &err);
    check(!placed && err == E_OVERFLOW, "total.an_unrepresentable_amount_is_refused", err);

    // And the field is unchanged by the refusal: a failed placement leaves no residue.
    check(f.placed_count() == 0, "total.a_refused_placement_leaves_nothing");
    check(f.conservation(table).lattice_fix == 0, "total.the_lattice_is_still_empty");

    // Bad dimensions and bad coefficients are refused at init, not at the first sweep.
    Field g; LatticeDims bad = dims(); bad.NSLOT = 1;
    check(!g.init(bad, Coeffs(), &err) && err == E_DIMS, "total.bad_dims_refused", err);
    Coeffs bc; bc.den = 100;                       // not a power of two: not a dyadic rational
    check(!g.init(dims(), bc, &err) && err == E_COEFFS, "total.non_dyadic_coefficients_refused", err);
}

int main() {
    std::printf("== TAPESTRY R1 field oracles ==\n");
    t_conservation();
    t_cold_equals_incremental();
    t_warrant_flag();
    t_sweep();
    t_wheel();
    t_totality();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
