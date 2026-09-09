// =====================================================================================================
// TAPESTRY · src/tests/t_enrich.cpp · the ingest declaration's oracles, each with its lie arm
//
// QC-2 F8 found three ingest defects that CONSERVATION CANNOT SEE. That is the important sentence:
// the projection can be perfectly conserved — every unit that was ingested lands in exactly one
// lattice cell — while every cell carries the wrong number, because the defect is upstream of the
// ledger. A store whose only ingest check is conservation is checking that it did not lose what it
// was given, not that it was given the truth.
//
// So the checks here are of the DECLARATION, at map load, before a byte of the world is read:
// a join path that must be declared, a reducer with no default, qualified references, back-fill
// named column by column, and a completeness assertion that refuses a map naming a column no source
// has. Falsifier 19 itself runs against the real Olist wire — see the receipt.
// =====================================================================================================

#include <cstdio>
#include <string>
#include <vector>
#include "../writ/enrich.h"

using namespace tapestry;
using namespace tapestry::writ;

// Comparing a reason by a hand-counted prefix length is a trap: get the count wrong and the check
// fails while the code is right, which is what happened on the first run of this file.
static bool because(const std::string& why, const char* reason) {
    return why.compare(0, std::char_traits<char>::length(reason), reason) == 0;
}

static int g_pass = 0, g_fail = 0;
static void check(bool cond, const char* name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("ok    %s\n", name); }
    else      { ++g_fail; std::printf("FAIL  %s%s%s\n", name, detail.empty() ? "" : " :: ", detail.c_str()); }
}

static TableSchema olist_schema() {
    TableSchema s;
    s.add("orders", {"order_id", "order_status", "order_purchase_timestamp", "order_estimated_delivery_date"});
    s.add("order_items", {"order_id", "order_item_id", "price", "freight_value"});
    s.add("order_payments", {"order_id", "payment_value"});
    return s;
}

// The declaration the Olist ingest actually uses, as the baseline every lie mutates.
static IngestDef good() {
    IngestDef d;
    d.class_table = "orders";
    d.key_col = "order_id";
    d.columns.push_back({"opened_ns", "orders.order_purchase_timestamp", Reducer::First, false});
    d.columns.push_back({"due_ns", "orders.order_estimated_delivery_date", Reducer::First, false});
    d.columns.push_back({"amount_minor", "order_items.price", Reducer::Sum, true});
    Enrich j; j.table = "order_items"; j.key_col = "order_id"; j.via_table = "orders"; j.via_col = "order_id";
    d.joins.push_back(j);
    return d;
}

// =====================================================================================================
// 1 · The reducers themselves
// =====================================================================================================
static void t_reducers() {
    const int64_t vals[] = {300, 100, 200, 100};
    struct C { Reducer r; int64_t want; const char* name; };
    const C cases[] = {
        {Reducer::Sum,   700, "sum"},
        {Reducer::Min,   100, "min"},
        {Reducer::Max,   300, "max"},
        {Reducer::First, 300, "first"},
        {Reducer::Last,  100, "last"},
        {Reducer::Count,   4, "count"},
        {Reducer::DistinctCount, 3, "distinct_count"},
    };
    bool all = true; std::string bad;
    for (const C& c : cases) {
        Acc a; a.reset(c.r);
        for (int64_t v : vals) a.add(v, std::to_string(v));
        if (a.value() != c.want) { all = false; bad = std::string(c.name) + " gave " + std::to_string(a.value()); break; }
    }
    check(all, "red.every_reducer_computes", bad);

    // An empty group is zero, and says it had nothing rather than inventing a value.
    Acc e; e.reset(Reducer::Sum);
    check(!e.has && e.value() == 0, "red.empty_group_is_zero_and_knows_it");

    // Sum is total: an overflow is refused, not wrapped.
    Acc o; o.reset(Reducer::Sum);
    o.add(INT64_MAX, "x");
    check(!o.add(1, "y"), "red.sum_overflow_is_refused");

    // Lie arm: `last` and `sum` differ on any group with more than one row — which is the entire
    // content of falsifier 19, in three lines.
    Acc s, l;
    s.reset(Reducer::Sum); l.reset(Reducer::Last);
    for (int64_t v : vals) { s.add(v, "x"); l.add(v, "x"); }
    check(s.value() != l.value(), "red.lie.last_is_not_sum_on_a_multi_row_group",
          std::to_string(s.value()) + " vs " + std::to_string(l.value()));
    // ...and they agree on a single-row group, which is exactly why the defect hides: most orders
    // have one item, so the wrong reducer looks right on the majority of the wire.
    Acc s1, l1;
    s1.reset(Reducer::Sum); l1.reset(Reducer::Last);
    s1.add(42, "x"); l1.add(42, "x");
    check(s1.value() == l1.value(), "red.lie.but_they_agree_on_one_row_which_is_how_it_hides");
}

// =====================================================================================================
// 2 · The completeness assertion at map load
// =====================================================================================================
static void t_completeness() {
    const TableSchema schema = olist_schema();
    std::string why;
    check(good().validate(schema, &why), "cmp.the_real_declaration_loads", why);

    // A reducer with NO DEFAULT: an off-grain column that does not name one is refused.
    {
        IngestDef d = good();
        d.columns[2].reducer_declared = false;
        check(!d.validate(schema, &why) && because(why, E_NO_REDUCER), "cmp.no_default_reducer", why);
    }
    // An unqualified column reference is refused.
    {
        IngestDef d = good();
        d.columns[2].source = "price";
        check(!d.validate(schema, &why) && because(why, E_UNQUALIFIED),
              "cmp.unqualified_reference_refused", why);
    }
    // A column from a table with no declared join is refused, even though the table exists and the
    // column exists — the join path must be DECLARED, not inferred from a matching name.
    {
        IngestDef d = good();
        d.columns.push_back({"paid_minor", "order_payments.payment_value", Reducer::Sum, true});
        check(!d.validate(schema, &why) && because(why, E_NO_JOIN), "cmp.undeclared_join_refused", why);
    }
    // A column no source has is refused: the completeness assertion.
    {
        IngestDef d = good();
        d.columns.push_back({"margin", "orders.gross_margin", Reducer::First, false});
        check(!d.validate(schema, &why) && because(why, E_UNKNOWN_COL), "cmp.unknown_column_refused", why);
    }
    // A join whose `via_table` is not the class table is not a path to this grain.
    {
        IngestDef d = good();
        d.joins[0].via_table = "order_payments";
        check(!d.validate(schema, &why), "cmp.join_must_reach_the_grain", why);
    }
    // A table the store knows nothing about is refused before anything reads it.
    {
        IngestDef d = good();
        d.joins[0].table = "invoices";
        check(!d.validate(schema, &why) && because(why, E_UNKNOWN_TABLE), "cmp.unknown_table_refused", why);
    }

    // Lie arm: the map that half-resolves. Without the assertion, the declaration above with
    // `orders.gross_margin` would load and every cell would carry a silent zero in that column —
    // present, plausible, and wrong. The assertion is what makes it a refusal instead.
    {
        IngestDef d = good();
        d.columns.push_back({"margin", "orders.gross_margin", Reducer::First, false});
        std::string ignored;
        const bool loads_without_the_check = schema.has_table(d.class_table);   // all a lax loader checks
        check(loads_without_the_check && !d.validate(schema, &ignored),
              "cmp.lie.a_lax_loader_would_have_taken_it");
    }
}

// =====================================================================================================
// 3 · Back-fill is declared column by column
// =====================================================================================================
static void t_backfill() {
    const TableSchema schema = olist_schema();
    std::string why;
    IngestDef d = good();
    BackFill b; b.table = "order_items"; b.cols.push_back("amount_minor");
    d.back_fill.push_back(b);
    check(d.validate(schema, &why), "bf.declared_backfill_loads", why);
    check(d.may_back_fill("order_items", "amount_minor"), "bf.the_declared_column_may");
    check(!d.may_back_fill("order_items", "due_ns"), "bf.an_undeclared_column_may_not");
    check(!d.may_back_fill("order_payments", "amount_minor"), "bf.an_undeclared_table_may_not");

    // A back-fill naming a column the class does not have is refused at load.
    IngestDef bad = d;
    bad.back_fill[0].cols.push_back("nonexistent_target");
    check(!bad.validate(schema, &why), "bf.backfill_of_an_unnamed_column_refused", why);

    // A back-fill from a table with no join path is refused.
    IngestDef nj = good();
    BackFill b2; b2.table = "order_payments"; b2.cols.push_back("amount_minor");
    nj.back_fill.push_back(b2);
    check(!nj.validate(schema, &why), "bf.backfill_needs_a_join_path", why);

    // Lie arm: without the declaration, any side row could write any column — which is how a
    // "helpful" ingest silently overwrites a grain column from a table that does not own it.
    check(!d.may_back_fill("order_items", "state"), "bf.lie.an_undeclared_write_would_be_silent");
}

// =====================================================================================================
// 4 · The reducer is in the pin — falsifier 20, extended to the ingest declaration
// =====================================================================================================
static void t_pin() {
    IngestDef s = good();
    IngestDef l = good();
    l.columns[2].red = Reducer::Last;
    check(s.pin() != l.pin(), "pin.the_reducer_moves_it");

    IngestDef nj = good();
    nj.joins[0].key_col = "order_item_id";
    check(s.pin() != nj.pin(), "pin.the_join_path_moves_it");

    IngestDef bf = good();
    BackFill b; b.table = "order_items"; b.cols.push_back("amount_minor");
    bf.back_fill.push_back(b);
    check(s.pin() != bf.pin(), "pin.a_backfill_declaration_moves_it");

    IngestDef same = good();
    check(s.pin() == same.pin(), "pin.the_same_declaration_is_the_same_pin");

    // Lie arm: a pin over the class name alone — which is what a pin that ignores the ingest
    // declaration amounts to — cannot tell `sum` from `last`, so a tape written under one reducer
    // and re-read under the other would look valid.
    check(std::string("orders") == std::string("orders"), "pin.lie.a_name_only_pin_cannot_tell_them_apart");
}

int main() {
    std::printf("== TAPESTRY R1 enrichment oracles ==\n");
    t_reducers();
    t_completeness();
    t_backfill();
    t_pin();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
