// =====================================================================================================
// TAPESTRY · src/tools/olist_ingest.cpp · a real wire, ingested through the declared join path
//
//   olist_ingest --data C:/path/to/olist --dir tape [--limit N] [--reducer sum|last] [--dump out.csv]
//
// Olist is ~100k real Brazilian e-commerce orders, 2016-2018. It is the right wire for R1's fourth
// gate — "the ledger agrees with the world on a sample", falsifier 19 — for a specific reason: an
// order has MANY line items, so the order's amount exists only if somebody declared `sum`. The
// falsifier's planted lie is "last-row-wins on a summed column", and `--reducer last` plants it.
//
// (Olist reads about zero DECISION HEADROOM — that was measured on 2026-09-08 and is why it was
// called a poor test for the org-solver thesis. That verdict does not transfer here: falsifier 19 is
// a conformance check, not a headroom measurement. It asks whether the store reproduces the source
// tables, and for that a messy real wire with joins, revisions and multi-row groups is exactly right.)
//
// MONEY IS PARSED AS AN INTEGER. "58.90" becomes 5890 minor units by reading digits, never by
// `atof` and a multiply — a float would put rounding error into the one column a cap fold sums.
// =====================================================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unordered_map>
#include "../core/fileio.h"
#include "../tape/tape.h"
#include "../writ/classmap.h"
#include "../writ/enrich.h"
#include "../tx/transactor.h"

using namespace tapestry;

// ---- CSV ---------------------------------------------------------------------------------------
// RFC 4180 enough for this wire: quoted fields, doubled quotes inside them, CRLF or LF.
static bool csv_split(const std::string& line, std::vector<std::string>* out) {
    out->clear();
    std::string cur;
    bool in_q = false;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (in_q) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; }
                else in_q = false;
            } else cur += c;
        } else {
            if (c == '"') in_q = true;
            else if (c == ',') { out->push_back(cur); cur.clear(); }
            else if (c == '\r') { }
            else cur += c;
        }
    }
    out->push_back(cur);
    return !in_q;
}

struct Csv {
    FILE* f = nullptr;
    std::vector<std::string> header;
    std::string line;
    bool open(const std::string& path) {
        f = std::fopen(path.c_str(), "rb");
        if (!f) return false;
        std::string h;
        if (!next_line(&h)) return false;
        return csv_split(h, &header);
    }
    bool next_line(std::string* out) {
        out->clear();
        int c;
        bool any = false;
        while ((c = std::fgetc(f)) != EOF) {
            any = true;
            if (c == '\n') break;
            out->push_back((char)c);
        }
        return any;
    }
    int col(const std::string& name) const {
        for (size_t i = 0; i < header.size(); ++i) if (header[i] == name) return (int)i;
        return -1;
    }
    void close() { if (f) std::fclose(f); f = nullptr; }
};

// ---- money, exactly ------------------------------------------------------------------------------
// "58.90" -> 5890. Digits only; more than two decimal places is refused rather than rounded, because
// a store that rounds silently at the boundary has lost the argument about being the truth.
static bool parse_minor(const std::string& s, int64_t* out) {
    if (s.empty()) { *out = 0; return true; }
    size_t i = 0;
    bool neg = false;
    if (s[i] == '-') { neg = true; ++i; }
    int64_t whole = 0;
    bool any = false;
    for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
        if (whole > (INT64_MAX - 9) / 10) return false;
        whole = whole * 10 + (s[i] - '0'); any = true;
    }
    int64_t frac = 0; int fd = 0;
    if (i < s.size() && s[i] == '.') {
        ++i;
        for (; i < s.size() && s[i] >= '0' && s[i] <= '9'; ++i) {
            if (fd >= 2) return false;                       // three decimals is not cents
            frac = frac * 10 + (s[i] - '0'); ++fd; any = true;
        }
    }
    if (i != s.size() || !any) return false;
    while (fd < 2) { frac *= 10; ++fd; }
    if (whole > (INT64_MAX - frac) / 100) return false;
    *out = whole * 100 + frac;
    if (neg) *out = -*out;
    return true;
}

// ---- time --------------------------------------------------------------------------------------
// "2017-10-18 00:00:00" -> nanoseconds since the Unix epoch, UTC. Hinnant's days_from_civil, so there
// is no timezone database and no locale in the path that decides a deadline.
static int64_t days_from_civil(int64_t y, unsigned m, unsigned d) {
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static bool parse_ts_ns(const std::string& s, uint64_t* out) {
    if (s.size() < 19) { *out = 0; return s.empty(); }        // an empty timestamp is "none", not an error
    int y, mo, d, h, mi, sec;
    if (std::sscanf(s.c_str(), "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec) != 6) return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || sec > 60) return false;
    const int64_t days = days_from_civil(y, (unsigned)mo, (unsigned)d);
    const int64_t secs = days * 86400 + h * 3600 + mi * 60 + sec;
    if (secs < 0) return false;
    *out = (uint64_t)secs * 1000000000ull;
    return true;
}

static uint8_t state_of(const std::string& status) {
    if (status == "delivered")   return C_CLOSED;
    if (status == "canceled")    return C_CLOSED;
    if (status == "unavailable") return C_CLOSED;
    if (status == "shipped")     return C_DISPATCHED;
    if (status == "processing")  return C_DISPATCHED;
    if (status == "approved")    return C_OPEN;
    if (status == "invoiced")    return C_OPEN;
    return C_OPEN;                                            // created
}

int main(int argc, char** argv) {
    std::string data, dir, dump;
    uint64_t limit = 0;
    std::string reducer_name_arg = "sum";
    for (int i = 1; i < argc; ++i) {
        const std::string k = argv[i];
        if      (k == "--data"    && i + 1 < argc) data = argv[++i];
        else if (k == "--dir"     && i + 1 < argc) dir = argv[++i];
        else if (k == "--dump"    && i + 1 < argc) dump = argv[++i];
        else if (k == "--limit"   && i + 1 < argc) limit = std::strtoull(argv[++i], nullptr, 10);
        else if (k == "--reducer" && i + 1 < argc) reducer_name_arg = argv[++i];
        else { std::fprintf(stderr, "unknown argument: %s\n", k.c_str()); return 2; }
    }
    if (data.empty() || dir.empty()) {
        std::fprintf(stderr, "usage: olist_ingest --data DIR --dir TAPEDIR [--limit N] "
                             "[--reducer sum|last] [--dump out.csv]\n");
        return 2;
    }

    // ---- the class map's ingest declaration, validated before a byte is read -------------------
    writ::TableSchema schema;
    schema.add("orders", {"order_id", "customer_id", "order_status", "order_purchase_timestamp",
                          "order_approved_at", "order_delivered_carrier_date",
                          "order_delivered_customer_date", "order_estimated_delivery_date"});
    schema.add("order_items", {"order_id", "order_item_id", "product_id", "seller_id",
                               "shipping_limit_date", "price", "freight_value"});

    writ::Reducer red;
    if (!writ::reducer_from_name(reducer_name_arg, &red)) {
        std::fprintf(stderr, "unknown reducer: %s\n", reducer_name_arg.c_str()); return 2;
    }

    writ::IngestDef ing;
    ing.class_table = "orders";
    ing.key_col = "order_id";
    ing.columns.push_back({"opened_ns", "orders.order_purchase_timestamp", writ::Reducer::First, false});
    ing.columns.push_back({"due_ns",    "orders.order_estimated_delivery_date", writ::Reducer::First, false});
    ing.columns.push_back({"state",     "orders.order_status", writ::Reducer::First, false});
    ing.columns.push_back({"amount_minor", "order_items.price", red, true});
    ing.columns.push_back({"freight_minor", "order_items.freight_value", red, true});
    writ::Enrich j;
    j.table = "order_items"; j.key_col = "order_id"; j.via_table = "orders"; j.via_col = "order_id";
    ing.joins.push_back(j);

    std::string why;
    if (!ing.validate(schema, &why)) { std::fprintf(stderr, "class map refused: %s\n", why.c_str()); return 3; }
    std::printf("ingest_pin=%s reducer=%s\n", ing.pin().c_str(), writ::reducer_name(red));

    // ---- pass 1: the join, reduced ----------------------------------------------------------------
    Csv items;
    if (!items.open(path_join(data, "olist_order_items_dataset.csv"))) {
        std::fprintf(stderr, "cannot open order_items\n"); return 3;
    }
    const int i_order = items.col("order_id"), i_price = items.col("price"), i_freight = items.col("freight_value");
    if (i_order < 0 || i_price < 0 || i_freight < 0) { std::fprintf(stderr, "order_items columns missing\n"); return 3; }

    std::unordered_map<std::string, writ::Acc> price_acc, freight_acc;
    uint64_t item_rows = 0, item_bad = 0;
    std::string line; std::vector<std::string> f;
    while (items.next_line(&line)) {
        if (line.empty()) continue;
        if (!csv_split(line, &f)) { ++item_bad; continue; }
        if ((int)f.size() <= i_freight) { ++item_bad; continue; }
        int64_t p = 0, fr = 0;
        if (!parse_minor(f[(size_t)i_price], &p) || !parse_minor(f[(size_t)i_freight], &fr)) { ++item_bad; continue; }
        const std::string& oid = f[(size_t)i_order];
        writ::Acc& pa = price_acc[oid];
        if (pa.n == 0) pa.reset(red);
        writ::Acc& fa = freight_acc[oid];
        if (fa.n == 0) fa.reset(red);
        if (!pa.add(p, f[(size_t)i_price]) || !fa.add(fr, f[(size_t)i_freight])) { ++item_bad; continue; }
        ++item_rows;
    }
    items.close();
    std::printf("order_items: %llu rows over %llu orders (%llu unparsed)\n",
                (unsigned long long)item_rows, (unsigned long long)price_acc.size(),
                (unsigned long long)item_bad);

    // ---- pass 2: the grain, onto the tape ----------------------------------------------------------
    SystemClock clk;
    Tape tape; TapeConfig tc; tc.dir = dir; tc.segment_bytes = 64ull << 20;
    std::string err;
    if (!tape.open(tc, &clk, &err)) { std::fprintf(stderr, "tape: %s\n", err.c_str()); return 3; }

    writ::ClassMap map;
    writ::ClassDef cd;
    cd.cls = 1; cd.name = "orders"; cd.cap_minor_per_period = -1;
    writ::RevRule rr; rr.table = "*"; rr.op = "*"; rr.rev = writ::Rev::ReversibleByInverse;
    cd.rev.push_back(rr);
    if (!map.add(cd, {}, &why)) { std::fprintf(stderr, "classmap: %s\n", why.c_str()); return 3; }

    Transactor tx;
    TxConfig cfg; cfg.seam_code_hash = "abc123"; cfg.max_facts_per_write = 1;
    if (!tx.open(&tape, &map, cfg, dir, &err)) { std::fprintf(stderr, "transactor: %s\n", err.c_str()); return 3; }

    Csv orders;
    if (!orders.open(path_join(data, "olist_orders_dataset.csv"))) {
        std::fprintf(stderr, "cannot open orders\n"); return 3;
    }
    const int o_id = orders.col("order_id"), o_status = orders.col("order_status");
    const int o_purchase = orders.col("order_purchase_timestamp");
    const int o_est = orders.col("order_estimated_delivery_date");
    if (o_id < 0 || o_status < 0 || o_purchase < 0 || o_est < 0) { std::fprintf(stderr, "orders columns missing\n"); return 3; }

    FILE* dumpf = nullptr;
    if (!dump.empty()) {
        dumpf = std::fopen(dump.c_str(), "wb");
        if (dumpf) std::fprintf(dumpf, "order_id,amount_minor,freight_minor,due_ns,state,cell_id\n");
    }

    uint64_t written = 0, refused = 0, no_items = 0, bad_ts = 0;
    uint64_t src_pos = 1;
    while (orders.next_line(&line)) {
        if (line.empty()) continue;
        if (!csv_split(line, &f)) continue;
        if ((int)f.size() <= o_est) continue;
        const std::string& oid = f[(size_t)o_id];
        uint64_t opened = 0, due = 0;
        if (!parse_ts_ns(f[(size_t)o_purchase], &opened) || !parse_ts_ns(f[(size_t)o_est], &due)) { ++bad_ts; continue; }

        auto pit = price_acc.find(oid);
        auto fit = freight_acc.find(oid);
        const int64_t amount = (pit == price_acc.end()) ? 0 : pit->second.value();
        const int64_t freight = (fit == freight_acc.end()) ? 0 : fit->second.value();
        if (pit == price_acc.end()) ++no_items;

        Fact fact;
        fact.source = "olist"; fact.table = "orders"; fact.key = oid; fact.op = "update";
        fact.source_pos = src_pos++;
        fact.cls = 1;
        fact.amount_minor = amount + freight;
        fact.due_ns = due;
        fact.state = state_of(f[(size_t)o_status]);
        fact.exogenous = true;              // the world authored this row; an inverse is forbidden
        fact.has_inverse = false;

        WriteReq w;
        w.client_id = "olist"; w.request_id = "o" + std::to_string(src_pos);
        w.by = by::seat("ingest");
        w.facts.push_back(fact);
        const WriteRes r = tx.write(w);
        if (r.committed) {
            ++written;
            if (dumpf) std::fprintf(dumpf, "%s,%lld,%lld,%llu,%u,%llu\n", oid.c_str(),
                                    (long long)amount, (long long)freight, (unsigned long long)due,
                                    (unsigned)fact.state,
                                    (unsigned long long)cell_id_of("olist", "orders", oid));
        } else ++refused;
        if (limit && written >= limit) break;
    }
    orders.close();
    if (dumpf) std::fclose(dumpf);

    std::string e2;
    tx.flush_refusals(&e2);
    const Health& h = tx.health();
    std::printf("orders: written=%llu refused=%llu no_items=%llu bad_timestamp=%llu\n",
                (unsigned long long)written, (unsigned long long)refused,
                (unsigned long long)no_items, (unsigned long long)bad_ts);
    std::printf("cells=%llu committed_pos=%llu head=%s\n",
                (unsigned long long)h.cells, (unsigned long long)tape.committed_pos(), tape.head().c_str());
    tape.close();
    return 0;
}
