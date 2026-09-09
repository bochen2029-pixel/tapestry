// =====================================================================================================
// TAPESTRY · src/writ/enrich.h · join paths, reducers, back-fill, and the completeness assertion
//
// Blueprint §3.3's enrichment paragraph, which exists because QC-2 F8 found three ingest defects that
// conservation cannot see — the projection can be perfectly conserved and still carry the wrong number
// into every cell, because conservation checks that what was ingested lands somewhere, not that what
// was ingested was right.
//
//   1 · AN EXPLICIT JOIN PATH. A side table reaches the class table by a declared
//       `Enrich{table, key_col, via_table, via_col}`, never by a column name that happens to match.
//
//   2 · A REDUCER PER ENRICHED COLUMN, FROM A CLOSED SET, WITH NO DEFAULT. `{sum, min, max, first,
//       last, count, distinct_count}` and nothing else, and an enriched column that does not name one
//       is REFUSED at map load. The default is the whole defect: an order with three line items has
//       one price only if somebody chose `sum`, and if nobody chose, the last row silently wins.
//       That is falsifier 19's planted lie, and it is a real number being wrong rather than missing.
//
//   3 · QUALIFIED COLUMN REFERENCES. `order_items.price`, never `price`.
//
//   4 · BACK-FILL AS ITS OWN DECLARATION. A side row may supply named columns of a cell it does not
//       own; anything else it carries is refused rather than quietly written.
//
//   5 · A COMPLETENESS ASSERTION AT MAP LOAD. Every column the class names must resolve from the class
//       table or a declared join, or the map does not load. A map that half-resolves is a map that
//       ingests half the truth and reports success.
// =====================================================================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <set>
#include <map>
#include "../core/bytes.h"

namespace tapestry {
namespace writ {

inline const char* const E_NO_REDUCER      = "enrich_no_reducer";
inline const char* const E_UNKNOWN_REDUCER = "enrich_unknown_reducer";
inline const char* const E_UNQUALIFIED     = "enrich_unqualified_column";
inline const char* const E_UNRESOLVED      = "enrich_unresolved_column";
inline const char* const E_NO_JOIN         = "enrich_no_join_path";
inline const char* const E_UNKNOWN_TABLE   = "enrich_unknown_table";
inline const char* const E_UNKNOWN_COL     = "enrich_unknown_source_column";
inline const char* const E_BACKFILL_UNDECL = "enrich_backfill_undeclared";

// The closed set. Adding one here is a schema change that moves every class pin that uses it.
enum class Reducer : uint8_t { Sum, Min, Max, First, Last, Count, DistinctCount };

inline const char* reducer_name(Reducer r) {
    switch (r) {
        case Reducer::Sum:           return "sum";
        case Reducer::Min:           return "min";
        case Reducer::Max:           return "max";
        case Reducer::First:         return "first";
        case Reducer::Last:          return "last";
        case Reducer::Count:         return "count";
        case Reducer::DistinctCount: return "distinct_count";
    }
    return "?";
}
// Note the absence of a default argument and of a fallback branch: an unrecognised name is an error
// the caller must handle, not a silently chosen reducer.
inline bool reducer_from_name(const std::string& s, Reducer* out) {
    if (s == "sum")            { *out = Reducer::Sum;           return true; }
    if (s == "min")            { *out = Reducer::Min;           return true; }
    if (s == "max")            { *out = Reducer::Max;           return true; }
    if (s == "first")          { *out = Reducer::First;         return true; }
    if (s == "last")           { *out = Reducer::Last;          return true; }
    if (s == "count")          { *out = Reducer::Count;         return true; }
    if (s == "distinct_count") { *out = Reducer::DistinctCount; return true; }
    return false;
}

// ---- the accumulator --------------------------------------------------------------------------------
// Integer only, like everything else that reaches a fold. `first` and `last` are defined by the order
// rows arrive from the source, which is the source's own order — so a class that uses them is
// declaring a dependency on that order, and saying so out loud is the point of having them named.
struct Acc {
    Reducer red = Reducer::Sum;
    int64_t v = 0;
    uint64_t n = 0;
    bool has = false;
    std::set<std::string> distinct;

    void reset(Reducer r) { red = r; v = 0; n = 0; has = false; distinct.clear(); }
    bool add(int64_t x, const std::string& raw) {
        ++n;
        switch (red) {
            case Reducer::Sum: {
                if ((x > 0 && v > INT64_MAX - x) || (x < 0 && v < INT64_MIN - x)) return false;
                v += x; break;
            }
            case Reducer::Min:   if (!has || x < v) v = x; break;
            case Reducer::Max:   if (!has || x > v) v = x; break;
            case Reducer::First: if (!has) v = x; break;
            case Reducer::Last:  v = x; break;
            case Reducer::Count: v = (int64_t)n; break;
            case Reducer::DistinctCount:
                distinct.insert(raw);
                v = (int64_t)distinct.size();
                break;
        }
        has = true;
        return true;
    }
    int64_t value() const { return has ? v : 0; }
};

// ---- the declarations -------------------------------------------------------------------------------
struct Enrich {
    std::string table;      // the side table, e.g. "order_items"
    std::string key_col;    // its joining column,  e.g. "order_id"
    std::string via_table;  // the class table,     e.g. "orders"
    std::string via_col;    // its joining column,  e.g. "order_id"
};

struct EnrichedCol {
    std::string target;     // the cell field this fills, e.g. "amount_minor"
    std::string source;     // QUALIFIED, e.g. "order_items.price"
    Reducer     red = Reducer::Sum;
    bool        reducer_declared = false;   // false is a refusal, never a default
};

struct BackFill {
    std::string table;                      // a row from this table may supply...
    std::vector<std::string> cols;          // ...exactly these target columns
};

// What the store knows about the shape of the world's tables. The completeness assertion is checked
// against this, so a map cannot name a column no source has.
struct TableSchema {
    std::map<std::string, std::vector<std::string>> tables;
    void add(const std::string& t, const std::vector<std::string>& cols) { tables[t] = cols; }
    bool has_table(const std::string& t) const { return tables.count(t) != 0; }
    bool has_column(const std::string& t, const std::string& c) const {
        auto it = tables.find(t);
        if (it == tables.end()) return false;
        for (const std::string& x : it->second) if (x == c) return true;
        return false;
    }
};

inline bool split_qualified(const std::string& s, std::string* table, std::string* col) {
    const size_t dot = s.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= s.size()) return false;
    if (s.find('.', dot + 1) != std::string::npos) return false;      // exactly one dot
    *table = s.substr(0, dot);
    *col = s.substr(dot + 1);
    return true;
}

// A class's ingest declaration: which table it is grained on, which columns it fills from where, which
// side tables reach it and by what path, and what a side row is allowed to back-fill.
struct IngestDef {
    std::string              class_table;   // the grain: one cell per row of this table
    std::string              key_col;       // that table's identity column
    std::vector<EnrichedCol> columns;       // every field of the cell, qualified
    std::vector<Enrich>      joins;
    std::vector<BackFill>    back_fill;

    // THE COMPLETENESS ASSERTION. Every named column must resolve from the class table or a declared
    // join; every enriched column must name a reducer; every reference must be qualified. A map that
    // fails any of these does not load, and the reason names the column.
    bool validate(const TableSchema& schema, std::string* reason) const {
        auto fail = [&](const char* r, const std::string& what) {
            if (reason) *reason = std::string(r) + ": " + what;
            return false;
        };
        if (!schema.has_table(class_table)) return fail(E_UNKNOWN_TABLE, class_table);
        if (!schema.has_column(class_table, key_col)) return fail(E_UNKNOWN_COL, class_table + "." + key_col);

        for (const Enrich& j : joins) {
            if (!schema.has_table(j.table))    return fail(E_UNKNOWN_TABLE, j.table);
            if (!schema.has_column(j.table, j.key_col)) return fail(E_UNKNOWN_COL, j.table + "." + j.key_col);
            if (j.via_table != class_table)    return fail(E_NO_JOIN, j.table + " -> " + j.via_table);
            if (!schema.has_column(j.via_table, j.via_col)) return fail(E_UNKNOWN_COL, j.via_table + "." + j.via_col);
        }
        for (const EnrichedCol& c : columns) {
            std::string t, col;
            if (!split_qualified(c.source, &t, &col)) return fail(E_UNQUALIFIED, c.source);
            if (!schema.has_table(t))                return fail(E_UNKNOWN_TABLE, t);
            if (!schema.has_column(t, col))          return fail(E_UNKNOWN_COL, c.source);
            if (t == class_table) {
                // The grain's own columns need no reducer: one row, one value.
                continue;
            }
            // Anything off the grain must have BOTH a declared join path and a declared reducer.
            bool joined = false;
            for (const Enrich& j : joins) if (j.table == t) { joined = true; break; }
            if (!joined) return fail(E_NO_JOIN, c.source);
            if (!c.reducer_declared) return fail(E_NO_REDUCER, c.source);
        }
        for (const BackFill& b : back_fill) {
            if (!schema.has_table(b.table)) return fail(E_UNKNOWN_TABLE, b.table);
            bool joined = (b.table == class_table);
            for (const Enrich& j : joins) if (j.table == b.table) { joined = true; break; }
            if (!joined) return fail(E_NO_JOIN, b.table);
            for (const std::string& c : b.cols) {
                bool named = false;
                for (const EnrichedCol& e : columns) if (e.target == c) { named = true; break; }
                if (!named) return fail(E_UNRESOLVED, b.table + " back-fills " + c);
            }
        }
        return true;
    }

    // May a row from `table` supply `target`? A side row that carries anything else is refused rather
    // than quietly written — §3.3's "back_fill declaration naming which columns a side row may supply".
    bool may_back_fill(const std::string& table, const std::string& target) const {
        for (const BackFill& b : back_fill)
            if (b.table == table)
                for (const std::string& c : b.cols) if (c == target) return true;
        return false;
    }

    // The ingest declaration's contribution to the class pin. Every field, length-prefixed, so a
    // reducer changing from `last` to `sum` moves the pin and invalidates what was derived under it.
    std::string pin() const {
        Blake2b h; h.init(32);
        auto s = [&](const std::string& x) {
            const uint64_t n = x.size();
            uint8_t le[8]; for (int k = 0; k < 8; ++k) le[k] = (uint8_t)((n >> (8 * k)) & 0xff);
            h.update(le, 8); h.update(x);
        };
        s("tapestry-ingest-pin-v1");
        s(class_table); s(key_col);
        for (const EnrichedCol& c : columns) {
            s(c.target); s(c.source);
            s(c.reducer_declared ? reducer_name(c.red) : "<none>");
        }
        for (const Enrich& j : joins) { s(j.table); s(j.key_col); s(j.via_table); s(j.via_col); }
        for (const BackFill& b : back_fill) { s(b.table); for (const std::string& c : b.cols) s(c); }
        uint8_t out[32]; h.finish(out);
        return hex_of(out, 32);
    }
};

} // namespace writ
} // namespace tapestry
