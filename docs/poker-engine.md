# Poker engine (milestone 2)

The engine implements a single six-player no-limit Texas hold'em hand. It is
independent of model files, bucket labels, a trainer and HTTP/frontend runtimes.
The public API is `backend/include/blueprint/poker.hpp`; the original reference
hand evaluator is in `backend/include/blueprint/evaluator.hpp`. Both are part of
`blueprint_core`. Build dependencies and commands remain in the
[model-import guide](model-import.md).

## Run a complete hand

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
build/backend/blueprint simulate-hand --seed 42
```

`simulate-hand` is a deterministic check/call smoke driver, not a learned policy,
match evaluator or training command. It plays all four streets with default rules
and reports the profile IDs, seed, number of actions, contested pot, final stacks
and utilities. Seed 42 produces 24 actions, a 600-chip pot and utilities
`[-100, -100, -100, -100, -100, 500]` in physical seat order. It needs no models.

## Versioned rules and dealing

`poker_rules_id = nlhe-six-v1` defines integer chips, six occupied seats, no ante,
no rake, no artificial raise-count cap, unordered suit-independent hand strength,
and odd chips to tied winners clockwise starting left of the button. Defaults
are starting stacks of 10,000, blinds 50/100 and button seat 0. Seats are numbered
clockwise: with button 0, SB=1, BB=2, UTG=3, HJ=4 and CO=5. After folds, the
original button/action order remains in force; this is not a new heads-up hand.

`PokerRules` allows explicit stacks, blinds and button for fixed scenarios and
future bounded pilots. There must still be six positive stacks. Each starting
stack and the big blind is at most `INT64_MAX/12`; `0 < SB <= BB` and `button < 6`.
These bounds keep chip totals, raise arithmetic, side-pot multiplication and
payouts within signed 64-bit range. Persist the complete rules values alongside
the profile ID when checkpoint/policy binding is introduced. A new `PokerState`
starts a new hand with the configured stacks; stack carryover is not implicit.

`deal_profile_id = mt19937-64-fisher-yates-rejection-no-burn-v1` fixes seeded
reproducibility independently of a standard library's distribution/shuffle
implementation. Start with card IDs 0..51, seed `std::mt19937_64`, then Fisher–Yates
from index 51 down to 1. For bound `i+1`, reject draws below
`(uint64_t(0)-bound) % bound`, and use `draw % bound` as the swap index. All arithmetic
for this sampling step is unsigned 64-bit. This avoids modulo bias.

Deal two clockwise rounds beginning left of the button (deck indices 0..11),
then reserve indices 12..16 for the board in reveal order. No burn cards are
needed in this simulation profile. All twelve hole cards, including folded
players' cards, remain removed from this same deck. The explicit `Deck` constructor
validates all 52 cards and supports deterministic fixture/replay inputs.

## State, actions and transitions

`PublicPokerState` contains only public knowledge: rules, street, phase, acting
seat, stacks, total/street contributions, returned uncalled amounts, fold/all-in
flags, outstanding-response and raise permissions, current bet, last full raise
size and revealed board. Unrevealed board slots are `hidden_card` (255).
`pot()` sums contributions **after** returned excess. During a hand,
`sum(stacks) + pot() == sum(starting_stacks)`.

Use `legal_actions()` at a decision. Its domain comprises fold when facing a
wager, check with nothing to call, call (possibly short/all-in), and an optional
inclusive integer `RaiseRange`. An opening bet uses `Action::raise_to` too. Targets
are total **street** contributions, not added chips or total-hand contributions.
A full raise target is at least `current_bet + last_full_raise`; a player who
cannot reach it may raise only to their all-in target. No chip-by-chip action
vector is allocated. A short all-in call is `Action::call`, not a raise. This
engine deliberately excludes non-standard folds when checking is available.

`apply(actor, action)` validates the acting seat, phase, target and action kind
before changing the state. Invalid input throws `std::invalid_argument` and
leaves it unchanged. Blinds do not consume a voluntary action: the BB retains
its option after limpers. Short blinds contribute only actual chips, but the
nominal BB establishes the multiway preflop bring-in and minimum raise.

A full raise changes the minimum increment. A short all-in does not. Each player's
last acted-at bet amount is tracked separately: cumulative short all-ins reopen
that player's raising rights only when the amount newly faced reaches the last
full increment. A later caller may therefore stay closed while an earlier caller
is reopened. A checked player facing an opening all-in below a full minimum is
also closed until the cumulative amount reaches that minimum. These conventions
follow the no-limit reopening rule in [Poker TDA rules, rule 49](https://www.pokertda.com/view-poker-tda-rules/).
This engine is a precise simulation profile, not an implementation of live-table
speech, out-of-turn, misdeal or discretionary floor procedures.

Rounds end when all live players who can act have fulfilled their obligations,
including the BB's option. Folded/all-in seats are skipped. Once only one funded
player remains, they may still owe a call/fold decision, but cannot bet or raise
into an uncontested side pot. If no call remains, there is no artificial check.
At that point a nominal short-BB bring-in is reduced to actual posted/wagered
chips. Unmatched excess is returned at each round close (or fold win); this can
restore chips to a player who had shoved. Refunds are recorded separately.

There are three phases:

- `decision`: call `apply` with an action in the concrete legal domain.
- `chance`: call `deal_next_street()` to reveal flop, turn or river and reset
  street betting. All-in runouts continue through chance phases without decisions.
- `terminal`: only one player remains, or river betting has closed. Call `settle()`.

A typical driver is:

```cpp
blueprint::PokerState game(42);
while (!game.is_terminal()) {
    if (game.public_state().phase == blueprint::HandPhase::chance) {
        game.deal_next_street();
    } else {
        auto legal = game.legal_actions();
        game.apply(game.public_state().actor,
                   legal.check ? blueprint::Action::check() : blueprint::Action::call());
    }
}
auto result = game.settle();
```

Milestone 3 will select a limited ordered bet-size menu from this concrete domain
and add public action history IDs; neither is part of milestone 2.

## Hidden information and branch isolation

`PokerState` privately owns dealt hole cards, the reserved runout and per-player
betting bookkeeping. `observation(viewer)` returns a value with that player's
own two cards and the public state; it includes no opponents' cards, future board,
seed or RNG state. Trusted simulation code chooses the viewer. Future policy
interfaces must receive the acting player's observation, never the simulator
object or observations for arbitrary seats. This is a data boundary, not a
security sandbox for untrusted C++ code.

Use ordinary value copies for traversal branches. No mutable arrays, pointers,
RNG streams or runouts are shared between copies. Mutating one branch cannot
change another, and sibling branches retain the same sampled deal. Undo is not
implemented; copied states are the milestone 2 reference approach. Revealed board
history stays in order even though bucket lookup treats it as unordered.

## Evaluation and settlement

`evaluate_hand` validates 5, 6 or 7 distinct card IDs and ranks the best five. It
uses rank multiplicities and suit/rank masks, with no generated tables, third-party
code or bucket-equity approximation. Larger `HandValue` wins; equality is a tie.
Values encode category followed by descending category-specific kickers. Categories
are high card, pair, two pair, trips, straight, flush, full house, quads and straight
flush. The wheel is five high, and neither suit nor hole-card usage breaks a tie.

`settle()` is a pure, repeatable calculation, available only at terminal states.
It constructs layers at every distinct contribution level, includes folded money
and excludes folded players from eligibility. Layers with different eligibility
remain separate; every layer is split independently. Remainder chips go to tied
winners clockwise from the seat left of the button. A fold win needs no further
board revelation or evaluation. A contested showdown requires five board cards.

The result contains each layer's amount/eligibility/winners/payouts, gross winnings,
returned uncalled amounts, final stacks and `utility = final_stack - starting_stack`.
Refunds are already included in remaining stacks; they are not paid again.
`sum(utility) == 0`. Terminal public state retains committed contributions and
pre-award stacks for inspection; use `Settlement::final_stacks` for payouts.
Calling `settle()` again cannot pay the pot twice or modify the hand.

## Validation

The existing CI now runs six CTest cases in Debug, Release and ASan/UBSan builds,
without any dataset downloads. The added tests cover:

- All button positions, blinds, BB option, street order, minimum raises and short
  opening/calling/raising all-ins; cumulative versus individual reopening rights.
- Uncalled shoves, short blinds, sole remaining funded-player decisions, all-in
  runouts, fold wins, multiple side pots with different winners, folded best hands,
  ties and odd chips, including the largest supported chip amounts.
- Card/deck/rules/action errors with unchanged state, public/private observations,
  hidden future cards, copy isolation and the fixed seed-42 deck vector.
- 2,000 bounded legal random hands across all buttons and varied stack sizes,
  checking invariants after every transition and replaying identical actions.
- Evaluator examples and kicker comparisons; 10,000 deterministic deals at each
  of 5/6/7 cards against an independent sorted-multiplicity five-card oracle and
  exhaustive five-card subset selection.
- All 2,598,960 five-card combinations, checking the nine known category frequencies
  and the category-specific counts of the 7,462 distinct hand strengths.

These establish reference engine behavior. A model-driven trainer, performance
optimization, real-time solving and a match harness remain later milestones.
The milestone 1 full-model integration validation is still pending independently
of these dataset-free poker tests.
