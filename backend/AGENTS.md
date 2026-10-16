# Backend instructions

These instructions apply to `backend/` and supplement the repository-wide `AGENTS.md`.

## Stack and structure
- Use C++20 and the existing CMake targets defined in `backend/CMakeLists.txt`.
- Keep public interfaces in `include/<libname>/`, implementations in `src/`, API implementation in `src/api/`, and tests in `tests/`.
- Prefer the C++ standard library and standard algorithms when they provide an appropriate solution.

## Style and design
- Apply the root `.clang-format` configuration (the project's actual style takes precedence over generic Google-style defaults).
- Match existing naming conventions, ownership models, and error-handling patterns.
- Prefer RAII, strong types, const correctness, and clear lifetime/ownership semantics.
- Avoid unnecessary allocations, copies, temporary containers, and repeated work in hot paths.
- Prefer understandable, measured performance improvements over speculative micro-optimizations.
