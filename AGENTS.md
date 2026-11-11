# AGENTS.md — Brezel

## Project Overview
Header-only C++ application framework on top of the EnTT ECS. Written and checked as C++20 (`CMakeLists.txt:5` says
C++23, but the project that uses Brezel compiles it as C++20: use C++20 features only).
**Important:** `docs/Framework_Guide.md` is the reference for the API, its constraints and its known defects. It
describes the code as it is, with `file:line` cites.
**You MUST keep `docs/Framework_Guide.md` updated whenever you make architectural or API changes.** Every public header
has an entry in its header index (section 13).

## Build & Run
- Normal use: as a subdirectory of the parent CMake project, where Brezel is an `INTERFACE` library. The parent defines
  the `spdlog` and `tinyxml2` targets and adds EnTT's include path. `README.md` has the snippet.
- Standalone testbed (top-level build into `build/`):
  ```bash
  git submodule update --init --recursive
  cmake -S . -B build -G Ninja && cmake --build build
  ./build/Brezel
  ```
  It does not build today: `src/main.cpp:2` includes `Brezel/Command/ValueChangeCommand.hpp`, which was never
  committed.
- Dependencies are git submodules in `extern/`: EnTT, spdlog, tinyxml2. GNU readline (system) is needed only by
  `Console/InteractiveConsole.hpp` and the testbed.
- New `.cpp` files in `src/` are picked up by globbing. As a subdirectory, any `.cpp` other than `src/main.cpp` turns
  Brezel into a static library (`CMakeLists.txt:34-41`).

## Codebase Map
- `include/Brezel/` — the framework headers (header-only).
- `src/main.cpp` — the standalone testbed (does not compile, see above).
- `docs/Framework_Guide.md` — the API reference.
- No tests in Brezel yet: the primitives are tested through the tests of the project that uses Brezel.

## Gotchas & Conventions
- **Domain-agnostic**: no application concept (machines, devices, venues, users of the app) in Brezel code or the
  guide. An application's thread rules live in the application.
- **No Parameters**: `Parameter<T>` has been completely purged. Components must remain pure POD structs.
- `Application` is a plain namespace with global state (`App/Application.hpp:14`), not a class.
- `EntityReference` stores both a UUID and a live `Entity` handle.
- Seven headers compile only after `Brezel/Core/ComponentRegistry.hpp` is included (guide section 13).
- `#pragma once` in all headers. No `.clang-format`, no linter.

## Agent Constraints
- **16GB RAM Constraint:** DO NOT use the `task` or `subagent` tools. You must perform all file edits and terminal commands yourself in this single session.
- Break large tasks into smaller conversational steps.
- When a change is agreed upon, implement it immediately.
