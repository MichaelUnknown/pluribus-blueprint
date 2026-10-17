# Implementation roadmap

This document is the authoritative implementation sequence for the blueprint trainer and dashboard. It replaces the earlier eight-step outline and nine-step acceptance table. Milestone 1 is implemented with synthetic-fixture validation; complete external-model integration validation remains pending. Milestone 2 is implemented and validated with deterministic, dataset-free engine/evaluator tests. Milestones 3–10 remain planned.

The [technical specification](blueprint-implementation.md) defines algorithms and artifact contracts; the [dashboard specification](blueprint-dashboard-spec.md) defines UI and API behavior. Their section numbers are not implementation milestone numbers.

## Delivery principles

- Milestones describe independently verifiable outcomes, not a requirement for exactly one PR each. Split substantial milestones, particularly 8 and 9, into focused PRs.
- Keep a deterministic single-threaded reference mode throughout development. Introduce correctness tests before optimizations.
- Establish stable history IDs, versioned rule/action/information profiles, and model identities before persistence or tree browsing.
- Use small deterministic fixtures in CI. Validate supplied complete models separately; never silently use partial pilot models for unrestricted production training.
- Run bounded pilots and measure memory early. A full training campaign is an experiment after implementation readiness, not a routine CI test or a promised runtime/win rate.

## Milestone summary

| Milestone | Deliverable | Dependencies |
| --- | --- | --- |
| 1 | Project foundation and model import | Supplied format specification; complete models for external integration validation |
| 2 | Poker engine | 1: card conventions and test infrastructure |
| 3 | Abstract game tree and storage contracts | 1–2 |
| 4 | Reference MCCFR and initial evaluation | 1–3 |
| 5 | Telemetry and progress dashboard | 4 for real training data; UI contracts/fixtures can start at 3 |
| 6 | Persistence and resume | 3–4; integrate telemetry from 5 |
| 7 | Snapshot-based tree explorer | 3, 5–6 |
| 8 | Strategy averaging and training refinements | 4, 6; extend dashboard from 5 and 7 |
| 9 | Scaling and parallelization | Validated reference and persistence from 4, 6, 8 |
| 10 | Operational readiness and systematic evaluation | 5–9 |

The table is a delivery order, not a requirement that backend work wait for the UI: milestone 6 can proceed independently of frontend completion in 5. Likewise, training refinements can proceed once their backend prerequisites are ready.

## 1. Project foundation and model import

**Scope:** C++20/CMake project, CI, test infrastructure, configuration, standalone v2 model reader, card canonicalization, BucketProvider, and the 169 preflop classes. Document working build/test commands as they are introduced.

**Acceptance:**
- Independently load and query flop, turn, and river models.
- Check the specification's canonical-key vectors, suit/card-order invariance, dimensions, checksums and round compatibility.
- Reject malformed files and incomplete production coverage. Keep intentionally partial fixtures explicitly restricted to tests.
- Run CI without downloading complete training datasets; provide a separate full-model integration check.

**Implementation status:** The foundation, strict v2 reader, BucketProvider, preflop classes, CLI and CI are implemented. See [build/test and import instructions](model-import.md). Complete external models and producer-supplied golden files were not available for this change; the separate integration check must still be run on those artifacts.

**Boundary:** No poker trainer or frontend runtime yet. Legacy v1 support is optional and must not delay the v2 contract.

## 2. Poker engine

**Scope:** Shared-deck sampling, six-player rules, blinds, action order, checks/folds/calls/raises, short all-ins, reopening, side pots, uncalled bets, showdown and payouts. Integrate and validate a hand evaluator.

**Acceptance:**
- Simulate complete legal hands deterministically from a seed.
- Test chip conservation and fixed edge-case hands independently of bucket classes.
- Preserve hidden-information boundaries and prevent state leakage across copied or undone branches.

**Implementation status:** The reference engine, deterministic deck sampler, 5–7-card evaluator, public/player observations, copy-based branching, settlement and `simulate-hand` smoke command are implemented. See the [poker-engine guide](poker-engine.md) for rules, API and checks.

**Boundary:** Concrete legal actions are engine behavior; the limited bet-size menu is introduced in milestone 3.

## 3. Abstract game tree and storage contracts

**Scope:** Versioned bet-size configuration, rounding and deduplication, information-set keys, stable public-history IDs, and reconstruction of public state. Implement a simple, on-demand strategy store with a memory limit. Define identities and interfaces needed by checkpoints, policy exports, and the tree API.

**Acceptance:**
- Navigate public action histories without training or materializing the entire game tree.
- Verify that identical information sets have identical ordered legal actions.
- Confirm that policies cannot depend on opponents' hidden cards or future board cards.
- Fix and test the imperfect-recall profile and preflop numbering.
- Publish typed dashboard/API fixtures; distinguish planned contracts from implemented endpoints.

**Boundary:** Budget memory now; defer compact encodings and caches until profiling. Tree navigation must not allocate regret tables.

## 4. Reference MCCFR and initial evaluation

**Scope:** Single-threaded external-sampling MCCFR with Float64 regrets, deterministic sampling, and no discounting or pruning. Add independently checkable small games, a bounded six-player pilot, and a simple match harness with frozen baselines.

**Acceptance:**
- Match hand-calculated one-step regret updates and an independent full-CFR reference on suitable small games.
- Validate own-reach-weighted strategy averaging on small games now, including the average used for two-player exploitability checks.
- Run legal six-player pilot hands across all streets with the model adapter; measure throughput and memory.
- Evaluate current policies through an in-memory policy interface before disk export exists.

**Boundary:** Small-game averaging is a correctness oracle, not the production preflop/postflop schedule. Production averaging arrives in milestone 8. Use a reduced, explicitly labelled pilot configuration and bounded traversal budget; do not require a full 100-BB campaign.

## 5. Telemetry and progress dashboard

**Scope:** Metric definitions, periodically aggregated counters, bounded telemetry/events, read-only API, Vue/TypeScript scaffold, live overview, and reconnect/stale states.

**Acceptance:**
- Observe an actual training run with explicit units and source timestamps.
- Show budget consumption, not percentage convergence.
- Distinguish client disconnection from trainer failure; reject obsolete responses after changing runs.
- Measure overhead against telemetry-disabled training; keep serialization, network I/O and dashboard locks out of traversal hot paths.

**Boundary:** No training controls or live mutable-regret inspection. Show checkpoint and averaging fields as unavailable until their milestones are implemented; never invent values to fill the UI.

## 6. Persistence and resume

**Scope:** Versioned checkpoint format, RNG and schedule state, model/configuration binding, atomic publication, current-policy snapshot/export and reader. Keep checkpoints and inspection/policy exports distinct.

**Acceptance:**
- A resumed reference-mode run matches an uninterrupted run under a controlled iteration-based schedule and the same configuration.
- Reject incompatible model/configuration identities and truncated or corrupt artifacts.
- Preserve the last durable checkpoint until the next one is fully committed.
- Publish immutable current-policy snapshots for inspection, including policy kind, history schema and explicit coverage/fallback semantics.
- Exercise future schedule serialization with deterministic fixtures; real wall-clock timing is not a promise of bitwise-identical results across machines.

**Boundary:** A current-policy export is an explicitly labelled inspection/test artifact, not the later averaged production blueprint. Extend persisted state when milestone 8 introduces averaging and refinements.

## 7. Snapshot-based tree explorer

**Scope:** Snapshot-pinned API and UI, ancestor/child navigation, public game states, private-class selection, action probabilities, pagination, deep links, and explicit fallback/error states.

**Acceptance:**
- Inspect an actual exported current policy across all streets without accessing mutable trainer tables.
- Retain the selected snapshot when new snapshots appear.
- Reset or recompute private classes on actor/street changes.
- Distinguish uniform initialization, legal zero-probability actions, unavailable diagnostics and storage errors.
- Keep requests and memory bounded for large trees.

**Boundary:** Whole-range aggregation and EV estimation require additional data and are not implied by per-class browsing. Optional board/hand scenario resolution can follow this milestone using BucketProvider; it does not depend on systematic evaluation in milestone 10.

**Integration checkpoint:** A reference trainer with resume, progress monitoring and tree inspection now works end to end. This is not yet the completed averaged production training pipeline.

## 8. Strategy averaging and training refinements

**Scope:** Production preflop averaging, postflop snapshot averaging, warm-up, discounting and negative-regret pruning. Extend checkpoints, exports, metrics and the browser's policy-kind labels.

Implement and review these as separate increments: averaging first, discounting next, pruning next, and their interaction last. Keep refinements individually configurable.

**Acceptance:**
- Compare averaging with the small-game oracle from milestone 4.
- Include uniform initial policies for states absent from earlier snapshots; never average raw regrets in place of policies.
- Test warm-up boundaries, scheduled operations, negative floors, zero-regret cases and pruning exceptions.
- Verify full traversals allow pruned actions to return.
- Verify resume preserves counters, averaging state, snapshot count and schedule state.
- Export averaged production policies only after valid averaging data exists; test shortened schedules explicitly rather than waiting hundreds of minutes in CI.
- Compare variants with the reference using fixed evaluation protocols; do not infer improvement solely from speed.

## 9. Scaling and parallelization

**Scope:** Profile first, then optimize memory layout, model lookup and caches. Add parallel traversals with explicit update semantics and consistent snapshot generation. Retain the reference mode.

**Acceptance:**
- Measure throughput, peak RAM, telemetry overhead, snapshot I/O and training pauses separately.
- Check synchronization and data-race safety, including allocation and snapshot publication.
- Compare numerical behavior and evaluation results with the reference. Document batch updates as an algorithmic variant where applicable.
- Enforce memory limits without silently dropping information sets.

**Boundary:** Compact numeric types, quantization and parallelism are separate changes with separate error/performance measurements. Bitwise equality with sequential online updates is not an appropriate acceptance criterion for a different batch algorithm.

## 10. Operational readiness and systematic evaluation

**Scope:** Reproducible training profiles, startup/resume instructions, resource and storage budgets, retention behavior, and systematic matches with seat rotation and paired deals. Add container/deployment packaging if selected for the initial deployment.

**Acceptance:**
- Start, monitor, interrupt and resume a representative run using documented procedures.
- Test the complete path from validated models through training, checkpoint, policy export and dashboard inspection.
- Report evaluation against frozen baselines and older policies with sample counts, units and confidence intervals based on independent deal groups.
- Document remaining resource limits and the configuration needed for a full six-player 100-BB campaign.

**Boundary:** A full training campaign follows readiness checks and available compute budgets. Record its artifacts and empirical results separately; completing this milestone does not guarantee a specific strategy strength or require a long campaign in a PR.

## Cross-cutting validation and PR practice

Each PR should state its milestone, the behavior it adds, validation performed, and remaining limitations. Prefer focused PRs with reviewable tests over one PR per milestone. Documentation and contracts must describe the behavior actually available at each stage.

Maintain these regression checks as implementation grows:
- Card identity and model compatibility.
- Poker legality and chip conservation.
- Hidden-information boundaries and action ordering.
- Reference regret updates and averaging.
- Checkpoint integrity and deterministic reference resume.
- Snapshot identity, stale data and explicit fallback semantics.
- Bounded resource use and independently measured performance.

The technical and dashboard specifications remain the detailed contracts. Update their affected sections when decisions change, while keeping delivery order and milestone numbering authoritative here.
