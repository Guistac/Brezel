# AGENTS.md — Brezel

## Project Overview
C++23 realtime application framework using EnTT ECS.
**Important:** `docs/Framework_Guide.md` is the absolute source of truth for architecture, constraints, and UI integration. 
**You MUST keep `docs/Framework_Guide.md` updated whenever you make architectural or API changes.**

## Build & Run
```bash
cmake -S . -B . && make
./Brezel
```
- CMake outputs to root, not `build/`. New `.cpp` files in `src/` are auto-picked up via globbing.
- Dependencies (EnTT, spdlog, pugixml) are in `extern/` (git submodules).

## Codebase Map
- `include/Brezel/` — Framework headers. It is a strictly header-only library.
- `src/main.cpp` — Main entry point and component testbed.
- `docs/Framework_Guide.md` — The strict architectural rulebook.

## Gotchas & Conventions
- **No Parameters**: `Parameter<T>` has been completely purged. Components must remain pure POD structs.
- `Application` is an `inline namespace` with global state, not a class.
- `EntityReference` stores both a UUID and a live `Entity` handle.
- `#pragma once` in all headers. No `.clang-format`, no linter.

## Agent Constraints
- **16GB RAM Constraint:** DO NOT use the `task` or `subagent` tools. You must perform all file edits and terminal commands yourself in this single session.
- Break large tasks into smaller conversational steps.
- When a change is agreed upon, implement it immediately.
