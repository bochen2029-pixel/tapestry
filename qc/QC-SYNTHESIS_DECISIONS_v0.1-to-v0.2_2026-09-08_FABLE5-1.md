# QC SYNTHESIS · DECISIONS FROM v0.1 TO v0.2
### What seven reviewers found, where they agreed, where they disagreed, and what the decider chose

**2026-09-08 · Dallas · Claude Fable 5.1 (`claude-fable-5-1`), the `C:\55555` session, as decider.** Inputs: `QC-1` tape and chain (Opus), `QC-2` cell, folds, field, ingest (Opus), `QC-3` cognitive index, judge, sizing (Fable), `QC-4` grades, license, quorum, writ (Opus), `QC-5` transactor, API, replication (Opus), `QC-6` red team (Fable), `QC-7` build, reuse, hygiene (Opus). Output: `C:\TAPESTRY\TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.2_2026-09-08_FABLE5-1.md`. v0.1 stands unedited.

---

## §1 · The verdict on v0.1

It did not survive as written. Four defects were each found independently by three or more reviewers and confirmed against code or a receipt, which makes them certain: the field and the grades had no legal clock, so no fold was a function of the tape; the hot row's one clock replaced the world's clock with the store's clock and deleted the ingest guard; the chain's canonicalization was structurally impossible and the reused writer never synced; and the cognitive index was sized for a model the estate does not run, omitting the 52.7 MB recurrent state that sits on the same line of the same receipt as the constant I used. One defect was found by the red team alone and is the deepest: the judge authored the verb, so the seam had no component and every safety receipt in the corpus lost its home. The three objects that survived every attack are the ones the design exists for: the tape with integer folds verified cold against incremental, the monotone license with per-verb horizons and both-sides evidence, and the hold and the refusal as rows.

---

## §2 · Convergence, by finding

| finding | reviewers | status in v0.2 |
|---|---|---|
| the field's slot depends on a host clock; no fold has a durable time base; horizon expiry is not an event | QC-1 F3b F7, QC-2 F1, QC-4 F1 F14, QC-6 F6 | three clocks; `t_epoch_ns` inside the hash; ticks drive the field, the horizons and the caps; `now` is the entry's stamp |
| `pos_last` is the store's clock, `src_rev` was the world's; the guard is deleted and the 64-byte row is full | QC-1 F4, QC-2 F3 F4, QC-4 F19, QC-6 F9, QC-7 F12 | 128-byte row with `pos_last` any-kind, `src_pos_last`, `pos_judged`; the ingest law as a constraint; eight change sites named |
| the canonicalization rule cannot carry its own hash and does not match fusord; the verifier is byte-level | QC-1 F1 F5 F17, QC-7 F15, QC-6 smaller | v0 hashes the literal prefix in wire order; v1 binary and a chain restart; no compatibility claim |
| under Raft, positions reuse and the chain forks | QC-1 F2, QC-5 F4 | two artifacts; commit protocol in §4.2 and §5 |
| `Tape::put` is fflush; throughput target under the wrong primitive | QC-1 F8, QC-5 F5 F9, QC-7 F7 | group commit; a p99 SLO; return checks; the sync added to the atomic replace |
| the recurrent state is 52.7 MB per sequence; the cap is 256; fork-at-zero measured a metadata fork | QC-1 F10, QC-3 F1 F2 F11, QC-6 F1, QC-7 F1 F4 F8 | §3.5 and §6 re-derived; slot pool; cold by default; falsifier 7 restated; the judge decision is open question 1 |
| the seam is erased; the judge emits the verb; the two-logit margin cannot choose five verbs | QC-6 F2, QC-3 F9, QC-7 F11, QC-4 F2 | §4.6; the judge emits margins only; the transactor refuses non-seam verbs; falsifier 17 |
| reversibility declared by the writer bypasses the warrant law | QC-4 F2, QC-6 F4 | derived from the pinned map; mismatch refused and warned |
| the pin is blind to the operator, the class id and the flags | QC-2 F6, QC-7 F3 | per-class pins with an integer mixer; falsifier 20 |
| the module gate forbids the peer's own sockets | QC-7 F2, QC-6 F7 | the peer is socketless over a named pipe; the transactor holds the network |
| the sweep never terminates at scale; float `dev`; FMA contraction differs by compiler | QC-2 F2 F11, QC-1 F3a F13 | relative tolerance, iteration cap on the snapshot, fixed-point `dev`, fold identity includes the build, contraction pinned |
| the field is not differential; the lattice rots between appends | QC-2 F1 F12 F13, QC-5 F21 | differential projection; deadline wheel; sweep per applied batch |
| the three ingest defects are real and invisible to conservation | QC-2 F8, QC-7 F12 | join paths, reducers, back-fill as its own fact, qualified columns, completeness assertion; falsifier 19 |
| fetch has no horizon, cost or cap; escalate needs two horizons; `grade` is both a kind and an output | QC-4 F1 F8 F12, QC-2 F9 F10, QC-6 F3 | seven horizons; grades as fold output only; five verdicts; sources tagged |
| kappa cannot demote an acting class; demotion absorbing; reason erased; cross-class starvation | QC-4 F3 | act cost from the wire; removed split; trailing window; `demoted` reason; separate pool |
| the jury is unconvenable and unmeasured; registration ungated; signatures bind nothing | QC-4 F4 F5 F6 F15, QC-6 F4 | human planet stated; human signature on irreversibles; judge quorum as the second key on reversible classes; registration gated; independence fold; the preimage; dissent kind |
| the lottery has no seed discipline | QC-4 F7 | commit-reveal; falsifier 12 |
| the license's monotone property cannot be a fold alone | QC-4 F9 | `MEET` and `MAX` in the fold; the widening gate in the transactor |
| the persistence clause is undefinable as written | QC-4 F13, QC-6 F10 | persistence classes with floors and caps; unmodelled cost holds |
| sagas conflated with transactions; effects have no delivery semantics | QC-5 F2 F3, QC-6 F8 | one transaction one entry; the outbox on the tape; derived idempotency keys; at-least-once |
| the transactor holds no state and cannot validate | QC-5 F1 | its own CPU fold; falsifier 11 |
| the API lacks registration, rules, ack, schema, health, verify, explain; serialize and fork are internal | QC-5 F12 F13 | twelve calls; idempotency keys |
| refusals as entries are a denial of service | QC-5 F11 | two refusal classes; coalescing; a refusal budget |
| shadow tapes bypass the single writer | QC-4 F17 | chained through the transactor |
| the index key omits the root pin and names one cell for a neighborhood | QC-3 F6, QC-2 F7 | key carries `root_pin` and `render_digest`; `stale_judgment` refusal |
| per-cell NVMe checkpoints duplicate the root | QC-3 F5 | root-only checkpoint |
| the serve pin value and seat vocabulary cannot transfer | QC-3 F8 | mechanism transfers; value retuned |
| "years of tape in hours" contradicts the sizing by three orders | QC-5 F15, QC-6 F5, QC-7 F10 | about 2,800 card-hours a year serial; replay throughput is R4's receipt |
| injection through templates; the judge sees its report card | QC-4 F18, QC-6 F3 | the delimited region; forbidden template contents; falsifier 16 |
| falsifiers that cannot fail: 3, 5, 7; twelve module oracles without lie arms | QC-1 F9 F14, QC-7 F8 F9 F13 | all restated; lie arms at R1 |
| terminology collisions and glossary gaps | QC-7 §8 | §13 rewritten |

---

## §3 · Where reviewers disagreed, and what I chose

1. **`pos_last` as the last fact or the last entry of any kind.** QC-5 wanted the last fact so a hold would not invalidate a still-accurate read state; QC-4 needed any-kind for the compare-and-swap in the quorum preimage. QC-2's content-addressed index removes QC-5's reason. **Chosen:** any kind on the hot row; the index is keyed on the rendered bytes' digest.
2. **The hot row at 64 or 128 bytes.** QC-2 defended the 64-byte code block; QC-4 and QC-6 showed three clocks cannot fit. **Chosen:** 128 bytes; 50,000 cells is 6.4 MB and the sizing does not care.
3. **The process shape.** QC-5 argued two processes over a frame socket; QC-7 showed the peer's gate forbids any socket on this platform; QC-3 said keep the gate. **Chosen:** two processes, the peer socketless over a named pipe or shared memory, the transactor holding the network.
4. **Full recompute per batch or differential.** QC-5 and QC-7 said full recompute per batch suffices for v0; QC-2 measured the differential update bit-identical and fifty thousand times cheaper on the dominant cost. **Chosen:** differential projection, because it satisfies verify-fold by construction; the sweep per batch.
5. **The baseline.** QC-2 offered add a fold or delete the pointer. **Chosen:** a fixed-point EMA fold with a ratified time constant, zero until it has history, shipped at R1 and declared inert until then.
6. **Grades as a kind or a fold output.** QC-4 said fold output only; QC-6 asked for source tags. **Chosen:** fold output with sources; the kind is deleted; outcomes arrive as facts and effector rows.
7. **A hold as a separate unrefusable call, versus the seam's stale-judgment refusal.** QC-5 wanted the hold unrefusable; QC-3 wanted a stale judgment refused. **Chosen:** holds are unrefusable on constraints and recorded as late when stale, as fusord records deferred judgments; stale verbs that act are refused.
8. **Demotion absorbing or not.** QC-4 showed it is absorbing by arithmetic. **Chosen:** not absorbing; a trailing window on confirmed outcomes with a ratified re-licensing criterion.
9. **Which planet.** QC-6 showed the blueprint mixed two. **Chosen:** the human planet in the body, the substitutions in Appendix A.
10. **The judge architecture.** QC-3 and QC-7 both raised it; neither decided. **Chosen:** open question 1, measured on two candidates before R3; v0 stays on the hybrid with the slot pool.
11. **Canonical JSON per RFC 8785, or literal bytes.** QC-1 offered three routes. **Chosen:** literal bytes in wire order for v0, which is what every existing receipt certifies, and binary for v1 with a chain restart.
12. **Quorum rule.** QC-4 showed N-of-M plus a dissent is not what falsifier 8 tests. **Chosen:** N approvals and zero dissents and an abstain ceiling, with dissent as a row.
13. **The laws.** QC-6 showed two of six were corollaries and one a category error. **Chosen:** four laws and two corollaries.
14. **DuckDB.** QC-7 proposed it and claimed it made the differential question moot; QC-2's measurement says the lattice sweep, not the scan, is the cost. **Chosen:** DuckDB for the constraint language and queries with threads pinned to one and float aggregates forbidden in folds; the field stays differential.

---

## §4 · What I declined, with the reason

- QC-1's RFC 8785 route: declined for v0, because the chain receipts certify literal bytes and the verifier is byte-level; adopted in spirit as the v1 binary form.
- QC-5's preference for `pos_last` as the last fact: declined, superseded by content addressing.
- QC-2's keep-the-64-byte-row: declined, three clocks.
- QC-7's "full recompute is moot with DuckDB": declined for the field, adopted for queries.
- QC-4's unanimity-only quorum: adopted with an abstain ceiling so an offline juror is distinguishable from a silent one.
- QC-3's suggestion to size v0 to the 16 GB card's few dozen warm cells as the product: adopted as the honest statement of what this card does, with the H200 row kept as a budget.
- QC-6's demand to drop the phrase "the tape, as always, is the proof" as unfalsifiable: declined; it is the house sign-off and no falsifier hangs on it.

---

## §5 · Receipts the reviewers produced

Under `C:\TAPESTRY\qc\scratch\`: `tape/probe.py`, `probe2.py`, `probe3.py` for canonicalization, FMA divergence and the verifier's seam blindness; `cell/` with `qc_probe`, `qc_field`, `qc_conv`, `qc_diff` binaries and the three compiler-flag builds of the core tests, plus the `swap/` build enumerating every change site; `license/qc4_probe.cpp` with six probes against dispatch; `build/` with both test suites at 15 of 15 and `cell_check`, `pin_check`; `ci/` with `bench_pps_npl{8,64,128,300}.txt`, `bench_probe_prefill.txt`, `ckpt_fit.py`, `gguf_hparams.py`; `red/gguf_keys_and_sizing.py`. Nothing outside `C:\TAPESTRY\qc\` was modified by any reviewer.

Measured this session and now load-bearing in v0.2: the recurrent slot at 50.25 MiB and its allocation at boot; the sequence cap at 256; prefill not batching on this card; the checkpoint fit to zero residual; the pin blind to three fields; the sweep parked at one ULP; the differential update bit-identical; both ingest guards dead above 2^32; the FMA divergence at 22 percent; the rename hazard; the verifier accepting a fork at genesis.

---

## §6 · What remains unmeasured after v0.2

The judge architecture on both candidates. The noise floor of a margin. The invalidation rate on a real wire. The maximum sequence count a recompiled build accepts. The replay throughput. The peer's fold against the transactor's fold at a commit boundary. The horizon distributions for hold, which need the stratum. Every H200 number in §6 of the blueprint.

---

*Written 2026-09-08 by Claude Fable 5.1 as decider. Where this file and a reviewer's receipt disagree, the receipt wins and this file is the defect.*
