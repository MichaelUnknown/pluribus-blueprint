# Technical specification: a Pluribus-style blueprint strategy

## 1. Goal, sources, and scope

This document specifies a **new offline training program** for six-player no-limit Texas hold'em. Its inputs are three finished, complete information abstractions with 200 buckets each for the flop, turn, and river. Its output is a persistent distribution over legal actions for every represented abstract information set. The producer program's `train` command in [pluribus-information-abstraction](https://github.com/MichaelUnknown/pluribus-information-abstraction) trains clusters, **not a poker strategy**. This repository does not yet implement a training command.

The existing data interface was checked against producer repository commit [`24615f8a018e68669c96fa60b2e0b03a95c11e64`](https://github.com/MichaelUnknown/pluribus-information-abstraction/commit/24615f8a018e68669c96fa60b2e0b03a95c11e64). Section 3 is a self-contained contract: an implementer needs the three binary models, but neither the producer repository's source code nor its running API server. Milestone 1 model import and milestone 2 poker rules are implemented as documented in the [model-import guide](model-import.md) and [poker-engine guide](poker-engine.md). Other trainer CLI names, export formats and architectural decisions below remain **proposals**.

Implementation order and milestone numbers are defined exclusively in the [implementation roadmap](implementation-roadmap.md). This document specifies the target technical behavior; its section numbers are not milestones.

Sources:

- [Brown/Sandholm, Science 2019, main article](https://noambrown.com/papers/19-Science-Superhuman.pdf), especially abstraction and self-play.
- [Supplement](https://noambrown.com/papers/19-Science-Superhuman_Supp.pdf), printed pages 12–18, especially Algorithm 1.
- Existing format contract: `backend/src/storage.cpp`, `storage_detail.hpp`, `catalogue.cpp`, `cards.cpp`, `features.cpp`, and `backend/CMakeLists.txt` in the producer repository at the commit above. These sources provide traceability; all details required for import are included below.

### Published parameters and limits of reproduction

The paper uses external-sampling MCCFR, 169 preflop classes, and 200 buckets for each subsequent round. Its training parameters are:

| Aspect | Published setting |
| --- | --- |
| Game | Six players, stacks of 10,000, blinds 50/100 |
| Discounting | Every 10 minutes up to 400 minutes: factor `d/(d+1)`, `d = minutes/10` |
| Pruning | After 200 minutes, in 95% of traversals; threshold −300,000,000 |
| Exceptions | Do not prune river actions or actions leading directly to terminal nodes |
| Regrets | 32-bit integers, floor of −310,000,000 |
| Preflop output | Average after 800 minutes; strategy update interval of 10,000 |
| Postflop output | Average of strategy snapshots taken every 200 minutes after 800 minutes |
| Turn/river raises | First raise: 0.5 pot, pot, all-in; subsequent raises: pot, all-in |

The complete preflop/flop bet-size tables are not published. The additions below therefore constitute a concrete reconstruction. A blueprint-only system is not the complete Pluribus agent with real-time search; six players and lossy abstraction do not provide a guarantee of Nash convergence or superhuman playing strength.

## 2. Components and responsibilities

| Component | Contract |
| --- | --- |
| `ModelReader` | Validates binary files once and provides immutable keys and bucket IDs |
| `BucketProvider` | `bucket(street, private_cards, revealed_board) -> uint8`; errors on a missing key |
| `PokerState` | Game rules, legal actions, contributions, pots, action order, payouts |
| `ActionAbstraction` | Deterministic, ordered list of concrete legal actions |
| `InformationIndex` | Public abstract action history plus the current private class |
| `Trainer` | Chance sampling, regret matching, MCCFR, scheduling |
| `StrategyStore` | Regrets, preflop counters, snapshot reading/writing, checkpoints |
| `PolicyReader` | Immutable exported action probabilities |
| `Evaluator` | Training diagnostics and separate matches against fixed opponents |

Implement a single-threaded reference mode first. A C++20 program fits the existing project, but the file interface is language-independent. Per-lookup REST calls or CLI processes do not belong in the training loop.

## 3. Data interface of the existing abstraction program

### 3.1 Required files and their meaning

Production use requires only `flop/model.bin`, `turn/model.bin`, and `river/model.bin`; their paths are configurable. Each model contains its catalogue and one stored bucket ID per key. Separate catalogues, feature shards, JSON manifests, and cluster centers are not needed for runtime lookup. Centers and diagnostics must nevertheless be correctly skipped or validated during import.

Bucket IDs are **labels**, not hand-strength rankings. The same number on two streets or from two training runs has no shared meaning. Replacing even one model invalidates the trained blueprint strategy unless an explicitly compatible relabeling has been established.

| Round in artifact | Board cards | Keys in complete catalogue | Sum of orbit weights | Feature dimension |
| --- | ---: | ---: | ---: | --- |
| 0 = Flop | 3 | 1,286,792 | 25,989,600 | `M`, default 50 |
| 1 = Turn | 4 | 13,960,050 | 305,377,800 | `M`, default 50 |
| 2 = River | 5 | 123,156,254 | 2,809,475,760 | 8 |

The round IDs are **not** the usual game street IDs 0=preflop, 1=flop, etc. Translate them explicitly in the adapter. All boards are unordered; in particular, the turn and river catalogues do not preserve reveal history. `complete=false` identifies pilot or partial models. These are suitable for import tests but must be rejected when starting production training. Even having 200 buckets does not establish complete coverage.

### 3.2 Primitives and envelope

`u8/u16/u32/u64` are unsigned integers of the corresponding bit widths. `f32/f64` are IEEE-754 binary32/binary64. All multibyte values are **little-endian**, with no padding, alignment, or implicit array lengths. A string is a `u32 byte_length` followed by exactly that many UTF-8 bytes, without a null terminator; the maximum is 1,024 bytes. The existing reader limits the total file size to 4 GiB.

| Byte offset | Type/length | Contents |
| --- | --- | --- |
| 0 | 8 bytes | ASCII `FLOPABS1`, including for version 2 |
| 8 | u32 | Kind: 1 catalogue, 2 feature metadata, 3 shard, 4 model |
| 12 | u32 | Version: 2 here; see below for legacy version 1 |
| 16 | u64 | Payload length `L` |
| 24 | u64 | FNV-1a-64 over only the `L` payload bytes |
| 32 | `L` bytes | Payload; EOF must follow immediately |

FNV-1a-64: start with `14695981039346656037`. For each byte `b`: `hash = ((hash XOR b) * 1099511628211) mod 2^64`. This is not a cryptographic integrity check. To bind the blueprint to its models, also store SHA-256 hashes of the **entire files** in the new manifest.

Every payload starts with these three strings, in this order:

1. `c6b6963670623e21166e185832dd3a775eb75b6d` (evaluator revision).
2. `suit24-private2-unordered-board-lex6-v2`.
3. `uniform-heads-up-half-ties-v2`.

The third string also applies to the river; do not replace it with the different human-readable `feature_model` from the JSON report. For compatible import, validate these values and do not silently accept unknown revisions.

### 3.3 Cards, canonical keys, and lookup

Card ID: `13*suit + rank`, with `suit = 0,1,2,3` for `c,d,h,s` and `rank = 0..12` for `23456789TJQKA`. Examples: `2c=0`, `Ac=12`, `2d=13`, `As=51`. Reject inputs containing duplicate cards or values outside 0..51.

For **each of the 24 joint suit permutations** of private and public cards:

1. Replace each card ID `c` with `13*permutation[c/13] + c%13` (integer division).
2. Sort the two private cards in ascending order; sort the board cards separately in ascending order.
3. Start with `key=0` and append the private cards first, then the board cards, using `key=(key<<6)|card`.
4. The numeric minimum of all 24 keys is the canonical key.

The first card value occupies the most significant used six-bit field. The flop requires 30 bits, the turn 36, and the river 42; the v2 file format always stores eight bytes. Do not sort all cards together, and never swap private cards with board cards. The orbit weight is the number of **distinct** keys produced this way, a positive divisor of 24.

Binary-search for the canonical key in the strictly ascending key array; a match at index `i` returns `assignments[i]`. If there is no match, report an error rather than using a random bucket, modulo mapping, or an unchecked nearest neighbor. Do not weight the result by the orbit weight.

Independently calculated test vectors (all with orbit weight 24):

| Hand | Board | Canonical key, decimal | Hex |
| --- | --- | ---: | --- |
| AcAd | Kh7s2d | 207882226 | `0x0c6407f2` |
| AcAd | Kh7s2d3c | 13304391666 | `0x31900e7f2` |
| AcAd | Kh7s2d3c4h | 851481053548 | `0xc64039c96c` |

The corresponding bucket IDs depend on the supplied model and cannot be specified in advance. Suit permutations, swapped hole cards, and permuted board order must each produce the same key and bucket ID.

### 3.4 Reused payload blocks (version 2)

`CatalogueData`:

| Order | Type | Contents |
| --- | --- | --- |
| 1 | u8 | Round 0/1/2 |
| 2 | u8 | Completeness flag, exactly 0 or 1 |
| 3 | u64 | Count `N` |
| 4 | `N` records | Each contains `u64 key`, `u8 orbit_weight`, **interleaved**, nine bytes per record |

`FeatureConfig` (29 bytes): `u8 round`, `u32 dimension`, `u32 completions`, `u32 opponent_hands`, `u64 legacy_seed`, `u64 grouping_hash`.

- Flop/turn: dimension 2..1000, completions 1081/46, opponent hands 990, grouping hash 0.
- River: dimension 8, completions 1, opponent hands 990; grouping hash as defined in Section 3.6.
- The writer sets `legacy_seed=1`; exact enumeration is seed-independent, and the reader ignores the value.

`TrainConfig` (31 bytes): `u32 buckets`, `u32 restarts`, `u32 max_updates`, `f64 relative_tolerance`, `u64 training_seed`, `u8 update`, `u8 weighting`, `u8 metric`.

- Version 2 requires exactly 200 buckets; restarts 1..1000, updates 1..100000, tolerance finite and in [0,1).
- Update: 0 median, 1 mean. Weighting: 0 canonical situations, 1 deals/orbit weights.
- Metric: 0 EMD, 1 squared Euclidean distance. Flop/turn require 0; river requires 1 and mean updates.

### 3.5 Complete model payload (kind 4)

After provenance, **without additional tags or block lengths**:

| Order | Type/count | Contents |
| --- | --- | --- |
| 1 | CatalogueData | Included situations |
| 2 | u64 | Fingerprint of the feature source catalogue |
| 3 | FeatureConfig | Feature definition |
| 4 | TrainConfig | Clustering configuration |
| 5 | u64 | Seed of the selected restart |
| 6 | `200 * dimension` f32 | Centers: all coordinates of bucket 0 first, then bucket 1, etc. |
| 7 | `N` u8 | Bucket IDs in the same order as the catalogue records |
| 8 | u64 | History length `H` |
| 9 | `H` records | Each contains `u32 restart`, `u32 iteration`, `u32 repairs`, `f64 objective_before_repair` (20 bytes) |
| 10 | 5 f64 | Objective, mean, median, p95, maximum, in that order |
| 11 | u64 | Occupancy length, must be 200 |
| 12 | 200 u64 | Number of canonical situations per bucket |
| 13 | 200 u64 | Weighted count per bucket |

There is **no second length field** before the weighted occupancies. These also count situations when `weighting=0`, and sum the stored orbit weights when `weighting=1`. IDs are in 0..199; 255 is not a valid bucket. Every bucket must be nonempty.

Flop/turn centers are nondecreasing CDFs in [0,1], with the last coordinate exactly 1. River centers contain eight independent equity values in [0,1], **not CDFs**. Reject NaN and infinity. History indices must lie within the configuration bounds; `H` is positive and at most `restarts*(max_updates+1)`. Diagnostic values are finite and nonnegative; median ≤ p95 ≤ maximum. Distances are bounded by 1 for EMD and sqrt(8) for the river. The river objective sums squared distances; its other distance statistics use the square root.

Import validation: check the envelope/checksum/EOF, provenance, round consistency, all dimensions and enums, sorted unique canonical keys, correct orbit weights, complete count/weight sum, IDs, and reconstructed occupancies. Check all multiplications and offset additions for overflow and against the remaining file length **before** allocating. After import, the trainer may retain only keys and assignments. The “nearest center” property cannot be verified from the model alone without feature files; the existing `validate --features ... --model ...` command can perform this additional check before delivery.

The source catalogue fingerprint is FNV-1a over its provenance plus CatalogueData, excluding the envelope. For partial models, it identifies the original complete source catalogue, not the embedded subset. Even a v2 model built from legacy features may have a v1 source catalogue fingerprint. Therefore, do not assume it equals the hash of the embedded v2 catalogue.

### 3.6 Other existing files: catalogue and features

These formats are included for auditing/further processing, not as additional trainer dependencies.

| Kind | Payload after provenance |
| --- | --- |
| 1 | CatalogueData |
| 2 | FeatureConfig, `u64 source_catalogue_hash`, `u32 shard_size` |
| 3 | FeatureConfig, `u64 source_catalogue_hash`, `u64 start_index`, `u64 record_count`, feature records |

Metadata is named `features.meta`. Shards are named `shard-NNNNNNNNNN.bin`, with a ten-digit, zero-padded decimal start index. `shard_size` is in 1..100000. Start indices are multiples of that size; a shard contains a full shard's worth of records or the remaining catalogue tail. Records refer to consecutive catalogue indices. Reject overlaps, incorrect filenames, hash/configuration mismatches, and missing coverage for complete training. A `.tmp` file is not a completed artifact; existing writers close files and rename them atomically but do not guarantee `fsync` protection against power loss.

Flop/turn record: `dimension` u16 histogram counters, summing to 1081 or 46, respectively. Each legal board completion produces a heads-up equity `e=(2W+T)/(2*990)` against uniformly distributed legal opponent hands. The bin is `min(M-1, floor(M*e))`; stored CDF coordinate `j` is computed as Float32 from `sum(count[0..j])/completions`. EMD is `sum(abs(cdfA[j]-cdfB[j]), j=0..M-2)/M`.

River record: 24 u16 values, arranged as `W1,T1,L1,W2,T2,L2,...,W8,T8,L8`. All counters together sum to 990; each group total is positive. Coordinate `g` is `(2Wg+Tg)/(2*(Wg+Tg+Lg))` as Float32. Groups use the following fixed 13×13 table; rows/columns represent ranks 2..A, the upper triangle is suited, the lower triangle offsuit, and the diagonal pairs. Labels are 1..8:

```text
4 1 1 1 1 1 2 2 2 4 4 4 6
1 6 1 1 1 1 2 2 3 4 4 6 6
1 1 6 1 1 2 2 2 3 3 4 4 6
1 1 1 6 3 3 3 3 3 3 4 4 6
1 1 1 1 7 3 3 3 3 4 5 6 6
1 1 1 2 3 7 3 3 5 5 5 6 7
1 1 2 2 3 3 8 3 5 5 5 6 7
2 2 2 2 3 3 3 8 5 5 5 7 7
2 2 2 2 3 3 3 5 8 5 7 7 7
2 2 4 4 4 4 5 5 5 8 7 7 7
4 4 4 4 4 4 5 5 5 5 8 7 7
4 4 4 6 6 6 6 6 7 7 7 8 7
6 6 6 6 6 6 6 7 7 7 7 7 8
```

`grouping_hash` is FNV-1a-64 over exactly these 169 **individual byte values** in row-major order, without spaces, newlines, or ASCII encoding of the digits. The grouping describes feature coordinates and is not a six-player opponent model. The trainer's showdown payoffs must be calculated from the cards actually dealt, never from these equity values.

### 3.7 Legacy version 1 and JSON

A minimal new trainer may explicitly support only v2 and reject v1 with a clear error message. If v1 is supported, all of the following differences apply together:

- Flop only, implicit round ID 0. Provenance strings two/three are `suit24-private2-public3-lex6-v1` and `uniform-heads-up-completed-board-half-ties-v1`; the evaluator revision remains the same.
- CatalogueData has no round byte; keys are u32 instead of u64 (five bytes per record).
- FeatureConfig has no round byte or grouping hash (20 bytes); 1081 completions and 990 opponent hands are still required.
- TrainConfig has no metric byte (30 bytes); EMD is implicit. Legacy files may contain fewer buckets; reject these for production use with 200 buckets.
- Other fields retain their order; array sizes follow the stored bucket count. The envelope is unchanged except for version 1.

`inspect` produces JSON diagnostics including `format_version`, `round`, `states`, `complete_catalogue`, `buckets`, `source_catalogue_fnv1a64`, `board_history=false`, and representatives. **It does not contain a complete key→bucket table** and does not replace the model file. Do not round UInt64 fingerprints in JSON through JavaScript Number; the new trainer should export hashes as hex strings.

## 4. Implement the game model

Implementation status: the milestone 2 reference engine implements this section using value copies, explicit street transitions and pure terminal settlement. Its exact rules/deal profiles, API, smoke command and validation are documented in the [poker-engine guide](poker-engine.md). There is no trainer yet.

### 4.1 Rules and state transitions

For the first version, use six fixed seats relative to the button, a starting stack of 10,000 integer chips, SB 50, BB 100, no antes, and no rake. Reset stacks for every training hand. Explicitly store: street, acting seat, button, remaining stacks, total and street contributions per seat, fold/all-in flags, current bet-to amount, last full raise size, and which players still need to respond or are allowed to raise again.

The engine needs `legal_actions`, `apply` with copies (or correct undo), `is_terminal`, `deal_next_street`, and `settle`. It must distinguish checks, folds, calls, full raises, and short all-in raises. A short all-in does not automatically reopen raising rights for players who have already acted. A round ends only when all players still able to act have fulfilled their obligations, not merely because contributions happen to be equal. Skip all-in players. If no decisions remain, complete the board and settle. Do not confuse heads-up action order after folds with the start of a new heads-up hand.

Build side pots from the distinct total contribution levels; folded players fund pots but are ineligible to win them. Return uncalled excess contributions. Evaluate the best five-card hand out of seven independently of the bucket class; split ties per pot. Implement a fixed odd-chip rule (first winner to the left of the button) and store it in the rules profile. Utility is `u_i = final_stack_i - starting_stack_i`, including blinds. Every terminal hand must satisfy `sum(u_i)=0`. A folded traverser can immediately return its loss once its final contribution is fixed.

### 4.2 Chance and private information

Deal all twelve hole cards and the board from **one shared deck without replacement**. Hidden cards of folded players remain removed from the deck. For a simple reference version, sample a uniform deck permutation per traversal and reveal cards as needed. The same runout may be used in counterfactual action branches; this correlation does not give the policy any additional information.

The simulator knows the entire deal, but the information function sees only the player's own hole cards, the currently revealed board, and public actions. Do not include opponents' cards, future board cards, or RNG states in the information key. Never sample buckets independently per player or street: that would lose card blockers and board correlations. Do not multiply regret updates by orbit weights again; uniform concrete deals already produce the correct frequencies.

## 5. Define action and information abstraction

### 5.1 Concrete initial proposal for bet sizes

The following **original, versioned starting configuration** makes the program implementable without unpublished tables. It is not a claim about the original Pluribus:

| Street | First bet/raise | Subsequent raises |
| --- | --- | --- |
| Preflop | 0.5; 0.75; 1; 1.5; 2; 3 pot; all-in | 0.5; 1; 2 pot; all-in |
| Flop | 0.33; 0.5; 0.75; 1; 1.5 pot; all-in | 0.5; 1 pot; all-in |
| Turn/river | 0.5; 1 pot; all-in | 1 pot; all-in |

Definitions: `P` is the total pot before the action, including all contributions; `c` is the full outstanding amount to call; `x` is the player's own street contribution. Pot factor `f` means raising **beyond the call** by `round_half_up(f*(P+c))`, giving a target contribution of `x+c+round_half_up(f*(P+c))`. Example: preflop UTG, pot 150, call 100, factor 1 yields a raise-to amount of 350. For reproducibility, store factors as rational numbers (`0.33=33/100`), avoiding platform-dependent floating-point rounding.

If the player cannot afford a full call, only fold/all-in call is available. Otherwise, clamp raise targets to the legal minimum and available stack; explicitly generate all-in when permitted. Deduplicate identical concrete targets after rounding/clipping. Order: fold when facing a bet, then check/call, then strictly ascending raise-to amounts. All-in must not appear twice. Do not impose an artificial raise-count limit in version 1; finite stacks bound the tree. Hash the bet-size configuration together with its rounding, legality, and deduplication rules.

### 5.2 Information key

Proposed profile with deliberate imperfect recall:

```text
InfoKey = (rules_id, action_abstraction_id, actor_relative_to_button,
           street, public_action_history_id, current_private_class)
```

The public history contains all previous abstract actions, concrete raise-to targets, and street transitions. It uniquely determines stacks, contributions, and active players; do not merge two histories merely because they have the same current pot size. The key contains neither raw board cards nor previous bucket IDs. This reduces private information to the **current** class. The simulator retains the actual reveal history even though BucketProvider discards it during lookup. A profile retaining previous buckets would define a different game and require new stores and measurements.

Create 169 lossless preflop classes, for example with a custom, fixed, documented numbering: pairs `id=rank` (0..12); for `low<high`, let `p=high*(high-1)/2+low`; suited `id=13+p`, offsuit `id=91+p`. This produces exactly 169 IDs; no existing artifact file supplies them. Postflop, the private class is the ID 0..199 from the corresponding model. Every shared information set must have the same ordered action list; assert this when inserting into the store.

## 6. Reference trainer: external-sampling MCCFR

For each information set `I` and action `a`, initialize cumulative regret `R[I,a]=0`. Regret matching:

```text
positive[a] = max(R[I,a], 0)
sigma[a] = positive[a] / sum(positive), if sum(positive)>0
sigma[a] = 1 / number_of_actions, otherwise
```

Execute a complete traversal with traverser `i`; cycle through the six seats as traversers. Initially use no pruning or discounting, Float64, and a deterministic seed:

```text
traverse(state, i):
    if terminal: return payoff(state, i)
    if i has folded and its loss is final: return -total_contribution[i]
    if chance: sample legal cards; return traverse(next_state, i)
    I = information_key(state, acting_player)
    sigma = regret_matching(R[I])       # unchanged local copy
    if acting_player != i:
        a ~ sigma
        return traverse(apply(state, a), i)
    for each legal abstract action a:
        value[a] = traverse(apply(state, a), i)
    value = sum(sigma[a] * value[a])
    for each action a:
        R[I,a] += value[a] - value
    return value
```

Use state copies or correct undo: no card, stack, or action-history leakage between sibling branches. When sampling according to the actual chance distribution and opponent policies, do not apply additional opponent-reach weighting to these regret differences. Exploration using a different sampling distribution requires a newly derived importance-sampling estimator; it is not a harmless sampler replacement.

Regret matching is not CFR+: negative regrets are retained. Start with Float64 to validate rules and estimators in isolation. Check against a full CFR reference on small perfect-recall games before connecting the intended poker abstraction.

### 6.1 Averaging and the exported strategy

Do not use visit frequencies from regret traversals as an average without validation. For the preflop profile, provide separate strategy accumulation: sample chance; at the player whose strategy is being collected, sample an action from its policy, increment that action's counter, and follow it; at other players, follow **all** legal actions. Stop at the end of preflop or when the player being collected folds. This incorporates that player's own reach probability into the samples. In the reference implementation, first compute the exact own-reach-weighted average on a small game as an oracle.

For the production profile, adopt the published warm-up and snapshot settings from Section 1 as a configurable schedule. Here, the separate preflop collector starts only after the warm-up boundary; previously collected test counters are discarded. Postflop snapshots store **normalized policies**, not raw regrets. Compute an arithmetic mean of snapshot policies; averaging regrets and only then normalizing would produce a different strategy.

Information sets that have not yet been allocated initially have zero regret and therefore a uniform policy. Snapshot averaging must account for that uniform policy in earlier snapshots where a later-created information set was still absent. Store the snapshot count globally; missing is not equivalent to a zero vector. If no valid samples/snapshots exist by export time, refuse production export; provide a separate current-policy export explicitly marked as a test export.

### 6.2 Add discounting and pruning

Implement scheduling based on monotonic **active training time**, excluding pauses and I/O interruptions while the trainer is stopped. For tests, configure the same mechanism with iteration boundaries; this is a documented variant. The checkpoint must include time boundaries, completed discount epochs, and snapshot deadlines. Delayed processing must neither skip due epochs nor apply them twice.

For pruning, sample one flag per traversal. Skip a traverser's action only if its regret is sufficiently negative, its policy probability is zero, it is not on the river, and it does not lead directly to a terminal node. Do not update regrets for skipped actions; compute the expected value from explored actions using the previously copied policy. As a safeguard for the special case “all regrets nonpositive, uniform policy,” explore all actions. The 5% full traversals must genuinely allow every traverser action so that actions can return.

The values in Section 1 depend on utility units, regret scaling, and the amount of training. If utility is expressed in big blinds, convert the threshold/floor accordingly (divide by 100 when BB=100); do not blindly reuse the same integer values. For a later integer optimization, document rounding, use at least int64 for intermediate calculations, and detect and handle positive overflow in a controlled way. The negative floor alone does not prevent positive overflow. Benchmark optimized variants against Float64.

## 7. Memory, parallelism, and persistence

The three key/assignment arrays together require approximately 1.25 GB decimal (`sum(N)*9`), excluding orbit weights and import buffers. The model also contains one orbit byte per key; measure peak import RAM separately. Memory mapping or streaming validation avoids unnecessary full copies. Start with binary search; add compact indexes and bounded caches only after profiling. With 24 suit permutations per lookup, caching per deal/player/street is a natural option.

Strategy tables dominate in the long run. Store public action sequences in a trie with stable IDs, then allocate contiguous blocks `[private_class][action]`. Allocate postflop blocks on first visit; preflop can be built upfront. For `S` visited sequences, 200 buckets, and average action count `A`, Float64 regrets alone cost approximately `S*200*A*8` bytes; one million sequences with A=4 require 6.4 GB. This is a budgeting example, not a size prediction. Budget separately for the tree structure, preflop counters, snapshots, and checkpoints. At the memory limit, checkpoint and stop cleanly rather than silently discarding states.

Parallelize only after reference validation passes. A safe first variant uses an immutable policy per mini-batch, workers with separate RNG streams and local regret deltas, and a deterministic merge at a barrier. This is a batch variant whose effect on learning behavior must be measured. Do not allow unprotected C++ writes to shared regrets; atomic individual updates alone do not make allocation and snapshot consistency correct. Perform discounting, snapshots, and checkpoints only at consistent boundaries. Keep the single-threaded mode as a reproducible oracle.

### 7.1 New checkpoint/export contract (to be implemented)

Define a separate versioned format before large training runs; **do not** reuse `FLOPABS1` for blueprint data. There are two distinct artifacts:

- **Checkpoint:** complete resumption state, including regrets, strategy counters, snapshot references/count, history index, iteration/traverser, active runtime, per-worker RNG states, discount/pruning status, and configuration.
- **Policy export:** only rules, action tree/order, model bindings, and normalized probabilities; do not promise resumption from this artifact.

Concrete initial portable proposal: a JSON manifest (`blueprint-checkpoint-v1` or `blueprint-policy-v1`) plus little-endian binary shards with their own lengths and SHA-256 hashes. The manifest contains the three model file hashes, source revision, rules schema, action schema, preflop numbering, information profile, utility unit, numeric types, seed/PRNG, training/export schedule, and shard list. Encode IDs and 64-bit counters in JSON as decimal strings. Binary policy records: `u64 history_id`, `u16 private_class`, `u16 action_count`, followed by `action_count` f32 values; the action list comes from the hash-bound history index. Order records lexicographically by `(history_id, private_class)`; prohibit duplicates. The history ID determines the street and actor. Missing records mean only the uniform initial policy defined in the manifest, not corrupted shards; therefore, store record coverage explicitly.

Before export, validate policies for finiteness, nonnegativity, and a positive sum; renormalize on reading after Float32 conversion. Add probability quantization only later, with an error budget. Checkpoint records can use the same key with an f64 regret vector; store preflop counters and RNG states in separately typed shards. Write and validate all shards completely, then atomically publish the manifest last. Synchronize files and the directory for crash durability. Resume must reject hash/profile mismatches and preserve the old checkpoint until the new commit is complete.

## 8. Delivery and validation

Follow the [ten-milestone implementation roadmap](implementation-roadmap.md) for sequencing, dependencies and acceptance gates. It supersedes the former nine-step table in this section.

The single-threaded reference and small-game averaging oracle arrive in milestone 4. Milestone 6 adds checkpoint/resume and explicitly labelled current-policy inspection exports; milestone 7 makes those snapshots browsable. Production averaging and training refinements arrive in milestone 8, followed by scaling in 9 and operational readiness in 10. A full 100-BB training campaign is an experiment after readiness, not a required CI run.

For model import, use small synthetic files with known bytes and producer-supplied golden models. Synthetic pilots test the parser but do not establish complete coverage. For MCCFR, use independent expected results rather than repeating the implementation under test.

Target CLI (`verify-models` is implemented; other commands below are not yet available):

```sh
blueprint verify-models --flop flop/model.bin --turn turn/model.bin --river river/model.bin
blueprint train --config blueprint.json --checkpoint-dir checkpoints --seed 42
blueprint resume --checkpoint checkpoints/latest/manifest.json
blueprint export --checkpoint checkpoints/latest/manifest.json --out policies/run-001
blueprint evaluate --policy policies/run-001 --opponents baselines.json --seed 123
```

Progress reporting should include at least completed traversals, nodes per second, visited information sets, RAM, pruning rate per street, checkpoint time, snapshot count, and active training time. Measure time only periodically or in batches; do not log per node.

Evaluate against multiple **frozen** baselines and older checkpoints, using seat rotation and paired deals. Report winnings in mbb/hand with confidence intervals derived from independent deal groups; do not count paired repetitions as independent hands. Self-play against the same policy alone cannot establish improvement. Exact exploitability is a testing tool for small two-player games, not a practical quality metric for the full six-player model. Do not equate full training with a promised runtime or win rate.

## 9. Definition of done

An initial complete blueprint trainer can independently import the three validated 200-bucket models, simulate legal 100-BB hands with six players, train deterministically in reference mode, resume correctly after interruption, and export a normalized policy that can be queried across all streets. Rules, abstractions, and model identities are immutably bound to the export. Tests and separate evaluation establish implementation properties; playing strength remains an empirical result. Real-time search, off-tree action translation, and a playable live agent are subsequent, separate projects.
