# QC-4 · JUDGMENT SEMANTICS, GRADES, THE LICENSE, QUORUM, THE WRIT, SECURITY
### Adversarial review of `TAPESTRY_ARCHITECTURE_BLUEPRINT_v0.1_2026-09-08_FABLE5-1.md`, §2.3 · §2.4 · §3.5 · §3.6 · §3.9 · §7 · falsifiers 8 and 10 · open question 7

**2026-09-08 · Claude Opus 5 · reviewer 4 of 7.** Evidence built and run: `osv_dispatch_test.cpp` compiles clean under `g++ 13.3.0 -O2 -std=c++17` (WSL Ubuntu-24.04) and passes 8/8; a purpose-written probe (`C:/TAPESTRY/qc/scratch/license/qc4_probe.cpp`) exercises six claims against the shipped code. Every finding marked CONFIRMED was executed or read directly out of a cited line. Line citations: `BP:n` = blueprint line n; `dispatch:n` = `osv_dispatch.h`; `core:n` = `osv_core.cuh`; `ingest:n` = `osv_ingest.h`; `test:n` = `osv_dispatch_test.cpp`.

---

## 1 · Verdict in three sentences

The judgment half of this blueprint does not yet have a definition of being wrong: the grade fold is not a function of the tape, one verb in five has no horizon at all, `escalate` cannot be graded from the rows the design specifies, and "reversed" — the entire meaning of a bad `act` — was dropped from the `fact` body that the shipped code already carried. The license and the quorum are the two load-bearing safety objects and both are currently ornamental: the monotone property is asserted of a fold that structurally cannot enforce it, the kappa meter as shipped is arithmetically incapable of demoting a class that acts, and the jury cannot be convened at all because §7's own module gate forbids the peer the network that off-card judges require while §5 budgets the card for exactly one 7 GB judge. Nothing in the slice is unsalvageable, but §2.4, §3.6, §3.9 and §7 as written would ship a store that licenses autonomy on act-side evidence it collected itself, signs irreversible writes with a quorum of clones, and reports all of it as calibrated.

---

## 2 · Findings, most severe first

---

### F1 · The grade fold is not a deterministic function of the tape, and `grade` is simultaneously a fold output and a tape kind
**§2.4, §3.9, §4, falsifier 1** · **BLOCKER** · **CONFIRMED (text)**

**Claim.** BP:117 "A fold is a registered, deterministic reducer over a tape span." BP:123 the grade fold is "maintained … at outcome arrival **and at horizon expiry**." BP:128 "cold recompute from genesis must equal the incremental result bit for bit." BP:194 "no wall time inside any template or fold … two cold rebuilds from one tape are bit-identical."

**Defect, part one.** Horizon expiry is not an event on the tape. An outcome arriving is an entry; a horizon lapsing is the absence of an entry plus the passage of time. A fold over a tape prefix cannot observe it. Two cold rebuilds run a week apart therefore produce different verdicts for the same decisions — `ungraded_yet` in one, `wrong` in the other — and falsifier 1 fails on the grade fold by construction, not by a planted lie. The blueprint lists a `tick` kind (BP:76) and never wires it to the grade fold.

**Defect, part two.** `grade` is a tape kind with a body (BP:72: `decision_pos, outcome_pos, verb, verdict, horizon_ns`) *and* the output of the grades fold (BP:123: "one `grade` per decision"). These are mutually exclusive under laws 1 and 3. If grades are entries, something writes them, and that writer is a decider, not a fold — grades then become an opinion on the tape rather than a derivation from it, and the license fold reading "`grade` entries" (BP:124) is reading a writer's claim. If grades are fold output, the `grade` tape kind is dead and the license fold's stated input is wrong.

**Defect, part three.** The clock the horizons are measured in does not exist across a restart. `t_mono_ns` is "monotonic stamp from the transactor's clock" (BP:54); `t_wall_ms` is "informational only, never used for ordering" (BP:56). A monotonic clock has an arbitrary epoch and resets on reboot, and §6 (BP:222) prescribes exactly that: "v0: restart." Meanwhile deadlines — the thing an `h_hold` horizon must be compared against — arrive from the world as wall-clock strings (`ingest:64` `parse_ts`, `ingest:278` `c.due_ns = parse_ts(*v)`). The hold horizon therefore requires comparing a wall-clock deadline to a monotonic stamp. They are not comparable, and `osv_core.cuh:36` forbids the fix ("nothing measures staleness in wall time").

**Fix.** (a) Materialise time on the tape: the transactor emits a `tick` at a ratified cadence, and horizon expiry is defined as "the first `tick` whose stamp exceeds `decision.stamp + horizon_ns`" — a tape event, so the fold stays pure. (b) Grades are fold output only; delete the `grade` tape kind, or rename the kind `outcome_link` and make it an *input* the world/effector writes. (c) Promote `t_wall_ms` to a durable, transactor-enforced-monotone `t_stamp_ms` (`max(prev, now)`), used for horizons and deadlines only, never for ordering. Add falsifier 11: *the grade that does not move* — rebuild the grade fold twice at two different real times from one tape and require bit-identity.

---

### F2 · The writer supplies the reversibility class, and `write(tx)` is a documented bypass of the `o_warrant` safety property
**§3.6, §3.7, §3.1** · **BLOCKER** · **CONFIRMED (code + text)**

**Claim.** BP:172 `write(tx)` request is `facts[] each with inverse, reversibility, judgment_ref, keys[]`. BP:142 the transactor will "check the reversibility class and, for irreversible, verify the quorum signatures."

**Defect.** The class arrives inside the write. Check it against what? A judge that wants to bypass the quorum declares `reversibility: reversible` and no quorum is demanded. This is a straight regression from the shipped code, which already got it right and says so in a comment: irreversibility is `F_WARRANT`, and `ClassMap::flags` is `// F_CONTENT, F_EXOGENOUS, F_WARRANT — declared, not inferred` (`ingest:144`), re-derived from the pinned map on every touch — `ingest:283`: `c.flags = (uint8_t)((c.flags & (F_SHADOW | F_EXPLORE | F_DIRTY)) | cm.flags | F_PINNED);` under the comment `// Declared flags are the map's, never inferred` (`ingest:282`). The map is pinned (`ingest:165` `SchemaMap::pin`) and the blueprint extends that pin (BP:113). Every ingredient of the correct answer is already in the estate and the blueprint threw it away.

**Second defect, worse.** There are now two paths to an irreversible act, and they disagree. `gate()` returns before it reads the margin: `core:255` `if (c.flags & F_WARRANT) return V_ESCALATE;`. The falsifier `o_warrant` (`test:110-129`) asserts `warrant_acted == 0` and passes — measured this session: *"200 irreversible commitments: 0 acted, 195 held for a human that was not free."* But `write(tx)` with `keys[]` accepts an irreversible write that the gate never emitted and never could emit. The blueprint's client API is a legal channel around the one safety property the shipped code proves.

**Fix.** Reversibility is **derived, never declared**. Source of truth: the class map's `reversibility` field for the `(class, table, op)` triple, covered by `SchemaMap::pin`. The write's `reversibility` field becomes a *redundant assertion*; if it disagrees with the derived value the transactor emits `refuse(reversibility_mismatch)` **and** a `warn`, because a mismatch is the signature of an attempted quorum bypass and must be visible to the grade fold as an attribute of the proposing judge. Additionally: the binary class is insufficient. Open question 3 (BP:271) already names the take-back window, which makes reversibility *time-dependent* — the same write is `reversible_within_window` at pos P and `irreversible` at P+window. Make the class a triple `{reversible_by_inverse, reversible_within(window_ns), irreversible}`, and make the quorum requirement a function of the class **and** the horizon.

---

### F3 · The kappa meter cannot demote a class that acts; demotion is absorbing, unattributed, and steals adjudication from every other class
**§2.4 (kappa fold), §11** · **BLOCKER** · **CONFIRMED (executed)**

**Claim.** BP:125 the kappa fold outputs "created and removed per class, forecast flag until outcomes arrive." BP:292 imports `ClassKappa` and `Dispatch::run` verbatim as "the allocate and kappa folds." The task asks whether the blueprint's kappa fold closes the loop the shipped code leaves open. **It does not close it, and the loop it fails to close is pointed at the wrong verb.**

**Evidence (PROBE 2, executed).**
```
BEFORE demote  cls3: acted=214 worked=0 esc=0 created=0.0 removed=1926.0 kappa=0.000 -> ok
AFTER  p1      cls3: acted=0 worked=0 esc=214 created=2568.0 removed=0.0 kappa=1e+09 -> DEMOTE
AFTER  p2/p3   cls3: (identical)                                          kappa=1e+09 -> DEMOTE
STARVED        cls3: acted=0 esc=0 held=214 created=0.0 removed=0.0 kappa=0    -> ok
```

Four separate defects fall out of those four lines.

1. **Kappa can never demote an acting class.** `VerbCost::act = 0.0f` (`dispatch:124`), so pass 4 does `K.created += cost.act; K.removed += cost.human_baseline;` (`dispatch:280`) — created stays 0 while removed climbs by 9.0 per act. `kappa() = created/removed` (`dispatch:150`) is therefore **0.000 forever** for a class that acts, and `cools_the_plant()` is `kappa() >= 1.0` (`dispatch:151`), so it is false forever. The irony limit fires only on classes that *escalate* — i.e. classes already under human supervision. The meter is aimed at the one population it cannot help.
2. **Demotion is absorbing by arithmetic.** A demoted class emits only `escalate` and `hold` (`dispatch:231`), and `escalate` adds to `created` and explicitly nothing to `removed` (`dispatch:287`: `// removes nothing: a human still did it`). So `removed` is pinned at 0 and `kappa()` returns the 1e9 sentinel forever. There is no arithmetic path back. Nothing in the shipped code ever writes `Dispatch::demoted` — it is a bare `std::map<uint32_t,bool>` (`dispatch:178`) set by hand in the test at `test:193` (`D.demoted[3] = true;`), and the "cold/hot" kappa comparison at `test:203-204` is two literals, not a measurement.
3. **The meter reads `ok` for a starved demoted class.** With zero human minutes the demoted class's escalations all become budget holds, `created` returns to 0, `kappa()` returns 0.0 and `report_dispatch` prints `ok` (`dispatch:319-322`). The class is at its most dangerous — irreversible-adjacent work backing up with no human — and the meter says it is fine.
4. **The reason is erased.** `dispatch:231` sets `why = D_OK` when demotion rewrites ACT/WORK to ESCALATE. PROBE 3, executed: *class 3 (demoted) escalations with reason==ok: 214; other classes' escalations with reason==ok: 0.* The `VerbRow` carries no marker of demotion, so a grade fold reading the tape cannot separate "the judge wanted a human" from "kappa took autonomy away." Since the blueprint's `verb` body (BP:70) is `verb, margin, reason, judge, refs[]`, this erasure carries straight onto the tape.

**Fifth defect, cross-class (PROBE 4, executed).** Demoting one class under a finite budget starves *other* classes' irreversibles:
```
no demotion : 50 briefs funded | cls3 got  6 | budget-starved  60 | irreversibles starved 60
cls3 demoted: 50 briefs funded | cls3 got 33 | budget-starved 252 | irreversibles starved 91
delta in irreversibles pushed into a budget hold by ONE class's demotion: +31
```
Demotion converts 214 free acts into 214 competitors for the same 50 briefs. Pass 2 ranks by `look_value` (`dispatch:183-186`), so the demoted class's newly-escalating volume outbids other classes' irreversible commitments and pushes 31 more of them into `D_BUDGET` holds. The safety property still holds locally (nothing acted) but the *system* got less safe: 31 more irreversible obligations sitting unread. The blueprint imports pass 2 as the `allocate` fold (BP:126, BP:292) and inherits this whole coupling silently.

**Fix.** (a) Set `cost.act` from the wire per class — the supervision an unattended act actually creates (the spot-check rate, the exception handling, the reversal cleanup) is never zero, and a zero there is the reason the meter is blind. (b) `removed` must be credited **only when an outcome confirms the commitment did not come back** — the code's own honest note (`dispatch:136-139`) says exactly this and then credits it at dispatch time anyway; the blueprint's "forecast flag" (BP:125) carries the flag forward without fixing the accounting. Split into `removed_forecast` and `removed_confirmed`, and demote on `removed_confirmed` only. (c) Demotion must **not** be absorbing: it must be a fold over a trailing window of *confirmed* outcomes with a ratified re-licensing criterion, so a class can recover; an absorbing demotion is a one-way ratchet that ends with everything escalating and the whole design converging on the human queue it was built to delete. (d) The demoted rewrite must carry a new `DReason` — `D_DEMOTED` — and the blueprint's `verb` body must carry the *counterfactual* verb the gate produced before the rewrite. (e) Demoted-class escalations must draw from a **separate, capped** budget pool so demotion cannot cannibalise adjudication for irreversibles in other classes.

---

### F4 · The jury cannot be convened, and "trained apart" is a self-declared string that the design's own lineage mechanism falsifies
**§3.6, §7, §5, falsifier 8, open question 7** · **BLOCKER** · **CONFIRMED (text + arithmetic)**

**Claim.** BP:234 "Quorum for irreversibles is N-of-M across judges trained apart, because the independence of their errors is the only accountability a planet without signers has." The imported no-humans model specifies the jury as "several frontier models from different labs, run on the same record."

**Defect A — the module gate forbids the network the jury needs.** BP:150 and BP:234: the peer "has no network module loaded except the tape and transactor sockets," cited **[M]** as a real gate in fusord. Off-card frontier judges are reachable only over a network. The transactor is "small and boring" (brainstorm §2) with no model runtime. **No component in this architecture can obtain a vote from an external model.** §7's proudest control and §7's central mechanism are the same paragraph and they cancel.

**Defect B — the card is budgeted for one judge.** BP:207: "warm cells per 141 GB card, after **7 GB weights** and the root." Singular. M frontier models from different labs at frontier scale do not co-reside there; M 7-GB models are not frontier. The sizing table (§5) and the quorum (§7) describe different machines and the blueprint never reconciles them.

**Defect C — independence is asserted, never measured.** The `judge` registration body (BP:74) is `weight_sha256, serve_pin, template_pins[], classes[], mode, lineage`. `lineage` is a free string. Nothing on the tape measures error correlation. Worse, the design's own lineage mechanism *manufactures* correlated judges: the imported no-humans model says "Adapters make this cheap: one base, many lineages." M LoRA lineages off one base have M distinct `weight_sha256` values and share every training error they inherited. The quorum counts distinct hashes and calls that independence.

**Defect D — falsifier 8 does not test what it is named for.** BP:261: "The quorum that holds. An irreversible write with one dissenting key is refused. Lie: a quorum of one." That falsifier passes with M byte-different clones of one model. It tests N-of-M arithmetic; it tests nothing about decorrelation, which BP:234 names as the *entire* justification for the mechanism.

**Defect E — "one dissenting key" is not the rule §7 states, and a dissent has no row.** N-of-M plus one dissent *satisfies* N-of-M. Falsifier 8 demands refusal, so the actual rule is "N approvals **and** zero dissents" — unanimity among voters — which is not what BP:234 says. And a dissent must be observable: there is no `dissent` kind, and `jury` mode "signs only" (BP:158), so a judge that votes no produces *nothing*, indistinguishable from a judge that was offline. Law 2 says a hold is a row; a jury's no is a hold and it is not a row.

**Defect F — all M jurors read one rendering.** Even granting decorrelated weights: the M judges run in one peer (BP:150 "One process per card"), against one cognitive index, one template pin, one shared root trunk (BP:134). Training independence gives nothing against an input-side attack (see F10); a poisoned rendering hits all M identically. The axiom the design needs is *joint* independence of weights **and** evidence, and it has neither stated.

**Fix.** (a) Name the process that holds jury egress and give it its own gate, allowlist, and audit lane — the peer keeps its gate; the jury does not run in the peer. (b) Make independence a **measured, folded, pinned** quantity: every registered judge replays a common held-out tape span in shadow; the fold computes pairwise disagreement rate per class; a quorum is valid only if its M signers' maximum pairwise agreement is **below a ratified ceiling**, and the ceiling is a constitution parameter. (c) `lineage` becomes `base_weight_sha256` plus `adapter_sha256`, and two judges sharing a `base_weight_sha256` **count as one vote**. (d) Add a `dissent` kind and require the quorum rule to be `N approvals ∧ 0 dissents ∧ M − N − dissents ≤ ratified_abstain_ceiling`, so an offline juror is distinguishable from a silent one. (e) Rewrite falsifier 8 into two: *8a — the quorum that holds*: one dissent refuses. *8b — the quorum that is not one model*: M registrations sharing a base hash are refused as a quorum, and M measured-correlated judges are refused as a quorum.

---

### F5 · A quorum signature binds nothing the blueprint specifies, so every valid signature is replayable
**§3.6, §3.7, §3.1** · **BLOCKER** · **CONFIRMED (omission) / PLAUSIBLE (attacks)**

**Claim.** BP:172 `write(tx)` carries `keys[]`. BP:142 "verify the quorum signatures against registered judge keys." The blueprint never states what bytes are signed.

**Defect.** If the signature covers only the write body (`table, key, op, before, after, inverse, source_pos`, BP:68), then it is a bearer token for a *state transition shape*, not for an *event*. Five concrete replays follow, none blocked by anything written:

1. **Position replay.** The same approved irreversible write is resubmitted after the cell has returned to the same `before` state. `before/after` still match; the signature still verifies; the irreversible act happens twice.
2. **Judgment substitution.** `judgment_ref` is a *sibling field* of `keys[]` in the request (BP:172). If it is outside the signed bytes, a quorum obtained for judgment A attaches to judgment B, and law 6's "every verb stamped with both pins" becomes decorative — the stamp and the authorisation are no longer bound.
3. **Saga re-partition.** A saga is `facts[]` (BP:172, BP:103). Per-fact signatures let an attacker re-cut a signed 9-fact saga into a 3-fact saga plus 6 unsigned facts, or graft signed facts onto a different saga. The atomicity guarantee (BP:190) is about constraints, not about the signed set.
4. **Reversibility upgrade.** If the reversibility class is outside the signed bytes, a quorum collected for a reversible write is attachable to an irreversible one. This composes with F2 into a complete bypass.
5. **Shadow-to-primary harvest.** `jury` mode "signs only" (BP:158) and replay runs judges in shadow over history (BP:180). A jury signature produced during a replay of a historical span is, byte-for-byte, a signature over a write body. Nothing in the blueprint distinguishes it from a live vote. Years of shadow replay (BP:180, "years of tape in hours") is a signature-harvesting oracle.

**Also.** `pos_last` is the natural compare-and-swap token, and open question 9 (BP:277) proposes the wrong value for it: *"proposal: last `fact`, with judgment positions in the cold side record."* For a quorum binding you need **last entry of any kind** — otherwise a `hold` or a competing `verb` landing between the vote and the write leaves `pos_last` unchanged and the stale signature still verifies.

**Fix — a signature must bind, as one canonical preimage:**
`domain_tag ‖ tape_genesis_h ‖ mode ‖ saga_digest ‖ n_facts ‖ ∀cell(cell_id ‖ pos_last_any_kind) ‖ reversibility_class ‖ judgment_ref ‖ signer(weight_sha256 ‖ serve_pin ‖ key_id) ‖ template_pin ‖ schema_pin ‖ not_valid_after_pos`.
`domain_tag` and `tape_genesis_h` kill cross-tape and cross-purpose replay; `mode` kills the shadow harvest; `pos_last` per cell makes the signature a compare-and-swap; `not_valid_after_pos` bounds the window in the deposit clock rather than in wall time. The transactor refuses on any mismatch with a typed reason, and the refusal is an entry (falsifier 4).

---

### F6 · Registering a judge requires no key, so the jury is sybil-able from inside
**§2.1, §3.5, §7** · **BLOCKER** · **CONFIRMED (text)**

**Claim.** BP:73 the `rule` kind is annotated "**needs keys**." BP:74 the `judge` kind — `weight_sha256, serve_pin, template_pins[], classes[], mode, lineage` — carries no such annotation, and §3.5 says only "Registration by `judge` entry" (BP:158).

**Defect.** Registering a judge is minting a quorum member. If a `judge` entry is not gated by the operator key or the constitution quorum, then anything that can append can manufacture the M signers that authorise irreversible writes. Combined with F4c (adapters give distinct weight hashes cheaply) this is a complete takeover with no rule change and no key theft: register M lineages, satisfy N-of-M, sign anything. §7's assurance that "judges hold signing keys **bound at registration**" (BP:234) is exactly backwards — it is registration that needs the gate.

**Fix.** `judge` is a rule kind, gated identically to `rule`, and additionally: a registration is inert until an independence fold (F4b) has scored it against the existing roster over a common span, and a registration that would drop the roster's measured decorrelation below the ratified ceiling is refused.

---

### F7 · The canary and stratum lottery has no seed discipline: it is either predictable to the judge or non-replayable
**§2.4 (allocate fold), §3.9, imported constitution** · **BLOCKER** · **CONFIRMED (reasoning)**

**Claim.** BP:126 the `allocate` fold outputs "the funded set and the holds with reasons; **the human stratum and the canary lottery**." BP:194 specifies "a fixed seed per judge invocation recorded on the entry" — for the *judge*, not for the allocator. The imported reconciliation (§3) makes the stratum "the instrument that grades holds at all once the humans have left the class," and the imported no-humans model makes the null stratum constitutional.

**Defect.** A lottery inside a deterministic fold needs a seed, and there are only two places to get one, both fatal as written:
- **Wall time or an external PRNG** → the allocate fold is not a function of the tape, falsifier 1 fails, and the canary set cannot be reconstructed at audit.
- **The tape (`pos`, or a hash of the prefix)** → the lottery is *computable by the judge*, which reads the tape by design (BP:170 `subscribe`, BP:171 `query(sql, as_of_pos)`). A judge that can compute whether this cell is in the canary or the stratum can behave differently on the sampled cells. That destroys the one instrument that grades the hold side and the one control arm that grades the ranking. It is the cleanest Goodhart surface in the entire design, and it is on the object the constitution declares non-adjustable.

**Fix.** Commit–reveal, or a VRF, over the deposit clock: the transactor commits `H(secret ‖ epoch)` as an entry at the start of each period; membership is `PRF(secret, cell_id) < rate`; the `secret` is revealed as an entry at the *end* of the period. During the period the judge cannot compute membership; after the period any replayer can verify it exactly. Rate is the ratified, pinned parameter (reconciliation §4). Add falsifier 12: *the lottery that cannot be read early* — a judge with full read access at position P cannot predict period-P membership better than chance, and a replayer at P+1 reproduces it exactly.

---

### F8 · `fetch` has no horizon, no cost and no cap — a free, uncapped, permanently ungraded verb that is 17% of all decisions
**§2.3, §3.9, falsifier 10, §12** · **BLOCKER** · **CONFIRMED (executed)**

**Claim.** BP:109 horizons per verb are `{h_act, h_work, h_hold, h_escalate}`. BP:299 the glossary says "**Verb**: hold, act, work, fetch, escalate" — five. `core:81` `V_FETCH` is a real verb with a real threshold (`core:264`, `fetch_margin = 0.2`, `core:241`).

**Evidence (PROBE 5, executed on the test's own worked period):**
```
5000 open -> act 2706 · work 400 · escalate 40 · FETCH 859 · hold 995
fetch share of all verbs : 17.2%
VerbCost::fetch          : 0.0 supervision minutes   (dispatch:125)
kappa contribution       : created += 0.0, removed += 0   (dispatch:289)
```

**Defect.** One decision in six has no horizon, therefore no verdict, therefore no contribution to n_eff on any side of any threshold, therefore no effect on the license. It also costs nothing in kappa and is capped by nothing — pass 3 caps `work` against `SeatPool`, pass 2 caps `escalate` against `WarrantPool`, and no pass touches `fetch` at all. There is no fetch-depth limit anywhere.

**Consequence — this is the design's Goodhart escape hatch.** A judge under a grading regime that punishes wrong acts and (eventually) wrong holds has one verb that is free, unlimited, and never scored. Emit `fetch` and you are never wrong. The commitment ages, the deadline approaches, `slot_of` walks it toward slot 0 (`core:151-156`), and the record shows a diligent judge gathering context. The blueprint has no rule that a cell may fetch at most k times before the fetch is itself graded as a hold.

**Fix.** (a) Add `h_fetch` and grade a fetch by whether the fetched context actually moved the margin across a threshold within the horizon — a fetch that changes nothing is `wrong`. (b) Cap fetch depth per cell per period as a native constraint, and convert an over-cap fetch to `hold` with reason `D_FETCH_EXHAUSTED`, mirroring the budget degradation (`dispatch:248-253`). (c) Set `VerbCost::fetch` from the wire — a fetch costs card time, and on the no-humans planet compute *is* the adjudication budget. (d) Amend falsifier 10 to name five verbs, and add falsifier 13: *the verb that cannot hide* — every verb kind has a finite horizon and a verdict distribution, and a class whose fetch share exceeds a ratified ceiling is unlicensable.

---

### F9 · The monotone license property is not expressible as a fold; part of it must live in the transactor
**§2.4, §3.9** · **MAJOR** · **CONFIRMED (reasoning)**

**Claim.** BP:124 the license fold "may narrow on evidence, may widen only within a ratified rule." BP:184 it "narrows on any evidence, widens only within a ratified rule, and never touches the stratum rate downward."

**Defect.** "May not widen" is a predicate over a *transition between two fold values*, not over a tape prefix. A pure fold recomputed cold from genesis has no previous value; it returns whatever the tape says. So the property as written can only ever be *checked after the fact*, never *enforced* — and a check that fires after an unlicensed widening has already licensed the act is not a fence. The task's question is exactly right: it cannot be a fold property without the ratification rows in its input, and even with them, only *half* of it becomes a fold.

**The decomposition that works.** Two functions, in two places:

*In the fold (pure, prefix-only):*
```
license(P, class, band) = MEET( evidence_ceiling(P, class, band),
                                ratified_ceiling(P, class, band) )
evidence_ceiling  = the widest scope the grades on prefix P support, under the
                    both-sides n_eff rule; antitone in evidence (more evidence,
                    never wider)
ratified_ceiling  = the widest scope any rule row on prefix P authorises for this
                    (class, band); a pure lookup, sourced ONLY from rule rows
stratum_rate(P)   = MAX( ratified_rate(P), evidence_demanded_rate(P) )
```
Monotone-in-the-right-direction then falls out as a theorem: the fold cannot widen beyond `ratified_ceiling` because widening is not in its range, and it cannot lower the stratum rate because `MAX` is not a lowering operator.

*In the transactor (a constraint, where no model runs):* a `rule` row that **raises** `ratified_ceiling` or **lowers** `ratified_rate` for any (class, band) is refused unless it carries the operator key or the constitution quorum, **and** its effective position is at least `ratification_delay_pos` in the future (a timelock in the deposit clock), **and** the longest finite horizon has elapsed since the evidence it cites. A `rule` row that narrows or raises the rate needs no such gate. This asymmetry is the whole property, and it is a constraint, not a reduction.

**Fix.** Move the sentence out of §2.4's fold table and into §3.6's constraint list, restated as above; state the fold's inputs exactly (see the table in §3 below); and add the `MEET`/`MAX` forms to §3.9 so the property is checkable rather than promised.

---

### F10 · The both-sides n_eff rule is specified three incompatible ways, and the gate it must run in takes one scalar
**§2.4, §3.9, §11, falsifier 10** · **MAJOR** · **CONFIRMED (code + text)**

**Three statements, pairwise inconsistent:**
- BP:124 — "n_eff **per verb**" (5 numbers).
- BP:184 — "n_eff above the floor **on both sides of the threshold**" (2 numbers, and "the threshold" is singular).
- BP:263, falsifier 10 — "A band is never licensed on act-side grades alone. Lie: **pooled n_eff across verbs**."

There is no single threshold. `gate()` has three (`core:238-244`): `act_margin = 1.0` separates {act, work} from {fetch, hold}; `fetch_margin = 0.2` separates {fetch} from {hold}; `escalate_margin = 0.5` is a *dispersion* threshold separating {escalate} from everything, and it is not on the same axis as the other two. "Both sides of the threshold" does not name a partition.

**And the structure cannot carry it.** `GateIn` has a single `float n_eff` (`core:250`; PROBE 6: `sizeof(GateIn) = 20`), consumed as one scalar comparison `if (g.n_eff < p.n_eff_floor) return V_ESCALATE;` (`core:257`), fed one value per lattice cell (`dispatch:217`). §11 (BP:290) carries `gate` forward "**unchanged**." A gate that takes one scalar cannot enforce a rule about two or five, so falsifier 10 as written is untestable against the component the blueprint says it is testing.

**The reconciled rule (from reconciliation §3), stated precisely — this is the answer to the task's question:**
> A (class, band) is `calibrated` at position P **iff**, for **every** verb v the class can emit under its current license, `n_eff_v(P) ≥ n_eff_floor`, where `n_eff_v` counts only decisions of verb v whose horizon `h_v` has expired on the tape and whose verdict is `right` or `wrong` (never `ungraded_yet`, never `ungradable`). The band's effective n_eff is `min over v`. Per verb, **not** per side, because "side" is ill-defined across three thresholds; and per *emittable* verb, so that demoting a class does not silently license it by removing the verbs it was failing on.

**Consequence to state in §3.9, measured (PROBE 5).** `min over v` is dominated by the rarest verb, and `escalate` is rare *by construction* because pass 2 caps it: **40 escalations in a 5000-commitment period** against `n_eff_floor = 175` → **4.4 periods** minimum. Worse, those 40 are `stable_sort`ed by `look_value` (`dispatch:240-243`) and truncated at the budget — the top of the value ranking, not a random draw. So the escalate leg of the license is estimated on a systematically biased sample, and no amount of waiting fixes bias. **The escalate n_eff must be estimated from the stratum/canary lottery, not from the funded briefs**, and §3.9 must say so.

**Fix.** Extend `GateIn` to `n_eff[5]` and `calibrated[5]`, or pass a `BandEvidence` struct; the "unchanged" claim in §11 for `gate` is wrong and must be struck. Rewrite falsifier 10 as: *a band whose `min over emittable v` of graded n_eff is below the floor is refused a license, and a band licensed while any verb's graded count is zero is a failure.*

---

### F11 · n_eff is indexed by a bucket whose membership turns over every period, and "band" is never defined
**§2.4, §3.9** · **MAJOR** · **CONFIRMED (executed)**

**Claim.** BP:124 the license fold produces "calibrated per class and **band**." The word "band" appears at BP:111, BP:124, BP:184 and in falsifier 10, and is defined nowhere in the blueprint, including the glossary (BP:299).

**Defect.** In the shipped code the only per-cell evidence index is the lattice cell, `i = project_one(seg, cls, slot)` (`dispatch:212`, `core:159`), and `slot` is `slot_of(d, now_ns, due_ns)` — *relative to now* (`core:151-156`). So a commitment migrates between evidence buckets as time passes, without changing in any way.

**Evidence (PROBE 6, executed).** One unchanged commitment, deadline fixed at +5 days:
```
day +0 -> lattice cell 101      day +3 -> cell 98
day +1 -> cell 100              day +4 -> cell 97
day +2 -> cell 99               day +5 -> cell 96
```
Six evidence buckets in six days. n_eff attached to such a bucket is not the effective sample behind the decisions that bucket will judge — the population is a *conveyor*, and the grades accumulated in slot 5 are read by decisions being made in slot 0 about different commitments at a different urgency. That is a non-stationary estimator presented as a calibration.

**Fix.** Define `band` explicitly in §2.3 as a stable, time-invariant partition of the margin (or of a margin×amount grid) per class — a *calibration bin*, which is what "calibrated per band" means everywhere else in the estate — and state that it is **not** the lattice cell. Bands are pinned in the ratification row alongside the criterion (reconciliation §4). The lattice slot stays what it is: a dynamics bucket for the field, never an evidence index.

---

### F12 · The outcome row is undefined for every verb; "reversed" is not expressible because the blueprint dropped `inverse_of`
**§3.9** · **MAJOR** · **CONFIRMED (code + text)**

**Claim.** BP:184 gives four verdicts and a horizon join and no outcome predicates. The task asks what grades each verb. The blueprint's rows cannot answer for any of the five.

- **act / work — "reversed" has no representation.** The blueprint's `fact` body is `table, key, op, before, after, inverse, source_pos` (BP:68). `inverse` is the *pre-computed undo to be used later*; there is no field saying "this fact **is** the undo of position N." The shipped code already had it and the blueprint deleted it: `dispatch:87` — `uint64_t inverse_of; // every effect carries its inverse; 0 means it is the inverse`. Without `inverse_of` on the `fact` body, the grade fold cannot join a reversal to the act it reverses, and *reversal is the entire definition of a wrong act*.
- **escalate — ungradable in these rows.** The human decision is a row, but the grade of an escalation is not "what the human decided" — it is whether the escalation was *worth twelve minutes* (`dispatch:122`). That requires the counterfactual: the verb the gate would have emitted. `Brief` carries none (`dispatch:78-83`); the blueprint's `verb` body carries none (BP:70); and F3 shows the reason is erased on the demote path. So escalate has a horizon (BP:109 `h_escalate`) and no computable verdict.
- **hold — no predicate, and the no-deadline case is undecidable.** BP:227 says no-outcome holds are "`ungradable` … graded only by the stratum." But `ungradable` is defined as "no deadline **and no outcome**" (BP:184), and "no outcome" is a property of the *whole* tape, not of a prefix — at position P a hold with no outcome yet is indistinguishable from one that gets an outcome at P+1. A prefix fold cannot emit `ungradable`. The verdict set is therefore not computable as specified.
- **fetch — see F8.** No horizon at all.

**Second defect — excluding `ungradable` re-creates the exact bias falsifier 10 exists to kill.** BP:227 excludes ungradable from the license. Ungradable decisions are systematically the no-deadline holds (reconciliation §3: "Holds with no deadline are excluded from licensing and graded only by the stratum"). Excluding them removes hold-side mass from the denominator and leaves the act side intact — which is precisely the act-heavy licensing bias falsifier 10 names. Excluding a biased subset does not remove the bias; it hides it.

**Third defect — "graded only by the stratum" has no stratum on the planet the blueprint imports.** The stratum is a *human* lottery (reconciliation §3). §2.4 (BP:126) keeps "the human stratum and the canary lottery" while §3.6 and §7 import the no-humans constitution. On that planet the canary acts on **reversible classes only** (reconciliation §6), so a no-deadline hold in an irreversible class is ungradable forever, by both instruments, on both planets. The blueprint never says so.

**Fix.** (a) Restore `inverse_of` to the `fact` body and require the effector to stamp it. (b) Add `counterfactual_verb` and `counterfactual_margin` to the `verb` body so escalate is gradeable. (c) Give each class a ratified `h_hold_nodeadline`; after it elapses on the tape a no-deadline hold closes as `ungradable`, making the verdict a prefix function. (d) Make the **ungradable fraction** a first-class licensing gate: a band whose ungradable fraction exceeds a ratified ceiling is unlicensable *regardless of n_eff*, and the stratum/canary rate scales with that fraction. (e) The verdict set needs a fifth member: `right` and `wrong` cannot express an act that succeeded and was reversed outside `h_act` — add `reversed_late`, counted against the license and used to test whether `h_act` was set too short. Per-verb outcome rows are specified in the table in §3 below.

---

### F13 · The persistence clause is not a constraint, and as written it defaults to pass
**§3.6** · **MAJOR** · **CONFIRMED (text)**

**Claim.** BP:162 lists among "native constraint kinds": "the persistence clause (no write may reduce the store's own capacity to persist without quorum)."

**Defect.** Every other kind in that list is decidable on rows the transactor holds: check expressions on the cell row, foreign keys between cells, exposure caps, a reversibility class, a signature count. The persistence clause is a predicate over a *counterfactual future world state*. "The store's own capacity to persist" is, per the imported no-humans model, energy, compute, bodies and the record — **none of which is a cell, a class, or a column anywhere in this blueprint.** There is no persistence table, no telemetry ingestion path for reserves, and no definition of "reduce."

**Why this is worse than an incomplete spec.** A constraint that cannot evaluate a predicate refuses nothing. It sits in the list looking like a fence and passes every write. That is falsifier 4's planted lie ("a constraint that drops the write") in its most dangerous form: a constraint that *silently permits*. And it is listed under §7's security model as though it were enforcement.

**Is it definable as a constraint at all? Partly — and the residual must be a hold, not a pass.** Definable part: declare persistence resources as ordinary classes with cells — `energy_reserve`, `compute_reserve`, `body_count`, `tape_replica_count`, `tape_bytes_retained` — ingested from the organisation's own telemetry exactly as any other wire (this is what the imported §3 means by "obligations the organization has to itself, ingested from its own telemetry"). Then the clause becomes two decidable native constraints already in the list: a **floor check** (no committed write may drive a persistence cell below its ratified floor) and an **exposure cap** (no class may consume more than its ratified share of a persistence resource per period). Undefinable residual: a write whose persistence cost is not modelled at all. That residual must **refuse or hold**, never pass — and the blueprint must say which. Given law 2 and the imported "running out of certainty degrades to holding," it holds, with reason `D_PERSISTENCE_UNMODELLED`, and the count of such holds is printed, because that count is the honest measure of how much of the clause is actually enforced.

**Fix.** Rewrite BP:162's clause as the floor + cap + unmodelled-holds triple above; add the persistence classes to §2.3; add falsifier 14: *the clause that is not a comment* — a write that provably reduces a modelled persistence reserve below its floor is refused, and a write whose persistence cost is unmodelled produces a hold, never a commit.

---

### F14 · Caps are placed in two layers at once, and the period has no durable clock
**§3.6, §7, §2.1** · **MAJOR** · **CONFIRMED (text)**

**Claim, twice, differently.** BP:162 lists "exposure caps per class per period" as a **transactor** constraint kind. BP:234 says "The **effector layer** beside the store carries caps per class per period." The same cap, in two layers, with no statement of which is authoritative or whether they are the same number.

**Defect A — they cap different things.** A transactor cap limits *committed writes*; an effector cap limits *bytes leaving*. They diverge whenever a committed write fails to leave (effector down, counterparty rejects) or leaves without a matching commit. Two caps that drift are worse than one: the store believes it is under cap while the effector refuses, so the tape records a legal commitment that the world never saw — and *that* is the state the whole design exists to make impossible.

**Defect B — the period's clock does not survive a restart.** The only ordering clock is `pos`; the only time stamp usable for ordering is `t_mono_ns` (BP:54), and `t_wall_ms` is explicitly excluded (BP:56). A monotonic clock resets on reboot, and §6 prescribes restart as the v0 recovery (BP:222). So period boundaries move on every crash, and a cap can be reset by a crash loop. **Answer to the task's question: the period's clock must be the deposit clock** — a period is a span of positions delimited by `tick` entries the transactor emits, so the boundary is on the tape, replayable, and crash-stable.

**Defect C — the boundary is unspecified.** No statement of whether a saga may straddle a boundary, whether unused cap carries forward, or what happens to a write in flight at the tick. Specify: caps are evaluated as a fold over committed `fact` entries **since the last cap tick**; a saga is evaluated wholly against one period (the period of its first fact); unused cap never carries forward (carry-forward makes a quiet period into a burst license).

**Defect D — answer to "can a cap be exhausted by refused writes?" As written, unknown; and the naive implementation says yes.** A cap held as a mutable counter decremented during constraint validation leaks on every refusal — and §4 (BP:190) guarantees saga atomicity for *appends*, not for validation-time side effects, so a 9-cell saga refused at cell 7 can consume cap for cells 1–6. That turns the cap into a denial-of-service surface: an attacker (or a looping judge) burns a class's entire period cap with writes that are all refused, and the refusals are correctly recorded while the class silently loses its ability to act. **The cap must be a fold over committed facts, never a mutable counter**, evaluated at validation and re-evaluated per saga as a whole. State it, and add falsifier 15: *the cap that refusals cannot spend* — N refused writes followed by one legal write leave the legal write funded.

---

### F15 · "A judge's key can sign a verb but not a rule" is unenforceable on the planet §3.6 imports
**§3.6, §7** · **MAJOR** · **CONFIRMED (text)**

**Claim.** BP:162 "Constraints change only by `rule` entries signed by the operator key, **or by the constitution quorum on a planet with no operator**. A judge's key can sign a `verb`; it cannot sign a `rule`." BP:234 repeats it.

**Defect.** On the planet with no operator, the constitution quorum **is made of judges** — that is the entire substitution the imported model performs ("a jury of heterogeneous models … the signature"). So the sentence reduces to: judges' keys cannot sign rules, except that the only thing that can sign a rule is a set of judges' keys. Either there exists a key that widens its own license, or the no-humans mode is unsupported. The blueprint claims both in one paragraph.

**Answer to the task's structural question — how would it be enforced?** Not by key capability alone. Three mechanisms, all needed:
1. **Separate key types by construction**, not by policy: a judge's verb key is a distinct keypair from its jury key, generated at registration, with the *domain tag* inside the signature preimage (F5). A verb signature over a rule preimage does not verify — the separation is cryptographic, not a transactor lookup that can be misconfigured.
2. **A transactor check on the key registry**, which is a fold over `key_register` / `key_revoke` entries, so "which key may sign which kind" is derivable from the tape and auditable, rather than a config the operator can silently change.
3. **The asymmetry that actually does the work is not who signs but what the signature can do**: a jury quorum may sign a rule that **narrows** (any time, any quorum) and may only sign a rule that **widens** after the longest finite horizon has elapsed, with a timelock in positions, and with a rollback test passed — the three conditions the imported constitution names and the blueprint dropped, keeping only "the constitution quorum."

**Fix.** Restate BP:162 as: a judge's *verb* key signs verbs and holds only; a judge's *jury* key signs quorum tokens only; neither authors a rule; a rule is authorised by the operator key **or** by a jury quorum **plus** the horizon, timelock and rollback-test conditions, and a widening rule requires all of them. Restore the three conditions to §3.6 and §7 explicitly.

---

### F16 · The `rule` kind is undifferentiated, so a horizon change gets a template-typo's gate
**§2.1, §2.3, §3.6** · **MAJOR** · **CONFIRMED (text)**

**Claim.** BP:73 one kind, one gate: `rule` with body `kind, payload, quorum[]` covering "writ, constraint, template, horizon, license changes."

**Defect.** These have wildly different blast radii and the blueprint gives them one signature policy. Specifically, **a horizon change is a license widening in disguise**: shortening `h_hold` makes hold-side decisions "gradeable" sooner, which raises hold-side n_eff, which satisfies the both-sides rule, which licenses the band — with no change to the license row at all. F10's arithmetic makes this the *cheapest* way to license a band: the honest path needs 4.4 periods of escalations, the dishonest path needs one `rule` entry retyping a nanosecond constant. And there is no floor on `h_hold` and no requirement that it relate to the class's observed deadline distribution.

**Fix.** Split `rule` into sub-kinds with distinct gates: `rule.template`, `rule.horizon`, `rule.constraint`, `rule.license`, `rule.writ`, `rule.key`, `rule.judge`. Classify each as **narrowing** or **widening** and gate accordingly (F9). Any horizon *shortening* is widening. Derive `h_hold` from a fold over the class's measured deadline distribution (e.g. the 95th percentile of `due_ns − opened_ns`) and refuse a `rule.horizon` that sets it below that derived floor without the full widening gate.

---

### F17 · Shadow tapes are an ungoverned write path into the license
**§3.5, §3.8, §3.3** · **MAJOR** · **CONFIRMED (text)**

**Claim.** BP:150 the peer "Writes nothing to **the tape** except through the transactor's API" — singular, and per the glossary (BP:299) "the tape" is the primary. BP:158 lineages are "several judges registered against one tape, each with its own **shadow tape**, graded per class by the grade fold." BP:180 a replay writes "a separate shadow tape."

**Defect.** Shadow tapes are explicitly outside the single-writer rule, and they are simultaneously *inputs to grading*, hence to the license, hence to what runs unattended. A judge (or the peer process) that writes its own shadow tape can manufacture its own grades and license itself. There is no statement that shadow tapes are hash-chained, transactor-serialised, or that entries on them are distinguishable from primary entries after the fact. This composes with F5.5 into signature harvesting and with F4 into a self-certifying jury.

**Fix.** Shadow tapes go through the same transactor, in the same chain, with a `shadow_id` in the header and a `mode` field in the signature preimage; law 3 admits no exception. A shadow entry may never be joined to a primary outcome except by the grade fold, and the grade fold must record which tape each side of the join came from.

---

### F18 · Prompt injection reaches the shared trunk, and the pinned template guarantees it reproduces
**§2.3, §2.5, §7** · **MAJOR** · **CONFIRMED (text)**

**The named attack.** `fact` bodies carry `before`/`after` from the world (BP:68) — counterparty-authored text: order memos, claim narratives, invoice references, email bodies. The class template renders "a cell and **its neighborhood**" into the judge's canonical bytes (BP:110). The judge emits verbs. Nothing in §7 (BP:232-234) addresses content at all; §7 is entirely about keys, network modules and caps.

**Three amplifiers the design supplies for free.**
1. **The pin makes it deterministic.** A template pinned by hash pins the *renderer*, not the *rendered content*. §4's determinism guarantee therefore guarantees that a working injection works identically on every replay, every machine, every lineage.
2. **The cognitive index caches the poisoned read state**, keyed `(cell_id, pos_last, template_pin, judge_pin)` (BP:132), and it is invalidated only by a write to the cell or its neighborhood — not by anything noticing the content.
3. **The trunk is shared by every fork.** BP:134: "The trunk holds the standing prefix every cell inherits: the writ digest, the schema, **the precedent digest**." A precedent digest derived from prior decisions is derived from prior judgments, which are influenceable by prior injected facts. That is a persistence mechanism into a prefix that every cell in the organisation inherits — and every juror reads (F4-F).

**Fix.** (a) World-authored text is rendered in a delimited, escaped, provenance-tagged region the template may not lift out of; (b) the template is **forbidden** from placing world-authored bytes in the trunk — the precedent digest is a hash, never prose; (c) add falsifier 16: *the fact that cannot instruct* — a fact whose text field contains an instruction string must produce a margin bit-identical (or within run noise) to the same fact with that string replaced by equal-length filler, or the template fails its pin.

**Other trust boundaries and the attack each does not stop:**

| boundary | what it stops | what it does not stop |
|---|---|---|
| world → transactor (`fact` ingest) | nothing stated — no authentication of the change stream is specified anywhere | forged facts; CDC replay (see F19 — the blueprint destroyed the stale-row comparand) |
| judge → transactor (verb keys) | an unregistered model emitting verbs | a registered judge flooding `hold` (cost 0, never refused, never capped) or `fetch` (F8); no verb-rate constraint exists |
| peer → network (module gate, **[M]**) | exfiltration over sockets | exfiltration through **effector payloads** — a judge encodes data in a `fact` body the effector sends to a counterparty. §7 caps volume per class per period and "prints every byte," but printing is not refusing and there is no content constraint on outbound bodies |
| operator key → rules | unsigned rule entries | a single key with no threshold, no rotation kind, no revocation kind, no timelock; theft and legitimate rotation are the same tape shape, and the tape *records* the compromise without *preventing* it |
| `judge` registration | nothing — no key required (F6) | sybil quorum |
| shadow ↔ primary | nothing (F17) | self-certification, signature harvest |
| template pin | a changed renderer going unnoticed | a legal `rule.template` as a **DoS**: BP:225 "mass invalidation, staged by ranking; the cost is printed" — printed, not capped. One legal entry forces the peer to re-render everything |
| transactor clock | nothing — no attestation | a wrong or restarted clock mis-grading every decision at once (F1, F14) |

---

### F19 · The 64-byte cell has a three-way collision, and the fix in §2.2 destroys falsifier 9's comparand
**§2.2, falsifier 9, open question 9** · **MAJOR** · **CONFIRMED (executed)**

**Claim.** BP:82: "the 32-bit `src_rev` and the 4 padding bytes become a 64-bit `pos_last`, which closes the truncation defect found in `osv_ingest.h`." Falsifier 9 (BP:262): "A wire whose positions exceed 2^32 still **refuses stale rows and detects replays**. Lie: the 32-bit `src_rev`."

**Evidence (PROBE 1, executed).**
```
sizeof(Commitment) = 64 · declared members = 60 · tail padding = 4 bytes
blueprint 2.2 spends src_rev(4) + padding(4) on pos_last(8) -> 0 bytes left
```

**Defect A — two different clocks were merged.** `src_rev` is the **source** LSN, per wire; `pos_last` is the **tape** position. `Ingest::apply`'s replay defence compares them: `if (r.rev == cur->src_rev) { ++n.duplicate; return false; } if (r.rev < cur->src_rev) { ++n.stale_refused; return false; }` (`ingest:316-319`). Replace the field with a tape position and the comparand is gone — the stale-row refusal has nothing to compare `r.rev` against. **The blueprint's fix removes the mechanism falsifier 9 tests, while keeping the falsifier's text.** The cell needs *both* a 64-bit source revision and a 64-bit tape position, and after §2.2's change there are zero bytes left for the first.

**Defect B — the same 4 bytes are claimed twice more.** Reconciliation §5 allocates that identical padding to `judged_rev`: "`Commitment` has four padding bytes, so a `judged_rev` fits in the cache line" — the freshness stamp without which "the seam cannot tell a stale margin from a fresh one." The blueprint's `Cell` (BP:85-99) keeps `margin // last judged` with **no judged-at stamp**, so it reintroduces the exact stale-margin defect the reconciliation identified, and §3.5's decode-failure rule (BP:158, "never a stale margin") has no field to enforce against.

**Defect C — open question 9 asks the wrong question and proposes the wrong answer.** BP:277 asks whether `pos_last` is the last `fact` or the last entry of any kind, proposing "last `fact`." For the quorum compare-and-swap (F5) the answer must be **last entry of any kind**, or a `hold` or competing `verb` between the vote and the write leaves the signature valid.

**Fix.** Accept 128 bytes (two cache lines) for the hot row, or move `amount`/`seg`/`seat` to the cold record. The three fields that must be on the hot row are `pos_last_any_kind` (u64, the CAS token), `src_rev` (u64, the replay defence), and `judged_rev` (u64 or u32, the freshness guard). State which cache-line budget was chosen and re-run the conservation and determinism falsifiers against it, because §5's entire sizing table (BP:203, "50,000 · 3.2 MB") is downstream of the 64.

---

### F20 · `Ingest::fill` clears `F_BLOCKED` on every update, and the blueprint imports it unchanged
**§11, §3.9** · **MINOR** · **CONFIRMED (code)**

`ingest:283` preserves only `F_SHADOW | F_EXPLORE | F_DIRTY` across a fill and re-derives the rest from `cm.flags`. `F_BLOCKED` (`core:70`) is in neither set, so any CDC update to a blocked commitment silently clears it — and `gate()` returns `V_HOLD` on `F_BLOCKED` (`core:256`) with `D_BLOCKED` as its reason (`dispatch:223`). A blocked cell therefore becomes actionable on the next unrelated field update, and the `D_BLOCKED` reason disappears from the grade fold's population. BP:291 imports `Ingest::apply` with only two changes named (64-bit positions, back-fill). **Fix:** add `F_BLOCKED` to the preserved mask, or derive it from `blocked_by` on every fill; either way say which in §11.

---

## 3 · Precise specification

### 3a · Reversibility — where the class comes from

| aspect | specification |
|---|---|
| **source of truth** | The **class map**, field `reversibility` on `ClassMap`, keyed by `(class, table, op)`. Covered by `SchemaMap::pin` (`ingest:165`), so a change is a visible re-pin and a `rule.constraint` entry. Precedent: `ingest:144`, `// declared, not inferred`. |
| **values** | `reversible_by_inverse` · `reversible_within(window_ns)` · `irreversible`. Not binary — open question 3's take-back window makes it time-dependent. |
| **who may assign** | Nobody at write time. The transactor **derives** it from the pinned map for every touched cell. |
| **role of the write's field** | A redundant assertion only, for the writer's own error detection. |
| **what the transactor must refuse** | (1) any write whose declared class disagrees with the derived class → `refuse(reversibility_mismatch)` **plus** a `warn`, and the mismatch is an attribute of the proposing judge in the grade fold; (2) any write on a cell whose derived class is `irreversible` without a valid quorum, **including via `write(tx)`** — closing the §3.7 bypass of `o_warrant`; (3) any `fact` on an `irreversible` cell whose `inverse` is absent or is the null inverse; (4) any write against `reversible_within(w)` submitted at a position where `w` has already elapsed — that write is `irreversible` and needs the quorum. |
| **what must never happen** | A judge's own declaration determining whether its write needs a quorum. |

### 3b · The license fold — exact inputs and predicate

| aspect | specification |
|---|---|
| **inputs (complete)** | `grade` verdicts (fold output, per decision, per verb, horizon-expired only) · `rule.license` rows (the ratification: class, band criterion, stratum rate, canary rate, quorum-of-M) · `rule.horizon` rows · `rule.judge` registrations (which verbs a class may emit) · `tick` entries (horizon expiry as a tape event) · the **kappa fold's** `removed_confirmed` output (BP:124 omits this and cannot compute its own "demoted per class" without it) · the pinned `schema_pin` and `template_pin` at each decision. |
| **predicate (the fold, pure over prefix P)** | `license(P, cls, band) = MEET( evidence_ceiling(P), ratified_ceiling(P) )` where `ratified_ceiling` is a lookup sourced **only** from `rule.license` rows and `evidence_ceiling` is antitone in evidence. `calibrated(P, cls, band) ⟺ ∀ v ∈ emittable_verbs(cls): n_eff_v(P) ≥ n_eff_floor`, with `n_eff_v` counting only horizon-expired decisions whose verdict ∈ {`right`, `wrong`, `reversed_late`}. Band n_eff = `min over v`. Additionally `ungradable_fraction(P, cls, band) ≤ ratified_ungradable_ceiling`, else unlicensable regardless of n_eff. `stratum_rate(P) = MAX( ratified_rate(P), evidence_demanded_rate(P) )` — `MAX` is why the rate can never be lowered by the fold. |
| **what must live in the transactor, not the fold** | The asymmetry itself. A `rule` row that **raises** `ratified_ceiling`, **lowers** `ratified_rate`, or **shortens** any horizon is refused unless it carries (operator key ∨ jury quorum) **and** a position timelock **and** the longest finite horizon elapsed since its cited evidence **and** a passed rollback test. Narrowing rows need none of this. Monotonicity is a **constraint**, not a reduction — a pure fold has no previous value to be monotone against (F9). |
| **escalate's n_eff** | Estimated from the stratum/canary lottery, **never** from the budget-funded briefs, which are the `look_value`-ranked top of the queue (`dispatch:240-243`) and a biased sample (F10). |

### 3c · The outcome row, per verb

| verb | horizon | outcome rows that grade it | `right` | `wrong` | `ungradable` |
|---|---|---|---|---|---|
| `act` | `h_act` (reversal window) | any `fact` with `inverse_of = act.pos` (**field must be restored**, `dispatch:87`); world compensation facts (chargeback / return / credit / repudiation) matched by class; cell reopen (`ingest:342-349`, counted `n.reopened`) | no reversal and no compensation within `h_act`, and the obligation closed | reversed, compensated, or reopened within `h_act`. `reversed_late` if after `h_act` — counted against the license and used to test whether `h_act` is too short | never — an act always has an observable world outcome |
| `work` | `h_work` | as `act`, plus a worker-output acceptance/rework fact | accepted, not reworked, not reversed | reworked or reversed within `h_work` | as `act` |
| `hold` | `h_hold` = deadline + breach detection; `h_hold_nodeadline` (ratified, per class) when `due_ns = 0` | deadline-breach fact; a later `act` on the same cell whose outcome is worse than the counterfactual at hold time; in shadow, a human/world act on a cell the judge held (immediate hold-side signal, reconciliation §3); stratum/canary lottery outcomes | the obligation closed within its deadline without action, **or** the stratum acted on a lottery draw from this band and the outcome was no better | deadline breached while held; the stratum/canary acted and the outcome was better than holding | `due_ns = 0` **and** `h_hold_nodeadline` elapsed with no outcome — a *prefix* predicate, not "no outcome ever" |
| `escalate` | `h_escalate` (human/jury decision arrival) | the human's decision row, joined against the **`counterfactual_verb` recorded on the escalation** (**field must be added**) | the human's decision **differs** from `counterfactual_verb` — the twelve minutes (`dispatch:122`) bought a different answer | the human ratified `counterfactual_verb` — a rubber stamp; kappa numerator with no denominator (`dispatch:287`) | the human never decided within `h_escalate`; must also be distinguishable from `D_BUDGET`-starved holds |
| `fetch` | `h_fetch` (**must be added**, BP:109 omits it) | the next judgment on the same cell after the fetch | the post-fetch margin crossed a threshold — the fetch changed the decision | the margin did not move across any threshold, or the cell fetched again beyond the ratified depth cap | never — a fetch always has a successor judgment or a deadline |

Verdict set: `right`, `wrong`, `reversed_late`, `ungraded_yet`, `ungradable` — five, not four. `ungraded_yet` is excluded from n_eff. `ungradable` is excluded from n_eff **and counted in `ungradable_fraction`**, which is itself a licensing gate — excluding it silently is the act-side bias falsifier 10 exists to kill (F12).

### 3d · What a quorum signature must bind

One canonical preimage, all fields, no exceptions:

| field | why |
|---|---|
| `domain_tag` (`"tapestry.quorum.v1"`) | a verb signature must not verify as a rule signature — separation is cryptographic, not policy (F15) |
| `tape_genesis_h` | cross-organisation / cross-tape replay |
| `mode` (`live` \| `shadow`) | kills signature harvesting from replays of history (F5.5, BP:180) |
| `saga_digest` + `n_facts` | kills saga re-partitioning and grafting |
| `∀ touched cell: (cell_id ‖ pos_last_any_kind)` | makes the signature a compare-and-swap; **must be last entry of any kind**, not last `fact` — open question 9 proposes the wrong value (F19-C) |
| `reversibility_class` (the **derived** one) | a quorum for a reversible write must not upgrade |
| `judgment_ref` | binds the authorisation to the judgment that justified it; without it, law 6's pins are decorative |
| `signer(weight_sha256 ‖ serve_pin ‖ key_id)` | retuned judges' old signatures expire; enables the independence check |
| `template_pin` ‖ `schema_pin` | the vote was cast on a specific rendering of a specific world; a re-pin invalidates it |
| `not_valid_after_pos` | bounds the window in the deposit clock, never in wall time |

**Key scheme.** Ed25519, two keypairs per judge generated at registration: a `verb_key` and a `jury_key`, distinguished by `domain_tag` inside the preimage. Custody: private keys inside the peer's process, never on the tape; the registry of public keys is a fold over `rule.key_register` / `rule.key_revoke` entries, both gated as rules. Rotation: a `rule.key` entry naming old and new key ids, with a position timelock, so a stolen key's rotation is visible before it binds. Revocation is immediate on the operator key or a jury quorum. **Independence is measured, not asserted:** each registered judge replays a common held-out span in shadow; a fold computes pairwise disagreement per class; a quorum is valid only if its M signers' maximum pairwise agreement is below the ratified ceiling, and two judges sharing a `base_weight_sha256` count as **one vote** (F4).

---

## 4 · What is right and must not be changed

1. **Law 2 and the `hold` kind.** A decision not to act as a first-class row with margin and reason, surviving compaction, is the single best idea in the design and the shipped code already proves the pattern (`o_no_silence`: 4000 open → 4000 rows, verbs sum to 4000).
2. **The safety property, verbatim.** Running out of humans degrades to holding and never to acting (`dispatch:22-27`, `dispatch:248-253`). Measured this session: 120 minutes → exactly 10 briefs, 1990 held with reason `budget-exhausted`, **0 acted**. Keep pass 2's structure exactly as it is; my F3 fix adds a separate pool, it does not touch the degradation rule.
3. **`o_warrant`.** Irreversible never acts, even under pressure: 200 irreversibles, 0 acted, 195 held. This is the property F2 exists to protect from §3.7.
4. **`o_evidence`.** Unmeasurable is never mistaken for safe — `n_eff = 20` against floor 175 gives 0 acted, 600 escalated; uncalibrated likewise. `core:257-258` returning `V_ESCALATE` before reading the margin is exactly right and must survive the `GateIn` widening in F10.
5. **The refusal that never silences** (falsifier 4) and refusals as entries (BP:142). Every fix I propose above routes through it.
6. **Reversibility, inverses, and the quorum existing at all** as native store concerns rather than application convention. The mechanisms are broken; the placement is right.
7. **The monotone asymmetry as a stated law** — narrow on evidence, widen only on a ratified rule, never author the rule. F9 moves where it is enforced; the law itself is the most important sentence in §3.9.
8. **Per-verb horizons and both-sides grading as the stated goal** (BP:109, BP:184, falsifier 10). The reconciliation's diagnosis — that a pooled reducer licenses on act evidence while the hold side is unknown — is correct and the blueprint is right to import it. F10 sharpens the rule; it does not question it.
9. **`verify_fold`** (BP:128): cold recompute from genesis must equal the incremental result bit for bit. This is the check that would have caught F1 on day one, and it must be a gate on R4, not a mode.
10. **The honest note in `report_dispatch`** that kappa is a forecast (`dispatch:324-327`) and that an autonomy count must never print without the error rate of what ran unattended. Promote it from a printed paragraph to a **column and a refusal** (BP:64 already promises this) — the report must refuse to emit a license state at all while `removed_confirmed` is zero.

---

## 5 · Top three changes

1. **Reversibility is derived from the pinned class map, never declared by the writer, and `write(tx)` gets no path to an irreversible act that the gate would refuse** — this is one field, one transactor check, one typed refusal, and without it the entire quorum is optional at the writer's discretion (F2).
2. **Give the grade fold a tape-anchored clock and a fifth verb**: horizon expiry becomes a `tick` entry, `grade` stops being both a fold output and a tape kind, `h_fetch` and `inverse_of` and `counterfactual_verb` are added — because today 17.2% of decisions are ungradeable by construction, "reversed" is inexpressible, `escalate` has no verdict, and two cold replays of one tape disagree (F1, F8, F12).
3. **Make independence measured and registration gated, or delete the jury and say so**: a `judge` entry gated like a rule, `base_weight_sha256` collapsing lineages to one vote, a pairwise-disagreement fold with a ratified ceiling, a `dissent` kind, and a named process that may actually reach an off-card model — because as written §7's module gate forbids the network the jury needs, §5 budgets one 7 GB judge, and falsifier 8 passes with M clones of one model (F4, F6).

---

## 6 · Open questions to add to §10

11. **What is a band?** The word carries the license in §2.4, §3.9 and falsifier 10 and is defined nowhere. Is it a margin bin, a margin×amount grid cell, or the lattice cell? If the lattice cell, PROBE 6 shows its membership turns over every period and it cannot carry a calibration.
12. **In what clock is a horizon measured, and how does it survive a restart?** `t_mono_ns` does not; `t_wall_ms` is forbidden for ordering; deadlines arrive from the world as wall time (`ingest:278`). Pick one and state the reconciliation.
13. **Where does the canary/stratum lottery's seed come from, such that a judge with full read access cannot predict membership and a replayer can reproduce it exactly?** Commit–reveal, VRF, or the instrument that grades holds is not blind.
14. **What credits kappa's `removed`, and when?** The shipped code credits at dispatch and says in its own comment that this is an assumption. If the blueprint's kappa fold does not split forecast from confirmed, the irony limit never fires on a class that acts (PROBE 2: acted=214, created=0, kappa=0.000, verdict `ok`).
15. **Is demotion absorbing, and should it be?** It is, by arithmetic, in the shipped code. If it should not be, what is the re-licensing criterion and which fold computes it? If it should be, say so and price the ratchet — every demotion is permanent and the terminal state is everything escalating.
16. **Where is the effector's cap, and where is the transactor's?** §3.6 and §7 place the same cap in two layers. If both, which is authoritative when they disagree, and what is the entry recording the disagreement?
17. **Which process may reach a jury member off the card, and what is its gate?** §7 forbids the peer a network; §5 budgets the card for one judge; the transactor has no model runtime.
18. **Are shadow tapes chained and transactor-serialised?** They feed the grade fold, hence the license. If the peer writes them directly, law 3 has an exception on the path that decides what runs unattended.
19. **How many bytes is the hot row, really?** `pos_last`, `src_rev` (64-bit) and `judged_rev` cannot coexist in 64 (PROBE 1: 4 bytes of padding, all three claimed). §5's whole sizing table is downstream of the answer.
20. **What is "the store's own capacity to persist" as rows?** Name the persistence classes and their floors, or state that the clause is aspirational and that unmodelled persistence cost produces a hold rather than a silent pass.
21. **Is the quorum rule N-of-M, or N-of-M with zero dissents?** §7 says the first; falsifier 8 requires the second; and a dissent currently has no row, so a juror voting no is indistinguishable from a juror that was offline.

---

*QC-4 of 7. Evidence: `osv_dispatch_test.cpp` 8/8 pass and `qc4_probe.cpp` 6/6 probes, both built with `g++ 13.3.0 -O2 -std=c++17` under WSL Ubuntu-24.04 on 2026-09-08; sources and probe retained in `C:/TAPESTRY/qc/scratch/license/`. Where this report and a receipt disagree, the receipt wins and this report is the defect.*
