// =====================================================================================================
// TAPESTRY · src/fold/grades.h · the grades fold
//
// Blueprint §3.4's `grades` row and §4.11's table. One graded decision per VERB entry — five verdicts,
// every grade carrying its source — produced at outcome arrival and at the first tick past the
// decision's horizon. Not a tape kind: QC-4 settled that grades are fold OUTPUT only, because a grade
// somebody can write down is a grade somebody can write down wrongly.
//
// FOUR RULES THAT ARE EASY TO GET BACKWARDS, AND ARE THE POINT
//
//   1 · A HOLD IS GRADED. The verb that changes no row is the majority of what an autonomous system
//       does, and if it is ungraded the record flatters: you can only audit the actions. A hold is
//       right when the world closed the cell inside the horizon anyway, and wrong when the deadline
//       passed while it was held.
//
//   2 · AN ESCALATION THE HUMAN RUBBER-STAMPS IS WRONG. §4.11: escalate is *right* when "the human's
//       decision differed from the counterfactual" and *wrong* when "the human ratified the
//       counterfactual, a rubber stamp". The escalation bought nothing — it spent a human on a
//       decision the machine had already made correctly. This inverts the intuition that agreement
//       is success, and it is the only way escalation can be measured rather than assumed safe.
//
//   3 · N_EFF IS PER VERB, NEVER POOLED. Falsifier 10's planted lie is pooled `n_eff`: a class with
//       thousands of graded holds and four graded acts looks well-evidenced in aggregate and is not.
//       Counts here are keyed `(class, verb, verdict)` and there is no total to read by accident.
//
//   4 · A VERB WITH NO FINITE HORIZON CANNOT BE GRADED, SO ITS CLASS IS UNLICENSABLE. Falsifier 13's
//       lie is "fetch with no horizon" — the verb that quietly becomes the answer to everything
//       because nothing can ever call it wrong. `Horizons::all_finite` refuses that class.
//
// Time comes only from ticks. §3.4: every fold reads `now` as the `t_epoch_ns` of the entry being
// folded, so a decision expires because the tape said so, never because the host clock moved.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "../tx/cells.h"
#include "../core/blake2b.h"
#include "../core/bytes.h"

namespace tapestry {
namespace fold {

enum class Verdict : uint8_t { UngradedYet = 0, Right = 1, Wrong = 2, ReversedLate = 3, Ungradable = 4 };
enum class Source  : uint8_t { Own = 0, Borrowed = 1, Canary = 2, Stratum = 3 };

inline const char* verdict_name(Verdict v) {
    switch (v) {
        case Verdict::UngradedYet:  return "ungraded_yet";
        case Verdict::Right:        return "right";
        case Verdict::Wrong:        return "wrong";
        case Verdict::ReversedLate: return "reversed_late";
        case Verdict::Ungradable:   return "ungradable";
    }
    return "?";
}
inline const char* source_name(Source s) {
    switch (s) {
        case Source::Own:      return "own";
        case Source::Borrowed: return "borrowed";
        case Source::Canary:   return "canary";
        case Source::Stratum:  return "stratum";
    }
    return "?";
}

// §3.3's seven horizons. Every one must be finite and non-zero, including `h_fetch` — falsifier 13.
struct Horizons {
    uint64_t h_act = 0, h_work = 0, h_fetch = 0;
    uint64_t h_hold = 0, h_hold_nodeadline = 0;
    uint64_t h_escalate_arrival = 0, h_escalate_resolved = 0;

    bool all_finite() const {
        const uint64_t hs[7] = {h_act, h_work, h_fetch, h_hold, h_hold_nodeadline,
                                h_escalate_arrival, h_escalate_resolved};
        for (uint64_t h : hs) if (h == 0 || h == UINT64_MAX) return false;
        return true;
    }
    // Which horizon grades this verb. `hold` is measured FROM THE DEADLINE when there is one, so the
    // caller passes the cell's `due_ns` and gets back an absolute instant, not a duration.
    uint64_t deadline_for(uint8_t verb, uint64_t at_ns, uint64_t due_ns) const {
        switch (verb) {
            case V_ACT:      return at_ns + h_act;
            case V_WORK:     return at_ns + h_work;
            case V_FETCH:    return at_ns + h_fetch;
            case V_ESCALATE: return at_ns + h_escalate_arrival + h_escalate_resolved;
            case V_HOLD:     return (due_ns != 0) ? (due_ns + h_hold) : (at_ns + h_hold_nodeadline);
        }
        return at_ns;
    }
};

// One decision, and what became of it.
struct Decision {
    uint64_t pos = 0;                  // the verb entry's position — the decision's identity
    uint64_t cell = 0;
    uint32_t cls = 0;
    uint8_t  verb = V_HOLD;
    uint64_t at_ns = 0;
    uint64_t due_ns = 0;               // the cell's deadline as of the decision
    uint8_t  counterfactual_verb = V_HOLD;   // what the gate would have said, for escalate
    Source   source = Source::Own;
    Verdict  verdict = Verdict::UngradedYet;
    uint64_t horizon_ns = 0;           // the absolute instant after which an outcome cannot grade it
    uint64_t graded_at_ns = 0;
    bool     margin_crossed = false;   // for fetch: did the next judgment cross a threshold
    bool     fetch_cap_hit = false;
};

// The per-verb tally. There is deliberately no `total` field: falsifier 10's lie is a pooled count,
// and the surest way not to read one by accident is not to compute one.
struct VerbTally {
    uint64_t right = 0, wrong = 0, reversed_late = 0, ungraded_yet = 0, ungradable = 0;
    uint64_t graded() const { return right + wrong + reversed_late; }   // n_eff for this verb alone
    uint64_t seen()   const { return graded() + ungraded_yet + ungradable; }
};

class Grades {
public:
    void set_horizons(uint32_t cls, const Horizons& h) { horizons_[cls] = h; }

    // A class whose verbs cannot all be graded is unlicensable, and the fold says which verb.
    bool licensable(uint32_t cls, std::string* why) const {
        auto it = horizons_.find(cls);
        if (it == horizons_.end()) { if (why) *why = "no horizons for class"; return false; }
        if (!it->second.all_finite()) { if (why) *why = "a verb has no finite horizon"; return false; }
        return true;
    }

    // ---- the inputs, fed in position order ------------------------------------------------------
    void on_verb(uint64_t pos, uint64_t cell, uint32_t cls, uint8_t verb, uint64_t at_ns,
                 uint64_t due_ns, uint8_t counterfactual, Source src) {
        Decision d;
        d.pos = pos; d.cell = cell; d.cls = cls; d.verb = verb; d.at_ns = at_ns; d.due_ns = due_ns;
        d.counterfactual_verb = counterfactual; d.source = src;
        auto it = horizons_.find(cls);
        d.horizon_ns = (it == horizons_.end()) ? at_ns : it->second.deadline_for(verb, at_ns, due_ns);
        open_[cell].push_back(decisions_.size());
        decisions_.push_back(d);
        now_ = at_ns > now_ ? at_ns : now_;
    }

    // A world fact landing on a cell. `reverses_pos` names the decision it undoes; `closed` says the
    // obligation is discharged; `reworked` and `reopened` are the act's other two failure shapes.
    void on_fact(uint64_t cell, uint64_t at_ns, uint64_t reverses_pos,
                 bool closed, bool reworked, bool reopened) {
        now_ = at_ns > now_ ? at_ns : now_;
        auto it = open_.find(cell);
        if (it == open_.end()) return;
        for (size_t idx : it->second) {
            Decision& d = decisions_[idx];
            if (d.verdict != Verdict::UngradedYet) continue;
            const bool inside = at_ns <= d.horizon_ns;
            switch (d.verb) {
                case V_ACT:
                case V_WORK: {
                    const bool undone = (reverses_pos == d.pos) || reworked || reopened;
                    if (undone) grade(d, inside ? Verdict::Wrong : Verdict::ReversedLate, at_ns);
                    else if (closed && inside) grade(d, Verdict::Right, at_ns);
                    break;
                }
                case V_HOLD:
                    // The world closed it while we were holding: the hold cost nothing and was right.
                    if (closed && inside) grade(d, Verdict::Right, at_ns);
                    break;
                case V_ESCALATE:
                    // Rule 2. The human's decision arrives as a fact; agreeing with the machine's
                    // counterfactual means the escalation bought nothing.
                    if (inside) {
                        const bool ratified = (human_verb_ == d.counterfactual_verb);
                        grade(d, ratified ? Verdict::Wrong : Verdict::Right, at_ns);
                    }
                    break;
                case V_FETCH:
                    if (inside && d.margin_crossed) grade(d, Verdict::Right, at_ns);
                    break;
                default: break;
            }
        }
    }

    // The human's decision, recorded before the fact that carries it, so escalate can be graded
    // against the counterfactual rather than against the outcome.
    void set_human_verb(uint8_t v) { human_verb_ = v; }

    // The next judgment on a cell grades a fetch: did the margin cross a threshold it had not crossed.
    void on_judgment(uint64_t cell, uint64_t at_ns, bool crossed, bool cap_hit) {
        now_ = at_ns > now_ ? at_ns : now_;
        auto it = open_.find(cell);
        if (it == open_.end()) return;
        for (size_t idx : it->second) {
            Decision& d = decisions_[idx];
            if (d.verdict != Verdict::UngradedYet || d.verb != V_FETCH) continue;
            d.margin_crossed = crossed;
            d.fetch_cap_hit = cap_hit;
            if (at_ns <= d.horizon_ns) grade(d, (crossed && !cap_hit) ? Verdict::Right : Verdict::Wrong, at_ns);
        }
    }

    // Time as a row. At the first tick past a decision's horizon it is graded on what did NOT happen:
    // an act nobody reversed is right; a hold whose deadline passed is wrong; anything whose outcome
    // simply never arrived is UNGRADABLE — excluded from n_eff and counted in the ungradable fraction,
    // which is the honest place for it.
    void on_tick(uint64_t now_ns) {
        now_ = now_ns > now_ ? now_ns : now_;
        for (Decision& d : decisions_) {
            if (d.verdict != Verdict::UngradedYet) continue;
            if (now_ <= d.horizon_ns) continue;
            switch (d.verb) {
                case V_ACT:
                case V_WORK:
                    grade(d, Verdict::Right, now_);            // survived its horizon unreversed
                    break;
                case V_HOLD:
                    // The deadline passed while held. That is the hold's own failure mode.
                    grade(d, (d.due_ns != 0 && d.due_ns <= now_) ? Verdict::Wrong : Verdict::Ungradable, now_);
                    break;
                case V_ESCALATE:
                    grade(d, Verdict::Ungradable, now_);       // an unreached escalation
                    break;
                case V_FETCH:
                    grade(d, Verdict::Wrong, now_);            // the margin never crossed
                    break;
                default:
                    grade(d, Verdict::Ungradable, now_);
                    break;
            }
        }
    }

    // ---- the outputs ------------------------------------------------------------------------------
    VerbTally tally(uint32_t cls, uint8_t verb) const {
        VerbTally t;
        for (const Decision& d : decisions_) {
            if (d.cls != cls || d.verb != verb) continue;
            switch (d.verdict) {
                case Verdict::Right:        ++t.right; break;
                case Verdict::Wrong:        ++t.wrong; break;
                case Verdict::ReversedLate: ++t.reversed_late; break;
                case Verdict::UngradedYet:  ++t.ungraded_yet; break;
                case Verdict::Ungradable:   ++t.ungradable; break;
            }
        }
        return t;
    }
    VerbTally tally(uint32_t cls, uint8_t verb, Source src) const {
        VerbTally t;
        for (const Decision& d : decisions_) {
            if (d.cls != cls || d.verb != verb || d.source != src) continue;
            switch (d.verdict) {
                case Verdict::Right:        ++t.right; break;
                case Verdict::Wrong:        ++t.wrong; break;
                case Verdict::ReversedLate: ++t.reversed_late; break;
                case Verdict::UngradedYet:  ++t.ungraded_yet; break;
                case Verdict::Ungradable:   ++t.ungradable; break;
            }
        }
        return t;
    }

    // The fraction of a class's decisions that can never be graded. §4.11 caps it, because a class
    // that mostly cannot be measured is a class that mostly cannot be licensed.
    double ungradable_fraction(uint32_t cls) const {
        uint64_t n = 0, u = 0;
        for (const Decision& d : decisions_) {
            if (d.cls != cls) continue;
            ++n;
            if (d.verdict == Verdict::Ungradable) ++u;
        }
        return n ? (double)u / (double)n : 0.0;
    }

    // §4.11: "a band is calibrated only when EVERY emittable verb clears the floor". The minimum over
    // verbs, never the pool — and the verb that fails is named, because "not calibrated" without a
    // reason is not actionable.
    bool calibrated(uint32_t cls, const std::vector<uint8_t>& emittable, uint64_t floor,
                    double ungradable_ceiling, uint64_t canary_floor, std::string* why) const {
        for (uint8_t v : emittable) {
            const VerbTally t = tally(cls, v);
            if (t.graded() < floor) {
                if (why) *why = std::string("verb ") + std::to_string((int)v) + " has n_eff " +
                                std::to_string(t.graded()) + " below floor " + std::to_string(floor);
                return false;
            }
        }
        // The act side needs CANARY-source outcomes specifically: the machine's own draw in an
        // unlicensed reversible class. Borrowed evidence does not license acting.
        const VerbTally act_canary = tally(cls, V_ACT, Source::Canary);
        if (act_canary.graded() < canary_floor) {
            if (why) *why = "act has " + std::to_string(act_canary.graded()) +
                            " canary-source outcomes, floor " + std::to_string(canary_floor);
            return false;
        }
        const double uf = ungradable_fraction(cls);
        if (uf > ungradable_ceiling) {
            if (why) *why = "ungradable fraction " + std::to_string(uf) + " over ceiling";
            return false;
        }
        return true;
    }

    const std::vector<Decision>& decisions() const { return decisions_; }
    uint64_t now_ns() const { return now_; }

    std::string digest() const {
        Blake2b h; h.init(32);
        for (const Decision& d : decisions_) {
            const uint64_t f[6] = {d.pos, d.cell, (uint64_t)d.cls, (uint64_t)d.verb,
                                   (uint64_t)d.verdict, d.graded_at_ns};
            for (uint64_t v : f) {
                uint8_t le[8]; for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((v >> (8 * k)) & 0xff);
                h.update(le, 8);
            }
        }
        uint8_t out[32]; h.finish(out);
        return hex_of(out, 32);
    }

private:
    void grade(Decision& d, Verdict v, uint64_t at_ns) {
        d.verdict = v;
        d.graded_at_ns = at_ns;
    }

    std::vector<Decision> decisions_;
    std::map<uint64_t, std::vector<size_t>> open_;
    std::map<uint32_t, Horizons> horizons_;
    uint64_t now_ = 0;
    uint8_t  human_verb_ = V_HOLD;
};

} // namespace fold
} // namespace tapestry
