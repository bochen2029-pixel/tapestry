# QC-6 · RED TEAM · TAPESTRY ARCHITECTURE BLUEPRINT v0.1

**2026-09-08 · Dallas · Claude Fable 5.1 (`claude-fable-5-1`), red-team subagent of the `C:\55555` session.** Subject: `C:/TAPESTRY/TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1_2026-09-08_FABLE5-1.md` (read twice, in full). Read in full beside it: the brainstorm, the seven Org Solver and forklift documents in `C:/55555/`, `C:/fusor1/FUSOR_KERNEL_CONVERGENCE_2026-09-04_FABLE5-1.md`, `C:/fusor1/FUSOR_CRYSTALLIZATION_W_WHOLE-READ_2026-09-04_FABLE5-1.md`, the three `osv_*` headers and both `osv_*_test.cpp` files in `C:/Websites/aorta-site/_upload/`, and the load-bearing regions of `C:/fusor1/converge/src/fusord.cpp` (header, clocks, `Blake2b`/`chain_hash`, `module_gate`, `Tape`, `Config`, boot context parameters, `checkpoint`, `restore`, `probe_one`, `judge_and_maybe_emit`). Receipts were hunted with `everywhere.exe` across `C:/fusor1`, `C:/NEW`, `C:/IT`, `C:/auricle`; the GGUF header of the judge was read directly; the arithmetic below is pinned in `C:/TAPESTRY/qc/scratch/red/gguf_keys_and_sizing.py`. Register: every claim names its file and line. Nothing here softens. Where this file and a dated receipt disagree, the receipt wins.

Line numbers for the blueprint are the file's own (L1 to L303). Line numbers for other files are as read today.

---

## 1 · Verdict

The blueprint does not survive as written: its one novel object, the per-cell cognitive index with copy-on-write forks, is sized for an attention-only model, and the judge it names is a 3:1 recurrent hybrid on which every sequence carries a 52.7 MB recurrent state, so the warm set is 2,200 cells rather than 15,000 and the "fork that costs nothing" cannot pass its own receipt gate on that model. Independently of sizing, the blueprint commits the exact error of law the reconciliation document called out by name: the judge authors the verb, the seam has no component, the license, demotion and evidence floors have no operation in the write path, and irreversible writes are signed by judge keys, which the runbook, the OSV gate, and two of the fifteen falsifiers the blueprint claims to carry forward all forbid. The tape, the folds with cold-versus-incremental verification, the monotone license property with per-verb horizons, and the hold-and-refusal record survive every attack and are worth keeping; the design lives if the seam is put back and the cognitive index is re-sized or re-modeled for the hybrid, and dies if either is left as is.

---

## 2 · The ten findings, ranked

### F1 · The cognitive index is sized for a model the blueprint did not choose; on the 9B hybrid every sequence carries a 52.7 MB recurrent state

**Claim.** §2.5 L132: a fork "shares pages and copies on write, which is `llama_memory_seq_cp` under `kv_unified` **[M: fork at 0 MiB, m0-0]**"; L134 "the per-cell cost is the suffix, not the whole context". §5 L204-208: KV per token about 17 KB, per-cell suffix 8.5 MB, warm cells per 141 GB card about 15,000, about 45,000 with 4-bit KV. §9.7 "N forks of one read state consume pages only as they diverge." §8 R3's receipt gate: "the fork that costs nothing."

**Attack.** The judge is `C:/models/Qwen3.5-9B-emit-v11-Q5_K_M.gguf` (fusord.cpp L1059). Its GGUF header, read today: `qwen35.block_count = 33`, `qwen35.full_attention_interval = 4`, `qwen35.attention.head_count_kv = 4`, `key_length = value_length = 256`, `qwen35.ssm.inner_size = 4096`, `ssm.state_size = 128`, `ssm.group_count = 16`, `ssm.time_step_rank = 32`. That is 8 attention layers and 25 gated-delta recurrent layers (which also settles the whole-read's "8 of 33 versus 16 of 64" dispute at §9 L245 for the 9B: 8 of 33). Attention KV per token at q8_0: 8 × 4 × 256 × 2 × 1.0625 B = 17,408 B, which is the receipted 17,432 B/token. Recurrent state per sequence: 25 × 32 × 128 × 128 × 4 B = 52.4 MB. Every checkpoint on disk agrees: (434 tok, 60,257,248 B), (512, 61,616,944), (1005, 70,210,920), (1247, 74,429,464), (2343, 93,534,936) fit 17,432 B/token plus 52,691,760 B fixed with zero residual across three tapes (`C:/fusor1/build/smoke1/tape.jsonl`, `C:/fusor1/converge/smoke/*/fusor_ledger.jsonl`). The convergence document says it in words: "about 52.7 MB fixed (the recurrent state)" (§7.3 L178) and "`seq_cp` copies a recurrent state, not just cell membership" (§6.4 L152). The vendored `llama.h` L340 defines `n_seq_max` as "max number of sequences (i.e. distinct states for recurrent models)": one full recurrent state per sequence, reserved at context creation.

**Why the receipt reads 0 MiB.** `m0_demo.cpp` (`C:/auricle/src/m0/m0_demo.cpp` L75-76, L96) ran on the same 9B hybrid with `n_seq_max = 8; kv_unified = true` and measured VRAM delta at fork time. With eight recurrent slots pre-reserved at boot, a fork is a device memcpy into a slot already paid for, so the delta is zero. `C:/IT/OBSERVATIONS.md` L527 reads the receipt as "fork 0 MiB (seq_cp refcount; copy ≈768 MiB)". The receipt is real and on the right architecture; it measures the moment of forking, not the memory a sequence occupies. The blueprint cites it for the latter.

**Arithmetic (script in scratch).** Free after 7 GB weights and the root: 133.8 GB. Per warm cell on the hybrid: 8.7 MB attention suffix (500 × 17,432, and that is already q8_0: fusord.cpp L1713-1716 runs `GGML_TYPE_Q8_0` KV by default) plus 52.7 MB recurrent state = 61.4 MB. Warm cells: **2,179**, not 15,000 (7×). Four-bit KV halves only the attention half: **2,345**, not 45,000 (19×; the blueprint's 3× step from 15,000 to 45,000 also assumed the 17 KB was unquantized). Reserving 15,000 sequences would take 790 GB of recurrent state at init; 45,000 would take 2.4 TB. The NVMe checkpoint of a warm cell (§3.4) is 61 MB, not 8.5 MB. And `llama.cpp` compiles a hard cap on sequence ids (`LLAMA_MAX_SEQ`; 64 in mid-2025 trees, 256 later; the source is not vendored on this box so the value fusord links against could not be pinned), while fusord itself runs `n_seq_max = 8` (L1713); "one sequence per warm cell" at any of the blueprint's counts is outside every value that constant has had.

**Corollaries the blueprint inherits.** (a) A rebuilt cell cannot reattach stored attention pages to a fresh sequence without re-decoding the suffix, because the recurrent state after the suffix must be recomputed; on a hybrid the per-cell index degenerates to "cache the root, re-decode 500 tokens per judgment", which is a prompt cache, not an index. (b) `llama.h` L382-384 says of `kv_unified`: "try to disable when n_seq_max > 1 for improved performance when the sequences do not share a large prefix"; the blueprint's regime (an 8,000-token shared prefix, then 15,000 divergent 500-token suffixes in one unified buffer of 7.5 M cells) is a regime no receipt covers; the 110-130 ms probe constant was measured with `n_seq_max = 8` and at most 2,343 live cells. (c) The "160k-on-16 GB receipt" §5 L208 leans on is operator-attested, not measured (`C:/NEW/BRAIN_RECONTEXT_FUSOR-AT-CENTER_2026-08-21.md` L133: "Operator-attested [OA]: 160k+ TurboQuant no-knee"), for recall, and `C:/NEW/FORK2_HANDOFF_SNAPSHOT_FABLE5_2026-08-16.md` L35 names the open item "emit-margin/boundary-mass fidelity under quantized KV"; §10 Q1 concedes it.

**Verdict.** Dies as designed on the chosen judge. Survives only by one of: an attention-only judge (which voids every [M] constant borrowed from the 9B: probe cost, KV/token, the seam finding, the pins); or accepting about 2,200 warm cells on a 141 GB card and re-deriving §5; or dropping the per-cell index for the hybrid and keeping the root cache with per-judgment suffix decode, which is what the arithmetic already forces.

---

### F2 · The seam is erased: the judge authors the verb, and the license, demotion and evidence floors have no operation in the write path

**Claim.** §3.5 L158: the judge runtime "reads the margin as emit minus hold on the fork ... and emits a `verb` or `hold` entry through the transactor stamped with the judge's pins." §2.1 L70: the `verb` entry's provenance is `by: judge:<weight_sha256>@<serve_pin>`. Law 6 L21: "The judge is a procedure whose body is weights."

**Attack.** The reconciliation is explicit that this is an error of law, not of arithmetic: "JUDGE = Judge + Seam ... `probe_one` returns a float, and `gate` returns a verb ... A single operation that reads the fork and emits the verb has no place where the fence lives, and every safety receipt in the corpus sits on the boundary they erased" (`ORG-SOLVER_RECONCILIATION_FIVE-VS-EIGHT` §1 L10). The Eight: "the pass proposes, the seam disposes, and neither reduces to the other" (§1 L42). The whole-read's law 7: "Nothing learned may dispose" (§3 L101). In shipped code the seam is `gate` (`osv_core.cuh` L254-266): margin plus pressure, refused outright on `F_WARRANT`, `F_BLOCKED`, `n_eff < n_eff_floor`, `!calibrated`, high dispersion; then `Dispatch::run` pass 2 (`osv_dispatch.h` L236-260) rewrites escalations into holds under the human budget, which the reconciliation calls "the safety property ... lives in that pass and nowhere else" (§1 L12).

In the blueprint `gate` appears once, in §11 L290, as "the field fold and the seam, unchanged", and is never invoked: §3.5 goes margin to verb with nothing between. The transactor's validation list (§3.1 L142) checks constraints, reversibility class, quorum, inverse; it never reads the license fold, the demotion state, the n_eff floor or calibration. So `D_THIN_EVIDENCE`, `D_UNCALIBRATED`, `D_DISPERSED` and the kappa demotion (`osv_dispatch.h` L60-62, L229-231) have no operation. `probe_one` yields one scalar (fusord.cpp L2048-2056, `l[emit_tok] - l[hold_tok]`); the blueprint's verb set is five (§12 L299); the reconciliation §5 L60 named "the seat-to-verb map is a decision", and the blueprint does not make it. §2.3 L109 lists four horizons for the five verbs: `{h_act, h_work, h_hold, h_escalate}`; `fetch` has none, so it is never graded (see F6). The allocate fold (§2.4 L126) runs "per period" over verbs that are already tape entries written per delta; OSV rewrote the want before the row existed. The order verb, allocate, effect is unspecified, and the effector consumes "committed writes" (§1 L35). The falsifier list (§9) carries neither `o_no_silence` nor `o_budget_degrades_to_hold` (`osv_dispatch_test.cpp` L69, L84), which §8 R1 says it carries forward.

**Root cause, which is the category error the brief asked about.** A stored procedure's return is authoritative inside the transaction that invoked it. A judge's output is a margin, a proposal that a deterministic seam disposes into a verb. Calling the judge a stored procedure is precisely the move that lets §3.5 write the verb from the margin. The estate's own definition (`FUSOR_CRYSTALLIZATION_W_WHOLE-READ` §0 L12): "everything that crosses from the mind into the world passes a deterministic seam the mind cannot edit." Law 6, as stated, deletes that seam.

**Verdict.** Dies as a safety design without change. Survives if: the judge writes only a `margin` entry; a named deterministic seam (OSV's `gate` plus the budget pass plus license, demotion, n_eff and quorum) authors every `verb` and `hold` entry `by: seam:<code_hash>` with `judge_ref`; the transactor refuses any verb not authored by the seam; and a falsifier "nothing learned disposes" is added with its planted lie (a verb entry whose author is a judge key).

---

### F3 · A counterparty's free text becomes an irreversible write: no content fence between fact rows and judge context, and F2 plus F4 carry it to the world

**Claim.** §2.3 L110: the template renders "a cell and its neighborhood"; the brainstorm (§2 L46) says the render includes "its recent tape"; §1 L41: "a cell's bytes go from row to template to tokens to logits without leaving the card." §7 makes no mention of injection. §2.3 declares `F_CONTENT` classes, whose discharge "requires words" (`osv_core.cuh` L72).

**Attack.** The runbook's own rule: "Internal lanes first, so there is no injection surface" (§0 L17) and "the day a customer-facing free-text lane opens, the injection surface opens with it" (§7 L130). The blueprint opens it on day one: every `fact` row's `after` (§2.1 L68) is rendered into judge context by a template with no content class, no quarantine, no dual-source rule, no escaping of the serve format's lane prefix. The judge's serve format is lane-prefixed lines, `[lane] text` (fusord.cpp L946-953, `SEED_EXAMPLES`), and the probe is `"\n[SEAT — mandate]\nwatcher:"` (L972-974); a fact row whose text contains a newline and a bracketed lane forges a percept, and the serve pin guarantees those bytes are exactly what the tune was conditioned on. The chain then runs: fact row → template → margin → (no seam, F2) `verb` entry signed by the judge's key → `write(tx)` with `keys[]` (§3.7 L172) → quorum of "judges trained apart" that are in practice lineages, "one base, many lineages" (`PLANET` §2 L44), sharing the base's susceptibility → effector → world. Nothing in §3.6's constraint kinds (check expressions on the row, foreign keys, exposure caps, reversibility, quorum, persistence) is content-aware. The only detection is after the fact, by the stratum or a grade at horizon.

**Goodhart avenues, same section because they share the cause (the judge sees too much and is graded too little).** (a) `fetch` has no horizon (§2.3 L109), so a judge that emits `fetch` on every hard cell is never wrong. (b) Holds on cells with no deadline are `ungradable` and "excluded from license" (§3.9 L184, §6 L227); a judge, or a lineage selected by the grade fold, that holds wherever it will not be graded and acts where the answer is trivial is licensed. (c) The template renders "its recent tape": the judge's own `verb`, `hold` and `grade` entries, so the judge sees which of its decisions were graded and how; the trunk carries "the writ digest, the schema" (§2.5 L134), and the schema now holds the license row with the stratum and canary rates per class (§2.3 L111), so the judge can know which classes are checked. The planet document pinned exactly this: "the machine must not choose how much it is checked" (§3 L54). (d) The escalate horizon is "decision arrival" (reconciliation §3 L42); a judge graded on whether the adjudicator agreed learns the adjudicator, not the outcome. (e) Lineages "graded per class by the grade fold" (§3.5 L158) with selection on the same grades the judge can see: selection for looking good is the default, not a corner case.

**Verdict.** The injection chain is a kill on the current text because F2 and F4 remove the two fences that would have stopped it. Survives with: a content class on every rendered field with the serve-format prefix escaped by construction; a fence that no `F_CONTENT` or counterparty-sourced byte reaches a judge whose verbs can sign an irreversible; grades and license rows never rendered into judge context (the judge reads the world, never its report card); a horizon for `fetch`; and grade entries tagged by source (see F5).

---

### F4 · The signature moved from a human to a quorum of judge keys, silently, against the runbook, the OSV gate, and two tests the blueprint carries forward; and the independence it buys is unfunded

**Claim.** §3.1 L142: "for irreversible, verify the quorum signatures against registered judge keys." §7 L234: "Quorum for irreversibles is N-of-M across judges trained apart, because the independence of their errors is the only accountability a planet without signers has." §8 R5 receipt gate: "one dissenting key and the irreversible write does not exist." §9.8 "The quorum that holds."

**Attack.** On the human planet the runbook says: "The signature stays human, and any plan that says otherwise is a liability plan, not a compression plan" (§7 L130) and keeps "the signature, the people who sign irreversible commitments and carry liability" as one of four seats that stay (§2 Phase 5 L73). OSV's gate returns `V_ESCALATE` on `F_WARRANT` before it reads the margin (`osv_core.cuh` L255: "irreversible: a human signs, always") and dispatch's law: "Warrant commitments escalate always, and hold when the budget is spent. They never act" (`osv_dispatch.h` L35). Two of the fifteen falsifiers §11 L293 carries forward assert it: `o_gate` requires `warr == V_ESCALATE` (`osv_core_test.cpp` L277) and `o_warrant` is titled "irreversible never acts, even under pressure" (`osv_dispatch_test.cpp` L110-133). R1 (§8 L243) carries "the seven core falsifiers", including `o_gate`; R5 (L247) executes irreversibles on judge signatures. The rungs contradict each other.

The blueprint mixes planets without naming one. §3.6 L162 says rules change "by rule entries signed by the operator key, or by the constitution quorum on a planet with no operator"; §7's justification ("a planet without signers") is the no-humans frame; the purpose statement and §5's "large headquarters" are the human frame. The planet document made judge-quorum accountability conditional on "several frontier models from different labs" (§0 L15) and said plainly "a million forks of one model are one opinion" (§2 L42). The blueprint's sizing has one judge's weights on one card (§5 L207); its lineages are "several judges registered against one tape" (§3.5) which the planet document defines as adapters on one base; and the cognitive index is keyed by `judge_pin` (§2.5 L132), so an M-judge quorum needs M read states per warm cell, none of which is in §5. The `judge` registration entry (§2.1 L74: `weight_sha256, serve_pin, template_pins[], classes[], mode, lineage`) has no public-key field, so §3.1's "verify the quorum signatures against registered judge keys" has nothing to verify against. And the judge's signing key lives in the peer process beside the weights whose output it signs (§3.3, §3.5); a signature made by the process that produced the margin attests to the process, not to independent judgment.

**Verdict.** Dies as an accountability story on the human planet; survives on the human planet only if irreversibles route `escalate` to a human signature by the seam regardless of quorum, with judge quorum reserved for the reversible classes the canary licenses. On the planet without humans it survives only with M genuinely separate judges (separate weights, separate processes or cards, separate keys held outside the peer), and §5 re-sized by M.

---

### F5 · Graduation from replay: the canary rung is regressed, the license fold cannot tell borrowed grades from own grades, and the replay budget is off by three orders against the blueprint's own constant

**Claim.** §8 R4 receipt gate: "a replayed year produces a graduation list with n_eff on both sides." §3.8 L180: "years of tape in hours [BUDGET]". §2.4: the license fold reads `grade` entries; §2.1 L72: a `grade` carries `decision_pos, outcome_pos, verb, verdict, horizon_ns` and nothing about how the outcome was obtained.

**Attack.** The reconciliation §6 L70: "In shadow the resident's own act verbs are never executed, so they are graded only where they agree with the person, by borrowing the person's outcome. Where the resident says act and the person held, the resident's act is ungraded, and that disagreement region is exactly the region graduation is about. The honest order has three rungs, not two: shadow with no Effect; canary ... then graduation." Replay is shadow over history. R4's gate licenses on it. The license fold has no way to require canary outcomes because the `grade` entry does not say whether `outcome_pos` is a human's outcome borrowed under agreement, the machine's own outcome under a canary act, or a stratum act on a held cell; the reconciliation's three grade sources are collapsed into one row kind. "Canary rate" appears in the license row (§2.3 L111) and "the canary lottery" in the allocate fold (§2.4 L126), but no fold reads them as a precondition of `calibrated`.

The budget: §5 L209-210 gives 250,000 judgments a day and 7.6 card-hours a day at 110 ms serial. A replayed year at the same constant is 2,788 card-hours, 116 card-days; the runbook estimated "weeks of compute" (§2 Phase 2 L51) for two to three years. "Hours" is wrong by three orders of magnitude serial, and by one to two orders under any batching the blueprint does not specify. The blueprint's §3.8 contradicts its §5.

**Verdict.** Survives with change: tag every `grade` with `source ∈ {borrowed, canary, stratum}` and make the license fold require a canary-source n_eff floor on the act side of every band before `calibrated`; state the replay budget from §5, or specify the batching that changes it.

---

### F6 · The determinism claim is self-contradicting and contradicts the whole-read; and the field fold has no legal clock

**Claim.** §4 L194: "The falsifier is replay: two cold rebuilds from one tape are bit-identical in every fold and every shadow tape." §9.1 the same. §4: "no wall time inside any template or fold." §2.1 L55: `t_wall_ms` is "informational only, never used for ordering or in any template."

**Attack.** §9.6 L259 already concedes the shadow half: a rebuilt index "matches the un-evicted control within the run's noise." A shadow tape's margins are GPU floats; under `kv_unified` with batch composition set by eviction timing, two cold rebuilds do not produce bit-identical logits, a margin near zero flips, a verb flips, and the shadow tapes differ. The whole-read corrected the master's unqualified fold law for exactly this reason: "Every durable *knowledge* structure is a deterministic fold ... and disposition and held relations are not. A resident rebuilt from its tape is the twin" (§3 law 5 L99; §6 L185), and its 09-04 amendment: "replay is a bounded-divergence claim with a measured envelope, never bit-identity" (§3 law 12 L106). Law 1 (L16) restates the uncorrected form, and §4 applies it to shadow tapes.

The clock: the field fold is `slot_of(now_ns, due_ns)` (`osv_core.cuh` L151-156); ripeness is a function of now. §4 forbids wall time in any fold; `due_ns` is the world's wall time (`parse_ts`, `osv_ingest.h` L64-72); `t_mono_ns` (§2.1 L54) is a steady clock, and fusord's own comment says a steady clock does not survive a process: "Real wall time (Unix epoch ms): the only clock that survives a process" (fusord.cpp L167-168). So on replay after any transactor restart the field fold has no `now` comparable to `due_ns`, and the one clock that would work is the one §2.1 forbids from every template. The grade fold ("within the horizon") has the same defect.

**Verdict.** Survives with change: split Law 1 into integer folds (bit-identical, falsifier 1 as written) and judged folds (bounded divergence with a pre-registered envelope); define `now` for every fold as the `t_wall_ms` of the entry at the fold position, read from the tape, which is deterministic on replay because it is data, not a clock; keep wall time out of templates only in the sense that it enters as a rendered field of the entry.

---

### F7 · The [M] citations, checked against their receipts

| blueprint cite | receipt | finding |
|---|---|---|
| §2.5 "fork at 0 MiB, m0-0" | `C:/auricle/src/m0/m0_demo.cpp` L75-76, L96; `C:/IT/OBSERVATIONS.md` L527 | Real, on the 9B hybrid, `n_seq_max = 8`, VRAM delta at fork time with eight recurrent slots pre-reserved. Measures the wrong quantity for sizing (F1). |
| §5 "17,432 B marginal, smoke 1 checkpoints" | convergence §7.3 L178; five checkpoint rows on three tapes | Correct, and already q8_0 (fusord.cpp L1713-1716). The fixed 52,691,760 B the same receipt carries is omitted. |
| §3.5, §5 "probe_one, ~110 to 130 ms per three-seat judgment" | convergence §6.4 L152 "[R every tape]"; §7 table L161-163 | Real: p50 121, p95 135 ms; 106-163 ms. Per **three**-seat boundary, on a 16 GB RTX 4070 Ti SUPER with two tenants (14,085 of 16,376 MiB used, §1 L38), at 434-2,343 live cells, `n_seq_max = 8`. Borrowed to one cell judgment on a 141 GB card with a 7.5 M-cell unified buffer: three regime changes, no measurement. |
| §3.5 "the seam finding of 2026-09-04" | convergence B.7 L455, "[R ... the other session's measurement, quoted for the record]" | An [R] quoted from another session's source comment (fusord.cpp L2038-2044), not an [M] by the citing author. The finding itself is sound. |
| §4 "o_projection_order, the float lie differs by order" | `osv_core_test.cpp` L110-141 | A test source, not a dated receipt of a run; no run log or pass line located (`orgsolver.html` searched). The float-lie clause is informational: the test asserts only `identical` (L140) and prints "no (weak test on this data)" rather than failing (L137-139). |
| §5 "exhaustive embedding scan, 1.67 M vectors in 4.88 ms, CORTEX" | `C:/NEW/BRAIN_RECONTEXT_FUSOR-AT-CENTER_2026-08-21.md` L117 "[M-class]"; CONNECTOME L91 "4.88 ms over 1.67M × 384-d" | Real; at 384 dimensions on the 4070 Ti SUPER. The dimension is dropped; 1024-d is about 2.7× the bytes. Harmless as a bound at 50,000 cells. |
| §7 "the gate exists in fusord" | fusord.cpp L435-449 | Exists. Its list is `ws2_32.dll, winhttp.dll, wininet.dll, urlmon.dll, dnsapi.dll, ggml-rpc.dll`. A TCP or Unix-domain socket on Windows needs `ws2_32.dll`, so §3.3's "no network module loaded except the tape and transactor sockets, enforced by a module gate at boot as in fusord" is impossible as in fusord; and §1 L41 (two processes), §3.7 L166 (in-process C++ for the peer) and §10 Q2 (undecided) contradict each other on whether a socket is even needed. |
| §5 "the estate's 160k-on-16 GB receipt" | BRAIN_RECONTEXT L133 "[OA]"; FORK2_HANDOFF L35 | Operator-attested, for recall, under TurboQuant; margin fidelity under quantized KV is a named open item. Marked [BUDGET] in the blueprint, but called a receipt. |

The blueprint cites neither "13 µs" (the whole-read's hygiene table L243 marks it "UNVERIFIED in code") nor either side of the 63.4 versus 21.1 dispute (L239-240). That is to its credit, and I checked that no other row of the hygiene table (L236-250) is quoted by the blueprint. The one hygiene-table number the blueprint touches, the layer count, is settled above by the GGUF header for the 9B.

**Verdict.** No single row kills; together they show that four of eight [M] tags are borrowed across model, card, grain or author. Survives with the register corrected: [M] for the four that are measurements of the quantity claimed, [R] and [D] for the rest.

---

### F8 · "No application tier" is a relocation, the saga is conflated with an ACID transaction, and falsifier 5 is false for two folds

**Claim.** Brainstorm §4 L76: "it has no application tier at all, because the application was the screen and nothing is looking." Blueprint §0 L12: counterparty messaging "is the effector layer beside it"; §1 L35 "Effector layer: capped writers to the world"; §7 "prints every byte that leaves". §2.2 L103: "the transactor refuses the whole saga if any cell's constraint refuses"; §4 L190: "Sagas across cells are atomic at the transactor: all cells' constraints pass or nothing is appended." §9.5: after a saga and its inverses "every fold equals its state before the saga."

**Attack.** The effector must speak counterparty APIs (carrier, payment gateway, mail) with auth, idempotency keys, retries and rate limits; the runbook's Phase 3 sends "escalations ... to named people with briefs" (§2 L61); OSV's `Brief` struct (`osv_dispatch.h` L78-83) carries `why`, `margin`, `dispersion`; the blueprint's entry kinds (§2.1 L64-76) have no brief and no rendering of one. Humans still read; the screen moved and lost its name. The three-operations document already said where the inverse lives: "a compensating transaction is application-level, the saga pattern" (§2 L24). The blueprint then makes the saga atomic at the transactor, which is a multi-row ACID transaction and needs no inverse; the inverse is needed for the world effect, which is never atomic and is §10 Q3, "who owns it, the store or the effector layer", left open at the load-bearing spot. Falsifier 5 cannot hold as written: after a saga and its inverses the tape has 2N more entries, the `kappa` fold has accrued the act's cost (`osv_dispatch.h` L279-281: `created`, `removed` on `V_ACT`), and the `grades` fold may already hold a verdict on the verb that caused the saga; only the `state` fold can return to its prior value, and only if the inverse is an exact before-image.

**Verdict.** Survives with change: name the effector as the application tier it is, with its own constraint set and its own tape rows (brief rendered, byte sent, acknowledgment received); restrict falsifier 5 to the `state` fold; and answer Q3 before R5, since R5's receipt gate depends on it.

---

### F9 · Per-delta judgment drops the per-period no-silence law and the budget pass; the hot row's margin is undated

**Claim.** §3.5 L158: "Invocation per delta." §2.2 L103: "A cell hibernates when untouched." Law 2: the decision not to act is recorded. §2.2 `Cell`: `margin` (last judged) and `pos_last` (last fact touch); §10 Q9: judgment positions in the cold side record.

**Attack.** OSV's first law of dispatch: "Every open commitment receives exactly one verb, every period. Silence is never the record" (`osv_dispatch.h` L30), tested by `o_no_silence` (`osv_dispatch_test.cpp` L69), which §11 L293 carries forward. Ripeness moves with time, not deltas (`slot_of`); a held cell whose deadline passes with no delta is never re-judged under per-delta invocation and writes no new hold, so the record of "the decision not to act" has a hole exactly where the reconciliation put the most expensive failure: "missed actions, which at the organization radius are breached deadlines" (§3 L40). `tick` entries exist "as in fusord" (§2.1 L76) but nothing says a tick invokes judgment. The budget pass needs the period's whole want set; per-delta verbs land one at a time.

Freshness: the reconciliation's third design decision, "a margin must carry the revision it was judged at or the seam cannot tell a stale margin from a fresh one; `Commitment` has four padding bytes, so a `judged_rev` fits in the cache line" (§5 L60). The blueprint spent the padding and `src_rev` on `pos_last` (the last **fact**, §10 Q9) and moved judgment positions off the hot row. OSV's gate reads `c->margin` from the row (`osv_dispatch.h` L214). The hot row can no longer answer whether its margin predates its last fact without a cold read per cell per period.

**Verdict.** Survives with change: judgment per period over every open cell (per delta in addition, not instead), with a `hold` row per open cell per period as OSV writes it; and a `pos_judged` on the hot row, taking the bytes from `seg` or `seat` (both are `stable_u32` hashes that could live in the cold record) or accepting a second cache line.

---

### F10 · The persistence clause is undefinable as a constraint, "survives every compaction" survives nothing, and the six laws are four

**Claim.** §3.6 L162: "the persistence clause (no write may reduce the store's own capacity to persist without quorum)." Law 2 L17: "survives every compaction." §3.2 L146: "Compaction never rewrites a segment." §0 L14: "Every design decision below descends from one of these."

**Attack.** §3.6's constraint kinds are check expressions on the row, foreign keys, caps, reversibility, quorum. "Reduces the store's own capacity to persist" is a prediction about the world (does this write cancel the energy contract, lower the replication factor, spend the compute the next fold needs), not a property of a row; every append reduces free disk. The planet document derived persistence as a ranking signal ingested "from its own telemetry" (§3 L52) plus a jury gate on writes that irreversibly reduce it (§1 L30), not as a check constraint; a constraint that needs judgment to evaluate puts a model where Law 4 says none runs. As a constraint it is either vacuous or a Law 4 violation. If it means anything checkable it means exposure caps on named resource classes, which §3.6 already lists.

"Survives every compaction": §3.2 has no compaction; a snapshot is a fold checkpoint plus the retained tape "forever". A `state` fold checkpoint contains cell rows and no holds; holds survive only because the tape is never deleted, which Law 1 already says. Falsifier 3's lie, "a snapshot that keeps only state changes", is exactly what a `state` checkpoint is, and it violates nothing. The falsifier cannot fail unless someone deletes the tape.

Independence: Law 5 ("everything on the GPU is re-derivable from the tape") is a corollary of Law 1 ("every durable structure is a deterministic fold over it"); Law 2's second clause is a corollary of Law 1's "nothing is ever edited" and is vacuous on this design; Law 6 is the category error of F2. Four laws stand independently: the tape and its folds, one writer, the writ as constraint, the hold as a row. "Every design decision descends from one of these" is decorative; the decisions that matter (per-delta invocation, judge-signed verbs, judge-quorum irreversibles, one sequence per cell) descend from none of them.

**Verdict.** Survives with change: restate the persistence clause as caps on named resource classes plus a jury gate on a declared list of irreversible resource writes; drop "compaction" or define one; print the four laws and two corollaries.

---

### Smaller defects, unranked (each real, none a kill on its own)

- **Durability is not what fusord does.** `Tape::put` ends with `std::fflush(f); // durable as it goes` (fusord.cpp L906); fflush is a user-space flush, not `FlushFileBuffers`/fsync. §4 L192 says durable when fsynced; §8 R0 reuses `Tape`. Ten thousand fsyncs a second on one core with no group commit (§3.1 [BUDGET]) is not plausible on Windows without one.
- **The chain construction is not fusord's.** fusord hashes the literal prefix bytes it writes and appends `prev`,`h` outside them (L902-909); `verify_chain.py` recomputes over the literal row. §2.1 L62 hashes `canonical(body_and_header)`. Reconcilable only if the writer always writes canonical bytes and hashes what it wrote; say so. §2.1 L49 "field names follow fusord.cpp's tape" is false: fusord rows carry `k, ms, i, mind, m`; the blueprint's carry `pos, t_mono_ns, cell, cls, by, body`; only `k, prev, h` overlap, so "its tools read TAPESTRY tapes unchanged" is limited to the chain verifier.
- **The `judge` registration has no key** (§2.1 L74) though §3.1 verifies signatures against "registered judge keys" and §7 says keys are "bound at registration".
- **`ungradable` is unreachable as specified.** Grades are emitted "at outcome arrival and at horizon expiry" (§2.4 L123); a hold with no deadline has no finite horizon, so the verdict "no deadline and no outcome" is never emitted unless emitted at decision time, which §2.4 does not say.
- **The stratum rate's upward monotone is missing.** The reconciliation §4 L50: "Fold may raise it on evidence ... and may never lower it." §3.9 says only "never touches the stratum rate downward."
- **The two pins are stamped but not asserted as a pair.** §2.1 L59 stamps both; the reconciliation §7 L78 requires "the pair was minted together by the tune", and the org-radius triple includes `SchemaMap::pin`; the `verb` entry carries neither the map pin nor a minted-together assertion.
- **`hold` carries `seam_probes`** (§2.1 L69), a fusord count of re-probes during a seat's speech; it has no meaning for a cell judgment unless the seam of F2 exists and re-probes.
- **The subscribe transport.** §3.7 L166 says fusord's tailer "works unchanged"; `LaneTail` tails a text spool file with `t_mono_ns\tlane\tgrain\ttext` frames (fusord.cpp L568-593), not a socket and not JSON. Someone must write that spool from the tape; unspecified.
- **Invalidation storms are unpriced.** §2.5 L132 invalidates on any touch to "any cell in its rendered neighborhood"; with pressure in the render, one fact invalidates every neighbor in its lattice cell; at 250,000 facts a day the warm set churns faster than it is judged. §5 assumes a warm set that stays warm.
- **`t_mono_ns` across transactor restarts** (§2.1 L54) is not monotone across processes; the same fusord comment (L167) applies. Positions are the only safe ordering; the entry should say so and drop the ordering role from `t_mono_ns`.

---

## 3 · The three claims that survive every attack

**S1 · The tape as the only truth, with integer folds verified cold against incremental.** Law 1 restricted to integer folds, §2.4's `verify_fold` (L128), and falsifiers 1, 2 and 9. This is the part that is already code and already receipted: BLAKE2b matches `hashlib` on six vectors including a split update (convergence §2 L46, B.1); 193 chained rows across four tapes with zero breaks (B.3); torn-row head recovery is implemented at `Tape::open` (fusord.cpp L863-897); fixed-point projection is order-independent by construction (`osv_core.cuh` L124-126, `o_projection_order`). Every attack I mounted on determinism (F6) lands on the judged folds and the shadow tape, not on this. The 64-bit `pos_last` fix (§2.2 L82) closes a real truncation (`osv_ingest.h` L284, L337) and belongs here too.

**S2 · The monotone license property with per-verb horizons and both-sides n_eff.** §2.4 L124 and §3.9 L184: narrows on evidence, widens only within a ratified rule, never lowers the stratum rate, reads at the longest finite horizon, requires n_eff on both sides, excludes no-deadline holds from licensing and grades them by the stratum; falsifier 10 with its planted lie "pooled n_eff across verbs". This is a faithful carry of reconciliation §2-§4, and it survives F3 and F5 because those attack its inputs (which grades, from which source, on which verbs), not the property. Give `fetch` a horizon and tag grades by source and the property is exactly the pawl the corpus asked for.

**S3 · The hold as a row and the refusal that never silences.** §2.1's `hold` (margin, reason, judge) and `refuse` (attempted, reason) entries; falsifiers 3 and 4. The whole-read names this as the one genuinely unclaimed object in the estate: "the record of holds and killed continuations with margins on an owned, hash-chained tape" (§7.8 L218). It is trivially implementable, it is what fusord already writes (hold rows on the wire, `unsaid`, `e_suppressed`), and nothing in my attack set touches the object itself; F9 attacks when it is written, not what it is.

---

## 4 · The one change

Put the seam back as a named, deterministic, code-hashed component and make it the only author of verbs. The judge writes a `margin` entry and nothing else. The seam runs per period over every open cell (and per delta in addition), reads the last margin with its `pos_judged`, the pressure, the flags, the license row, the demotion state, the n_eff floor and the calibration band, applies the budget pass, and writes every `verb` and `hold` entry `by: seam:<code_hash>` with `judge_ref`; irreversibles route to a human signature or, on the planet without one, to M separate judges whose keys live outside the peer; the transactor refuses any `verb` not authored by the seam, and a new falsifier, "nothing learned disposes", plants a judge-signed verb and expects `refuse`. This one change closes F2 outright, removes the injection-to-irreversible chain of F3 (an injected margin becomes at most an escalate), restores the runbook's human signature in F4, restores the no-silence law and the budget pass in F9, makes `seam_probes` meaningful, and reconciles Law 6 with Law 4 by making the model's output a proposal again. F1 is not fixed by design but by arithmetic: any re-sizing that admits the recurrent state (about 2,200 warm cells on this card, or an attention-only judge with its constants re-measured) survives; the seam is the change that no arithmetic can substitute for, and it is the one the reconciliation already wrote down and the blueprint dropped.

---

## 5 · Sentences unfalsifiable as written

Each is quoted from the blueprint with its location and the reason it cannot be failed as it stands.

1. §0 L10: "It holds every open obligation whole and already read ... refuses forbidden writes by construction ... and can always be rebuilt from its own tape." "Whole", "already read" and "by construction" name no test; "always" admits none.
2. §0 L14: "Every design decision below descends from one of these." No decision is traced; the ones in F2, F4 and F9 descend from none.
3. Law 2 L17: "survives every compaction." No compaction exists in §3.2.
4. Law 3 L18: "Two hands never sell one seat." A slogan; the property is linearizability per cell, which §4 states falsifiably.
5. Law 5 L20: "Losing it costs time, never truth." Tautological once Law 1 defines truth as the tape.
6. §2.1 L78: "Two implementations that disagree on canonical bytes will disagree on `h`, which is the point: the chain is the conformance test." Tautology.
7. §2.2 L103: "no two entries for one cell are ever concurrent." On a single append-only log, concurrent entries cannot exist by definition.
8. §2.2 L103: "A cell hibernates when untouched." No duration.
9. §2.3 L110: "how a cell and its neighborhood render." "Neighborhood" is undefined, so §2.5's invalidation rule ("any cell in its rendered neighborhood is touched") has no test.
10. §2.4 L124: "calibrated per class and band." "Calibrated" is never defined; in shipped code `calibrated_of` is consumed and never computed (the Eight §3 row 7).
11. §2.5 L132: "cold cells lose their pages first." "Cold" is defined by the ranking that evicts.
12. §3.1 L142: "two orders above any organization's event rate." "Any organization."
13. §3.2 L146: "the full tape is retained in object storage forever."
14. §3.6 L162: "no write may reduce the store's own capacity to persist without quorum." Undefinable as a check (F10).
15. §3.8 L180: "with instrument-reflexivity zero because the humans were not watched when it was written." No instrument for reflexivity is named; the past cannot be re-run with one.
16. §3.9 L184: "narrows on any evidence." "Evidence" is not a named statistic with a threshold.
17. §4 L190: "Reads at a position are repeatable forever."
18. §4 L192: "The peer is not durable and does not need to be." The second clause has no metric.
19. §6 L221: "no truth lost." Tautological given Law 1.
20. §7 L234: "No allow verb anywhere in the store." A naming claim: the quorum token authorizes an irreversible write and is not called "allow".
21. §7 L234: "the independence of their errors is the only accountability a planet without signers has." "The only" admits no test.
22. §8 R3 L245: "the fork that costs nothing." No unit, no threshold; on the hybrid it costs 52.7 MB and the sentence still passes if "cost" means VRAM delta at fork time.
23. §9.6 L259: "within the run's noise." Noise is not pre-registered.
24. §9.7 L260: "consume pages only as they diverge." The unit "pages" excludes the recurrent state by construction, so the falsifier passes while the memory is spent.
25. §12 L299: "Tape: the append-only chained log, the only truth." Definition presented as claim.
26. L302: "The tape, as always, is the proof." Slogan.

---

## 6 · Open questions to add to §10

11. Which planet is this blueprint for, stated once: is the signature on an irreversible a human key or a judge quorum, and does the answer live in the license row per class?
12. Where does the seam run, what is its code hash, and who is the `by` of a `verb` entry? Is the transactor allowed to accept a verb authored by a judge key at all?
13. What is the seat-to-verb map from one scalar margin to five verbs, and is v11 (three chat-watcher seats, mandates verbatim under pin `0xe7ffa5704ba31076`) the judge, or is the judge a new tune with a new pin? If the latter, which [M] constants survive?
14. What is the per-sequence recurrent state on the chosen judge in bytes, what is `LLAMA_MAX_SEQ` in the tree the peer links against, and is the cognitive index a live sequence, a stored recurrent state plus attention pages, or a root cache with suffix re-decode?
15. Unified or per-stream KV for the warm set, per `llama.h` L382-384's own guidance, and the probe cost measured at the warm set's cell count rather than at 2,343 cells.
16. What is `now` for the field fold and the grade fold on replay, given that `due_ns` is world wall time and `t_mono_ns` does not survive a restart?
17. Does the judge see its own grades, the license rows, or the stratum and canary rates in any rendered context, and what is the falsifier that proves it does not?
18. What is the content fence between a counterparty-sourced field and a judge whose margin can reach an irreversible, and how is the serve format's lane prefix escaped by construction?
19. What horizon grades `fetch`, and how is a `grade` tagged by source (borrowed under agreement, canary, stratum) so the license fold can require own outcomes?
20. What is the pre-registered noise envelope for falsifier 6, and is the shadow-tape replay claim bit-identity or bounded divergence?
21. Which entry kind carries the brief, who renders it, and where does the effector's acknowledgment land on the tape?
22. Where do judge keys live, if not in the peer process beside the weights, and what does the `judge` entry carry so that §3.1 has a key to verify against?
23. What is the ordering of `verb`, allocate and effect within a period, and can an effect consume a `verb` entry that allocate has not yet disposed?
24. fsync policy and group commit for the transactor, and whether `Tape::put`'s fflush is replaced.
25. How many fact rows a day invalidate how many warm cells under the neighborhood rule, measured on the Olist wire before §5 is believed.

---

## Appendix A · Internal contradictions, indexed

| a | b | note |
|---|---|---|
| §1 L41 two processes in v0 | §3.7 L166 in-process C++ for the peer; §10 Q2 undecided | three answers to one question |
| §3.3 L150 sockets to tape and transactor, gate "as in fusord" | fusord.cpp L435 `FORBIDDEN_MODULES` includes `ws2_32.dll` | a socket needs the forbidden module |
| §4 L194 shadow tapes bit-identical | §9.6 L259 "within the run's noise" | one of them is the law |
| §4 L194 no wall time in any fold | field fold needs `now` against `due_ns` (world wall time) | the fold has no legal clock |
| §2.3 L109 four horizons | §12 L299 five verbs | `fetch` ungraded |
| §3.8 L180 years in hours | §5 L209-210: 7.6 card-hours per day | 116 card-days per year serial |
| §5 L206 per-cell cost 8.5 MB (suffix) | §5 L204's own receipt carries 52.7 MB fixed per sequence | F1 |
| §8 R1 carries `o_gate`, `o_warrant` (warrant never acts) | §8 R5 executes irreversibles on judge quorum | rungs contradict |
| §8 R1 carries `o_no_silence` | §3.5 invocation per delta | law dropped |
| §2.2 L82 `pos_last` takes `src_rev` and the padding | §10 Q9 judgment positions off the hot row | freshness lost |
| §3.6 L162 rules signed by the operator key | §7 L234 "a planet without signers" | planets mixed |
| Law 4 L19 "where no model runs" | Law 6 L21 the judge runs in the store; §10 Q2 one process possible | the fence's location is open |
| §2.1 L49 field names follow fusord | §2.1 table | only `k, prev, h` overlap |

## Appendix B · Regressions against the documents the blueprint claims descent from

| property | source | status in the blueprint |
|---|---|---|
| Judge proposes, seam disposes | Eight §1; reconciliation §1; whole-read law 7 | **regressed** (F2) |
| Signature stays human on the human planet; warrant never acts | runbook §7, Phase 5; `gate`; `o_gate`, `o_warrant` | **regressed** (F4) |
| Canary before graduation; shadow grades cannot license the disagreement region | reconciliation §6; planet §7 | **regressed** (F5) |
| Freshness: a margin carries the revision it was judged at | reconciliation §5 | **regressed** (F9) |
| One verb per open commitment per period; silence never the record | dispatch L30; `o_no_silence` | **regressed** (F9) |
| Fold law qualified: knowledge re-folds, judged state is bounded divergence | whole-read §3 law 5, law 12 | **regressed** (F6) |
| Injection surface opens only with the free-text lane, later | runbook §0, §7 | **regressed** (F3) |
| The machine must not choose how much it is checked; rates unreadable by the fold | planet §3 | **partially regressed** (F3c: rates may render into the trunk) |
| Monotone license: narrow on evidence, widen within a ratified rule | reconciliation §2 | carried |
| Per-verb horizons, both sides above the floor, read at the longest finite horizon | reconciliation §3 | carried (minus `fetch`) |
| Stratum rate a ratified pinned parameter, never lowered by the fold | reconciliation §4 | carried; upward monotone not stated |
| Two pins on every verb | reconciliation §7 | carried; minted-together not asserted; map pin absent |
| No-deadline holds excluded from license, graded by the stratum | reconciliation §3 | carried |
| The valve: one writer, inverse recorded, refusals by construction | substrate §0; three ops §2 | carried |
| DDL as writ | three ops §2 | carried (`rule` entries with quorum) |
| 32-bit `src_rev` truncation | three ops §2; reconciliation §5 | fixed |
| Scaffolding computable; transforms decay into code | substrate §1, §5 | absent (no compile rung, no retained-path stratum) |
| Rollback to a prior fold by a trigger the current judge cannot veto | planet §0, §4, §6 | absent |
| The constitution's change gate: jury, longest horizon, rollback test | planet §4 | absent; `rule` needs only the operator key |
| Uniform component and null stratum in the ranking, pinned | planet §3; runbook §5 | absent from the allocate fold |

---

*Written 2026-09-08 by the red-team subagent at the parent session's request. Arithmetic: `C:/TAPESTRY/qc/scratch/red/gguf_keys_and_sizing.py`. Nothing in this file is a new measurement except the GGUF header read and the checkpoint-row fit, both reproducible by that script. The tape, as always, is the proof.*
