// QC-7: TAPESTRY 2.3 says "The map's pin (SchemaMap::pin) is extended to cover horizons and
// templates." Before extending it, does the pin cover what it already claims to? The pin's own
// comment (osv_ingest.h:163-164) says: "train == serve: if the map drifts, the ledger it produced
// is a different object". This asks whether three specific drifts move the pin.
#include "osv_ingest.h"
#include <cstdio>
using namespace osv;

static SchemaMap base() { return olist_map(); }

int main() {
    const uint64_t p0 = base().pin();
    std::printf("baseline olist_map().pin()          = 0x%016llx\n\n", (unsigned long long)p0);

    // (1) Flip the predicate OPERATOR. open_when goes from "status IN {...}" to
    //     "status NOT IN {...}" — the obligation now exists exactly when it did not.
    SchemaMap a = base();
    a.classes[0].open_when.op = P_NOT_IN;
    std::printf("open_when.op  P_IN -> P_NOT_IN       = 0x%016llx  %s\n",
                (unsigned long long)a.pin(), a.pin() == p0 ? "*** PIN UNCHANGED ***" : "pin moved");

    // (2) Flip the DECLARED FLAGS. F_WARRANT is what makes a write irreversible and forces the
    //     escalate path (osv_core.cuh:255, osv_dispatch.h:222). TAPESTRY 7 hangs quorum off it.
    SchemaMap b = base();
    b.classes[0].flags = F_WARRANT;
    std::printf("flags F_EXOGENOUS -> F_WARRANT       = 0x%016llx  %s\n",
                (unsigned long long)b.pin(), b.pin() == p0 ? "*** PIN UNCHANGED ***" : "pin moved");

    // (3) Change the numeric decision class the lattice indexes by.
    SchemaMap c = base();
    c.classes[0].cls = 7;
    std::printf("cls 0 -> 7                           = 0x%016llx  %s\n",
                (unsigned long long)c.pin(), c.pin() == p0 ? "*** PIN UNCHANGED ***" : "pin moved");

    // Control: a drift the pin DOES cover, so the test is not vacuous.
    SchemaMap d = base();
    d.classes[0].due_col = "order_delivered_customer_date";
    std::printf("due_col changed (control)            = 0x%016llx  %s\n",
                (unsigned long long)d.pin(), d.pin() == p0 ? "*** PIN UNCHANGED ***" : "pin moved");

    const bool leaks = (a.pin() == p0) || (b.pin() == p0) || (c.pin() == p0);
    const bool control_ok = d.pin() != p0;
    std::printf("\nverdict: %s\n", (leaks && control_ok)
        ? "SchemaMap::pin is BLIND to predicate operator, declared flags and class id"
        : "pin covers all four drifts");
    return 0;
}
