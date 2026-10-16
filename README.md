# Pluribus blueprint

An offline blueprint-strategy trainer for six-player no-limit Texas hold'em, inspired by Brown and Sandholm's [Pluribus paper](https://noambrown.com/papers/19-Science-Superhuman.pdf) and [supplement](https://noambrown.com/papers/19-Science-Superhuman_Supp.pdf).

**Status:** specification and project setup. The trainer, API, and dashboard are not implemented yet; there are no build or run commands at this stage.

## Planned scope

- C++20 / CMake poker engine and external-sampling MCCFR trainer, with checkpoints and policy export.
- Import of complete flop, turn, and river models with 200 buckets each from [pluribus-information-abstraction](https://github.com/MichaelUnknown/pluribus-information-abstraction), plus 169 lossless preflop classes.
- Vue 3 / TypeScript dashboard for training progress and browsing policies at immutable game-tree snapshots.

This project computes a blueprint. Real-time search and off-tree action translation are separate future work; identical reproduction of Pluribus or a particular playing strength is not guaranteed.

## Documentation

- [Blueprint implementation plan and input formats](docs/blueprint-implementation.md)
- [Preliminary dashboard specification](docs/blueprint-dashboard-spec.md)

Proposed commands, APIs, and formats in these documents are design targets, not implemented features. The dashboard mockup referenced in its specification is a separate design preview, not included in this repository.

## Implementation sequence

1. Project foundation, model import, and preflop classes.
2. Complete poker engine and card sampling.
3. Action abstraction and information-set indexing.
4. Single-threaded MCCFR reference trainer.
5. Strategy averaging, snapshots, export, and resume.
6. Discounting and negative-regret pruning.
7. Memory optimization and parallelization.
8. Independent evaluation and full 100-BB training.

Dashboard contracts can start alongside steps 1–3, live telemetry after step 4, and snapshot-based policy browsing after step 5.

## Development

Planned implementation lives in `backend/` and `frontend/`; specifications live in `docs/`. Follow the root and directory-specific `AGENTS.md` instructions and submit focused pull requests. Build, test, and deployment instructions will be added with their implementations.
