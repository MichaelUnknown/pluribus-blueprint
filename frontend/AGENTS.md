# Frontend instructions

These instructions apply to `frontend/` and supplement the repository-wide `AGENTS.md`.

## Stack and conventions

- Use Vue 3 Composition API, TypeScript, and Vite; prefer `<script setup lang="ts">` for new Vue single-file components.
- Follow the current lightweight stack: Vue, built-in browser APIs, and existing utilities before introducing additional libraries.
- Keep components focused and reuse existing components, composables, API helpers, and chart utilities.
- Follow existing component naming and file organization; use PascalCase for new Vue component filenames.
- Type props, emits, and API data; avoid `any` and avoid suppressing TypeScript errors without a documented reason.

## Reactivity and data flow

- Keep purely local state in components and share reusable reactive behavior through composables.
- Prefer `computed` for derived values; use watchers only for real side effects.
- Keep data fetching and API transformations outside presentation components when practical.
- Avoid duplicating backend-owned calculations or scattering copies of server state across components.
- Handle loading, failure, empty, disconnected, and stale-data states explicitly.

## Formatting and validation

- Follow `frontend/.prettierrc.json` (single quotes, no semicolons, 100-character print width). Do not assume ESLint is configured.
- From `frontend/`, use `npm ci` when dependencies need installation.
- Run `npm run format:check` for formatting and `npm run build` for TypeScript checking and a Vite production build.
- Run `npm test` for Vitest unit tests; run `npm run test:e2e` for Playwright tests when applicable and the required environment is available.
- Avoid formatting unrelated files or editing generated build artifacts.
