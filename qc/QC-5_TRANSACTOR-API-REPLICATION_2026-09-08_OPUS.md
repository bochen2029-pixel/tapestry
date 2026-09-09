# QC-5 · THE TRANSACTOR, CONSISTENCY, SAGAS, THE CLIENT API, REPLICATION
### Adversarial review of `TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1_2026-09-08_FABLE5-1.md` · slice: §3.1, §3.2, §3.7, §3.8, §4, §6, rungs R0 and R6, open questions 2, 3, 4, 8

**2026-09-08 · Claude Opus 5 · reviewer 5 of 7, the distributed-systems slice.** Read in full: the blueprint; `C:/fusor1/converge/src/fusord.cpp` (2,761 lines, in three targeted passes); `C:/NEW/FUSOR_MASTER-ONEPAGER_ADDENDUM-F_THE-BUILD-SPEC_2026-08-24_FABLE5.md` §3; `C:/55555/ORG-SOLVER_THE-THREE-OPERATIONS_ACID-IS-THE-SEAM_2026-09-08_FABLE5-1.md`; `C:/55555/TAPESTRY_..._BRAINSTORM_...md`; `C:/IT/RUNG0-SMOKE-RECEIPT-2026-08-23.md`. Precedents checked against source: TigerBeetle VSR, Datomic, Raft, Durable Objects, the transactional outbox, Materialize.

---

## 1 · Verdict

The data model and the six laws survive review, but the transactor as specified cannot enforce its own constraints, cannot be atomic with the world, and cannot hold both "position equals the deposit clock" and "the chain never forks" once Raft is under it — three separate impossibilities that the blueprint states as settled facts. The v0 code reuse is worse than it reads: `Tape::put` is `fflush`, not `fsync`, and swallows every failure mode silently, so the store of record inherits a durability claim that is false, and the lane-contract tailer proposed as the subscribe transport truncates any entry over 16 KiB and resets to offset zero on segment rotation. Nothing here kills the design — the laws are the right laws and the fixes are all local — but R0 must not be built from the blueprint as written, because four of its load-bearing sentences are wrong and one of its two gates does not test durability at all.

---

## 2 · Findings, most severe first

---

### F1 · The transactor cannot evaluate its own constraints
**§3.1, §3.3, §3.6, §1** · **BLOCKER** · **CONFIRMED**

**Claim.** §3.1: "For each proposed write: validate the constraint set for every touched cell." §3.6: native constraint kinds include "exposure caps per class per period" and "foreign keys between cells." §1: "Two processes in v0 … the transactor on CPU beside the tape files, and the peer on the card." §3.3: the peer holds "the cell table, the lattice, … the fold indexes."

**Defect.** An exposure cap per class per period is a *fold* — a sum over a window of the tape. A foreign key between cells is a *read of the cell table*. Both live on the peer. The transactor is a different process, on a different device, and the blueprint gives it no state of its own. So the commit path either (a) makes a synchronous call into the GPU process to evaluate every constraint, or (b) evaluates constraints against nothing.

Path (a) is disqualified by measurement. `C:/IT/RUNG0-SMOKE-RECEIPT-2026-08-23.md:18`: under co-tenancy, probe latency ran **0.6–24.6 s against a 44 ms bench figure**, and latency-to-notice climbed **26 s → 183 s**. Putting the card in the commit path means the system of record's write latency is bounded by nothing.

Path (b) means the writ is not enforced.

**Fix.** State the missing requirement: **the transactor holds its own authoritative, CPU-resident fold of exactly the state its constraints read** — the cell table's constrained columns, and the per-class-per-period exposure accumulators — maintained by the same deterministic reducer the peer runs, in the same fixed point. The peer's copy is then a second, redundant materialization of the same fold, which is a *feature*: it is a continuous cross-check. Add the falsifier that makes it one:

> **Falsifier 11 — two folds, one answer.** At every commit boundary, the transactor's state fold and the peer's state fold produce the same digest. Planted lie: a float accumulator on one side, or a constraint evaluated against a stale copy.

Corollary the blueprint must also state: **constraint expressions must be pure, total, and cost-bounded.** A check expression with unbounded cost stalls the only writer, and there is no second writer to take over.

---

### F2 · "Sagas atomic at the transactor" is two claims: one true, one impossible
**§2.2, §4, §3.1, open question 3** · **BLOCKER** · **CONFIRMED**

**Claim.** §4: "Sagas across cells are atomic at the transactor: all cells' constraints pass or nothing is appended." §2.2: "A cross-cell write is a saga: one `fact` per cell, each carrying its inverse, applied in order." §3.1: "require an inverse for every `fact`."

**Defect, part one — the word is wrong.** A saga is, by its 1987 definition and by the estate's own reading of it (`ORG-SOLVER_THE-THREE-OPERATIONS...md:24`, "a compensating transaction is application-level, the saga pattern"), the construction you reach for *because you cannot have atomicity*. The blueprint describes something that *is* atomic — all constraints pass or nothing is appended — and calls it a saga. An implementer reading §2.2 will build compensation machinery for the case that does not need it (cells inside one tape) and will not build it for the case that does (effects to the world), because §4 has told them effects are covered.

**Defect, part two — a multi-entry transaction is not atomic to a reader.** §2.2 says "one `fact` per cell … applied in order." Each fact gets its own `pos`. The fold on the peer applies entries one at a time. A `query(sql, as_of_pos)` landing between fact 1 and fact 2 of a two-cell transaction reads a state that never legally existed. Atomic *append* is not atomic *apply*, and the blueprint never distinguishes them.

**Defect, part three — the mandatory inverse makes the store unable to ingest the world.** §3.1 requires an inverse for *every* fact. But §2.3 declares `F_EXOGENOUS` as a class flag, and §1's diagram has `W -->|facts| TX` and `W -->|outcomes| TX`. A payment that cleared, a counterparty that replied, a sensor reading — these have no inverse the store can apply. Requiring one forces the implementer either to fabricate an inverse (a lie on the tape) or to refuse to record reality.

**Defect, part four — "the inverse" for a sent email does not exist, and the blueprint conflates three different things.** There is (i) an **undo**, which restores prior state and is valid only inside the tape; (ii) a **compensation**, which is a *new* action that offsets an old one (a correction email, a refund) and is itself refusable, gradeable, and capable of failing; (iii) a **retraction**, which cancels an effect that has not yet left. The `inverse` field means (i). §4's durability paragraph says nothing about (ii) or (iii), and §4's consistency paragraph implies they are covered.

**Fix.**

1. **Rename.** Inside the tape it is a **transaction**, not a saga. The word "saga" appears in the design only at the effector boundary.
2. **One transaction, one entry, one position.** Replace "one `fact` per cell" with a single `tx` entry whose body carries `facts[]`. Then `pos` names the transaction; `as_of_pos` is a transaction boundary by construction; `cell.pos_last` is a transaction position; and falsifier 5 ("the inverse that unwinds") becomes checkable, because there is a single position to roll back to. This costs nothing and closes defect two outright.
3. **The inverse is required only where it means something.** An `inverse` is mandatory on a fact that the store itself originated and that is reversible; it is forbidden on an `F_EXOGENOUS` fact. Encode that as a constraint, not a convention.
4. **Effects go through a transactional outbox, and delivery is at-least-once, never exactly-once.** See F3.

---

### F3 · The effect path: outbox, idempotency, and where the take-back window lives
**§2.2, §4, §7, open question 3** · **BLOCKER (specification missing)** · **CONFIRMED**

**Claim.** §1's diagram: `TX -->|committed writes with inverses| EFF`. §7: "The effector layer beside the store carries caps per class per period and prints every byte that leaves." Nothing anywhere specifies delivery semantics.

**Defect.** The tape and the world cannot be committed atomically; this is the two-generals result and it is not negotiable. Without a specified protocol, the obvious implementation — transactor commits, then calls the effector — loses effects on a crash between the two, and the obvious fix — effector calls first, then commits — duplicates them. The blueprint has neither, so an implementer will invent one, and there is no falsifier that would catch either failure.

**Fix — the exact model.**

**The outbox is the tape.** A write that requires an external effect appends, *in the same entry as its facts*, an `effect` intent:

```
{ k: "tx", body: { facts:[…], effects:[ {
    effector, payload_digest, idem_key, not_before_ns, takeback_until_ns,
    reversibility, cap_class } ] } }
```

**The idempotency key is derived from the tape, never generated.** `idem_key = blake2b256(entry.h ‖ effect_index)`. This is the whole trick: an effector that crashes mid-send recomputes the identical key from the tape on restart with no durable state of its own. A random key would have to be persisted before use, which reintroduces the same two-phase problem one layer down.

**Delivery is at-least-once; the counterparty's dedup on `idem_key` makes it effectively-once; exactly-once is never claimed anywhere.** This is the transactional-outbox result as universally stated: the pattern guarantees at-least-once to the broker, and consumer-side idempotency converts that to effectively-once. Write it into §4 as a law, because "committed writes with inverses" currently reads as a stronger promise than the physics allows.

**The effector is a subscriber, not a callee.** It subscribes from its own durable cursor, performs the effect, and appends `effect_result` through the transactor. The transactor never calls out; the commit path never waits on the world. This also means the effector's caps (§7) are evaluated against the tape, which is the only place they can be evaluated honestly.

**Where the delayed outbox with a take-back window lives — the answer to open question 3: split at one seam, and the seam is the clock.**

- **The store owns the decision and the deadline.** `not_before_ns` and `takeback_until_ns` are fields on the `effect` intent, on the tape, chained, replicated. A cancellation is a `retract` entry naming the `idem_key`. Nothing is edited; law 1 holds.
- **The effector owns the timer and the send.** Before sending, it must have read the tape *past* `not_before_ns` and found no `retract`. "Past" means: it has applied an entry whose `t_mono_ns ≥ not_before_ns`. This is why the tape needs idle ticks — otherwise the effector cannot distinguish "no retraction" from "I have not caught up." fusord already emits exactly this (`fusord.cpp:2559-2565`, the idle tick with `gap_s`), for exactly this reason. Reuse it and say so.
- **The failure mode that must be typed.** If the effector sends and dies before appending `effect_result`, the honest state is *unknown*, not *sent* and not *failed*. §3.9's verdict set is `right | wrong | ungraded_yet | ungradable`. Add **`unknown_delivery`**, and make an `effect_attempt` entry mandatory *before* the send (so the tape always records that a byte may have left). Without this row the store will silently claim an email was never sent when it was.

**What "the inverse" means for a sent email:** nothing. It means a *compensation* — a second effect, itself capped, licensed, refusable and gradeable — or, inside the take-back window, a *retraction*. Say this in §2.2 in those words, because the current text implies the store can undo the world.

---

### F4 · Under Raft, four claims cannot all hold on one artifact
**§2.1, §3.2, §4, §6, R6** · **BLOCKER** · **CONFIRMED**

**Claims.** §2.1: `pos` is "the deposit clock; monotone; never reused," and `h = blake2b256(prev ‖ canonical(body_and_header))` — so `pos` is inside the hashed bytes. §3.2: "v1 three-node Raft with the leader as transactor." §4: "Reads at a position are repeatable forever." Law 1: the chain is the only truth.

**Defect.** Raft's log-matching property makes `(index, term)` unique, not `index`. An entry a leader appends locally and does not commit before losing leadership is overwritten by the next leader at the same index with a different term: a new leader replicates its own log to followers, overwriting uncommitted entries. So **index — and therefore `pos`, and therefore `h`, since `pos` is hashed — is assigned twice.** TigerBeetle is the precedent that shows this is normal and survivable: the primary appends the prepare to the WAL, "assigning it the next sequence number and adding a checksum pointer to the previous log entry," and on a view change the new primary, on a nack quorum of blank headers for a possibly-uncommitted op, **truncates the log.** The hash chain forks at the uncommitted suffix. That is not a bug in TigerBeetle; it is what a chained consensus log does.

So one of the following must give:
- "position equals the deposit clock, never reused," or
- "the chain never forks," or
- the tape file is the Raft log.

**Fix — give up the third, keep the first two.** The Raft log and the tape are **two artifacts**:

- **The Raft log** is internal, truncatable, fsynced for safety (Raft requires fsync before ack; an un-fsynced ack can lose a committed entry on a correlated power failure), and is never read by any subscriber, verifier, fold, or human.
- **The tape** is written by the deterministic apply loop on every replica, in committed order, and receives **only committed entries**. Since Raft's leader-append-only property means a leader never deletes its own entries, an entry that survives to commit keeps the `(index, term, h)` it was created with, and a truncated entry never reaches the tape. **Positions are reused in the Raft log; they are never reused on the tape.** Both laws hold.

Consequences to write down:

- **The leader computes `h`, at propose time, and puts it in the replicated payload**, so every replica writes a byte-identical tape. It must not *publish* `h` or `pos` to any client before commit.
- **The chain becomes a fold over the committed log** — which is law 1's own shape, and is worth stating as such.
- **Raft's no-op entry** (the one a new leader appends in its term to commit prior-term entries) consumes an index and therefore a `pos`. It must be a typed tape kind (`term`, alongside `hdr`), not an unexplained gap or an untyped row.
- **Membership-change entries** (joint consensus) likewise consume positions and must be typed `rule` entries with `kind: config`.
- **Do not let the Raft library compact the tape.** Every Raft implementation snapshots and truncates its log. If the tape *is* the log, Raft deletes the truth. This is the concrete trap the two-artifact rule prevents, and it must be in §3.2 in bold.
- **Cost:** one synchronous fsync (the Raft log, before ack) plus one asynchronous append (the tape). The tape append need not be synchronous, because the tape is a deterministic function of the committed log; on restart, replay the log from the tape's end. One fsync on the critical path, not two.

---

### F5 · The tape's durability is `fflush`, not `fsync`, and the append path swallows every failure
**§3.1, §3.2, §4, §11, R0** · **BLOCKER** · **CONFIRMED**

**Claim.** §3.1: "append; fsync; publish the position." §4: "An entry is durable when fsynced on the leader." §11: "`fusord.cpp` · `Tape`, `chain_hash`, `Blake2b`, `write_atomic`, `replace_file` → the tape store's entry writer and chain."

**Defect.** `fusord.cpp:902-909`:

```cpp
std::string put(const std::string& body) {
    if (!f) return prev;
    const std::string h = chain_hash(prev, body);
    std::fprintf(f, "%s,\"prev\":\"%s\",\"h\":\"%s\"}\n", body.c_str(), prev.c_str(), h.c_str());
    std::fflush(f);   // durable as it goes, not at exit
    prev = h; ++n;
    return h;
}
```

Four defects in seven lines, each fatal for a system of record:

1. **`fflush` is not `fsync`.** It moves bytes from the stdio buffer to the OS page cache. It survives process death; it does not survive machine death. The comment on line 906 claims durability it does not provide, and §11's reuse mapping inherits that claim. Windows needs `FlushFileBuffers`; POSIX needs `fsync`/`fdatasync`.
2. **`if (!f) return prev;`** — a write to a closed or never-opened tape returns the *previous* hash and reports success. The caller cannot tell an append from a no-op. This is a silent-loss path in the one component that may not have one.
3. **`std::fprintf`'s return is unchecked.** A short write on ENOSPC produces a torn row and returns normally.
4. **`std::fflush`'s return is unchecked.** Same.

**Fix.** Annotate §11's row: *"`Tape::put` minus its durability claim."* The v0 entry writer must: fatal on `!f`; check `fprintf`'s byte count; check `fflush`; call `FlushFileBuffers`/`fsync` before the position is published; and it must do all of that **once per group-commit batch**, not once per entry (F9). Add to R0's gate: `kill -9` the transactor 1,000 times at random points under load; every entry acked to a client is present with its `h`; no unacked entry is present; the chain verifies from genesis.

---

### F6 · The peer is not forbidden from reading uncommitted entries, and has no rollback if it does
**§3.3, §3.7, §4, §6** · **MAJOR** · **CONFIRMED**

**Claim.** §3.3: "Reads the tape as a subscriber from its last applied position." §4: "The peer is not durable and does not need to be." §6: "peer crash → no truth lost."

**Defect.** Nothing says the peer may read only committed entries, and in v0 the natural implementation reads them uncommitted. `Tape::put`'s `fflush` (`fusord.cpp:906`) makes an entry visible to a file tailer the instant it is written and before any fsync. On a power loss the tape can lose its tail while the peer has already folded it. The peer then rebuilds from the tape and arrives at a *different* state than it was serving — silently, since §6's "no truth lost" row assumes the rebuild is a no-op.

Worse under Raft: the peer subscribed to a leader's local log would apply entries at positions that a later leader reassigns. The field fold's fixed-point accumulators are not invertible without an undo log; `cell.pos_last` would carry a revoked position; and the whole staleness discipline (`ORG-SOLVER_...md:26`: "the deposit clock is the log position") is built on positions being final.

**Fix.** State it as a law, in §4:

> **The peer never reads an uncommitted entry.** Its input is the apply stream of the committed log. There is no rollback protocol on the peer, because there is nothing to roll back.

And add the four subscription rules the blueprint is missing:
- the peer **refuses** any delta with `pos ≤ applied_pos` (duplicate delivery is expected; see F7);
- the peer **fatals** on a gap (`pos > applied_pos + 1`) rather than skipping;
- the peer **verifies** `delta.prev == last_h` on every delta — the chain, not a sequence number, is the gap detector;
- on **leader change**, the peer reconnects and resumes at `applied_pos + 1` with that chain check. If the check fails, the peer's history diverged from the cluster's and it must **rebuild from the last fold checkpoint at or below the cluster's committed position** — never continue. Add this row to §6; it is the most likely real corruption in the whole design and §6 does not contain it.

Precedent for the shape: Datomic peers "see all transactions up to their time basis, in order, with no gaps," and see only *completed* transactions.

---

### F7 · Duplicate delivery breaks Conservation, and nothing prevents it
**§3.7, §2.4, §9** · **MAJOR** · **CONFIRMED**

**Claim.** §3.7: `subscribe(query, from_pos)` returns "a delta stream with positions." §9 falsifier 2: "Every unit of amount lands in exactly one lattice cell."

**Defect.** Any resumable stream is at-least-once. fusord learned this the expensive way and encoded it: `fusord.cpp:575-578` — "the cursor is committed as INGESTED, never as read or pushed. A crash while the ring holds a backlog replays the ring (at-least-once, visible on the tape), never skips it" — with a `replayed_frames` counter carried to the end row (`fusord.cpp:2608`). The blueprint drops both the discipline and the counter. A `fact` delivered twice and folded twice double-counts an amount, and falsifier 2 fails in production while passing in test, because the test never redelivers.

**Fix.** The dedup rule from F6 (`refuse pos ≤ applied_pos`), plus a `replayed` counter on the health call, plus one line in §9 amending falsifier 2: *the conservation test must include a redelivery of a random suffix of the stream.*

---

### F8 · `query(as_of_pos)` has no defined isolation, and historical reads have no version store
**§3.7, §4, §2.4** · **MAJOR** · **CONFIRMED**

**Claim.** §3.7: `query(sql, as_of_pos)` → "SQL over any fold at a position." §4: "Reads at a position are repeatable forever."

**Three defects.**

1. **A reader can see a fold between two appends.** §2.4 says the field fold is "maintained on every append" *and* runs a "sweep kernel to tolerance." A relaxation sweep is not instantaneous. If a delta is admitted while the sweep for the previous position is in flight, the fold's value at that position was never materialized and `query(as_of_pos=P)` cannot be answered honestly.
2. **A reader can see a torn transaction** — until F2's one-entry-per-transaction fix lands.
3. **"Repeatable forever" has no implementation.** The folds are mutable arrays in HBM with no MVCC. Datomic can promise as-of cheaply because its index is a persistent tree in storage with structural sharing; TAPESTRY has a 64-byte-row table on a card. A read at an old position is a *rebuild*, and the blueprint prices it at nothing.

**Fix — adopt the Durable Objects gate pattern verbatim, because it is exactly this problem.** DO uses input gates to stop events interleaving with a storage operation and output gates to prevent premature confirmation of writes; writes coalesce so the gate waits O(1) round trips, not O(n).

- **Input gate:** the peer applies deltas in **batches at commit boundaries**, runs the sweep to fixed point, and only then publishes `applied_pos = P` atomically. No delta is admitted mid-sweep.
- **Output gate:** the transactor does not publish `pos` or ack a client until the batch's fsync (v0) or majority ack (v1) returns.
- **Then state the guarantee exactly**, and put this sentence in §4 replacing "Reads at a position are repeatable forever":

> `query` is **snapshot isolation at a committed, published position, monotonic per peer**. A query with no `as_of_pos` is served at the peer's current `applied_pos`, which is returned with the rows. A query with `as_of_pos ≤ applied_pos` and above the peer's retained-version floor is served from the snapshot; below the floor it is a rebuild from the nearest fold checkpoint and is priced in seconds. A query with `as_of_pos > applied_pos` blocks until the peer applies it or refuses with `stale_basis`. **Reads are not linearizable across peers**: if peer A observes a write and then peer B is read, B may not have it — this is Datomic's guarantee and it is the right one, but it must be written down, not assumed.
- **Read-your-writes** is then constructible and must be named as a call parameter: after `write` returns `pos P`, `query(…, min_pos=P)` blocks until the peer has applied P. Without this the model client cannot read what it just wrote, and it will retry (F13).

---

### F9 · Ten thousand entries a second with fsync per entry is not achievable, and the target is 3,400× the need
**§3.1, §5** · **MAJOR** · **CONFIRMED**

**Claim.** §3.1: "Throughput target for v0: ten thousand entries a second on one core **[BUDGET]**, which is two orders above any organization's event rate."

**Defect.** With one fsync per entry, maximum throughput is `1 / fsync_latency`. On an SSD without power-loss protection that is 1–4 ms, i.e. **250–1,000 entries/s**. Enterprise NVMe with PLP reaches ~7,000 fully durable transactions/s and only "provided those transactions are issued by a sufficient number of threads" — which is the opposite of "on one core." The stated target is off by one to one and a half orders of magnitude, and it is stated next to an fsync requirement that makes it impossible.

Worse, the target is unnecessary by the blueprint's own sizing. §5: 250,000 judgments/day = **2.9 entries/s mean**. The target is ~3,400× the need. A throughput number that far above the requirement invites exactly one optimization — dropping the fsync — which is the one thing that must never be dropped.

**Fix.**

1. **Replace the throughput target with a latency SLO**, because the measured failure mode on this estate is latency, not throughput: `RUNG0-SMOKE-RECEIPT:18` records latency-to-notice climbing 26 s → 183 s, and that is what a store must never do. Proposed: **p99 commit latency < 10 ms at a sustained 200 entries/s and a 2,000-entry burst**, on one core.
2. **Specify group commit, since it is the mechanism that makes even 200/s durable.** The transactor accumulates arriving entries into a batch while an fsync is in flight; when the fsync returns, every entry in the batch is acked. Throughput becomes `batch_size / fsync_latency`; at batch 32 and fsync 1 ms that is 32,000/s, three times the abandoned target, with p50 latency ≈ 1.5 × fsync and p99 ≈ 2 × fsync plus queueing. This is what PostgreSQL, InnoDB and Kafka all do and there is no reason to invent anything.
3. **The latency cost is free relative to the product's own clock.** A judgment costs 110–130 ms [M: `probe_one`, §3.5]. A 2 ms commit is 1.5–2% of one judgment. Say this in §3.1 so nobody trades durability for a number that does not matter.
4. **Group commit requires a dedicated commit thread** with an in-flight-fsync queue. "Single-threaded append loop" (§3.1) makes group commit impossible: one thread doing validate-then-fsync-then-validate cannot batch. The correct shape is **one logical writer, three threads**: validate → commit/fsync → publish. "One writer" is a serialization law, not a thread count. Fix the wording; it will otherwise be implemented literally.
5. Under Raft the same batch is the replication unit: commit latency = 1 RTT + parallel follower fsync ≈ 1.2–2 ms on a LAN. Unchanged conclusion.

---

### F10 · The v0 subscribe transport truncates entries and cannot survive segment rotation
**§3.7, §3.2, §11** · **MAJOR** · **CONFIRMED**

**Claim.** §3.7: "the lane-contract v0.1 text framing remains accepted for subscribe so fusord's tailer works unchanged." §11: "`LaneTail`, the lane contract → the v0 subscribe transport." §3.2: "Append-only segment files, 64 MiB each, named by first position."

**Four defects, all in `fusord.cpp`.**

1. **Frames over 16 KiB are silently truncated.** `fusord.cpp:590`: `size_t frame_cap = 16384;`. `:747` and `:769`: bytes past the cap are **dropped** and counted, and the frame is delivered short. A `fact` entry carrying `before`, `after` and `inverse` for a real row routinely exceeds 16 KiB. The subscriber receives a mutilated body, cannot verify `h` against it, and — with the current design — has no way to tell a truncated entry from a small one except a counter it does not read.
2. **Resume is by byte offset, not by position.** `:643-654` (`prefix_hash` over the ≤4096 bytes before the offset) and `:635-642` (`read_cursor_file`). A byte offset is meaningless across 64 MiB segment files.
3. **Rotation resets the stream to zero.** `:730-732`: `if (n < base) { reset_events++; off = 0; base = 0; offset.store(0); … }`, and `:627`: a cursor-hash mismatch produces `set_start(0)` with reason `cursor_mismatch_rotation_reset`. With segmented tape files, every segment roll either restarts the subscriber from the beginning of the world or from byte zero of the wrong file.
4. **The framing is lossy for the hashed bytes.** The lane contract is `t_mono_ns \t lane \t grain \t text` with "tabs/newlines flattened" by the producer (Addendum-F §3.1). Canonical JSON survives that, but the contract's own text says the cap is a *header field* to be disclosed — which the tailer ignores in favour of its compiled-in 16384.

**Fix.** The lane contract stays as the **venue → transactor intake** transport, which is what it was designed for and what it is good at. It is **not** the subscribe transport for a tape whose entries are the truth. For v0 subscribe: a length-prefixed binary/JSON frame stream over a socket, resumed by `(pos, last_h)`, with no cap and an explicit `TOO_LARGE` refusal rather than a silent truncation. If the file-tail path is kept as a fallback, it must be per-segment with a `(segment, offset, pos, h)` cursor and a hard failure — not a reset — on mismatch.

---

### F11 · Typed refusals on the tape are a denial-of-service with no rate limit anywhere in the design
**§3.1, §3.7, §9.4** · **MAJOR** · **CONFIRMED**

**Claim.** §3.1: "A refusal is itself an entry." §9 falsifier 4: "Every refused write appears as a `refuse` entry with a typed reason."

**Defect.** A judge in a loop — a plausible failure mode for a model client with a retry policy — proposing a forbidden write at 10,000/s produces 10,000 permanent, chained, Raft-replicated, forever-retained tape entries per second. Each costs a validation pass, a slot in the group commit, a replication round trip, storage forever, and a delta to every subscriber. It starves real writes on the single writer. And the attack surface is the *writ itself*: the harder the constraint, the more refusals it generates. There is no rate limit in §3.1, §3.6, §3.7 or §7.

**Fix — three layers, and one honest amendment to falsifier 4.**

1. **Separate protocol faults from judgments.** A refusal that reflects a decision about the world — `constraint_violated`, `quorum_insufficient`, `license_exceeded`, `pin_mismatch` — is an entry. A refusal that reflects a transport or client fault — `malformed`, `not_leader`, `rate_limited`, `duplicate_request`, `stale_basis` — is a **counter on the health call, not an entry**. The tape records the organization's decisions, not the protocol's hiccups. This alone removes most of the volume.
2. **Coalesce identical refusals.** Identical `(principal, constraint_id, cell, reason)` inside a window collapses to one entry carrying `count`, `first_pos`, `last_pos`. This preserves the *meaning* of falsifier 4 — no refusal is ever silent — while bounding the volume.
3. **Budget, then demote.** A per-principal token bucket at admission, debiting refusals at (say) 10× the rate of accepts. On exhaustion of a daily refusal budget, the **license fold demotes the principal** — a mechanism §3.9 already has ("narrows on any evidence"), applied to a new kind of evidence. Further writes are refused at admission with a counter and one summary entry, not one entry each.
4. **Amend falsifier 4 explicitly, in the document, as a doctrine change:** *"Every refused write is either an entry or is counted in an entry."* Do not let this happen by accident in the implementation.

**Where the limits belong:** admission (per principal, per second) at the transactor's front door, before validation; per-class-per-period caps in the constraint layer where they already are; per-effector caps in the effector layer where §7 already puts them. Three limits, three layers, none of them optional.

---

### F12 · The client API is missing six calls, two of its seven are not client calls, and there is no way to register a judge or change a rule
**§3.7, §3.5, §3.6, §2.4** · **MAJOR** · **CONFIRMED**

**Claim.** §3.7: "Seven calls."

**Defects.**

- **There is no call that emits a `judge` or a `rule` entry.** §2.1 defines both kinds; §3.5 requires "registration by `judge` entry"; §3.6 requires that constraints "change only by `rule` entries signed by the operator key." `write(tx)` takes `facts[]`. So the API as specified cannot register a judge or ratify a rule — the two operations without which nothing else in the system can start. This is a hole, not an omission.
- **`serialize(cell, template_pin)` and `fork(handle)` are not client calls.** §3.3 states the judge runs inside the peer "so a cell's bytes go from row to template to tokens to logits without leaving the card." Any *external* client calling `serialize` moves the canonical bytes off the card, and `fork(handle)` returns a handle to VRAM pages an external process cannot use. Either these are the peer's internal ABI (correct) and do not belong in the client API table, or the design admits an external judge and the zero-copy claim in §3.3 is false. Pick one; the blueprint currently asserts both.
- **No acknowledgement / cursor commit on `subscribe`.** This is the F1 lesson of fusord (`fusord.cpp:575-578`, `:656`, `:2530`: the cursor follows what was *ingested*, never what was read). Without it, at-least-once has no mechanism and a slow subscriber either loses deltas or the server must retain them forever.
- **No `unsubscribe`.** A stream with no teardown leaks server state and, with `subscribe`'s standing query, GPU state.
- **No schema introspection.** A model client cannot form a legal write without knowing the classes, the templates and their pins, the horizons, the constraints, the reversibility classes and its own current license. The alternative — putting them in the model's prompt — means the client reasons from a copy that goes stale, which is precisely the failure the writ-as-constraint law exists to prevent. **This is the single largest omission for a store whose reader is a model.**
- **No health.** Addendum-F §3.2 is explicit that the pill and the toggle "are contract elements, not implementation details — every venue ships them," and fusord implements the pill in full (`fusord.cpp:810-853`, 30 fields including `probe_ms_p95` and `lat_ms_p95`). The blueprint drops it. The peer's `applied_pos`, the leader's `committed_pos`, the lag between them, the warm ratio and the refusal rate are the five numbers any operator or model needs and none is reachable.
- **No `verify_fold`.** §2.4 says "Every fold has a `verify_fold` mode" and R1's gate depends on it. A mode that exists in prose and not in the interface cannot be run by the party that needs it.
- **No `explain_refusal`.** A typed reason without the constraint's identity and the offending value cannot be repaired by the model, which will retry the same write — see F11.
- **No leader redirection.** Under R6 a client connected to a follower must be told where the leader is. There is no `not_leader` reason and no field for the address.

**Is `hold` a separate call or a write with an empty fact set?** **Separate, and for exactly one reason:** law 2 makes a hold unrefusable, and `write` is the refusable path. Routing a hold through `write` puts the record of the non-event behind a constraint evaluator that could reject it, and falsifier 3 ("the hold that never vanishes") would die in the one place nobody looks. Keep `hold` as its own call on its own path, and specify that it evaluates no constraint because it changes no cell row. (Note for the data-model reviewer: §2.2 puts `margin` on the hot `Cell` while §2.4's state fold reads only `fact` entries — so `Cell.margin` is maintained by no declared fold. That inconsistency is what makes the hold-is-a-write question look open.)

**Do we need batch writes?** No new call. Declare the wire protocol **pipelined with client request ids**, so an ingest burst is N in-flight requests on one connection, not N round trips. That plus group commit gives the throughput without a second write verb whose partial-failure semantics would have to be invented.

---

### F13 · No client request idempotency: a model that retries a timed-out write applies it twice
**§3.1, §3.7** · **MAJOR** · **CONFIRMED**

**Claim.** Law 3: "One writer. A single serializing transactor appends to the tape. Two hands never sell one seat."

**Defect.** One writer prevents *concurrent* double-apply. It does not prevent *sequential* double-apply from a client retry. A client whose `write` times out — during a leader election, a GPU stall, a group-commit queue backup — cannot distinguish "not committed" from "committed, ack lost," and will retry. The retry is a new proposal and commits as a second entry. Two seats sold, by one hand, twice. The client here is a model with a retry policy, so this is not a corner case.

Both named precedents solve it and the blueprint uses neither: TigerBeetle has client sessions with request numbers and a cached reply per client; Datomic's transactor is the single point at which a transaction is identified.

**Fix.** Every write carries `(client_id, request_id)`. The transactor keeps a bounded reply cache keyed on that pair and, on a duplicate, returns the original `pos` with reason `duplicate_request{original_pos}` and appends nothing. The cache must be part of the replicated state machine, not leader-local, or a leader change reopens the hole.

---

### F14 · The cognitive-index checkpoint records the tape's chain head and the restore ignores it
**§3.4, §4, §6** · **MAJOR** · **CONFIRMED**

**Claim.** §3.4: "warm pages may be checkpointed to NVMe with a `.meta` carrying `pos_last`, template pin, judge pin, the same guards as fusord's `checkpoint` and `restore`; on restore the guards must match or the entry is rebuilt."

**Defect.** In fusord the guard is written and never enforced. `fusord.cpp:1530` writes it: `meta += "prev\t" + tape.prev + "\n";` — the tape's chain head at the moment of the checkpoint. `parse_meta` (`fusord.cpp:1550-1570`) has branches for `model`, `model_sha256`, `serve`, `npast`, `spool`, `t_last_frame_wall`, `cursor` and `tail` — **and no branch for `prev`**. `Kernel::restore` (`:1574-1607`) therefore checks model identity, serve hash, weight sha256 and token count, and never checks that the restored read state belongs to this tape's history. "The same guards as fusord's checkpoint and restore" therefore inherits a guard that does not run.

The consequence in TAPESTRY is worse than in fusord: after a crash that lost the tape's tail (F5), the peer restores KV pages describing a rendered context at a position the tape no longer contains, all pins matching, and judges from it.

**Fix.** Mandatory in TAPESTRY, and a one-line fix in fusord: restore refuses unless `meta.prev` is on the current tape's chain at or below the tape's head. Add the §6 row: *cognitive index restored from a rolled-back tape → refuse, rebuild lazily.*

---

### F15 · "Years of tape in hours" contradicts §5's own arithmetic by three orders of magnitude
**§3.8, §5** · **MAJOR** · **CONFIRMED**

**Claim.** §3.8: "Replaying history is the first thing run on any new judge and on any new organization: years of tape in hours **[BUDGET]**."

**Defect.** §5's own row: 250,000 judgments/day at 110 ms serial = **about 7.6 hours of card time per day of tape**. One year of tape is 365 × 7.6 h = **2,774 hours ≈ 115 days**, serial, on one card. "Years of tape in hours" requires roughly a 1,000× speedup over the number printed four sections earlier in the same document. Neither the three-seat co-decode figure (1.208× worst case) nor any batching factor stated anywhere reaches it. Two `[BUDGET]` rows in one document contradict each other, and the one that flatters the design is the one with no arithmetic.

**Defect, second half.** Replay speed is a *batching* claim: it is only fast if judgments batch across cells, which requires N cognitive-index forks resident simultaneously — capped by §5 at ~15,000 warm cells per card (45,000 with 4-bit KV). So the batch ceiling is the warm set, and batched decode time grows with batch size, so the speedup is well below the batch factor.

**Fix.** Delete the claim or replace it with the honest one: *"Replay throughput is `batch_size / batched_decode_ms`, bounded above by the warm-set size; it is unmeasured and is R4's first receipt."* Then make it R4's gate.

**Related, same section:** §3.8 does not say **who writes the shadow tape**. If the transactor does, a replay saturates the single writer that also serves production — the one resource the whole design has exactly one of. Fix: shadow tapes are peer-local files with their own chains; law 3 is per-tape, not global. State it.

**Related, and a direct contradiction with §6:** §3.8 promises replay over history; §2.3 says a schema-map pin change "invalidates every cognitive index entry"; §6's schema-change row says "replay from the change position." A replay that crosses a template or schema change must render each cell under the *historical* pin, which requires every template version to be retained forever and addressable by pin. That requirement appears nowhere, and §6's row contradicts §3.8's promise.

---

### F16 · Process shape (open question 2): the receipt cited does not answer the question, and the answer is two processes
**Open question 2, §1, §3.1, §3.3** · **MAJOR** · **CONFIRMED**

**Claim.** Open question 2: "One process or two in v0: fusord's shape puts transactor and peer in one; the blueprint splits them. **Decide by the tenancy receipt.**"

**First defect: the receipt does not decide it.** `RUNG0-SMOKE-RECEIPT-2026-08-23.md:18` measures **VRAM contention from a desktop workload** — "the desktop was holding ~14 GB VRAM before load (15.7/16.4 during; model partially evicted/shared)" — and concludes "the resident cannot share a 16 GB card with a full desktop workload at bench speeds." That is an argument about *card tenancy*, not about *process count*. A CPU-side transactor consumes no VRAM in either shape. Citing this receipt as the deciding evidence is a category error and should be struck from §10.

**The receipt does decide something else, and it is decisive.** It measures that **peer latency is unbounded in practice**: probes 0.6–24.6 s against a 44 ms bench, latency-to-notice 26 s → 183 s. That is the input to the real argument.

**The argument for one process (fusord's shape), honestly stated.**
- Zero IPC on the hot path: in fusord the GPU thread runs `poll → ingest_delta → judge_and_maybe_emit → tape.put` on one thread (`Kernel::run`, `fusord.cpp:2539-2578`), so a verdict reaches the tape with no serialization and no socket.
- One failure domain: the transactor's fold and the peer's fold cannot disagree, because there is only one (F1's new falsifier is unnecessary).
- The control plane already exists as one process's: the pill and the switch (`fusord.cpp:810-853`, `:502-518`), which Addendum-F §3.2 makes contract elements.
- Simplest R0: one binary, one tape, one cursor, one shutdown path (`Kernel::shutdown`, `:2580-2636`).

**The argument for two processes, and why it wins.**
1. **Head-of-line blocking, measured.** In fusord's shape the thread that owns the tape is the thread that runs the judge. In fusord that coupling delays *judgments*. In TAPESTRY the same thread is the **only writer of the system of record**: a 24.6 s probe stalls every fact from the world, every refusal, every rule change, for 24.6 s. The store's durability path would be behind a device whose measured tail latency is tens of seconds. That is disqualifying on its own.
2. **Failure isolation, and law 5.** The judge path is foreign code (llama.cpp, the CUDA driver) whose realistic killer — VRAM exhaustion at 15.7/16.4 GB — is exactly what the receipt measured. Law 5 says the peer is a re-derivable cache. A cache must be permitted to die without taking the truth with it. One process makes the cache's death the truth's death.
3. **The fsync path forbids it.** Group commit (F9) requires a dedicated commit thread with an in-flight-fsync queue. A single thread that judges, validates and fsyncs in sequence cannot batch, and each fsync (0.1–4 ms) steals GPU scheduling time. Once you have a separate commit thread with its own queue and its own state, the process boundary costs one socket and buys the isolation.
4. **The constraint-state requirement (F1) already puts an authoritative CPU-resident fold in the transactor.** That fold has no reason to live in the GPU process, where it would compete for the process's memory with the judge.

**Recommendation for v0: two processes.** Transactor = one *logical* writer implemented as three threads (validate → group-commit/fsync → publish), holding its own CPU-resident constraint fold. Peer = one process per card. Transport = a length-prefixed frame socket (Unix domain / named pipe); at the estate's ~3 appends/s a socket is three orders of magnitude more than adequate and a shared-memory ring is premature. Fix §3.7's "in-process C++ for the peer," which contradicts §1's "two processes in v0" outright.

---

### F17 · §9 has ten falsifiers and not one tests durability, crash, or leader change; R6's gate is unfalsifiable
**§9, R0, R6** · **MAJOR** · **CONFIRMED**

**Claim.** R0's gate: "two cold replays bit-identical; the refusal that never silences." R6's gate: "a leader kill mid-saga leaves the folds whole."

**Defect.** The ten falsifiers test determinism, conservation, holds, refusals, inverses, index purity, fork cost, quorum, clock width and grading. **None tests a crash. None tests an fsync. None tests a leader change.** The entire durability and consistency surface — the subject of §4 — is unfalsified. R6's gate is not a test: "mid-saga" is undefined once a transaction is one entry (F2), and "whole" is undefined entirely.

**Fix — restate R6's gate as four assertions under a fault injector, N ≥ 1,000 kills in the propose-to-commit window:**
1. every write **acked to a client** is present on all three replicas at the same `pos` with the same `h`;
2. no **unacked** write is present on the new leader's tape;
3. the peer's applied chain is a **prefix** of the new leader's tape;
4. after recovery, all three replicas' tape files are **byte-identical**.

**And build the fault injector at R0, not R6.** TigerBeetle's ability to make this class of claim rests on deterministic simulation testing; TAPESTRY's §4 determinism laws — fixed-point accumulation, canonical serialization, pinned reducers, no wall time, a fixed seed per invocation — are precisely what make that simulator *possible*, and the blueprint never says so. This is the most valuable thing the determinism laws buy and it is unclaimed. R0's own gate ("two cold replays bit-identical") is the degenerate zero-fault case of the same harness.

**Add to R0's gate:** the crash test from F5; the redelivery test from F7; the client-retry double-apply test from F13.

---

### F18 · Torn trailing bytes are left in the tape file, and "fold over the tape" is undefined for them
**§3.2, §6, §11** · **MINOR** · **CONFIRMED**

**Claim.** §3.2: "Torn trailing entries are skipped, counted, and warned, as in `Tape::open`."

**Defect.** `Tape::open` (`fusord.cpp:863-899`) does not skip the torn bytes — it recovers `prev` from the last complete row and then, at `:897`, **appends a newline to the torn bytes** so the next row starts clean. The partial entry stays in the file forever as a line-shaped object that is not in the chain. A fold defined as "a deterministic reducer over the tape" has no defined behaviour on it: a parser will fail, and a parser that skips unparseable lines will silently skip. Both cold replays would agree, so falsifier 1 passes while both replays are wrong.

(The chain-head recovery itself is safe — `:887` uses `rfind("\"h\":\"")` and `h` is the last field — but it is a byte scan of a format that §2.1 already plans to replace with length-prefixed binary. It should not be carried forward.)

**Fix.** One sentence in §2.4: **a fold's input is the chain-verified entry sequence; a line that is not in the chain is a hard error, never a skip.** And on open, truncate the torn bytes rather than terminating them, recording the discarded byte count in the `warn` entry.

---

### F19 · Two different things are both called "quorum"
**§3.6, §3.2, §7, R5, R6** · **MINOR** · **CONFIRMED**

Raft's majority (§3.2, R6) and the N-of-M judge signature set for irreversible writes (§3.6, §7, R5, falsifier 8) are unrelated mechanisms with different failure modes, different key material and different membership. Both are "quorum" throughout. An implementer will conflate them, and the conflation is dangerous in exactly one direction: satisfying the Raft majority is not evidence about judge independence. Rename the second to **writ quorum** or **signature set**.

---

### F20 · §6's failure table is missing the three failures that will actually happen
**§6** · **MINOR** · **CONFIRMED**

Missing rows: **(a) leader loses quorum / minority partition** — writes must refuse loudly with `not_leader` or `no_quorum`, never hang; the blueprint has no timeout and no refusal reason for it. **(b) peer ahead of the transactor** — see F6; this is the most likely real corruption and there is no row. **(c) fold checkpoint corrupt or pin-mismatched** — §2.1's `ckpt` body carries a `digest`, and §6 has no row for what happens when it disagrees. Add all three.

---

### F21 · Open question 8, answered: full recomputation per *append* is already too slow; per *batch* is fine, and the criterion is a latency criterion
**Open question 8, §2.4, §3.1** · **MINOR (answered)** · **PLAUSIBLE**

**First, a correction to the question.** §2.4 lists both `state` and `field` as "maintained: on every append," but the **state fold is differential by construction** — one fact touches one cell row. Only `field` (the lattice plus its relaxation sweep), `allocate` and `license` are candidates for full recomputation. The question is about `field`.

**The criterion, in the requested units.**

Let `R` = wall-clock ms of one full field recomputation over the open set, and `A` = appends/s.

- **Duty-cycle bound:** full recomputation stops being acceptable when `A × R > 100 ms per wall second` — i.e. when fold maintenance takes more than 10% of the peer's duty cycle, since the other 90% is judgments at 110–130 ms each, which are the product.
- **Latency bound, which binds first and is the one that matters:** it stops being acceptable when `R` exceeds a quarter of the delta-to-judgment budget, because the measured failure mode on this estate is latency-to-notice, not throughput (`RUNG0:18`, 26 s → 183 s). Both `R` and the p95 latency-to-notice are already instrumented in fusord's pill (`fusord.cpp:834-835`), so this is measurable from day one and needs no new machinery.

**Numbers.** §5 gives 250,000 judgments/day = 2.9 appends/s mean; a 20× business-hours-plus-CDC-burst peak is ~60 appends/s. At 60 appends/s the duty-cycle bound gives `R < 1.7 ms` — a full relaxation sweep over 50,000 cells to fixed point will not be 1.7 ms. **So the blueprint's parenthetical "likely yes for v0" is probably wrong at peak, and right at mean.**

**The escape that keeps v0 simple: recompute per commit batch, not per append.** The transactor already group-commits (F9); the peer already gates apply at commit boundaries (F8). At 60 appends/s with a 32-entry batch that is ~2 recomputes/s, and the duty-cycle bound becomes `R < 50 ms` — comfortably reachable for a 50,000-cell lattice on the card. **Recommendation: v0 recomputes the field once per applied batch. Differential maintenance (differential dataflow / Materialize's incremental view maintenance) is deferred to the point where `R > 50 ms` or the batch rate exceeds 2/s — and both numbers go on the health call from R2.**

---

### F22 · R6: which Raft, and what changes when the leader is the only writer and the only hasher
**R6, §3.2** · **MINOR (answered)** · **PLAUSIBLE**

**Which implementation.** **NuRaft** (eBay, C++11, Apache-2.0). Reasons: the transactor is C++ beside the tape; NuRaft exposes a custom `log_store` and a custom `state_machine`, which is precisely the two-artifact split F4 requires; and it is the consensus layer under ClickHouse Keeper, a production ZooKeeper replacement, which is the strongest available evidence that a custom log store on it works. Alternatives, ranked: Canonical's `raft` (C, small, used in Dqlite) if C++ is a liability; etcd/raft or openraft only if the transactor moves off C++, which would strand the fusord reuse.

**What changes when the leader is also the only writer and the only hasher.**
1. **Nothing forwards.** There is no write-forwarding; a client on a follower gets `not_leader{leader_addr}` — a refusal reason that does not exist yet (F12).
2. **`h` goes in the replicated payload**, computed by the leader at propose time, so every replica's apply loop writes a byte-identical tape (F4). Each replica should still report its computed `h` at intervals; a mismatch is fatal-loud, not a warning.
3. **The Raft log store must never be the tape**, or NuRaft's snapshot-and-truncate will delete the truth (F4).
4. **Follower reads need a bound.** The blueprint never says which node the peer subscribes to. If a peer subscribes to a follower for load, it can serve unboundedly stale reads. Either the peer subscribes to the leader, or every response carries `applied_pos` and the client's staleness bound is enforced (F8).
5. **The state machine must include the client reply cache** (F13), or a leader change reopens the double-apply hole.
6. **Two quorums** (F19). Do not let the Raft majority be mistaken for the writ quorum.

---

## 3 · The commit protocol for v1 (Raft + chain + positions), in numbered steps

Preconditions: the Raft log and the tape are two files (F4). `pos` is the committed index. `h` is leader-computed and travels in the payload. The client is a model and retries.

1. **Client sends** `write` with `(client_id, request_id)`, `facts[]`, `effects[]`, `inverses`, `reversibility`, `judgment_ref`, `signatures[]`, and `basis_pos` (the position the client's reasoning was based on).
2. **Leader check.** If not leader, refuse `not_leader{leader_addr}` — a counter, not a tape entry.
3. **Admission.** Debit the principal's token bucket. On exhaustion, refuse `rate_limited{retry_after_ns}` — a counter, not a tape entry (F11).
4. **Dedup.** If `(client_id, request_id)` is in the replicated reply cache, return the original `pos` with `duplicate_request{original_pos}`. Append nothing (F13).
5. **Validate, against the transactor's own CPU-resident fold** (F1): every touched cell's check expressions; foreign keys; exposure caps for the class and period; reversibility class; inverse present iff required (F2.3); writ quorum signatures verified against registered judge keys for irreversible classes; judge pin and serve pin match the registration; `basis_pos ≤ committed_pos`. On failure, produce a **refusal entry** and continue at step 6 with that entry — a refusal is a write.
6. **Serialize canonically.** Build the entry: `pos = next_index`, `t_mono_ns` from the transactor's single stamping authority, `prev = h(pos-1)`, `h = blake2b256(prev ‖ canonical(header ‖ body))`. `pos` is inside the hashed bytes.
7. **Batch.** Add the entry to the open group-commit batch. If an fsync is in flight, the batch stays open (F9).
8. **Propose.** When the in-flight fsync returns, close the batch and propose all its entries as one Raft `AppendEntries`.
9. **Replicate and fsync.** Leader fsyncs its Raft log; followers fsync theirs and ack. Raft requires the fsync before the ack; do not make it optional.
10. **Commit** on majority ack. `committed_pos` advances to the batch's last index. Nothing before this moment is published to anyone: not the position, not `h`, not the ack. This is the output gate.
11. **Apply, deterministically, on every replica, in index order:** append the entry bytes to the current tape segment; roll to a new segment at 64 MiB, named by first position; advance the transactor's own constraint fold; advance the reply cache.
12. **Tape fsync is asynchronous.** The tape is a deterministic function of the committed log; on restart, replay the log from the tape's end. One synchronous fsync per batch on the critical path, not two.
13. **Ack the client** with `pos`, `h`, and `committed_pos`.
14. **Publish the delta** to subscribers with `pos`, `prev`, `h`, and the entry. Subscribers verify `prev == last_h`, refuse `pos ≤ applied_pos`, and fatal on a gap (F6, F7).
15. **The peer gates its apply** (F8): it accumulates a batch, applies it, runs the field sweep to fixed point, then publishes `applied_pos` atomically. Readers see only published states.
16. **The effector subscribes independently**, from its own durable cursor, and performs effects at-least-once with the tape-derived `idem_key`; it appends `effect_attempt` before the send and `effect_result` after (F3).
17. **On leader change:** the new leader may truncate its Raft log's uncommitted suffix. No truncated entry ever reached the tape, so no position is reused there and no chain forks. The new leader appends its term's no-op, which lands on the tape as a typed `term` entry (F4).
18. **On peer reconnect:** resume at `applied_pos + 1` and verify `prev == last_h`. On mismatch, rebuild from the last fold checkpoint at or below `committed_pos`. Never continue.

**The one property this protocol buys that the blueprint asks for and cannot otherwise have:** the tape is byte-identical on all three replicas and contains only committed entries, so "the chain never forks," "position is the deposit clock, never reused," and "reads at a position are repeatable forever" are all simultaneously true — of the tape. They were never going to be true of a Raft log, and the blueprint currently conflates the two.

---

## 4 · Revised client API

Removed from the client surface: `serialize`, `fork` (peer-internal ABI; see F12). Added: eight. Kept and re-specified: five.

| call | request | response | notes |
|---|---|---|---|
| `subscribe(query, from_pos, cursor_mode)` | standing query over folds or the raw tape | delta stream: `{pos, prev, h, entry}` | at-least-once; client verifies `prev`; own writes return on lane `self` |
| `ack(subscription, pos)` | — | ok | the cursor is committed as **applied**, never as read (`fusord.cpp:575-578`) |
| `unsubscribe(subscription)` | — | ok | releases peer state |
| `query(sql, as_of_pos \| latest, min_pos)` | — | rows + `as_of_pos_served` + `applied_pos` | snapshot isolation at a published position; `min_pos` gives read-your-writes; historical below the version floor is a priced rebuild |
| `write(tx)` | `client_id, request_id, basis_pos, facts[], effects[], reversibility, judgment_ref, signatures[]` | `pos, h, committed_pos` or `refuse(reason)` | one transaction = one entry = one position; pipelined on the connection |
| `hold(cell, margin, reason, judgment_ref)` | — | `pos` | separate path, **unrefusable**, evaluates no constraint (law 2) |
| `admin(entry, signatures[])` | a `rule` or `judge` entry | `pos` or `refuse(reason)` | the missing call: registration and writ change; operator key or constitution quorum only |
| `explain_refusal(pos)` | — | `constraint_id, expression, offending value, repair hint` | so the model repairs instead of looping (F11) |
| `schema(as_of_pos)` | — | classes, templates + pins, horizons, constraints, reversibility classes, this principal's license | the model cannot form a legal write without it (F12) |
| `health()` | — | `leader, committed_pos, applied_pos, lag, warm_ratio, refusals/s, replayed, probe p50/p95, lat p50/p95, rename_failures` | Addendum-F §3.2 makes the pill a contract element; fusord ships 30 fields (`fusord.cpp:824-842`) |
| `verify_fold(fold, from_pos, to_pos)` | — | cold digest, incremental digest, equal? | §2.4 requires the mode; R1's gate needs the call |
| `replay(judge_pin, from_pos, to_pos, mode)` | — | shadow tape id | shadow tapes are peer-local with their own chains; law 3 is per-tape |

**Typed refusal reasons.** Entry-producing (a judgment about the world): `constraint_violated{constraint_id, cell, expr, actual}` · `quorum_insufficient{have, need, dissent[]}` · `license_exceeded{class, band}` · `pin_mismatch{expected, got}` · `no_inverse{fact_index}` · `unknown_cell` · `unknown_class`. Counter-only (a protocol fault): `malformed{field, why}` · `not_leader{leader_addr}` · `no_quorum` · `rate_limited{bucket, retry_after_ns}` · `duplicate_request{original_pos}` · `stale_basis{your_pos, current_pos}` · `too_large{limit}`.

---

## 5 · What is right and must not be changed

- **One serializing writer** is the correct spine, and the ACID reading behind it (`ORG-SOLVER_...md:22`) is correct: isolation is one writer, and the valve is the transaction manager, not a new component.
- **The tape as a distinct, chained artifact beside the store's derived state** is right, and the reason given is right: durability inside a database proves nothing about ordering to a reader outside it.
- **Position as the deposit clock** is the correct clock, and the 32-bit `src_rev` fix is a real defect caught. Keep falsifier 9.
- **The refusal that never silences** is the right law, even though it needs the DoS amendment in F11 — the amendment preserves it rather than weakening it.
- **Every fold has a `verify_fold` mode** (§2.4) is the right discipline; it just needs to be in the API.
- **The determinism laws in §4** — fixed point, canonical bytes, pinned reducers, no wall time, a recorded seed — are the load-bearing decision of the entire design. They are what make a deterministic fault-injection simulator possible, which is the only way any of the R6 claims become checkable. Do not weaken any of the five.
- **`pos_last` = the last `fact`, with judgment positions in the cold side record** (open question 9's proposal) is correct for my slice's reasons: it keeps the cognitive-index key `(cell_id, pos_last, template_pin, judge_pin)` stable across a `hold`, so a hold does not invalidate a read state that is still accurate. Adopt it.
- **fusord's F1 lesson** — the cursor is committed as *ingested*, never as read or pushed, and replay is at-least-once and visible on the tape rather than silent skipping (`fusord.cpp:575-578`) — is the single most valuable thing in the reused code. Carry it into `ack` and never lose it.
- **The idle tick** (`fusord.cpp:2559-2565`) looks like an operational nicety and is actually load-bearing for the take-back window (F3): it is how a subscriber distinguishes "nothing happened" from "I have not caught up." Keep it and document why.

---

## 6 · My top three changes

1. **Split the artifact: the Raft log is internal and truncatable; the tape receives only committed entries** — this is the only construction in which "position is the deposit clock, never reused" and "the chain never forks" are both true, and without it R6 is unbuildable.
2. **Give the transactor its own CPU-resident constraint fold, and stop calling multi-cell writes sagas** — the writ cannot be enforced by a process that holds no state, and the real saga is the effector boundary, where the outbox, the derived idempotency key, and at-least-once delivery belong.
3. **Replace `fflush` with group-commit `fsync` and replace the 10,000/s throughput target with a p99 latency SLO** — the reused `Tape::put` swallows four failure modes and the target invites the one optimization that must never be made.

---

## 7 · Open questions I would add to §10

11. **Where does the transactor's constraint fold live in memory, and what is its recovery time?** It must be rebuilt from a checkpoint plus the tape on every transactor restart, and that rebuild is the store's write outage. Measure it at R0 and put it on the health call.
12. **What is the retained-version floor for `query(as_of_pos)`, and who chooses it?** Fold checkpoint frequency, storage cost per checkpoint, and the price of a historical read below the floor. §4's "repeatable forever" is currently free in the document and expensive in the machine.
13. **When the counterparty has no idempotency support (SMTP, a fax, a phone call), what is the store's verdict for an effect whose result is unknown?** Proposed `unknown_delivery` (F3); the grade fold and the license fold must both handle it, and neither is specified for it.
14. **What is the client's timeout, and what must it do on expiry?** With `(client_id, request_id)` a retry is safe; without a specified timeout the model client will invent one, and it will be shorter than a leader election.
15. **Does the writ quorum survive a Raft leader change mid-collection?** Signatures gathered by one leader for an irreversible write, not yet committed when the leader dies: are they re-presentable to the new leader, or must the collection restart? This is the one place where the two quorums genuinely interact.
16. **What is the peer's maximum acceptable lag behind `committed_pos` before the store refuses to serve judgments from it?** Law 5 says the peer is a cache; a cache with unbounded staleness that emits verbs is worse than no cache. The bound belongs on the health call and in the constraint layer.
17. **How are the transactor's and the peer's folds reconciled when they disagree?** F1's new falsifier detects it. The blueprint has no procedure for what happens next, and "halt" may be the right answer — but it has to be chosen.

---

*Written 2026-09-08 by Claude Opus 5 as QC reviewer 5 of 7. Every `fusord.cpp` line number was read, not inferred. Every precedent named was checked against its own documentation. Nothing in this file is a measurement; the measurements it cites are `C:/IT/RUNG0-SMOKE-RECEIPT-2026-08-23.md` and the receipts the blueprint itself names. Where this file and a receipt disagree, the receipt wins and this file is the defect.*

**Sources for the named precedents:** [TigerBeetle VSR internals](https://github.com/tigerbeetle/tigerbeetle/blob/main/docs/internals/vsr.md) · [Datomic ACID](https://docs.datomic.com/transactions/acid.html) · [Datomic transaction processing](https://docs.datomic.com/transactions/transaction-processing.html) · [Raft (Princeton COS 418)](https://www.cs.princeton.edu/courses/archive/spring21/cos418/docs/L14-raft.pdf) · [Durable Objects: Easy, Fast, Correct](https://blog.cloudflare.com/durable-objects-easy-fast-correct-choose-three/) · [Transactional outbox (microservices.io)](https://microservices.io/patterns/data/transactional-outbox.html) · [Transactional outbox (AWS Prescriptive Guidance)](https://docs.aws.amazon.com/prescriptive-guidance/latest/cloud-design-patterns/transactional-outbox.html) · [fsync performance on storage devices (Percona)](https://www.percona.com/blog/fsync-performance-storage-devices/)
