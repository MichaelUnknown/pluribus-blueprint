# Frontend instructions

These instructions apply to `frontend/` and supplement the repository-wide `AGENTS.md`.

## Stack and conventions

- Use Vue 3 Composition API, TypeScript, and Vite; prefer `<script setup lang="ts">` for new Vue single-file components.
- The frontend is not scaffolded yet. Once introduced, prefer Vue, built-in browser APIs, and existing utilities before additional libraries.
- Keep components focused and reuse existing components, composables, API helpers, and chart utilities.
- Follow existing component naming and file organization; use PascalCase for new Vue component filenames.
- Type props, emits, and API data; avoid `any` and avoid suppressing TypeScript errors without a documented reason.

## Reactivity and data flow

- Keep purely local state in components and share reusable reactive behavior through composables.
- Prefer `computed` for derived values; use watchers only for real side effects.
- Keep data fetching and API transformations outside presentation components when practical.
- Avoid duplicating backend-owned poker rules, bucket lookup, or probability calculations.
- Handle loading, failure, empty, disconnected, and stale-data states explicitly. A disconnected client does not imply a failed training run.
- Preserve large IDs and counters as decimal strings rather than coercing them to JavaScript Number.
- Cancel obsolete requests and reject late responses after run, snapshot, or node selection changes.

## Dashboard semantics

- Follow `../docs/blueprint-dashboard-spec.md`. The initial dashboard is read-only; do not add training controls without an explicit scope change.
- Keep live telemetry separate from snapshot-pinned strategy inspection. Never silently switch the inspected snapshot when a newer one arrives.
- Load tree children on demand with bounded pagination; do not load an entire tree, blueprint, or model into the browser.
- Reset or recompute private-class selections when the actor or street changes. Bucket labels are not hand-strength ranks or learned ranges.
- Distinguish current policies, averaged policies, uniform initialization, zero-probability actions, and unavailable data. Never fabricate EV/reach values or convergence percentages.
- Provide keyboard-accessible controls, visible focus, explicit status labels, and responsive layouts. Clearly label synthetic fixtures and mockup data.

## Formatting and validation

- Follow `frontend/.prettierrc.json` (single quotes, no semicolons, 100-character print width). Do not assume ESLint is configured.
- No package manifest, lockfile, or npm scripts exist yet. Introduce and document them with the frontend scaffold rather than claiming the commands below already work.
- Once a package lock exists, use `npm ci` when dependencies need installation.
- Establish scripts for formatting checks, TypeScript checking/production build, Vitest unit tests, and Playwright end-to-end tests. Run the actual scripts defined in `package.json` for the affected scope; report missing scripts or browser dependencies.
- Test snapshot pinning, stale/reconnect states, late-response rejection, tree navigation, and class/actor transitions as they are implemented.
- Avoid formatting unrelated files or editing generated build artifacts.
