# Pluribus blueprint

An offline blueprint-strategy trainer for six-player no-limit Texas hold'em, inspired by Brown and Sandholm's [Pluribus paper](https://noambrown.com/papers/19-Science-Superhuman.pdf) and [supplement](https://noambrown.com/papers/19-Science-Superhuman_Supp.pdf).

**Status:** milestone 1 foundation implemented: C++20/CMake library, v2 model reader, card canonicalization, 169 preflop classes, model verification/query CLI and dataset-free tests. Complete external-model integration validation is still pending. The trainer, API and dashboard are not implemented yet.

## Planned scope

- C++20 / CMake poker engine and external-sampling MCCFR trainer, with checkpoints and policy export.
- Import of complete flop, turn, and river models with 200 buckets each from [pluribus-information-abstraction](https://github.com/MichaelUnknown/pluribus-information-abstraction), plus 169 lossless preflop classes.
- Vue 3 / TypeScript dashboard for training progress and browsing policies at immutable game-tree snapshots.

This project computes a blueprint. Real-time search and off-tree action translation are separate future work; identical reproduction of Pluribus or a particular playing strength is not guaranteed.

## Documentation

- [Ten-milestone implementation roadmap](docs/implementation-roadmap.md)
- [Blueprint technical specification and input formats](docs/blueprint-implementation.md)
- [Preliminary dashboard specification](docs/blueprint-dashboard-spec.md)

The [model-import guide](docs/model-import.md) documents implemented build, test, configuration and CLI commands. Other proposed trainer commands, APIs and formats remain design targets. The dashboard mockup referenced in its specification is a separate design preview, not included in this repository.

## Implementation sequence

The [implementation roadmap](docs/implementation-roadmap.md) is the authoritative sequence:

1. Project foundation and model import.
2. Poker engine.
3. Abstract game tree and storage contracts.
4. Reference MCCFR and initial evaluation.
5. Telemetry and progress dashboard.
6. Persistence and resume.
7. Snapshot-based tree explorer.
8. Strategy averaging and training refinements.
9. Scaling and parallelization.
10. Operational readiness and systematic evaluation.

Milestones may span multiple focused PRs. Dashboard contracts begin at milestone 3, live progress arrives in 5, and snapshot-based browsing in 7. Full training is a subsequent experimental campaign, not a routine implementation test.

## Development

Backend implementation lives in `backend/`; the planned dashboard lives in `frontend/`. Follow the root and directory-specific `AGENTS.md` instructions and submit focused pull requests.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

See the [model-import guide](docs/model-import.md) for dependencies and complete-model integration checks.
