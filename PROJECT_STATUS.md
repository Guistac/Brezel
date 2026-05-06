# Project Status & Roadmap — Brezel

## 1. Executive Summary
Brezel is a C++23 realtime application framework using EnTT ECS. It is currently in a "functional core" state where the underlying data model, reflection system, and CLI are operational, but advanced features like scripting, node graphs, and the realtime execution loop are yet to be implemented.

---

## 2. Current Implementation Status

### 2.1 Core Data Model (Implemented)
- **EnTT Integration**: Entities are `entt::entity` handles.
- **Hierarchy System**: `HierarchyComponent` manages parent-child relationships with path-based resolution.
- **Identity**: `IdentityComponent` provides unique names and UUIDs for every entity.
- **EntityReference**: Robust cross-referencing system using both UUIDs for persistence and raw handles for performance.

### 2.2 Hybrid Unified Reflection System (HURS) (Implemented)
- **Visitor Pattern**: Unified `reflect(ComponentVisitor&)` method in all components.
- **Property Tagging**: Metadata tags like `Tag::Persistent`, `Tag::CommandStack`, and `Tag::ReadOnly` drive system behavior.
- **Parameter<T>**: Typed property wrapper with `onChange` signals and metadata options.
- **Component Registry**: Runtime registration of components for discovery by XML/CLI/GUI systems.

### 2.3 Commands & Undo (Partial)
- **CommandStack**: Basic `execute()` and `undo()` functionality.
- **PropertyCommand**: Automatic undoable property changes via `Parameter<T>::set()`.
- **MISSING**: `redo()` functionality.

### 2.4 CLI & REPL (Implemented)
- **Interactive Console**: Readline-powered REPL with history and basic auto-completion.
- **Verbs**: `ls`, `tree`, `get`, `set`, `undo`, `save`, `load` are operational.
- **Path Resolution**: Global entity lookup via path strings (e.g., `set motor1.Motor.speed 5.0`).

### 2.5 Serialization (Implemented)
- **XML Project Files**: Recursive tree serialization using `pugixml`.
- **Handle Fixup**: Post-load pass to resolve `EntityReference` path strings back into live handles.

---

## 3. "Spec vs. Actual" Delta

| Feature | Spec (README.md) | Actual (Current Code) |
|---|---|---|
| **Memory** | `ObjectHandle<T>` | `EntityReference` (UUID-based) |
| **Strings** | Interned `StringID` | Standard `std::string` / raw `const char*` |
| **Scripting** | LuaJIT / sol2 integration | Not implemented |
| **Realtime** | Isolated RT Thread / VM | Not implemented |
| **Graph** | Node Graph / EnTT Nodes | Not implemented |
| **Undo** | Full Undo/Redo | Undo only (no Redo) |
| **GUI** | Remote GUI / ImGui | Headless / CLI only |

---

## 4. Roadmap

### Phase 1: Core Polish (Short Term)
1. **Redo Support**: Implement `CommandStack::redo()`.
2. **Type Support**: Expand `Parameter<T>` to fully support `int`, `bool`, and `Color`.
3. **StringID**: Implement the interned string system for O(1) property comparisons.
4. **Performance**: Replace `std::vector<Tag>` with a bitmask or fixed-size span for reflection tags.

### Phase 2: Scripting & Automation (Mid Term)
1. **Lua Binding**: Integrate `sol2` and bind the `Project` and `Entity` APIs.
2. **ScriptComponent**: Allow attaching Lua scripts to entities to handle events (`onTick`, `onParamChanged`).
3. **CLI Scripting**: Add an `exec` verb to run `.lua` batch files.

### Phase 3: Realtime Execution (Long Term)
1. **SharedBuffers**: Implement the atomic double-buffer for Main-to-RT communication.
2. **RT Thread**: Create the isolated high-priority loop.
3. **Graph Compiler**: Implement the topological sort and compilation of EnTT nodes into a flat RT execution array.

### Phase 4: UI & Ecosystem (Long Term)
1. **ImGui Inspector**: Integrated local debugger.
2. **Remote Protocol**: JSON-over-WebSocket protocol for browser-based editors.
3. **Node Editor**: Visual canvas for graph manipulation.

---

## 5. Development Principles
- **Conciseness over Boilerplate**: Use the reflection system to avoid repetitive code.
- **Realtime First**: Data structures should be designed with the RT/Main thread split in mind (no locks).
- **Tool-Friendly**: Everything must be accessible via CLI and XML to ensure the framework can run headless in CI/automation.
