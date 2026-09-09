# QC-7 · BUILD ORDER, CODE REUSE, FEASIBILITY, FALSIFIER EXECUTABILITY, DOCUMENT HYGIENE

**Reviewer slice:** §1 (diagram vs text), §8, §9, §10, §11, §12, and every `[M]` tag.
**Subject:** `C:/TAPESTRY/TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1_2026-09-08_FABLE5-1.md`
**Read in full:** the blueprint · `C:/fusor1/converge/src/fusord.cpp` (2,761 lines) · `osv_core.cuh` · `osv_ingest.h` · `osv_dispatch.h` · `osv_core_test.cpp` · `osv_dispatch_test.cpp` · `C:/fusor1/FUSOR_KERNEL_CONVERGENCE_2026-09-04_FABLE5-1.md` (459 lines) · `C:/auricle/runs/m0-0-shared-kv-2026-08-07.md` · the CORTEX scan receipt.
**Built and ran:** both OSV test suites, plus three purpose-written probes, with MSVC 14.44 (`C:/TAPESTRY/qc/scratch/build/`). Receipts in §0 below.
**Date:** 2026-09-08 · Claude Opus 5.

---

## 1 · Verdict in three sentences

The blueprint's reuse claims are, symbol for symbol, unusually honest — every function §11 names exists at the line it implies and does roughly what it says — but three of them are load-bearing in a way the named code will not bear, and the single most consequential one, `seq_cp` as a free fork, is contradicted by a dated receipt the blueprint's own predecessor cites four days earlier. The sizing in §5 is arithmetically correct on the constants it lists and wrong by about seven-fold on the constant it omits: the 9B hybrid judge carries roughly 52.7 MB of per-sequence recurrent state that `seq_cp` copies and 4-bit KV quantization does not touch, so a 141 GB card holds about 2,200 warm cells, not 15,000 or 45,000. The build order is a good order and the gates are mostly real receipts, but R2 reuses kernels that do not exist, R3's llama.cpp sequence budget is off by three orders of magnitude, and §9's falsifier 7 as written is a test that cannot fail — so the document is a strong draft whose defects are concentrated in exactly the two rungs (R2, R3) where the estate has the least running code.

---

## 0 · What I ran, so the findings below carry receipts

Compiler present: **MSVC 14.44.35207** (`C:/Program Files/Microsoft Visual Studio/2022/Community`) and **nvcc**; no `g++`, no `clang++`. All five OSV files were copied unmodified to `C:/TAPESTRY/qc/scratch/build/` and built with `cl /utf-8 /std:c++17 /O2 /EHsc /W4 /D NOMINMAX`.

| binary | compile | warnings | run | result |
|---|---|---|---|---|
| `osv_core_test.exe` | clean | **none at /W4** | exit 0 | **7/7 PASS** |
| `osv_dispatch_test.exe` | clean | one, `C4996 sscanf` at `osv_ingest.h:67` | exit 0 | **8/8 PASS** |

Total **15/15**, so §11's "fifteen" is the right count (see finding F13 for what the fifteen actually assert). `sizeof(Commitment)` printed 64 B, confirming `osv_core.cuh:101`.

Three probes I wrote to turn readings into receipts (sources in the scratch dir):

- `cell_check.exe` — the blueprint's §2.2 `Cell` struct **is exactly 64 bytes**, `alignof` 8, `pos_last` at offset 32, last byte at 63, no padding. §2.2's claim holds. The same binary reproduces the truncation defect: at revision `4294979641`, `osv_ingest.h:284`'s `(uint32_t)r.rev` stores `12345`, after which the idempotence guard (`osv_ingest.h:317`) calls a replay a fresh row **and** the monotonicity guard (`:318`) fails to refuse a stale one. Both arms of falsifier 9 confirmed.
- `pin_check.exe` — see finding F3. Output: flipping `open_when.op` from `P_IN` to `P_NOT_IN`, flipping `flags` to `F_WARRANT`, and changing `cls` **each leave `SchemaMap::pin()` bit-identical** at `0x53eb3784009566d2`; a control change to `due_col` moves it to `0xbe8e2e9b3c7346a4`.
- Static confirmations by grep: **zero** `__global__`, `<<<`, `cudaMalloc` or `blockIdx` in all five OSV files; `fusord.cpp` `Tape` uses `std::fflush` only (`:906`), never `fsync`/`FlushFileBuffers`.

---

## 2 · Findings, most severe first

### F1 · BLOCKER · CONFIRMED — §5, §2.5, §9.7, §8-R3: the per-sequence recurrent state is omitted, and it is the constant that decides the design

**Claim.** §2.5: "a fork shares pages and copies on write, which is `llama_memory_seq_cp` under `kv_unified` **[M: fork at 0 MiB, m0-0]**." §5: 17 KB/token → 8.5 MB per cell suffix → "about 15,000" warm cells per 141 GB card, "about 45,000" with 4-bit KV.

**Defect.** Both receipts exist and both are quoted correctly. Read together they say something the blueprint does not.

- `C:/auricle/runs/m0-0-shared-kv-2026-08-07.md:11,38` — "forking 3 branches over an 8k trunk added **0 MiB**", "`fork delta for 3 branches = 0 MiB` … `seq_cp is refcount, not copy`". Three branches. 8k trunk. 2026-08-07.
- `C:/fusor1/FUSOR_KERNEL_CONVERGENCE_2026-09-04_FABLE5-1.md:152` — "`seq_cp` **copies a recurrent state**, not just cell membership; the probe cost of about 110-130 ms per three-seat judgment [R every tape] already includes it." Four weeks later, same model.
- `…CONVERGENCE…:178` — "`60,257,248 B at 434 tokens` and `61,616,944 B at 512 tokens` give **17,432 B per token marginal and about 52.7 MB fixed (the recurrent state)**."

§5 takes the marginal from line 178 and leaves the fixed term on the same line of the same file. The two m0-0/09-04 receipts reconcile only one way: the fork adds 0 MiB *because llama.cpp preallocates the recurrent state for all `n_seq_max` slots at context creation*. m0-0's own VRAM trace shows it — `+trunk=11650`, `+3branch=11650`, unchanged at 1 MiB granularity, when three copies of a 52.7 MB state would have shown ~151 MiB. The cost is real and paid up front, per sequence slot.

**Consequence, on the blueprint's own numbers.** Per warm cell = 52.7 MB recurrent + 8.5 MB suffix = 61.2 MB. `(141,000 − 7,000 weights − 136 root) / 61.2 ≈ **2,190 warm cells**`, against §5's 15,000 — about 7× optimistic. The 4-bit row is worse: quantizing KV halves the 8.5 MB and does not touch the 52.7 MB, giving `134,000 / (52.7 + 4.25) ≈ **2,350**` against 45,000 — about 19× optimistic. §5's conclusion, "quantized KV covers the whole open set," is false by an order of magnitude, and "the warm set covering the ranked top third of cells" becomes the top 4%.

**Severity/confidence.** Blocker / CONFIRMED (both receipts read in full; arithmetic shown).

**Fix.** Add the 52.7 MB fixed term as its own row in §5 tagged `[M: 52.7 MB fixed, smoke 1 checkpoints]`, recompute the warm-set rows, and promote to §10 the question this forces: *does the judge have to be a recurrent hybrid?* A pure-attention judge of comparable size makes §2.5's copy-on-write fork true as stated and restores §5's sizing. That is a design fork, not a tuning knob, and it belongs above the line.

---

### F2 · BLOCKER · CONFIRMED — §7, §3.3, §3.7: the module gate forbids the very socket the peer is specified to hold

**Claim.** §7: "The peer process passes a module gate at boot: no network module except the tape and transactor sockets **[M: the gate exists in fusord]**." §3.3 repeats it. §3.7: "a Unix or TCP socket with length-prefixed JSON frames."

**Defect.** The gate exists and is exactly as cited — `fusord.cpp:436` `module_gate`, enumerating loaded modules against `FORBIDDEN_MODULES` at `:435`. That list is `{ "ws2_32.dll", "winhttp.dll", "wininet.dll", "urlmon.dll", "dnsapi.dll", "ggml-rpc.dll" }`, and a match returns false, which at `:1658-1661` is a **fatal, exit 2**. On Windows every socket — TCP *and* `AF_UNIX` — goes through Winsock, i.e. `ws2_32.dll`. A peer holding transactor and tape sockets loads `ws2_32.dll` and fails the gate on boot. fusord says so itself in three places: `:1110` "acts: no socket…", `:1940` "which is structurally true (this process holds no socket)", `:1957` "The kernel never opens a socket."

The gate is not a socket-scope check and cannot be made into one — it enumerates modules, not bound addresses. §7's security claim ("no network module except the tape and transactor sockets") is not a thing this mechanism can express.

**Severity/confidence.** Blocker / CONFIRMED.

**Fix.** Either (a) keep the gate verbatim and make the peer socketless — the tape and the transactor become shared-memory or file transports, which is what fusord already does with the spool and is the smaller change; or (b) replace the gate with a bound-address assertion plus an outbound-connect ban, and stop citing `[M: the gate exists in fusord]` for it, because that is a different mechanism with no receipt. Also downgrade the tag: existence-in-source is `[R]` in the estate's own register, not `[M]`, and the gate is Windows-only (`:474-481` stubs it to `return true` elsewhere), boot-only, and bypassable with `--egress-unchecked` (`:1081`, `:1658`).

---

### F3 · BLOCKER · CONFIRMED — §2.3: `SchemaMap::pin` is blind to the predicate operator, the declared flags, and the class id

**Claim.** §2.3: "The map's pin (`SchemaMap::pin`) is extended to cover horizons and templates. A change to the pin is a `rule` entry, and every cognitive index entry built under the old pin is invalidated."

**Defect.** Before extending the pin, note what it does not already cover. `osv_ingest.h:165-179` mixes `name, source, table, key_col, open_when.col + vals, close_when.col + vals, opened_col, due_col, amount_col, seat_col, segment_col`, and each `enrich.table/key_col`. It never mixes `open_when.op`, `close_when.op`, `cls`, or `flags`. Proven by `pin_check.exe` (§0): all three drifts leave the pin bit-identical while a control drift moves it.

This is not cosmetic in TAPESTRY. `flags` carries `F_WARRANT` (`osv_ingest.h:144`, `osv_core.cuh:67`), which is what makes a write irreversible, forces the escalate path (`osv_core.cuh:255`, `osv_dispatch.h:222`), and is what §7 hangs the N-of-M quorum off. And `open_when.op` flipping `P_IN`→`P_NOT_IN` inverts the meaning of "the obligation exists" — the map now opens commitments exactly where it used to close them — with no pin movement, therefore no `rule` entry, therefore no invalidation, therefore a cognitive index that is warm and wrong. The pin's own comment at `:163-164` promises the opposite: "if the map drifts, the ledger it produced is a different object."

**Severity/confidence.** Blocker / CONFIRMED (receipt in §0).

**Fix.** Fix the pin before extending it: mix `open_when.op`, `close_when.op`, `cls`, `flags`, and the new horizon and template objects, and re-mint every stored pin in the same commit. Add a falsifier: *every field of `ClassMap` moves the pin* — a loop over a mutated copy per field, which is the only form of this test that does not rot as fields are added. Then say in §2.3 that the pin is the writ's tripwire, not just the schema's.

---

### F4 · BLOCKER · PLAUSIBLE — §8-R3, §3.4: the llama.cpp sequence budget is off by three orders of magnitude

**Claim.** §3.4: "Reference implementation for v0: llama.cpp sequences, one root sequence plus one sequence per warm cell, `seq_cp` for forks, `seq_rm` for eviction, `kv_unified` on." §8-R3 reuses "llama.cpp sequences, fusord `checkpoint` and `restore`."

**Defect.** fusord sets `cp.n_seq_max = 8` (`fusord.cpp:1713`) and uses five fixed slots — `TRUNK=0, SCRIBE=5, GEN=6, DECIDE=7` (`:1132`). The blueprint needs one slot per warm cell, i.e. ~15,000 (§5) and, after F1, still ~2,200. `n_seq_max` is a context-creation parameter that preallocates per-slot state (F1), so it is not a soft limit you raise for free — raising it to 2,200 preallocates 2,200 × 52.7 MB before a token is decoded. Nothing in the estate has run llama.cpp above single-digit sequences; m0-0's largest was 3.

Second, the checkpoint primitive does not scale the way §3.4 implies. `llama_state_seq_save_file` (`fusord.cpp:1516`) writes **one file per sequence**. §3.4's "warm pages may be checkpointed to NVMe with a `.meta`" therefore means one file plus one `.meta` per warm cell: at 2,200 cells × 61.2 MB that is ~135 GB per checkpoint pass, and at §5's 15,000 it is ~920 GB. `checkpoint`/`restore` reuse verbatim gives you a per-cell serializer, not a page store.

**Severity/confidence.** Blocker / PLAUSIBLE (the `n_seq_max` semantics are read from fusord's use and m0-0's trace, not from the llama.cpp source, which I did not open).

**Fix.** State R3's real dependency: a paged KV store with an eviction path that is not "one llama sequence per cell." Either adopt a serving stack that already does paged attention with block-level sharing, or scope v0 to a warm set that fits `n_seq_max` in the low thousands and say so in §5. Add to §10: *what is the maximum `n_seq_max` this llama.cpp build accepts, and what does the context cost at that value?* — that is a one-hour measurement and it decides R3.

---

### F5 · MAJOR · CONFIRMED — §8-R2, §11: "the OSV_HD kernels" do not exist

**Claim.** §8-R2: "the peer on the card: folds and the sweep as kernels; exhaustive embedding scan | reuses **the OSV_HD kernels** | batching bit-identical; scan at CORTEX speed."

**Defect.** There are no kernels. `OSV_HD` is a macro that expands to `__host__ __device__` under `__CUDACC__` and to nothing otherwise (`osv_core.cuh:46-50`); it decorates *leaf functions*. Across all five files there is not one `__global__`, not one `<<<...>>>` launch, not one `cudaMalloc`, not one `blockIdx` (grep confirmed, §0). `sweep_segment` (`osv_core.cuh:216-225`) is a serial double loop; the comment at `:212` says "On device: one block per segment, one thread per cell" — a design note for a kernel that was never written. The projection's fixed-point accumulation, which is the whole determinism argument (`osv_core.cuh:26-29`), exists only as a host loop inside the *test* (`osv_core_test.cpp:40-53`), not in the module.

R2 also names no launch shape, no device allocation strategy, no red-black colouring launch, and no host↔device bit-identity harness — and its second gate ("scan at CORTEX speed") reuses nothing at all, because there is no embedding store, no vector type, and no GEMM anywhere in the OSV files.

**Severity/confidence.** Major / CONFIRMED.

**Fix.** Change R2's reuse cell to "`osv_core.cuh`'s `OSV_HD` leaves, which compile unchanged under `nvcc`; the kernels are new." That is still a genuine and valuable reuse — the same translation unit compiling both sides is exactly what makes "batching bit-identical" testable — but it is a property, not a kernel. Then name the launch shape (one block per segment, one thread per cell, two colour passes, fixed-point `atomicAdd` on `int64_t`) and the harness (run `sweep_segment` on host and the kernel on device over the same lattice, `memcmp` the `dev` array).

---

### F6 · MAJOR · CONFIRMED — §11, §3.7: `LaneTail` is a file tailer, not a subscribe transport

**Claim.** §11: "`fusord.cpp` · `LaneTail`, the lane contract → **the v0 subscribe transport**." §3.7: "the lane-contract v0.1 text framing remains accepted for subscribe so fusord's tailer works unchanged."

**Defect.** `LaneTail` (`fusord.cpp:579-776`) opens a *file* by path, polls its size, seeks, reads, and parses lines (`loop()` at `:719`). It is the consumer end of a spool on disk. It has no listener, no connection, no framing over a stream, no back-pressure protocol — its back-pressure is "block on a full ring and let the disk hold the backlog" (`:758-762`). §3.7's `subscribe(query, from_pos)` is a server pushing a filtered delta stream to a client. These are opposite ends of different mechanisms.

What is actually reusable is the *framing* — `t_mono_ns \t lane \t grain \t text` and the `#lane-contract v0.1` header (`parse()` at `:672-711`) — plus the cursor-with-prefix-hash idea (`:643-661`), which is a genuinely good design for a resumable subscriber. The tailer itself carries forward only if TAPESTRY writes a spool file that fusord reads, which is a compatibility path, not a transport.

Compounding: per F2, fusord cannot hold a socket by its own gate, so "the v0 subscribe transport" names code that is structurally forbidden from being a transport.

**Severity/confidence.** Major / CONFIRMED.

**Fix.** Split the §11 row: "`LaneTail`'s frame grammar and its cursor+prefix-hash resume → the subscribe *wire format* and the client's resume rule; the transport is new."

---

### F7 · MAJOR · CONFIRMED — §8-R0, §3.1, §4: the reused `Tape` does not fsync, assign `pos`, rotate segments, or canonicalize

**Claim.** §8-R0 reuses "`Tape`, `chain_hash`, `Blake2b`, `write_atomic` from fusord" for "the tape and the transactor on one node." §3.1: "assign `pos` and `t_mono_ns`; compute `h`; append; **fsync**." §4: "An entry is durable when fsynced on the leader."

**Defect.** Four gaps, all in the same 50 lines.

1. **No fsync.** `Tape::put` (`fusord.cpp:902-909`) does `fprintf` then `std::fflush(f)`, commented "durable as it goes, not at exit." `fflush` moves bytes from the C stdio buffer to the OS page cache; it is not `fsync`/`FlushFileBuffers`, neither of which appears anywhere in the file (grep, §0). A power loss loses flushed-but-unsynced rows. The durability law in §4 is not in the reused code.
2. **No `pos`.** `put` takes a caller-built body and appends only `,"prev":…,"h":…}`. There is no monotone counter, no position assignment, no reuse guard. §2.1's `pos` — "the deposit clock; monotone; never reused" — is new.
3. **No segments.** §3.2's 64 MiB segment files named by first position, with the chain continuing across them, do not exist; `Tape` is one appended file (`:896`).
4. **No canonicalizer.** §2.1 requires "keys sorted, no whitespace, integers as decimal, floats as their shortest round-trip decimal," and says "the chain is the conformance test." `chain_hash` (`:312`) hashes whatever string the caller built. Every row in fusord is hand-concatenated in field order. There is no canonical form and nothing enforces one.

What *does* carry cleanly and is worth keeping: `Blake2b` (`:235-309`, six vectors verified against Python `hashlib` per `…CONVERGENCE…:46` and B.1), `chain_hash`'s construction `blake2b256(prev_hex ‖ body)` (`:312-318`), `replace_file`'s 20×5 ms retry against the Windows rename hazard (`:362-379`, and the hazard itself is `[M]` at `…CONVERGENCE…:53`), `write_atomic` (`:380`), and `Tape::open`'s torn-trailing-row recovery (`:863-899`), which is a genuinely hard-won piece of code and exactly what §3.2 asks for.

**Severity/confidence.** Major / CONFIRMED.

**Fix.** Say in §8-R0 what is reused (the hash, the chain construction, the atomic replace, the torn-row recovery) and what is new (position assignment, segments, canonicalization, fsync with group commit). Then reconcile §3.1's `[BUDGET]` of 10,000 entries/s with a per-entry fsync: that is a 100 µs budget covering canonicalization, BLAKE2b, constraint evaluation, and a durable write. It is reachable only with group commit, which §3.1's serial "append; fsync; publish" does not describe. Either describe it or lower the budget.

---

### F8 · MAJOR · CONFIRMED — §9.7: "the fork that costs nothing" is a test that cannot fail

**Claim.** §9.7: "N forks of one read state consume pages only as they diverge. Lie: eager copying."

**Defect.** The obvious implementation — measure free device memory before and after `seq_cp`, assert the delta is ~0 — is exactly what m0-0 did (`m0-0-shared-kv-2026-08-07.md:38`), and it returns 0 MiB *whether or not the fork is lazy*, because the per-sequence state was preallocated at context creation (F1). The test passes for a reason unrelated to the property it claims to establish. A falsifier whose lie arm cannot be constructed is not a falsifier.

**Severity/confidence.** Major / CONFIRMED.

**Fix.** Restate the property as a function of `n_seq_max`, which is the variable that actually moves: build contexts at `n_seq_max ∈ {1, 2, 4, …, N}` with an identical trunk and record the resident footprint; the claim "forks are free" is the claim that the footprint is flat in `n_seq_max`. On this model it will not be — it will rise by ~52.7 MB per slot — and that failing arm is the honest receipt. Keep the original measurement as a second assertion (marginal fork cost within a fixed `n_seq_max` is zero), which is true and worth having.

---

### F9 · MAJOR · CONFIRMED — §9.5: "the inverse that unwinds" is false for two of the six folds

**Claim.** §9.5: "Apply a saga, apply its inverses, and **every fold** equals its state before the saga."

**Defect.** The kappa fold is a monotone accumulator. `ClassKappa` (`osv_dispatch.h:145-152`) holds `created`, `removed`, and five counters, all incremented and never decremented in `Dispatch::run`'s emit pass (`:277-295`). Applying an inverse produces *more* verbs, hence more `created`/`removed`, hence a kappa strictly further from where it started. The same holds for the grades fold (§2.4), which emits one `grade` per decision: the inverses are themselves decisions and are themselves graded. "Every fold" is false as written for at least these two, and the falsifier will fail on a correct implementation — the worst kind of failure, because the fix will be to weaken the test rather than the code.

**Severity/confidence.** Major / CONFIRMED.

**Fix.** Scope it: "every *state-bearing* fold (state, field, allocate) returns to its prior digest; the *accumulating* folds (grades, kappa, license) gain exactly the rows the saga and its inverse produced, and no others." That second clause is the stronger test anyway, and it is checkable by count.

---

### F10 · MAJOR · CONFIRMED — §3.8 vs §5: "years of tape in hours" contradicts §5's own arithmetic by ~360×

**Claim.** §3.8: "Replaying history is the first thing run on any new judge and on any new organization: **years of tape in hours [BUDGET]**."

**Defect.** §5 gives 250,000 judgments/day at 110 ms serial = 7.6 card-hours per day of tape. One year of tape is therefore 365 × 7.6 = **2,774 card-hours ≈ 116 days**, not hours. To reach "a year in hours" you need ~100 cards, or a batching factor of ~100 — and the estate's measured batching factor is 1.208× for 3 streams (`m0-0…:13`, "COST MULTIPLE (3 vs 1) = 1.208x"), i.e. ~2.5× aggregate throughput, not 100×. §8-R4's gate ("a replayed year produces a graduation list") inherits the contradiction.

**Severity/confidence.** Major / CONFIRMED (both numbers are the blueprint's own).

**Fix.** Replace with the honest figure and its lever: "a year of tape is ~2,800 card-hours serial; the replay is embarrassingly parallel across cells, so it is a fleet-size question, not a latency question." Then R4's gate should name the tape span it actually replays.

---

### F11 · MAJOR · PLAUSIBLE — §3.5, §12: a two-logit margin cannot choose among five verbs

**Claim.** §3.5: the peer "decodes the probe frame, **reads the margin as emit minus hold** on the fork … and emits a `verb` or `hold` entry." §12: "**Verb**: hold, act, work, fetch, escalate."

**Defect.** `probe_one` (`fusord.cpp:2048-2057`) computes exactly one scalar: `l[emit_tok] - l[hold_tok]` (`:2054`). That is a binary decision. The five-way verb in the estate comes from `gate()` (`osv_core.cuh:254-266`), a *deterministic function* of margin, pressure, dispersion, `n_eff` and calibration — not from the model. So in the reused parts, the judge decides *whether*, and a fixed rule decides *what*. The blueprint never says this, and it matters: §0 law 6 says "the judge is a procedure whose body is weights," §3.9 grades "per verb," and §3.5's license "reads per verb" — but if `gate()` picks the verb, the weights are not what is being graded on four of the five.

**Severity/confidence.** Major / PLAUSIBLE (the blueprint may intend a five-way readout; it does not say so).

**Fix.** Decide and state it in §3.5. If the judge picks the verb, the probe frame needs five tokens and `probe_one` is *not* reused verbatim. If `gate()` picks it, say so, and make §3.9's grading distinguish "the judge's margin was wrong" from "the gate's thresholds were wrong" — they are different defects with different repairs, and the current design cannot tell them apart.

---

### F12 · MAJOR · CONFIRMED — §11, §2.2: "the 32-bit fix" is under-specified and misses a second truncation site

**Claim.** §11: "`osv_ingest.h` · `SchemaMap`, `Pred`, `Ingest::apply` → … the fact-entry producer, **with 64-bit positions** and back-fill on late enrichment." §2.2 locates the defect in `osv_ingest.h`.

**Defect.** The truncation is in `osv_core.cuh`, not `osv_ingest.h` — `Commitment::src_rev` is declared `uint32_t` at `osv_core.cuh:95`. And there is a **second, unnamed** site: `osv_dispatch.h:95` declares `VerbRow::src_rev` as `uint32_t`, written from the commitment at `:276`. §11's `osv_dispatch.h` row says nothing about it, so a reader following §11 fixes the ingest side and leaves every emitted verb row carrying a truncated deposit clock — which is precisely the row TAPESTRY turns into a tape entry.

Full change set, by function (this is what §11 should say):

| # | file:line | symbol | change |
|---|---|---|---|
| 1 | `osv_core.cuh:95` | `Commitment::src_rev` | → `uint64_t pos_last`; drop the 4 trailing pad bytes. Verified: `sizeof` stays 64 (§0). |
| 2 | `osv_core.cuh:101` | `static_assert` | unchanged, still holds |
| 3 | `osv_ingest.h:284` | `Ingest::fill` | `c.src_rev = (uint32_t)r.rev;` → `c.pos_last = r.rev;` |
| 4 | `osv_ingest.h:337` | `Ingest::apply`, close branch | `cur->src_rev = (uint32_t)r.rev;` → `cur->pos_last = r.rev;` |
| 5 | `osv_ingest.h:317-318` | `Ingest::apply`, guards | both comparisons retarget `pos_last`. **Note the current bug is not mere truncation**: the `uint32` is *promoted* to `uint64`, so a stored-truncated value makes a replay look fresh *and* a stale row look fresh. Both proven (§0). |
| 6 | `osv_ingest.h:221-231` | `Ledger::digest` | hashes `sizeof(Commitment)` raw bytes; the layout change invalidates every stored digest. Re-mint in the same commit. |
| 7 | `osv_dispatch.h:95, :276` | `VerbRow::src_rev` | → `uint64_t pos_last`. **Not named in §11.** |
| 8 | `osv_core_test.cpp:72`, `osv_dispatch_test.cpp:48` | fixtures | both set `c.src_rev`; both suites stop compiling until updated |

**"Back-fill on late enrichment"** is a larger change than the phrase suggests, and §11 gives it four words. Today `Ingest::apply`'s enrichment branch (`osv_ingest.h:293-301`) caches the side row and returns `false` — "enrichment alone never opens or closes an obligation" (`:300`) — and it *discards the class it just matched* (`(void)ec;` at `:295`). `fill` is called only from the three commitment paths (`:329, :346, :351`), never from the enrichment path. So in the normal CDC ordering, where an order row precedes its items and payments, `resolve()` (`:262-273`) finds nothing, and the commitment keeps `amount=0, seat=0, seg=0` **forever** unless the class table happens to update again. On the Olist map (`:388-408`) that is `amount`, `seat` and `segment` — three of the five fields the lattice projects by. Back-fill requires:

- keep `ec`, compute `commitment_id(ec->source, ec->table, *k)`, find it in the ledger, and re-run `fill` for the side table's columns;
- emit a `fact` for the back-fill (TAPESTRY makes every change a row), which advances the cell's `pos_last` and therefore **invalidates its cognitive index** (§2.5) — late enrichment becomes an invalidation event, which §2.5 and §6 do not list;
- a second clock: the monotonicity guard keys on the *class row's* revision, but the back-fill is driven by the *side table's* revision. One guard, two clocks. 64-bit positions do not fix this; a per-source revision map does;
- counter repair: `IngestCounters::conserves()` (`:248-250`) counts an enrichment row as `enriched` and not `updated`; a back-fill that changes the ledger must be counted or the conservation identity breaks — and that identity is falsifier 2's sibling.

**Severity/confidence.** Major / CONFIRMED.

**Fix.** Replace §11's four words with the table above and a one-line note that back-fill is an invalidation event.

---

### F13 · MAJOR · CONFIRMED — §11, §9: "fifteen falsifiers, carried forward" — the count is right, the character is not

**Claim.** §11: "`osv_core_test.cpp`, `osv_dispatch_test.cpp` → **fifteen of the falsifiers, carried forward**." §9's frame: "Falsifiers, **each with its planted lie**." `osv_core_test.cpp:1-2`: "Each carries a planted lie: a deliberately broken variant that MUST fail, because an oracle that only ever passes is measuring nothing."

**Defect.** Fifteen is the right count (7 + 8, both suites built and passed here). But of the fifteen, only **three** assert a lie arm in the pass condition:

| suite | test | line | lie arm asserted? |
|---|---|---|---|
| core | `o_conservation` | `:102` | **yes** — `c.holds() && !lie.holds()` |
| core | `o_projection_order` | `:140` | **no** — `chk(…, identical, n)`; `float_differs` (`:135`) rides the note string only |
| core | `o_slot` | `:161` | no lie |
| core | `o_step` | `:217` | **yes** — `mx < 1e-3 && mx_lie > 1e-2` |
| core | `o_step_batch` | `:246` | **yes** — `identical && caught` |
| core | `o_gate` | `:276` | no lie |
| core | `o_pressure` | `:298` | negative control only |
| dispatch | `o_no_silence` | `:77` | no lie |
| dispatch | `o_budget_degrades_to_hold` | `:99` | negative control only |
| dispatch | `o_warrant` | `:124` | negative control only |
| dispatch | `o_determinism` | `:158` | **no** — the header at `:134` names a planted lie that is not implemented |
| dispatch | `o_two_capacities` | `:178` | no lie |
| dispatch | `o_kappa_demotes` | `:210` | meter control only |
| dispatch | `o_greedy_budget` | `:244` | **no** — the header at `:216` names a planted lie that is not implemented |
| dispatch | `o_evidence` | `:268` | no lie |

Two tests (`o_determinism`, `o_greedy_budget`) carry a comment beginning "THE PLANTED LIE:" above code that plants no lie. The file header's claim is therefore false of twelve of its fifteen tests, and §11 inherits it.

Separately, the fifteen and §9's ten are **different sets**. Mapping §9 → the suites: falsifier 2 (Conservation) = `o_conservation`; falsifier 1 (Replay) is partly `o_projection_order` + `o_step_batch`; falsifier 9 (the clock that does not truncate) tests the *defect these suites contain*. The other seven have no antecedent in the OSV code. "Fifteen of the falsifiers, carried forward" placed in §11 reads as "§9 is mostly already built"; it is not — nine-tenths of §9 is new.

**Severity/confidence.** Major / CONFIRMED (built and read).

**Fix.** In §11, write "fifteen module-level oracles, of which three assert a planted lie; the remaining twelve need lie arms before they carry §9's name." Then add the lie arms — they are cheap, and `o_determinism`'s and `o_greedy_budget`'s are already specified in their own comments.

---

### F14 · MINOR-to-MAJOR · CONFIRMED — `[M]` tags that are narrower than the sentence they support

Detailed in §4's table. The three that change a reader's decision:

- **§4 `[M: o_projection_order, the float lie differs by order]`** — the lie arm is *diagnostic, not asserted* (`osv_core_test.cpp:140`), and the note string's own alternative text is `"no (weak test on this data)"`, which would still PASS. On this box today it printed `yes`, so the property holds; the *test* does not enforce it.
- **§5 `[M: CORTEX]` for 1.67 M vectors in 4.88 ms** — the receipt is `C:/NEW/CONNECTOME_THE-SLIDE-RULE-CONNECTOME_DESIGN-v0.2_CALIBRAN_2026-09-01.md:123` and `…SECOND-BRAIN-RENDERED…:91`: "CORTEX measured 4.88 ms over 1.67M × **384-d** [M]". The blueprint omits the dimension. The estate's own live embedder is 1024-d (`…SECOND-BRAIN-RENDERED…:91`, `:8092 qwen3-embedding-0.6b, 1024-d`), ~2.7× the work per vector. And `C:/NEW/BRAIN_RECONTEXT_FUSOR-AT-CENTER_2026-08-21.md:117` records CORTEX's status as "spec'd; instruments defined; F-WHOLE pending" with the timing tagged **`[M-class]`**, not `[M]`.
- **§5 `[M for the constant]` on 110 ms** — the receipt (`…CONVERGENCE…:152`) says "about **110-130 ms**", the §7 run table gives p50 121 / p95 135 for smoke 1 and "**106-163**" for smoke 3, and the same tree holds a run at `probe_ms: 13834` with `mib_free: 0` (`C:/fusor1/converge/smoke/incident_2026-09-04/t6/fusor_ledger.jsonl:4`) — 100× degradation under VRAM exhaustion. §5 takes the floor of the range and calls it the constant. It is also a **three-seat** figure; whether one TAPESTRY judgment is one probe or three is not stated (see F11), and the factor is 3.

**Fix.** Carry the qualifier into the cell: `[M: 110-130 ms per three-seat judgment, p95 135, ×100 under VRAM pressure]`, `[M: CORTEX, 1.67 M × 384-d, organ status F-WHOLE pending]`.

---

### F15 · MINOR · CONFIRMED — §2.1's canonicalization defeats §2.1's compatibility claim

**Claim.** §2.1: "Field names follow `fusord.cpp`'s tape so its tools read TAPESTRY tapes unchanged," and "The bytes hashed are a canonical serialization: keys sorted…"

**Defect.** `verify_chain.py:22,30` (`C:/fusor1/convergence_tools/`) computes `body = line[:i]` where `i` is the index of `,"prev":"`, then `blake2b(prev.encode() + body)`. It hashes the **literal row prefix as written**. So a TAPESTRY row verifies under fusord's tool only if the row *is* written in canonical form. §2.1's own field table is ordered `pos, t_mono_ns, t_wall_ms, k, cell, cls, by, body, prev, h`, which is not lexicographic (sorted would be `body, by, cell, cls, h, k, pos, prev, t_mono_ns, t_wall_ms`). The blueprint never says the row is emitted in canonical order, and if it is, the presentation order in §2.1 misleads.

**Fix.** One sentence: "the row is written in canonical order, so the literal bytes before `,\"prev\"` are the hashed bytes and `verify_chain.py` works unchanged" — and reorder the §2.1 table to match, or note explicitly that the table is presentation order.

---

### F16 · MINOR · CONFIRMED — §11's "unchanged" holds at function level, not file level, and the row is incomplete

`step_cell` (`osv_core.cuh:187`), `sweep_segment` (`:216`), `gate` (`:254`), `Conservation` (`:276`) and `Lattice` (`:128`) were each checked against the §2.2 cell-row change. **None references `src_rev` or the padding**: `step_cell` reads only *lattice* flags (`L.flags[i] & F_WARRANT`, `:189`) and lattice arrays; `gate` reads only `c.flags`; `Conservation` is pure `int64_t`. So "unchanged" **holds**. Two caveats:

1. The *file* changes, because `Commitment` is defined in it (`:85-101`).
2. The row omits three functions the field fold cannot run without and which also carry forward unchanged: `project_one` (`:159`), `contributes` (`:168`), `slot_of` (`:151`), plus `cell_index`/`to_fix`/`from_fix`. `Dispatch::run` calls `project_one` at `:212` and `contributes` at `:202`.

Also: §11 says these become "the field fold and **the seam**, unchanged." There is no seam in `osv_core.cuh`. "Seam" is fusord vocabulary for the generation/intake boundary (`fusord.cpp:2114-2156`, `…CONVERGENCE…:§6`). The word is doing no work here and collides with a precise term.

---

## 3 · §1 — the diagram against the text

Ten arrows are drawn. **All ten correspond to something the text names** — including the two the review brief suspected were missing: `OP -->|rule entries, quorum keys| TX` is present (matches §3.6), and `W -->|outcomes| TX` is present (matches §3.9). What is missing is different.

**Missing, and each is a component §3 gives its own subsection:**

| # | missing edge or node | the text that requires it |
|---|---|---|
| 1 | **CI → PEER** (the warm read state returning into the judgment) | §3.5 "ensures the cognitive index is warm, forks it, decodes the probe frame" — the diagram is one-way, `PEER --> CI` |
| 2 | **The replay and shadow engine, entirely** — no node, no shadow tape | §3.8, and `replay` is one of the seven API calls (§3.7) |
| 3 | **FOLDS → CI** (the field's ranking drives eviction) | §2.5 "Evicted under memory pressure by the field's ranking"; §5 "the field's ranking decides what is warm" |
| 4 | **FOLDS → PEER/judge** (the license gating what a verb may do) | §3.9 "The license fold reads per verb…"; §2.4 license fold |
| 5 | **No client node at all** | §3.7's seven calls, "a Unix or TCP socket … for external clients" |
| 6 | **TAPE → TX** (head recovery, and constraint load) | §6 "recover head from the last complete entry"; §3.6 constraints arrive as `rule` entries on the tape |
| 7 | **Snapshots / `ckpt` files** — no node | §3.2, §2.1 kind `ckpt` |
| 8 | **`allocate` fold** | §2.4 lists six folds; the FOLDS node lists five — `state, field, grades, license, kappa`. `allocate` is dropped, and §11 maps `Dispatch::run` to it, so it is load-bearing |

**Present but wrong:**

- **The cognitive index appears twice** — inside `PEER`'s label ("…indexes, cognitive index, judges") *and* as the separate `CI` node. The diagram contradicts itself about whether CI is inside the peer. §3.3 says the peer holds the pages; §3.4 makes the manager a component. Pick one and draw the other as a sub-box.
- **`PEER -->|serialize, fork| CI`** labels an internal edge with two §3.7 *client API* verbs. Either the API call passes through here (then the client is missing, per #5) or the label should be internal vocabulary.
- **`PEER --> FOLDS`** is the only unlabeled edge.

---

## 4 · Every `[M]` tag, located

| # | § | the tag | receipt located | verdict |
|---|---|---|---|---|
| 1 | §2.5 | `fork at 0 MiB, m0-0` | `C:/auricle/runs/m0-0-shared-kv-2026-08-07.md:11,38,61` — "forking 3 branches over an 8k trunk added 0 MiB"; "seq_cp is refcount, not copy" | **narrower** — 3 branches, 8k trunk, 2026-08-07, and 0 MiB because the per-seq state is preallocated. Contradicted for the recurrent layers by `…CONVERGENCE…:152` four weeks later. See F1. |
| 2 | §3.5 | `probe_one, ~110 to 130 ms per three-seat judgment on the bench` | `…CONVERGENCE…:152` — "the probe cost of about 110-130 ms per three-seat judgment [R every tape]"; run table `:161-163` (p50 121/p95 135; "106-163") | **matches** the range; §5's use of the floor is the problem, not this tag |
| 3 | §3.5 | `the seam finding of 2026-09-04` | `…CONVERGENCE…:147` and Appendix B.7 `:455` — "a fork can be truncated in the attention layers, but its recurrent state cannot be rewound … a probe that ignored the failure returned stale logits as if they were margins"; the fatal-on-decode rule at `:153` | **matches** the half cited (decode failure is fatal, never a stale margin). The other half — *no judgment as of an earlier instant on this model* — is not cited, and it is the half that bears on §2.5's `pos_last`-keyed index and §3.8's replay |
| 4 | §4 | `o_projection_order, the float lie differs by order` | `osv_core_test.cpp:110-141`; lie computed at `:129-135`, **not asserted** at `:140` | **narrower** — the property held when I ran it, but the test does not enforce it. See F14. |
| 5 | §5 | `17,432 B marginal, smoke 1 checkpoints` | `…CONVERGENCE…:178`; the two checkpoints at `…:449-450` (60,257,248 B @ 434 tok; 61,616,944 B @ 512 tok) | **matches** the marginal exactly, **omits the same sentence's** "about 52.7 MB fixed (the recurrent state)". See F1. |
| 6 | §5 | `the estate's 160k-on-16 GB receipt is the precedent` (inside a `[BUDGET]`) | referenced in the corpus (e.g. `C:/fusor1/converge/primers/intellect-argument.chunks/chunk-001.md:613`, "past 160k with KV quantization"); `BRAIN_RECONTEXT…:133` files it under **`[OA]` operator-attested**, not `[M]` | **narrower** — operator-attested, and it is a *context-length* receipt, not a warm-set receipt |
| 7 | §5 | `CORTEX` — 1.67 M vectors in 4.88 ms | `C:/NEW/CONNECTOME_THE-SLIDE-RULE-CONNECTOME_DESIGN-v0.2_CALIBRAN_2026-09-01.md:123`; `…SECOND-BRAIN-RENDERED…:91` — "CORTEX measured 4.88 ms over 1.67M × **384-d** [M]"; status at `BRAIN_RECONTEXT…:117` "spec'd … F-WHOLE pending", tagged **`[M-class]`** | **narrower** — dimension omitted; organ unbuilt |
| 8 | §5 | `[M for the constant]` on 110 ms per judgment | same as #2; degraded case `C:/fusor1/converge/smoke/incident_2026-09-04/t6/fusor_ledger.jsonl:4` (`probe_ms:13834`, `mib_free:0`) | **narrower** — floor of a range, three-seat, VRAM-conditional |
| 9 | §4 | `o_projection_order, the float lie differs by order` (Determinism) | same as #4 | **narrower** |
| 10 | §7 | `the gate exists in fusord` | `fusord.cpp:436` (`module_gate`), list at `:435`, enforcement at `:1654-1663` | **narrower, and self-contradicting** — the gate exists and is `[R]` not `[M]`; it is Windows-only (`:474-481`), boot-only, bypassable (`:1081`), and it **forbids `ws2_32.dll`**, which the peer's own sockets require. See F2. |

Not found anywhere, and searched for: no receipt exists for §8-R5's "**the wallet-cap pattern**" — it is named as a reuse but points at no artifact in `C:/fusor1`, `C:/NEW` or `C:/55555`.

---

## 5 · §11, claim by claim

| §11 claim | symbol | file:line | verdict |
|---|---|---|---|
| `Tape` → the tape store's entry writer | `struct Tape`, `open`, `put` | `fusord.cpp:859, 863, 902` | **narrower** — no `pos`, no segments, no canonicalizer, `fflush` not `fsync` (`:906`). Torn-row recovery (`:863-899`) is excellent and does carry. F7 |
| `chain_hash` → the chain | `chain_hash` | `fusord.cpp:312` | **holds** — `blake2b256(prev_hex ‖ body)`, exactly §2.1's construction; verified against Python in `…CONVERGENCE…:46` |
| `Blake2b` | `struct Blake2b` | `fusord.cpp:235-309` | **holds** — 6/6 vectors match `hashlib` (`…CONVERGENCE…` B.1 `:387-395`) |
| `write_atomic` | `write_atomic` | `fusord.cpp:380` | **holds** |
| `replace_file` | `replace_file` | `fusord.cpp:362` | **holds** — 20×5 ms retry against the measured Windows rename hazard (`…CONVERGENCE…:53`, B.4) |
| `LaneTail`, the lane contract → **the v0 subscribe transport** | `struct LaneTail`, `parse` | `fusord.cpp:579, 672` | **false as reuse** — a file tailer with no socket; fusord's own gate forbids one. The *frame grammar* and the cursor+prefix-hash resume (`:643-661`) do carry. F6 |
| `probe_one` → the judge runtime | `Kernel::probe_one` | `fusord.cpp:2048` | **narrower** — exists and forks/probes/removes as described (`:2049-2055`), but returns one scalar `emit − hold` (`:2054`); five verbs need more. F11 |
| `speak` | `Kernel::speak` | `fusord.cpp:2068` | **narrower** — it is a *sentence generator with an interrupt seam*, not a verb emitter; TAPESTRY's judge emits a row, not prose. No stated role |
| the manners layer → the judge runtime and the CI manager | `looks_like_acceptance` `content_overlap` `near_dup` `resolve_on_line` | `fusord.cpp:1837, 1890, 1918, 1877` | **false as reuse for this purpose** — all four are English-conversation-specific: a hardcoded acceptance-phrase list (`:1838`), an English negator list (`:1839`), an English stopword list (`:1891-1899`), 60 %-word-overlap near-dup (`:1929`). Nothing maps to a rendered obligation row. F(see below) |
| `checkpoint`, `restore` → the CI manager | `Kernel::checkpoint`, `Kernel::restore` | `fusord.cpp:1511, 1574` | **narrower** — the *pattern* (tmp + write-through rename, `.prev` generation, `.meta` written last, guards checked on restore) carries beautifully. The primitive does not: `llama_state_seq_save_file` (`:1516`) is one file per sequence. F4 |
| `serve_hash` | `serve_hash`, `SERVE_HASH_PIN` | `fusord.cpp:982, 992` | **holds** — and the boot-time assert (`:1612-1627`, exit 2 on drift) is the right precedent for §2.3's template pin |
| `model_identity` | `model_identity` | `fusord.cpp:484` | **holds** — sha256 with a (path,size,mtime) cache; exactly §0 law 6's "identified by weight hash" |
| `module_gate` | `module_gate` | `fusord.cpp:436` | **narrower and self-contradicting** — see F2 |
| `Commitment` → `Cell` with `pos_last` | `struct Commitment` | `osv_core.cuh:85-101` | **holds** — verified: the blueprint's `Cell` is exactly 64 B, `pos_last` at offset 32 (§0) |
| `Lattice`, `step_cell`, `sweep_segment`, `gate`, `Conservation` → the field fold **unchanged** | | `osv_core.cuh:128, 187, 216, 254, 276` | **holds** at function level — none references `src_rev` or the padding. Incomplete: `project_one` (`:159`), `contributes` (`:168`), `slot_of` (`:151`) are also required and also unchanged. "the seam" names nothing in this file. F16 |
| `SchemaMap` → the class map with horizons and templates | `struct SchemaMap`, `pin` | `osv_ingest.h:149, 165` | **narrower** — the pin is blind to `open_when.op`, `close_when.op`, `cls`, `flags` (proven, §0). Fix before extending. F3 |
| `Pred` | `struct Pred`, `eval` | `osv_ingest.h:103, 108` | **holds** — five ops, deliberately small; the comment at `:97-98` is the right argument for §10.5 |
| `Ingest::apply` → the fact-entry producer, 64-bit, back-fill | `Ingest::apply` | `osv_ingest.h:288` | **narrower** — the 32-bit fix spans 8 sites across 3 files incl. one §11 misses; back-fill is a new code path plus a second clock plus a CI invalidation event. F12 |
| `Dispatch::run` → the allocate and kappa folds | `Dispatch::run` | `osv_dispatch.h:190` | **holds** — four passes, deterministic id-order walk (`:203`), one verb per open commitment, kappa accrued at `:277-295`. Note `VerbRow.src_rev` at `:95, :276` is a second truncation site |
| `look_value` | `Dispatch::look_value` | `osv_dispatch.h:183` | **holds** — greedy submodular ranking with the overdue doubler |
| `ClassKappa` | `struct ClassKappa` | `osv_dispatch.h:145` | **holds** — but monotone, which breaks §9.5. F9 |
| the two test files → **fifteen of the falsifiers** | | `osv_core_test.cpp`, `osv_dispatch_test.cpp` | **narrower** — 15 is the right count and both suites pass 15/15 here; only 3 assert a lie arm; 9 of §9's 10 have no antecedent. F13 |

---

## 6 · §9 — the ten falsifiers as executable tests

Each row: the one-line test design, and the planted lie that must fail.

| # | falsifier | executable test design | the planted lie | executable? |
|---|---|---|---|---|
| 1 | **Replay** | Build a fixed tape of N entries; run every fold from genesis in two separate processes; `memcmp` each fold's output buffer and each shadow tape byte for byte | Swap one fixed-point accumulator in the field fold for `float` — verbatim the pattern at `osv_core_test.cpp:129-135` — and assert the two rebuilds **differ** | **yes**, and the lie arm already exists as code |
| 2 | **Conservation** | `o_conservation` verbatim (`osv_core_test.cpp:80-103`): project a 20,000-row ledger, assert `ledger_fix == lattice_fix` exactly | Drop one commitment's contribution (`if (k != 777)`, `:94`) and assert `!lie.holds()` | **yes — already written, already passing** |
| 3 | **The hold that never vanishes** | Append H `hold` entries among F facts; snapshot; compact; rebuild from snapshot + tail; assert `count(hold) == H` and every `margin`/`reason` byte-identical to the pre-snapshot fold | A snapshot writer that serializes only entries whose `k` is in `{fact}`; assert the rebuilt hold count `< H` | **yes** |
| 4 | **The refusal that never silences** | Submit W writes of which R violate a registered constraint; assert appended `refuse` entries `== R`, each carrying a reason drawn from a closed enum (the `DReason` pattern, `osv_dispatch.h:56-68`) | A constraint evaluator that returns `false` without emitting; assert refuse count `== 0 ≠ R` | **yes** |
| 5 | **The inverse that unwinds** | Take an order-independent digest of every state-bearing fold (the XOR-of-record-hashes pattern, `osv_ingest.h:221-231`); apply saga S; apply its inverses in reverse; assert the digest returns | An inverse set that omits cell 2 of 2; assert the digest does **not** return | **yes, but must be re-scoped** — false as written for the kappa and grades folds (F9). Second arm: assert those two gained exactly the saga's + inverse's rows and no others |
| 6 | **The index that is a function** | Judge cell C, record margin m1; `seq_rm` its index; rebuild from the tape at the same `pos_last`; judge again → m2; assert **m1 == m2 bitwise** (the margin is a deterministic logit difference, `fusord.cpp:2054`, not a sample) | A template that interpolates wall time into the rendered bytes; assert m1 ≠ m2 | **yes — and stronger than the blueprint's version.** "within the run's noise" is unfalsifiable without a pre-registered noise floor. Note this test is `…CONVERGENCE…` §10 **T8**, which that document marks "never exercised, `[X]` today" — the estate has owed this receipt since 09-04 |
| 7 | **The fork that costs nothing** | **Not** free-memory-before-vs-after — that returns 0 regardless (F8). Instead: build contexts at `n_seq_max ∈ {1,2,4,…,N}` over an identical trunk and assert the resident footprint is flat in `n_seq_max` | Eager copying — **but the lie arm is unnecessary, because the true arm already fails on this model** at ~52.7 MB per slot | **yes, and it should be expected to FAIL** — which is the receipt worth having |
| 8 | **The quorum that holds** | Register M judge keys; submit an irreversible write signed by M−1 with one dissent; assert a `refuse` entry with reason `quorum` and assert no `fact` was appended | A verifier that returns true on the first valid signature; assert the write appended | **yes** |
| 9 | **The clock that does not truncate** | Feed revisions spanning 2^32 (2^32−10 … 2^32+10); assert (a) a replay of an identical revision is a duplicate no-op and (b) a lower revision is refused | `uint32_t src_rev` (`osv_core.cuh:95`) | **yes — I ran both arms today** (`cell_check.exe`, §0). At rev 4294979641 the truncated store makes the replay look fresh *and* the stale row look fresh. ~20 lines |
| 10 | **Both sides graded** | Build a grade set with `n_eff` above the floor on the act side and below it on the hold side; assert the license fold refuses to widen the band | Pool `n_eff` across verbs before the floor test; assert it licenses | **yes**, once the floor is a declared constant — the precedent is `GateParams::n_eff_floor = 175.0f` (`osv_core.cuh:242`) and the refusal path at `:257` |

**Summary:** ten of ten are executable. Two need restating first (5 must be scoped to state-bearing folds; 7 must be re-expressed against `n_seq_max` or it cannot fail). One is already written and passing (2). One I ran today (9). One is an outstanding estate debt (6 = T8).

---

## 7 · §8 — rungs, gates, and what is missing to make each buildable

Gates first: **six of seven gates are measurable receipts.** R0's "two cold replays bit-identical" is diffable; R1's conservation is `o_conservation`; R2's "batching bit-identical" is `o_step_batch`; R3's "index is a function" is falsifier 6; R5's "one dissenting key" is falsifier 8; R6's "leader kill mid-saga leaves the folds whole" is a chaos test with a fold digest. The exception is **R3's "the fork that costs nothing," which is not measurable as stated** (F8), and **R2's "scan at CORTEX speed," which has no code and a receipt at a different dimension** (F14).

| rung | missing before it is buildable |
|---|---|
| **R0** | No SQL engine (§10.4 open — see §9 below). No wire format beyond "length-prefixed JSON frames": no message schema, no error model, no version negotiation, no back-pressure rule. No canonicalizer — the requirement is stated (§2.1) and no code in the estate implements one. No fsync and no group-commit design (F7). No `pos` assignment, no segment rotation. No constraint expression language: §3.6 lists six *kinds* of constraint and no syntax, no evaluator, no type rules — and it should be the same expression language as the query layer (see §9). No test harness for "two cold replays": needs a deterministic tape generator (`gen_test.py` in `convergence_tools/` is the seed) and a fold-digest differ |
| **R1** | The **incremental** side of every fold is 100 % new. `project()` in the test (`osv_core_test.cpp:40`) is a full recompute; nothing in the estate maintains a fold on append. R1's own gate ("the fold verifies cold against incremental") therefore cannot be met by the named reuse. §10.8 half-acknowledges this; it should be promoted. Also: the full 32-bit change set (F12), back-fill (F12), and a pin fix (F3) |
| **R2** | No kernels exist (F5). Missing: launch shape (block-per-segment, thread-per-cell, two colour passes), device allocation for the cell table and lattice, `int64_t atomicAdd` for the fixed-point projection, a host↔device bit-identity harness, and — for the second gate — an embedding store, a vector layout, a quantization choice, and a GEMM. The CORTEX comparison also needs its dimension fixed (F14) |
| **R3** | No template language (§10.5 — see §9 below). No `serialize` implementation anywhere: fusord renders a fixed probe frame from string literals (`fusord.cpp:2051`), which is not a template. No eviction policy code, no refcounting, no page accounting. And the two blockers: `n_seq_max` (F4) and the recurrent state (F1). **R3 is not buildable as specified until F1 is answered** — it is the rung to re-scope, not the rung to schedule |
| **R4** | Verb arity unresolved (F11). No horizon values (§10.6 — must be measured from history *before* R4, so it is a prerequisite, not a parallel task). No shadow-tape format. No `n_eff` estimator and no band definition. The manners-layer reuse gives nothing here. And R4's gate inherits F10's 360× replay-budget error |
| **R5** | The "reuses" cell names no artifact: no signing library is chosen, and "the wallet-cap pattern" has no receipt in the estate (searched). Missing: key custody (§10.7 open), the reversibility classification rule, the effector's outbox with a take-back window (§10.3 open — and §10.3 does not say who owns it, which is a design decision, not a question). The canary and stratum need the license fold from R4 |
| **R6** | Names no Raft implementation. Missing: the saga↔replication interaction — a saga is atomic at the transactor (§4) and Raft commits per-entry, so either the whole saga is one Raft entry (bounding saga size) or the folds must tolerate a partial saga at a majority. That is the design R6's gate tests, and it is unspecified |

### Effort, in engineer-weeks

**Assumptions.** One senior C++/CUDA engineer already fluent in this estate; 40 h/week; this box's toolchain (MSVC 14.44 + nvcc, both verified present); reviewed but no separate QA function; "done" means the rung's gate emits a dated receipt into `C:/TAPESTRY/receipts/`; no new hardware; the open questions in §10 are answered *before* the rung that needs them (their answering time is counted in the rung).

| rung | eng-weeks | dominant cost |
|---|---|---|
| R0 | **10** | Constraint language + evaluator (2), canonicalizer (1), saga/quorum plumbing (1.5), segment store + snapshot (1.5), socket + framing + client (1), durability + crash tests (1), replay-equality harness (1), integration (1). Reuse saves ~3 days |
| R1 | **6** | Incremental folds + `verify_fold` (2), state fold from tape facts (1.5), Olist conformance (1), the full 32-bit change set + back-fill + pin fix (1), falsifiers (0.5) |
| R2 | **6** | Kernels from scratch (2), bit-identity harness (1), embedding store + scan + recall check (2), device memory management (1) |
| R3 | **12 + risk** | Conditional on F1 being answered. Template renderer + pin (2), index manager (3), llama.cpp integration incl. paging because 15k sequences do not fit (4), NVMe checkpoint per cell (2), falsifiers (1). **Add 4-6 if the judge model must change**; if it must, R3 restarts |
| R4 | **10** | Grade fold with per-verb horizons (2), license fold with `n_eff` both sides (2), shadow tape + replay engine (2), judge registration + pin enforcement (1), render→probe→verb loop (1), horizon measurement from history (1), graduation receipt (1) |
| R5 | **8** | Effector with caps + outbox + take-back (3), keys + N-of-M (2), reversibility classification (1), canary + stratum (1), falsifier (0.5) |
| R6 | **6** | Adopt a Raft (4), saga-safe leader-change tests (2) |
| | **58 weeks ≈ 1.1 engineer-years** | R0-R2 (22 w) is a defensible first milestone with a real receipt at each rung |

The estimate assumes R3 is re-scoped rather than attempted at 15,000 warm cells. If it is not, R3 does not converge and the number is meaningless.

---

## 8 · §12 and §2-§7 — terms used inconsistently

| term | the collision |
|---|---|
| **deposit clock** | Three distinct objects carry this name. §2.1: `pos` is "the deposit clock". §2.2: `pos_last` is "deposit clock: the tape position that last touched this cell". `osv_core.cuh:95` / `osv_ingest.h:28`: `src_rev`, the *source system's* LSN, is "the deposit clock". These are the tape's clock, the cell's watermark, and the wire's clock. §12 defines only "Position" |
| **cognitive index / read state / KV pages** | §2.5 title and §12 say "cognitive index"; §2.5's value is "a list of KV pages holding the judge's **read state**"; §1's CI node is labelled "KV pages per cell"; §9.7 says "N forks of one **read state**". Four names, one object |
| **verb / decision / hold** | §12: "**Verb**: hold, act, work, fetch, escalate" — hold is a verb. §2.1: `hold` and `verb` are **separate kinds**. §2.4's grades fold takes "`verb`, `hold`, and outcome `fact` entries" — so a hold is not a verb entry. §2.1's `grade` body has both `decision_pos` and `verb`. The license fold "reads per verb" (§3.9) and must therefore know whether holds are in the denominator. **This is a schema question, not a wording question** |
| **seat / judge** | §2.1: `by` is `seat:<id>` **or** `judge:<hash>@<pin>` — two different provenances. §2.2: `Cell.seat` is a uint32. `osv_core.cuh:94`: seat is "who holds it: a worker id". But in fusord, a *seat* is a model persona (`MINDS`, `fusord.cpp:960`: SPEAKER/SKEPTIC/SENTINEL), which is TAPESTRY's *judge*. §10.10 says "the seat vocabulary must not move" — but it has already moved, twice, and §12 does not define "seat" at all |
| **checkpoint / snapshot / ckpt** | §2.1: kind `ckpt` is "a fold checkpoint manifest". §3.2: "a **snapshot** is a `ckpt` entry plus a fold checkpoint file". §3.4: cognitive-index pages are "**checkpointed** to NVMe" — an unrelated artifact (KV bytes, not a fold), reusing fusord's `checkpoint`. §12 defines only "Snapshot" |
| **template pin** | §2.1: `judge` body carries `template_pins[]` (plural, per judge). §2.2: the cold record holds "the **class** template pin". §2.5: the index key holds one `template_pin`. §2.3: "The map's pin (`SchemaMap::pin`) is extended to cover … templates" — so is the template pin *inside* the map pin or beside it? The index key (§2.5) contains `template_pin` and `judge_pin` but **not** the map pin, so a map change that does not move the template pin leaves warm entries valid |
| **field / lattice** | §2.4 names the fold "field" and its output "the lattice"; §12: "**Field**: the lattice fold and its relaxation". Usable, but §1's FOLDS node lists five folds where §2.4 lists six — `allocate` is missing from the diagram |
| **seam** | §11 says `osv_core.cuh` becomes "the field fold and **the seam**, unchanged". There is no seam in that file; "seam" is fusord's term for the generation/intake boundary (`…CONVERGENCE…` §6) |
| **writ / rule / constraint** | §0 law 4 "the writ is a constraint"; §3.6 "the constraint layer, which is the writ"; §2.1 kind `rule` carries "writ, constraint, template, horizon, license changes". §12 defines none of the three |
| **§12 omissions** | Terms that are load-bearing in §2-§9 and absent from the glossary: **seat, writ, rule, constraint, transactor, effector, margin, n_eff, band, stratum, canary, hold (as a kind), refuse, deposit clock, shadow tape, lane, delta** |

---

## 9 · The two open questions the blueprint leaves (§10.4, §10.5)

### §10.4 — the SQL engine: **an embedded columnar engine on CPU for v0, and specifically DuckDB**

**Recommendation.** DuckDB, in-process, single-threaded, for v0. Reject "one engine throughout."

**Why.**
1. **Shape.** It is a single amalgamation, MIT, no server, builds with the MSVC already on this box. That matches §1's "two processes, one machine" exactly; a client-server database adds a third process and a socket the peer is forbidden to hold (F2).
2. **It settles §10.8 by making it moot.** A vectorized columnar scan over a 50,000-row cell table (3.2 MB, §5) is tens of microseconds. Full recomputation per append at organizational event rates is fast enough with three orders of margin, so v0 needs no differential maintenance — which is what §10.8 suspects ("likely yes for v0") and this makes concrete.
3. **`query(sql, as_of_pos)` falls out of the storage format.** If a fold checkpoint file (§3.2) *is* a Parquet file, the same engine reads a snapshot at a position with no extra machinery, and §3.2's "a cold start can begin at a position instead of genesis" becomes a file path rather than a subsystem.
4. **One expression language for the writ and the query.** §3.6's check expressions and §3.7's `query` should not be two dialects — if they are, the constraint layer and the query layer can disagree about what a row means, and the writ is whichever one you asked. Registering the constraint evaluator as scalar UDFs in the same engine gives one parser and one semantics.
5. **The cost, stated.** You must pin `SET threads=1` and forbid float aggregates inside any fold. DuckDB's parallel hash aggregate is order-dependent on floats — exactly the failure `osv_core.cuh:26-29` was written to prevent — so §4's determinism law has to be enforced *against* the engine's default, by a registered-fold review rule, not assumed. That is a real obligation and it belongs in §4.

**Rejected alternatives.** SQLite: row-store, no Parquet, and the license and grade folds are analytical scans over years of tape, which is the shape it is worst at. cuDF-class kernels on the card as the *authoritative* fold engine: forbidden by §0 law 5 — the peer is a cache, and putting the fold of record on a re-derivable device inverts the law. Keep cuDF for v2 as a *cache-side accelerator* whose results are checkable against the CPU fold, which is a receipt worth having anyway.

### §10.5 — the template language: **a fixed renderer per class in code, pinned by the hash of its source, for v0**

**Recommendation.** Code, not declarative, for v0. Revisit at v2 when the number of classes makes the rebuild cycle expensive.

**Why.**
1. **The restriction is the work.** §2.3 forbids a template from including wall time "or anything not derivable from the tape at the cell's `pos_last`." A declarative language must be restricted until it can express only that — and restricting an expression language *is* writing the renderer, plus an interpreter, plus a proof that the interpreter is deterministic. The estate has already written the argument, in the code the blueprint is reusing: `osv_ingest.h:97-98` — "A predicate language that can express anything becomes a workflow language, and a workflow language is the thing this design exists to delete."
2. **The pin is cheaper and stronger in code.** §10.5 concedes "the pin is required either way." A code renderer's pin is the sha256 of its translation unit plus the class id, computed at build and asserted at boot — which is precisely `serve_hash()` / `SERVE_HASH_PIN` (`fusord.cpp:982, 992, 1612-1627`), a mechanism that already exists in the estate, has a verified receipt (`…CONVERGENCE…:47, 50`), and refuses to load on drift. That is a stronger guarantee than hashing a template file, because it covers the renderer's *behavior*, not its text.
3. **Determinism is the falsifier.** §9.6 turns on the rendered bytes being a pure function of the tape. A fixed renderer has no interpreter to be non-deterministic in.
4. **The failure modes are asymmetric.** A code renderer that must later become declarative is a refactor with a mechanical migration. A declarative renderer that silently admits wall time poisons the cognitive index of every cell in a class, and §9.6 only catches it if the eviction test happens to run on an affected cell.
5. **The usual objection does not bind here.** "Code means a rebuild and a deploy to change a template" — true, but §2.3 already makes a template change a `rule` entry with quorum, and §6 already budgets a mass invalidation staged by ranking whose cost is printed. The deploy rides an event the design already has. It is not a new class of event.

**Concede one thing in §10.5's favor:** if a class's template must be authored by someone who cannot ship C++, the declarative path becomes necessary and should be scoped as "a restricted expression over the cell row and its named neighbors, with no function that is not a pure fold over tape-derived values" — and even then, pin the *compiled* form, not the source.

---

## 10 · Top three changes

1. **Put the 52.7 MB per-sequence recurrent state into §5 and re-derive the warm-set rows** — it turns 15,000/45,000 warm cells into ~2,200/~2,350, makes §9.7 falsifiable instead of vacuous, and forces the one design fork the blueprint has not asked: must the judge be a recurrent hybrid?
2. **Rewrite §8-R2 and §8-R3's "reuses" cells to name what exists** — R2 reuses `OSV_HD` leaves, not kernels (there are none); R3 reuses fusord's *checkpoint pattern*, not a page store, and cannot use one llama sequence per cell.
3. **Fix `SchemaMap::pin` before extending it, and reconcile §7's module gate with §3.7's sockets** — the first is a proven blind spot on `F_WARRANT` and the predicate operator (so the writ can change without invalidating anything); the second is a cited `[M]` that forbids the process the blueprint describes.

---

## 11 · Open questions I would add to §10

11. **Must the judge be a recurrent hybrid?** The 52.7 MB per-sequence state (`…CONVERGENCE…:178`) is the binding constraint on the warm set and it is a property of the *model class*, not the design. A pure-attention judge of comparable quality makes §2.5's copy-on-write fork true as written and multiplies the warm set ~7×. Decide before R3.
12. **What is the maximum `n_seq_max` this llama.cpp build accepts, and what does the context cost at that value?** A one-hour measurement that decides whether R3's "one sequence per warm cell" survives at all. fusord uses 8; m0-0's largest was 3.
13. **Does the judge choose among five verbs, or does a deterministic gate?** `probe_one` returns one scalar; `osv::gate` returns five verbs from thresholds. §3.9 grades "per verb" and cannot currently attribute a wrong verb to the weights or to the thresholds.
14. **Are holds `verb` entries or `hold` entries, and are they in the license denominator?** §2.1 and §12 disagree; the grade and license folds both depend on the answer.
15. **What is the noise floor of a margin, and is it zero?** §9.6 says "within the run's noise"; `fusord.cpp:2054` reads a deterministic logit difference, which should be bit-identical across a rebuild. If it is, say so and strengthen the falsifier. The estate has owed this measurement since 2026-09-04 (`…CONVERGENCE…` §10, T8, "never exercised, `[X]` today").
16. **What is the canonical byte order of a tape row, and does `verify_chain.py` still pass on it?** §2.1 requires sorted keys and claims fusord's tools work unchanged; `verify_chain.py:22` hashes the literal row prefix. Both can be true only if rows are written canonically.
17. **Who owns the outbox and the take-back window?** §10.3 asks *whether* the pattern is used and names two candidate owners; it does not decide, and R5 cannot start until it does.
18. **What does the second clock look like?** Back-fill on late enrichment is driven by the side table's revision while the monotonicity guard keys on the class row's — one guard, two clocks (F12). A per-source revision map is the likely answer and it changes the cell's cold record.
19. **Is the effector's own outcome a `fact`, and by what provenance?** §1 routes outcomes only through the world; §7 says the effector "prints every byte that leaves," and that record has no destination in the data model.
20. **What is the group-commit design?** §3.1's 10,000 entries/s `[BUDGET]` and §4's per-entry fsync durability are not simultaneously reachable with the serial loop as described.

---

*QC-7, 2026-09-08. Every file:line above was read from disk this session; every number tagged CONFIRMED was either quoted from a dated receipt or produced by a binary I compiled and ran, whose sources are in `C:/TAPESTRY/qc/scratch/build/`. Where this report and a receipt disagree, the receipt wins and this report is the defect.*
