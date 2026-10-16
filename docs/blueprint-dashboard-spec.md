# Blueprint dashboard — preliminary specification

Status: proposal v0.1, 16 October 2026. Companion to the [ten-milestone implementation roadmap](implementation-roadmap.md). UI language: English, matching the implementation document. The separate interactive mockup (not included in this repository) uses synthetic data only; it does not connect to a trainer.

## 1. Purpose and scope

Two primary workflows:

1. Observe a training run: determine whether it is advancing, resource constrained, checkpointing, disconnected, or failed.
2. Inspect a strategy: navigate an abstract action history and inspect the acting player's policy for a preflop class or postflop bucket at a fixed strategy snapshot.

MVP is read-only. Starting, pausing, stopping and reconfiguring training are deferred. Merely opening the dashboard must not change training or allocate new regret tables. Real-time solving, off-tree translation, opponent modelling and a full poker client are out of scope.

## 2. Navigation

Application shell: run selector, run status, connection freshness, Overview and Tree explorer tabs. Run identity is separate from selected snapshot identity: a running run may be inspected through an older immutable snapshot.

Overview shows telemetry for the selected run. Tree explorer pins a snapshot and never silently switches it when new snapshots arrive. Show a “New snapshot available” action instead. A deep link preserves run ID, snapshot ID, history ID, private class, and optional board/hand scenario.

## 3. Overview

| Element | Semantics |
| --- | --- |
| Run state | queued, running, checkpointing, paused, completed, failed; disconnected is a client connection state, not a trainer state |
| Budget | Active training time / configured time budget, or traversals / traversal budget; indicate which limit terminates a run when multiple limits exist |
| Progress | Budget consumption only, never percentage convergence; unbounded runs have no completion percentage or ETA |
| Work | Completed traversals, completed six-seat sweeps, visited nodes and allocated information sets, with explicit counter definitions |
| Throughput | Nodes/s and traversals/s over a labelled rolling window, e.g. 60 s |
| Resources | Trainer RSS, configured memory cap, worker count; optionally CPU utilisation with denominator stated |
| Schedule | Current discount/pruning phase, next snapshot and checkpoint; active training time versus elapsed wall time |
| Durability | Last successful checkpoint, its iteration and age; a pending checkpoint does not replace it |
| Diagnostics | Pruned eligible actions / eligible actions per street, full-traversal rate, model lookup errors |
| Events | Bounded, paginated event log with severity, timestamp and structured event type |

Display throughput and memory time series with explicit units and a selectable time window. Downsample in the backend and preserve extrema for resource-limit diagnostics. Missing samples appear as gaps, never zeros. Retain last values on disconnect and prominently mark their age. Do not infer training quality from declining regret totals, increasing node coverage or self-play payoff alone. An evaluation section can later show frozen-opponent results and confidence intervals with sample counts.

For the mockup: a 12-hour active-time budget, 4h 32m consumed, 37.8% budget usage. This is illustrative, not a forecast of a sufficient training budget.

## 4. Tree explorer

### 4.1 What is being browsed

The navigation tree represents public abstract action histories, not a fully enumerated concrete card tree. A node identifies street, acting seat and legal action list. For this proposed imperfect-recall profile, its policy row is selected by the acting player's current private class: 169 preflop classes or 200 postflop buckets. Earlier private classes are not part of the key.

A bucket is not a concrete hand, a strength rank, or an opponent range. Bucket 42 on the turn is unrelated to bucket 42 on the flop. Changes of actor or street must not carry an old private-class selection over as if it described the new player's cards. Default to an explicitly labelled class selection for the new actor, or recompute it from a valid concrete scenario.

### 4.2 Layout and interactions

- Top: immutable snapshot selector and policy kind (current regret-matched policy, averaged export, or snapshot ensemble). Only show kinds actually present in the artifact.
- Left: expandable history/path navigation. Breadcrumbs navigate to ancestors. Load children on demand; never render the entire tree. Distinguish decision, chance, terminal and unallocated-history nodes with text labels.
- Center: selected node's public state: actor, street, pot, amount to call, minimum raise-to, stacks, street/total contributions, folded/all-in players. All chip values share a unit toggle (chips or BB, with BB size displayed).
- Right/below: private-class selector; action probabilities and child navigation. Optional diagnostics show regret and sample count only when the selected artifact actually contains them.
- At chance boundaries: advance to the next abstract street. A separate optional scenario mode accepts board cards and one player's private cards and performs bucket lookup. No fabricated chance probabilities over buckets.

The local mockup offers a deliberately small but legal sequence: UTG acts with pot 150, raises to 350, HJ then folds, and CO is next to act. Pot remains 500 after HJ folds; HJ/CO each face 350 to call. SB/BB retain their blind contributions. Changing the private-class selector changes the synthetic policy without changing public state. Branches absent from the demo are explicitly disabled; this is not a complete tree simulator.

### 4.3 Policy semantics

Per action show label, concrete raise-to amount where applicable, probability, and status. Probabilities sum to one within floating-point tolerance. A legal zero-probability action remains visible and navigable. “Pruned during training”, “zero policy probability”, “unallocated” and “not present in this snapshot” are separate concepts.

Unallocated information sets may use the trainer's documented uniform initial policy, but label it “Uniform fallback — not trained in this snapshot”. A missing/corrupt shard or incompatible model is an error, never fallback. Null/unavailable EV or reach is shown as “Not available”; do not invent values from regret or bucket equity. If action EV is added later, include estimator, units, sample count and uncertainty.

Default action weights are conditional on the selected private class. A “whole-range” strategy requires an explicit weighting distribution (uniform combinations, supplied range or estimated reach) and must not be produced by averaging 200 buckets uniformly. In particular, bucket occupancy from information abstraction is not a learned range.

### 4.4 Scenario mode (optional follow-up to milestone 7)

Accept current street, board of exactly 3/4/5 cards and two private cards for a named player. Reject duplicates, malformed cards and incompatible round/model. Reuse the versioned BucketProvider. Display canonical key and bucket as optional diagnostics. Preserve actual reveal history in the scenario even though bucket lookup uses an unordered board.

A scenario illustrates an existing policy; it does not establish the probability of reaching that board, change the abstract action history, or solve a subgame. Do not expose sampled opponents' hidden cards as if they were observable policy input. If the actor changes and its private cards are unknown, ask for that actor's class/hand instead of reusing the previous actor's bucket.

## 5. Backend contract — proposed, not existing endpoints

Prefer a separate blueprint service, reusing Vue 3/TypeScript conventions from the abstraction dashboard where practical. This specification does not assume the existing clustering API already exposes strategy data. The poker engine and trainer remain authoritative for legal actions, state reconstruction and probabilities.

| Endpoint | Result |
| --- | --- |
| GET /api/blueprint/runs?cursor=… | Paginated runs and configuration summaries |
| GET /api/blueprint/runs/{run}/metrics?from=…&to=…&max_points=600 | Bounded time series and metric definitions |
| GET /api/blueprint/runs/{run}/events | SSE telemetry/events with monotonically increasing event ID; heartbeat |
| GET /api/blueprint/runs/{run}/snapshots?cursor=… | Immutable snapshot descriptors, kind, completion status and model/configuration hashes |
| GET /api/blueprint/snapshots/{snapshot}/nodes/{history} | Public state and node kind, bound to snapshot |
| GET /api/blueprint/snapshots/{snapshot}/nodes/{history}/children?cursor=…&limit=50 | Legal child actions and IDs; chance/terminal metadata |
| GET /api/blueprint/snapshots/{snapshot}/nodes/{history}/policy?private_class=… | Ordered probabilities, policy kind, provenance and availability/fallback status |
| POST /api/blueprint/snapshots/{snapshot}/resolve-cards | Read-only computation of canonical key and bucket for a validated scenario |

All IDs and counters wider than JavaScript's exact integer range are decimal strings. Timestamps are UTC ISO 8601, rendered in the user's zone. Durations use numeric seconds. Response includes schema_version, run_id, snapshot_id where applicable and source timestamp. Node IDs are opaque and stable within a versioned action/history schema, not pointers or mutable allocation offsets. Responses bind rule, action abstraction and three model hashes. Reject mismatches before browsing.

Policy response example:

```json
{
  "schema_version": 1,
  "snapshot_id": "snap-272",
  "history_id": "0",
  "actor": "UTG",
  "street": "preflop",
  "private_class": 12,
  "private_class_label": "AA",
  "policy_kind": "current",
  "availability": "stored",
  "actions": [
    {"id": "fold", "probability": 0.0},
    {"id": "call", "probability": 0.03},
    {"id": "raise-to-350", "probability": 0.72},
    {"id": "raise-to-650", "probability": 0.20},
    {"id": "all-in", "probability": 0.05}
  ]
}
```

Example data and action schema are illustrative; the production response must include every legal action from the run's bound action abstraction. HTTP 400 for invalid inputs, 404 for unknown run/node, 409 for incompatible snapshot/configuration, 410 for expired snapshot and 503 for temporarily unavailable storage. Include structured error codes and correlation IDs. A client may retain previously loaded immutable data after network failure, clearly marked offline.

## 6. Consistency and low overhead

Aggregate worker-local counters every configurable interval, default 1 second. UI refresh target 2 seconds. No per-node telemetry locks, strings, JSON serialization or network I/O in traversal hot paths. Export telemetry through a bounded queue; coalesce metric updates and count dropped samples, preserving error/checkpoint events in durable storage.

The tree API reads completed immutable snapshots through a separate reader. It must never traverse or lock mutable live regret tables. If current-policy inspection is desired, create an explicit consistent inspection snapshot at a training barrier and show its capture time. Keep telemetry live independently of snapshot age.

Snapshots become discoverable only after their manifest is atomically committed. Browser queries use bounded pagination, request cancellation and server-side limits; cancel stale requests on run/snapshot changes and reject late responses belonging to an old selection. Cache immutable responses by snapshot/hash and use a bounded server cache. Pin active snapshots or return an explicit expiry error if retention removes them. Record snapshot I/O and training pause time separately.

SSE reconnect uses Last-Event-ID; if replay history has expired, fetch a fresh summary and indicate a history gap. Polling fallback every 5 seconds. Mark telemetry stale after 10 seconds without a sample; a network disconnect must not be labelled a failed training run.

Provisional performance goals, to measure rather than assume: under 1% trainer throughput loss with telemetry enabled on a representative benchmark; p95 cached node/policy response under 250 ms; bounded page size regardless of total tree size. On a slow machine reduce refresh rate rather than blocking training. Benchmark snapshot generation separately.

## 7. Security and accessibility

Bind local deployments to loopback by default; remote use requires authentication, TLS and run-level authorization. Do not expose raw filesystem paths, credentials or arbitrary file download endpoints. Parameterize model/snapshot references by IDs. Read-only APIs remain bounded to prevent expensive broad scans. Future training control operations require separate permissions and confirmation for destructive actions.

Keyboard-accessible navigation and native buttons/selects; visible focus, text labels for status and action categories, colour-independent probability values. Responsive layout stacks panels; large tables paginate. Desktop target 1280 px and above, usable at 360 px. Preserve selections in browser history/deep links in the real product. Clearly label all mockup data as synthetic.

## 8. Delivery mapping

Milestone numbers refer to the [implementation roadmap](implementation-roadmap.md), the authoritative delivery sequence.

| Dashboard increment | Milestone | Deliverable |
| --- | --- | --- |
| Contracts and fixtures | 3 | Typed API contracts, synthetic fixtures and public-state navigation design |
| Read-only progress dashboard | 5, using training data from 4 | UI scaffold, live metrics/events, stale/reconnect behavior and overhead measurements |
| Persistence integration | 6 | Checkpoint status and immutable current-policy snapshot discovery |
| Snapshot tree explorer | 7, using artifacts from 6 | Per-class policies, lazy history navigation, deep links and fallback/error semantics |
| Optional scenario mode | Follow-up to 7 | Board/hand resolution through BucketProvider; no dependency on evaluation |
| Averaging and training phases | 8 | Policy-kind selection, warm-up/discount/pruning state and valid averaged exports |
| Scale and retention | 9 | Bounded caches, large-tree behavior and consistent snapshots under parallel training |
| Evaluation display | 10 | Optional presentation of frozen-opponent results and confidence intervals |

Avoid blocking backend implementation on frontend completion. Publish contracts and fixtures early, then replace mock adapters with the real service. Before a capability exists, show it as unavailable: milestone 5 does not imply checkpoint support, and milestone 7 does not imply an averaged production policy. No browser should need to load an entire model or blueprint.

## 9. Acceptance scenarios

1. Start a run, observe counters advancing; disconnect the stream and show stale data without changing the trainer state.
2. Resume a checkpoint: preserve cumulative counters and distinguish the new process session from the run identity.
3. Pin snapshot A; receive snapshot B; navigation and probabilities remain on A until explicitly switched.
4. Traverse a raise and folds: reconstruct correct pot, stacks, actor and action order; breadcrumbs restore the ancestor.
5. Change class: probabilities update but public state does not. Sum is one and legal zero-probability actions remain visible.
6. Browse an unallocated row: explicitly label uniform fallback. Missing shard produces an error instead.
7. Enter duplicate board/hand cards: reject with an actionable validation message. Change street: invalidate incompatible class/scenario selections.
8. Browse a large tree with bounded API responses and memory; navigation never allocates trainer nodes.
9. Verify telemetry overhead and checkpoint consistency independently; test keyboard navigation and narrow layouts.

## 10. Open decisions for implementation

- Whether the blueprint service shares deployment/authentication with the abstraction dashboard or runs independently. Proposed MVP: separate service, same frontend conventions.
- Whether inspection snapshots are also training snapshots or a lighter derivative export; preserve the same identity/consistency contract either way.
- Which policy kinds are exported at each stage; do not promise diagnostics that cannot be recovered from a policy-only export.
- Retention and storage budget for snapshots, especially when browsing pins old versions.

The mockup demonstrates Overview/Tree navigation, snapshot selection, private-class changes and a short action path. Charts, counters and probabilities are fixtures. Control of a real training process and complete game-tree generation are intentionally not implemented in the mockup.
