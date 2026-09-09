// qc4_probe.cpp — QC-4 evidence probes for the license/grade/quorum slice.
// Build (WSL): g++ -O2 -std=c++17 qc4_probe.cpp -o qc4_probe
#include "osv_dispatch.h"
#include <cstring>
#include <cstdio>
#include <random>

using namespace osv;

struct World {
    Ledger L;
    LatticeDims d{4, 6, 12, 86400ull * 1000000000ull};
    std::vector<int64_t> load, cnt;
    std::vector<float>   cap, base, dev;
    std::vector<uint8_t> flg;
    std::vector<float>   dispersion;
    std::vector<double>  n_eff;
    std::vector<bool>    calibrated;
    Lattice lat;
    uint64_t now = parse_ts("2017-06-15 00:00:00");

    void build(size_t n_commitments, uint32_t seed, float margin_bias = 2.0f, float disp = 0.0f) {
        const int N = d.count();
        load.assign(N, 0); cnt.assign(N, 0); cap.assign(N, 0.f); base.assign(N, 0.f);
        dev.assign(N, 0.f); flg.assign(N, 0);
        dispersion.assign(N, disp); n_eff.assign(N, 1000.0); calibrated.assign(N, true);
        lat.d = d; lat.load_fix = load.data(); lat.count_fix = cnt.data(); lat.capacity = cap.data();
        lat.baseline = base.data(); lat.dev = dev.data(); lat.flags = flg.data();
        std::mt19937 rng(seed);
        for (size_t i = 0; i < n_commitments; ++i) {
            Commitment c; std::memset(&c, 0, sizeof c);
            c.id = 5000 + i;
            c.cls = rng() % 6; c.seg = rng() % 4; c.seat = rng() % 20;
            c.amount = 10.0f + (float)(rng() % 5000) / 100.0f;
            c.due_ns = now + (uint64_t)(rng() % 9) * 86400ull * 1000000000ull;
            c.margin = margin_bias + (float)((int)(rng() % 200) - 100) / 100.0f;
            c.state = C_OPEN;
            c.src_rev = (uint32_t)i;
            L.live[c.id] = c;
        }
    }
    void set_flag_on(size_t every, uint8_t f) {
        size_t k = 0;
        for (auto& kv : L.live) { if (k++ % every == 0) kv.second.flags |= f; }
    }
};

static Dispatch make_dispatch(float human_minutes, float worker_slots) {
    Dispatch D;
    D.workers.push_back(SeatPool{1, worker_slots, 0});
    D.warrants.push_back(WarrantPool{100, human_minutes, 0});
    return D;
}

// ---------------------------------------------------------------------------------------------
// PROBE 1 · the 64-byte budget: is there room for BOTH a 64-bit pos_last and a source revision?
// ---------------------------------------------------------------------------------------------
static void probe_layout() {
    std::printf("\n== PROBE 1 · the cache line budget ==\n");
    std::printf("  sizeof(Commitment)            = %zu\n", sizeof(Commitment));
    const size_t declared = 4*sizeof(uint64_t) + 2*sizeof(float) + 4*sizeof(uint32_t) + 4*sizeof(uint8_t);
    std::printf("  sum of declared members       = %zu\n", declared);
    std::printf("  tail padding available        = %zu bytes\n", sizeof(Commitment) - declared);
    std::printf("  blueprint 2.2 spends src_rev(4) + padding(%zu) on pos_last(8) -> %zu bytes left\n",
                sizeof(Commitment) - declared, sizeof(Commitment) - declared - (sizeof(Commitment)-declared));
    std::printf("  RECONCILIATION 5 also claims the same padding for judged_rev(4).\n");
    std::printf("  A 64-bit source revision (the stale-row comparand in Ingest::apply) needs 8 more.\n");
    std::printf("  VERDICT: pos_last, judged_rev and a 64-bit src_rev cannot all fit in 64 bytes.\n");
}

// ---------------------------------------------------------------------------------------------
// PROBE 2 · demotion: absorbing? and does the kappa meter read "ok" for a demoted class?
// ---------------------------------------------------------------------------------------------
static void probe_kappa_absorbing() {
    std::printf("\n== PROBE 2 · is demotion absorbing, and what does the meter read? ==\n");
    World W; W.build(1200, 6, 3.0f, 0.0f);

    Dispatch D = make_dispatch(1e6f, 1e6f);
    DispatchResult before = D.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);
    const ClassKappa& b3 = before.by_class.at(3);
    std::printf("  BEFORE demote  cls3: acted=%ld worked=%ld esc=%ld created=%.1f removed=%.1f kappa=%.3f -> %s\n",
                b3.acted, b3.worked, b3.escalated, b3.created, b3.removed, b3.kappa(),
                b3.cools_the_plant() ? "DEMOTE" : "ok");

    D.demoted[3] = true;
    for (int period = 1; period <= 3; ++period) {
        DispatchResult R = D.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);
        const ClassKappa& k = R.by_class.at(3);
        std::printf("  AFTER  p%-2d     cls3: acted=%ld worked=%ld esc=%ld created=%.1f removed=%.1f kappa=%.3g -> %s\n",
                    period, k.acted, k.worked, k.escalated, k.created, k.removed, k.kappa(),
                    k.cools_the_plant() ? "DEMOTE" : "ok");
    }
    std::printf("  A demoted class never ACTs or WORKs, so `removed` stays 0 forever and kappa can never\n"
                "  fall back under C_A_MACHINE=%.1f. NOTHING in the shipped code ever clears demoted[].\n", C_A_MACHINE);

    // now starve the budget so the demoted class's escalations all become holds
    Dispatch S = make_dispatch(/*human minutes*/ 0.0f, 1e6f);
    S.demoted[3] = true;
    DispatchResult T = S.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);
    const ClassKappa& t3 = T.by_class.at(3);
    std::printf("  STARVED        cls3: acted=%ld esc=%ld held=%ld created=%.1f removed=%.1f kappa=%.3g -> %s\n",
                t3.acted, t3.escalated, t3.held, t3.created, t3.removed, t3.kappa(),
                t3.cools_the_plant() ? "DEMOTE" : "ok");
    std::printf("  ^ the meter reads 'ok' for a fully starved demoted class: created=0/removed=0 -> kappa 0.\n");
}

// ---------------------------------------------------------------------------------------------
// PROBE 3 · does the demote path erase the reason on the verb row?
// ---------------------------------------------------------------------------------------------
static void probe_reason_erasure() {
    std::printf("\n== PROBE 3 · can the tape tell a demoted escalation from a merit escalation? ==\n");
    World W; W.build(1200, 6, 3.0f, 0.0f);
    Dispatch D = make_dispatch(1e6f, 1e6f);
    D.demoted[3] = true;
    DispatchResult R = D.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);
    long esc3_ok = 0, esc3_other = 0, esc_other_cls_ok = 0;
    for (const auto& r : R.rows) {
        if (r.verb != V_ESCALATE) continue;
        if (r.cls == 3) { (r.reason == D_OK) ? ++esc3_ok : ++esc3_other; }
        else if (r.reason == D_OK) ++esc_other_cls_ok;
    }
    std::printf("  class 3 (demoted) escalations with reason==ok : %ld  (other reasons: %ld)\n", esc3_ok, esc3_other);
    std::printf("  other classes' escalations with reason==ok    : %ld\n", esc_other_cls_ok);
    std::printf("  osv_dispatch.h:231 sets why=D_OK when demotion rewrites ACT/WORK -> ESCALATE.\n");
    std::printf("  VERDICT: the VerbRow carries NO marker of demotion. A grade fold reading the tape\n"
                "  cannot separate 'the judge wanted a human' from 'kappa took autonomy away'.\n");
}

// ---------------------------------------------------------------------------------------------
// PROBE 4 · does demoting one class steal adjudication minutes from the others?
// ---------------------------------------------------------------------------------------------
static void probe_budget_theft() {
    std::printf("\n== PROBE 4 · demotion of one class under a finite human budget ==\n");
    World W; W.build(1200, 6, 3.0f, 0.0f);
    W.set_flag_on(11, F_WARRANT);          // some irreversibles that MUST reach a human

    auto count_funded = [](const DispatchResult& R, uint32_t cls) {
        long n = 0; for (const auto& b : R.briefs) if (b.cls == cls) ++n; return n;
    };

    Dispatch A = make_dispatch(/*minutes*/ 600.0f, 1e6f);   // room for 50 briefs
    DispatchResult RA = A.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);

    Dispatch B = make_dispatch(600.0f, 1e6f);
    B.demoted[3] = true;
    DispatchResult RB = B.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);

    long warrant_starved_A = 0, warrant_starved_B = 0;
    for (const auto& r : RA.rows) if (r.verb == V_HOLD && r.reason == D_BUDGET &&
                                      (W.L.live[r.id].flags & F_WARRANT)) ++warrant_starved_A;
    for (const auto& r : RB.rows) if (r.verb == V_HOLD && r.reason == D_BUDGET &&
                                      (W.L.live[r.id].flags & F_WARRANT)) ++warrant_starved_B;

    std::printf("  no demotion : %zu briefs funded | cls3 got %ld | budget-starved %ld | irreversibles starved %ld\n",
                RA.briefs.size(), count_funded(RA, 3), RA.budget_starved, warrant_starved_A);
    std::printf("  cls3 demoted: %zu briefs funded | cls3 got %ld | budget-starved %ld | irreversibles starved %ld\n",
                RB.briefs.size(), count_funded(RB, 3), RB.budget_starved, warrant_starved_B);
    std::printf("  delta in irreversibles pushed into a budget hold by ONE class's demotion: %+ld\n",
                warrant_starved_B - warrant_starved_A);
}

// ---------------------------------------------------------------------------------------------
// PROBE 5 · fetch: how much of the tape has no horizon, and what does it cost the judge?
// ---------------------------------------------------------------------------------------------
static void probe_fetch_hole() {
    std::printf("\n== PROBE 5 · the fetch hole ==\n");
    World W; W.build(5000, 99, 1.6f, 0.35f);
    W.set_flag_on(9, F_WARRANT);
    W.set_flag_on(3, F_CONTENT);
    Dispatch D = make_dispatch(480.0f, 400.0f);
    DispatchResult R = D.run(W.L, W.lat, W.now, &W.dispersion, &W.n_eff, &W.calibrated);
    const long f = R.count(V_FETCH), tot = R.open_seen;
    std::printf("  one period, the test's own worked example: %ld open\n", tot);
    std::printf("    act %ld  work %ld  escalate %ld  FETCH %ld  hold %ld\n",
                R.count(V_ACT), R.count(V_WORK), R.count(V_ESCALATE), f, R.count(V_HOLD));
    std::printf("  fetch share of all verbs      : %.1f%%\n", 100.0 * f / tot);
    std::printf("  blueprint 2.3 horizons        : {h_act, h_work, h_hold, h_escalate}  <- no h_fetch\n");
    std::printf("  VerbCost::fetch               : %.1f supervision minutes\n", D.cost.escalate * 0.0f + VerbCost{}.fetch);
    std::printf("  kappa contribution of a fetch : created += %.1f, removed += 0\n", VerbCost{}.fetch);
    std::printf("  VERDICT: %.1f%% of decisions are free, uncapped, unhorizoned and ungraded.\n", 100.0 * f / tot);
    std::printf("  Escalations gradeable this period: %ld against GateParams::n_eff_floor = %.0f\n",
                R.count(V_ESCALATE), D.gp.n_eff_floor);
    std::printf("  -> min-over-verbs n_eff needs %.1f periods of escalations to clear the floor,\n"
                "     and that sample is the budget-truncated TOP of the value ranking, not a random draw.\n",
                D.gp.n_eff_floor / (double)R.count(V_ESCALATE));
}

// ---------------------------------------------------------------------------------------------
// PROBE 6 · the gate's evidence input: one scalar, or one per verb / per side?
// ---------------------------------------------------------------------------------------------
static void probe_neff_shape() {
    std::printf("\n== PROBE 6 · the shape of the evidence input the gate actually takes ==\n");
    std::printf("  GateIn fields                 : margin, pressure, dispersion, n_eff (ONE float), calibrated\n");
    std::printf("  sizeof(GateIn)                = %zu bytes\n", sizeof(GateIn));
    std::printf("  Dispatch::run indexes n_eff_of[i] where i = project_one(seg, cls, slot) -- a LATTICE CELL.\n");
    World W; W.build(0, 1);
    Commitment c; std::memset(&c, 0, sizeof c);
    c.cls = 2; c.seg = 1; c.state = C_OPEN;
    const uint64_t day = 86400ull*1000000000ull;
    c.due_ns = W.now + 5*day;
    std::printf("  same commitment, evidence bucket as its deadline approaches:\n");
    for (int elapsed = 0; elapsed <= 6; ++elapsed) {
        const uint64_t now = W.now + (uint64_t)elapsed*day;
        std::printf("    day +%d  -> lattice cell %d\n", elapsed, project_one(W.d, c, now));
    }
    std::printf("  VERDICT: one commitment walks through 6 different evidence buckets in 6 days without\n"
                "  changing at all. n_eff is attached to a bucket whose membership turns over every period,\n"
                "  so it is NOT the effective sample behind the decisions that bucket will judge.\n");
}

int main() {
    std::printf("QC-4 PROBES · grades, license, quorum, writ\n");
    probe_layout();
    probe_kappa_absorbing();
    probe_reason_erasure();
    probe_budget_theft();
    probe_fetch_hole();
    probe_neff_shape();
    std::printf("\n");
    return 0;
}
