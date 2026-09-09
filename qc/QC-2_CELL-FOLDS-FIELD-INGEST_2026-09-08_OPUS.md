# QC-2 · THE CELL, THE CLASS MAP, THE FOLDS, THE FIELD, INGEST
### Adversarial review of `TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1_2026-09-08_FABLE5-1.md`, §2.2 · §2.3 · §2.4 · §3.9 · §5 · falsifiers 2 and 10 · the `osv_core.cuh` / `osv_ingest.h` reuse claims in §11 · open questions 6, 8, 9

**2026-09-08 · Claude Opus 5 (`claude-opus-5[1m]`), reviewer 2 of 7. Register: every CONFIRMED finding below carries a compiled receipt from `C:/TAPESTRY/qc/scratch/cell/`. PLAUSIBLE means reasoning only. Where this file and a receipt disagree, the receipt wins.**

**Build environment for every receipt:** MSVC 19.44 (`cl` 14.44.35207, `/O2 /std:c++17 /EHsc`), Intel i9-9900K, Windows 11. **No `g++` and no `clang++` exist on this box** (`g++ --version` and `clang++ --version` both `command not found` in Git Bash; `cl` is not on PATH either — it is reached through `vcvars64.bat`). The task's `g++ -O2 -std=c++17` line could not be run verbatim; MSVC was substituted and the sources compiled unmodified. `nvcc` 13.1 is present but was not used: there is no device build of the test.

**Baseline, before any of my changes:** `osv_core_test.cpp` compiled clean and ran **7 / 7 PASS**, exit 0. All seven core falsifiers hold as shipped.

---

## 1 · Verdict

The cell row's byte arithmetic works only because the blueprint's code block silently reorders the struct — the prose sentence beside it describes a 72-byte object, and the swap it proposes deletes the ingest monotonicity guard without putting a replacement anywhere in the data model. The field fold is not a fold: `slot_of` takes `now`, `now` is nowhere on the tape, and the sweep's own stopping rule provably never fires at the blueprint's own sizing — I ran it 200,000 iterations and the max move parked at exactly one float ULP, forever, while the same system in double converged in 12. The three ingest defects §11 promises to fix are all real and all confirmed by receipt, but they are worse than described: each one leaves the ledger *conserving the wrong number*, so not one of the fifteen carried-forward falsifiers can catch any of them.

---

## 2 · Findings, most severe first

---

### F1 · The field fold reads a clock that is not on the tape — BLOCKER · CONFIRMED

**§2.4** (field fold, "maintained on every append") against **§4** ("no wall time inside any template or fold") and **falsifier 1** ("two cold rebuilds from one tape are bit-identical in every fold").

**The claim.** The field is a deterministic fold over the tape, re-derivable, and two cold rebuilds are bit-identical.

**The defect.** The lattice slot of a cell is `slot_of(d, now_ns, due_ns)` — `osv_core.cuh:151-156` — and `project_one` (`:159-164`) passes `now_ns` straight through. `now` is not an entry field, not a fold input, and not mentioned in §2.4's input column ("the cell table"). The field fold is therefore a function of `(tape, now)`, not of the tape. Two cold rebuilds at different times produce different lattices, and falsifier 1 cannot pass — not "might fail on a float", *cannot pass*, by the fold's signature.

**Evidence.** `C:/TAPESTRY/qc/scratch/cell/qc_field.cpp`, probe [7]: the same 50,000-commitment ledger projected and relaxed at `now` and at `now + 60 s` →

```
[7] same tape, 'now' 60 s apart -> identical field: NO
```

The predecessor already solved this and the blueprint dropped it: `ORG-SOLVER_THE-IRREDUCIBLE-OPERATIONS`, §2, line 67 — "**Ticks, idle time.** Ingest of a synthetic percept." The blueprint even lists `tick` as a kind in §2.1 and then never connects it to anything.

**The second half of the same hole.** §2.4 says the field is maintained "on every append." When time passes with no append, nothing happens, and the lattice's picture of urgency rots. Measured on the estate's own sizing (`qc_field.cpp` [5], [6]):

| idle time, zero appends | commitments whose lattice slot is now wrong |
|---|---|
| 1 h | 1,102 of 50,000 (2.2%) — 70 of them newly **overdue**, i.e. should be slot 0, maximum pressure |
| 4 h | 1,353 (2.7%) |
| 8 h | 1,691 (3.4%) |
| 16 h | 2,413 (4.8%) |
| 24 h | 3,066 (6.1%) |

`NSLOT = 16` at one-hour slots is a **16-hour horizon**. A weekend with no append saturates every slot index and slot 0 — "overdue is maximum pressure, never negative time" — never fires. The pressure term is an additive input to `gate` (`osv_core.cuh:260`), so a stale field changes verbs; `qc_field` [5] measured `max |dev_after − dev_before| = 21.26` across a single idle hour on a field whose act threshold is 1.0.

**Fix.** Three sentences into §2.4 and §4:
1. `now` for every fold is the `t_mono_ns` of the entry being folded. Never a host clock read. This makes the field a fold again and costs nothing.
2. The transactor appends a `tick` entry on a fixed cadence — **`slot_ns / 4`**, so slot error stays under a quarter slot — *plus* an event-driven tick scheduled at `min(due_ns)` over open cells, so the instant a deadline passes is a tape position. A deadline wheel over `due_ns` makes that scheduling O(1).
3. State the cadence as a `rule`-entry parameter, not a constant, so a change is a re-pin.

Falsifier to add: **the clock that is a row.** Replay a tape whose only entries for six hours are ticks; the field at each tick is bit-identical to a cold rebuild to that position. Planted lie: a fold that reads the host clock.

---

### F2 · "Sweep kernel to tolerance" does not terminate at the blueprint's own sizing — BLOCKER · CONFIRMED

**§2.4**, field fold, "maintained: on every append; **sweep kernel to tolerance**." **§11** claims `step_cell`, `sweep_segment` are reused **unchanged**.

**The defect.** `Lattice::tol` defaults to `1e-4f` (`osv_core.cuh:137`) and is an **absolute** tolerance on a **float** `dev`. At the page's sizing — 50,000 commitments, amounts 1–11, projected into the tests' own 4 × 12 × 16 lattice with capacity 200–320 — `|dev|` reaches **3,676**, where one float ULP is **2.441e-4**, larger than `tol`. The iteration converges numerically in eight sweeps and then the termination test can never be satisfied.

**Evidence.** `C:/TAPESTRY/qc/scratch/cell/qc_conv.cpp`:

```
source term src=load-capacity : min -320.0  max 4585.3  mean|src| 535.7
 iter        max move      max|dev|
      8      3.003e-02      3676.12
     16      2.441e-04      3676.12
   1000      2.441e-04      3676.12
 200000      2.441e-04      3676.12
  |dev| max = 3676.12   1 ULP = 2.441e-04   tol = 1.000e-04   ratio tol/ULP = 0.41
  SAME system in DOUBLE: converged to 1e-4 in 12 iterations
```

200,000 iterations, max move pinned at exactly one ULP. The stencil is fine — the same system in `double` clears 1e-4 in **12** iterations. This is precision, not algebra.

**Why no existing falsifier catches it.** `o_step` (`osv_core_test.cpp:182-186`) carries `if (mx <= G.L.tol) break;` and **that break has never once executed**. Receipt, `qc_diff.cpp` probe (1): at `o_step`'s own scale, `|dev|max = 2.913`, 1 ULP there is `2.384e-07`, `tol` is `1e-7`, final max move `2.384e-07` — `break fired at iteration: NEVER (ran the full 4000)`. The test passes because its oracle is a *tolerance* comparison against a double-precision Jacobi reference (`mx < 1e-3`), which is insensitive to the stopping rule. The shipped suite ships a convergence test it has never run.

**Second consequence, in my slice.** `step_cell:208` sets `F_DIRTY` when `moved > tol`. With `moved` parked at 1 ULP, the dirty bit is a function of *magnitude*, not of movement. Measured (`qc_diff` (2)): after 200 sweeps at the 50k sizing, **30 of 768 cells are permanently dirty** — and they are precisely the highest-`|dev|` cells. §2.5 evicts the cognitive index "by the field's ranking." The ranking's dirty signal is inverted: the busiest cells report "always changing" whether or not they moved.

**Fix.** (a) Relative tolerance: `tol * max(1.0f, ||dev||_inf)`. (b) A hard iteration cap recorded as a `rule` parameter and stamped on the fold's `ckpt`, so "to tolerance" is a bounded, tape-determined procedure rather than a `while`. (c) Promote `dev` to `double`, or to fixed point like `load_fix` — the field is the input to a threshold and 24 bits of mantissa at |3,676| is 2.4e-4 of resolution on a margin whose act threshold is 1.0.

---

### F3 · The `pos_last` swap deletes the ingest monotonicity guard and puts nothing back — BLOCKER · CONFIRMED

**§2.2** ("the 32-bit `src_rev` … become a 64-bit `pos_last`") and **§11** ("`osv_ingest.h` … with 64-bit positions").

**The defect.** `src_rev` and `pos_last` are **two different clocks**. `src_rev` is the *source system's* LSN (`SourceRow::rev`, `osv_ingest.h:85` — "LSN / commit sequence — the deposit clock, monotone per source"). `pos_last` is TAPESTRY's *own* tape position (§2.2: "the tape position that last touched this cell"). The monotonicity law — `osv_ingest.h:315-319`, "a row from the past is REFUSED — never merged, because merging it would silently rewrite the present with a stale fact and nothing downstream could tell" — compares the incoming **source LSN** against the stored one. Replace the field with a tape position and that comparison has nothing to read: tape positions are assigned by the transactor and are monotone by construction, so the guard becomes a tautology and every stale CDC row merges silently. §2.2's cold side record lists "the tape span `[pos_open, pos_last]`, the cognitive index handle, the class template pin, and per-verb grade counters" — **no source LSN anywhere in the data model.**

**Evidence.** I applied the blueprint's §2.2 struct in place (`C:/TAPESTRY/qc/scratch/cell/swap/`) and compiled. Every site the compiler names is a site that reads the *source* clock:

```
osv_ingest.h(284)  c.src_rev = (uint32_t)r.rev;        <- fill(): stores the SOURCE LSN
osv_ingest.h(317)  if (r.rev == cur->src_rev)          <- idempotence guard
osv_ingest.h(318)  if (r.rev <  cur->src_rev)          <- monotonicity guard
osv_ingest.h(337)  cur->src_rev = (uint32_t)r.rev;     <- on close
```

**How bad the existing truncation actually is — worse than falsifier 9 says.** Falsifier 9 ("the clock that does not truncate") frames this as stale rows and replays. Receipt `qc_probe.cpp` probe P3 shows **both guards die completely** above 2^32, because `r.rev` is `uint64_t` and `cur->src_rev` is `uint32_t`, so the truncated value is *promoted* and every row above 2^32 compares as strictly newer:

```
applied rev = 4294967796   stored src_rev = 500  (truncated: YES)
exact replay  -> duplicate counter moved by 0   (law: must be 1)
stale row     -> stale_refused moved by 0       (law: must be 1)
updated counter moved by 2                      (law: must be 0)
```

Idempotence *and* monotonicity, both silently off. Note `Ledger::high_rev` is already `uint64_t` (`osv_ingest.h:208`, compared correctly at `:290`) — so the counters look healthy while the per-cell guard is dead. That is why this has never surfaced.

**Fix.** Both clocks must exist and the blueprint must say so.
- Hot row: `pos_last` (uint64, tape position) — as proposed.
- Cold side record: `src_pos_last` (uint64, the source LSN). One per cell suffices, because `id = fnv1a(source, table, key)` binds a cell to exactly one `(source, table)`.
- §2.1's `fact` body **already carries `source_pos`**. Make it load-bearing: add a native constraint in §3.6 — *a `fact` whose `source_pos` is not strictly greater than the cell's `src_pos_last` is refused with reason `stale_source`, and a `fact` whose `source_pos` equals it is a no-op counted as `duplicate`*. That moves the ingest law from a header comment into the writ, where §0 law 4 says it belongs.

---

### F4 · §2.2's prose describes a 72-byte struct; only the code block is 64 — MAJOR · CONFIRMED

**§2.2**: "the 32-bit `src_rev` and the 4 padding bytes become a 64-bit `pos_last`."

**The defect.** They are not adjacent, so they cannot "become" one field in place. Measured offsets (`qc_probe.cpp` P1):

```
sizeof(Commitment) = 64  alignof = 8
  ... seat=48 src_rev=52 state=56 flags=57 verb=58 gear=59
  last named byte ends at 60 -> TRAILING PADDING = 4 bytes at [60..63]
  src_rev occupies [52..55]; the padding is at [60..63]. They are NOT adjacent.

sizeof(CellNaive) = 72     (pos_last left where src_rev was, no reorder)

sizeof(Cell) = 64  alignof = 8     (the blueprint's code block, with its reorder)
  id=0 opened_ns=8 due_ns=16 blocked_by=24 pos_last=32 amount=40 margin=44
  cls=48 seg=52 seat=56 state=60 flags=61 verb=62 gear=63
  TRAILING PADDING = 0 bytes;  pos_last 8-byte aligned at offset 32: OK
```

So: **the answer to the byte-arithmetic question is yes, but only for the code block.** 64 bytes exactly, 8-byte alignment satisfied, and — unremarked by the blueprint — **zero padding**, where `Commitment` carries four trailing pad bytes.

**Two things the blueprint must add.**

*(i) The reorder is not cosmetic.* `amount 32→40, margin 36→44, cls 40→48, seg 44→52, seat 48→56`. Every consumer that reads the row as raw bytes changes: `Ledger::digest` (`osv_ingest.h:221-231`) walks all `sizeof(Commitment)` bytes, so **every digest value changes** and any stored digest from before the swap is void. This site produces **no compiler error** and is the one real trap in the change.

*(ii) The swap removes a live nondeterminism the blueprint should claim.* `Ledger::digest` hashes the padding. Receipt `qc_probe` P2 — two records identical in every named field, differing only in byte 60 (pure padding):

```
every NAMED field identical: yes
digest A = 2884680215261607669
digest B = 14256426965640564334
digests differ on a padding byte alone: YES
```

`Cell` has no padding at all, so the class of bug where an uninitialized or non-copied pad byte moves a digest disappears. That is a genuine argument for the change and §2.2 does not make it.

**Every site that changes — the complete list, enumerated by the compiler, not by grep.** Applying the swap and building both test binaries:

| file:line | site | caught by the compiler? |
|---|---|---|
| `osv_core.cuh:95` | the field declaration | n/a |
| `osv_ingest.h:284` | `Ingest::fill` — `c.src_rev = (uint32_t)r.rev` | yes, C2039 |
| `osv_ingest.h:317` | idempotence check | yes, C2039 |
| `osv_ingest.h:318` | monotonicity check | yes, C2039 |
| `osv_ingest.h:337` | close path | yes, C2039 |
| `osv_dispatch.h:276` | `VerbRow{… w.c->src_rev}` | yes, C2039 |
| `osv_core_test.cpp:72` | `c.src_rev = 1` | yes, C2039 |
| `osv_dispatch_test.cpp:48` | `c.src_rev = (uint32_t)i` | yes, C2039 |
| **`osv_dispatch.h:95`** | **`VerbRow::src_rev` stays `uint32_t`** | **yes — C2397, verified** |
| **`osv_ingest.h:221-231`** | **`Ledger::digest` raw-byte walk** | **NO — silent** |

I checked the `VerbRow` trap specifically, because a 32-bit field on the *tape row* would reintroduce the exact truncation one layer down. It is caught: `VerbRow{…}` is list-initialization, so narrowing is ill-formed — `osv_dispatch.h(276): error C2397: conversion from 'const uint64_t' to 'uint32_t' requires a narrowing conversion`. Good news, worth stating in §11: **the change is mechanically discoverable except for `Ledger::digest`.** `VerbRow` grows from 48 to 56 bytes; it is not size-asserted and nothing depends on that.

---

### F5 · The field fold's declared input cannot produce two of its declared outputs — MAJOR · CONFIRMED

**§2.4**, field row: input **"the cell table"**; output "the lattice … load in fixed point, **capacity**, **baseline**, deviation."

- **`capacity`** is what the seats assigned to a lattice cell can discharge per slot (`osv_core.cuh:133`). It comes from `SeatPool::per_period` and `WarrantPool::minutes_per_period` (`osv_dispatch.h:106-118`), which are *not* in the cell table. The cell row carries `seat` (an id), not a rate. The field fold as specified cannot compute its own source term.
- **`baseline`** has **no producer anywhere in the shipped code.** It is `nullptr` in `Lattice`, zero in every test allocator (`osv_core_test.cpp:30`), and no fold in §2.4 emits it.

**Why that matters beyond a missing input.** `dev` is defined as `pressure − baseline` and the module's stated law is "deviation-from-baseline storage — a commitment behaving like the baseline costs nothing" (`osv_core.cuh:33`, `:105-109`), which is the entire justification for storing the residual rather than the level. With `baseline ≡ 0`, `dev` **is** `pressure`, the sparsity is zero, and the law is decorative. It is also unfalsified: no falsifier in `osv_core_test.cpp` or in §9 touches `baseline`.

**Fix.** Either (a) add a **baseline fold** to §2.4 with its own row — input, output, clock, and the rule for how slowly it moves, since a baseline that tracks the field instantly makes `dev` identically zero and a baseline that never moves makes it the level; or (b) delete `baseline` from the design and say plainly that the lattice stores the level, and strike the deviation law from the reuse claim in §11. Do not ship it as an unfed pointer. Same for `capacity`: name the seat-capacity table as a second declared input to the field fold.

---

### F6 · The pin is blind to three things that change the ledger, and its granularity is map-wide — MAJOR · CONFIRMED

**§2.3**: "The map's pin (`SchemaMap::pin`) is extended to cover horizons and templates. A change to the pin is a `rule` entry, and every cognitive index entry built under the old pin is invalidated."

**Defect (a): the pin does not cover what it must.** `SchemaMap::pin()` (`osv_ingest.h:165-179`) mixes only `std::string` fields. It never mixes `Pred::op`, `ClassMap::cls`, or `ClassMap::flags`. Receipt, `qc_probe.cpp` P4:

```
open_when.op P_IN vs P_NOT_IN   -> pins equal: YES  *** BLIND ***   (opposite ledgers!)
cls 0 vs 9 (the lattice index)  -> pins equal: YES  *** BLIND ***
flags +F_WARRANT (a human signs)-> pins equal: YES  *** BLIND ***
due_col changed (control)       -> pins equal: no   (pin does move here)
```

Flipping `P_IN` to `P_NOT_IN` inverts which rows open an obligation and **the pin does not move**. Changing `cls` re-indexes the whole lattice and the pin does not move. Adding `F_WARRANT` — the flag that makes `gate` return `V_ESCALATE` unconditionally (`osv_core.cuh:255`), the difference between "a machine may act" and "a human signs" — and the pin does not move. §2.3 says "extended to cover horizons and templates"; it must first be **corrected to cover what it already claims to cover**. `mix` takes a `const std::string&`, so this is not an oversight that fixes itself — it needs an integer mixer.

**Defect (b): the granularity is wrong.** `pin()` folds every class into one `uint64_t`, and §2.3 invalidates *every* cognitive index entry on any change. Receipt P4: adding an unrelated second class changes the map pin. Cost at §5's own sizing: ~15,000 warm cells × 8.5 MB ≈ **127 GB of KV** discarded and ~**7.5 M tokens** of re-prefill, because someone edited a `due_col` in a class those cells do not belong to. §6 acknowledges the cost ("mass invalidation, staged by ranking; the cost is printed") and then keeps the granularity — printing a bill is not a design.

**Fix — what a class-scoped pin looks like.** Three pins, not one:

1. `class_pin[c]` = hash of class `c`'s own fields *including* `open_when.op`, `close_when.op`, `cls`, `flags`, and the new horizon block.
2. `template_pin[c]` = hash of class `c`'s template source, minted separately, because a template edit must not invalidate the *ledger*'s identity and a map edit must not necessarily invalidate the *render*.
3. `map_pin` = hash of the ordered vector of `class_pin[c]`, used only where the whole ledger's identity is the object — the train-equals-serve assertion of `ORG-SOLVER_RECONCILIATION` §7, which names `SchemaMap::pin` as the third leg at organization radius.

The cognitive index key already carries `template_pin` per entry (§2.5), so per-class invalidation is nearly free: invalidate only entries whose class's `template_pin` moved. A `rule` entry that changes one class's horizon then costs zero KV.

---

### F7 · The cognitive index key is not a function of the bytes it names — MAJOR · CONFIRMED (by code and spec read)

**§2.5**: key `(cell_id, pos_last, template_pin, judge_pin)`; value = the judge's read state of **"the cell's rendered context"**; "invalidated when the cell **or any cell in its rendered neighborhood** is touched."

**The defect.** The value depends on the neighborhood; the key names only the cell. Touch a neighbor and the rendered bytes change while the key does not. Correctness then rests entirely on the invalidation walk being exhaustive and never racing — a cache whose key does not determine its value, guarded by a side channel. The brainstorm is explicit that the render reaches beyond the row: "renders a cell and its neighborhood into canonical bytes: the row, its joins, its contract's definition of done, **its recent tape**" (`TAPESTRY_…BRAINSTORM`, line 46).

**Falsifier 6 cannot catch this.** "Evict and rebuild **a cell's** cognitive index; the next margin matches the un-evicted control." It exercises one cell. A neighborhood-staleness bug survives it by construction.

**Interaction with open question 9.** OQ9 proposes `pos_last` = the last *`fact`*, with judgment positions in the cold record. For the index key that is the right choice **for the cell itself** — two judgments at the same `pos_last` should hit — and it makes the neighborhood problem strictly worse, because the neighborhood's positions are not in the key at all. It also opens a second stale path: after a `tick` (F1), the cell's *slot* changes with no `fact`, so anything time-derived in the render changes with `pos_last` frozen.

**Fix.**
- Key on `(render_digest, template_pin, judge_pin)` where `render_digest` is the BLAKE2b of the canonical rendered bytes themselves, with `cell_id` kept only as a secondary index for eviction. The index becomes content-addressed, invalidation becomes a *performance* mechanism rather than a correctness one, and §2.5's "the index is a function" stops being a hope.
- Tighten §2.3's template rule. It currently says the template is "forbidden from including wall time." That is too narrow: `due_ns − now` is not wall time and a template author will render "due in 3 days" without thinking. State it as: **the template may render `due_ns` absolutely; it may render no quantity computed against `now`, and no pressure, slot, or field value.**
- Restate falsifier 6's planted lie as **"a template that renders time-to-deadline"** and add a second: **touch a neighbor, do not touch the cell, and the margin must move.**

**One thing here is right and must not be softened:** excluding pressure from the template is correct, not an oversight. `ORG-SOLVER_RECONCILIATION` §1 line 10 — "Pressure enters `gate` as an additive term and never touches `probe_one`." The seam law depends on it. Do not "enrich" the template with the field.

---

### F8 · The three ingest defects are real, and the blueprint's one-clause fix under-specifies all three — MAJOR · CONFIRMED

**§11**: "`osv_ingest.h` · `SchemaMap`, `Pred`, `Ingest::apply` | the class map with horizons and templates; the fact-entry producer, with 64-bit positions and **back-fill on late enrichment**." That clause is the entire specification for three distinct defects. Each is confirmed by receipt (`qc_probe.cpp` P5) and each needs its own field in the class map.

**(a) A side-table row arriving after its class row is never applied. CONFIRMED.**

```
after class row           : amount = 0.00  seat = 0
after LATE payments+items : amount = 0.00  seat = 0   enriched rows = 2
```

`Ingest::apply` branch (a), `osv_ingest.h:294-301`, caches the row and `return false` — "enrichment alone never opens or closes an obligation." There is no path that revisits an existing commitment. In a real Olist CDC stream the order insert precedes its items and payments, so **this is the normal case, not the edge**: `amount` stays 0 until some unrelated order-row update happens to re-run `fill`.

*What the class map must specify:* a `back_fill` declaration per `Enrich` naming (i) which of the class's columns that side table may supply, and (ii) the **revision the back-fill writes**. That second half is the part the blueprint's clause hides, and it is a genuine design decision, not an implementation detail: if the back-fill sets the cell's revision to the side row's LSN, a legitimate class-row update with an intermediate LSN is later refused as stale; if it leaves the revision alone, the back-fill is invisible to idempotence and replays twice. Under F3's fix the answer is clean — the back-fill emits its own `fact` entry with the side row's `source_pos`, and the per-cell `src_pos_last` guard applies to it like any other. Say so.

**(b) `seller_state` is a two-hop join the one-hop enrichment cannot perform, and `seg` stays zero. CONFIRMED.**

```
all side rows arrived FIRST, then the class row:
  amount = 77.00 (payments, one hop: works)   seat = 388706410 (items, one hop: works)
  seg    = 0  <- seller_state, TWO hops (order -> items.seller_id -> sellers)
  sellers table counted UNMAPPED: 1   named: olist.olist_sellers_dataset
```

`Ingest::resolve` (`osv_ingest.h:262-273`) keys **every** side table by `r.key` — the class row's own key, the order id. `olist_sellers_dataset` is keyed by `seller_id`. No lookup can ever hit. The map's own comment admits it and ships anyway: `c.segment_col = "seller_state"; // via sellers enrichment (keyed through items)` (`osv_ingest.h:402`), with no `Enrich` entry for sellers in the list at `:404-405`.

*The dangerous half is the silence.* The sellers table is counted and **named** loudly, which is the module's law working correctly. But `seg` is never assigned — `resolve` returns `nullptr` and `fill:281` simply does not fire — so it keeps the `memset` zero. `project_one` then computes `seg % NSEG = 0` for **all 50,000 cells**. The lattice's entire segment dimension collapses to one tile; capacity in segments 1–3 is unreachable; and because segments share no stencil edge (`osv_core.cuh:183-184`), three quarters of the field relaxes to an empty solution. **Conservation still holds exactly**, so falsifier 2 passes. Nothing catches it.

*What the class map must specify:* an explicit join path, not a flat `Enrich` list. Minimum shape — `Enrich{table, key_col, via_table, via_col}`, so `sellers` is declared as *keyed by `seller_id`, reached through `olist_order_items_dataset.seller_id`*. And a **completeness assertion per class**: every column named in `opened_col / due_col / amount_col / seat_col / segment_col` must be resolvable from the class table or a declared join, checked at map-load time and refused as a `rule` violation if not. A column the map names and can never resolve is exactly as loud a defect as an unmapped table, and today it is exactly as silent.

**(c) One-to-many side tables reduce to last-row-wins. CONFIRMED.**

```
3 payment rows 100.00 + 50.00 + 3.50  (true order value 153.50)
  c.amount = 3.50  <- the LAST row only
2 item rows SELLER_P then SELLER_Q -> c.seat = stable_u32("SELLER_Q")
  amount is understated by 150.00 on every multi-payment order
```

`side[r.table][*k] = r.fields;` (`osv_ingest.h:298`) is an assignment. Olist orders carry N payment rows keyed by `payment_sequential` and N item rows keyed by `order_item_id`. Every multi-payment order's `amount` is the last installment, not the total. Again: conservation holds — the ledger conserves the **wrong** amount into the lattice, and no falsifier in either test file compares the ledger to the world.

*What the class map must specify:* a **reducer per enriched column**, from a closed set — `sum`, `min`, `max`, `first`, `last`, `count`, `distinct_count` — with **no default**. `payment_value` takes `sum`; `seller_id` takes `first` under a declared ordering column, or the class must declare that a multi-seller order is a different obligation. The set must be closed for the same reason `Pred` is small (`osv_ingest.h:96-99`): an open reducer language is a workflow language.

**A fourth defect in the same function, unlisted anywhere.** `resolve` returns the **first** match across side tables in declaration order (`:265-272`), and falls back to the class row *before* any side table (`:264`). Two side tables sharing a column name resolve by declaration order, silently. Require **qualified column references** (`table.column`) in the class map.

**Add a falsifier.** Every one of these defects is invisible to conservation, so §9 needs one that is not: **the ledger agrees with the world.** Replay a wire; for a sample of cells, recompute the class's declared columns directly from the source tables by an independent query and compare. Planted lie: last-row-wins on a summed column.

---

### F9 · Four horizons, five verbs — MAJOR · CONFIRMED

**§2.3**: "Horizons per verb `{h_act, h_work, h_hold, h_escalate}`."

**Defect (a): `h_fetch` is missing.** The verb set is closed and has five members (`osv_core.cuh:77-83`); `V_FETCH` is emitted by `gate` (`:264`), counted by `DispatchResult::count`, and given a cost of `0.0` in `VerbCost` (`osv_dispatch.h:125`). It is therefore the one verb that is free, ungraded, and unbounded: fetch → re-judge → fetch is a loop nothing in the design terminates. It is also not free in the resource that actually binds — §5 budgets 250,000 judgments a day and a fetch spends one. **Add `h_fetch`, and define its verdict: a fetch is `right` if the next judgment on the same cell crosses a threshold it did not previously cross, and `wrong` if it returns the same verb.** Without that, `fetch` is the verb a judge learns to emit when it wants to never be graded.

**Defect (b): escalate needs two.** `ORG-SOLVER_RECONCILIATION` §3 line 42 gives one — "decision arrival for escalate" — and one is not enough. Decision arrival grades *reachability* (did a human answer inside the window), which is a property of the human budget, not of the judgment. It does not grade *whether escalating was right*. Under one horizon every escalate that gets any reply at all grades `right`, and `gate` returns `V_ESCALATE` on **four of its five refusal paths** — warrant, thin `n_eff`, uncalibrated, high dispersion (`osv_core.cuh:255-262`) — so escalate becomes the verb that is never wrong. It also costs 12 minutes of the only inelastic resource in the design (`VerbCost::escalate = 12.0`, `osv_dispatch.h:122`), inflating `ClassKappa::created` with no counterweight. **Two horizons:** `h_escalate_arrival` (was the human reached in time) and `h_escalate_resolved` (the human's chosen verb, then that verb's own horizon), so an escalation whose human simply held is gradable as a wasted look.

**Defect (c): the grade fold has no data model for what an "outcome" *is*.** See F10.

---

### F10 · The grade fold cannot point at a row for the two commonest outcomes — MAJOR · PLAUSIBLE

**§3.9** and the `grade` body in **§2.1**: `decision_pos, outcome_pos, verb, verdict, horizon_ns`. `outcome_pos` is a tape position. Two of the four outcomes it must express have no position to name.

**What is an outcome for a hold, in rows?** The reconciliation is precise about the semantics — "deadline plus breach detection for hold" (§3, line 42) — and a deadline breach is **the absence of a closing `fact` by `due_ns`**. Nothing in the world emits it. There is no entry. `outcome_pos` has nothing to point at, so the row shape cannot express the outcome of the verb that carries most of the mass. This is the same hole as F1 seen from the other side, and it has the same fix: **the breach outcome is the `tick` entry at or after `due_ns`, and its position is the `outcome_pos`.** Once the clock is a row, the hold's outcome is a row. Say it in §3.9.

The three hold outcomes and their rows, which §3.9 must enumerate:
| hold outcome | the row | verdict |
|---|---|---|
| closed by the world inside the horizon | the closing `fact` | `right` |
| deadline passed, still open | the `tick` at/after `due_ns` | `wrong` |
| no deadline, no outcome | none | `ungradable` — excluded from license, graded only by the stratum |

**What is the outcome row for an act that was reversed?** A reversal is a later `fact` on the same cell that undoes the act. Nothing marks it as a reversal *of* anything: the `fact` body is `table, key, op, before, after, inverse, source_pos` with no `reverses_pos`. The grade fold would have to infer the relationship by matching a later fact's `before/after` against the earlier fact's stored `inverse` — which breaks on a partial reversal (a refund of part of an amount), on a reversal executed by a different mechanism (a credit memo rather than an order-status flip), and it cannot distinguish *the effector taking its own act back* from *a counterparty independently reversing it*. Those two grade oppositely: the first is the machine being wrong, the second is the world changing.

**Fix.** (i) Add `reverses_pos` to the `fact` body, stamped by the effector layer whenever it applies a stored inverse — that covers self-reversal exactly. (ii) Add a per-class `reversal_when` predicate, in the same `Pred` shape as `open_when`/`close_when`, for reversals that arrive from the world — this keeps the reversal definition where every other fact about the world already lives, in the class map, and it re-pins when it changes. (iii) State in §3.9 that a `grade` row's `horizon_ns` is the horizon **in force at `decision_pos`**, not at grade time; otherwise a `rule` entry that changes a horizon retroactively regrades history and the license fold stops being a function of the tape prefix. Falsifier: change a horizon by `rule`, replay, and every pre-change `grade` is bit-identical.

---

### F11 · The sweep is float, so "the same translation unit" does not give the same arithmetic — MAJOR · CONFIRMED

**§4** ("Fixed-point accumulation for every parallel sum"), **falsifier 1**, **§8 R2** ("batching bit-identical"), and **§11**'s claim that `step_cell`/`sweep_segment` carry over **unchanged**. The reuse claim inherits `osv_core.cuh`'s header promise (lines 5-8): "`g++ -x c++` and `nvcc` emit the same arithmetic. Not 'the same equations' as a promise — the same translation unit as a **property**."

**The defect.** The fixed-point law covers `load_fix` — the projection — and nothing else. `Lattice::dev` is `float*` and `step_cell` accumulates `lap`, `src`, `r`, `nd` in float. Red-black colouring buys *order* independence within a colour; it buys nothing against FMA contraction, which is a compiler decision. `nvcc` defaults to `-fmad=true`. Host compilers default differently. Same source, different bits.

**Evidence.** The shipped `osv_core_test.cpp`, unmodified, built three ways from one file:

```
/fp:precise        o_step  max|d| = 1.70e-07     CORE: 7/7 pass
/fp:fast           o_step  max|d| = 1.90e-07     CORE: 7/7 pass
/arch:AVX2 /fp:fast o_step  max|d| = 2.05e-07     CORE: 7/7 pass
```

Three different converged fields from one translation unit and one input. **All three pass**, because `o_step`'s oracle is `mx < 1e-3` — a tolerance, not a bit comparison. `o_step_batch` does compare bits (`memcmp`) but only *within* one binary, so it cannot see this either.

**Consequence for the build order.** §8 R1 runs the folds "on the host"; R2 moves them to the card with the gate "batching bit-identical." R2's gate as written will pass on the card while producing a field that differs from R1's host field — and §0 law 5 ("the peer is a cache … losing it costs time, never truth") assumes the two agree.

**Fix.** (a) Pin the floating-point contract, not just the source: `-ffp-contract=off` on the host and `-fmad=false` on `nvcc`, recorded as a build pin the way `serve_hash` is a serve pin, and asserted at boot. (b) Better, and cheaper than it sounds: make `dev` fixed-point like `load_fix`, since it is already the case that `to_fix`/`from_fix` exist and the field feeds a threshold at 1.0 with values reaching 3,676 — see F2, where float resolution is already the binding constraint. (c) Restate R2's receipt gate as **host and device bit-identical**, not "batching bit-identical."

---

### F12 · Open question 8, answered: yes for live, no for replay, and the blueprint contradicts itself by 8× — MAJOR · CONFIRMED

**OQ8**: "Whether the field's standing query needs differential maintenance in v0 or whether full recomputation per append is fast enough at organizational event rates (likely yes for v0)."

Measured on this box, single-threaded host code — a **floor**, not a card measurement; treat every absolute below as [BUDGET] and the ratios as the finding (`qc_field.cpp`, `qc_diff.cpp`):

| operation | cost |
|---|---|
| full re-projection, 50,000 cells → 768 lattice cells | **0.66–0.75 ms** |
| differential update, one fact (subtract old slot, add new) | **0.000013 ms** |
| one red-black sweep, 768 cells | **0.0055 ms** |
| 12 sweeps (what the system actually needs — see F2) | **0.066 ms** |
| **full recompute per append** | **0.82 ms → 1,219 appends/s** |
| **differential per append** | **0.067 ms → 14,949 appends/s** |

**Projection is 92% of the full-recompute budget.** The sweep is not the problem; re-reading 50,000 rows to rebuild a 768-cell histogram is.

**At organizational rates (a few per second): yes, comfortably.** 1,219/s against ~5/s is 0.4% utilization. OQ8's parenthetical is correct for live traffic.

**At replay rates (thousands per second): no.** 1,219/s is below the requirement, and the blueprint contradicts itself in two places:
- **§3.1** targets **10,000 entries/s** on one core for the transactor. **§2.4** maintains the field **on every append**. The field caps the pipeline at 1,219/s. **An 8× internal contradiction between two [BUDGET] rows.**
- **§3.8** promises "years of tape in hours." An organization at 5 events/s produces ~158 M entries a year; at 1,219/s that is **36 hours per year replayed** — days, not hours, for the "years" the section names. The differential fold does the same year in **2.9 hours**.

**The differential alternative, and why it is not a v1 optimization.** On a `fact` touching cell *c*: subtract `to_fix(c.amount)` and `to_fix(1.0f)` from `project_one(c_old)`, apply the fact, add them back at `project_one(c_new)`. O(1) instead of O(50,000). **It is bit-identical to full recomputation** — receipt, `qc_diff.cpp` probe (3), 1,000 mixed facts including amount changes, deadline moves that cross slots, and closures:

```
load_fix  bit-identical : YES
count_fix bit-identical : YES
```

The reason it is exact is the design's own fixed-point choice: integer subtraction undoes integer addition. A float accumulator could not do this. So the differential fold satisfies §2.4's `verify_fold` ("cold recompute from genesis must equal the incremental result bit for bit") **by construction rather than by test** — it is roughly fifteen lines, and it is the one place where a stated law pays for itself immediately.

For a `tick` (F1), the same trick applies with a deadline wheel: only the cells whose `due_ns` crosses a slot boundary need updating — 1,102 of 50,000 in an idle hour, not all of them.

**And the blueprint regressed here.** The brainstorm already had it right: "The lattice … is a materialized view the store maintains **incrementally** as entries land" (line 40). §2.4 downgraded that to "on every append" and OQ8 reopened a question the predecessor had closed.

---

### F13 · A warm start after a slot shift is not a warm start — MINOR · CONFIRMED

**§2.4**, "on every append; sweep kernel to tolerance," with `dev` persisting between sweeps.

When `now` advances a slot width, every commitment's slot index shifts, so the lattice's *contents* move while `dev` — indexed by lattice cell, not by commitment — does not. The retained `dev` is the converged answer for a different population. Measured (`qc_field` [5]): `max |dev_after − dev_before| = 21.26` across one idle hour, on a field whose act threshold is 1.0.

Two consequences the blueprint should state: (i) a tick is a near-cold re-solve, not an incremental one, so it must be budgeted as such; (ii) because `dev` carries across appends, the field fold is **stateful**, and §2.4's "cold recompute from genesis must equal the incremental result bit for bit" then requires that a `ckpt` store `dev` and the `flags` array **exactly** — a snapshot that stores only `load_fix` and re-solves will not reproduce the incremental result. §2.1's `ckpt` body (`fold, pos, digest, bytes`) permits this; §3.2 and §6 must say it is required.

---

### F14 · §9 has ten falsifiers, §11 claims fifteen carried forward, and one of the fifteen contradicts §3.5 — MINOR · CONFIRMED

The arithmetic checks: `osv_core_test.cpp` has 7 `chk` calls, `osv_dispatch_test.cpp` has 8 — fifteen. But §9's own list is ten, and the two sets are never reconciled: the reader cannot tell whether the design's falsifier suite is 10, 15, or 25 (§9's preamble in the OSV page promises 25).

At least one carried-forward falsifier cannot survive as written. `o_no_silence` (`osv_dispatch_test.cpp:77`) asserts `DispatchResult::no_silence()` — "one verb per commitment, **every period**, no exceptions" (`osv_dispatch.h:90, :159, :170`). §3.5 invokes the judge **per delta**, and §5 sizes the load as "50,000 cells at 5 deltas each," which is delta-driven. Under per-delta invocation a cell with no delta in a period emits no row and `no_silence()` is false by construction. That is a genuine law change — from *no silence per period* to *no silence per delta* — and the blueprint makes it implicitly, in a sizing table.

**Fix.** §11 must list which of the fifteen survive, which are amended, and what replaces `o_no_silence`. The natural replacement: **every delta on an open cell produces exactly one `verb` or `hold` or `refuse` entry, and no delta produces two.** That preserves what the law was for — the hold is never a silence — under the new invocation clock.

---

### F15 · Open question 6 is partly unanswerable as posed — MINOR · PLAUSIBLE

**OQ6**: "Horizon values per class on a real wire, measured from history before any are declared."

Circular as written: measuring a decision→outcome lag requires joining decisions to outcomes, which is what a horizon gates. Resolvable for act and work — replay history with **no horizon cut**, take the empirical lag distribution, set `h` to a named quantile, and record the measurement itself as a `rule` entry so the choice is on the tape. Not resolvable for **hold**: a hold's outcome arrives by deadline breach or not at all, and the un-breached holds are exactly the ones with no row (F10), so the lag distribution is right-censored and act-heavy. `ORG-SOLVER_RECONCILIATION` §3 line 44 names the only instrument that fixes it — "the stratum … is the instrument that grades holds at all once the humans have left the class." **OQ6 should say: `h_act` and `h_work` are measurable from history before v0; `h_hold` is not measurable until the stratum runs, and until then the hold side's `n_eff` is zero and no band is licensed.** That is the safe direction, and it should be stated rather than discovered.

---

## 3 · What is right and must not be changed

1. **The 64-byte reordered `Cell` in §2.2's code block.** Exactly 64 bytes, 8-byte aligned, `pos_last` correctly placed at offset 32, and — unlike `Commitment` — **zero padding**. Verified by compilation. Keep the code block; fix the prose.
2. **Fixed-point accumulation for the projection.** It is not merely a determinism law; it is what makes the differential fold in F12 exact and therefore what makes `verify_fold` provable rather than testable. `o_projection_order` also confirms the float lie is live on real data ("float accumulation differs by order = yes").
3. **`pos_last` as a 64-bit tape position.** The truncation defect is real and worse than falsifier 9 states — receipt P3 shows both the idempotence and monotonicity guards die above 2^32, not just one. Widening is right. Just do not delete the source clock while doing it (F3).
4. **Pressure enters at the gate, never at the template or the probe.** `ORG-SOLVER_RECONCILIATION` §1: "Pressure enters `gate` as an additive term and never touches `probe_one`." §2.3's exclusion of non-tape-derivable values from the template is this law in the render, and it is the reason the cognitive index can be a function at all.
5. **`slot_of`'s two boundary rules** — overdue lands in slot 0 (maximum pressure, never negative time), no deadline lands in the far bucket. `o_slot` checks 600 offsets and all stay in range. Carry it verbatim.
6. **Warrant cells as boundary conditions**, never relaxed (`step_cell:189`) and never acted (`gate:255`). `o_pressure` confirms pressure cannot override the fence.
7. **`ungradable` as a verdict distinct from `wrong`**, and holds with no deadline excluded from licensing (§3.9, §6). This is the reconciliation's §3 conclusion carried correctly.
8. **UNMAPPED IS LOUD.** Receipt (b) shows the sellers table was counted *and named*. That law works. Extend it to unresolvable columns (F8b), do not weaken it.
9. **`Ledger::high_rev` is already `uint64_t`** and compared at full width (`osv_ingest.h:208, :290`). The 64-bit clock is half-present; finish it.

---

## 4 · My top three changes

1. **Put the clock on the tape.** `now` for every fold is the `t_mono_ns` of the entry being folded; the transactor appends `tick` entries at `slot_ns/4` plus a deadline-wheel tick at each `min(due_ns)`. Without this, falsifier 1 cannot pass, the field rots 2.2% per idle hour, and the hold has no outcome row.
2. **Make the field fold differential and fix its stopping rule.** Subtract-old / add-new is bit-identical to full recomputation (receipt), 50,000× cheaper on the dominant 92% of the cost, and closes the 8× contradiction between §3.1's 10,000 appends/s and §2.4's per-append field. Replace absolute `tol` with `tol · max(1, ‖dev‖∞)` plus a tape-recorded iteration cap, or the sweep never terminates at the blueprint's own sizing.
3. **Keep both clocks and make the writ enforce the ingest law.** `pos_last` (tape) on the hot row, `src_pos_last` (source LSN, 64-bit) in the cold record, and a native constraint in §3.6 that refuses a `fact` whose `source_pos` does not exceed the cell's — moving idempotence and monotonicity out of a header comment and into the constraint layer where §0 law 4 puts them.

---

## 5 · Open questions I would add to §10

11. **Is `now` a tape value, and at what cadence are `tick` entries appended?** Slot width, quarter slot, or a deadline wheel — and is the cadence a `rule` parameter that re-pins when changed?
12. **What is the outcome row for a hold, for a reversal, and for a fetch?** `grade.outcome_pos` is a tape position; two of the four cases currently have no row to name.
13. **Does escalate take one horizon or two, and what bounds a fetch loop?** Under one escalate horizon and no `h_fetch`, two of five verbs are ungradable and therefore free.
14. **Is `dev` float, double, or fixed-point, and what is the exact stopping rule and floating-point contract that survives the host→device move?** Three MSVC flag settings already produce three different fields from one source.
15. **What produces `baseline`, on what clock, and how slowly does it move?** With `baseline ≡ 0` the deviation-storage law is decorative. Same question for `capacity`: which fold supplies it, given the cell table has seats and not rates?
16. **Is the cognitive index keyed by the cell's position or by a digest of the rendered bytes?** The value spans a neighborhood; the key names one cell.
17. **Which of the fifteen carried-forward falsifiers survive per-delta invocation, and what replaces `o_no_silence`?** §9 lists ten and §11 claims fifteen; the two sets are never reconciled.
18. **Per-class pins or one map pin, and what exactly does each cover?** Today `SchemaMap::pin` is blind to `Pred::op`, `cls`, and `flags` — including `F_WARRANT`.
19. **Does the class map declare a reducer per enriched column and an explicit join path?** Without both, one-to-many silently means last-row-wins and a two-hop column silently means zero.
20. **Do folds read class-map state as-of the entry's position?** If not, a `rule` entry changing a horizon retroactively regrades history and the license fold stops being a function of the tape prefix.

---

## Appendix · Receipts

All artifacts under `C:/TAPESTRY/qc/scratch/cell/`. Sources copied unmodified from `C:/Websites/aorta-site/_upload/`; nothing outside `C:/TAPESTRY/qc/` was written.

| file | what it proves |
|---|---|
| `osv_core_test.exe` | baseline **7/7 PASS**, exit 0, `sizeof(Commitment) == 64` |
| `qc_probe.cpp` / `.exe` | P1 struct offsets and the 72-byte naive swap · P2 digest is padding-sensitive · P3 both ingest guards die above 2^32 · P4 pin blind to `op`/`cls`/`flags` · P5 the three ingest defects |
| `qc_field.cpp` / `.exe` | per-append cost, idle-hour slot rot, `now`-dependence of the field |
| `qc_conv.cpp` / `.exe` | the tolerance never fires: 200,000 iterations parked at 1 ULP; double converges in 12 |
| `qc_diff.cpp` / `.exe` | `o_step`'s break never fires · `F_DIRTY` is magnitude-dependent · differential update bit-identical · honest per-append costs |
| `swap/` | the §2.2 struct applied in place; compiler errors enumerate every change site |
| `core_precise.exe`, `core_fast.exe`, `core_avx2.exe` | one source, three fp settings, three different converged fields, all three passing |

---
*QC-2 of 7, written 2026-09-08 by Claude Opus 5. Every CONFIRMED row above was compiled and run on this box; every PLAUSIBLE row is reasoning from the cited lines and is marked as such. The receipt wins.*
