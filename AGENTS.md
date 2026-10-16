# Repository instructions

These instructions apply throughout the repository. More specific `AGENTS.md` files in subdirectories supplement these rules.

## Workflow and pull requests
- **Always make repository changes through a pull request.** Create a dedicated feature branch from the current default branch; never commit changes directly to `main`.
- Keep each PR focused and limited to the requested scope. Do not make unrelated refactors, formatting-only changes, or dependency upgrades.
- Review the diff before opening the PR and clearly summarize changed files, validation performed, and any limitations.
- Preserve existing behavior and compatibility unless the task specifically requires otherwise.

## Project structure
- `backend/`: C++20 / CMake application
- `frontend/`: Vue 3 / TypeScript / Vite dashboard.
- `Dockerfile` and `docker/`: separate API/frontend images and Nginx configuration.
- `docs/`: project documentation.
- Read the relevant README and existing code before changing interfaces or architecture.

## Engineering practices
- Prefer clear, minimal changes and existing standard-library or framework features over new dependencies.
- Follow existing naming, file organization, and implementation patterns.
- Keep business logic and authoritative calculations in the backend; the frontend should present API results.
- Treat the backend API and its OpenAPI/Swagger documentation as a shared contract. Coordinate backend and frontend changes when modifying endpoints or response shapes.
- Handle errors explicitly; avoid silent failures and misleading progress or status reporting.
- Do not hand-edit generated files, vendored dependencies, or build output.
- Never commit credentials or secrets. Do not expose secrets in client-side environment variables.
- Add or update targeted tests for behavior changes, and run relevant checks when practical. Report checks not run rather than claiming success.
- Respect the existing license and file-header conventions.

## Formatting
- Use the repository's `.clang-format` for C++ rather than relying on textual style guidance alone.
- Follow `frontend/.prettierrc.json` for frontend code.
- Format only the files touched by the task; avoid repository-wide formatting churn.
