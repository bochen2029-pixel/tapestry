// =====================================================================================================
// TAPESTRY · src/tests/t_grades.cpp · the grades fold's oracles, each with its lie arm
//
// Two of the blueprint's falsifiers live here whole:
//   falsifier 10 · every verb graded — a band is never licensed while any emittable verb's graded
//                  count is below the floor, or while the act side lacks canary-source outcomes
//                  lie: pooled n_eff
//   falsifier 13 · the verb that cannot hide — every verb kind has a finite horizon and a verdict
//                  distribution
//                  lie: fetch with no horizon
// =====================================================================================================

#include <cstdio>
#include <string>
#include <vector>
#include "../fold/grades.h"

using namespace tapestry;
using namespace tapestry::fold;

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}

static const uint64_t SEC = 1000000000ull;
static const uint64_t T0  = 1757000000ull * SEC;

static Horizons hz() {
    Horizons h;
    h.h_act = 10 * SEC; h.h_work = 20 * SEC; h.h_fetch = 5 * SEC;
    h.h_hold = 10 * SEC; h.h_hold_nodeadline = 30 * SEC;
    h.h_escalate_arrival = 5 * SEC; h.h_escalate_resolved = 10 * SEC;
    return h;
}

// =====================================================================================================
// 1 · Falsifier 13 · every verb kind has a finite horizon
// =====================================================================================================
static void t_horizons() {
    Grades g;
    g.set_horizons(1, hz());
    std::string why;
    check(g.licensable(1, &why), "hz.a_full_set_is_licensable", why);

    // The planted lie: fetch with no horizon. The verb that quietly becomes the answer to everything
    // because nothing can ever call it wrong.
    Horizons bad = hz(); bad.h_fetch = 0;
    Grades g2; g2.set_horizons(1, bad);
    check(!g2.licensable(1, &why), "hz.lie.fetch_with_no_horizon_is_unlicensable", why);

    Horizons inf = hz(); inf.h_hold_nodeadline = UINT64_MAX;
    Grades g3; g3.set_horizons(1, inf);
    check(!g3.licensable(1, &why), "hz.an_infinite_horizon_is_no_horizon", why);

    Grades g4;
    check(!g4.licensable(7, &why), "hz.a_class_with_no_horizons_is_unlicensable", why);

    // A hold's horizon runs from the DEADLINE, not from the decision — a hold on a cell due in an
    // hour has an hour longer to be right than one due now.
    const Horizons h = hz();
    check(h.deadline_for(V_HOLD, T0, T0 + 100 * SEC) == T0 + 110 * SEC, "hz.hold_measured_from_the_deadline");
    check(h.deadline_for(V_HOLD, T0, 0) == T0 + 30 * SEC, "hz.hold_without_a_deadline_uses_its_own");
    check(h.deadline_for(V_ESCALATE, T0, 0) == T0 + 15 * SEC, "hz.escalate_has_two_horizons");
}

// =====================================================================================================
// 2 · Act and work: right, wrong, and reversed late
// =====================================================================================================
static void t_act() {
    Grades g; g.set_horizons(1, hz());

    // An act nobody reverses, graded at the first tick past its horizon.
    g.on_verb(1, 100, 1, V_ACT, T0, 0, V_HOLD, Source::Own);
    // An act reversed inside its horizon: wrong.
    g.on_verb(2, 200, 1, V_ACT, T0, 0, V_HOLD, Source::Own);
    g.on_fact(200, T0 + 5 * SEC, /*reverses*/ 2, false, false, false);
    // An act reversed AFTER its horizon: reversed_late, which is a different fact about the world
    // than "wrong" and is why the verdict set has five members.
    g.on_verb(3, 300, 1, V_ACT, T0, 0, V_HOLD, Source::Own);
    g.on_fact(300, T0 + 50 * SEC, /*reverses*/ 3, false, false, false);
    // An act whose cell closed cleanly inside the horizon: right, immediately.
    g.on_verb(4, 400, 1, V_ACT, T0, 0, V_HOLD, Source::Own);
    g.on_fact(400, T0 + 2 * SEC, 0, /*closed*/ true, false, false);
    // Work that was reworked: wrong.
    g.on_verb(5, 500, 1, V_WORK, T0, 0, V_HOLD, Source::Own);
    g.on_fact(500, T0 + 3 * SEC, 0, false, /*reworked*/ true, false);

    g.on_tick(T0 + 60 * SEC);

    const VerbTally a = g.tally(1, V_ACT);
    check(a.right == 2 && a.wrong == 1 && a.reversed_late == 1, "act.verdicts",
          "r=" + std::to_string(a.right) + " w=" + std::to_string(a.wrong) +
          " rl=" + std::to_string(a.reversed_late));
    const VerbTally w = g.tally(1, V_WORK);
    check(w.wrong == 1, "act.rework_is_wrong");
    check(a.ungraded_yet == 0 && w.ungraded_yet == 0, "act.the_tick_graded_everything_past_its_horizon");

    // Lie arm: a grader that treats any reversal as "wrong" loses the distinction between a decision
    // that was wrong and one the world changed its mind about later.
    check(a.reversed_late == 1 && a.wrong == 1, "act.lie.late_is_not_the_same_as_wrong");
}

// =====================================================================================================
// 3 · The hold is graded — the verb that is most of what an autonomous system does
// =====================================================================================================
static void t_hold() {
    Grades g; g.set_horizons(1, hz());

    // The world closed it while we held: the hold cost nothing and was right.
    g.on_verb(1, 100, 1, V_HOLD, T0, T0 + 100 * SEC, V_ACT, Source::Own);
    g.on_fact(100, T0 + 20 * SEC, 0, /*closed*/ true, false, false);
    // The deadline passed while held: wrong. This is the hold's own failure mode and the reason a
    // hold cannot be a free action.
    g.on_verb(2, 200, 1, V_HOLD, T0, T0 + 10 * SEC, V_ACT, Source::Own);
    // A hold on a cell with no deadline that nothing ever resolved: UNGRADABLE, not right.
    g.on_verb(3, 300, 1, V_HOLD, T0, 0, V_ACT, Source::Own);

    g.on_tick(T0 + 200 * SEC);

    const VerbTally h = g.tally(1, V_HOLD);
    check(h.right == 1, "hold.closed_by_the_world_inside_the_horizon_is_right", std::to_string(h.right));
    check(h.wrong == 1, "hold.deadline_passed_while_held_is_wrong", std::to_string(h.wrong));
    check(h.ungradable == 1, "hold.no_deadline_and_no_outcome_is_ungradable", std::to_string(h.ungradable));
    check(h.graded() == 2, "hold.n_eff_excludes_the_ungradable_one", std::to_string(h.graded()));

    // Lie arm: a grader that scores an unresolved hold as `right` because nothing went wrong would
    // report n_eff of 3 here and a perfect record. Silence is not evidence.
    check(h.graded() != h.seen(), "hold.lie.silence_would_have_counted_as_success",
          std::to_string(h.graded()) + " graded of " + std::to_string(h.seen()) + " seen");
}

// =====================================================================================================
// 4 · The rubber stamp — an escalation the human ratifies is WRONG
// =====================================================================================================
static void t_escalate() {
    Grades g; g.set_horizons(1, hz());

    // The human disagreed with what the gate would have done: the escalation earned its cost.
    g.on_verb(1, 100, 1, V_ESCALATE, T0, 0, /*counterfactual*/ V_ACT, Source::Own);
    g.set_human_verb(V_HOLD);
    g.on_fact(100, T0 + 5 * SEC, 0, true, false, false);

    // The human ratified the counterfactual: a rubber stamp. §4.11 calls this WRONG, because the
    // escalation spent a human on a decision the machine had already made correctly.
    g.on_verb(2, 200, 1, V_ESCALATE, T0, 0, /*counterfactual*/ V_ACT, Source::Own);
    g.set_human_verb(V_ACT);
    g.on_fact(200, T0 + 5 * SEC, 0, true, false, false);

    // An escalation nobody ever reached: ungradable on arrival.
    g.on_verb(3, 300, 1, V_ESCALATE, T0, 0, V_ACT, Source::Own);
    g.on_tick(T0 + 100 * SEC);

    const VerbTally e = g.tally(1, V_ESCALATE);
    check(e.right == 1, "esc.a_human_who_differed_is_right", std::to_string(e.right));
    check(e.wrong == 1, "esc.a_rubber_stamp_is_wrong", std::to_string(e.wrong));
    check(e.ungradable == 1, "esc.an_unreached_escalation_is_ungradable");

    // Lie arm: the intuitive grader, which scores agreement as success. It would report 2 right and
    // 0 wrong here — and a class that escalates everything would look flawless while buying nothing.
    check(e.right != 2, "esc.lie.agreement_is_not_success");
}

// =====================================================================================================
// 5 · Fetch has a horizon and a verdict, so it cannot become the answer to everything
// =====================================================================================================
static void t_fetch() {
    Grades g; g.set_horizons(1, hz());

    g.on_verb(1, 100, 1, V_FETCH, T0, 0, V_HOLD, Source::Own);
    g.on_judgment(100, T0 + 2 * SEC, /*crossed*/ true, /*cap_hit*/ false);
    g.on_verb(2, 200, 1, V_FETCH, T0, 0, V_HOLD, Source::Own);
    g.on_judgment(200, T0 + 2 * SEC, /*crossed*/ false, false);
    g.on_verb(3, 300, 1, V_FETCH, T0, 0, V_HOLD, Source::Own);
    g.on_judgment(300, T0 + 2 * SEC, true, /*cap_hit*/ true);
    g.on_verb(4, 400, 1, V_FETCH, T0, 0, V_HOLD, Source::Own);   // no judgment ever arrives

    g.on_tick(T0 + 60 * SEC);
    const VerbTally f = g.tally(1, V_FETCH);
    check(f.right == 1, "fetch.crossing_a_threshold_is_right", std::to_string(f.right));
    check(f.wrong == 3, "fetch.not_crossing_or_hitting_the_cap_is_wrong", std::to_string(f.wrong));
    check(f.graded() == 4, "fetch.every_fetch_got_a_verdict");

    // Lie arm: with no horizon a fetch is never graded, so a class can fetch forever and never be
    // called wrong. Falsifier 13's exact shape.
    Horizons no_fetch = hz(); no_fetch.h_fetch = 0;
    Grades g2; g2.set_horizons(1, no_fetch);
    std::string why;
    check(!g2.licensable(1, &why), "fetch.lie.without_a_horizon_the_class_is_unlicensable", why);
}

// =====================================================================================================
// 6 · Falsifier 10 · every verb graded, and n_eff is never pooled
// =====================================================================================================
static void t_calibration() {
    Grades g; g.set_horizons(1, hz());

    // A class with plenty of graded holds and almost no graded acts. Pooled, it looks well-evidenced.
    for (int i = 0; i < 200; ++i) {
        g.on_verb((uint64_t)(1000 + i), (uint64_t)(1000 + i), 1, V_HOLD, T0, T0 + 5 * SEC, V_ACT, Source::Own);
        g.on_fact((uint64_t)(1000 + i), T0 + 2 * SEC, 0, true, false, false);
    }
    for (int i = 0; i < 3; ++i) {
        g.on_verb((uint64_t)(2000 + i), (uint64_t)(2000 + i), 1, V_ACT, T0, 0, V_HOLD, Source::Canary);
        g.on_fact((uint64_t)(2000 + i), T0 + 2 * SEC, 0, true, false, false);
    }
    g.on_tick(T0 + 100 * SEC);

    const std::vector<uint8_t> emittable = {V_ACT, V_HOLD};
    std::string why;
    check(!g.calibrated(1, emittable, /*floor*/ 100, /*ungradable ceiling*/ 0.2, /*canary floor*/ 2, &why),
          "cal.a_verb_below_the_floor_blocks_the_band", why);
    check(why.find("verb 1") != std::string::npos, "cal.and_names_which_verb", why);

    // Lie arm: the pooled count. 203 graded decisions clears a floor of 100 comfortably — and it is
    // the wrong number, because the act side has three.
    const uint64_t pooled = g.tally(1, V_ACT).graded() + g.tally(1, V_HOLD).graded();
    check(pooled >= 100 && g.tally(1, V_ACT).graded() < 100,
          "cal.lie.pooled_n_eff_would_have_licensed_it",
          "pooled=" + std::to_string(pooled) + " act=" + std::to_string(g.tally(1, V_ACT).graded()));

    // The act side needs CANARY-source outcomes specifically; borrowed evidence does not license
    // acting. With the floor lowered, the canary requirement is what still holds it.
    Grades b; b.set_horizons(1, hz());
    for (int i = 0; i < 10; ++i) {
        b.on_verb((uint64_t)(3000 + i), (uint64_t)(3000 + i), 1, V_ACT, T0, 0, V_HOLD, Source::Borrowed);
        b.on_fact((uint64_t)(3000 + i), T0 + 2 * SEC, 0, true, false, false);
        b.on_verb((uint64_t)(4000 + i), (uint64_t)(4000 + i), 1, V_HOLD, T0, T0 + 5 * SEC, V_ACT, Source::Own);
        b.on_fact((uint64_t)(4000 + i), T0 + 2 * SEC, 0, true, false, false);
    }
    b.on_tick(T0 + 100 * SEC);
    check(!b.calibrated(1, emittable, 5, 0.2, /*canary floor*/ 3, &why), "cal.borrowed_does_not_license_acting", why);
    check(why.find("canary") != std::string::npos, "cal.and_says_it_is_the_canary_floor", why);

    // With canary-source acts, the same class calibrates.
    Grades c; c.set_horizons(1, hz());
    for (int i = 0; i < 10; ++i) {
        c.on_verb((uint64_t)(5000 + i), (uint64_t)(5000 + i), 1, V_ACT, T0, 0, V_HOLD, Source::Canary);
        c.on_fact((uint64_t)(5000 + i), T0 + 2 * SEC, 0, true, false, false);
        c.on_verb((uint64_t)(6000 + i), (uint64_t)(6000 + i), 1, V_HOLD, T0, T0 + 5 * SEC, V_ACT, Source::Own);
        c.on_fact((uint64_t)(6000 + i), T0 + 2 * SEC, 0, true, false, false);
    }
    c.on_tick(T0 + 100 * SEC);
    check(c.calibrated(1, emittable, 5, 0.2, 3, &why), "cal.canary_evidence_licenses_it", why);

    // The ungradable ceiling still bites even when every count clears.
    Grades u; u.set_horizons(1, hz());
    for (int i = 0; i < 10; ++i) {
        u.on_verb((uint64_t)(7000 + i), (uint64_t)(7000 + i), 1, V_ACT, T0, 0, V_HOLD, Source::Canary);
        u.on_fact((uint64_t)(7000 + i), T0 + 2 * SEC, 0, true, false, false);
        u.on_verb((uint64_t)(8000 + i), (uint64_t)(8000 + i), 1, V_HOLD, T0, T0 + 5 * SEC, V_ACT, Source::Own);
        u.on_fact((uint64_t)(8000 + i), T0 + 2 * SEC, 0, true, false, false);
    }
    for (int i = 0; i < 40; ++i)                                  // escalations nobody reaches
        u.on_verb((uint64_t)(9000 + i), (uint64_t)(9000 + i), 1, V_ESCALATE, T0, 0, V_ACT, Source::Own);
    u.on_tick(T0 + 100 * SEC);
    check(u.ungradable_fraction(1) > 0.2, "cal.ungradable_fraction_measured",
          std::to_string(u.ungradable_fraction(1)));
    check(!u.calibrated(1, emittable, 5, 0.2, 3, &why), "cal.the_ungradable_ceiling_bites", why);
}

// =====================================================================================================
// 7 · Time comes from ticks, and the fold is a function of what it was fed
// =====================================================================================================
static void t_determinism() {
    auto build = [](Grades* g) {
        g->set_horizons(1, hz());
        for (int i = 0; i < 50; ++i) {
            const uint8_t verb = (i % 3 == 0) ? (uint8_t)V_ACT : ((i % 3 == 1) ? (uint8_t)V_HOLD : (uint8_t)V_FETCH);
            g->on_verb((uint64_t)i, (uint64_t)(100 + i), 1, verb, T0 + (uint64_t)i * SEC,
                       (i % 4 == 0) ? 0 : T0 + (uint64_t)(i + 20) * SEC, V_ACT,
                       (i % 5 == 0) ? Source::Canary : Source::Own);
            if (i % 2 == 0) g->on_fact((uint64_t)(100 + i), T0 + (uint64_t)(i + 1) * SEC, 0, true, false, false);
            if (i % 3 == 2) g->on_judgment((uint64_t)(100 + i), T0 + (uint64_t)(i + 1) * SEC, i % 6 == 2, false);
        }
    };
    Grades a, b;
    build(&a); build(&b);
    check(a.digest() == b.digest(), "det.same_inputs_same_grades");

    // A tick with no new inputs must not change a decision that is already graded.
    a.on_tick(T0 + 500 * SEC);
    const std::string after_first = a.digest();
    a.on_tick(T0 + 500 * SEC);
    check(a.digest() == after_first, "det.a_repeated_tick_changes_nothing");

    // Lie arm: a grader reading the host clock would grade without a tick. Nothing here moves until
    // one arrives, so the digest before the tick differs from the one after — and only then.
    b.on_tick(T0 + 500 * SEC);
    check(b.digest() == after_first, "det.lie.time_only_moves_by_a_tick");
}

int main() {
    std::printf("== TAPESTRY R1 grades oracles ==\n");
    t_horizons();
    t_act();
    t_hold();
    t_escalate();
    t_fetch();
    t_calibration();
    t_determinism();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
