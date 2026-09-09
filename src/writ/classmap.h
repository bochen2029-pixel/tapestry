// =====================================================================================================
// TAPESTRY · src/writ/classmap.h · the class map, the per-class pins, and the writ's constraint kinds
//
// Blueprint §3.3 and §4.7. The R0 subset: reversibility per (class, table, op), check expressions on
// the cell row, exposure caps per class per period, and the quorum-of-M a reversible class demands as
// a second key. Horizons, templates, enrichment, join paths and reducers are R1 and are absent here
// rather than half-present.
//
// THE PIN. QC-2 F6 and QC-7 F3 measured the shipped pin blind to the predicate operator, the class id
// and the flags — so a rule change that mattered left the pin unmoved and every cached judgment stayed
// "valid". Falsifier 20 is the answer: "a loop over a mutated copy per class-map field; the lie is a
// string-only mixer". So `class_pin` covers EVERY field of the class, each length-prefixed so two
// different splittings of the same bytes cannot collide, and the check programs enter by their
// PROGRAM HASH — which is over opcodes and constants, not source text, so reformatting a rule does not
// invalidate a pin but changing what it means does.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "expr.h"
#include "../core/blake2b.h"
#include "../core/bytes.h"
#include "../tx/cells.h"

namespace tapestry {
namespace writ {

// §3.3: reversibility per (class, table, op), derived by the transactor — never declared by the writer.
enum class Rev : uint8_t { ReversibleByInverse = 0, ReversibleWithin = 1, Irreversible = 2 };

inline const char* rev_name(Rev r) {
    switch (r) {
        case Rev::ReversibleByInverse: return "reversible_by_inverse";
        case Rev::ReversibleWithin:    return "reversible_within";
        case Rev::Irreversible:        return "irreversible";
    }
    return "?";
}
inline bool rev_from_name(const std::string& s, Rev* out) {
    if (s == "reversible_by_inverse") { *out = Rev::ReversibleByInverse; return true; }
    if (s == "reversible_within")     { *out = Rev::ReversibleWithin;    return true; }
    if (s == "irreversible")          { *out = Rev::Irreversible;        return true; }
    return false;
}

struct RevRule {
    std::string table;       // "*" matches any table
    std::string op;          // "*" matches any op
    Rev         rev = Rev::Irreversible;
    uint64_t    window_ns = 0;   // meaningful for ReversibleWithin
};

struct Check {
    std::string id;          // named, because a refusal must say WHICH clause refused it
    std::string source;      // kept for explain_refusal
    Program     prog;
};

struct ClassDef {
    uint32_t    cls = 0;
    std::string name;
    std::vector<RevRule> rev;
    std::vector<Check>   checks;
    int64_t     cap_minor_per_period = -1;   // -1 = uncapped; else exposure per class per period
    uint32_t    quorum_m = 0;                // N-of-M second keys on reversible classes (enforced at R5)
    uint32_t    quorum_n = 0;
    uint8_t     flags = 0;
    std::string class_pin;                   // filled by seal()

    // The reversibility of a write, DERIVED. §3.3: "a writer's declaration is a redundant assertion
    // and a mismatch is refused and warned". Most specific rule wins; the default is the safe one.
    Rev reversibility_of(const std::string& table, const std::string& op, uint64_t* window_ns) const {
        const RevRule* best = nullptr;
        int best_score = -1;
        for (const RevRule& r : rev) {
            const bool t_ok = (r.table == "*" || r.table == table);
            const bool o_ok = (r.op == "*" || r.op == op);
            if (!t_ok || !o_ok) continue;
            const int score = (r.table == "*" ? 0 : 2) + (r.op == "*" ? 0 : 1);
            if (score > best_score) { best_score = score; best = &r; }
        }
        if (!best) { if (window_ns) *window_ns = 0; return Rev::Irreversible; }
        if (window_ns) *window_ns = best->window_ns;
        return best->rev;
    }
};

namespace detail {
// Length-prefixed mixing: "a" ‖ "bc" and "ab" ‖ "c" must not hash alike, or two different class maps
// share a pin and the invalidation that should have happened does not.
inline void mix_str(Blake2b& h, const std::string& s) {
    const uint64_t n = s.size();
    uint8_t le[8]; for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((n >> (8 * k)) & 0xff);
    h.update(le, 8);
    h.update(s);
}
inline void mix_u64(Blake2b& h, uint64_t v) {
    uint8_t le[8]; for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((v >> (8 * k)) & 0xff);
    h.update(le, 8);
}
} // namespace detail

// Every field, in a fixed order. Add a field to ClassDef and you must add it here; the oracle
// `pin.every_field_moves_it` mutates each one in turn and fails if the pin does not move.
inline std::string class_pin_of(const ClassDef& c) {
    Blake2b h; h.init(32);
    detail::mix_str(h, "tapestry-class-pin-v1");
    detail::mix_u64(h, c.cls);                       // the class id — the shipped pin ignored this
    detail::mix_str(h, c.name);
    detail::mix_u64(h, (uint64_t)c.flags);           // and the flags
    detail::mix_u64(h, (uint64_t)c.cap_minor_per_period);
    detail::mix_u64(h, c.quorum_n);
    detail::mix_u64(h, c.quorum_m);
    detail::mix_u64(h, c.rev.size());
    for (const RevRule& r : c.rev) {
        detail::mix_str(h, r.table);
        detail::mix_str(h, r.op);                    // and the predicate operator
        detail::mix_u64(h, (uint64_t)r.rev);
        detail::mix_u64(h, r.window_ns);
    }
    detail::mix_u64(h, c.checks.size());
    for (const Check& k : c.checks) {
        detail::mix_str(h, k.id);
        detail::mix_str(h, k.prog.program_hash);     // meaning, not spelling
    }
    uint8_t out[32]; h.finish(out);
    return hex_of(out, 32);
}

class ClassMap {
public:
    // The binding every check in this map compiles against: the cell row plus `now_ns`.
    static Binding binding() {
        Binding b;
        for (int i = 0; i < COL_COUNT; ++i) b.names.push_back(CELL_COLUMN_NAMES[i]);
        return b;
    }

    // Add a class. Its checks are compiled here, at rule load, so a rule that cannot compile is
    // refused when it is proposed and never when a write depends on it.
    bool add(ClassDef def, const std::vector<std::pair<std::string, std::string>>& checks,
             std::string* reason) {
        const Binding b = binding();
        for (const auto& kv : checks) {
            Check c; c.id = kv.first; c.source = kv.second;
            std::string why;
            if (!compile(c.source, b, &c.prog, &why)) {
                if (reason) *reason = "check '" + c.id + "': " + why;
                return false;
            }
            def.checks.push_back(std::move(c));
        }
        for (const ClassDef& e : classes_) {
            if (e.cls == def.cls) { if (reason) *reason = "duplicate class id"; return false; }
        }
        def.class_pin = class_pin_of(def);
        classes_.push_back(std::move(def));
        seal();
        return true;
    }

    const ClassDef* find(uint32_t cls) const {
        for (const ClassDef& c : classes_) if (c.cls == cls) return &c;
        return nullptr;
    }
    const std::vector<ClassDef>& all() const { return classes_; }
    const std::string& map_pin() const { return map_pin_; }

private:
    // §3.3: "a `map_pin` over them". Invalidation is per class; the map pin exists so a peer can tell
    // in one comparison whether ANY class moved.
    void seal() {
        Blake2b h; h.init(32);
        detail::mix_str(h, "tapestry-map-pin-v1");
        detail::mix_u64(h, classes_.size());
        for (const ClassDef& c : classes_) detail::mix_str(h, c.class_pin);
        uint8_t out[32]; h.finish(out);
        map_pin_ = hex_of(out, 32);
    }
    std::vector<ClassDef> classes_;
    std::string map_pin_;
};

} // namespace writ
} // namespace tapestry
