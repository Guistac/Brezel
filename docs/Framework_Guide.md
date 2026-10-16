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

*Deterministic Derivation:* For scenarios requiring canonical, zero-negotiation UUIDs (such as optical rigid bodies or network proxy handles derived from external names or byte identifiers), `Brezel::UUID` provides:
```cpp
Brezel::UUID uuid1 = Brezel::UUID::deriveFromBytes(dataPtr, dataLen);
Brezel::UUID uuid2 = Brezel::UUID::deriveFromString("Sensor_Rig_01");
```
Both use 64-bit FNV-1a hashing to deterministically generate non-zero, valid `Brezel::UUID` instances.

*Convenience Accessors & Equality:* `Brezel::Entity` provides `entity.getName()` and `entity.getUUID()` to query identity metadata directly without manual `try_get<IdentityComponent>()` lookups. It provides `operator==` and `operator!=` for exact EnTT handle equality comparison, and an `explicit operator bool()` for handle validity checks.

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
- `Tag::Networked`: The property is replicated across network sessions (participates in distributed binary delta sync).
- `Tag::CommandStack`: Modifying this property via the UI or CLI automatically pushes an Undo/Redo command.
- `Tag::ReadOnly`: The UI should lock the field; it cannot be modified by the user.
- `Tag::Hidden`: The UI should not render this property at all.

### 4.3 Component Registration & Flags
Before a component can be used in the Editor or loaded from XML, it must be registered in the application startup phase. The string name provided becomes the XML element tag.
```cpp
ComponentRegistry::registerComponent<MotorConfig>("MotorConfig", {
    .displayName = "Motor Configuration",
    .category    = "Actuation",
    .flags       = ComponentRegistry::ComponentFlag::None
});

// Empty tags automatically auto-detect isTag = true and category = "Tags"
ComponentRegistry::registerComponent<DynamicVehicleTag>("DynamicVehicleTag");

// Transient runtime conduits (IPC queues, triple buffers, network pipes):
// Reflected in UI / Debug Inspector, but skipped by XML Serializer so save files stay clean
ComponentRegistry::registerComponent<StreamComponent<OdometryFeedback>>(
    "StreamComponent_OdometryFeedback",
    ComponentRegistry::ComponentFlag::Transient
);

// Networked distributed components (replicated across remote machines/proxies):
ComponentRegistry::registerComponent<TelemetryComponent>(
    "TelemetryComponent",
    {
        .flags     = ComponentRegistry::ComponentFlag::Networked,
        .authority = ComponentRegistry::ReplicationAuthority::ServerAuthoritative
    }
);

// Flags support bitwise composition:
// ComponentRegistry::ComponentFlag::Transient | ComponentRegistry::ComponentFlag::Networked

// Replication Authority Semantics:
// enum class ReplicationAuthority : uint8_t {
//     LocalOnly           = 0, // Never replicated across network (local RT buffers, vision solvers)
//     ServerAuthoritative = 1, // Central show console owns state; replicated Server -> Client
//     ClientAuthoritative = 2  // Remote hardware SBC owns state; replicated Client -> Server
// };

// Optional: Register custom UI drawer callback (for specialized multi-property widgets)
// Note: Registered drawers are automatically preserved if components are re-registered.
ComponentRegistry::setCustomDrawer<MotorConfig>([](Brezel::Entity entity) {
    return drawMotorConfigCustomGui(entity);
});

// Component Lifecycle & Side-Effect Callbacks:
// Registered handlers execute automatically under world.write() during mutation processing,
// enabling self-contained subsystem side effects (e.g., bounding box recalculation, RT air-gap sync):
ComponentRegistry::registerComponent<ShapeComponent>("ShapeComponent", {
    .displayName = "2D Shape",
    .category    = "Geometry",
    .flags       = ComponentRegistry::ComponentFlag::Networked,
    .onUpdated   = [](Brezel::Entity e) { e.get<ShapeComponent>().recalculateBounds(); },
    .onCreated   = [](Brezel::Entity e) { /* Initial setup on attachment */ },
    .onRemoved   = [](Brezel::Entity e) { /* Cleanup on detachment */ }
});
```

### 4.4 Entity Cloning & Duplication
Brezel provides high-level ECS cloning primitives that automatically allocate a new UUID, copy all registered non-transient components using `info.copyComponent`, and assign a new entity name:
```cpp
// 1. Direct Entity API
Brezel::Entity clonedEntity = sourceEntity.clone("Cloned Robot");

// 2. ComponentRegistry Helper
Brezel::Entity clonedEntity = Brezel::ComponentRegistry::cloneEntity(sourceEntity, "Custom Name");
```

### 4.5 Component Authoring & Automation Quick Reference

#### 1. What Components MUST Provide (Mandatory Minimum)
1. **Plain Old Data (POD) Struct**:
   ```cpp
   struct MotorConfig {
       float maxRpm{3000.0f};
       float ratedCurrentA{12.5f};
       bool inverted{false};
   };
   ```
2. **Free-Function `reflect` Specialization**:
   ```cpp
   namespace Brezel {
   template<typename V>
   void reflect(MotorConfig& cfg, V& v) {
       v.visit_property("maxRpm", cfg.maxRpm, {Tag::Persistent, Tag::Networked});
       v.visit_property("ratedCurrentA", cfg.ratedCurrentA, {Tag::Persistent, Tag::Networked});
       v.visit_property("inverted", cfg.inverted, {Tag::Persistent, Tag::Networked});
   }
   }
   ```
3. **Registration Call at Startup**:
   ```cpp
   Brezel::ComponentRegistry::registerComponent<MotorConfig>("MotorConfig");
   ```

#### 2. What Automations Exist BY DEFAULT (Zero Boilerplate)
- **Type-Erased CRUD Operations**: `createComponent` (`e.add<T>()`), `removeComponent` (`e.remove<T>()`), `hasComponent` (`e.has<T>()`), and `copyComponent` are auto-synthesized for every registered type.
- **Entity Cloning**: `entity.clone()` automatically copies all registered non-transient components to the clone without custom copying code.
- **Tag Auto-Detection**: Empty structs (`std::is_empty_v<T> == true`) or category `"Tags"` automatically set `isTag = true`, rendering as compact tag pills in UIs.
- **Generic Visitor Reflection**: Serializers, debug inspect visitors, and GUI reflection visitors iterate properties automatically via `info.reflect(entity, visitor)`.
- **Discrete Mutation Handling**: Standard mutations (`Brezel::Mutations::UpdateComponent`, `RemoveComponent`, `AddComponent`) resolve the entity and invoke `info.reflect`, `createComponent`, or `removeComponent` safely under `world.write()`.

#### 3. What Components CAN OPTIONALLY Register
- **`displayName`**: Human-readable label in UI/menus (e.g. `"Motor Configuration"`). Defaults to `saveString`.
- **`category`**: Grouping category in add/inspection menus (e.g. `"Actuation"`, `"Geometry"`, `"Tags"`).
- **`flags`**:
  - `ComponentFlag::None` (default).
  - `ComponentFlag::Transient`: Excluded from project serialization (ideal for RT triple-buffer bridges, queues, sockets).
  - `ComponentFlag::Networked`: Replicated across network transport layers.
  - `ComponentFlag::Hidden`: Hidden from end-user UI inspector trees.
- **`authority`**:
  - `ReplicationAuthority::LocalOnly` (default).
  - `ReplicationAuthority::ServerAuthoritative`: Replicated Server -> Client.
  - `ReplicationAuthority::ClientAuthoritative`: Replicated Client -> Server.
- **Lifecycle Callbacks (`onUpdated`, `onCreated`, `onRemoved`)**:
  - `.onUpdated = [](Entity e) { ... }`: Executed whenever component fields are modified via `UpdateComponent` or remote delta sync.
  - `.onCreated = [](Entity e) { ... }`: Executed when the component is first attached.
  - `.onRemoved = [](Entity e) { ... }`: Executed when the component is removed.
- **Custom UI Drawer (`setCustomDrawer<T>`)**:
  - Overrides the default property-by-property reflection drawer with a specialized custom ImGui widget.

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

> [!NOTE]
> Entity identity metadata (`Name`, `DisplayName`, and `UUID`) is persisted strictly as XML attributes on the `<Entity>` root element. `reflectEntityComponents` excludes `IdentityComponent` from child element reflection to prevent duplicate tags, and `addReflectEntityComponent` guards component creation to preserve pre-existing entity component instances.

### 6.3 High-Speed Binary Serialization (`BinaryVisitor.hpp`)
For network state replication, IPC streaming, or fast component caching, Brezel provides compact, zero-copy binary visitors:
- `Brezel::BinarySerializer`: Serializes reflected component properties into a contiguous byte vector.
- `Brezel::BinaryDeserializer`: Deserializes packed byte streams with strict bounds checking and component type-hash verification.

```cpp
#include "Brezel/Serialization/BinaryVisitor.hpp"

// 1. Serialize component (filtering for networked properties)
Brezel::BinarySerializer serializer(Brezel::BinaryPropertyFilter::NetworkedOnly);
serializer.beginComponent("MotorConfig");
Brezel::reflect(motorConfig, serializer);
serializer.endComponent();

std::span<const uint8_t> payload = serializer.getBuffer();

// 2. Deserialize payload on receiver
Brezel::BinaryDeserializer deserializer(payload, Brezel::BinaryPropertyFilter::NetworkedOnly);
if (deserializer.beginComponent("MotorConfig")) {
    Brezel::reflect(targetMotorConfig, deserializer);
    deserializer.endComponent();
}
```

Both visitors support tag filtering via `BinaryPropertyFilter`:
- `BinaryPropertyFilter::All`: Serializes/deserializes all reflected properties.
- `BinaryPropertyFilter::NetworkedOnly`: Only processes properties tagged with `Tag::Networked`.
- `BinaryPropertyFilter::PersistentOnly`: Only processes properties tagged with `Tag::Persistent`.

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
- Subsystems across the repo define their own discrete mutation structs locally (e.g., `MecanumConfigMutation`, `UpdateShapeVertices`).
- Enforces `sizeof(T) <= 256` at compile time. Zero heap allocation inside the envelope itself.
- Trivially copyable PODs execute with zero-overhead `std::memcpy` and no-op destruction.
- Payloads exceeding 256 bytes (or dense geometric/spline data) explicitly store large data in `std::shared_ptr`; `AnyMutation` safely manages placement construction, destruction, and reference-counted copy/move semantics via type-erased function pointers.
- When drained, `mutation.execute(dispatcher)` unpacks and dispatches the typed event to connected subsystem listeners.

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

### 9.7 Entity Queue Component (`Brezel::QueueComponent<T, Capacity = 64, Tag = void>`)
Generic ECS component bridging an entity to a lock-free Single-Producer Single-Consumer (SPSC) `RingBuffer<T, Capacity>` at a stable memory address (`Brezel/Concurrency/QueueComponent.hpp`):
- Provides lossless in-order FIFO queuing for discrete event commands (e.g., `MotionCommand`, E-Stops, Cue triggers) crossing the NRT $\leftrightarrow$ RT boundary.
- Resolves EnTT "swap-and-pop" relocation by holding a `std::shared_ptr<RingBuffer<T, Capacity>>` or non-owning `RingBuffer<T, Capacity>*`.
- Works for any trivially copyable Plain Old Data (POD) payload.
- Can be tagged (`QueueComponent<Payload, 64, EmergencyTag>`) if an entity needs multiple distinct queues of the same payload type.

### 9.8 Project Thread Safety & RAII Registry Accessors (`Brezel::Project`)
Thread safety across NRT systems (UI rendering, serialization, supervisor ticks, mutation execution) is enforced at compile time via RAII scoped guards on `Brezel::Project`:
- The raw `entt::registry` is strictly encapsulated (private). Direct registry access via `getRegistry()` is removed.
- **`Project::read()`**: Returns a scoped `ReadAccess` guard holding a `std::shared_lock<std::shared_mutex>`. Exposes const view iterators (`view<const Components...>()`), `get<T>()`, `try_get<T>()`, and `getEntity(e)`. Multiple reader threads (e.g., UI rendering, serialization) query the ECS simultaneously with zero lock contention.
- **`Project::write()`**: Returns a scoped `WriteAccess` guard holding a `std::unique_lock<std::shared_mutex>`. Exposes mutable views, `emplace<T>()`, `remove<T>()`, and `destroy(e)`.
- **`Project::resolveEntity(uuid, fallback)`**: Resolves an entity either by its persistent `UUID` or by a local fallback `Entity` handle.
- **`Project::getEntities()`**: Returns a const reference to the internal `std::unordered_map<UUID, Entity>` of all registered entities in the project for lock-free mutation handler traversal.
- Eliminates manual lock management and prevents data races across concurrent NRT threads while maintaining 100% EnTT quarantine.

### 9.9 Cross-Platform Thread Naming (`Brezel::setThreadName`)
Located in `Brezel/Concurrency/ThreadUtils.hpp`:
- Cross-platform utility that labels the currently executing thread for LLDB/GDB debuggers, Xcode, VS Code, Instruments, and OS thread samplers.
- Automatically handles platform differences (macOS `pthread_setname_np(name)` vs Linux `pthread_setname_np(pthread_self(), name)` vs Windows `SetThreadDescription`).
- Automatically integrated into `Brezel::PeriodicLoop::start` to name periodic background workers.

### 9.10 Standard Lifecycle Mutations (`Brezel/Concurrency/StandardMutations.hpp`)
Pre-packaged, domain-agnostic discrete mutations and dispatcher sink connectors for asynchronous entity lifecycle and continuous stream/queue management:
- `Brezel::Mutations::DestroyEntity`: Destroys an entity permanently by UUID or handle fallback.
- `Brezel::Mutations::ClearProject`: Clears all entities in the project.
- `Brezel::Mutations::CreateEntity`: Creates a new entity with optional UUID and display name.
- `Brezel::Mutations::RenameEntity`: Renames an entity asynchronously.
- `Brezel::Mutations::SetComponent<T>`: Pure typed state mutation carrying a C++ value object `T`, assigning it directly (`entity.add<T>(comp)`), invoking `onUpdated`, and firing `Brezel::Events::ComponentChanged` on the Dispatcher.
- `Brezel::Mutations::UpdateComponent`: Generic state mutation carrying a binary serialized component payload, updating the target entity, invoking its `onUpdated` hook, and firing `Brezel::Events::ComponentChanged` under `world.write()`.
- `Brezel::Mutations::RemoveComponent`: Generic mutation detaching a component by `StringID` and invoking its `onRemoved` hook under `world.write()`.
- `Brezel::Mutations::AddComponent`: Generic mutation instantiating a registered component by `StringID` and invoking its `onCreated` hook under `world.write()`.
- `Brezel::Mutations::AttachStream<T, Tag>`: Attaches a `StreamComponent<T, Tag>` to an entity.
- `Brezel::Mutations::DetachStream<T, Tag>`: Detaches a `StreamComponent<T, Tag>` from an entity.
- `Brezel::Mutations::AttachQueue<T, Capacity, Tag>`: Attaches a `QueueComponent<T, Capacity, Tag>` to an entity.
- `Brezel::Mutations::DetachQueue<T, Capacity, Tag>`: Detaches a `QueueComponent<T, Capacity, Tag>` from an entity.
- `Brezel::Mutations::registerStandardMutationHandlers(project, dispatcher)`: Automatically registers lifecycle, generic component handlers, and typed `SetComponent<T>` mutation handlers for all registered components (and any components registered dynamically afterwards), associating the Dispatcher with the Project.
- `Brezel::Mutations::registerStreamMutationHandlers<T, Tag>(project, dispatcher)`: Automatically registers typed stream handlers.
- `Brezel::Mutations::registerQueueMutationHandlers<T, Capacity, Tag>(project, dispatcher)`: Automatically registers typed queue handlers.
- `Brezel::Events::ComponentChanged`: Event emitted when an entity's component is modified via `SetComponent` or `UpdateComponent`.
- `Brezel::Events::EntitySpawned`: Event emitted when an entity is spawned into the scene.
- `Brezel::Events::EntityDestroyed`: Event emitted when an entity is permanently removed from the scene.





