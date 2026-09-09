# QC-1 · THE TAPE, THE CHAIN, DETERMINISM, REPLAY, SNAPSHOTS
### Adversarial review of `TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1_2026-09-08_FABLE5-1.md`, §2.1 · §3.1 · §3.2 · §4 · §6 · falsifiers 1, 3, 4, 9

**2026-09-08 · Claude Opus 5 · reviewer 1 of 7.** Sources read in full: the blueprint; `C:/fusor1/converge/src/fusord.cpp` (2,761 lines, in four chunks); `C:/fusor1/FUSOR_KERNEL_CONVERGENCE_2026-09-04_FABLE5-1.md` (§2, §5.2, §6.4, §7.3, §9, Appendix B); `C:/Websites/aorta-site/_upload/osv_core.cuh`; `osv_ingest.h`; `osv_core_test.cpp`; and `C:/fusor1/convergence_tools/verify_chain.py` and `tape_rows.py` (the tools the blueprint claims read TAPESTRY tapes unchanged). Executable probes are in `C:/TAPESTRY/qc/scratch/tape/probe.py`, `probe2.py`, `probe3.py`; every CONFIRMED finding below was either read out of the named source line or produced by running one of those.

---

## 1 · Verdict in three sentences

The tape's laws are right and the chain is the right primitive, but the blueprint's three load-bearing sentences about it — the canonicalization rule in §2.1, "the same construction as `chain_hash` in fusord", and "two cold rebuilds are bit-identical" in §4 — are each false as written, and the first two are false in ways that cannot be patched with wording because they are structurally impossible: a key-sorted canonical object cannot carry the hash of itself, and fusord's verifier hashes the literal on-disk bytes rather than any canonical form. Falsifier 9's proposed fix does not close falsifier 9: `pos_last` is the store's clock and the defect it replaces (`src_rev`) was the world's clock, and the 64-byte cell has zero bytes left to hold both. Under Raft (§3.2 v1) the design as written assigns `pos` and `h` before commit, so a leader change reuses a position and forks the chain, which makes R6's own receipt gate unsatisfiable.

---

## 2 · Findings, most severe first

---

### F1 · §2.1 — the canonicalization rule cannot produce a self-carrying entry, and does not match fusord
**Severity: BLOCKER · Confidence: CONFIRMED**

**Claim.** "`h` | hex64 | `blake2b256(prev ‖ canonical(body_and_header))`, the same construction as `chain_hash` in fusord" and "The bytes hashed are a canonical serialization: keys sorted, no whitespace, integers as decimal, floats as their shortest round-trip decimal."

**Defect, part 1 — the construction is not fusord's.** `fusord.cpp:312-318`:

```cpp
static std::string chain_hash(const std::string& prev_hex, const std::string& body) {
    Blake2b b; b.init(32);
    b.update((const uint8_t*)prev_hex.data(), prev_hex.size());
    b.update((const uint8_t*)body.data(), body.size());
```

and `fusord.cpp:902-907` (`Tape::put`):

```cpp
const std::string h = chain_hash(prev, body);
std::fprintf(f, "%s,\"prev\":\"%s\",\"h\":\"%s\"}\n", body.c_str(), prev.c_str(), h.c_str());
```

Three differences the blueprint does not name: (a) `prev` is the **64-character hex ASCII string**, not 32 raw bytes — §2.1 writes only "`prev ‖`", and a v1 "length-prefixed binary with the same fields" would naturally concatenate 32 raw bytes and silently change every hash in the store; (b) `body` is the **literal emitted row prefix in insertion order**, never sorted, never canonicalized — `{"k":"tick","ms":12,"gap_s":3`; (c) `prev` and `h` are **appended after** the hashed bytes and are therefore *not inside* them, whereas §2.1's `body_and_header` reads as though the header (which contains `prev`) is inside, hashing `prev` twice.

**Defect, part 2 — `h` cannot be a member of a key-sorted canonical object.** Sorting §2.1's ten field names gives `body, by, cell, cls, h, k, pos, prev, t_mono_ns, t_wall_ms`. `h` lands at index 4 of 10 (probe.py §2). A writer cannot emit `h` before it has hashed the five fields that follow it. So either the on-disk bytes are not canonical — in which case a byte-level verifier fails — or `h` is not a member of the canonical object, which §2.1 must say and does not.

**Defect, part 3 — the estate's verifier is byte-level and will reject a canonicalized tape.** `C:/fusor1/convergence_tools/verify_chain.py`:

```python
i = line.rfind(MARK.encode())          # MARK = ',"prev":"'
body = line[:i]
want = hashlib.blake2b(prev.encode() + body, digest_size=32).hexdigest()
```

It never parses the JSON, never sorts keys, never re-serializes. It hashes the raw on-disk bytes preceding `,"prev":"`. A TAPESTRY writer that hashes a canonical re-serialization while emitting a different byte order produces a tape on which this tool reports a HASH MISMATCH on every row. The convergence receipt §2 that the blueprint leans on ("K4 tape chains … 0 breaks, 0 seams") is a receipt *for the byte-level construction*.

**Defect, part 4 — "keys sorted, no whitespace, integers as decimal, floats as their shortest round-trip decimal" is not a canonical form.** Measured, `probe.py` §3-§4 and `probe2.py` §B:
- **Float rendering.** Shortest-round-trip fixes the *digits*, never the *rendering*. Python writes `1e-05`; ECMAScript's `Number::toString` — which RFC 8785 (JCS) mandates verbatim — writes `1e-5`. Python switches to exponent form at `1e16`; ECMAScript at `1e21`; C++ `std::to_chars(chars_format::general)` at a third threshold. Three conforming implementations, three hashes.
- **Unicode.** `{"note":"aéb"}` serialized with raw UTF-8 and with `\u00e9` both satisfy "keys sorted, no whitespace". Their BLAKE2b digests are `c03690ad74c588b0…` and `6d714f42591f1f97…` (probe2.py §B). The rule does not choose.
- **Integers.** `pos` is `uint64`. `2^63 + 12345` round-trips through an IEEE-double JSON reader as `9223372036854788096` — silently wrong, no error raised (probe.py §4). Every reader whose JSON numbers are doubles (JavaScript, many Go/Java default paths) reads a different `pos` than the writer wrote, and therefore a different canonical body.
- Not addressed at all: `-0.0` vs `0.0`; NaN/Inf (JSON has no encoding, and `margin` is a float); duplicate keys; the ordering key (UTF-16 code units per RFC 8785, vs UTF-8 bytes, vs codepoints — these differ for anything above U+FFFF); nesting depth; empty objects.

"Two implementations that disagree on canonical bytes will disagree on `h`, which is the point: the chain is the conformance test" is the argument that the spec need not be precise because the test will catch it. It will not: it catches disagreement only after two implementations have each written to a production tape, and it cannot say which one is right.

**Fix.** Choose one, and say which:
1. **Keep fusord's construction and delete the word "canonical."** State the law as: *the hashed bytes ARE the on-disk bytes of everything preceding `,"prev":"` in the row, byte for byte.* Verification becomes `read the line, split at the last `,"prev":"`, hash`. `verify_chain.py` already implements it. No JSON parser is in the trust path. Field order is the writer's, and a reader never needs to reproduce it. This is the cheapest correct answer for v0 and it preserves every existing receipt.
2. **For v1, a binary canonical form is the honest answer** — and the blueprint should say so now rather than promise "the same fields" in binary. Concretely: fixed header (`pos` u64 LE, `t_mono_ns` u64 LE, `k` u8 enum, `cell` u64 LE, `cls` u32 LE), then `by` and `body` as length-prefixed CBOR with the deterministic-encoding profile (RFC 8949 §4.2.1: shortest-form integers, definite lengths, keys sorted by encoded bytes), floats as raw IEEE-754 binary64 (no decimal rendering to disagree about, `-0.0` and NaN distinguishable, no 2^53 cliff), `prev` as 32 raw bytes. `h = blake2b256(prev_bytes ‖ header ‖ body)`. Then state explicitly that **the v1 chain does not continue the v0 chain**: the last v0 entry's `h` is carried into the v1 genesis entry as a `body` field and the v0 tape is retained as its own chain. §2.1's "the same fields" hides a hash-space change that would otherwise be discovered on migration day.
3. If JSON must remain the hashed form, cite **RFC 8785** by name and adopt it whole, including its ECMAScript number rule and its UTF-16 sort key — and then note that RFC 8785 has no lossless representation for `uint64 > 2^53`, so `pos` must be a string.

---

### F2 · §3.2 + §6 + R6 — under Raft, `pos` is reused and the chain forks on a leader change
**Severity: BLOCKER · Confidence: PLAUSIBLE (reasoning from the stated design; no Raft code exists yet to read)**

**Claim.** §3.1: the transactor "assign[s] `pos` and `t_mono_ns`; compute[s] `h`; append; fsync; publish the position." §3.2: "v1 three-node Raft with the leader as transactor." §2.1: `pos` is "monotone; never reused." §6: "transactor crash → v1: Raft election." R6's receipt gate: "a leader kill mid-saga leaves the folds whole."

**Defect.** The blueprint has the leader stamp `pos`, `t_mono_ns` and `h` *before* replication. Raft does not guarantee that a proposed entry commits. Standard sequence: leader A proposes entry E at index N, replicates it to one follower, dies. B is elected with a log that lacks E. B proposes E' at index N. Raft's log-matching property forces the follower to truncate E and accept E'. Consequences, all of which contradict §2.1 or §6:

1. **`pos` N has been held by two different entries.** §2.1's "never reused" is violated. If `pos` were assigned at commit instead, it would not be.
2. **The chain forks at N.** `h_N` is a function of the entry's content; E and E' have different content, so `h_N` differs, and every `h` after N differs. The chain is not "the only truth" — it is one of two truths, and the one that lost has already been read.
3. **A subscriber has folded a phantom.** §3.3: the peer "reads the tape as a subscriber **from its last applied position**." Applied, not committed. A peer that applied E has a cell table containing a fact that never happened, and folds are forward-only reducers over an append-only log — there is no un-fold. §6's "peer crash → rebuild folds from the last snapshot plus the tape since" does not cover *the peer did not crash and its folds are wrong*.
4. **The saga is the worst case, which is exactly R6's gate.** §2.2: "A cross-cell write is a saga: one `fact` per cell … applied in order, and the transactor refuses the whole saga if any cell's constraint refuses." If a saga is N Raft entries, a leader kill between entries 3 and 4 commits a partial saga. The folds are then *not* whole, and R6's receipt gate cannot be met by construction.

**"Is 'position equals the deposit clock' well defined under replication?"** No, twice over. (a) `pos` is well defined only at commit, and the blueprint assigns it at propose. (b) `t_mono_ns` is defined as coming from "the transactor's clock, one stamping authority" — but under Raft the stamping authority *changes at every election*, and the clock is per-machine. See F7: node B's `steady_clock` epoch is its own boot time, unrelated to node A's, so `t_mono_ns` can move **backward** across an election while `pos` moves forward.

**Fix.**
- `pos` and `h` are assigned **at commit**, by the deterministic commit order, never at propose. The replicated log entry carries `{cell, cls, by, body}` and nothing else; every node computes `pos`, `t_mono_ns` and `h` from the committed sequence, so all three nodes derive the same chain from the same committed log and no node ever emits a hash for an entry that might be truncated.
- **A saga is one Raft entry**, containing its N facts. Atomicity is then Raft's, not the transactor's.
- Subscribers (`subscribe(query, from_pos)`, §3.7) are served **committed positions only**; the API must say so, and the peer's "last applied position" must be renamed "last committed position applied."
- Every entry carries the **leader term** so that a `t_mono_ns` comparison across a term boundary is refusable rather than silently wrong.
- Add a falsifier: *kill the leader mid-saga, 100 times; no position is ever held by two hashes, and no subscriber ever sees a position it later has to forget.*

---

### F3 · §4 + falsifier 1 — "two cold rebuilds are bit-identical" is not defensible as stated
**Severity: BLOCKER · Confidence: CONFIRMED for the mechanisms; the claim itself is untested**

**Claim.** §4: "Determinism. Fixed-point accumulation for every parallel sum **[M: o_projection_order]** … a fixed seed per judge invocation recorded on the entry. The falsifier is replay: two cold rebuilds from one tape are bit-identical in every fold and every shadow tape." Falsifier 1: "Two cold rebuilds from one tape are bit-identical in every fold and shadow tape. Lie: a float accumulator anywhere."

Four independent mechanisms break it. The fixed-point argument covers only one of the four.

**(a) The lattice sweep is float, and its multiply-adds diverge between nvcc and MSVC.** `osv_core.cuh:187-210`, `step_cell`, contains four multiply-add shapes:

```cpp
lap += L.k_cls  * (L.dev[cell_index(L.d, seg, cls - 1, slot)] - d0);
lap += L.k_slot * (L.dev[cell_index(L.d, seg, cls, slot - 1)] - d0);
const float r  = src + lap - L.decay * d0;
const float nd = d0 + L.omega * r / diag;
```

`nvcc` contracts `a*b+c` into a single-rounded FMA by default (`-fmad=true`); MSVC `/fp:precise` — this box's host compiler — does not. Measured on the exact shape `lap + k*(x-d0)` over 200,000 random draws: **44,064 of them (22.0%) differ between the fused and the unfused form** (probe3.py §D; first divergence `k=0.80281530614694985, x=-0.47293582601330186, d0=2.7813628108832393, lap=-6.9876715195295214`, gap 1.78e-15). The header of `osv_core.cuh` claims the opposite as a design property: *"`g++ -x c++` and `nvcc` emit the same arithmetic. Not 'the same equations' as a promise — the same translation unit as a property."* The same translation unit does not imply the same arithmetic when contraction settings differ, and no build file in the estate pins `-fmad=` or `/fp:contract`. This is precisely the R1→R2 hand-off: R1 builds the field fold "on the host", R2 rebuilds it "as kernels", and the R2 gate says "batching bit-identical."

`to_fix` (`osv_core.cuh:125`) survives contraction only by accident: `v * (float)65536.0 + 0.5f` is safe because 65536 is a power of two, so the product is exact and fusing changes nothing. Nothing in the source records that dependence — `FIX_SCALE` is a bare `static const double 65536.0` with no `static_assert` that it is a power of two, and no clamp before the `(int64_t)` cast, whose input comes from `std::atof` on an untrusted source column (`osv_ingest.h:279`). A `FIX_SCALE` of `1e6` or `100` would break host/device identity silently.

**(b) The field fold takes `now_ns` from the clock, not from the tape.** `project_one` (`osv_core.cuh:159-164`) calls `slot_of(d, now_ns, c.due_ns)` (`:151`), which buckets by *time remaining to the deadline*. A cold replay run at a different wall instant projects the same commitments into different slots, therefore a different lattice, therefore different pressures, therefore different margins, therefore different verbs. This directly contradicts §4's "no wall time inside any template or fold" and `osv_core.cuh`'s own header line "the deposit clock is the world's clock; nothing measures staleness in wall time". The blueprint never says where `now_ns` comes from.

**(c) The sampler's RNG is chain-global, not per-invocation.** §4 says "a fixed seed per judge invocation recorded on the entry." `fusord.cpp:1725-1732` builds **one** sampler chain at boot with `llama_sampler_init_dist(11)`. That RNG advances across every sample of the whole run. A replay starting at position P therefore has a different RNG history than the original and produces different text, so the shadow tape is not reproducible even if every logit is. (The *margin* is safe: `probe_one`, `fusord.cpp:2048-2057`, returns `l[emit_tok] - l[hold_tok]` straight off the logits with no sampling. The margin is deterministic-modulo-kernel; the spoken sentence is not.)

**(d) Nothing that determines a GPU kernel's arithmetic is on the tape.** §2.1 stamps `judge:<weight_sha256>@<serve_pin>`. Neither covers the CUDA driver, the nvcc version, the `-arch` target, the batch schedule, or the tensor-split — and llama.cpp's kernel selection is memory- and batch-size-sensitive. The estate's own receipt shows probe latency 106-163 ms varying with free VRAM (convergence §7, §5.2). The `dec()` helper chunks at 512 tokens (`fusord.cpp:1078-1093`), so the batch split is a function of how many tokens are pending, which on a live wire is a function of arrival timing.

**What determinism claim IS defensible, and what falsifier 1 should assert.** Three tiers, and the blueprint should state all three because they have different costs and different consumers:

| tier | claim | achievable |
|---|---|---|
| **T1 · Exact, pinned** | Two cold rebuilds of any fold, **by the same binary on the same device with the same driver**, are bit-identical. The binary's SHA-256, the driver version, the device name and the batch schedule are on the `ckpt` entry. | Yes. This is the falsifier worth running, and it catches the float accumulator the current wording is aimed at. |
| **T2 · Exact, portable** | The **state fold and every integer/fixed-point fold** are bit-identical across host and device, across cards, and across compilers. | Yes, and it is the reason fixed point exists. Assert it *only* of the integer folds. |
| **T3 · Bounded, cross-runtime** | The **field fold and every judge margin** agree across host/device/card/driver to a stated tolerance, and **no verb flips** within that tolerance. | This is the only honest claim for float and for logits. It requires a *margin-to-threshold clearance* to be recorded per decision so that "no verb flips" is checkable, not asserted. |

Rewrite falsifier 1 as three: **1a** (T1, pinned binary, every fold, bit-identical); **1b** (T2, integer folds only, across host and device — this is the one that catches a float accumulator, and it is the one `o_projection_order` already almost tests); **1c** (T3, no verb flips across runtimes, with the clearance on the row). Bit-identity of a *shadow tape* across runtimes should be deleted from the blueprint; it is not achievable and claiming it makes every other determinism claim suspect.

---

### F4 · §2.2 + falsifier 9 — the proposed fix does not close falsifier 9, and there is no room for one that does
**Severity: BLOCKER · Confidence: CONFIRMED**

**Claim.** §2.2: "the 32-bit `src_rev` and the 4 padding bytes become a 64-bit `pos_last`, which closes the truncation defect found in `osv_ingest.h`." Falsifier 9: "A wire whose positions exceed 2^32 still refuses stale rows and detects replays. Lie: the 32-bit `src_rev`."

**The defect is real and worse than "truncation."** `osv_ingest.h` has two truncating store sites, not one — `:284` in `fill()` and `:337` in the close path, which does **not** go through `fill()`:

```cpp
c.src_rev = (uint32_t)r.rev;        // :284
cur->src_rev = (uint32_t)r.rev;     // :337
```

and the monotonicity guard at `:316-319` compares a `uint64_t` against the truncated `uint32_t`:

```cpp
if (r.rev == cur->src_rev) { ++n.duplicate; return false; }
if (r.rev <  cur->src_rev) { ++n.stale_refused; return false; }
```

Simulated (probe.py §7), with true LSNs above 2^32:

```
rev=4294967306  stored_before=0   -> applied   stored_after=10
rev=4294967299  stored_before=10  -> applied   stored_after=3     <- STALE, should have been refused
rev=4294967306  stored_before=3   -> applied   stored_after=10    <- REPLAY, should have been a no-op
```

**Both** halves of falsifier 9 fail: a stale row overwrites the present, and an exact replay is counted as an update and returns `true`, firing a spurious downstream delta. The blueprint names only the truncation; the mixed-width comparison is the mechanism.

**Why the fix does not close it.** §2.2 defines `pos_last` as "**the tape position** that last touched this cell", and §12 repeats "Position: an entry's index, the deposit clock". Tape positions are assigned by the transactor at append time and are monotone *by construction*: the stale source row, arriving late, gets a **higher** tape position than the fresh row it overwrites. `rev < pos_last` can therefore never fire. The staleness refusal is not fixed — it is deleted. `osv_ingest.h:85` is explicit about what `src_rev` was: *"LSN / commit sequence — the deposit clock, **monotone per source**."* That is the *world's* clock. `pos_last` is the *store's* clock. Silently equating them is a doctrinal error, not a widening.

**And there is nowhere to put the field that would fix it.** The blueprint's `Cell` is exactly full (probe.py §8):

```
id(8)+opened_ns(8)+due_ns(8)+blocked_by(8)+pos_last(8)+amount(4)+margin(4)
  +cls(4)+seg(4)+seat(4)+state/flags/verb/gear(4) = 64 bytes; padding left = 0
```

The 4 padding bytes that could have carried a 32-bit source revision have been consumed by the widening of the wrong field.

**Fix.** Keep both clocks, and be explicit about which is which:
- `pos_last` (u64, hot row) = the tape's clock, for cache invalidation and the cognitive-index key. Keep it.
- `source_rev` (u64, **cold side record**, per source) = the world's clock, and it is what the monotonicity guard reads. The guard belongs in the fact-entry producer, before the transactor sees the write, and its refusal must emit a `refuse` entry with reason `stale_source_rev` — which is also what makes falsifier 4 ("the refusal that never silences") cover the ingest path, which today it does not.
- Fix **both** cast sites (`osv_ingest.h:284` and `:337`); the blueprint's §11 names only `Ingest::apply`, and `fill()` is the other one.
- Restate falsifier 9 so it names the observable: *feed a wire whose LSNs exceed 2^32, then replay one row and inject one out-of-order row; the tape shows exactly one `refuse` with reason `stale_source_rev` and one `duplicate` no-op, and the cell table is bit-identical before and after.*
- Note in §2.2 that removing the padding also removes a latent hazard: `Ledger::digest()` (`osv_ingest.h:221-231`) hashes all 64 raw bytes including padding, which is zero only because of the `memset` on open. With zero padding the digest is unconditionally well defined. Say so — it is a genuine improvement the blueprint does not claim.

---

### F5 · §2.1 — "fusord's tools read TAPESTRY tapes unchanged" is false
**Severity: MAJOR · Confidence: CONFIRMED**

**Claim.** §2.1: "Field names follow `fusord.cpp`'s tape so its tools read TAPESTRY tapes unchanged." §3.7: "the lane-contract v0.1 text framing remains accepted for subscribe so fusord's tailer works unchanged." §11 lists `Tape`, `chain_hash`, `Blake2b`, `write_atomic`, `replace_file` as reused.

**Evidence.** `grep -c '"pos"' fusord.cpp` → **0**. `grep -o '"t_wall[a-z_]*"'` → **nothing**. Of §2.1's ten fields, fusord's tape has **three**: `k`, `prev`, `h`, plus `t_mono_ns` on the `b` and `hdr` rows only. It has none of `pos`, `t_wall_ms`, `cell`, `cls`, `by`, `body`.

Conversely, `tape_rows.py` — the estate's tape reader — keys on `k`, `i`, `reason`, `lane`, `m_spk`/`m_skp`/`m_sen`, `lat_ms`, `probe_ms`, `clause`, `mind`, `m`, `cause`, `say`, `aired`, `killed`, `why`. **A TAPESTRY entry carries none of them.** Run against a TAPESTRY tape it prints a `counts:` line and nothing else. `fusord.cpp:36` states the compatibility contract it is actually honouring: *"Same record kinds and keys as S0 (`hdr b e e_suppressed e_rearm e_resolved tick molt end`) so `soak --brief/--review` read it unchanged."* The blueprint's kind list contains `tick`, `hdr`, `end` from that set and drops the rest.

**Three concrete collisions**, not merely absences:
1. **`ckpt`.** fusord's body is `{ms, why, toks, bytes, cursor, ok, dur_ms}` (`fusord.cpp:1540`). The blueprint's is `{fold, pos, digest, bytes}`. Same kind, different schema, `bytes` meaning two different things.
2. **`before` / `after`.** fusord's `molt` row uses them as **token counts** (`fusord.cpp:1498`). The blueprint's `fact` body uses them as **old and new column values**. Any tool that switches on those keys is now wrong.
3. **`ms` and `t0_wall`.** Every fusord row carries `ms = rel_ms() = wall_ms() - wall0`, milliseconds since **this process** started — it restarts at 0 on every restart. And `fusord.cpp:168-171` says outright that the function named `wall_ms` *"was steady_clock ms, not wall time; the name is kept because the ledger's `ms` column and the soak tools expect it."* The header row's field named `t0_wall` (`fusord.cpp:1771`) is therefore **not wall time**. A clock field whose name lies has already shipped in this estate once.

**Fix.** Delete the compatibility claim, or make it true and cheap: emit a **projection** of the TAPESTRY tape in fusord's row shape (`k`, `ms`, `i`, `mind`, …) as a separate wire file, exactly as fusord already projects the tape onto `verdicts.jsonl`. Then "fusord's tools work unchanged" is a statement about a projection the store maintains, which is honest, rather than about the tape, which is false. And rename the blueprint's `ckpt` kind to `snapshot` so the collision is gone.

---

### F6 · §3.2 + §6 — `Tape::open` silently forks the chain to genesis, and per-segment verification cannot tell
**Severity: MAJOR · Confidence: CONFIRMED**

**Claim.** §3.2: "Append-only segment files, 64 MiB each, named by first position, **with the chain continuing across segments**." §11: `Tape` is reused.

**Defect 1 — the reused code cannot continue a chain across files.** `fusord.cpp:860, 863-897`. `Tape::open` recovers `prev` from *the file it is opening*. A newly rolled 64 MiB segment is size 0, so the recovery block is skipped entirely and `prev` remains its initializer, `GENESIS` (`:860`). Every segment roll therefore starts a new chain at genesis, silently, with no `warn`.

**Defect 2 — the recovery window is 64 KiB, and failure is silent.** `fusord.cpp:872`: `const uint64_t lo = sz > 65536 ? sz - 65536 : 0;`. The walk-back loop (`:877-887`) exits at `if (start == 0) break;` **without setting `prev`**, leaving it `GENESIS`. The only `warn` (`fusord.cpp:1635`, `warn("torn_row_skipped", ...)`) fires on `torn_bytes`, which is set only when the file's last byte is not a newline. A tape whose trailing 64 KiB contains no complete row — one `fact` entry carrying a wide `before`/`after`/`inverse` triple is enough — forks to genesis with no diagnostic at all.

**Defect 3 — the estate's verifier cannot distinguish a continuation from a fork.** `verify_chain.py` treats a first row whose `prev` is not genesis as a *seam* ("legal if it matches the last h of an earlier tape") and a first row whose `prev` **is** genesis as normal. Run against two synthetic single-segment tapes (probe3.py, second run):

```
--- segment that CONTINUES a prior chain ---
  L1: first row prev is not genesis ... resumed chain head
  rows=4 breaks=0 seams=1     CHAIN OK   exit 0
--- segment that FORKED at genesis (what Tape::open produces on a new file) ---
  rows=4 breaks=0 seams=0     CHAIN OK   exit 0
```

**Both exit 0.** The forked segment is *cleaner-looking* than the correct one. R0's receipt gate and convergence T5 ("Chain continuity … `verify_chain.py` reports 0 breaks and 0 seams") both pass on a chain that has forked at every roll.

**Fix.**
- The segment header must carry `prev_of_first_entry` and `first_pos`, and `Tape::open` on a segment named `<first_pos>` must load `prev` from **the previous segment's last row** (or from the header) and **fail fatally** if it cannot. `prev = GENESIS` is legal only for the segment named `0`.
- Widen the recovery window by doubling until a head is found or the file start is reached; a file with bytes and no recoverable head is **fatal**, never genesis.
- Replace the "seam" concession in the verifier with an explicit expected-`prev` argument: `verify_chain.py --expect-prev <hex> segment_N` — a tool that accepts either answer is not a falsifier.
- Add to falsifier 1's family: *roll a segment mid-run; the concatenated chain verifies with zero seams.*

---

### F7 · §2.1 + §3.9 — `t_mono_ns` has no epoch that survives a reboot, and the store has no durable time base
**Severity: MAJOR · Confidence: CONFIRMED**

**Claim.** §2.1: "`t_mono_ns` | uint64 | monotonic stamp from the transactor's clock, one stamping authority" and "`t_wall_ms` … **never used for ordering or in any template**." §2.3 defines per-verb horizons in nanoseconds; §3.9's grade fold "joins each decision to outcomes within its verb's horizon."

**Defect.** `fusord.cpp:161-164`:

```cpp
static inline uint64_t mono_ns() {
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}
```

`steady_clock`'s epoch is unspecified and on Windows is boot time. `t_mono_ns` therefore **resets to near zero on every reboot** and is **incomparable across machines** — which under Raft (§3.2 v1) means incomparable across leaders. fusord's own comment for `epoch_ms()` (`:175-178`) says it plainly: *"the only clock that survives a process."*

So the blueprint has forbidden the only durable clock (`t_wall_ms`) from being used for anything, and mandated a clock that cannot express a duration across a restart. §2.3's `horizon_ns`, §2.2's `due_ns` and `opened_ns`, §3.9's join, and §2.4's "at horizon expiry" all need a time base that survives a reboot, an election, and a machine move. None exists.

**Fix.** Three clocks, named for what they are, and the law states which is usable where:
- `t_seq` — `pos` itself. The only ordering authority. Already there.
- `t_epoch_ns` — TAI or UTC nanoseconds from the leader, monotone-enforced by the transactor (`max(observed, last + 1)`), **stamped once, inside the hash**, and carrying the leader term. This is what `due_ns`, `opened_ns` and `horizon_ns` are measured in. It is the field the blueprint currently calls `t_wall_ms` and forbids.
- `t_mono_ns` — the leader's monotonic reading, useful only for durations **within one leader epoch** (probe latency, sweep time). Say that, or drop it from the entry and put it on the operational rows only.

Note that a horizon expressed in `t_epoch_ns` makes the grade fold's "at horizon expiry" a *tape event* (a `tick` entry whose `t_epoch_ns` crosses the horizon) rather than a wall-clock timer, which is what keeps replay deterministic. The blueprint should say that the grade fold is driven by tape entries only, never by a timer.

---

### F8 · §3.1 + §4 — "fsync" is nowhere in the reused code
**Severity: MAJOR · Confidence: CONFIRMED**

**Claim.** §3.1: "assign `pos` and `t_mono_ns`; compute `h`; append; **fsync**; publish the position." §4: "An entry is durable when fsynced on the leader (v0)."

**Evidence.** `grep 'fsync|_commit|FlushFileBuffers' fusord.cpp` → **zero hits**. `Tape::put` (`fusord.cpp:906`) does `std::fflush(f); // durable as it goes, not at exit` — that is a flush of the C library buffer into the OS page cache, not to the platter. `write_atomic` (`:403-411`) is worse: `fwrite`, `fclose`, `MoveFileExA` with no `fflush`+`_commit` on the temp file and no directory sync, so on a power loss the rename can land pointing at a zero-length file — and `write_atomic` is what the checkpoint `.meta`, the cursor and the pill go through, i.e. exactly the snapshot metadata §3.2 depends on.

The blueprint's throughput target — "ten thousand entries a second on one core **[BUDGET]**" — is a budget for `fflush`, not for `fsync`. A per-entry `FlushFileBuffers` on this class of hardware is order 10^2–10^3/s, one to two orders below the stated target. The [BUDGET] tag does not rescue this, because it is not a sizing question: the number was arrived at under the wrong durability primitive.

**Fix.** Say which primitive, and re-budget. Either (a) `FlushFileBuffers` per entry and restate the target at ~10^3/s, or (b) group commit — fsync once per batch of entries or per N milliseconds, publish positions only after the fsync returns, and state the bounded window of entries that are appended-but-not-durable. (b) is the right answer and it costs one sentence in §3.1 and one row in §4. Also: `write_atomic` needs `fflush` + `_commit` before the rename for anything the store will later trust, and the snapshot `.meta` is exactly that.

---

### F9 · §3.2 + falsifier 3 — the snapshot design does not support the claim, and falsifier 3 is unfalsifiable as written
**Severity: MAJOR · Confidence: CONFIRMED (the gap is textual; the fusord precedent is code-verified)**

**Claim.** Law 2: a hold "survives every compaction." §3.2: "**Compaction never rewrites a segment.** A snapshot is a `ckpt` entry plus a fold checkpoint file, so a cold start can begin at a position instead of genesis, and the full tape is retained in object storage forever." Falsifier 3: "Every `hold` survives snapshot, compaction, and replay. Lie: a snapshot that keeps only state changes."

**Defect 1 — "compaction" is used three times and never defined, and §3.2 makes it a no-op.** If compaction never rewrites a segment and the full tape is retained forever, then nothing is ever removed and falsifier 3 cannot fail: you cannot lose a hold to an operation that does not exist. A falsifier that cannot fail is not a falsifier. Meanwhile `osv_core.cuh:63` *does* define a compaction that removes things — `C_CLOSED // discharged; leaves the ledger at the next compaction` — and it operates on the **fold**, not the tape. The blueprint inherits that code and never reconciles the two meanings.

**Defect 2 — what is retained versus derived is never stated, and the honest answer is uncomfortable.** Sorting it out:

| object | retained or derived | where the blueprint says so |
|---|---|---|
| the `hold` **entry** | retained, forever, on the tape | law 1; safe |
| the `hold` in the **cell table** (state fold) | derived; **gone** once the cell closes and the compaction in `osv_core.cuh:63` runs | nowhere |
| the `hold` in the **grades fold** | derived; reachable only while inside the verb's horizon | §2.4, implicitly |
| the fold **checkpoint file** | retained; its contents, format, and whether its `digest` is verified on load are unspecified | §2.1's `ckpt` body names a `digest`; §6 never checks it |

**Defect 3 — the interesting failure is a cold start from a snapshot, and it is not covered.** A hold recorded at position P−1000 on a cell that closed at P−500 is in *neither* the snapshot at P (the cell left the cell table) *nor* the tape after P. A cold start "at a position instead of genesis" cannot see it. If any verb's horizon is longer than the snapshot interval, the grade and license folds are wrong after a restart — and §2.4 says the license fold is "read at the longest finite [horizon]". Nothing ties snapshot retention to the longest horizon.

**Defect 4 — the snapshot path is never verified against the genesis path.** §2.4: "Every fold has a `verify_fold` mode: **cold recompute from genesis** must equal the incremental result bit for bit." That checks genesis-vs-incremental. It does not check **snapshot+tail vs genesis**, which is the path §3.2 introduces and §6 relies on ("rebuild folds from the last snapshot plus the tape since"). Falsifier 1 as written — "two cold rebuilds from one tape" — is two *genesis* rebuilds. It cannot catch a bad snapshot. And the cited precedent for the guards is itself broken: `Kernel::checkpoint` writes `meta += "prev\t" + tape.prev + "\n"` (`fusord.cpp:447`), and `parse_meta` (`:469-489`) has **no `prev\t` branch**. The chain-head guard is written and never read. §3.4's "the same guards as fusord's `checkpoint` and `restore`" is therefore weaker than it sounds: the guards that exist are model path (a *string* comparison), serve hash, token count, and a weight SHA that is skipped whenever either side is empty (`:501`).

**Fix.**
- Define compaction in §12. Proposal: *compaction removes nothing from the tape; it drops **closed cells** from the cell table and evicts **cold segments** to object storage. A `hold` is never in scope for either, because a hold is a tape entry.* Then falsifier 3 has to be restated against something that can actually fail (below).
- State the snapshot contract: a fold checkpoint at position P retains (i) the fold's full state at P, (ii) every **open obligation** carried forward regardless of age, and (iii) every **ungraded decision** whose horizon has not expired, with its `pos`. Its `digest` is verified on load and a mismatch is fatal with a `warn` entry.
- Add the retention law: **no snapshot may be used as a cold-start base if `now − snapshot.t_epoch_ns < longest_finite_horizon`** is violated in the other direction — i.e. the fold must carry forward every decision still inside a horizon, or the store must replay from before the oldest such decision.
- Restate falsifier 3 so it can fail: *take a snapshot at P; cold-start a second store from the snapshot plus the tape after P; every `hold`, `verb` and `grade` reachable by `query(sql, as_of_pos)` in the genesis-built store is reachable, with identical rows, in the snapshot-built one.* That is falsifiable, and its planted lie ("a snapshot that keeps only state changes") then actually plants.
- Add: the checkpoint `.meta` guard must include the chain head, and it must be **read back**. Fixing `parse_meta` to parse the `prev` it already writes is a two-line change and it is the guard that catches a checkpoint restored onto a forked tape.

---

### F10 · §2.5 + §3.8 + §3.4 — the cognitive index cannot be built "as of `pos_last`" on the measured model
**Severity: MAJOR · Confidence: CONFIRMED (against the receipt)**

**Claim.** §2.5: "Key: `(cell_id, pos_last, template_pin, judge_pin)`." §2.3: the template renders "anything derivable from the tape at the cell's `pos_last`." §3.8: replay is "a subscribe from `from_pos` with the judge in `shadow` mode … years of tape in hours **[BUDGET]**."

**Evidence.** Convergence Appendix B.7, quoted verbatim from the measurement:

> "A judgment cannot be made 'as of' an earlier instant on this trunk. The 9B is a 3:1 recurrent hybrid: a fork can be truncated in the attention layers, but its recurrent state cannot be rewound; `llama_memory_seq_rm(fork, at, -1)` refuses (measured 2026-09-04: the removal was a silent no-op, the probe then failed with 'inconsistent sequence positions', and a probe that ignored the failure returned stale logits as if they were margins)."

§6.4 draws the design rule: *"a delayed judgment is a judgment about now, and the record must say so."* The blueprint does not carry that rule forward. Keying the cognitive index on `pos_last` implies you can hold, or reconstruct, a read state *as of* a past position. On this model class you can only build it **forward from scratch**. Three consequences the blueprint should state:

1. A warm entry at `(cell, pos_last=P)` cannot be rewound to `P−k`; it can only be discarded and rebuilt. §2.5's "Invalidated when the cell … is touched" is therefore not an optimisation choice, it is the only option, and the cost is the full suffix rebuild every time, not a truncation.
2. §3.8's "years of tape in hours" is a [BUDGET] computed against the wrong unit cost. The measured 110-130 ms per three-seat judgment (§3.5 [M]) is the probe on an **already-warm** fork. A replay judgment is cold by definition: it pays the suffix re-render first. §5 acknowledges this once ("Cold judgments pay a re-render of the suffix, about 500 tokens, before the probe") but §3.8's budget does not include it.
3. The 17,432 B/token constant is the **marginal** cost on a model whose ~52.7 MB is **fixed recurrent state per sequence** (§7.3 arithmetic reproduced exactly in probe.py §9: 17,432.0 B/token, 52.692 MB fixed). §5's "warm cells per 141 GB card … about 15,000" assumes the per-cell cost is the suffix. 15,000 sequences × 52.7 MB of recurrent state is **0.8 TB** before a single token of KV (probe.py §10). A copy-on-write fork can share attention pages; it cannot share a recurrent state that must diverge — which is precisely why `seq_cp` "copies a recurrent state, not just cell membership" (§6.4). §5's warm-cell count is out by roughly two orders of magnitude *on this judge*. This is §5's number, not mine to fix, but falsifier 7 ("the fork that costs nothing") sits on the same fault line and the [M: fork at 0 MiB, m0-0] receipt needs re-reading against B.7 before it is cited again.

**Fix.** State in §2.5 that the cognitive index is **build-forward-only**: `pos_last` in the key is a *validity stamp*, not a seek target, and there is no operation that moves an entry backward. State in §3.8 that a replay judgment's cost is `suffix_render + probe`, and re-derive the budget. Add to §10: *does the judge chosen for v0 have a rewindable read state at all, and if not, what does that cost the replay engine per judgment.*

---

### F11 · §2.1 — the row builder is lossy on control bytes and does not validate UTF-8, so the chain attests to a mutated value
**Severity: MAJOR · Confidence: CONFIRMED**

**Evidence.** `fusord.cpp:187-195`, `jesc`, the escaper every tape row passes through:

```cpp
if (c == '"') o += "\\\""; else if (c == '\\') o += "\\\\";
else if (c == '\n') o += "\\n"; else if (c == '\t') o += "\\t"; else if (c == '\r') {}
else if ((unsigned char)c >= 0x20 || c < 0) o += c;
```

Every `\r` is **deleted**. Every byte in `0x00`–`0x1F` other than `\n` and `\t` is **deleted**. Measured: `b'ok\x01\x02BEL\x07\rEND\x1f!'` → `'okBELEND!'`, five bytes silently gone (probe.py §5). And bytes ≥ 0x80 pass through raw via the `c < 0` branch with **no UTF-8 validation**, so a Latin-1 source value produces a row that a strict reader cannot decode at all — a Python fold or judge subscribing to the tape gets `UnicodeDecodeError: 'utf-8' codec can't decode byte 0xe9 in position 24` before it can even disagree about the hash (probe2.py §A).

On fusord's tape this is harmless: the bodies are kernel-generated English. On a TAPESTRY `fact` row the body carries `before` and `after` **from a real database column**, and CRLF, `\x00`, and non-UTF-8 legacy encodings are ordinary contents of real columns. The chain then hashes, and attests to, a value that is **not what the world said** — while law 1 says the tape is the truth.

**Fix.** The serializer must be total and lossless: escape `\r` as `\\r` and every other C0 byte as `\uXXXX` (both are required by RFC 8259 anyway — the current code emits *invalid JSON* for a raw `\x01`, which it instead deletes). Validate UTF-8 at the ingest boundary and represent a non-UTF-8 column value explicitly (base64 with a type tag), refusing with a `refuse` entry rather than mangling. Add to falsifier 4's family: *a `fact` whose value contains every byte 0x00–0xFF round-trips through the tape byte-for-byte, or is refused with a typed reason.* There is no third outcome.

---

### F12 · §2.1 — `t_wall_ms` is inside the hash and inside the templates' reach
**Severity: MAJOR · Confidence: CONFIRMED for the mechanism, PLAUSIBLE for the consequence**

**Question asked: is `t_wall_ms` ever safe to carry, and does its presence create a temptation the templates cannot resist?**

**Safe to carry — with two conditions the blueprint does not state.**
1. It must be **outside the hashed bytes**. As written it is a field of the entry, therefore inside `canonical(body_and_header)`, therefore inside `h`. It is then the one field of every entry that is not a function of the tape, which means the transactor's output can never be re-derived and audited from its inputs. Every other determinism claim in §4 is about re-derivation; this field is the single exception and nothing marks it as one.
2. It must be labelled **non-monotone**. NTP steps and leap smears mean two entries can carry equal or decreasing `t_wall_ms` while `pos` increases. §2.1 says "informational only, never used for ordering" but does not say *why*, so the first person to sort a report by it will and the report will disagree with the tape.

**The temptation is real and this estate has already lost to it once.** §2.3 forbids templates from including wall time; §4 repeats "no wall time inside any template or fold". Both are prose prohibitions against a field that is physically present on every entry the template renders from. The precedent: `fusord.cpp:168-171` documents that the function named `wall_ms()` is **`steady_clock`**, not wall time, and that the name was kept for compatibility — and the header row consequently ships a field literally named `t0_wall` (`fusord.cpp:1771`) whose value is not wall time. A clock name that lies has already shipped here.

**Fix — make it structural, not textual.** Three options, in order of strength:
1. **Best:** `t_wall_ms` is not a field of the entry at all. It lives in a side index `pos → t_wall_ms`, written by the transactor, outside the chain, and the tape carries `t_epoch_ns` (F7) as the one durable clock the folds may read. Templates take a typed projection of the cell that physically has no wall-clock member — the renderer cannot reach what is not in its input type.
2. If it must stay on the entry, put it **after** `h` in the row (a trailer, excluded from the hashed bytes) and name it `t_wall_ms_unhashed_informational` so that any use of it is visible in a grep.
3. Enforce it: the template pin (§2.3) is computed over the renderer's source; extend the pin's computation to **refuse to register a template whose source mentions any wall-clock symbol**, and make that refusal a `refuse` entry. The estate's own conclusion about heredocs applies verbatim: *"Advisory text cannot win that argument; a hook does not have to."*

---

### F13 · §2.4 — a fold's identity is "the hash of its source", which does not determine its arithmetic
**Severity: MINOR · Confidence: CONFIRMED (follows from F3a)**

§2.4: "A fold is a registered, deterministic reducer over a tape span, **identified by the hash of its source**." F3(a) shows the same source compiled by nvcc and by MSVC computes a different lattice, 22% of the time on the sweep's core expression. Source hash is therefore not the fold's identity. The `verify_fold` mode as specified compares two runs of the *same binary* and proves nothing about next year's rebuild.

**Fix.** Fold identity = `blake2b256(source_hash ‖ compiler_id ‖ flag_string ‖ target_arch)`, and the whole tuple goes on the `ckpt` entry. Pin `-fmad=false` **or** `/fp:contract=on` — either is fine, the point is that both sides must agree and the choice must be recorded. And add a `static_assert` that `FIX_SCALE` is a power of two, plus a clamp before `to_fix`'s `(int64_t)` cast, since `amount` is `std::atof` of an untrusted column (`osv_ingest.h:279`) and a value above 2^47 makes the cast undefined behaviour inside a "deterministic" fold.

---

### F14 · §11 + R2 — `o_step_batch`, the falsifier carried forward for "batching bit-identical", is vacuous
**Severity: MINOR · Confidence: CONFIRMED**

§11 carries `osv_core_test.cpp` forward as "fifteen of the falsifiers"; R2's receipt gate is "batching bit-identical". The test that is supposed to prove it, `osv_core_test.cpp:228-248`:

```cpp
auto run = [](int, float bump) { ... };
const auto all = run(0, 0.0f), one = run(1, 0.0f);
const bool identical = std::memcmp(all.data(), one.data(), ...) == 0;
```

The first lambda parameter is **unnamed and unused**. `run(0,0.0f)` and `run(1,0.0f)` execute the identical loop nest over identical data. `identical` is true by construction; the test named *"one numerical path whatever the size"* never varies the size or the batching. Only the `bump` lie is live.

Related: `o_projection_order` (`:110-141`) computes `float_differs` — the planted lie that fixed point exists to prevent — and then **does not include it in the pass condition**: `chk("o_projection_order …", identical, n)`. The lie is printed, not asserted, contradicting the file's own opening line ("a deliberately broken variant that MUST fail").

The property `o_step_batch` claims *is* true — I checked the stencil: over a 12×16 (cls, slot) lattice there are **zero** same-colour neighbour pairs in the 5-point stencil (probe3.py §E), so a red-black half-sweep reads only the opposite colour and is genuinely order-independent within one binary. The test simply does not test it.

**Fix.** Make `run` take the batch factor and actually vary it (one segment at a time vs all segments per colour vs a device grid). Assert `float_differs` in `o_projection_order`. Do not carry a vacuous test forward under R2's gate.

---

### F15 · §6 — the torn row's bytes stay in the file, and the estate's verifier counts them as a chain break
**Severity: MINOR · Confidence: CONFIRMED**

§6: "torn entry | one partial line | skipped, counted, `warn` entry, next entry starts clean." The recovery (`fusord.cpp:896-897`) does:

```cpp
f = std::fopen(p.c_str(), "ab");
if (f && torn) { std::fputc('\n', f); std::fflush(f); }
```

It **terminates** the torn bytes rather than removing them. The garbage line stays in the file forever. The chain itself is intact across the hole (the next row's `prev` is the last complete row's `h`), so this is not a correctness bug — but `verify_chain.py` does not skip non-parsing lines; it prints `L{n}: MALFORMED (no prev field)` and **counts a break**. A tape with a real torn row therefore fails its own chain verifier. This has never been exercised: none of the four verified tapes (convergence B.3) contains one.

**Fix.** Truncate the file to the last complete row instead of appending a newline. The torn bytes were never committed and never hashed, so removing them loses nothing, keeps the tape a pure sequence of chained entries, and keeps `verify_chain.py` honest. `torn_bytes` already measures exactly what to remove, and the `warn` entry (`fusord.cpp:1635`) already reports it. Note that truncating an append-only file is the one write that must be allowed, and §3.2 should say so explicitly rather than leaving it to be discovered.

---

### F16 · §2.2 — the cell id is an unkeyed 64-bit FNV-1a with no collision detection
**Severity: MINOR (v0) / MAJOR (once §7 is real) · Confidence: CONFIRMED**

§2.2: "`id` … stable: `fnv1a(source, table, key)`, 0 reserved." `osv_ingest.h:186-194` returns `h ? h : 1` — a designed collision of the 0 preimage onto id 1. FNV-1a is not collision-resistant and not keyed. At 50,000 cells the birthday risk is negligible, but the tape is forever, `cell` is also the cognitive-index key and the constraint layer's key, and §7 is a security model: anyone who can name a row in a source system can manufacture a colliding `(source, table, key)` and land a write on somebody else's cell. Law 3 ("one writer per cell") then serialises two unrelated obligations into one, silently.

**Fix.** Either widen to `blake2b128(source ‖ 0x1f ‖ table ‖ 0x1f ‖ key)` truncated to 64 bits (same width, no adversarial construction), or keep FNV and **detect**: the cold side record already exists (§2.2), so store the full `(source, table, key)` beside the id and refuse on mismatch with a `refuse` entry, reason `id_collision`. The second is cheap, keeps the hot row at 64 bytes, and turns a silent merge into a typed refusal — which is what falsifier 4 is for.

---

### F17 · §2.1 — small precision defects
**Severity: MINOR · Confidence: CONFIRMED**

- `h | hex64` and `prev | hex64` are ambiguous — 64 bits or 64 characters? It is 64 characters / 256 bits. In a table that is the entire conformance surface, write `hex[64] (blake2b-256)`.
- §2.1's kind table lists `tick, hdr, end, warn, fatal` "as in fusord" but reproduces none of their bodies, and the store will inherit fusord's other kinds (`b, e, e_suppressed, e_rearm, e_resolved, unsaid, molt, heard, brief, counsel, discard, replay, vram, trunc, switch, stop`) the moment the judge runtime runs. Either enumerate the full kind set or say that judge-runtime kinds are namespaced (`judge.e`, `judge.unsaid`).
- §2.1 says the v1 form is "length-prefixed binary **with the same fields**". Same fields, different bytes, therefore different hashes. See F1's fix (3): say that the v1 chain restarts and carries the v0 head forward.

---

## 3 · What is right and must not be changed

1. **Law 1, and hashing `prev` before the body.** The order matches `chain_hash` exactly (`fusord.cpp:312-318`), and the BLAKE2b implementation is verified against Python `hashlib` on six vectors including an exact 128-byte block, a 300-byte multi-block, and a split two-part update (convergence B.1). The hash function is not in doubt; only the bytes fed to it are.
2. **Recovering the chain head from the tape itself**, not from a sidecar. Convergence §8 measured the alternative: a per-row write-through rename is both a hot-path cost and "the exact file the rename hazard can freeze, after which the next run forks the chain from a stale head" — and the rename hazard is real (B.4, `MoveFileEx` FAILED WinError 5 while a reader holds the destination). Keep head-from-tape.
3. **Fixed-point accumulation for the projection.** `to_fix`/`from_fix` with `FIX_SCALE = 65536` and int64 accumulators is exactly right, and `o_projection_order` demonstrates the property it claims for the fixed-point path. Keep it, and extend it to every remaining float accumulator rather than retreating from it.
4. **Red-black colouring.** Verified: zero same-colour neighbour pairs in the 5-point (cls, slot) stencil (probe3.py §E), so a half-sweep reads only the opposite colour and is order-independent within one binary. The comment in `osv_core.cuh:172-185` is correct and the property is worth keeping as the reason the sweep can be a kernel at all.
5. **Torn-row recovery walking back to a row that ends `}\n`.** This is convergence F6 already applied (`fusord.cpp:877-887` vs §5.2 defect 11). It is right; only the genesis fallback and the 64 KiB window are wrong (F6 above).
6. **The refusal as an entry, and the hold as an entry.** Falsifiers 3 and 4 point at the two things most systems lose. Keep both; F9 only asks that falsifier 3 be made capable of failing.
7. **Widening `src_rev` + padding into one 8-byte field, keeping `sizeof(Cell) == 64`.** The arithmetic is exact (probe.py §8) and removing the padding also makes `Ledger::digest()` unconditionally well defined. The *choice of which clock to put there* is wrong (F4); the structural move is right.
8. **`probe_one` returning a logit difference rather than a sample.** `fusord.cpp:2053-2054` — the margin never touches the sampler, so the margin is the reproducible part of a judgment. Keep the margin as the recorded quantity; F3(c) is about the generated text, not the margin.
9. **The `.meta` sidecar written last, and `<ckpt>.prev` kept one generation back** (`fusord.cpp:428-461`). The pattern is right. It only needs its `prev` guard read back (F9) and an fsync (F8).

---

## 4 · Top three changes

1. **Delete "canonical" from §2.1 for v0** and state the law as *the hashed bytes are the on-disk bytes preceding `,"prev":"`* — which is what `chain_hash` does, what `verify_chain.py` checks, and what every existing receipt certifies; then commit v1 to a **binary** canonical form (deterministic CBOR + raw IEEE floats + `prev` as 32 bytes) and say out loud that the v1 chain restarts.
2. **Assign `pos` and `h` at commit, not at propose; make a saga one Raft entry; serve subscribers committed positions only** — otherwise a leader change reuses a position, forks the chain, and R6's own receipt gate is unsatisfiable.
3. **Split falsifier 1 into three tiers** (bit-identical for a pinned binary + device + driver whose hashes are on the `ckpt` entry; bit-identical across host and device for the **integer** folds only; **no verb flips** within a stated tolerance for the float folds and the judge margins), and pin `-fmad`/`/fp:contract` in the build so the middle tier is even testable.

---

## 5 · Open questions to add to §10

11. **Which clock carries a duration across a reboot and across a leader change?** `t_mono_ns` is `steady_clock` and resets at boot (`fusord.cpp:161-164`); `t_wall_ms` is forbidden from every fold. `due_ns`, `opened_ns` and `horizon_ns` need a third clock that the blueprint does not define. Proposal: `t_epoch_ns`, monotone-enforced by the transactor, term-stamped, inside the hash.
12. **Is the wire's clock kept at all after `src_rev` becomes `pos_last`?** If yes, where does a 64-bit `source_rev` live now that the hot row has zero padding? If no, what refuses a stale CDC row, and what does falsifier 9 actually assert?
13. **What is the exact fold-checkpoint format, and is its `digest` verified on load?** §2.1 names a `digest` in the `ckpt` body; §6's recovery path never checks it, and the cited precedent (`parse_meta`, `fusord.cpp:469-489`) writes a `prev` guard it never reads.
14. **Is a snapshot ever usable as a cold-start base while a decision inside it is still within its verb's horizon?** If the fold must carry open holds and ungraded decisions forward regardless of age, say so and size it; if not, state the minimum tape retention as a function of the longest finite horizon.
15. **Does the v0 judge have a rewindable read state?** B.7 says the 9B hybrid does not. If not, `pos_last` in the cognitive-index key is a validity stamp and never a seek target, every invalidation is a full suffix rebuild, and §3.8's replay budget must be re-derived at `suffix_render + probe` per judgment.
16. **What does a torn row's bytes become — truncated, or left in the file?** §6 says "next entry starts clean"; the reused code leaves the bytes and appends a newline, and `verify_chain.py` then counts a break. Pick one and make the verifier agree.
17. **Which compiler and which contraction setting is the reference for a fold?** A fold identified by "the hash of its source" is not identified at all while nvcc and MSVC disagree on 22% of the sweep's multiply-adds.
18. **How is a fold un-applied?** Folds are forward-only reducers. Under Raft a subscriber can apply an entry that is later truncated (F2). Either subscribers never see uncommitted positions, or the blueprint owes an inverse for the fold, which it does not have.
19. **What is the durability primitive and what is the resulting throughput?** §3.1 says fsync; the reused code does `fflush` only; the 10,000/s [BUDGET] was computed under the wrong primitive.
20. **Does the byte-exactness law apply to the world's bytes?** A `fact` carrying a column containing `\r`, `\x00`, or non-UTF-8 currently loses those bytes silently (`jesc`, `fusord.cpp:187-195`) and the chain then attests to the mutated value. Round-trip, or refuse — the blueprint should say which.

---

*Reviewer: Claude Opus 5, 2026-09-08. Probes and chunked sources under `C:/TAPESTRY/qc/scratch/tape/` and `C:/TAPESTRY/qc/scratch/conv/`; nothing outside `C:/TAPESTRY/qc/` was modified. Where this report and a receipt disagree, the receipt wins and this report is the defect.*
