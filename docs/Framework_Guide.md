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

#### Per-entity replication authority (`addAuthorityRule`, `authorityFor`)
A component type's `authority` is fixed at registration. When ownership has to differ per entity (one entity owns a
component that is server-owned on every other entity), register an authority rule and have the replication code ask
`authorityFor(entity, info)` instead of reading `info.authority`:
```cpp
// A rule returns an authority, or std::nullopt to leave the decision to the next rule and finally to info.authority.
ComponentRegistry::addAuthorityRule(
    [](const Brezel::Entity& e, const ComponentRegistry::ComponentTypeInfo& info)
        -> std::optional<ComponentRegistry::ReplicationAuthority> {
        if (info.saveString == StringID::from("IdentityComponent") && e.has<OwnedByRemoteHostTag>())
            return ComponentRegistry::ReplicationAuthority::ClientAuthoritative;
        return std::nullopt;
    });

// Replication code (burst, edit routing, session-up send):
const auto authority = ComponentRegistry::authorityFor(entity, info);
```
Rules:
- The first rule that answers wins, in registration order; with no rule registered `authorityFor` returns
  `info.authority`, so a project that registers none behaves exactly as with per-type authority.
- Rules are registered at startup, on the thread that registers components, before any replication starts. The list
  is read-only afterwards, which is why `authorityFor` takes no lock.
- `authorityFor` is called where the caller already has world access to the entity (holds a world lock, or is the
  world's single writer); it takes none itself.
- A rule reads only the entity's own components: no other entity, no global or mutable state, no side effects.

Guard on the receiving side (`acceptsRemoteDelta`): the code that applies a delta from a peer asks whether the sender
may write that component on that entity, passing the sender's authority (`ServerAuthoritative` for a delta from the
server, `ClientAuthoritative` for one from a client):
```cpp
if (!ComponentRegistry::acceptsRemoteDelta(entity, info, ComponentRegistry::ReplicationAuthority::ServerAuthoritative))
    return;  // dropped: this side owns the component on this entity
```
| Owner here (`authorityFor`) | Delta from the server | Delta from a client |
| :-- | :-- | :-- |
| `ServerAuthoritative` | applied | dropped |
| `ClientAuthoritative`, from the type | applied (the server is the owner's remote control panel; its burst skips the type) | applied |
| `ClientAuthoritative`, from a rule | dropped | applied |
| `LocalOnly` | applied | applied |

- `sender` `LocalOnly` means the origin is not stated (local tools, tests): never dropped.
- With no rule registered, nothing a server sends is dropped, so per-type behaviour is unchanged; a client's delta
  for a server-owned type is dropped.
- A sender uses the same resolution before sending: it does not send what the receiver would drop (the burst skips
  client-owned components, a server edit of a rule-owned component stays local).
- Both sides must register the same rules (they run the same startup code); an old peer without the rule keeps
  sending, and the new receiver drops what it owns.

Guard on the local side (`acceptsLocalEdit`, `acceptsLocalRename`, `RemoteMirrorTag`): the receiving guard alone lets a
node edit locally a component that the remote host owns; the edit is never sent and reverts at the next session up. The
application marks the local mirror of an entity hosted by a remote client with `RemoteMirrorTag` (where it binds the
mirror, on the world's single writer; runtime state, not registered), and local authoring paths ask before editing:
```cpp
// Generic inspector: show the name read-only on a mirror whose name the client owns
const bool readOnly = !ComponentRegistry::acceptsLocalRename(entity);
// Any component: the standard RenameEntity handler already refuses (no change applied)
if (!ComponentRegistry::acceptsLocalEdit(entity, info)) return;
```
| Entity | Component owner (`authorityFor`) | Local edit |
| :-- | :-- | :-- |
| no `RemoteMirrorTag` | any | allowed |
| `RemoteMirrorTag` | `ServerAuthoritative` or `LocalOnly` | allowed |
| `RemoteMirrorTag` | `ClientAuthoritative`, from the type | allowed (the server is the owner's remote control panel; the edit is forwarded) |
| `RemoteMirrorTag` | `ClientAuthoritative`, from a rule | refused |

- The standard `RenameEntity` handler calls `acceptsLocalRename()`: a refused rename changes nothing. Other standard
  handlers do not call `acceptsLocalEdit()` yet.
- `acceptsLocalRename()` returns true when `IdentityComponent` is not registered.

**Not yet in Brezel: a fully featured runtime authoring model.** Today Brezel has replication ownership (type authority,
per-entity rules, the receiving guard) and this one local guard. Still to design (Stacato O-15, after the show): a
local-edit policy for every component and every standard handler; edits made on the side that does not own a
component routed to the owner and applied there, so a remote edit (a rename from the server) works instead of being
refused; the node's role (server, client, standalone) known to the framework; how ownership is declared (project,
binding, runtime) and transferred; conflicts after a disconnection; what each side persists. `RemoteMirrorTag` and
`acceptsLocalEdit()` are interim and will be replaced by that model.

### 4.4 Entity Cloning & Duplication
Brezel provides high-level ECS cloning primitives that automatically allocate a new UUID, copy all registered non-transient components using `info.copyComponent`, and assign a new entity name:
```cpp
// 1. Direct Entity API
Brezel::Entity clonedEntity = sourceEntity.clone("Cloned Robot");

// 2. ComponentRegistry Helper
Brezel::Entity clonedEntity = Brezel::ComponentRegistry::cloneEntity(sourceEntity, "Custom Name");
```
Both helpers write the registry directly and neither registers the clone's UUID in the `Project` nor fires an event, so call them only where you hold the writer's lock. From any other thread (a GUI, a script, a network handler) push `Brezel::Mutations::CloneEntity{ sourceUuid, sourceHandle, newUuid, newName }` (`StandardMutations.hpp`) instead. Brezel registers no handler for it: the host's handler, applied inside the writer's lock, creates the clone through `Project::createEntityWithUUID` (so it is in the UUID map), copies the registered, non-transient components, runs their `onCreated` hooks, fires `Events::EntitySpawned` and refuses the sources its policy protects. The producer can pass its own `newUuid` to find the clone once the mutation has been applied; 0 lets the handler generate one.

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
- **`Brezel::TripleBuffer<T>`**: Lock-free, wait-free Simpson 4-slot asynchronous buffer (two pairs of two slots) for high-frequency producer/consumer communication (e.g. RT thread <-> NRT thread) without blocking or mutexes. Latest value wins; older unread values are overwritten.
  - **One writer, one reader.** Exactly one thread calls `write()` (several producers are allowed only when one mutex serializes them, as `MotionIngress` does) and exactly one thread calls `read()`. A second unsynchronized writer breaks the algorithm.
  - **Memory ordering (BUG-22, 2026-10-20).** The reader's store of `readingPair_` followed by its load of `slotInPair_`, and the writer's stores followed by its next load of `readingPair_`, form a store-buffering pattern that release/acquire does not order. `write()` starts with a `seq_cst` fence and `read()` has one between the `readingPair_` store and the `slotInPair_` load. Without them two back-to-back writes tore about one read in 100,000 on an arm64 Mac; with them none (stress test in `tests/test_stage_ecs.cpp`).
  - **Writer's load of `readingPair_` is acquire (BUG-90, 2026-10-23).** It pairs with the reader's release store, which follows the reader's copy of the previous value, so that copy happens before the writer reuses the slot. The fence alone (placed before the load) did not make that formal; ThreadSanitizer reported it at every RT channel.
  - **`hasNew()` / `read()`.** `read()` clears the flag (atomic exchange) before it chooses the pair, so a write landing during the copy keeps its flag and the next `if (hasNew()) read()` picks it up. `read()` always returns the latest value, new or not.
  - **`read()` always returns `true`; it is not a freshness test (BUG-55).** Before the first `write()` it copies the value-initialised slot (`T{}`), and after that it returns the last value again until a new one arrives. `if (buffer.read(v))` therefore accepts a buffer nobody has written yet. Freshness comes from the payload or the flag: give the payload a producer timestamp that is never zero once written and treat zero as "nothing yet", or test `hasNew()` before `read()` when only new values matter (single reader only, since `read()` clears the flag).
  - **`readLatest()`** is a peek without the reader handshake: it does not clear the flag and may tear against a concurrent write. Use it only for display, never for control data.
  - Payloads must be trivially copyable.
- **`Brezel::RingBuffer<T, Capacity>`**: Fixed-capacity lock-free SPSC circular FIFO for bounded streaming packets.
  - One producer thread, one consumer thread; `Capacity - 1` slots are usable. `push()` loads `tail_` with acquire (BUG-90, 2026-10-23), pairing with `pop()`'s release store, so the consumer's copy of a slot happens before the producer overwrites it.
- **`Brezel::StallWatchdog`** (`Brezel/Concurrency/StallWatchdog.hpp`, 2026-10-23): detects a stalled thread from outside it. A watched thread calls `beat(id)` at least once per timeout (wait-free, one atomic store); the watchdog's own thread checks every interval (default 100 ms) and calls the listener once when an entry goes stalled and once when it beats again. `watch()` counts as the first beat, so a thread that never beats is caught; a thread that stops on purpose calls `unwatch()`. Up to 16 entries. The listener runs on the watchdog thread with no watchdog lock held and must not block. `check(now)` drives it by hand in tests.

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
Bounded 512-byte stack envelope (`AnyMutation::STORAGE_SIZE`; raised from 256 so a 16-marker optical constellation update fits, Stacato D-74) for open-ended mutation routing across subsystems without a centralized static variant:
- Subsystems across the repo define their own discrete mutation structs locally (e.g., `MecanumConfigMutation`, `UpdateShapeVertices`).
- Enforces `sizeof(T) <= STORAGE_SIZE` at compile time; mutation headers assert against `Brezel::AnyMutation::STORAGE_SIZE`, not a literal. Zero heap allocation inside the envelope itself.
- Trivially copyable PODs execute with zero-overhead `std::memcpy` and no-op destruction.
- Payloads exceeding 512 bytes (or dense geometric/spline data) explicitly store large data in `std::shared_ptr`; `AnyMutation` safely manages placement construction, destruction, and reference-counted copy/move semantics via type-erased function pointers.
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
- `Entity` handles still reach live components without a guard; the **ECS wall** (section 9.11) counts and locates every such access at runtime.

### 9.9 Cross-Platform Thread Naming (`Brezel::setThreadName`)
Located in `Brezel/Concurrency/ThreadUtils.hpp`:
- Cross-platform utility that labels the currently executing thread for LLDB/GDB debuggers, Xcode, VS Code, Instruments, and OS thread samplers.
- Automatically handles platform differences (macOS `pthread_setname_np(name)` vs Linux `pthread_setname_np(pthread_self(), name)` vs Windows `SetThreadDescription`).
- Automatically integrated into `Brezel::PeriodicLoop::start` to name periodic background workers.
- `Brezel::makeNonRealtimeThread(avoidCpu)`: makes the calling thread `SCHED_OTHER` (priority 0) with an affinity of every configured CPU except `avoidCpu` (negative: all). On Linux a thread inherits its creator's policy, priority and CPU set, so a helper started from a real-time thread must call this first or it runs at real-time priority on the real-time core. Returns false if a call failed; a no-op on macOS and Windows.

### 9.9b Raw monotonic clock (`Brezel/Foundation/MonotonicRawClock.hpp`)
- `Brezel::monotonicRawNanoseconds()`: nanoseconds of a monotonic clock that no time correction steps or slews. Linux: `CLOCK_MONOTONIC_RAW`; `std::chrono::steady_clock` (`CLOCK_MONOTONIC`) never jumps but follows an NTP daemon's rate corrections, so a loop paced by it drifts against an external clock while the daemon corrects. macOS and Windows: `steady_clock`, already raw there.
- Unspecified epoch: compare its values only with each other. Wait-free, safe on a real-time thread. Used to pace the EtherCAT cycle against the slaves' reference clock.

### 9.9c Remote clock estimate (`Brezel/Time/RemoteClockEstimator.hpp`)
- Placeholder: `Brezel::RemoteClockEstimator` (`reset()`, `addSample(remoteNs, localNs)`, `hasEstimate()`, `offsetNs()`, `toLocalNs(remoteNs)`) estimates a remote monotonic clock's offset from the fastest arrival over a sliding window. Interface only, stub bodies; the section is written with the implementation.

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

### 9.11 ECS wall (`Brezel/Core/EcsWall.hpp`)
A runtime check of the one ECS access rule: **a thread touches entity components only while it holds a `read()` or
`write()` scope of the project, or when it is the project's writer thread.**
- **State**: each `Project` owns an `EcsWall`, published in its registry context (`ctx()`, type `EcsWall*`). The wall
  holds the writer thread id (`Project::setWriterThread(id)`, set by whoever runs the single-writer loop, on that
  thread; an empty id clears it). `ReadAccess` / `WriteAccess` record their scope in a thread-local table (up to 8
  projects per thread; a moved guard is counted once).
- **Checked accessors**: `Entity::add`, `add_or_replace` (up to 4 constructor arguments), `remove`, `has`, `try_get`,
  `get`, `isValid`, `operator bool`, `getParent`, `getName`, `getUUID`, `getPath`, `setParent`, and
  `Project::destroyEntity`, `resolveEntity`, `getEntityByPath`. Each takes a trailing defaulted
  `std::source_location`, so the site is the caller's line (`operator bool` cannot take one: its site is
  `Entity.hpp`). `handle()` and `registry()` are unchecked escape hatches. Entities of a plain `entt::registry` (no
  project, e.g. a GUI scratch copy) are never checked.
- **On a violation**: an atomic total is incremented and the site is recorded in a fixed table of `EcsWall::kMaxSites`
  (32) slots: file, line, function, thread name (`"main"` for the unnamed process main thread, else the id in hex),
  first time (steady clock, ns) and count per site, keyed by file, line and thread. Recording is lock-free and
  allocation-free; a full table counts `unrecordedViolations()`. The violating thread only records.
- **Mode**: without `NDEBUG` a violation prints the site to stderr and calls `std::abort()`, unless the environment
  holds `BREZEL_ECS_WALL=count` **when the project is constructed** (a temporary crutch for test runs; never set it in
  code or build files). With `NDEBUG` the wall only counts.
- **Reading it** (any thread): `totalViolations()`, `unrecordedViolations()`, `siteCount()`, `site(i, SiteInfo&)`
  (false while slot `i` is empty or being filled). The writer loop's owner polls `site()` and logs each new site once;
  the destructor prints the total and every site to stderr when there was a violation.
- **Named waivers** (`EcsWallWaiver`): a stack object that names a known violation while it lives on its thread:
  `const Brezel::EcsWallWaiver waiver{"thread", "reason", "follow-up task"};` (string literals; the constructor
  also takes a defaulted `std::source_location`). An access the wall would reject is then not a violation: it is
  counted in `waivedAccesses()` and recorded in a second fixed table of `kMaxWaivedSites` (32) slots keyed by the
  **waiver's** file and line and the thread, whose `SiteInfo` also carries `reason` and `followUp`
  (`waivedSiteCount()`, `waivedSite(i, SiteInfo&)`, `unrecordedWaivedAccesses()`); it never asserts. The access
  itself is unchanged. The waiver covers everything called inside its scope on its thread (any project's wall);
  waivers nest and the innermost one is recorded. Thread-local pointer, no lock, no allocation. Waived accesses are
  not in `totalViolations()`; the destructor prints them after the violations. A waiver is debt, not a fix: each one
  names the task that removes it.
- **Cost**: one registry-context lookup, one thread-id compare and a scan of at most 8 thread-local entries per access.

---

## 10. Fitting Math (`Brezel/Math/`)

Header-only, C++20, thread-agnostic, no allocation and no storage of the input (D-73). Doubles throughout. First user:
the optical mount calibration in Stacato (D-71, D-74 point 6); tested through Stacato's `tests/test_calibration.cpp`
until Brezel has its own test target.

### 10.1 Dense linear least squares (`LeastSquares.hpp`)
`Brezel::Math::LeastSquares<N>` (N = 1 to 8 unknowns) folds equations in one at a time and never keeps them:
- `addRow(std::array<double, N> a, double b, double weight = 1.0)`: one equation `a . x = b`; a non-finite value or a
  weight that is not positive marks the problem invalid.
- `solve()` returns `LeastSquaresSolution<N>`: `ok`, `x`, `residualRms` (`sqrt(sum(w * r^2) / rows)` at the minimum),
  `rows`, `usedQr`.
- Two factorizations are updated per row: the normal equations, solved by Cholesky (the usual path), and an
  upper-triangular QR factor updated by Givens rotations, used when a Cholesky pivot falls below `1e-10` of its diagonal
  (ill-conditioned for the squared system). The QR factor also gives the exact minimum residual. A QR diagonal below
  `1e-9` of the largest (rank deficient), fewer rows than unknowns, or invalid input returns `ok == false`.
- Cost per row: O(N^2); `solve()` is O(N^3) on the stack.

```cpp
Brezel::Math::LeastSquares<4> ls;                 // spin: x = cx + cos(a) qx - sin(a) qy, y = cy + sin(a) qx + cos(a) qy
for (const auto& s : samples) {
    ls.addRow({1, 0, std::cos(s.yaw), -std::sin(s.yaw)}, s.x);
    ls.addRow({0, 1, std::sin(s.yaw),  std::cos(s.yaw)}, s.y);
}
const auto sol = ls.solve();                       // sol.x = {cx, cy, qx, qy}
```

### 10.2 Total least-squares 2D line (`LineFit.hpp`)
`LineFitAccumulator` keeps a running mean and co-moments (Welford): `add(x, y)`, `count()`, `fit()`. `LineFit2D`:
`ok` (at least 2 distinct points), `centroid`, unit `direction` (principal axis of the point covariance; its sign is
arbitrary, orient it with a displacement), `rms` (perpendicular RMS, the square root of the small eigenvalue; about
`1e-8` on exact data because of the cancellation), `extent` (standard deviation along the line), `count`. Free
functions: `fitLine2D(n, pointAt)` where `pointAt(i)` returns something indexable `[0]`, `[1]`, and
`fitLine2D(const double* x, const double* y, n)`.

### 10.3 Kasa circle fit (`CircleFit.hpp`)
`fitCircleKasa(n, pointAt)` and `fitCircleKasa(x, y, n)` return `CircleFit2D`: `ok` (at least 3 non-collinear points),
`centre`, `radius`, `rms` (of distance minus radius), `count`. Algebraic fit (`x^2 + y^2 + D x + E y + F = 0` through
`LeastSquares<3>` on coordinates shifted to the first point), two passes over the points. It is biased towards smaller
radii on short noisy arcs: use it as a cross-check, not as the only estimate.
