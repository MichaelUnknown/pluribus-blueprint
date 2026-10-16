# Repository instructions

These instructions apply throughout the repository. More specific `AGENTS.md` files in subdirectories supplement these rules.

## Workflow and pull requests
- **Always make repository changes through a pull request.** Create a dedicated feature branch from the current default branch; never commit changes directly to `main`.
- Keep each PR focused and limited to the requested scope. Do not make unrelated refactors, formatting-only changes, or dependency upgrades.
- Review the diff before opening the PR and clearly summarize changed files, validation performed, and any limitations.
- Do not merge a PR unless explicitly requested.
- Preserve existing behavior and compatibility unless the task specifically requires otherwise.

## Project scope and structure
- This repository currently contains specifications and development conventions, not a runnable trainer or dashboard. Treat the stacks below as implementation targets until their build files exist.
- `backend/`: C++20 / CMake poker engine, blueprint trainer, artifact readers, and API.
- `frontend/`: Vue 3 / TypeScript / Vite dashboard for training progress and strategy inspection.
- `docs/`: implementation and interface specifications. Read `README.md`, `docs/blueprint-implementation.md`, and `docs/blueprint-dashboard-spec.md` before changing architecture or contracts.
- Docker images and deployment configuration are future work; do not assume they exist.
- The implementation document's producer source paths and pinned revision refer to the separate information-abstraction repository, not this repository.
- Distinguish published Pluribus details, proposed reconstruction choices, and implemented behavior. Do not describe proposed commands or APIs as available.

## Engineering practices
- Prefer clear, minimal changes and existing standard-library or framework features over new dependencies.
- Follow existing naming, file organization, and implementation patterns.
- Keep poker rules, legal actions, model lookup, and authoritative probability calculations in the backend.
- Treat API schemas and their OpenAPI documentation, once introduced, as a shared contract. Coordinate backend and frontend changes when modifying endpoints or response shapes.
- Version changes to rules, action abstraction, information keys, and artifact formats. Bind checkpoints and policies to the exact input models and configuration; do not silently reuse incompatible training state.
- Handle errors explicitly; avoid silent failures and misleading progress or status reporting.
- Do not hand-edit generated files, vendored dependencies, or build output.
- Never commit credentials or secrets. Do not expose secrets in client-side environment variables.
- Keep full models, feature datasets, training checkpoints, and generated policies out of Git. Use small deterministic fixtures for tests and document external data requirements.
- Add or update targeted tests for behavior changes, and run relevant checks when practical. Report checks not run rather than claiming success.
- Preserve applicable license notices and file-header conventions when reusing code; do not assume upstream code is unlicensed.

## Formatting and documentation
- Use the repository's `.clang-format` for C++ rather than relying on textual style guidance alone.
- Follow `frontend/.prettierrc.json` for frontend code.
- Keep repository documentation in English and update relevant specifications when contracts change.
- Format only the files touched by the task; avoid repository-wide formatting churn.
