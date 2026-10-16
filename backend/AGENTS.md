# Backend instructions

These instructions apply to `backend/` and supplement the repository-wide `AGENTS.md`.

## Stack and structure
- Use C++20 and CMake. No `backend/CMakeLists.txt` exists yet; introduce build targets with the first implementation and document working build/test commands.
- Keep public interfaces in `include/<libname>/`, implementations in `src/`, API implementation in `src/api/`, and tests in `tests/`.
- Keep the poker engine and trainer usable without the HTTP service or dashboard.
- Prefer the C++ standard library and standard algorithms when they provide an appropriate solution.

## Style and design
- Apply the root `.clang-format` configuration (the project's actual style takes precedence over generic Google-style defaults).
- Match existing naming conventions, ownership models, and error-handling patterns.
- Prefer RAII, strong types, const correctness, and clear lifetime/ownership semantics.
- Avoid unnecessary allocations, copies, temporary containers, and repeated work in hot paths.
- Prefer understandable, measured performance improvements over speculative micro-optimizations.

## Poker and training correctness
- Keep concrete simulation state separate from information-set keys. A policy must not depend on opponents' hidden cards or unrevealed board cards.
- Draw cards from one shared deck without replacement; use the correct model for each street. Bucket IDs are model-specific labels, not strength rankings.
- Validate artifact versions, dimensions, checksums, coverage, and model/configuration identities before training. Missing or corrupt data is an error, not an arbitrary bucket or policy fallback.
- Retain a deterministic, seeded, single-threaded reference mode. Validate regret updates and strategy averaging on small independently checkable games before optimizing.
- Test poker invariants and edge cases: chip conservation, side pots, short all-ins, reopening, action order, and ties. Test suit canonicalization and card/round validation independently.
- Preserve negative regrets for regret matching; do not silently substitute CFR+ or change sampling/weighting rules.
- Treat checkpoint/resume and policy export as different contracts. Persist enough state for reproducible reference-mode resume, including RNG and training schedule state.
- Introduce parallelism with explicit synchronization and numerical semantics; never allow C++ data races in regret updates or allocation.

## Dashboard boundary and validation
- Publish bounded, periodically aggregated telemetry; avoid per-node serialization, logging, network I/O, or dashboard locks in traversal hot paths.
- Serve tree inspection from committed immutable snapshots. Browsing must not allocate training nodes or read inconsistent mutable regret tables.
- Keep snapshot identity and policy kind explicit. Distinguish documented uniform initialization from missing/corrupt storage.
- Add targeted tests and benchmarks for changed behavior. Once CMake/CTest targets exist, use their documented commands and report unavailable checks. Do not use full training runs as routine unit tests.
