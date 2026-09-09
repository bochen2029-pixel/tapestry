# QC-3 · THE COGNITIVE INDEX, THE JUDGE RUNTIME, THE PEER, AND THE SIZING
### Adversarial review of TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1 sections 2.5, 3.3, 3.4, 3.5, 5, falsifiers 6 and 7, open questions 1, 2, 9, 10, and the fusord reuse claims in section 11

**2026-09-08 · Dallas · Claude Fable 5.1 (`claude-fable-5-1`), QC reviewer 3 of 7.** Register: every number below is tagged **[M]** (measured today on this box, command and file named), **[R]** (read from a dated receipt on disk, path named), **[S]** (read from source or a header, file:line named), **[D]** (derived by arithmetic from [M]/[R]/[S] inputs, shown), or **[BUDGET]** (an estimate). Confidence per finding is CONFIRMED (verified against code, a header, a receipt, or a run) or PLAUSIBLE (reasoning only). Scratch and raw outputs: `C:/TAPESTRY/qc/scratch/ci/` (`bench_pps_npl{8,64,128,300}.txt`, `bench_probe_prefill.txt`, `ckpt_fit.py`, `gguf_hparams.py`). Nothing outside `C:/TAPESTRY/qc/` was modified; the GPU runs were read-only benchmarks against the registered judge weights.

---

## 1 · Verdict

The cognitive index as specified cannot be built on the judge the estate has: on the 3:1 recurrent hybrid every diverged fork owns a 50.25 MiB f32 recurrent-state slot that is allocated at boot for all `n_seq_max` slots, the build the kernel links refuses `n_seq_max` above 256, and the "fork at 0 MiB" receipt measured a metadata fork before any decode with the slots already paid for, so the section 5 warm set is not 15,000 or 45,000 cells but at most about 250 on any card with this substrate, about 50 to 80 on the estate's 16 GB card, and about 2,100 on an H200 even if the cap is recompiled away. The throughput row is built from the wrong constant on the wrong card and omits the cold path that this correction makes the majority path, the per-cell NVMe checkpoint duplicates the shared root into every file, the index key omits the root pin that every entry depends on, and the determinism claims assert bit-identity that GPU batching does not provide. The laws survive intact (the peer is a cache, the tape is the truth, a hold is a row), the measured constants the blueprint cites are correct where it cites them (17,432 B per token, 52.7 MB fixed, q8_0 quality-neutral, the 0 MiB KV share), and the fix is a re-sizing plus a root pin plus a scoped determinism contract, not a new design; the one open decision that changes the economics by an order of magnitude is the judge architecture itself.

---

## 2 · Findings, most severe first

### F1 · The per-warm-cell cost is 58.5 MiB, not 8.5 MB, because a warm cell on this judge is a diverged fork and a diverged fork owns a recurrent-state slot

- **Sections:** 2.5 ("a fork shares pages and copies on write ... [M: fork at 0 MiB, m0-0]"), 5 (per-cell suffix "500 tokens · about 8.5 MB"; "warm cells per 141 GB card ... about 15,000"), 9.7.
- **Claim:** the per-cell cost is the suffix KV; a fork costs nothing until it diverges.
- **Defect:** the cost of a fork that has *diverged* is exactly what a warm cell is, and on this model it is dominated by the recurrent state, which the sizing table never mentions. The m0-0 receipt is real but measures the wrong thing for this table: it forked three branches over an 8k trunk and read the VRAM delta *before decoding on any branch*, on a context whose eight recurrent slots had already been allocated inside the "+model/ctx" delta.
- **Evidence:**
  - The judge is `qwen35`, 33 blocks of which one is a NextN predictor (`qwen35.nextn_predict_layers = 1`; loader: "offloading 32 repeating layers"), `full_attention_interval 4`, so 8 attention layers and 24 gated-delta-net layers; `head_count_kv 4`, `key_length 256`, `value_length 256`; `ssm.time_step_rank 32`, `ssm.state_size 128`, `ssm.group_count 16`, `ssm.inner_size 4096`, `ssm.conv_kernel 4` **[R: GGUF metadata of `C:/models/Qwen3.5-9B-emit-v11-Q5_K_M.gguf`, read with `gguf_hparams.py`]**. The recurrent memory log skips layers 3, 7, 11, ... (`bench_pps_npl8.txt`, "layer 3: skipped").
  - Recurrent state per sequence slot: `llama_memory_recurrent: CUDA0 RS buffer size = 402.00 MiB` at `n_seq_max 8`, `3216.00 MiB` at 64, `6432.00 MiB` at 128 **[M: `bench_pps_npl{8,64,128}.txt`]** = **50.25 MiB = 52,690,944 B per slot**, allocated at context creation whether or not a fork exists. The decomposition matches the architecture exactly: 24 layers × (32 × 128 × 128 state + 3 × 8,192 conv) × 4 B = 52,690,944 B **[D]**.
  - The checkpoint receipts fit `bytes = 52,691,760 + 17,432 × toks` with **zero residual** over about 100 distinct points from 261 to 8,703 tokens **[M: `ckpt_fit.py` over every fusord tape under `C:/fusor1/build`, `C:/fusor1/converge/smoke`, `C:/fusor1/sandbox`]**; 52,691,760 − 52,690,944 = 816 B of file header. The convergence document's 52.7 MB is confirmed and is the recurrent state.
  - Attention KV: `llama_kv_cache: CUDA0 KV buffer size = 272.00 MiB` at `n_ctx 16384` **[M]** = **17,408 B per token at q8_0** (8 layers × 4 KV heads × 512 × 34/32) **[D]**; the receipt's 17,432 is 17,408 + 24 B of per-token bookkeeping in the state file.
  - The fork is metadata in both memories: unified KV `seq_cp` "since both sequences are in the same stream, no data copy is necessary we just have to update the cells meta data" (`cells.seq_add`), recurrent `seq_cp` sets `tail_dst.tail = tail_src.tail` and inserts the seq id; the recurrent copy happens on the fork's **first decode** in `find_slot` ("empty_cell.src = orig_cell.src; orig_cell.seq_id.erase(seq_id); empty_cell.seq_id.insert(seq_id)") **[S: llama.cpp master `src/llama-kv-cache.cpp`, `src/llama-memory-recurrent.cpp`, fetched 2026-09-08]**.
  - The m0-0 receipt: `cp.n_seq_max = 8; cp.kv_unified = true` **[S: `C:/auricle/src/m0/m0_probe.cpp:126-127`]**; `seq_cp` to three branches then `vram_fork = gpu_used_mib()` with no decode between **[S: lines 157-161]**; base model `Qwen3.5-9B-Q5_K_M`, not the v11 tune; "+model/ctx=11618" over base 4615 = 7,003 MiB, which already contains 8 × 50.25 = 402 MiB of slots **[R: `C:/auricle/runs/m0-0-shared-kv-2026-08-07.md`]**.
- **Arithmetic:** per warm cell = 50.25 MiB slot + 500 × 17,408 B = 8.30 MiB KV = **58.55 MiB** (q8_0), 54.65 MiB (q4_0 KV). The recurrent slot is 86 percent of the cell. Section 5's 8.5 MB is the 14 percent.
- **Severity:** blocker. **Confidence:** CONFIRMED.
- **Fix:** put the recurrent slot in the table as its own row; re-size (section 3 below); state the fork cost as "0 B until the first decode, then one slot of 50.25 MiB plus the suffix KV"; make the m0-0 citation say what it measured.

### F2 · The reference implementation "one sequence per warm cell" is capped at 256 sequences by the build the kernel links, and every slot is paid at boot

- **Sections:** 3.4 ("llama.cpp sequences, one root sequence plus one sequence per warm cell"), 5, 6 ("memory pressure: ranked eviction").
- **Claim:** warm cells scale with memory (15,000 to 45,000).
- **Defect:** the substrate has a compile-time sequence cap and an eager per-slot allocation; neither is mentioned. Scratch slots for probes compete for the same cap (three per in-flight judgment, one per seat, because `probe_one` forks per seat).
- **Evidence:**
  - `n_seq_max 300` is refused at context creation: `llama_init_from_model: failed to initialize the context: n_seq_max must be <= 256` **[M: `bench_pps_npl300.txt`, `C:/llama.cpp` build b9627, the binary set beside the DLLs fusord loads]**; `#define LLAMA_MAX_SEQ 256` **[S: llama.cpp master `src/llama-cparams.h`]**; the KV cache's per-cell membership is a bitset over `LLAMA_MAX_SEQ` and `find_slot` is a contiguous scan that fails when `n_tested >= cells.size()` **[S: `src/llama-kv-cache.cpp`]**.
  - `n_seq_max 128` on the 16 GB card: context created (weights 5,657 MiB + KV 272 + RS 6,432 + compute 608 = 12,969 MiB) but the 128-fork decode produced no result row inside a 10-minute wall with a 3 to 5 GB co-tenant on the card **[M: `bench_pps_npl128.txt`, header printed, no data row]**. The slots are bought before the first token.
  - The header comment on the field says it: `n_seq_max; // max number of sequences (i.e. distinct states for recurrent models)` **[S: `C:/auricle/third_party/llama.cpp/include/llama.h:340`]**.
  - fusord runs `cp.n_seq_max = 8` with four named sequences (TRUNK 0, DECIDE 7, GEN 6, SCRIBE 5) **[S: `fusord.cpp:1132`, `:1713`]**; `probe_one` does `seq_rm(DECIDE)`, `seq_cp(TRUNK, DECIDE)`, decode, `seq_rm(DECIDE)` per seat **[S: `fusord.cpp:2048-2057`]**.
  - llama.cpp's paged-KV proposal (Phase 1, April to May 2026) excludes hybrid and recurrent models, defers prefix caching and copy-on-write to Phase 2, treats the 256 cap as "fail admission early", and has no maintainer response **[R: github discussion #21961, fetched]**.
- **Arithmetic:** warm ≤ 256 − 1 (root) − 3 × (judgments in flight). At a probe batch of 16 judgments: warm ≤ 207 on any card.
- **Severity:** blocker for the sizing; major for the design. **Confidence:** CONFIRMED.
- **Fix:** size the warm set as a slot pool of ≤ 255 including scratch; state the cap and the eager allocation in 3.4; put "recompile with a larger `LLAMA_MAX_SEQ` (bitset per cell grows, `find_slot` scan at a million cells is untested)" in section 10 as a decision, not an assumption.

### F3 · The 4-bit KV row is wrong three times over

- **Sections:** 5 ("with 4-bit KV quantization: about 45,000 [BUDGET; the estate's 160k-on-16 GB receipt is the precedent]"), 10 Q1.
- **Claim:** 4-bit KV triples the warm set and the 160k receipt is its precedent.
- **Defect:** (a) q8_0 to q4_0 is 34/32 → 18/32 bytes per value, a factor of 1.89, not 3; (b) the KV is 14 percent of a warm cell, so halving it changes the cell count by about 7 percent; (c) the 160k receipt is a **q8_0** result, and the same receipt found q4_0 **not** quality-neutral on reasoning.
- **Evidence:** `q8_0 ... 163k, quality-neutral; q4_0 ... literal 3/3 at every rung incl. 262k; multi-hop 12/15, scattered misses ... too lossy for the reasoning needle` **[R: `C:/auricle/runs/weff-kvquant-2026-08-10.md`, table and finding 3]**; the kernel's own note `q8_0 KV: quality-neutral (08-11 W_eff receipt, 98k→163k)` **[S: `fusord.cpp:1712`]**; q4_0 per-token from the same 8 layers: 9,216 B **[D]**.
- **Severity:** major. **Confidence:** CONFIRMED.
- **Fix:** delete the row or replace it with "q4_0 KV: about 2,270 cells on an H200 with the cap lifted, and the estate's receipt says 4-bit loses multi-hop reasoning; not a production lever until a margin-level receipt exists". Q1 should ask about the recurrent state's precision (see F12), not KV bytes.

### F4 · The throughput row uses the 16 GB card's three-seat serial constant on a 141 GB card and omits the cold path that F1 and F2 make the majority path

- **Sections:** 5 ("card-time at 110 ms per judgment, serial: about 7.6 hours"; "cold judgments pay a re-render of the suffix, about 500 tokens, before the probe"), 3.5.
- **Claim:** one card judges a large headquarters in 7.6 hours.
- **Defect:** 110 ms is the RTX 4070 Ti SUPER's three-seat probe (p50 113 to 127 ms at 8.2 GB free), not an H200 number; with ≤ 250 warm slots for 50,000 cells the warm fraction is at most a few percent, so nearly every judgment is cold; a cold judgment pays a 500-token prefill that batching does not make cheaper on this card; and if verbs or holds carry generated prose, generation dominates everything.
- **Evidence:**
  - Three-seat probe p50 115 to 127 ms, p95 120 to 144 ms; "the bench's 44 ms is one seat on a free card" **[R: `C:/fusor1/build/BUILD_LOG_2026-09-04.md:57,194`]**; co-tenancy `44 ms free vs 0.6–24.6 s loaded; 104 ms at 15 GiB free vs 500 ms at 10.5 GiB` **[S: `fusord.cpp:1323-1325`]**.
  - Probe frame (25 tokens) prompt-processing on the judge today: B=1 34 ms; B=8 63 ms (7.9 ms each); B=64 621 ms (9.7 ms each) **[M: `bench_probe_prefill.txt`]**. Batching buys 3.5 to 4.3× per probe.
  - Cold suffix (512 tokens): B=1 124 ms (4,123 t/s); B=8 979 ms (122 ms each); B=64 8,752 ms (137 ms each) **[M: same file]**. Batching buys nothing: the card is compute-saturated at 512 tokens. The m0-0 receipt's 4,849 t/s prefill agrees **[R]**.
  - Forks batched for decode: shared 512-token prompt, then 8 forks: 28.6 ms per step (1.43× one fork); 64 forks: 75.9 ms per step (3.8×) **[M: `bench_pps_npl8.txt`, `bench_pps_npl64.txt`]**; m0-0's 1.208× at N=3 agrees **[R]**. Decode across forks batches well; prefill does not on this card.
  - Generated speech: 500 to 700 ms per 28-token line **[R: convergence doc §9.3 D2]**; 10.08 ms per decode step at N=1 **[R: m0-0]**.
- **Arithmetic (4070 Ti SUPER, zero co-tenancy, 250,000 judgments, warm fraction ≤ 0.1):** batched probes: 250,000 × (0.1 × 30 ms + 0.9 × 160 ms) = 10.2 h; serial probes: 250,000 × (0.1 × 120 + 0.9 × 250) ms = 16.5 h. Under the measured co-tenancy (0.6 to 24.6 s probes) the day does not fit at all. Generated prose on every judgment: 250,000 × 0.5 to 0.7 s = 35 to 49 h. **[D]**
- **Severity:** major. **Confidence:** CONFIRMED for every constant; the composition is [D].
- **Fix:** split the row by card and by path (warm, cold, generated), with the constants named per card; on the 16 GB card the honest number is a wire of 5,000 to 15,000 judgments a day in shadow; holds must carry a typed reason code and the margin, never generated prose; acts may generate a line, budgeted.

### F5 · Per-cell NVMe checkpoints of warm pages duplicate the shared root into every file and cannot re-share it on restore; on this box a 60 MB checkpoint took 4 seconds at the median

- **Sections:** 3.4 ("warm pages may be checkpointed to NVMe with a `.meta` ... the same guards as fusord's `checkpoint` and `restore`").
- **Claim:** warm entries persist across peer restarts.
- **Defect:** `llama_state_seq_save_file` serializes every cell that carries the sequence id, which for a cell sequence forked off the root includes all 8,000 root cells; `llama_state_seq_load_file` assigns loaded cells to the destination sequence only, so a restored cell owns a private copy of the root instead of sharing it, and the warm set shrinks by 132 MiB per restore. There is no range parameter in the API. The write path on this box is also slow.
- **Evidence:** state write loops `cells.seq_has(i, cur)` over all cells; state read builds a ubatch with `ubatch.seq_id[i] = &dest_seq_id` **[S: `src/llama-kv-cache.cpp`, fetched]**; the API has no p0/p1 on save or load **[S: `llama.h:859-872`]**; fusord's `checkpoint` writes `TRUNK` whole and `restore` requires an empty destination (`llama_memory_seq_rm(mem, TRUNK, -1, -1); // the load wants an empty destination`) **[S: `fusord.cpp:1516`, `:1588`]**. Durations of periodic checkpoints on this box for files of 57 to 62 MB: n = 1,723 rows, min 56 ms, **p50 4,328 ms, p90 8,967 ms, max 16,067 ms** **[R: `C:/fusor1/converge/smoke/{t3,t6,incident_2026-09-04/t3,incident_2026-09-04/t6}/fusor_ledger.jsonl`, `k:ckpt why:periodic dur_ms`]**; the smoke-1 pair was 56 and 92 ms from page cache **[R: convergence §7.3]**.
- **Arithmetic:** per cell file = 52.7 MB + (8,000 + 500) × 17,432 = 200.9 MB; 2,000 cells = 402 GB; at this box's median write rate about 14 s per cell. **[D]**
- **Severity:** major. **Confidence:** CONFIRMED.
- **Fix:** delete per-cell checkpoints from v0. Law 5 already says the peer is a cache; a cold peer rebuilds the root once (8k prefill, 1.7 s on this card) and re-derives cells on touch. Keep one checkpoint for the root sequence only, with fusord's `.meta` guards verbatim.

### F6 · The key omits the root pin, the root's own change rate is unspecified, and the seam between a judgment and its write is missing

- **Sections:** 2.5 (key `(cell_id, pos_last, template_pin, judge_pin)`; "the root is shared ... the writ digest, the schema, the precedent digest"; invalidation "when the cell or any cell in its rendered neighborhood is touched, or when the template pin changes"), 2.3, 3.5, 10 Q9.
- **Claim:** the index is a pure function of the key; a warm entry is a read state at `pos_last`.
- **Defect:** every warm entry's attention over the root and every entry's recurrent state descend from the root bytes, so an entry is a function of `(root_pin, cell_id, pos_last, template_pin, judge_pin)`; the key drops the first term, and the invalidation rule has no clause for root movement other than the template pin. If the "precedent digest" moves per judgment, every entry is invalid after every judgment and the index is never warm. The as-of-now property from the convergence document is not contradicted, because TAPESTRY never rewinds a fork; it re-derives from the tape, so a warm entry is honestly a judgment as of `(root_pin, pos_last)`. What is missing is the seam: if a fact for the cell lands between the render and the verb write, the verb was judged against a state that no longer exists, and nothing in 3.1 or 3.5 refuses it.
- **Evidence:** the recurrent state cannot be truncated at `n_rs_seq 0`: `llama_memory_seq_rm` "Returns false if a partial sequence cannot be removed" **[S: `llama.h:717-722`]**; the measured no-op and the stale-logit failure **[R: convergence §6.4, B.7; `fusord.cpp:2036-2047`]**; fusord's answer for its stream is `at`, `now`, `late`, `covers` on the boundary row **[S: `fusord.cpp:2172-2175`, `:2321-2323`]**; the state file's linear fit shows the root's 8,000 tokens are 139 MB of every cell's context **[M]**.
- **Severity:** major. **Confidence:** CONFIRMED for the dependence; the seam gap is PLAUSIBLE (an omission, not a measured failure).
- **Fix:** key = `(root_pin, cell_id, pos_last, template_pin, judge_pin)`; every `verb` and `hold` entry carries `root_pin`, `pos_last` read, and `pos_judged` (the peer's applied position at probe time); the transactor refuses a `verb` whose cell `pos_last` has moved since `pos_last` read (`refuse: stale_judgment`), the optimistic-concurrency check that replaces fusord's seam; the root's components get declared update cadences by `rule` (the precedent digest per period, never per entry); a `rule` that changes the root is followed by a staged re-warm whose cost is printed: on the 16 GB card about 70 cells × 124 ms = 9 s; on an H200 2,100 cells × about 35 ms = 74 s **[D]**. For replay over history (3.8), the folds must render the root as of any position, which means the writ, schema and precedent history live on the tape (add to section 10).

### F7 · The determinism claims assert what GPU batching does not provide, the seed is irrelevant to a margin, and falsifier 6 has no noise floor

- **Sections:** 4 ("a fixed seed per judge invocation recorded on the entry ... two cold rebuilds from one tape are bit-identical in every fold and every shadow tape [M: o_projection_order]"), 9.1, 9.6, 3.5.
- **Claim:** shadow tapes are bit-identical across rebuilds; margins after eviction and rebuild match "within the run's noise".
- **Defect:** (a) the margin is `l[emit_tok] - l[hold_tok]` read off the logits **[S: `fusord.cpp:2053-2054`]**; no sampler touches it, so a seed says nothing about it (the seed governs generated text only, and it is already fixed at 11 by the do-not-touch list **[S: `fusord.cpp:1728`]**); (b) logits on a GPU depend on the batch composition through the reduction order in matmul, normalization and attention, so a judgment co-batched with different neighbours, or run at a different batch width, differs in low bits, and the recurrent layers' chunking follows the ubatch split; (c) across cards the kernels differ; (d) the `o_projection_order` receipt is a host test that the fixed-point lattice projection is order-independent and that float accumulation differs by order, which says nothing about a judge's logits; (e) "within the run's noise" is undefined, and a planted wall-time token that moves the margin by less than the noise would pass.
- **Evidence:** batch invariance as the root cause of inference nondeterminism, with RMSNorm, matmul and attention as the batch-variant kernels and a 1.6 to 2.1× cost for batch-invariant versions (26 s → 42 to 55 s) **[R: Thinking Machines, 2025-09-10, fetched]**; LMCache on hybrids: "Generation is not bit-exact between a cached and a fresh run", validate at score level **[R: docs.lmcache.ai hybrid models, fetched]**; the test body `o_projection_order` **[S: `C:/TAPESTRY/qc/scratch/cell/osv_core_test.cpp:110-141`]**.
- **Severity:** major. **Confidence:** CONFIRMED for (a), (d), (e) and for the mechanism in (b); the magnitude on this build is unmeasured, which is the point.
- **Fix:** a scoped contract. Bit-identity is asserted only within a **registration tuple** `(judge weight hash, serve pin, llama.cpp build hash, card model, driver, batching policy)` recorded on the `judge` entry, where the batching policy is itself a fold over the tape (batch = judgments pending at position p in cell-id order, width fixed at registration), so replay reproduces the same ubatches. Across tuples assert verb identity plus |Δmargin| ≤ ε. **Noise floor protocol for falsifier 6:** 32 cells spanning the margin range, each judged 16 times (4 alone, 4 co-batched with 7 random others, 4 with 63 others, 4 after a cold boot); noise_p95 = the 95th percentile over cells of the spread; record it on the judge entry; then falsifier 6 asserts (i) the rendered canonical bytes are bit-identical after evict-and-rebuild (deterministic, and it is what catches the wall-time lie), (ii) |m_rebuilt − m_control| ≤ noise_p95, (iii) verb identity unless the control margin lies within noise_p95 of a gate threshold, in which case the gate's fetch band should have abstained, and (iv) the planted lie moves the margin by more than 3 × noise_p95 on at least one cell, or the test has no teeth. If noise_p95 measures 0 on this build, bit-identity within the tuple is a finding, not an assumption.

### F8 · The serve pin value and the seat vocabulary cannot transfer; the mechanism can

- **Sections:** 11 (`probe_one`, `speak`, `serve_hash` "become the judge runtime"), 10 Q10 ("the serve pin and the seat vocabulary must not move"), 3.5.
- **Claim:** fusord's judge transfers with its pin.
- **Defect:** the pinned serve bytes are a chat-stream watcher: "silently shadowing a live, continuous work stream: an operator and their AI assistant, working ... hold (stay silent) or emit (speak now)" with three seats named SPEAKER, SKEPTIC, SENTINEL and a probe frame of seat name plus mandate **[S: `fusord.cpp:937-979`]**; the pin `0xe7ffa5704ba31076` covers exactly those literals and is asserted before the model loads with exit 2 on drift **[S: `:982-993`, `:1612-1627`]**; "Re-pinning is a retune, in the same commit" **[R: convergence §9.4]**. A judge that reads a rendered commitment under a class template and emits a margin for act/work/fetch/escalate/hold is a different serve format. Either the commitments are rendered as a fake chat stream to keep the pin (the tune's conditioning becomes a lie) or the judge is a new tune with a new pin. Q10 as written forbids the only honest option. A second consequence: three seats cost three forks and three scratch slots per judgment; the new tune can carry one probe frame that yields every seat's margin from one decode.
- **Severity:** major. **Confidence:** CONFIRMED.
- **Fix:** rewrite Q10: the pin *mechanism* (`serve_hash`, the boot assertion, `.meta` `serve` guard, `--serve-hash`) transfers verbatim; the pin *value*, the seat names, the probe and cue frames are retuned for the commitment judge and re-pinned in the tune's commit; the `judge` entry's `serve_pin` is that new value.

### F9 · The judge emits a margin; the verb belongs to `gate()`, which the blueprint never places

- **Sections:** 3.5 ("emits a `verb` or `hold` entry"), 3.3, 2.4.
- **Claim:** the judge runtime decides the verb.
- **Defect:** the only verb function in the estate takes the judged margin plus the cell's flow pressure, the foresee dispersion, `n_eff` and `calibrated`, escalates when `n_eff < 175` or uncalibrated, and holds when blocked **[S: `osv_core.cuh:238-266`]**. The blueprint's judge runtime goes from margin to entry with no gate call, no source for `dispersion`, and no statement of which fold supplies `n_eff`/`calibrated` per cell. A judge that writes verbs directly bypasses the floor that "makes any of this delete anything".
- **Severity:** major. **Confidence:** CONFIRMED (absence in the text against a present function).
- **Fix:** the judge emits `margin` only; the peer applies `gate()` with `pressure` from the field fold and `n_eff`/`calibrated` from the license fold; the entry records the gate inputs beside the margin; name the source of `dispersion` or set it to zero with a `[BET]`.

### F10 · Substrate: neither vLLM nor SGLang removes the wall for a hybrid judge; they remove it for a pure-attention judge

- **Sections:** 3.4, 10 Q1, Q2.
- **Claim:** llama.cpp sequences are the v0 reference and paged KV is implied for later.
- **Defect:** the blueprint compares substrates as if the judge architecture were fixed and neutral. It is neither.
- **Evidence:** vLLM sets the attention block to 528 tokens for Qwen3.5 hybrids "to ensure that attention page size is >= mamba page size", prefix caching is 0 percent for prompts under one block, open as of 2026-04-23 **[R: vllm #40696, fetched]**; LMCache align mode snapshots the Mamba state only at block boundaries, block sizes 544 to 944 tokens by model **[R]**; SGLang's Unified Radix Cache (2026-08-11): "A recurrent state is valid only at an exact prefix checkpoint", one checkpoint at the reusable frontier, copied into a private slot before mutation **[R]**; Marconi (MLSys 2025): hybrid reuse is all-or-nothing, naive caching produces "a deluge of (large) cache entries per sequence", admission must weigh compute saved against memory **[R: arXiv 2411.19379]**. Those engines keep the recurrent state in the model dtype (bf16, half of llama.cpp's f32 slot) but the shape of the cost is the same. For a pure-attention judge the picture inverts: the index is the token-prefix radix tree, the key is the prefix hash (so a root change invalidates by construction), truncation and as-of-then work, no per-sequence fixed cost, and copy-on-write blocks exist. Moving the existing judge to vLLM/SGLang would also move three do-not-touch items at once (q8_0 KV → fp8/bf16, `kv_unified` → paged blocks, the sampler chain → theirs) and requires non-GGUF weights, which is a different weight hash and therefore a different registered judge.
- **Severity:** major for the roadmap. **Confidence:** CONFIRMED for the facts; the recommendation is design.
- **Fix:** v0 stays on llama.cpp with a slot pool ≤ 255, batched cold re-derivation as the primary path, and Marconi-style admission (warm only what will be re-touched inside the pool's horizon). Q1 becomes "which judge architecture", measured on two candidates: this hybrid (58.5 MiB per warm cell, prefill 124 ms per 500 tokens on the 16 GB card) against a pure-attention model in the same class (per-cell cost = suffix KV only; for a 36-layer GQA-8 8B at q8_0 about 39 MB per 500 tokens, no slot, no cap on divergence beyond memory, radix caching available).

### F11 · Falsifier 7 passes vacuously

- **Section:** 9.7 ("N forks of one read state consume pages only as they diverge. Lie: eager copying.").
- **Defect:** on this substrate the VRAM delta at `seq_cp` is 0 by construction because every slot was allocated at boot; the test as written cannot fail, and the real eager cost (n_seq_max × 50.25 MiB before any fork exists) is invisible to it.
- **Evidence:** F1, F2 receipts.
- **Severity:** minor (a test defect). **Confidence:** CONFIRMED.
- **Fix:** falsifier 7 measures (i) the RS and KV buffers at boot as a function of `n_seq_max` and asserts the sizing table's slot cost, (ii) bytes per diverged fork = slot + suffix KV, (iii) that undiverged forks add 0 B; the planted lie becomes "a fork that decodes eagerly".

### F12 · "Cannot be rewound" is a property of `n_rs_seq 0`, and the layer counts in the estate's notes are wrong

- **Sections:** 2.5, 3.5 (citing the convergence document), 5 ("the 9B hybrid judge").
- **Defect:** the vendored header the kernel builds against exposes `n_rs_seq` ("number of recurrent-state snapshots per seq for rollback") **[S: `llama.h:341`, `:545`]**; the recurrent memory allocates `n_rows = mem_size * (1 + n_rs_seq)` and partial `seq_rm` succeeds when the rollback is within `n_rs_seq` **[S: `src/llama-memory-recurrent.cpp`, fetched]**; fusord runs at `n_rs_seq = 0` **[M: bench logs; fusord sets none]**. So a fork can be rewound by up to k tokens at k × 50.25 MiB per sequence, which is useless at position granularity but must be stated as the condition. Separately, estate notes carry "48 delta-rule layers" and "25 of 33 SSM"; the truth is 24 gated-delta-net layers, 8 attention layers, 1 NextN layer **[R: GGUF; M: RS buffer arithmetic]**. The recurrent slot is f32 in llama.cpp; vLLM/SGLang hold it in bf16.
- **Severity:** minor. **Confidence:** CONFIRMED.
- **Fix:** cite the property as "at `n_rs_seq 0`"; carry the correct decomposition in section 5; add "is a bf16 recurrent state margin-neutral" to section 10, because it is the one 2× lever on the per-cell cost that exists.

### F13 · Terminology: the unified KV is share-and-append, never copy-on-write

- **Section:** 2.5 ("a fork shares pages and copies on write").
- **Defect:** forks cannot modify shared cells; new tokens go to new cells and `seq_rm` on the fork only removes its id from the bitset. The recurrent state is copy-on-first-decode. The phrase invites a falsifier for a mechanism that does not exist.
- **Evidence:** F1's source citations.
- **Severity:** minor. **Confidence:** CONFIRMED.
- **Fix:** "a fork shares the root's KV cells by reference and appends its own; its recurrent state is copied into its own slot at its first decode".

### F14 · Two receipts cited in section 5 are not on disk as runs

- **Section:** 5 ("exhaustive embedding scan 1.67 M vectors in 4.88 ms [M: CORTEX]").
- **Defect:** the only occurrences found are chat-export mentions ("Your own CORTEX measurement (4.88ms over 1.67M vectors)"), no run file **[R: `C:/auricle/claude_chat_export_2026-08-15zzz/...chat.md:19940`; GPU grep of `C:/auricle`, `C:/fusor1`, `C:/55555`]**. Not this slice's core, but a row marked [M] must name a file.
- **Severity:** minor. **Confidence:** CONFIRMED (absence).
- **Fix:** name the receipt or mark the row [R: chat] until it is re-run.

---

## 3 · Corrected sizing table, every assumption stated

**Assumptions.** Judge = `Qwen3.5-9B-emit-v11-Q5_K_M` as registered; substrate = llama.cpp b9627 as linked by fusord, `kv_unified`, q8_0 K/V, flash attention, `n_rs_seq 0`; root 8,000 tokens and suffix 500 tokens are the blueprint's [BUDGET] figures, kept; three seats, one scratch slot per seat per in-flight judgment; probe batch of 16 judgments (48 scratch slots) unless stated; card A = RTX 4070 Ti SUPER 16,376 MiB with a co-tenant using 3,009 to 5,374 MiB (measured today); card B = H200 taken as 141 × 10^9 B = 134,468 MiB; H200 timings are [BUDGET] scaled from card A by about 4× on prefill and decode with no receipt.

| quantity | blueprint | corrected | class |
|---|---|---|---|
| hot row per cell | 64 B | 64 B | SPEC |
| judge weights on device | 7 GB | 5,657 MiB device + 667 MiB host-mapped | M |
| compute and output buffers | absent | 501 to 608 MiB compute; 0.95 MiB per output slot | M |
| KV per token, q8_0 | about 17 KB | 17,408 B (17,432 in a state file) | M |
| KV per token, q4_0 | implied 1/3 | 9,216 B; not margin-validated; the receipt says 4-bit loses multi-hop | D, R |
| recurrent state per sequence slot | absent | 52,690,944 B = 50.25 MiB, f32, × n_seq_max at boot | M |
| sequence cap | absent | n_seq_max ≤ 256 | M |
| shared root | 8,000 tok · about 136 MB | 132.8 MiB KV + one slot = 183 MiB | D |
| per warm cell | 8.5 MB | 58.55 MiB (q8_0) · 54.65 MiB (q4_0) | D |
| per probe in flight | 0 | one slot + about 0.5 MiB KV; three per judgment | D |
| warm cells, card A, co-tenant 5.4 GB, 48 scratch | not sized | (11,002 − 5,657 − 608 − 183 − 2,412) / 58.55 ≈ 36 | D |
| warm cells, card A, co-tenant 3.0 GB, 12 scratch | not sized | (13,367 − 5,657 − 608 − 183 − 603) / 58.55 ≈ 107 | D |
| warm cells, any card, this build | 15,000 | ≤ 256 − 1 − scratch ≈ 207 | M |
| warm cells, card B, cap lifted, q8_0 | 15,000 | (134,468 − 5,657 − 1,000 − 183 − 2,412) / 58.55 ≈ 2,130 | D |
| warm cells, card B, cap lifted, q4_0 | 45,000 | ≈ 2,280 | D |
| warm judgment, three seats, card A | 110 ms | 113 to 127 ms serial [M]; about 30 ms with probes batched (9.7 ms per probe at B=64) [M, D] | |
| cold judgment, card A | "a re-render" | warm + 124 to 137 ms suffix prefill, batching does not help | M |
| warm / cold judgment, card B | 110 ms | about 10 ms / about 45 ms | BUDGET |
| judgments per day | 250,000 | 250,000; bursts to about 9 per second in an 8-hour day | BUDGET |
| card-time per day, card A, ≤ 10 percent warm | 7.6 h | 10.2 h (batched probes) to 16.5 h (serial); does not fit under the measured co-tenancy | D |
| card-time per day, card B, 30 percent warm | 7.6 h | about 2.3 h, mostly cold | BUDGET |
| generated reason line | absent | 0.5 to 0.7 s per 28 tokens on card A; 250,000 lines = 35 to 49 h; holds must not generate | R, D |
| root re-warm after a root rule change | "the cost is printed" | card A about 70 × 124 ms ≈ 9 s; card B about 2,100 × 35 ms ≈ 74 s | D, BUDGET |
| per-cell NVMe checkpoint | "warm pages" | about 201 MB per cell (root duplicated); this box p50 4.3 s per 60 MB | D, M |
| exhaustive embedding scan | 4.88 ms / 1.67 M | no run file found; mark [R: chat] | R |

Reading the table: on the estate's own card the cognitive index is a cache of a few dozen cells in front of a cold path that costs about a quarter of a second per judgment; the card can shadow a wire of five to fifteen thousand judgments a day, not a headquarters. On an H200 with the cap recompiled away it is a cache of about two thousand cells, four percent of the open set, in front of a cold path that fits the day. In both cases the warm set is decided by admission economics (Marconi), not by "the ranked top third".

---

## 4 · What is right and must not be changed

- **The constants the blueprint cites are correct where cited:** 17,432 B per token and the 52.7 MB fixed part reproduce to zero residual over about 100 checkpoints; q8_0 KV is quality-neutral to 163k on this card; `seq_cp` of the unified KV is a 0 B share. The defect is what the table omitted, not what it measured.
- **q8_0 KV, `kv_unified`, flash attention, decode-failure-is-fatal, the `.meta` guards (model path, weight sha256, serve hash, token count), `serve_hash` as a boot assertion, the module gate, `model_identity`** transfer verbatim and should. Keep `kv_unified`: the shared root is the one thing on this substrate that is genuinely free.
- **The batched co-decode pattern** is real and stronger than the blueprint uses: 64 forks decode at 3.8× the cost of one; probes batch at 3.5 to 4.3× per probe. Section 5 should lean on it for probes and forks, and not for prefill on this card.
- **Law 5, the peer is a cache,** is what makes "cold by default" acceptable; do not weaken it to justify checkpoints.
- **The deposit clock in the key** (`pos_last`), the hold as a row with its margin, the judge identified by weight hash plus serve pin, invalidation on cell or neighbourhood touch: all right, all kept; add `root_pin` beside them.
- **The as-of-now doctrine** from the convergence document is honoured by re-derivation from the tape; TAPESTRY never needs to rewind a fork, which is the correct way around the measured property.

---

## 5 · Top three changes

1. **Re-size section 5 on the measured per-warm-cell cost (50.25 MiB recurrent slot + 8.3 MiB suffix KV) and the 256-slot cap:** about 50 to 100 warm cells on the 16 GB card, about 200 on any card with this build, about 2,100 on an H200 with the cap lifted; make batched cold re-derivation the primary path, warm admission Marconi-style, and add the judge-architecture decision (hybrid versus pure attention) as the first open question because it moves every number in the table by an order of magnitude.
2. **Add `root_pin` to the index key and to every `verb`/`hold` entry, refuse a verb whose cell moved since it was read (`stale_judgment`, the optimistic-concurrency seam), declare the root's update cadences by `rule`, and delete per-cell NVMe checkpoints** (the API duplicates the root into each file and this box wrote 60 MB checkpoints at a 4.3 s median).
3. **Replace "bit-identical shadow tapes" and "within the run's noise" with a scoped determinism contract** recorded on the `judge` entry (build, card, driver, batching-policy-as-a-fold), a measured noise floor, verb identity plus ε across registrations, and a falsifier 6 that asserts canonical-byte identity and proves the planted lie exceeds the floor; and retune the judge for commitments with a new pin and one multi-seat probe frame, because the v11 serve bytes are a chat watcher and cannot move to TAPESTRY unchanged.

---

## 6 · Open questions to add to section 10

1. **Judge architecture.** Hybrid (fat per-sequence f32 state, cheap per token, no truncation) versus pure attention (no fixed cost, radix or paged prefix caching, truncation and as-of-then work): measure both on the tenancy receipt before R3; the cognitive index's economics flip with the answer.
2. **What the root contains and how often each component moves.** Writ digest, schema pin, precedent digest: declared cadences by `rule`; the precedent digest must not move per entry.
3. **The invalidation rate on a real wire.** Touches per cell per day and neighbourhood fan-out from the Olist conformance wire, so the warm hit rate is a measurement before the warm set is sized.
4. **The determinism tuple.** Which of (build hash, card model, driver, batching policy) is pinned on the `judge` entry; the measured noise floor; ε across cards for jury quorum, and whether a margin inside ε of a threshold must abstain.
5. **Reasons.** Typed codes on holds; generated prose only on acts, with a daily generation budget in the sizing table.
6. **Where `gate()` runs** and what supplies `dispersion`, `n_eff`, `calibrated` per cell; whether the entry carries the gate inputs.
7. **The scratch-slot pool.** Probes in flight per card, one probe frame per seat or one multi-seat frame (a retune question), and the slot budget it takes from the warm set.
8. **Lifting the cap.** Recompile llama.cpp with a larger `LLAMA_MAX_SEQ` (per-cell bitset grows; `find_slot`'s linear scan at a million cells is untested; llama.cpp's paged-KV Phase 1 excludes hybrids) versus waiting for a substrate that pages recurrent state.
9. **Replay over history needs the root as of any position.** Are the writ, schema and precedent histories themselves on the tape so a fold can render the root at `from_pos`?
10. **Recurrent state precision.** llama.cpp holds it in f32; vLLM and SGLang in bf16 (half the slot). Is a bf16 state margin-neutral, and is there a switch worth adding to the vendored build?

---

## Appendix · Reuse of `fusord.cpp` in the judge runtime, function by function

| fusord | lines | transfers verbatim | needs rewriting | conflicts with §9.4 |
|---|---|---|---|---|
| `serve_hash`, `SERVE_HASH_PIN`, boot assertion, `--serve-hash` | 982-993, 1612-1627, 2730-2735 | the mechanism | the literals and the pin value (a retune) | the value must move; "re-pinning is a retune, in the same commit" applies |
| `module_gate`, `load_backends_by_name` | 400-448 | yes | no | none |
| `model_identity`, `sha256_file` | 449-500 | yes | no | none |
| `checkpoint`, `restore`, `parse_meta`, `.meta` guards | 1511-1608 | yes, for the root sequence only | per-cell use is not expressible (F5) | none |
| `probe_one` | 2048-2057 | the shape: fork, decode frame, read `l[emit] - l[hold]`, release | fork from the cell's sequence, not TRUNK; batch N cells × seats in one `llama_decode` with per-token seq ids and `n_outputs_max`; a scratch-slot pool under the cap; a new probe frame | the probe frame literal (retune) |
| `decode` wrapper, fatal on failure, `read_vram` | 1241, 1372-1379, 1326-1345 | yes | no | none |
| `speak`, `sample_from` | 2068-2166, 1807-1816 | only if acts generate a line | the seam (`drain_intake`, re-probe, kill) is replaced by the transactor's `stale_judgment` refusal | sampler chain and 28-token cap apply only if kept |
| `judge_and_maybe_emit` | 2172-2352 | the record discipline: one row per seat per boundary, holds with margins, wire rows carrying tape hashes | the manners layer (dup, refractory, acceptance, resolved) is chat-specific and does not transfer; the row shapes become `verb`/`hold` entries with `root_pin`, `pos_last`, `pos_judged` | seat names and mandates (retune) |
| `do_molt`, scribe rung, reseed | 1440-1502 | no | not needed: the root is static and cells are rebuilt | molt cue and reseed become irrelevant, not violated |
| `read_frontier`, segmenter, nerve, flush law, ticks | 1349-1361, 1400-1437, 2359-2366 | no | no stream to segment; the deposit clock is the position | flush law and segmenter set become irrelevant |
| gear 2 (`write_brief`, `counsel_admit`, `prune_briefs`) | 1958-2034 | the staleness envelope idea | maps onto the `fetch` verb at the gate | none |
| `LaneTail`, `Ring`, cursor | 526-776 | as the subscribe transport (§11 already says so) | frames become tape deltas with positions | none |
| `Tape`, `chain_hash`, `Blake2b`, `write_atomic`, `replace_file` | 230-395, 859-911 | yes (another slice's subject) | none | none |
| `Reservoir`, `Pill`, `Vitals` | 785-853 | yes | add warm ratio, slot occupancy, cold prefill p50/p95 | none |

*Written 2026-09-08 by Claude Fable 5.1 as QC reviewer 3. Every [M] above was produced today by a command named in the text with its raw output kept under `C:/TAPESTRY/qc/scratch/ci/`; every [R] and [S] names its file and line. The tape, as always, is the proof.*
