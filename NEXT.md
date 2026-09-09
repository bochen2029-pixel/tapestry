# NEXT · where this repo stands and what to do with it

**2026-09-09 · Claude Opus 5.** Written to be the first file a new session reads. Where this file and a dated receipt disagree, the receipt wins.

---

## §1 · What is built

Seven commits, **370 checks, 0 failures**, every oracle carrying a lie arm. `build\build.cmd test` runs 363 of them; `build\build_gpu.cmd test` runs the other 7 and needs an sm_89 card.

| rung | state | receipt |
|---|---|---|
| **R0.1** the tape | done | `receipts/R0.1_TAPE-STORE_…` |
| **R0.2** the transactor and the writ | done | `receipts/R0.2_THE-TRANSACTOR-AND-THE-WRIT_…` |
| **R0.3** the wire, the injector, the durability gate | done — **rung R0 closed** | `receipts/R0.3_THE-SOCKET-THE-INJECTOR-…` |
| **R1.1** the field fold | done | `receipts/R1.1_THE-FIELD-FOLD_…` |
| **R1.2** enrichment, and the ledger against the world | done | `receipts/R1.2_ENRICHMENT-AND-THE-OLIST-WIRE_…` |
| **R1.3** the grades fold | done — **R1's folds built** | `receipts/R1.3_THE-GRADES-FOLD_…` |
| **R2.1** host and device bit-for-bit | done — **R2's determinism gate met** | `receipts/R2.1_HOST-AND-DEVICE-BIT-FOR-BIT_…` |

About 6,500 lines under `src\`: `core/` (hash, chain, lossless writer, durable files), `tape/` (wire form, segments, group commit, recovery, verifier), `writ/` (the expression language, class pins, the ingest declaration), `tx/` (the 128-byte cell, the cell table, the one writer), `fold/` (the field's leaves and host class, the grades fold), `net/` (frames and the one request grammar), `tools/`, `tests/`.

---

## §2 · The two facts that decide what comes next

**1 · Nothing has run end to end.** The folds exist as libraries that the oracles call. **The peer does not exist.** §4.3 describes a socketless process that reads committed deltas over a named pipe, verifies `prev == last_h` on every delta, refuses `pos ≤ applied_pos`, fatals on a gap, applies in batches at commit boundaries, runs the sweep, and publishes `applied_pos` atomically. None of that is written. Today the transactor holds a cell table and cap accumulators in its own process, and the field and grades folds are called only by tests.

This is why **falsifier 11 — "two folds, one answer", the transactor's fold and the peer's fold agreeing at every commit boundary — cannot be run at all.** It is the only falsifier in the blueprint that is currently untestable for want of a component rather than for want of a judge.

**2 · R3 is blocked, and blocked deliberately.** §11's open question 1 and `qc/NEXT_FROM-FACTOR_2026-09-08.md` both say the same thing: the judge-architecture decision — the recurrent hybrid's 50.25 MiB per-sequence slot and 256-slot cap against a pure-attention judge with paged prefix caching — must be **measured on this card before R3**, because every sizing constant in §6 is borrowed from the hybrid and voids if the judge changes. Starting R3's slot pool before that measurement means building the pool around a constraint that may not exist.

---

## §3 · The recommendation, in order

### First: **R2.2 — the peer process.** Unblocked, structural, and cheap.

It needs no judge, no card time beyond what R2.1 already proved, and no new data. It is the piece that turns five libraries into a system, and it makes falsifier 11 runnable for the first time. Concretely:

- the named-pipe or shared-memory ring the transactor writes committed deltas into;
- the peer's apply loop with the chain check, the position guard (already in `Transactor::apply_committed`, and the same rule applies here), the gap fatal, and the rebuild-from-snapshot-on-mismatch path;
- `applied_pos` published atomically, readers seeing only published states;
- fusord's module gate kept verbatim, so the peer holds no socket — the one place §12's reuse is a hard requirement rather than a convenience;
- **falsifier 11 as its gate**: run both folds over the same tape and compare digests at every commit boundary. `CellTable::digest()` and `Field::projection_digest()` already exist for exactly this.

### Second: **the Chicago 311 wire — which closes an open question rather than merely conforming.**

See §4. The reason to do it second rather than first is that it wants somewhere to land: with the peer built, ingesting it exercises the whole spine at once.

### Third: **R3's blocking measurement**, when there is appetite for the GPU work. Do not start R3's slot pool before it.

### What NOT to do next

Do not build the seam (R4) before the peer: the seam lives *in* the peer, and writing it into the transactor to make progress would put the verb author on the wrong side of the line the fourth law draws. Do not extend the client API past its four calls until something needs a fifth — `subscribe` in particular should be written when the peer needs it, so it is shaped by a real consumer.

---

## §4 · The dataset question, answered with numbers

Checked live on 2026-09-09 through `C:\kernel.sh` (a real browser, local backend) against the Socrata APIs.

| candidate | rows | verdict |
|---|---|---|
| **Chicago 311 Service Requests** (`v6vf-nfxy`) | **14,613,716**, updated daily — last update the day it was checked | **recommended** |
| NYC 311 (`erm2-nwe9`) | 22,407,550 | **rejected**: the `due_date` column exists and is **0.4% populated** — 1,315 of 306,442 in June 2025 |
| Chicago Food Inspections (`4ijn-s7e5`) | 315,223 | a good small second wire: `inspection_type` carries re-inspections and `results` carries pass/fail, so a failed inspection followed by a re-inspection is a literal `reverses_pos` |
| Olist | 99,441 orders | already used, and correct for what it was used for (falsifier 19). About zero decision headroom — measured 2026-09-08, `C--ABC` session |
| Home Credit | 307,511 applications | +0.066 AUC over self-report but only **+0.005** beyond the score a human already sees. Selective labels: outcomes exist only for approved loans |

**Why Chicago 311 and not something with a deadline column.** Nothing published carries a usable per-request deadline — NYC's is empty and Chicago has no such column, and the 311 request-types dataset (`dgc7-2pdf`) carries only type, code and count, no SLA. **That does not matter, and chasing it was the wrong instinct.** §3.3 makes horizons a property of the **class**, not the cell, and §11's open question 11 says outright that horizon values are *"measurable from history for act and work before v0"*. So the right move is to measure the per-type closure-time distribution from the 14.6 M closed records and take a percentile as `h_act` — which **closes an open question** instead of merely conforming to a wire.

**What Chicago 311 has that Olist and Home Credit both lack** — measured on June 2025, 177,333 requests:

- **8,703 duplicates (4.9%), every one carrying `parent_sr_number`.** Explicit, city-labelled linkage from a duplicate to the original. That is simultaneously the ingest law's duplicate case, a real `blocked_by` dependency edge between cells, and the "the record knew a near-identical ticket was already solved" signal the `C--ABC` session went looking for and could not find in a public set.
- **173,996 closed (98.1%)**, so outcomes actually arrive and the grades fold's ungradable fraction will be small — unlike a wire where most decisions are never resolved and no band can ever be licensed.
- `sr_type` gives 100+ real classes; `created_department` / `owner_department` give seats; `status` gives a lifecycle; `last_modified_date` gives the source revision clock the ingest law needs; `legacy_record` / `legacy_sr_number` give genuine two-identity-system messiness.

**Practical note:** do not pull 14.6 M rows through the browser. `C:\fetcher` is the resilient downloader on this box, and the Socrata CSV export endpoint takes `$where` — one year or one department is the right first slice. Ingest is currently **485 writes/second** (see §5), so a million rows is about half an hour; slice accordingly or fix the batching first.

**Licence:** the portal says "See Terms of Use" rather than naming a standard licence. Read it before redistributing anything derived; using it locally to test a store is not redistribution.

---

## §5 · Known limitations, named rather than discovered later

- **Bulk ingest pays one flush per row.** `Transactor::write` commits its own batch, so a load runs at **485 writes/second** against the **7,250/second** the same storage layer reaches at batch 16. §4.8's answer is pipelined requests with client request ids; R0.3's server serves one request at a time and cannot pipeline. Fix this before ingesting anything large.
- **The reply cache is unbounded.** §4.8's open question 7 (client timeout and retry policy) has to be answered before it can be trimmed, because a cache that forgets a request before the client stops retrying re-admits the write.
- **The transactor is one thread**, not §4.1's three.
- **Eight of §4.8's twelve calls are absent** — deliberately absent, not stubbed.
- **R2 still owes** the embedding store and exhaustive scan, and a sweep throughput number: R2.1's harness uses a 1,536-cell lattice, the right size to test arithmetic and the wrong size to time a card.
- **No verb on any tape has been graded.** The grades fold is tested against planted inputs because there is no seam until R4. That is correct — a fold is defined by its inputs — but it means the fold has never seen a real decision.

---

## §6 · Deltas against the blueprint, accumulated

Each is argued in the receipt that made it. Collected here so a next QC has one list: the `seg` and `warn` kinds and the ghost rule (R0.1); the `$b64` tag and the v0 float policy (R0.1); the `Cell` struct's arithmetic — **§3.2's own `static_assert(sizeof(Cell) == 128)` does not compile, the struct is 120 bytes** — and `amount_minor`, `not_authored_by_seam`, and the cap's contribution rule (R0.2); the idempotency key on the tape, the applied-position guard, and `torn_tail_bytes` (R0.3); dyadic-rational coefficients and the baseline's shift (R1.1); integer money at the boundary and the ingest pin (R1.2); `Horizons::all_finite` treating zero as no horizon and `VerbTally` exposing no total (R1.3); and the big one — **§5's tier table should move the sweep from *bounded* to *exact and portable*** (R2.1).

The declined dependency is also a delta: §9's R0 row specifies DuckDB for the constraint language and the QC synthesis §3.14 ratifies it. It is not used. `src/writ/expr.h` explains why in its header, and the operator's decision is recorded there.

---

*Written 2026-09-09 by Claude Opus 5. Nothing in this file is a measurement except where it names one and says where it was made.*
