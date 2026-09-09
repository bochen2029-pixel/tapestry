// =====================================================================================================
// TAPESTRY · src/writ/expr.h · the constraint expression language
//
// WHY THIS EXISTS INSTEAD OF DUCKDB. Blueprint §9's R0 row says "the constraint language as scalar
// UDFs in DuckDB"; the QC synthesis §3.14 records that choice. It is declined here, deliberately, and
// the reason is §3.4: a fold is identified by the hash of its source together with the compiler
// identity, the flag string and the target architecture. A third-party query planner sitting inside
// the one writer's refusal decision is in none of that tuple, so §5's "exact within a tuple" claim
// would rest on a library the tuple cannot name — in a store whose whole argument is that every
// durable structure re-derives from the tape. The read path (§4.8's `query(sql, as_of_pos)`) is a
// separate question and nothing in R0 through R2 needs it. Recorded as a delta in the R0.2 receipt.
//
// WHAT IT IS. A total, deterministic, integer-only expression language, compiled once at rule load
// into a flat stack-machine program and evaluated per row with no allocation:
//
//     amount_minor <= 500000 && (state != 3 || due_ns > now_ns)
//
//   * INT64 AND BOOL, NOTHING ELSE. No floats: a float comparison is not portable across the host and
//     the card, and money that is summed by a cap fold must be exact (falsifier 1b, and QC-1/QC-2's
//     FMA probes). Money is carried in minor units.
//   * TOTAL. Overflow, division by zero and modulo by zero are typed evaluation errors, never traps
//     and never wrapped values. A constraint that cannot be evaluated REFUSES the write; it never
//     silently passes it (falsifier 4: the refusal that never silences).
//   * IDENTIFIED. `program_hash` covers the opcodes, the constants, the bound column names in order,
//     and this evaluator's own version tag — so a class pin moves when any of them moves
//     (falsifier 20: every field moves the pin).
//   * ANSWERABLE. A program records which columns it reads, so a refusal can name the offending
//     values and §4.8's `explain_refusal` has something true to print.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <cstdlib>
#include "../core/bytes.h"

namespace tapestry {
namespace writ {

// Bumped whenever the evaluator's semantics change. It is inside every program hash, so a semantic
// change invalidates every pin that depended on the old meaning rather than silently regrading history.
inline const char* const EVAL_VERSION = "tapestry-expr-v1";

enum class Ty : uint8_t { Int, Bool };

enum Op : uint8_t {
    OP_CONST,      // push consts[a]
    OP_LOAD,       // push row[a]
    OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,
    OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
    OP_NOT,
    OP_JZ,         // pop; if false jump to a
    OP_JNZ,        // pop; if true  jump to a
    OP_DUP,        // duplicate top (short-circuit result)
    OP_POP,
    OP_ABS, OP_MIN, OP_MAX,
};

struct Ins { Op op; uint32_t a; };

// The typed reasons this language can produce. Compile-time ones are rule defects (the rule is
// refused at admission); run-time ones are constraint refusals of the write being validated.
inline const char* const E_PARSE          = "expr_parse";
inline const char* const E_UNKNOWN_COLUMN = "expr_unknown_column";
inline const char* const E_TYPE           = "expr_type";
inline const char* const E_NOT_BOOL       = "expr_not_bool";
inline const char* const E_TOO_COMPLEX    = "expr_too_complex";
inline const char* const E_OVERFLOW       = "expr_overflow";
inline const char* const E_DIV_ZERO       = "expr_div_by_zero";
inline const char* const E_STACK          = "expr_stack";

// A binding is the ordered list of column names a program may read. The ORDER is part of the program
// hash, so two classes that bind the same names differently are two different programs.
struct Binding {
    std::vector<std::string> names;
    int index_of(const std::string& n) const {
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == n) return (int)i;
        return -1;
    }
};

struct Program {
    std::vector<Ins>     code;
    std::vector<int64_t> consts;
    std::vector<uint8_t> reads;        // one flag per bound column: did this program load it
    std::string          source;       // the expression as written, kept for explain_refusal
    std::string          program_hash; // see hash_program
    uint32_t             max_stack = 0;
    bool                 ok = false;
};

// ---- the compiler ---------------------------------------------------------------------------------
// Recursive descent, one pass, no allocation beyond the program. Grammar, loosest first:
//   or   := and ( "||" and )*
//   and  := cmp ( "&&" cmp )*
//   cmp  := add ( ("=="|"!="|"<"|"<="|">"|">=") add )?      -- non-associative, on purpose
//   add  := mul ( ("+"|"-") mul )*
//   mul  := un  ( ("*"|"/"|"%") un )*
//   un   := ("!"|"-")? prim
//   prim := INT | IDENT | "(" or ")" | fn "(" or ["," or] ")"      fn in {abs, min, max}
class Compiler {
public:
    Compiler(const std::string& src, const Binding& b) : s_(src), b_(b) {}

    bool compile(Program* out, std::string* reason) {
        p_ = 0; depth_ = 0; stack_ = 0; max_stack_ = 0;
        prog_ = Program();
        prog_.source = s_;
        prog_.reads.assign(b_.names.size(), 0);
        Ty t;
        if (!parse_or(&t, reason)) return false;
        skip_ws();
        if (p_ != s_.size()) return fail(E_PARSE, reason);
        if (t != Ty::Bool) return fail(E_NOT_BOOL, reason);
        if (max_stack_ > 120) return fail(E_TOO_COMPLEX, reason);   // the evaluator's stack is fixed
        prog_.max_stack = max_stack_;
        prog_.program_hash = hash_program(prog_, b_);
        prog_.ok = true;
        *out = prog_;
        return true;
    }

    // The identity of a compiled constraint: the opcodes, the constants, the bound names in order and
    // the evaluator's version. Note the SOURCE TEXT IS NOT IN IT — two spellings of the same program
    // are the same constraint, and reformatting a rule must not invalidate a pin.
    static std::string hash_program(const Program& pr, const Binding& b) {
        Blake2b h; h.init(32);
        h.update((const uint8_t*)EVAL_VERSION, std::char_traits<char>::length(EVAL_VERSION));
        h.update((const uint8_t*)"\x1f", 1);
        for (const std::string& n : b.names) { h.update(n); h.update((const uint8_t*)"\x1f", 1); }
        h.update((const uint8_t*)"\x1e", 1);
        for (const Ins& i : pr.code) {
            const uint8_t o = (uint8_t)i.op;
            h.update(&o, 1);
            uint8_t le[4] = { (uint8_t)(i.a & 0xff), (uint8_t)((i.a >> 8) & 0xff),
                              (uint8_t)((i.a >> 16) & 0xff), (uint8_t)((i.a >> 24) & 0xff) };
            h.update(le, 4);
        }
        h.update((const uint8_t*)"\x1e", 1);
        for (int64_t c : pr.consts) {
            const uint64_t u = (uint64_t)c;
            uint8_t le[8];
            for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((u >> (8 * k)) & 0xff);
            h.update(le, 8);
        }
        uint8_t out[32]; h.finish(out);
        return hex_of(out, 32);
    }

private:
    bool fail(const char* r, std::string* reason) {
        if (reason) *reason = std::string(r) + " at offset " + std::to_string(p_);
        return false;
    }
    void skip_ws() { while (p_ < s_.size() && (s_[p_] == ' ' || s_[p_] == '\t')) ++p_; }
    bool eat(const char* lit) {
        skip_ws();
        const size_t n = std::char_traits<char>::length(lit);
        if (s_.compare(p_, n, lit) != 0) return false;
        // An operator must not be the prefix of a longer one: "<" must not eat "<=".
        if (n == 1 && (lit[0] == '<' || lit[0] == '>' || lit[0] == '!' || lit[0] == '=')
            && p_ + 1 < s_.size() && s_[p_ + 1] == '=') return false;
        if (n == 1 && lit[0] == '&' && p_ + 1 < s_.size() && s_[p_ + 1] == '&') return false;
        if (n == 1 && lit[0] == '|' && p_ + 1 < s_.size() && s_[p_ + 1] == '|') return false;
        p_ += n; return true;
    }
    void emit(Op o, uint32_t a = 0) { prog_.code.push_back(Ins{o, a}); }
    void push_stack(int n) {
        stack_ += n;
        if (stack_ > (int)max_stack_) max_stack_ = (uint32_t)stack_;
    }

    bool parse_or(Ty* t, std::string* reason) {
        if (depth_ > 64) return fail(E_TOO_COMPLEX, reason);
        ++depth_;
        if (!parse_and(t, reason)) return false;
        while (true) {
            skip_ws();
            if (!eat("||")) break;
            if (*t != Ty::Bool) return fail(E_TYPE, reason);
            // short circuit: dup; jnz end; pop; <rhs>; end:
            emit(OP_DUP); push_stack(1);
            const size_t jmp = prog_.code.size(); emit(OP_JNZ, 0); push_stack(-1);
            emit(OP_POP); push_stack(-1);
            Ty rt;
            if (!parse_and(&rt, reason)) return false;
            if (rt != Ty::Bool) return fail(E_TYPE, reason);
            prog_.code[jmp].a = (uint32_t)prog_.code.size();
        }
        --depth_;
        return true;
    }
    bool parse_and(Ty* t, std::string* reason) {
        if (!parse_cmp(t, reason)) return false;
        while (true) {
            skip_ws();
            if (!eat("&&")) break;
            if (*t != Ty::Bool) return fail(E_TYPE, reason);
            emit(OP_DUP); push_stack(1);
            const size_t jmp = prog_.code.size(); emit(OP_JZ, 0); push_stack(-1);
            emit(OP_POP); push_stack(-1);
            Ty rt;
            if (!parse_cmp(&rt, reason)) return false;
            if (rt != Ty::Bool) return fail(E_TYPE, reason);
            prog_.code[jmp].a = (uint32_t)prog_.code.size();
        }
        return true;
    }
    bool parse_cmp(Ty* t, std::string* reason) {
        if (!parse_add(t, reason)) return false;
        skip_ws();
        Op o;
        if      (eat("==")) o = OP_EQ;
        else if (eat("!=")) o = OP_NE;
        else if (eat("<=")) o = OP_LE;
        else if (eat(">=")) o = OP_GE;
        else if (eat("<"))  o = OP_LT;
        else if (eat(">"))  o = OP_GT;
        else return true;
        if (*t != Ty::Int) return fail(E_TYPE, reason);
        Ty rt;
        if (!parse_add(&rt, reason)) return false;
        if (rt != Ty::Int) return fail(E_TYPE, reason);
        emit(o); push_stack(-1);
        *t = Ty::Bool;
        return true;
    }
    bool parse_add(Ty* t, std::string* reason) {
        if (!parse_mul(t, reason)) return false;
        while (true) {
            skip_ws();
            Op o;
            if      (eat("+")) o = OP_ADD;
            else if (eat("-")) o = OP_SUB;
            else break;
            if (*t != Ty::Int) return fail(E_TYPE, reason);
            Ty rt;
            if (!parse_mul(&rt, reason)) return false;
            if (rt != Ty::Int) return fail(E_TYPE, reason);
            emit(o); push_stack(-1);
        }
        return true;
    }
    bool parse_mul(Ty* t, std::string* reason) {
        if (!parse_unary(t, reason)) return false;
        while (true) {
            skip_ws();
            Op o;
            if      (eat("*")) o = OP_MUL;
            else if (eat("/")) o = OP_DIV;
            else if (eat("%")) o = OP_MOD;
            else break;
            if (*t != Ty::Int) return fail(E_TYPE, reason);
            Ty rt;
            if (!parse_unary(&rt, reason)) return false;
            if (rt != Ty::Int) return fail(E_TYPE, reason);
            emit(o); push_stack(-1);
        }
        return true;
    }
    bool parse_unary(Ty* t, std::string* reason) {
        skip_ws();
        if (eat("!")) {
            if (!parse_unary(t, reason)) return false;
            if (*t != Ty::Bool) return fail(E_TYPE, reason);
            emit(OP_NOT);
            return true;
        }
        if (eat("-")) {
            if (!parse_unary(t, reason)) return false;
            if (*t != Ty::Int) return fail(E_TYPE, reason);
            emit(OP_NEG);
            return true;
        }
        return parse_primary(t, reason);
    }
    bool parse_primary(Ty* t, std::string* reason) {
        skip_ws();
        if (p_ >= s_.size()) return fail(E_PARSE, reason);
        if (eat("(")) {
            if (!parse_or(t, reason)) return false;
            if (!eat(")")) return fail(E_PARSE, reason);
            return true;
        }
        const char c = s_[p_];
        if (c >= '0' && c <= '9') {
            // A decimal integer, and only that. No hex, no underscores, no leading zeros: one spelling.
            const size_t start = p_;
            while (p_ < s_.size() && s_[p_] >= '0' && s_[p_] <= '9') ++p_;
            const std::string lit = s_.substr(start, p_ - start);
            if (lit.size() > 1 && lit[0] == '0') return fail(E_PARSE, reason);
            errno = 0;
            const long long v = std::strtoll(lit.c_str(), nullptr, 10);
            if (errno == ERANGE) return fail(E_OVERFLOW, reason);
            prog_.consts.push_back((int64_t)v);
            emit(OP_CONST, (uint32_t)(prog_.consts.size() - 1)); push_stack(1);
            *t = Ty::Int;
            return true;
        }
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
            const size_t start = p_;
            while (p_ < s_.size() && ((s_[p_] >= 'a' && s_[p_] <= 'z') || (s_[p_] >= 'A' && s_[p_] <= 'Z')
                                      || (s_[p_] >= '0' && s_[p_] <= '9') || s_[p_] == '_')) ++p_;
            const std::string id = s_.substr(start, p_ - start);
            skip_ws();
            if (p_ < s_.size() && s_[p_] == '(') {           // a function call
                ++p_;
                if (id == "abs") {
                    Ty at;
                    if (!parse_or(&at, reason)) return false;
                    if (at != Ty::Int) return fail(E_TYPE, reason);
                    if (!eat(")")) return fail(E_PARSE, reason);
                    emit(OP_ABS); *t = Ty::Int; return true;
                }
                if (id == "min" || id == "max") {
                    Ty at, bt;
                    if (!parse_or(&at, reason)) return false;
                    if (!eat(",")) return fail(E_PARSE, reason);
                    if (!parse_or(&bt, reason)) return false;
                    if (!eat(")")) return fail(E_PARSE, reason);
                    if (at != Ty::Int || bt != Ty::Int) return fail(E_TYPE, reason);
                    emit(id == "min" ? OP_MIN : OP_MAX); push_stack(-1);
                    *t = Ty::Int; return true;
                }
                return fail(E_UNKNOWN_COLUMN, reason);
            }
            if (id == "true" || id == "false") {
                prog_.consts.push_back(id == "true" ? 1 : 0);
                emit(OP_CONST, (uint32_t)(prog_.consts.size() - 1)); push_stack(1);
                *t = Ty::Bool;
                return true;
            }
            const int idx = b_.index_of(id);
            if (idx < 0) { if (reason) *reason = std::string(E_UNKNOWN_COLUMN) + ": " + id; return false; }
            prog_.reads[(size_t)idx] = 1;
            emit(OP_LOAD, (uint32_t)idx); push_stack(1);
            *t = Ty::Int;
            return true;
        }
        return fail(E_PARSE, reason);
    }

    const std::string& s_;
    const Binding& b_;
    size_t p_ = 0;
    int depth_ = 0, stack_ = 0;
    uint32_t max_stack_ = 0;
    Program prog_;
};

inline bool compile(const std::string& src, const Binding& b, Program* out, std::string* reason) {
    Compiler c(src, b);
    return c.compile(out, reason);
}

// ---- the evaluator ---------------------------------------------------------------------------------
// Total: every arithmetic fault is a typed reason, not a trap and not a wrapped value. A constraint
// that cannot be evaluated is a REFUSAL of the write, never a pass.
inline bool eval(const Program& pr, const int64_t* row, size_t ncols, bool* out, std::string* reason) {
    auto fail = [&](const char* r) { if (reason) *reason = r; return false; };
    if (!pr.ok) return fail(E_STACK);
    int64_t st[128];
    int sp = 0;
    const size_t n = pr.code.size();
    size_t pc = 0;
    while (pc < n) {
        const Ins& i = pr.code[pc];
        auto need = [&](int k) { return sp >= k; };
        switch (i.op) {
            case OP_CONST: if (sp >= 128) return fail(E_STACK);
                           if (i.a >= pr.consts.size()) return fail(E_STACK);
                           st[sp++] = pr.consts[i.a]; break;
            case OP_LOAD:  if (sp >= 128) return fail(E_STACK);
                           if (i.a >= ncols) return fail(E_UNKNOWN_COLUMN);
                           st[sp++] = row[i.a]; break;
            case OP_DUP:   if (sp >= 128 || !need(1)) return fail(E_STACK); st[sp] = st[sp - 1]; ++sp; break;
            case OP_POP:   if (!need(1)) return fail(E_STACK); --sp; break;
            case OP_JZ:    if (!need(1)) return fail(E_STACK);
                           if (i.a > n) return fail(E_STACK);
                           if (st[--sp] == 0) { pc = i.a; continue; }
                           break;
            case OP_JNZ:   if (!need(1)) return fail(E_STACK);
                           if (i.a > n) return fail(E_STACK);
                           if (st[--sp] != 0) { pc = i.a; continue; }
                           break;
            case OP_NOT:   if (!need(1)) return fail(E_STACK); st[sp - 1] = (st[sp - 1] == 0) ? 1 : 0; break;
            case OP_NEG:   if (!need(1)) return fail(E_STACK);
                           if (st[sp - 1] == INT64_MIN) return fail(E_OVERFLOW);
                           st[sp - 1] = -st[sp - 1]; break;
            case OP_ABS:   if (!need(1)) return fail(E_STACK);
                           if (st[sp - 1] == INT64_MIN) return fail(E_OVERFLOW);
                           if (st[sp - 1] < 0) st[sp - 1] = -st[sp - 1]; break;
            default: {
                if (!need(2)) return fail(E_STACK);
                const int64_t b2 = st[--sp];
                const int64_t a2 = st[sp - 1];
                int64_t r = 0;
                switch (i.op) {
                    case OP_ADD:
                        if ((b2 > 0 && a2 > INT64_MAX - b2) || (b2 < 0 && a2 < INT64_MIN - b2)) return fail(E_OVERFLOW);
                        r = a2 + b2; break;
                    case OP_SUB:
                        if ((b2 < 0 && a2 > INT64_MAX + b2) || (b2 > 0 && a2 < INT64_MIN + b2)) return fail(E_OVERFLOW);
                        r = a2 - b2; break;
                    case OP_MUL: {
                        // Checked before multiplying: signed overflow is undefined behaviour, so it
                        // cannot be detected after the fact.
                        if (a2 > 0) {
                            if (b2 > 0)      { if (a2 > INT64_MAX / b2) return fail(E_OVERFLOW); }
                            else if (b2 < 0) { if (b2 < INT64_MIN / a2) return fail(E_OVERFLOW); }
                        } else if (a2 < 0) {
                            if (b2 > 0)      { if (a2 < INT64_MIN / b2) return fail(E_OVERFLOW); }
                            else if (b2 < 0) { if (b2 < INT64_MAX / a2) return fail(E_OVERFLOW); }
                        }
                        r = a2 * b2; break;
                    }
                    case OP_DIV:
                        if (b2 == 0) return fail(E_DIV_ZERO);
                        if (a2 == INT64_MIN && b2 == -1) return fail(E_OVERFLOW);
                        r = a2 / b2; break;                       // C++ truncation toward zero, one rule
                    case OP_MOD:
                        if (b2 == 0) return fail(E_DIV_ZERO);
                        if (a2 == INT64_MIN && b2 == -1) return fail(E_OVERFLOW);
                        r = a2 % b2; break;
                    case OP_EQ: r = (a2 == b2); break;
                    case OP_NE: r = (a2 != b2); break;
                    case OP_LT: r = (a2 <  b2); break;
                    case OP_LE: r = (a2 <= b2); break;
                    case OP_GT: r = (a2 >  b2); break;
                    case OP_GE: r = (a2 >= b2); break;
                    case OP_MIN: r = (a2 < b2) ? a2 : b2; break;
                    case OP_MAX: r = (a2 > b2) ? a2 : b2; break;
                    default: return fail(E_STACK);
                }
                st[sp - 1] = r;
                break;
            }
        }
        ++pc;
    }
    if (sp != 1) return fail(E_STACK);
    *out = (st[0] != 0);
    return true;
}

} // namespace writ
} // namespace tapestry
