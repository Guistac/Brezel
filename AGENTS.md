# AGENTS.md — Brezel

## Project Overview
C++23 realtime application framework using EnTT ECS. README.md is a full architecture spec; the code is a partial implementation. Trust the code over the spec.

## Build
```bash
git submodule update --init --recursive   # first clone only
cmake -S . -B . && make                    # configure + build
```
- In-source build (CMake outputs to root, not `build/`)
- `file(GLOB_RECURSE)` in CMakeLists.txt — new `.cpp` files in `src/` auto-pick up
- Links: EnTT, spdlog, pugixml, readline (system)
- `compile_commands.json` is generated — clangd works

## Source Layout
- `include/framework/` — all headers, zero `.cpp` files (header-only framework)
- `src/main.cpp` — only cpp file; also contains demo components (Motor, TestComponent)
- `extern/` — git submodules (entt, spdlog, pugixml) — ignore these

## Architecture (Actual, Not Spec)
- **EnTT ECS** is the data model, NOT an Object tree with `unique_ptr` children. Entities are `entt::entity` handles; hierarchy lives in `HierarchyComponent`.
- **Visitor-based reflection**: every component implements `reflect(ComponentVisitor&)`. One reflect drives serialization, undo, CLI, and debug via tag filtering.
- **Tag system**: `Tag::Persistent` (XML), `Tag::CommandStack` (undo), `Tag::Hidden`, `Tag::ReadOnly`, `Tag::Networked`, etc.
- Components must derive from `BaseComponent` and be registered: `ComponentRegistry::registerComponent<T>("Name")`. The name becomes the XML element tag.

## Not Yet Implemented (per README spec)
Lua scripting, node graphs, realtime thread, SharedBuffers, remote GUI, ImGui, `ObjectHandle<T>`, `StringID`, custom Logger (spdlog used instead), allocators.

## Gotchas
- `Parameter<T>::toString()/fromString()` only implemented for `float` and `std::string`. Int/bool will silently break.
- `CommandStack::redo()` does not exist yet.
- `Application` is an `inline namespace` with global state, not a class.
- `Parameter<T>::set()` is defined in `PropertyCommand.hpp` (circular dep break). If you touch Parameter, check PropertyCommand.
- Everything lives in the global namespace except `Xml::`, `CLI::`, `Application::`.
- No tests, no CI, no `.clang-format`, no linter.

## Conventions
- `#pragma once` in all headers
- `EntityReference` stores both a UUID and a live `Entity` handle (not pure ID-based like the spec's `ObjectHandle`)
- `Project::m_name` is public (serializer writes to it directly)

# Agent Instructions
- You are an autonomous developer agent.
- You have full permission to edit files in this repository.
- NEVER tell the user you cannot access their filesystem.
- When a change is agreed upon, immediately use the `write_file` tool.

# 16GB RAM Constraints
- DO NOT use the `task` or `subagent` tools. 
- You must perform all file edits (write_file) and terminal commands yourself in this single session.
- If a task is too large, break it down into smaller steps within this chat.
