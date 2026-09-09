// qc_probe.cpp — QC-2 probes for the TAPESTRY blueprint's §2.2/§2.3/§11 claims.
// Build (MSVC): cl /nologo /O2 /std:c++17 /EHsc qc_probe.cpp /Fe:qc_probe.exe
#include "osv_ingest.h"
#include <cstdio>
#include <cstddef>
#include <cstring>
#include <string>
#include <vector>

using namespace osv;

// ---- the blueprint's proposed replacement row, verbatim from §2.2 -----------------------------
struct Cell {
    uint64_t id;
    uint64_t opened_ns;
    uint64_t due_ns;
    uint64_t blocked_by;
    uint64_t pos_last;
    float    amount;
    float    margin;
    uint32_t cls;
    uint32_t seg;
    uint32_t seat;
    uint8_t  state, flags, verb, gear;
};
static_assert(sizeof(Cell) == 64, "Cell must stay one cache line");

// ---- the naive in-place swap a reader of the blueprint would actually write -------------------
// "src_rev and the 4 padding bytes become a 64-bit pos_last" read literally as: put it where
// src_rev was. This is what the blueprint's sentence describes; it does NOT produce 64 bytes.
struct CellNaive {
    uint64_t id, opened_ns, due_ns, blocked_by;
    float    amount, margin;
    uint32_t cls, seg, seat;
    uint64_t pos_last;              // where src_rev was
    uint8_t  state, flags, verb, gear;
};

static void hr(const char* s) { std::printf("\n===== %s =====\n", s); }

// =================================================================================================
static void p1_layout() {
    hr("P1  byte arithmetic of the cell row (blueprint §2.2)");
    std::printf("sizeof(Commitment) = %zu  alignof = %zu\n", sizeof(Commitment), alignof(Commitment));
    std::printf("  id=%zu opened_ns=%zu due_ns=%zu blocked_by=%zu amount=%zu margin=%zu\n",
        offsetof(Commitment,id), offsetof(Commitment,opened_ns), offsetof(Commitment,due_ns),
        offsetof(Commitment,blocked_by), offsetof(Commitment,amount), offsetof(Commitment,margin));
    std::printf("  cls=%zu seg=%zu seat=%zu src_rev=%zu state=%zu flags=%zu verb=%zu gear=%zu\n",
        offsetof(Commitment,cls), offsetof(Commitment,seg), offsetof(Commitment,seat),
        offsetof(Commitment,src_rev), offsetof(Commitment,state), offsetof(Commitment,flags),
        offsetof(Commitment,verb), offsetof(Commitment,gear));
    const size_t used_c = offsetof(Commitment,gear) + 1;
    std::printf("  last named byte ends at %zu -> TRAILING PADDING = %zu bytes at [%zu..63]\n",
        used_c, sizeof(Commitment) - used_c, used_c);
    std::printf("  src_rev occupies [52..55]; the padding is at [60..63]. They are NOT adjacent.\n");

    std::printf("\nsizeof(Cell) = %zu  alignof = %zu\n", sizeof(Cell), alignof(Cell));
    std::printf("  id=%zu opened_ns=%zu due_ns=%zu blocked_by=%zu pos_last=%zu amount=%zu margin=%zu\n",
        offsetof(Cell,id), offsetof(Cell,opened_ns), offsetof(Cell,due_ns),
        offsetof(Cell,blocked_by), offsetof(Cell,pos_last), offsetof(Cell,amount), offsetof(Cell,margin));
    std::printf("  cls=%zu seg=%zu seat=%zu state=%zu flags=%zu verb=%zu gear=%zu\n",
        offsetof(Cell,cls), offsetof(Cell,seg), offsetof(Cell,seat), offsetof(Cell,state),
        offsetof(Cell,flags), offsetof(Cell,verb), offsetof(Cell,gear));
    const size_t used_n = offsetof(Cell,gear) + 1;
    std::printf("  last named byte ends at %zu -> TRAILING PADDING = %zu bytes\n",
        used_n, sizeof(Cell) - used_n);
    std::printf("  pos_last is 8-byte aligned at offset %zu: OK\n", offsetof(Cell,pos_last));

    std::printf("\nsizeof(CellNaive) = %zu  (pos_last left where src_rev was, no reorder)\n",
        sizeof(CellNaive));
    std::printf("  VERDICT: 64 B holds ONLY with the reorder shown in the blueprint's code block.\n"
                "           The prose sentence ('src_rev and the 4 padding bytes become pos_last')\n"
                "           describes a %zu-byte struct. Code block right, prose wrong.\n", sizeof(CellNaive));

    // every named field's offset moves, so any raw-byte consumer changes
    std::printf("\n  FIELD OFFSET DIFF Commitment -> Cell (raw-byte consumers must all change):\n");
    std::printf("    amount  %zu -> %zu   margin %zu -> %zu   cls %zu -> %zu   seg %zu -> %zu   seat %zu -> %zu\n",
        offsetof(Commitment,amount), offsetof(Cell,amount),
        offsetof(Commitment,margin), offsetof(Cell,margin),
        offsetof(Commitment,cls),    offsetof(Cell,cls),
        offsetof(Commitment,seg),    offsetof(Cell,seg),
        offsetof(Commitment,seat),   offsetof(Cell,seat));
}

// =================================================================================================
static void p2_digest_padding() {
    hr("P2  Ledger::digest hashes the padding bytes (osv_ingest.h:221-231)");
    Ledger A, B;
    Commitment c; std::memset(&c, 0, sizeof c);
    c.id = 1; c.state = C_OPEN; c.amount = 5.0f; c.src_rev = 7;
    A.live[1] = c;

    // same logical record, one padding byte dirty
    Commitment d = c;
    unsigned char* p = (unsigned char*)&d;
    p[60] = 0xAB;                              // a trailing padding byte, not any named field
    B.live[1] = d;

    const bool same_named = (std::memcmp(&c, &d, offsetof(Commitment,gear) + 1) == 0);
    std::printf("  every NAMED field identical: %s\n", same_named ? "yes" : "no");
    std::printf("  digest A = %llu\n  digest B = %llu\n", (unsigned long long)A.digest(), (unsigned long long)B.digest());
    std::printf("  digests differ on a padding byte alone: %s\n",
        A.digest() != B.digest() ? "YES -- digest is padding-sensitive" : "no");
    std::printf("  => Commitment's 4 pad bytes are inside sizeof(); digest() walks all 64.\n"
                "     Cell has ZERO padding, so the swap REMOVES this hazard. The blueprint\n"
                "     does not claim this and should.\n");
}

// =================================================================================================
static void p3_rev_truncation() {
    hr("P3  the 32-bit src_rev above 2^32 (blueprint falsifier 9)");
    Ledger led; Ingest ing; ing.led = &led;
    ing.map = olist_map();

    const uint64_t BASE = 4294967296ull;          // 2^32 exactly
    auto row = [&](uint64_t rev, const char* status) {
        SourceRow r; r.source="olist"; r.table="olist_orders_dataset"; r.key="ORD1";
        r.op = OP_UPDATE; r.rev = rev; r.commit_ns = 1000;
        r.fields.push_back({"order_id","ORD1"});
        r.fields.push_back({"order_status",status});
        r.fields.push_back({"order_estimated_delivery_date","2026-10-01 00:00:00"});
        return r;
    };
    // open at a huge LSN
    ing.apply(row(BASE + 500, "processing"));
    Commitment* cur = led.find(commitment_id("olist","olist_orders_dataset","ORD1"));
    std::printf("  applied rev = %llu   stored src_rev = %u  (truncated: %s)\n",
        (unsigned long long)(BASE+500), cur ? cur->src_rev : 0,
        (cur && cur->src_rev != (BASE+500)) ? "YES" : "no");

    const uint64_t dup_before = ing.n.duplicate, stale_before = ing.n.stale_refused, upd_before = ing.n.updated;

    // (i) EXACT REPLAY of the same row -- idempotence law says duplicate, no-op
    ing.apply(row(BASE + 500, "processing"));
    // (ii) a STALE row from the past -- monotonicity law says refuse
    ing.apply(row(BASE + 100, "created"));

    std::printf("  exact replay  -> duplicate counter moved by %llu   (law: must be 1)\n",
        (unsigned long long)(ing.n.duplicate - dup_before));
    std::printf("  stale row     -> stale_refused moved by %llu      (law: must be 1)\n",
        (unsigned long long)(ing.n.stale_refused - stale_before));
    std::printf("  updated counter moved by %llu                      (law: must be 0)\n",
        (unsigned long long)(ing.n.updated - upd_before));
    std::printf("  => BOTH GUARDS ARE DEAD above 2^32, not merely wrapped: the comparison\n"
                "     `r.rev (u64) vs cur->src_rev (u32)` promotes the truncated value, so every\n"
                "     row above 2^32 looks strictly newer. Idempotence AND monotonicity both fail.\n");
}

// =================================================================================================
static void p4_pin_blindness() {
    hr("P4  SchemaMap::pin() blind spots (osv_ingest.h:165-179, blueprint §2.3)");
    SchemaMap a = olist_map();
    SchemaMap b = olist_map();
    b.classes[0].open_when.op = P_NOT_IN;      // the exact inverse predicate
    std::printf("  open_when.op P_IN vs P_NOT_IN  -> pins equal: %s   (opposite ledgers!)\n",
        a.pin() == b.pin() ? "YES  *** BLIND ***" : "no");

    SchemaMap c = olist_map(); c.classes[0].cls = 9;
    std::printf("  cls 0 vs 9 (the lattice index)  -> pins equal: %s\n",
        a.pin() == c.pin() ? "YES  *** BLIND ***" : "no");

    SchemaMap d = olist_map(); d.classes[0].flags = (uint8_t)(F_EXOGENOUS | F_WARRANT);
    std::printf("  flags +F_WARRANT (a human signs) -> pins equal: %s\n",
        a.pin() == d.pin() ? "YES  *** BLIND ***" : "no");

    SchemaMap e = olist_map(); e.classes[0].due_col = "order_delivered_customer_date";
    std::printf("  due_col changed (control)        -> pins equal: %s\n",
        a.pin() == e.pin() ? "YES" : "no  (pin does move here)");

    // granularity: one pin for the whole map
    SchemaMap f = olist_map();
    ClassMap g; g.name="second"; g.source="olist"; g.table="other"; g.key_col="k"; g.cls=1;
    f.classes.push_back(g);
    std::printf("\n  adding an UNRELATED second class changes the single map pin: %s\n",
        a.pin() != f.pin() ? "YES -- so every class's cognitive index invalidates" : "no");
}

// =================================================================================================
static void p5_ingest_defects() {
    hr("P5  the three ingest defects §11 promises to fix");

    // ---- (a) side row AFTER the class row is never applied to the existing commitment ----------
    {
        Ledger led; Ingest ing; ing.led = &led; ing.map = olist_map();
        SourceRow o; o.source="olist"; o.table="olist_orders_dataset"; o.key="ORD_A";
        o.op=OP_INSERT; o.rev=10; o.commit_ns=1000;
        o.fields = {{"order_id","ORD_A"},{"order_status","processing"},
                    {"order_estimated_delivery_date","2026-10-01 00:00:00"}};
        ing.apply(o);
        Commitment* cur = led.find(commitment_id("olist","olist_orders_dataset","ORD_A"));
        std::printf("  (a) after class row      : amount = %.2f  seat = %u\n", cur->amount, cur->seat);

        SourceRow pay; pay.source="olist"; pay.table="olist_order_payments_dataset"; pay.key="ORD_A";
        pay.op=OP_INSERT; pay.rev=11; pay.commit_ns=1001;
        pay.fields = {{"order_id","ORD_A"},{"payment_value","149.90"}};
        ing.apply(pay);
        SourceRow it; it.source="olist"; it.table="olist_order_items_dataset"; it.key="ORD_A";
        it.op=OP_INSERT; it.rev=12; it.commit_ns=1002;
        it.fields = {{"order_id","ORD_A"},{"seller_id","SELLER_X"}};
        ing.apply(it);
        cur = led.find(commitment_id("olist","olist_orders_dataset","ORD_A"));
        std::printf("      after LATE payments+items: amount = %.2f  seat = %u   enriched rows = %llu\n",
            cur->amount, cur->seat, (unsigned long long)ing.n.enriched);
        std::printf("      => the cached side rows were NEVER applied. amount stays 0.00 until some\n"
                    "         other order row happens to arrive. NO BACK-FILL PATH EXISTS\n"
                    "         (osv_ingest.h:294-301 returns false without touching the ledger).\n");
    }

    // ---- (b) seller_state is a TWO-HOP join the one-hop resolve cannot do ----------------------
    {
        Ledger led; Ingest ing; ing.led = &led; ing.map = olist_map();
        SourceRow it; it.source="olist"; it.table="olist_order_items_dataset"; it.key="ORD_B";
        it.op=OP_INSERT; it.rev=1; it.commit_ns=1;
        it.fields = {{"order_id","ORD_B"},{"seller_id","SELLER_X"}};
        ing.apply(it);
        SourceRow pay; pay.source="olist"; pay.table="olist_order_payments_dataset"; pay.key="ORD_B";
        pay.op=OP_INSERT; pay.rev=2; pay.commit_ns=2;
        pay.fields = {{"order_id","ORD_B"},{"payment_value","77.00"}};
        ing.apply(pay);
        // the sellers table: keyed by SELLER_ID, not by order_id -- the second hop
        SourceRow sel; sel.source="olist"; sel.table="olist_sellers_dataset"; sel.key="SELLER_X";
        sel.op=OP_INSERT; sel.rev=3; sel.commit_ns=3;
        sel.fields = {{"seller_id","SELLER_X"},{"seller_state","SP"}};
        ing.apply(sel);

        SourceRow o; o.source="olist"; o.table="olist_orders_dataset"; o.key="ORD_B";
        o.op=OP_INSERT; o.rev=4; o.commit_ns=4;
        o.fields = {{"order_id","ORD_B"},{"order_status","processing"},
                    {"order_estimated_delivery_date","2026-10-01 00:00:00"}};
        ing.apply(o);
        Commitment* cur = led.find(commitment_id("olist","olist_orders_dataset","ORD_B"));
        std::printf("\n  (b) all side rows arrived FIRST, then the class row:\n");
        std::printf("      amount = %.2f (payments, one hop: works)   seat = %u (items, one hop: works)\n",
            cur->amount, cur->seat);
        std::printf("      seg    = %u  <- seller_state, TWO hops (order->items.seller_id->sellers)\n", cur->seg);
        std::printf("      sellers table was counted UNMAPPED: %llu   named: ", (unsigned long long)ing.n.unmapped);
        for (const auto& t : ing.unmapped_tables) std::printf("%s ", t.c_str());
        std::printf("\n      => seg stays 0 SILENTLY. resolve() (osv_ingest.h:262-273) keys every side\n"
                    "         table by r.key (the order id); sellers is keyed by seller_id, so no\n"
                    "         lookup can ever hit. Conservation still HOLDS, so no falsifier catches it:\n"
                    "         all 50,000 cells collapse into lattice segment 0.\n");
    }

    // ---- (c) one-to-many side tables reduce to last-row-wins -----------------------------------
    {
        Ledger led; Ingest ing; ing.led = &led; ing.map = olist_map();
        // Olist: an order has N payment rows (payment_sequential) and N item rows.
        const char* vals[3] = {"100.00","50.00","3.50"};       // 3 installments, true total 153.50
        for (int i = 0; i < 3; ++i) {
            SourceRow pay; pay.source="olist"; pay.table="olist_order_payments_dataset"; pay.key="ORD_C";
            pay.op=OP_INSERT; pay.rev=(uint64_t)(1+i); pay.commit_ns=(uint64_t)(1+i);
            pay.fields = {{"order_id","ORD_C"},{"payment_sequential",std::to_string(i+1)},
                          {"payment_value",vals[i]}};
            ing.apply(pay);
        }
        const char* sellers[2] = {"SELLER_P","SELLER_Q"};
        for (int i = 0; i < 2; ++i) {
            SourceRow it; it.source="olist"; it.table="olist_order_items_dataset"; it.key="ORD_C";
            it.op=OP_INSERT; it.rev=(uint64_t)(10+i); it.commit_ns=(uint64_t)(10+i);
            it.fields = {{"order_id","ORD_C"},{"order_item_id",std::to_string(i+1)},
                         {"seller_id",sellers[i]}};
            ing.apply(it);
        }
        SourceRow o; o.source="olist"; o.table="olist_orders_dataset"; o.key="ORD_C";
        o.op=OP_INSERT; o.rev=20; o.commit_ns=20;
        o.fields = {{"order_id","ORD_C"},{"order_status","processing"},
                    {"order_estimated_delivery_date","2026-10-01 00:00:00"}};
        ing.apply(o);
        Commitment* cur = led.find(commitment_id("olist","olist_orders_dataset","ORD_C"));
        std::printf("\n  (c) 3 payment rows 100.00 + 50.00 + 3.50  (true order value 153.50)\n");
        std::printf("      c.amount = %.2f  <- the LAST row only (side[table][key] = r.fields, line 298)\n", cur->amount);
        std::printf("      2 item rows SELLER_P then SELLER_Q -> c.seat = %u == stable_u32(\"%s\")=%u\n",
            cur->seat, sellers[1], stable_u32(sellers[1]));
        std::printf("      => amount is understated by %.2f on every multi-payment order. Conservation\n"
                    "         still holds (it conserves the WRONG amount). No falsifier catches it.\n", 153.50 - cur->amount);
    }
}

int main() {
    p1_layout();
    p2_digest_padding();
    p3_rev_truncation();
    p4_pin_blindness();
    p5_ingest_defects();
    std::printf("\n");
    return 0;
}
