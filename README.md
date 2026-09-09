# TAPESTRY

**A store for an organization that runs on models: append-only, hash-chained, and built so that no model can author the decision to act.**

The tape is the truth. A hold is a row. One writer. The writ is a constraint, and nothing learned disposes.

Working name. Early rungs — R0.1 and R0.2 of seven are built and receipted; everything after is written down and not yet code.

---

## The four laws

1. **The tape is the truth.** One append-only, hash-chained log. Nothing is ever edited. Every durable structure is a fold over it: integer folds re-derive bit for bit; judged folds re-derive within a stated envelope.
2. **A hold is a row.** The decision *not* to act is recorded, with its margin and its reason, and the tape is never deleted.
3. **One writer.** A single serializing transactor appends. Two hands never sell one seat.
4. **The writ is a constraint, and nothing learned disposes.** Prohibitions, caps, reversibility and quorum rules live in the store, where no model runs. There is no `allow` verb. A judge proposes a *margin*; a deterministic, code-hashed seam disposes it into a verb, and the transactor refuses any verb the seam did not author.

Two corollaries. **The peer is a cache** — everything on the card re-derives from the tape, and losing it costs time, not truth. **The judge is identified, not trusted** — a registered model with a weight hash, a serve pin and keys, whose every margin is stamped and whose verbs it never writes.

---

## Why an organization needs this and a database does not give it

A normal store answers *what is true now*. An organization that delegates decisions to models needs three things a normal store will not give you:

- **The record of what was not done.** Most of what an autonomous system does is decline to act. If holds are absent, the record flatters: you can only audit the actions, and the actions are the minority.
- **A refusal that cannot be silent.** Every refused write is an entry on the tape or is counted inside one. A constraint that drops a write leaves a hole, and a store that permits that hole cannot be audited afterwards.
- **A boundary a model cannot cross.** The gap between "the model scored this 0.8" and "the money moved" has to be a named, hashed, deterministic component — not a prompt, not a policy document, and not the model's own judgment about its own authority.

---

## What is built

| rung | scope | receipt |
|---|---|---|
| **R0.1** | the tape: the v0 wire form, the literal-bytes chain, 64 MiB segments with chained headers, position assignment, the monotone epoch stamp, group commit with a real `fsync`, head recovery, the lossless writer, the verifier, a deterministic generator | [`receipts/R0.1_TAPE-STORE_2026-09-08_OPUS5.md`](receipts/R0.1_TAPE-STORE_2026-09-08_OPUS5.md) |
| **R0.2** | the transactor: the five-step write path, the idempotency reply cache, refusals in two classes with coalescing, derived reversibility, the ingest law, exposure caps as a fold, and TAPESTRY's own constraint expression language | [`receipts/R0.2_THE-TRANSACTOR-AND-THE-WRIT_2026-09-08_OPUS5.md`](receipts/R0.2_THE-TRANSACTOR-AND-THE-WRIT_2026-09-08_OPUS5.md) |

**204 checks, 0 failures.** Every oracle carries a *lie arm*: the same check run against input with a planted defect, which must **fail**. A run is green only when both arms behave, so a check that has quietly become a tautology turns the suite red instead of staying quiet.

Some numbers from the receipts, all measured on one desktop (i9-9900K, Samsung 970 EVO Plus NVMe, MSVC 19.44, `/W4 /WX /fp:strict`):

- Group commit: **p99 3.2 ms at 7,250 entries/s**, single-threaded. Unbatched has the same p50 — `FlushFileBuffers` is a fixed ~2 ms toll, not a per-byte cost — so the batch buys 15× throughput at no latency cost.
- The same run with the flush removed is 27× faster and durable of nothing. That is what a writer that calls `fflush` durable is actually measuring.
- Two cold generations of the same script are **byte-identical**: same digest, same head, across 8 segments and 5,008 rows.
- **200 hard process kills** under load, across two arms: every reopen recovered, 1.87 M rows verified, 0 failures — after the defect in the next section was fixed.

---

## The defect the kill test found

The oracles were green at 88/88 when the kill harness first ran. Then 100 hard kills found this:

A segment file is named by its first position, and a roll opens that file while the batch that will fill it is still pending. A kill in that window leaves a file whose *name* is a position that was **assigned but never published**. Recovery read the name as a position and resumed nine positions past the last committed row — a permanent gap in the ordering clock, in a store whose own rule is that positions are published only after the flush returns.

It hit **12 of 100 kills**. The fix: a trailing segment holding no complete row is the ghost of an uncommitted roll, discarded at open with a `warn` row on the tape; the last complete row is the only authority a position may come from. Both shapes are regression oracles now.

The point is not the bug. The point is that a synthetic test suite at 88/88 did not contain it, and a hundred kills did.

---

## Build and run

Windows, MSVC 2022. No dependencies — the hash, the writer, the expression language and the chain are all in-tree, because the thing that makes the record trustworthy should not be something you install.

```
build\build.cmd test
```

Compiles two oracle binaries and `tapectl` into `bin\`, then runs 204 checks.

```
bin\tapectl gen    --dir tape --entries 5000 --seg-bytes 262144 --batch 16
bin\tapectl verify --dir tape
bin\tapectl status --dir tape        what a reopen would find, without writing
bin\tapectl bench  --dir tape --entries 20000 --batch 16
powershell -File build\killtest.ps1 -Iterations 100
```

The tape is JSON Lines, one entry per line, hashed over the literal on-disk bytes:

```
{"pos":0,"term":0,"t_epoch_ns":1757000000000000000,"k":"seg","cell":0,"cls":0,"by":"system",
 "body":{"first_pos":0,"prev_of_first_entry":"000…"},"prev":"000…","h":"9f2…"}
```

`h = blake2b256(prev_hex ‖ the row's bytes before ,"prev":")`. There is no canonicalization step: the bytes on disk are what is hashed, which is why an independent verifier can check a tape it did not write.

---

## Layout

```
src/core/     BLAKE2b-256, the chain hash, the lossless JSON writer, files that tell the truth about durability
src/tape/     the wire form and its strict scanner, segments, positions, group commit, recovery, the verifier
src/writ/     the constraint expression language and the class map's pins
src/tx/       the 128-byte hot row, the cell table, and the one writer
src/tools/    tapectl
src/tests/    the oracles, each with its lie arm
build/        build.cmd, the kill harness, the cross-check against the estate's own verifier
receipts/     one dated receipt per rung — where a receipt and a document disagree, the receipt wins
qc/           seven independent adversarial reviews of the v0.1 design, and the adjudication between them
```

`TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.2` is the design being built. `v0.1` stands unedited as history: it did not survive its own review, and the diff between them is in `qc/QC-SYNTHESIS_DECISIONS_v0.1-to-v0.2`. Both documents, and the seven reviews, reference paths and measurements on the machine they were written on; they are kept as written rather than tidied, because a receipt that has been cleaned up is not a receipt.

`qc/scratch/` holds the reviewers' probes and their raw outputs. Two things the reviewers copied there are *not* published: the chunked source of `fusord.cpp` and the `osv_*` engine headers, which belong to other codebases and are not this repository's to relicense. The reviews quote them with line numbers where the argument depends on it, and that is the evidence.

---

## Discipline

Append-only, all the way up. Corrections are new files, not edits. Every rung ends in a dated receipt that carries the command and its output, and every claim in a receipt is marked: **[M]** measured here, **[R]** read from source, **[D]** derived with the chain shown, **[SPEC]** a decision rather than a measurement. Where a receipt and a document disagree, the receipt wins and the document is the defect.

Design work here was written by Claude Fable 5.1 and reviewed by seven adversarial passes; the implementation and its receipts are by Claude Opus 5, at the operator's direction. The receipts say which is which, including where building the thing found the specification wrong — twice so far, once by compiling a struct that does not add up to the size it asserts.

MIT licensed.
