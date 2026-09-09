// QC-7: does the blueprint's 2.2 Cell struct actually hold at 64 bytes, and does the
// 32-bit fix in osv_ingest.h survive a >2^32 revision? Two independent checks.
#include <cstdint>
#include <cstddef>
#include <cstdio>

struct Cell {                 // verbatim from TAPESTRY blueprint 2.2
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
static_assert(sizeof(Cell) == 64, "blueprint Cell must stay one cache line");

// The osv_ingest.h monotonicity guard, reproduced exactly (osv_ingest.h:284,317-318,337).
struct Commit32 { uint32_t src_rev; };
struct Commit64 { uint64_t pos_last; };

int main() {
    std::printf("sizeof(Cell)            = %zu  (blueprint 2.2 claims 64)\n", sizeof(Cell));
    std::printf("alignof(Cell)           = %zu\n", alignof(Cell));
    std::printf("offsetof(pos_last)      = %zu\n", offsetof(Cell, pos_last));
    std::printf("offsetof(gear)          = %zu (last byte at %zu)\n", offsetof(Cell, gear), offsetof(Cell, gear));

    // The truncation the blueprint 2.2 says pos_last closes.
    const uint64_t wal = 4294967296ull + 12345ull;      // a Postgres LSN just past 2^32
    Commit32 a; a.src_rev = (uint32_t)wal;              // osv_ingest.h:284 verbatim
    Commit64 b; b.pos_last = wal;
    std::printf("\nwire revision           = %llu\n", (unsigned long long)wal);
    std::printf("stored as uint32 src_rev= %llu   <- osv_ingest.h:284 `c.src_rev = (uint32_t)r.rev`\n",
                (unsigned long long)a.src_rev);
    std::printf("stored as uint64 pos_last= %llu\n", (unsigned long long)b.pos_last);

    // The monotonicity comparison at osv_ingest.h:317-318, with the truncated value held.
    // r.rev is uint64, cur->src_rev is uint32 -> the uint32 is promoted, not the uint64 truncated,
    // so the guard compares 4294979641 against 12345 and calls a REPLAY a fresh row.
    const uint64_t r_rev = wal;
    const uint32_t cur_src_rev = a.src_rev;
    std::printf("\nreplay of the SAME row: r.rev==cur->src_rev ? %s  (idempotence, osv_ingest.h:317)\n",
                (r_rev == (uint64_t)cur_src_rev) ? "yes - duplicate" : "NO - the replay is applied again");
    std::printf("stale-row guard:        r.rev <  cur->src_rev ? %s  (monotonicity, osv_ingest.h:318)\n",
                (r_rev < (uint64_t)cur_src_rev) ? "yes - refused" : "NO - a stale row is NOT refused");
    return 0;
}
