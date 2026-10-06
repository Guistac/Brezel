# Brezel Framework: Comprehensive Guide

Brezel is a C++23 Real-Time (RT) capable application framework built completely on top of the **EnTT** Entity Component System. It provides a robust, heavily decoupled architecture designed specifically to separate Non-Real-Time (NRT) systems (like UIs, networking, serialization, and scripting) from hard Real-Time execution loops (like 1kHz motion planning and EtherCAT fieldbus control).

---

## 1. Core Philosophy: The Air-Gap

The most critical architectural constraint in Brezel (and its host ecosystem, Stacato) is the strict separation between NRT and RT layers.

- **Non-Real-Time (NRT)**: UI (ImGui), Serialization, File I/O, Network, User Input. This layer interacts heavily with `entt::registry`, creates/destroys entities, pushes commands to the Undo/Redo stack, and dynamically accesses memory.
- **Real-Time (RT)**: High-frequency control loops. The RT loop **MUST NEVER** interact with `entt::registry`, allocate heap memory (no `new`, no `std::vector::push_back`), throw exceptions, or block on standard mutexes.

To achieve this, all domain data crossing the boundary must be strictly **Plain Old Data (POD)**.

---

## 2. Core ECS & Scene Graph

While EnTT provides a flat database of entities, Brezel layers a traditional Scene Graph and identity system on top using built-in components.

### 2.1 Identity & UUIDs
Every entity in Brezel must have an `IdentityComponent`.
```cpp
struct IdentityComponent {
    std::string name;
    std::string displayName;
    UUID uuid;
};
```
- `name`: A sanitized string (no spaces or slashes) used for CLI path resolution (e.g., `motor_1`).
- `uuid`: A 64-bit globally unique identifier used for persistent cross-referencing across save files and network sessions.

*Convenience Accessors:* `Brezel::Entity` provides `entity.getName()` and `entity.getUUID()` to query identity metadata directly without manual `try_get<IdentityComponent>()` lookups.

### 2.2 Hierarchy (Parent/Child)
Parent-child relationships are handled by the `HierarchyComponent`.
```cpp
struct HierarchyComponent {
    entt::entity parent = entt::null;
    std::vector<entt::entity> children;
};
```
*Note: Do not manipulate these components directly. Use `Entity::setParent(other)` to safely link entities.*

### 2.3 Safe Cross-Referencing (`EntityReference`)
Never store raw pointers or raw `entt::entity` handles in your components if they need to be serialized or passed between NRT systems. Use `EntityReference`.
```cpp
struct EntityReference {
    UUID uuid;
    Entity entity; 
};
```
An `EntityReference` stores the UUID persistently. When a project is loaded from XML, Brezel runs a post-load "fixup" pass that scans all `EntityReference`s, looks up their UUIDs in a map, and populates the live `Entity` handle.

---

## 3. Component Architecture (Pure PODs)

Components in Brezel must **NOT** inherit from any base classes and must **NOT** contain heavy heap-allocating wrappers or virtual tables. They are pure C++ structs.

```cpp
struct MotorConfig {
    float maxVelocity{100.0f};
    float maxAcceleration{50.0f};
    std::vector<float> lookupTable;
    EntityReference targetAxis;
};
```

---

## 4. The Reflection System

Instead of intrusive virtual methods, Brezel uses static free-functions for reflection. This allows the GUI, CLI, and Serializer to dynamically discover properties without polluting the POD structs, keeping them lightweight and RT-safe.

### 4.1 The `reflect` Free-Function
For every component you create, you must provide a generic `reflect` function in the same namespace:

```cpp
template<typename V>
void reflect(MotorConfig& m, V& v) {
    // Basic types
    v.visit_property("maxVelocity", m.maxVelocity, {Tag::Persistent, Tag::CommandStack});
    v.visit_property("maxAcceleration", m.maxAcceleration, {Tag::Persistent, Tag::CommandStack});
    
    // Entity References
    v.visit_property("targetAxis", m.targetAxis, {Tag::Persistent});
    
    // Vectors require a VectorAccessor
    VectorAccessor va(m.lookupTable);
    v.visit_property("lookupTable", va, {Tag::Persistent});
}
```

### 4.2 Property Tags (`Tag::`)
Tags define how systems interact with a property:
- `Tag::Persistent`: The property will be written to and loaded from XML save files.
- `Tag::CommandStack`: Modifying this property via the UI or CLI automatically pushes an Undo/Redo command.
- `Tag::ReadOnly`: The UI should lock the field; it cannot be modified by the user.
- `Tag::Hidden`: The UI should not render this property at all.

### 4.3 Component Registration
Before a component can be used in the Editor or loaded from XML, it must be registered in the application startup phase. The string name provided becomes the XML element tag.
```cpp
ComponentRegistry::registerComponent<MotorConfig>("MotorConfig");
```

---

## 5. State Mutation & The Command Stack

Because NRT and RT layers are separated, you must be careful how you mutate data in the NRT layer to ensure the rest of the application (like the GUI or network listeners) reacts correctly.

### 5.1 Modifying Values (Undo/Redo)
When the UI modifies a component, use `ValueChangeCommand<T>` to push the change onto the Undo/Redo stack. This command operates non-intrusively on raw memory addresses.

```cpp
auto cmd = std::make_unique<ValueChangeCommand<float>>(
    &config.maxVelocity,   // Pointer to raw memory
    config.maxVelocity,    // Old value
    150.0f,                // New value
    "Set Max Velocity"     // Description
);
project.getStack().pushAndExecute(std::move(cmd));
```

### 5.2 EnTT Change Callbacks
EnTT native signals replace classic observer patterns (like `onChange`). When a component is modified, you must notify EnTT using `patch` so it can emit `on_update` signals to any NRT listeners (like network replicators or UI refresh triggers).

```cpp
// 1. Listen for updates (e.g. GUI refresh or Network Replication)
void onMotorUpdated(entt::registry& reg, entt::entity entity) {
    const auto& m = reg.get<MotorConfig>(entity);
    spdlog::info("Motor config changed! New velocity: {}", m.maxVelocity);
}
registry.on_update<MotorConfig>().connect<&onMotorUpdated>();

// 2. Notify EnTT after modification (e.g. after a ValueChangeCommand executes)
registry.patch<MotorConfig>(entity);
```

---

## 6. Serialization

Brezel currently uses `pugixml` for serialization. 
*(Note: A migration to `tinyxml2` is planned to ensure full compatibility with the existing Stacato backend.)*

### 6.1 Saving & Loading
The entire `Project` (which wraps the EnTT registry) can be saved and loaded with single function calls:
```cpp
Application::saveProject(&project, "stage_setup.xml");
Project* p = Application::loadProject("stage_setup.xml");
```

### 6.2 XML Structure
Because of the reflection system, the XML output is clean, semantic, and highly readable:
```xml
<Project Name="MainStage">
  <Entity Name="Winch1" DisplayName="Stage Left Winch" UUID="78453489">
    <MotorConfig>
      <maxVelocity val="100.0" />
      <maxAcceleration val="50.0" />
      <targetAxis UUID="12345678" /> <!-- Resolved post-load -->
    </MotorConfig>
  </Entity>
</Project>
```

---

## 7. Command Line Interface (CLI)

Brezel includes a robust `InteractiveConsole` that provides terminal access directly into the ECS property graph. This is invaluable for debugging headless servers or automating tests.

The CLI utilizes the reflection system via `SearchVisitor` to locate properties dynamically by string paths.

### 7.1 Path Resolution
Entities and properties can be addressed using dot/slash notation. 
Format: `EntityName.ComponentName.PropertyName`
Example: `Winch1.MotorConfig.maxVelocity`

### 7.2 Core Commands
- `ls`: List all top-level entities or children of an entity.
- `tree`: Print the entire hierarchy and component state recursively.
- `get <path>`: Read a property value. 
  - `get Winch1.MotorConfig.maxVelocity` -> `100.0`
- `set <path> <value>`: Write a property value (automatically pushes to the CommandStack for Undo/Redo).
  - `set Winch1.MotorConfig.maxVelocity 150.0`
- `undo` / `redo`: Step backwards or forwards through the command stack.
- `save <file>` / `load <file>`: Triggers project serialization.

---

## 8. Multi-threading & Synchronization

1. **The Writer (NRT)**: The UI and network threads execute commands on the `CommandStack`, mutate components, and trigger `registry.patch<T>()`.
2. **The Reader (RT)**: The RT thread reads the pure POD components via raw pointers securely passed to it (e.g., during an atomic synchronization phase, or via double-buffered lock-free queues), **entirely bypassing EnTT** during the RT cycle. 
3. **The Contract**: The NRT thread promises never to invalidate memory (e.g., destroying an entity or resizing a vector) while the RT thread is executing its 1ms cycle. Memory lifecycle changes are deferred until a safe synchronization point.

---

## 9. Framework Concurrency & Air-Gap Primitives

Brezel provides a suite of generic, stage-agnostic concurrency primitives in `include/Brezel/Concurrency/` and `include/Brezel/Core/`:

### 9.1 Lock-Free Bridges
- **`Brezel::TripleBuffer<T>`**: Lock-free, wait-free Simpson 3-slot asynchronous buffer for high-frequency producer/consumer communication (e.g. 1 kHz RT thread <-> NRT thread) without blocking or mutexes.
- **`Brezel::RingBuffer<T, Capacity>`**: Fixed-capacity lock-free SPSC circular FIFO for bounded streaming packets.

### 9.2 Periodic Loop (`Brezel::PeriodicLoop`)
High-precision dedicated background thread executor with drift correction and anti-windup clamping:
```cpp
Brezel::PeriodicLoop loop("WorkerThread");
loop.start(50.0, [](double dt) {
    // Executes at exactly 50 Hz with absolute deadline tracking
});
loop.stop();
```

### 9.3 Asynchronous Mutation Queue (`Brezel::MutationQueue<T>`)
Double-buffered MPSC queue for thread boundary crossing (e.g., UI/Scripts -> World Supervisor):
- Producers acquire a brief lock to push mutations into an ingestion buffer.
- The consumer drains by swapping the buffer in O(1) time, eliminating lock contention.
*(Differentiated from `CommandStack`: `CommandStack` manages polymorphic GUI Undo/Redo history, whereas `MutationQueue` carries discrete POD state mutations into the ECS).*

### 9.4 Type-Erased Mutation Envelope (`Brezel::AnyMutation`)
Bounded 256-byte stack envelope for open-ended mutation routing across subsystems without a centralized static variant:
- Subsystems across the repo define their own POD mutation structs locally (e.g., `MecanumConfigMutation`, `ObstacleMutation`).
- Enforces `sizeof(T) <= 256` and `std::is_trivially_copyable_v<T>` at compile time.
- Payloads exceeding 256 bytes must explicitly store large data in `std::shared_ptr`.
- When drained, `mutation.execute(dispatcher)` unpacks and dispatches the typed event to connected subsystem listeners with zero dynamic allocation.

### 9.5 Event Dispatcher Quarantine Wrapper (`Brezel::Dispatcher`)
An alias/wrapper over `entt::dispatcher` provided in `Brezel/Core/Dispatcher.hpp`. Allows host applications to utilize typed event pub/sub without directly including `<entt/entt.hpp>` in application code, preserving strict EnTT quarantine.

### 9.6 Entity Stream Component (`Brezel::StreamComponent<T, Tag = void>`)
Generic ECS component bridging an entity to a lock-free `TripleBuffer<T>` at a stable memory address (`Brezel/Concurrency/StreamComponent.hpp`):
- Resolves the EnTT "swap-and-pop" relocation danger by holding a `std::shared_ptr<TripleBuffer<T>>` or `TripleBuffer<T>*`, ensuring external producer/consumer worker threads read and write to fixed memory addresses while EnTT manages entity lifecycles.
- Works for any trivially copyable Plain Old Data (POD) payload in any direction (Inbound telemetry or Outbound control setpoints).
- **Multiple Streams of the Same Payload Type**: If an entity needs to receive or send multiple streams sharing the exact same payload type `T` (e.g., Primary NIC vs Backup NIC, or Front LiDAR vs Rear LiDAR), differentiate them cleanly using the optional `Tag` template parameter:
```cpp
template <typename T, typename Tag = void>
struct StreamComponent {
    std::shared_ptr<TripleBuffer<T>> buffer;
    TripleBuffer<T>* rawBuffer{nullptr};
    uint64_t lastTimestampNs{0};
    uint32_t statusWord{0};
    bool isConnected{false};

    TripleBuffer<T>* get() const;
    bool isValid() const;
};

// Example usage:
struct PrimaryNicTag {};
struct BackupNicTag {};

entity.addComponent<Brezel::StreamComponent<TelemetryPayload, PrimaryNicTag>>(bufA);
entity.addComponent<Brezel::StreamComponent<TelemetryPayload, BackupNicTag>>(bufB);
```



