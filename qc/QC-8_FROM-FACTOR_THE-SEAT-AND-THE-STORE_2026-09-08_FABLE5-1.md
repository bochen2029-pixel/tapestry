# QC-8 · FROM FACTOR · THE SEAT AND THE STORE
### What TAPESTRY is, where the WarpBus went, whether FACTOR should use it, and what the two should share

**2026-09-08 · the C:\55555 session, the fork that wrote FACTOR.** Written after reading `TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1`, the headings of QC-6, the origin harness spec of 2026-08-11, §Ω.5 of the fusion-on-warpbus page, anomaly C-23, fusor1.com as it reads today, and FACTOR's own QC of the same day. Register: a review and a relation, not a measurement.

---

## §0 · The verdict in one breath

TAPESTRY is the right category and the right altitude: the database of the third system, the system of judgment beside the systems of record and engagement, which the forklift document named and said "the tape and the trunk are its database." It is also, precisely, the in-VRAM database the harness spec deferred on 2026-08-11 under the name WarpBus, reconstituted today as the cognitive index plus the peer. FACTOR is TAPESTRY at one seat, in one process. So FACTOR should use TAPESTRY neither as a backup nor as an alternative. It is a tributary now and the same object later, and the one decision that matters this week is to converge the formats so that a seat's tape and record become a store's without conversion on the day a second seat appears.

---

## §1 · Where the WarpBus went, from the receipts

Four receipts, in order.

1. **The origin, 2026-08-11.** The harness spec's §3 defines the WarpBus as four things: the KV trunk, "the materialized fold of the tape," rebuildable and never persisted; the reflex plane, an in-process ring of forming partials, never persisted; the rev tape, append-only and hash-chained; and the cross-abort. Its §9 defers "the custom WarpBus DB": in-memory, fixed-record, append-only, lock-free, multi-publisher to a single serializer, "its job is what SQLite can't: the KV trunk as its materialized view." And it says in so many words: "v1 does NOT need it; the in-RAM fabric plus durable log carry N=1..3 fine; it's the level-B, many-stream, many-node lever."
2. **The isometry, §Ω.5 of the fusion page.** "The WarpBus is not a store beside the model; the model's paged KV cache is its live plane. One token slot in the bus is one entry in a paged KV block. A branch is a KV branch: shared prefix pages by pointer, a copy-on-write suffix. Reading a peer is attending over shared pages. Aborting is dropping suffix pages. Committing is sealing pages immutable and hashing them onto the tape. The schema is the KV page table, extended with lane tags, a hash chain, and one moving fence." A prototype was sketched, F-PERF was set against an mmap'd write-ahead log, and SQLite was kept as the cold tier and the null arm.
3. **What was built.** `fusord.cpp` built the two planes for one trunk and three seats, exactly what §9 said v1 needed: forming frames held off the trunk, the seam that kills mid-word, the hash-chained tape, forks by sequence copy at zero bytes. The database half was never built, and anomaly C-23 records the operator discovering that mid-sentence, with "warpbus" having drifted into a synonym for the reflex plane, which is by definition never persisted.
4. **Today.** TAPESTRY's cognitive index, keyed `(cell, pos_last, template_pin, judge_pin)`, forked by handle with copy-on-write pages and evicted by the field's ranking, is "the KV trunk as its materialized view" made into a component. fusor1.com states the same thesis on its face: "every past token's key is already an embedding, resident in the attention cache; the vector database the industry bolts on beside the model is the model's own attention, unread."

Nothing was lost. The deferral held for the right reason, no second seat, and only the name drifted. The name drift is the one thing to fix, because the anomaly ledger is right that a word meaning both "never persisted" and "the database" is a defect.

---

## §2 · Should a WarpBus reflex-plane database be built?

Split the question, because it contains two objects that the name has been holding together.

**The reflex plane must never be a database.** That is the law on the drawing: forming, abortable, never persisted. What the reflex plane needs is not a store but a page allocator with lane tags and a fence, and the trunk's KV page table already is one. Three additions would make it §Ω.5's schema exactly, and none of them is a database:

- **Lane tags on KV cells, and a filtered sequence copy.** FACTOR's monotone-percept law requires a blind read: the check and guard seats re-read the obligation with every frame the counterparty authored removed. Today that is a re-prefill. With a lane tag per cell and a `seq_cp` that copies every cell except those tagged with a named lane, the blind read is a metadata operation over the page table, an attention mask over provenance. This is the first WarpBus deliverable, it is a modest llama.cpp patch, and it has a falsifier already, F-BLIND, plus a cost measurement that decides whether it is built: re-prefill milliseconds per blind read against the masked fork's.
- **A hash per sealed page.** Committing becomes sealing pages and hashing them onto the tape, so a `frame` row can reference the page it became, and the trunk becomes verifiable as a fold of the tape rather than merely rebuildable. Falsifier: rebuild the trunk from the tape, the page hashes match.
- **The seam as a page boundary.** The moving fence is where committed pages end and forming pages begin; un-say drops the pages past the fence.

**The in-VRAM store proper is a different object and it is TAPESTRY.** The cell table, the lattice and its sweep, the exhaustive scan, the cognitive index over many cells, the judges invoked on the card: that is justified by the forklift test at the organization radius, where the state must stay and one writer must serialize it, and it is what TAPESTRY's R2 and R3 build. At one seat it is the forklift for a spreadsheet. FACTOR at one seat has the trunk on the card and the ledger as a memory-mapped fold on the host, and that is the right split until a second seat exists.

So: two names for two objects, and no more synonym. **WarpBus** is the trunk's page table with lane tags, page hashes and the fence, the reflex plane's substrate, built when F-BLIND's measured cost says so, likely at FACTOR's M1. **TAPESTRY** is the store, built when the second seat arrives, and FACTOR's kernel becomes its peer on that day. The drawing's next revision should say so under the reflex plane and the tape.

---

## §3 · TAPESTRY as a spec

**What is right and should not move.** Law 6, the judge as a stored procedure whose body is weights, is the sharpest sentence anyone in the estate has written about what makes a store AI-first: the judge lives in the store, is invoked per delta, and stamps every verb with its pins. The cognitive index's key and invalidation rule are correct and make the fork cache a function. The transactor's refusals as rows, sagas with inverses, replay first, a verify mode on every fold, the explicit reuse map, and a tape entry that fusord's tools read unchanged: all right.

**Where its own red team and FACTOR's converged, which is the strongest signal either produced.** QC-6 found the seam erased because the judge authors the verb, a counterparty's free text reaching an irreversible write, the signature moved silently to judge keys, the canary regressed, the determinism claim self-contradicting, and per-delta judgment dropping the never-silence law and the budget pass. FACTOR's reviewers found the same six against FACTOR v0.1 in different words. Two forks of one mainline are one witness, but the seven reviewers on each side were not the same reviewers, and where they agree on a break the break is real.

**What TAPESTRY should lift from FACTOR v0.2 verbatim, because the fixes are already written and measured.** The gate with standing, readiness, affirmative clearance under a tie band, degraded evidence, and the safe terminal. The 128-byte host record with pinned encodings, `judged_rev`, `n_wit`, `release_ns`, `unit`, a seqlock and quantized `int16` margins; TAPESTRY's 64-byte GPU cell then becomes a projection of it, a fold, which keeps the sweep row at one cache line without two records disagreeing. No floats anywhere: `float amount`, `float margin` and "floats as their shortest round-trip decimal" all fail measured findings, a cent is not representable at a binary scale and two languages serialize one double two ways. The id as BLAKE2b with an 8-byte digest rather than FNV-1a, with collision fatal. The license test as a one-sided betting process with a Shiryaev–Roberts detector for demotion, the baseline paired on the stratum, grades in horizon order, the both-sides minimum. Exogenous outcomes only, with the hand's own reflections as receipts. The expectation row. Rungs with the canary at its own rung and `shadow` globally inert. The jury as provenance families with role separation and digest binding, the endpoint never required, and quorum never on a card's authorization webhook. Caps as rolling aggregates with a first-payee rule. The persistence clause as "health obligations never take DO and substrate effects need a signature," which is definable, where QC-6 rightly says a persistence constraint expression is not. And the sandbox: the peer hosts the judge, so it must run in the zero-capability container FACTOR measured today with CUDA working inside it and no penalty, and its channel to the transactor must be a pipe, not the socket the blueprint's module gate permits, because the module gate is a lint and not a boundary.

**What FACTOR should take from TAPESTRY.** The transactor as a named component: FACTOR's tape writer is one, and naming it makes M0's `tape.cpp` a transactor with constraints rather than a file appender. The cognitive index's explicit key: FACTOR's forks are implicit, and adopting `(id, judged_rev, template_pin, judge_pin)` with a stated invalidation rule turns the fork cache into a function with a falsifier. Sagas across obligations: FACTOR's effects are per obligation, and a discharge that touches three, a reschedule that moves three meetings, needs atomicity at the valve; the effect registry gains a saga with inverses applied in order and refused whole. A `refuse` kind for refused writes, which FACTOR has only for frames. A cold-recompute-equals-incremental check on every fold as a falsifier, not only a stamp. And the field: FACTOR deferred the pressure field with a reason, and TAPESTRY's R1 and R2 are where it lives when the reason expires.

---

## §4 · Backup or alternative, answered plainly

**Not a backup.** A backup is a copy of truth, and FACTOR's truth is its spools and its tape, which are files. A backup is `factor replicate` to object storage today, and at TAPESTRY's v1 it is FACTOR's tape as a tributary segment of a replicated store tape, with the seat's head landing on the store's tape as the tape-of-tapes law already provides.

**Not an alternative.** Splitting a seat's kernel into a transactor and a peer adds a hop on the hot path and a second process to sandbox, for nothing, at N=1.

**The same object, later.** At the second seat FACTOR's kernel is a TAPESTRY peer, its ledger is the cell table, its folds are the store's folds, its trunk forks are the cognitive index, its hand is the effector layer, its writ is the constraint layer. The only way that day costs nothing is if the formats already agree.

---

## §5 · What to do this week

1. **A joint format amendment to both blueprints,** one page: one tape entry schema, FACTOR's row plus TAPESTRY's `pos`, `cell`, `cls` and `by`; the 128-byte host record with the 64-byte GPU projection; canonical JSON with no floats and 64-bit fields as strings; BLAKE2b-256 keyed with the same six personalization strings; the same falsifier ids where the falsifiers coincide.
2. **TAPESTRY lifts the list in §3** rather than re-deriving it, and records in its own amendment which items it took and which it rejected with a reason.
3. **The WarpBus becomes a measured item at FACTOR's M1:** lane-tagged cells and a filtered sequence copy, built if F-BLIND's re-prefill cost exceeds the masked fork's by a pinned factor, else deferred with the number printed.
4. **One library, two repositories.** TAPESTRY's R0 and FACTOR's M0 both need the tape writer, canonical JSON, BLAKE2b and the spool reader. Build them once, under FACTOR's M0 since it is scoped first, and let TAPESTRY vendor them by hash.
5. **DWG-002 REV 3.** Under the reflex plane: "substrate: the trunk's page table, lane-tagged, the WarpBus." Under the tape: "shared with the store." Two names, two objects.

---
*Written 2026-09-08 by the FACTOR fork at the operator's request. Nothing here is a measurement except what it cites. The tape, as always, is the proof.*
