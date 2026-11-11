# Brezel Framework Guide

Brezel is a header-only C++ application framework on top of the **EnTT** entity component system. It gives an
application an ECS world with identity, hierarchy, reflection and serialization (the non-real-time side), and a small
set of wait-free channels and tools for code that runs on a real-time loop. It knows nothing about the application's
domain.

**How to read this guide.** It describes the code as it is at Brezel commit `1d31d48` (2026-11-11). A cite such as
`Project.hpp:202` is a file under `include/Brezel/` and a line in it. Where the code has a known defect, the guide says
so next to the API, so that a reader does not rely on behaviour that is not there. Stacato tracker rows (ARCH-, BUG-,
TEST-) are named where one exists.

**Status.**
- Language: `CMakeLists.txt:5` sets C++23, but the only consumer (Stacato) compiles Brezel as C++20. Every header
  compiles as C++20 (checked 2026-11-11). Write C++20 only.
- Header-only. Dependencies: EnTT (`extern/entt`, version 4.0), spdlog (`StringID` formatter, the load report),
  tinyxml2 (XML) and GNU readline (`Console/InteractiveConsole.hpp` only). How to build: `README.md`.
- No tests in Brezel. The primitives are tested through Stacato's `tests/` (Stacato TEST-09).
- Eight headers do not compile on their own. Seven need `Brezel/Core/ComponentRegistry.hpp` included first (and
  `ConsoleVisitor.hpp` also `Brezel/Core/EntityReference.hpp`); `CoreComponents.hpp` is included through
  `Entity.hpp`. Section 13 lists every header.

---

## 1. Core rule: real-time code never touches the ECS

Brezel separates two kinds of code.

- **Non-real-time (NRT) code** owns the `Project`, its entities and components, files, the network and the user
  interface. It may lock, allocate and log.
- **Real-time (RT) code** is a periodic loop with a deadline. It never touches a `Project`, an `Entity`, an EnTT
  registry or a component. It never takes a mutex, allocates, logs or throws.

The two sides exchange **trivially copyable payloads** through the wait-free channels of section 9.1
(`TripleBuffer`, `RingBuffer`, `TriggeredCapture`) or through plain atomics. Section 8 says how a channel is set up and
who keeps it alive.

What is safe on an RT thread: `TripleBuffer::write`, `read`, `hasNew`; `RingBuffer::push`, `pop`, `size`, `empty`;
`TriggeredCapture::record`; `StallWatchdog::beat`; `monotonicRawNanoseconds()`; `RemoteClockEstimator` and the fitting
math of section 10 (no allocation). Nothing else in Brezel is: `MutationQueue` locks and allocates, the implicit
`StringID(const char*)` constructor locks and allocates, and everything that touches a `Project` or an `Entity` belongs
to NRT code.

Brezel names no threads. The application decides which threads exist, which one writes the world and which channels
connect them. Stacato's rules are in Stacato's `docs/reference/THREADING_AND_ECS_CONTRACT.md` (section 3 lists its
channels).

---

## 2. Core ECS & Scene Graph

EnTT stores a flat table of entities. Brezel adds identity, a parent/child hierarchy and a UUID index, all owned by a
`Project` (section 9.8).

### 2.1 Identity & UUIDs
Every entity that a `Project` creates gets an `IdentityComponent` and a `HierarchyComponent`
(`Project.hpp:331-345`).
```cpp
struct IdentityComponent {   // CoreComponents.hpp:14-18
    std::string name;
    std::string displayName;
    UUID uuid;
};
```
- `name`: the strict, path-safe name used by paths (section 7.1). `Project::sanitizeName` (`Project.hpp:308-319`)
  replaces every byte that is not a letter, a digit, `_` or `-` with `_`; an empty result becomes `"Entity"`. So `"a b"`
  becomes `"a_b"`, and every byte of a non-ASCII character becomes `_` on macOS and glibc (the code calls `std::isalnum` on a plain `char`, which is undefined for negative values; ARCH-103).
  `createEntity`, `createEntityWithUUID` and the XML loader sanitize. `restoreEntity` takes the name as given. The
  `RenameEntity` mutation (`StandardMutations.hpp:227-228`) and `clone()` (`ComponentRegistry.hpp:402`, a name such as
  `"a_b (Copy)"`) do **not** sanitize (ARCH-103).
- `displayName`: the name as the user typed it. `Entity::getDisplayName()` returns it, or `name` when it is empty
  (`Entity.hpp:209-214`); use it for anything shown to a person.
- `uuid`: a 64-bit identifier for references in files and across the network. 0 is invalid (`UUID.hpp:19`).
- Reflection: `name` and `displayName` are `Persistent` and `Networked` (`CoreComponents.hpp:41-45`); `uuid` is not
  reflected (XML stores it as an attribute, section 6.2). `registerBrezelCoreComponents()` registers
  `IdentityComponent` as `Networked` and `ServerAuthoritative` (`ComponentRegistry.hpp:429-440`);
  `registerStandardMutationHandlers` calls it.

**Generating UUIDs.** `UUIDProvider::generate()` (`UUIDProvider.hpp:17-21`) uses a thread-local `std::mt19937_64`
seeded once per thread from one `std::random_device` value, and never returns 0. Collisions are unlikely, not
impossible (ARCH-103).

**Deriving UUIDs.** For identifiers that must be the same on every node without negotiation:
```cpp
Brezel::UUID a = Brezel::UUID::deriveFromBytes(dataPtr, dataLen);
Brezel::UUID b = Brezel::UUID::deriveFromString("sensor_01");
```
Both use 64-bit FNV-1a and never return 0: a zero hash becomes 1, and empty input returns `UUID(1)`
(`UUID.hpp:24-41`).

**UUID index.** The `Project` keeps a map from UUID to entity. `createEntity`, `createEntityWithUUID` and
`restoreEntity` add to it; `destroyEntity` removes from it; `clear` empties it. A second entity with an existing UUID is
created, but the map keeps the first one (`Project.hpp:342`, ARCH-103). An entity cloned with `Entity::clone()` is not
in the map (section 4.4).

**Accessors.** `Entity::getName()`, `getDisplayName()` and `getUUID()` read the identity without a manual `try_get`.
`operator==` and `operator!=` compare the EnTT handles; `explicit operator bool()` is `isValid()`
(`Entity.hpp:104-115`).

### 2.2 Hierarchy (Parent/Child)
```cpp
struct HierarchyComponent {  // CoreComponents.hpp:9-12
    Entity parent;
    std::vector<Entity> children;
};
```
- `Entity::setParent(parent)` (`Entity.hpp:174-180`) sets `parent` and appends the entity to the parent's
  `children`. It does not remove the entity from a previous parent and does not reject itself or a cycle. Call it once
  per entity. A second call lists the child under two parents, and the XML save then writes it twice (ARCH-103).
- `Project::destroyEntity` (`Project.hpp:221-229`) does not unlink the entity from its parent or its children, and does
  not destroy the children. Unlink first if the hierarchy matters.
- `forEachChildEntity(f)` skips children whose handle is no longer valid. `getParent()` returns an invalid `Entity` at
  the root. `getPath()` returns the names from the root down.

### 2.3 Safe Cross-Referencing (`EntityReference`)
A component that refers to another entity stores an `EntityReference`, never a raw `entt::entity` or pointer.
```cpp
struct EntityReference {     // EntityReference.hpp:7-17
    UUID uuid;
    Entity entity;
    void set(Entity& ent);   // stores the handle and copies its UUID
};
```
- The UUID is what is saved and sent. The handle is filled from the UUID.
- After an XML load, `relinkProjectReferences(project, report)` (`EntityReferenceLinker.hpp:51-54`) visits every
  component of every entity reachable from the top level and sets `entity = project.getEntityByUUID(uuid)`. A UUID it
  cannot find adds a `Warning` to the report. `Application::loadProject` runs it; `Xml::loadProject` does not, so a
  caller of `Xml::loadProject` runs it itself.
- A binary decode (section 6.3) updates `uuid` only and leaves the old handle in place (`BinaryVisitor.hpp:276-284`).
  After a decode, resolve by UUID (ARCH-103).

---

## 3. Component Architecture (Pure PODs)

A component is a plain struct: no base class, no virtual function, no wrapper type around its fields.

```cpp
struct SamplerConfig {
    float rateHz{100.0f};
    int channels{2};
    std::vector<float> weights;
    EntityReference source;
};
```

Rules the code imposes:
- **Field types.** The reflection visitors handle `int`, `float`, `bool`, `std::string`, `StringID`, `EntityReference`
  and a list through a `VectorAccessorBase` (`Visitor.hpp:46-52`). Any other type (`double`, `uint32_t`, an enum, a
  math vector) does not bind to a `visit_property` overload and does not compile. Convert it in `reflect` to a local of
  a supported type (`StreamComponent.hpp:52-55` does this for display), knowing that a write to the local is lost, or
  write a `VectorAccessorBase` that exposes its parts.
- **Not empty.** A registered type needs at least one field (section 4.3).
- **Default-constructible**: `AddComponent`, `UpdateComponent` and the XML loader create it with `add<T>()`.
- **Copy-constructible** to be cloned; otherwise `clone()` skips it without a message (`ComponentRegistry.hpp:304`).
- A component may hold `std::string` and `std::vector` (NRT data). Data for an RT loop never sits in a component that
  the RT reads; it travels as a trivially copyable payload through a channel (section 8).

---

## 4. The Reflection System

Brezel discovers a component's fields through a free function, not through virtual methods on the component. The XML
and binary serializers, the console and an application's inspector all walk the same function.

### 4.1 The `reflect` Free-Function
```cpp
template <typename V>
void reflect(SamplerConfig& c, V& v) {
    v.visit_property("rateHz", c.rateHz, {Brezel::Tag::Persistent, Brezel::Tag::Networked});
    v.visit_property("channels", c.channels, {Brezel::Tag::Persistent});
    v.visit_property("source", c.source, {Brezel::Tag::Persistent});
    Brezel::VectorAccessor weights(c.weights);
    v.visit_property("weights", weights, {Brezel::Tag::Persistent});
}
```
- **Where it lives.** The registry calls `reflect(*component, visitor)` unqualified (`ComponentRegistry.hpp:298`).
  Argument-dependent lookup finds it in the component's own namespace, or in namespace `Brezel` (through the visitor
  argument, `ComponentVisitor` is in `Brezel`). Both work.
- **A missing `reflect` is silent.** A catch-all `template <typename T, typename V> void reflect(T&, V&) {}`
  (`ComponentRegistry.hpp:16-17`) is chosen when no better match exists: the component then has no properties. It
  saves empty, never replicates, shows nothing in an inspector, and the compiler says nothing (ARCH-103).
- **Call it unqualified.** To reflect a value by hand, write `reflect(value, visitor)`. A qualified
  `Brezel::reflect(value, visitor)` turns off argument-dependent lookup: when the component's `reflect` is not in
  namespace `Brezel`, the catch-all is chosen and nothing is visited (checked 2026-11-11: a binary encode wrote the
  12-byte header and no property). Through the registry, use `info.reflect(entity, visitor)`.
- **Property names** are `StringID`s. A string literal converts through the implicit `StringID(const char*)`
  constructor, which interns the string under a global mutex on every call (section 11.1).
- **Order matters** for the binary format (section 6.3): both ends must visit the same properties in the same order.
- **Actions.** `v.visit_action("name", fn)` exposes a callable (the console's `call` verb, section 7.2).
- **Lists.** `VectorAccessor<T>` (`Visitor.hpp:87-107`) reflects a `std::vector<T>` of `int`, `float`, `bool`,
  `std::string` or `StringID`. Two known defects:
  - **XML loses list values (BUG-155).** The XML loader visits each element without tags
    (`XmlDeserializer.hpp:180`), and the load visitors skip a property without `Persistent`. The list comes back with
    the right size and default values (a round trip of `{1.5, 2.5, 3.5}` loaded `{0, 0, 0}`, checked 2026-11-11). The
    binary format is not affected. Until it is fixed, write a `VectorAccessorBase` whose `visit_element` passes its own
    tags (`{Tag::Persistent, ...}`) to `visit_property`.
  - **Struct elements.** For any other element type `VectorAccessor` calls a member `m_vec[i].reflect(visitor)`
    (`Visitor.hpp:104`), not the free function, and no visitor opens a node per element: the XML writes every
    element's fields side by side and reads them back as copies of element 0 (ARCH-103). Flatten such a list into a
    custom `VectorAccessorBase`.

### 4.2 Property Tags (`Tag::`)
Tags are declared in `Visitor.hpp:16-24`. Brezel itself reads only two of them.

| Tag | Stated meaning | Read by Brezel |
| :-- | :-- | :-- |
| `Persistent` | saved to and loaded from XML | yes: XML save and load skip a property without it; `BinaryPropertyFilter::PersistentOnly` |
| `Networked` | replicated | yes: `BinaryPropertyFilter::NetworkedOnly` (the header comment still says "Reserved") |
| `ReadOnly` | the user may not change it | no: the console's `set` ignores it. An application inspector may honour it |
| `Hidden` | not shown | no. An application inspector may honour it |
| `CommandStack` | undoable | no: nothing pushes a command for it (section 5.3); only the helper `hasCommandStack()` tests it |
| `Load_Critical`, `Load_Warn` | fail or warn when missing on load | no: the XML loader reports every missing property as `Warning` (for example `XmlDeserializer.hpp:66-80`) |

`ComponentVisitor` has the helpers `isPersistent`, `isNetworked` and `hasCommandStack` (`Visitor.hpp:68-83`).

### 4.3 Component Registration & Flags
A component must be registered before it can be created by name, saved, loaded, cloned or replicated. The save string
becomes the XML element name and the binary type hash.
```cpp
using namespace Brezel;

ComponentRegistry::registerComponent<SamplerConfig>("SamplerConfig", {
    .displayName = "Sampler",
    .category    = "Acquisition",
    .flags       = ComponentRegistry::ComponentFlag::None
});

// A tag: presence is its meaning. It needs one field (see below) and says it is a tag.
struct ArchivedTag { bool present{true}; };
ComponentRegistry::registerComponent<ArchivedTag>("ArchivedTag", { .category = "Tags", .isTag = 1 });

// Runtime-only state (channels, handles): visible to visitors, skipped by the XML save
ComponentRegistry::registerComponent<StreamComponent<Sample>>("StreamComponent_Sample",
                                                             ComponentRegistry::ComponentFlag::Transient);

// Flags and authority without the options struct
ComponentRegistry::registerComponent<StatusComponent>("StatusComponent",
    ComponentRegistry::ComponentFlag::Networked,
    ComponentRegistry::ReplicationAuthority::ClientAuthoritative);

// Flags compose: ComponentFlag::Transient | ComponentFlag::Networked

// enum class ReplicationAuthority : uint8_t {        // ComponentRegistry.hpp:113-117
//     LocalOnly           = 0, // never replicated
//     ServerAuthoritative = 1, // the server owns it; replicated server -> client
//     ClientAuthoritative = 2  // the client owns it; replicated client -> server
// };

// Optional custom drawer for an application inspector; kept when the type is registered again
ComponentRegistry::setCustomDrawer<SamplerConfig>([](Brezel::Entity entity) { return drawSampler(entity); });

// Lifecycle hooks (which paths run them: section 4.5)
ComponentRegistry::registerComponent<Outline>("Outline", {
    .displayName = "Outline",
    .category    = "Geometry",
    .flags       = ComponentRegistry::ComponentFlag::Networked,
    .onUpdated   = [](Brezel::Entity e) { e.get<Outline>().recomputeBounds(); },
    .onCreated   = [](Brezel::Entity e) { /* first attachment */ },
    .onRemoved   = [](Brezel::Entity e) { /* runs after the component is gone */ }
});
```
- **Overloads** (`ComponentRegistry.hpp:260`, `:324`, `:331`): `(name, ComponentOptions)`, `(name, flags)` and
  `(name, flags, authority)`.
- **Startup only.** The registry is two global maps with no lock (`componentInfoById` keyed by the EnTT type hash,
  `componentInfoByTypeName` keyed by the save string, `ComponentRegistry.hpp:149-150`). Register every type at
  startup, before another thread uses the registry.
- **Registering again** replaces the entry and keeps the custom drawer (`:291-294`).
- **Order with the mutation handlers.** A type registered after `registerStandardMutationHandlers` gets its
  `SetComponent<T>` handler connected at once (`:316-318`).
- **`setCustomDrawer<T>` before `registerComponent<T>`** creates an entry with no `reflect` function; if the type is
  never registered, visiting an entity that has it throws `std::bad_function_call` (ARCH-103).
- **Tags must not be empty.** `isTag` defaults to "category is `Tags`, or `std::is_empty_v<T>`" (`:273-277`). An empty
  struct does not compile: the registry's `reflect` wrapper calls `entity.try_get<T>()` (`:297`), which EnTT rejects
  for an empty type (checked 2026-11-11). Give a tag one field and set `.category = "Tags"` or `.isTag = 1`.
- **Defaults.** `displayName` defaults to the save string; `category` to `"Tags"` for a tag and `"General"` otherwise
  (`:279-289`); `flags` to `None`; `authority` to `LocalOnly`.

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
Rules (`ComponentRegistry.hpp:163-187`):
- The first rule that answers wins, in registration order; with no rule registered `authorityFor` returns
  `info.authority`, so a project that registers none behaves exactly as with per-type authority.
- Rules are registered at startup, on the thread that registers components, before any replication starts. The list
  is read-only afterwards, which is why `authorityFor` takes no lock.
- `authorityFor` is called where the caller already has world access to the entity (holds a world lock, or is the
  world's single writer); it takes none itself.
- A rule reads only the entity's own components: no other entity, no global or mutable state, no side effects.

Guard on the receiving side (`acceptsRemoteDelta`, `ComponentRegistry.hpp:200-205`): the code that applies a delta from
a peer asks whether the sender may write that component on that entity, passing the sender's authority
(`ServerAuthoritative` for a delta from the server, `ClientAuthoritative` for one from a client):
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
application marks the local mirror of an entity hosted by a remote client with `RemoteMirrorTag`
(`CoreComponents.hpp:33`; where it binds the mirror, on the world's single writer; runtime state, not registered), and
local authoring paths ask before editing (`ComponentRegistry.hpp:216-232`):
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
per-entity rules, the receiving guard) and this one local guard. Still to design (Stacato O-15, ARCH-91): a local-edit
policy for every component and every standard handler; edits made on the side that does not own a component routed to
the owner and applied there, so a remote edit (a rename from the server) works instead of being refused; the node's
role (server, client, standalone) known to the framework; how ownership is declared (project, binding, runtime) and
transferred; conflicts after a disconnection; what each side persists. `RemoteMirrorTag` and `acceptsLocalEdit()` are
interim and will be replaced by that model.

### 4.4 Entity Cloning & Duplication
Two helpers copy an entity (`ComponentRegistry.hpp:397-444`):
```cpp
Brezel::Entity copy  = sourceEntity.clone("Copy Name");                            // 1. Entity API
Brezel::Entity copy2 = Brezel::ComponentRegistry::cloneEntity(sourceEntity, "");   // 2. registry helper
```
What they do:
- Create a new EnTT entity with a new UUID, a fresh `IdentityComponent` (`name` and `displayName` both set to the new
  name, or `"<source name> (Copy)"` when it is empty; not sanitized) and an empty `HierarchyComponent`. The copy is a
  top-level entity, even if the source has a parent; children are not copied.
- Copy every registered component the source has, except `IdentityComponent`, `HierarchyComponent`, `Transient` types
  and types that are not copy-constructible.

What they do not do: register the UUID in the `Project` (`getEntityByUUID` and references do not find the copy), run
`onCreated`, fire `EntitySpawned`, or take a lock. **Call them only on the writer thread or while holding `write()`.**
From any other thread push `Brezel::Mutations::CloneEntity{ sourceUuid, sourceHandle, newUuid, newName }`
(`StandardMutations.hpp:61-74`) instead. Brezel registers no handler for it: the host's handler, applied inside the
writer's lock, creates the clone through `Project::createEntityWithUUID` (so it is in the UUID map), copies the
registered, non-transient components, runs their `onCreated` hooks, fires `Events::EntitySpawned` and refuses the
sources its policy protects. The producer can pass its own `newUuid` to find the clone once the mutation has been
applied; 0 lets the handler generate one.

### 4.5 Component Authoring & Automation Quick Reference
This section is the reference for writing a component.

#### 1. What a component must provide
1. A plain struct whose reflected fields have supported types (section 3), with at least one field, default- and
   copy-constructible.
2. A free function `reflect(T&, V&)` in the component's namespace or in `Brezel` (section 4.1). Tag every field that
   must be saved `Persistent`, every field that must be replicated `Networked`.
3. One `registerComponent<T>("Name", ...)` call at startup, before other threads start (section 4.3). A type that is
   replicated also needs `ComponentFlag::Networked`: a binary encode with the `NetworkedOnly` filter skips a registered
   type without it and writes nothing, whatever its property tags (section 6.3).

```cpp
struct LimitConfig {
    float maxRate{10.0f};
    int retries{3};
    bool enabled{true};
};

template <typename V>
void reflect(LimitConfig& c, V& v) {
    v.visit_property("maxRate", c.maxRate, {Brezel::Tag::Persistent, Brezel::Tag::Networked});
    v.visit_property("retries", c.retries, {Brezel::Tag::Persistent, Brezel::Tag::Networked});
    v.visit_property("enabled", c.enabled, {Brezel::Tag::Persistent, Brezel::Tag::Networked});
}

Brezel::ComponentRegistry::registerComponent<LimitConfig>("LimitConfig",
                                                          Brezel::ComponentRegistry::ComponentFlag::Networked);
```

#### 2. What Brezel does for every registered type
- **Type-erased operations** in `ComponentTypeInfo` (`ComponentRegistry.hpp:130-147`): `createComponent`
  (`add<T>()`), `removeComponent`, `hasComponent`, `copyComponent`, and `reflect(entity, visitor)`, so code can work
  with a component known only by its save string.
- **Mutations by name**: `AddComponent`, `RemoveComponent` and `UpdateComponent` (binary payload) find the type by its
  save string; `SetComponent<T>` carries a typed value (section 9.10).
- **Serialization**: XML save and load (section 6), binary encode and decode (section 6.3).
- **Cloning**: `clone()` and `cloneEntity()` copy it (section 4.4).
- **Tag detection**: `isTag` from the category (section 4.3).

#### 3. What a component can register optionally
`ComponentOptions` (`ComponentRegistry.hpp:119-128`):
- `displayName`: the label in menus. Default: the save string.
- `category`: the group in menus (`"Tags"` marks a tag). Default: `"Tags"` for a tag, `"General"` otherwise.
- `flags` (`ComponentRegistry.hpp:77-81`):
  - `ComponentFlag::None` (default).
  - `ComponentFlag::Transient`: skipped by the XML save and by `clone()`, and by a binary encode with the
    `PersistentOnly` filter. For runtime-only state: channels, handles, sockets.
  - `ComponentFlag::Networked`: encoded by a binary encode with the `NetworkedOnly` filter.
  - There is no `Hidden` flag; `Tag::Hidden` is a property tag (section 4.2).
- `authority`: `LocalOnly` (default), `ServerAuthoritative`, `ClientAuthoritative` (section 4.3).
- `isTag`: -1 detects (default), 0 or 1 forces it.
- `onCreated`, `onUpdated`, `onRemoved`: hooks taking the `Entity` (table below).
- A custom drawer: `setCustomDrawer<T>(fn)` (section 4.3). Brezel stores it; the application's inspector calls it.

#### 4. Which path runs which hook and event
| Path | `onCreated` | `onUpdated` | `onRemoved` | Event |
| :-- | :-- | :-- | :-- | :-- |
| `AddComponent` mutation | yes, when it creates the component | no | no | none |
| `UpdateComponent` mutation | yes, when it creates the component | yes, even after a failed decode | no | `ComponentChanged` |
| `SetComponent<T>` mutation | no | yes | no | `ComponentChanged` (registered types only) |
| `RemoveComponent` mutation | no | no | yes, after the removal | none |
| `CreateEntity` mutation | no | no | no | `EntitySpawned` |
| `DestroyEntity` mutation | no | no | no | `EntityDestroyed` |
| `ClearProject`, `RenameEntity` mutations | no | no | no | none |
| `Entity::add`, `add_or_replace`, `remove`, a field write through `get<T>()` | no | no | no | none |
| XML load, `clone()`, `cloneEntity()` | no | no | no | none |
| `CloneEntity` mutation | the host's handler decides | | | the host's handler decides |

Sources: `StandardMutations.hpp:194-329`, `ComponentRegistry.hpp:239-254`, `:366-380`, `:397-425`. Events are
triggered only when the project has a dispatcher (`Project::setDispatcher`, set by
`registerStandardMutationHandlers`). `onRemoved` receives the entity after the component is gone, so it cannot read
the old value.

#### 5. Rules for hooks
- A hook runs on the thread that applies the mutation, inside that thread's `write()` scope (or on the writer thread).
- A hook must not call `project.read()` or `project.write()`: the lock is not recursive and the thread would deadlock
  (section 8.1). It uses the `Entity` it is given.
- A hook that must reach an RT loop writes a payload into a channel (section 8.3); it never shares the component.

---

## 5. State Mutation & The Command Stack

### 5.1 Changing a component
There are two ways, and they differ in what runs.
- **Directly**, on the writer thread or while holding `write()`: `entity.add<T>(value)`, `entity.get<T>().field = x`.
  No hook runs and no event fires.
- **Through a mutation**, from any thread: build a mutation (section 9.10), wrap it in an `AnyMutation`
  (section 9.4), push it into the host's `MutationQueue` (section 9.3). The writer drains the queue inside `write()`
  and calls `execute(dispatcher)`; the handler applies the change and runs the hooks and events of section 4.5.

Brezel provides the mutations, the envelope, the queue and the handlers. The host provides the writer loop:
```cpp
// Host setup, once
Brezel::Project project("Main");
Brezel::Dispatcher dispatcher;
Brezel::Mutations::registerStandardMutationHandlers(project, dispatcher);
Brezel::MutationQueue<Brezel::AnyMutation> mutations;

// Any thread
mutations.push(Brezel::AnyMutation(Brezel::Mutations::SetComponent<LimitConfig>(uuid, LimitConfig{5.0f, 2, true})));

// The writer thread, once per cycle
auto batch = mutations.drain();
{
    auto w = project.write();          // the handlers run inside this scope
    for (auto& m : batch) m.execute(dispatcher);
}
```

### 5.2 EnTT change callbacks
Brezel's hooks (`onCreated`, `onUpdated`, `onRemoved`) are its own; Brezel does not use EnTT signals. The EnTT
registry's signals are still reachable through the write guard, for example
`project.write()->on_update<LimitConfig>().connect<&f>()`. `Entity::add` and `add_or_replace` both call EnTT's
`emplace_or_replace` (`Entity.hpp:159-163`), which fires EnTT's `on_construct` or `on_update`. A field written through
`get<T>()` fires nothing until `write()->patch<T>(entity.handle().entity())`.

### 5.3 The command stack (undo/redo)
- `Command` (`Command/Command.hpp:6-12`): an interface with `execute()`, `undo()` and `getDescription()`.
- `CommandStack` (`Command/CommandStack.hpp:8-39`): `pushAndExecute(std::unique_ptr<Command>)` runs the command, keeps
  it for undo and clears the redo list; `undo()` and `redo()` move one command between the lists. No size limit, no
  lock, not thread-safe.
- Each `Project` owns one: `project.getStack()` (`Project.hpp:191`), also stored in the registry context as
  `CommandStack*` (`Project.hpp:166`).

There is no ready-made command. `ValueChangeCommand`, which earlier versions of this guide described, does not exist:
`src/main.cpp:2` includes a header that was never committed, which is why the standalone testbed does not build
(`README.md`). Nothing in Brezel pushes a command: `Tag::CommandStack` is not read, and the console's `set` writes
without one (section 7.2). An application that wants undo writes its own `Command` classes. A command that changes a
component follows section 5.1 like any other code: from a thread other than the writer, its `execute()` and `undo()`
push mutations. Stacato does not use the command stack (Stacato D-19).

---

## 6. Serialization

Brezel serializes through the reflection visitors: XML (tinyxml2) for files, a packed binary format for messages.
Neither format carries a schema version.

### 6.1 Saving & Loading
Functions:
- `Xml::saveProject(project, path)` (`XmlSerializer.hpp:141-151`): writes a `<Project Name="...">` root and every
  top-level entity with its children. Returns false if the file cannot be written. `Xml::saveProjectToNode(project,
  node, doc)` writes the entities under an existing node.
- `Xml::loadProject(project, path, report)` (`XmlDeserializer.hpp:303-329`): reads the file into `project`. It adds
  entities; it does not clear the project first. It returns false only when the file cannot be read or parsed; every
  other problem goes into the report. It does not relink references (section 2.3). `Xml::loadProjectFromNode` reads
  the entities under a node.
- `Application::saveProject(project, path)` and `Application::loadProject(path)` (`App/Application.hpp:40-67`): the
  same with a policy (section 12).

The load report (`DeserializationValidation.hpp:10-58`) collects `ValidationError`s with a `Severity` (`Valid`,
`Warning`, `Critical`), the entity path, the component type and a message; `getSeverity()` is the worst one, and
`log()` prints them through spdlog. What the loader reports:
- `Critical`: an entity without a UUID or with UUID 0; a list without a valid `ListSize`, or a negative one; no
  `<Project>` root (and then the loader dereferences the missing element and crashes, `XmlDeserializer.hpp:307-314`,
  ARCH-103).
- `Warning`: a missing property (whatever its `Load_` tag), a missing or empty name or display name, a name changed by
  sanitizing, an unknown component element, and (from the relink pass) a reference that does not resolve.

Two load policies exist, and the caller chooses:
- `Application::loadProject` refuses a file with any `Critical` error (returns `nullptr` and logs), logs warnings and
  keeps the project.
- A caller of `Xml::loadProject` decides itself: it must check `report.getSeverity()`, run `relinkProjectReferences`,
  and decide whether to clear the target project before or after a successful read.

**Locks.** None of these functions takes a lock. Saving reads entities: hold `read()` or be the writer. Loading creates
entities: hold `write()` or be the writer. `Application::loadProject` loads into a new project without either, so in a
build without `NDEBUG` the ECS wall stops it at the first entity (`XmlDeserializer.hpp:220`, checked 2026-11-11); with
`BREZEL_ECS_WALL=count` or in a release build it only counts the accesses.

Load behaviour to know:
- Missing properties keep the component's default value. Unknown elements are skipped with a warning.
- A component element creates the component if the entity does not have it, then reflects into it
  (`ComponentRegistry.hpp:366-380`); no hook runs.
- `ListSize` is trusted up to 2^31 before any element is checked (ARCH-103).
- Lists of plain values come back with default values (BUG-155, section 4.1).

### 6.2 XML Structure
Output of `Xml::saveProject` (checked 2026-11-11):
```xml
<Project Name="Main">
    <Entity Name="sampler_1" DisplayName="Sampler 1" UUID="9681272863961780438">
        <SamplerConfig>
            <rateHz val="100"/>
            <channels val="2"/>
            <source UUID="177571177274479468"/>
            <!--Entity Name: "probe_a"-->
            <weights ListSize="3">
                <E0 val="1.5"/>
                <E1 val="2.5"/>
                <E2 val="3.5"/>
            </weights>
        </SamplerConfig>
        <Children/>
    </Entity>
</Project>
```
- `Name`, `DisplayName` and `UUID` are attributes of `<Entity>`. `reflectEntityComponents` skips
  `IdentityComponent` (`ComponentRegistry.hpp:357`), and the loader skips an `<IdentityComponent>` element
  (`XmlDeserializer.hpp:274`).
- Each component is an element named by its save string; each property an element named by its label, with the value
  in `val`. An `EntityReference` stores `UUID` and is followed by a comment naming the target. A list stores
  `ListSize` and elements `E0`, `E1`, ... (`XmlCommon.hpp:7-11`).
- Children are written inside `<Children>`. `Transient` components and properties without `Persistent` are not written.

### 6.3 High-Speed Binary Serialization (`BinaryVisitor.hpp`)
`BinarySerializer` and `BinaryDeserializer` encode reflected components for messages and caches.
```cpp
#include "Brezel/Serialization/BinaryVisitor.hpp"

// Encode (the filter picks the properties). Call reflect unqualified (section 4.1).
Brezel::BinarySerializer serializer(Brezel::BinaryPropertyFilter::NetworkedOnly);
if (serializer.beginComponent("LimitConfig")) {
    reflect(limitConfig, serializer);
    serializer.endComponent();
}
std::span<const uint8_t> payload = serializer.getBuffer();   // 21 bytes for the LimitConfig of section 4.5:
                                                             // 8 hash + 4 length + 4 + 4 + 1

// Decode
Brezel::BinaryDeserializer deserializer(payload, Brezel::BinaryPropertyFilter::NetworkedOnly);
if (deserializer.beginComponent("LimitConfig")) {
    reflect(targetConfig, deserializer);
    deserializer.endComponent();
}
if (deserializer.hasError()) { /* targetConfig may be partly written */ }
```
Format (`BinaryVisitor.hpp:70-170`): per component, the save string's 64-bit hash, a 32-bit payload length, then the
properties in `reflect` order: `int` as 32-bit, `float` as 4 bytes, `bool` as one byte, `std::string` as a 32-bit
length and the bytes, `StringID` as its 64-bit hash, `EntityReference` as its 64-bit UUID, a list as a 32-bit count and
the elements. Values are copied in the machine's byte order; there is no conversion. There are no field tags: both ends
must run the same `reflect` with the same filter.

Filters (`BinaryPropertyFilter`, `:22-26`): `All`, `NetworkedOnly` (properties tagged `Networked`; a registered type
without the `Networked` flag is skipped whole by the serializer, `:76-78`), `PersistentOnly` (properties tagged
`Persistent`; a `Transient` type is skipped whole). A skipped type gives an empty buffer: `beginComponent` returns
false and writes no header, and the decoder's `beginComponent` on that buffer sets `hasError()`. The 21 bytes above
need the `Networked` flag of the section 4.5 registration; registered without it, the same code gives 0 bytes (checked
2026-11-11). The serializer checks flags only for a registered type: an unregistered one is always encoded. The
decoder checks no flags.

Decoder checks and limits (ARCH-103):
- Every scalar read and every string length is checked against the buffer; a short buffer sets `hasError()`.
- A wrong type hash skips the component's bytes and `beginComponent` returns false (`:189-216`).
- A list count is not checked against the remaining bytes: the decoder resizes the list to the count first
  (`:286-298`).
- On an error, the fields decoded so far stay written; nothing is rolled back.
- The `UpdateComponent` handler (section 9.10) retries a payload whose header does not match as raw properties without
  a header, does not check `hasError()`, and runs `onUpdated` anyway (`StandardMutations.hpp:283-296`).

---

## 7. Command Line Interface (CLI)

`Console` (`Console/Console.hpp`) reads and writes component properties by path, through the reflection visitors.
`CLI::runInteractive(console)` (`Console/InteractiveConsole.hpp:51-71`) wraps it in a GNU readline prompt with
completion; `exit` or `quit` leaves it. Stacato does not use the console.

### 7.1 Path Resolution
A path is `EntityPath.Component.property`. `EntityPath` is entity names separated by `/` (`Project::getEntityByPath`,
`Project.hpp:269-305`), for example `rig/sampler_1.SamplerConfig.rateHz`. The first name is matched against every
entity with that name, at any depth, and the first match wins; the following names are matched among the children.

### 7.2 Core Commands
`console.exec(line)` runs one line; output goes to the `onOutput` signal (`Signal<std::string>`, section 11.2).
- `get <path>`: prints an `int`, `float`, `bool` or `std::string` property. Other types are reported as not found.
- `set <path> <value>`: writes an `int`, `float`, `bool` (`true` or `1`) or `std::string` property.
- `call <path>`: runs an action (`visit_action`, section 4.1).
- `ls <entity path>`: lists the entity's components and children. It needs a path; there is no top-level listing.
- `undo`: calls `project.getStack().undo()`.

There is no `tree`, `redo`, `save` or `load` verb. Completion offers `get`, `set`, `call`, `ls`, `undo`, `exit` and
`quit` (`Console.hpp:77-86`).

Limits (ARCH-103): `set` writes through a raw pointer to the live field (`Console.hpp:335-373`): no lock, no mutation,
no hook, no event, no command, and `ReadOnly` is ignored. The path lookups take no scope, so in a build without
`NDEBUG` the ECS wall stops a console that runs off the writer thread. Run the console on the writer thread, or around
`exec` hold `write()`.

---

## 8. Multi-threading & Synchronization

### 8.1 One lock per project
A `Project` guards its registry with one `std::shared_mutex` (`Project.hpp:349`).
- `project.read()` returns a `ReadAccess` that holds the shared lock: many readers at once.
- `project.write()` returns a `WriteAccess` that holds the exclusive lock.
- The lock is not recursive. A thread that holds `read()` or `write()` and asks for `write()` again deadlocks; so does a
  hook or mutation handler that calls either (it already runs inside `write()`).
- One thread may be named the **writer** (`project.setWriterThread(id)`, `Project.hpp:184`): the thread that applies
  mutations. It may read entities without a scope. It still takes `write()` around its writes, so that a reader never
  sees a half-written component.
- Every other thread reaches entities only inside a `read()` or `write()` scope, and changes them only by pushing a
  mutation (section 5.1).

### 8.2 What is checked
Nothing is checked at compile time. At run time the ECS wall (section 9.11) checks the `Entity` accessors and
`Project::destroyEntity`, `resolveEntity` and `getEntityByPath`: an access off the writer thread without a scope is
counted, located and, in a build without `NDEBUG`, aborts.

Not checked:
- `Entity::handle()` and `Entity::registry()`, the EnTT escape hatches.
- `Project::createEntity`, `restoreEntity`, `createEntityWithUUID`, `clear`, `getEntityByUUID` and `getEntities`:
  they use the registry and the UUID map directly. Call them inside the matching scope.
- `Project::forEachTopLevelEntity` (`Project.hpp:256-264`) walks the registry directly too. Only its parent test is
  checked: `!hierarchy.parent` goes through `Entity::operator bool`, so for an entity that has a parent the wall
  checks that access with the site `Entity.hpp:115`. Call it inside a scope as well.
- Read versus write. `ReadAccess::getEntity` and `wrap` return a mutable `Entity` (`Project.hpp:70-76`); writing
  through it under the shared lock is a data race the wall does not see.
- Global state: the component registry (section 4.3) and `Application`'s project list (section 12) have no lock;
  the `StringID` table has its own mutex.

Stacato tracks the remaining bypasses in its own code as ARCH-18.

### 8.3 Real-time code and components
The RT loop never reads or writes a component (section 1). The pattern:
1. The channel (a `TripleBuffer<T>`, a `RingBuffer<T, N>`) lives at a fixed address, outside EnTT's storage, because
   EnTT moves components when entities are destroyed. A `StreamComponent` or `QueueComponent` (sections 9.6 and 9.7)
   holds a `shared_ptr` or a raw pointer to it, so that NRT code can find the channel from the entity.
2. NRT code takes the raw channel pointer once, inside a scope or on the writer thread, and hands it to the RT loop
   when it starts. The RT loop keeps only that pointer.
3. Exactly one thread writes a channel and exactly one thread reads it.
4. The owner keeps the channel alive until the RT loop has stopped using it. Removing the component or destroying the
   entity releases the component's `shared_ptr`; if that was the last owner, the channel is freed while the RT loop
   may still hold its pointer.
5. Payloads are trivially copyable.

### 8.4 Where an application's thread rules live
Brezel names no threads. Each application states its own: which thread is the writer, which threads exist, and which
channel connects which pair. For Stacato that is `docs/reference/THREADING_AND_ECS_CONTRACT.md` in the Stacato
repository (section 3 lists the channels).

---

## 9. Framework Concurrency & Air-Gap Primitives

Generic concurrency primitives in `include/Brezel/Concurrency/` and `include/Brezel/Core/`.

### 9.1 Lock-Free Bridges
- **`Brezel::TripleBuffer<T>`** (`TripleBuffer.hpp`): despite its name, a lock-free, wait-free Simpson four-slot buffer
  (two pairs of two slots, `T data_[2][2]`) for passing the latest value between two threads without blocking. Latest
  value wins; older unread values are overwritten.
  - **One writer, one reader.** Exactly one thread calls `write()` (several producers are allowed only when one mutex
    serializes them) and exactly one thread calls `read()`. A second unsynchronized writer breaks the algorithm.
  - **Memory ordering (BUG-22, 2026-10-20).** The reader's store of `readingPair_` followed by its load of
    `slotInPair_`, and the writer's stores followed by its next load of `readingPair_`, form a store-buffering pattern
    that release/acquire does not order. `write()` starts with a `seq_cst` fence and `read()` has one between the
    `readingPair_` store and the `slotInPair_` load. Without them two back-to-back writes tore about one read in 100,000
    on an arm64 Mac; with them none (stress test in Stacato `tests/test_stage_ecs.cpp`).
  - **Writer's load of `readingPair_` is acquire (BUG-90, 2026-10-23).** It pairs with the reader's release store,
    which follows the reader's copy of the previous value, so that copy happens before the writer reuses the slot. The
    fence alone (placed before the load) did not make that formal; ThreadSanitizer reported it.
  - **`hasNew()` / `read()`.** `read()` clears the flag (atomic exchange) before it chooses the pair, so a write
    landing during the copy keeps its flag and the next `if (hasNew()) read()` picks it up. `read()` always returns the
    latest value, new or not.
  - **`read()` always returns `true`; it is not a freshness test (BUG-55).** Before the first `write()` it copies the
    value-initialised slot (`T{}`), and after that it returns the last value again until a new one arrives.
    `if (buffer.read(v))` therefore accepts a buffer nobody has written yet. Freshness comes from the payload or the
    flag: give the payload a producer timestamp that is never zero once written and treat zero as "nothing yet", or
    test `hasNew()` before `read()` when only new values matter (single reader only, since `read()` clears the flag).
  - **`readLatest()`** is a peek without the reader handshake: it does not clear the flag and may tear against a
    concurrent write (ARCH-19). Use it only for display, never for control data.
  - Payloads must be trivially copyable and default-constructible. `TripleBuffer` does not check this itself;
    `StreamComponent` does (`StreamComponent.hpp:28`).
- **`Brezel::RingBuffer<T, Capacity>`** (`RingBuffer.hpp`): fixed-capacity lock-free FIFO for one producer thread and
  one consumer thread.
  - `push()` returns false when full, `pop()` false when empty. One slot always stays empty, so `Capacity - 1` items
    fit (a `RingBuffer<T, 64>` holds 63). Any capacity of 2 or more works (indices wrap by modulo); `RingBuffer<T, 1>` compiles but holds nothing, so every `push()` fails.
  - `push()` loads `tail_` with acquire (BUG-90, 2026-10-23), pairing with `pop()`'s release store, so the consumer's
    copy of a slot happens before the producer overwrites it.
  - `size()` and `empty()` are estimates while the other side runs. `clear()` stores both indices with relaxed
    ordering and is safe only while neither side runs (`RingBuffer.hpp:68-71`, ARCH-103); to discard, the consumer pops
    until empty.
  - The header comment asks for trivially copyable payloads; `QueueComponent` checks it.
- **`Brezel::TriggeredCapture<T, Capacity>`** (`TriggeredCapture.hpp`, 2026-11-03): an oscilloscope's single-shot
  capture in software. One writer thread calls `record(sample, trigger)` once per cycle (wait-free, no allocation):
  while armed, samples go into an inline ring of `Capacity`; a trigger (the sample's flag, or `requestTrigger()`)
  starts a post-trigger count, after which the capture freezes with the history before the trigger, the trigger sample
  and the samples after it. One controller thread calls `arm(postTriggerSamples)` (clamped to `Capacity - 1`, so the
  trigger sample is always kept), `disarm()` and `forEachFrozen(f, &triggerIndex)`; arm and disarm are requests the
  writer applies at its next `record()`, and only the writer touches the ring and its indices, so reading while
  `FROZEN` never races a write. `freezeCount()` tells the controller a new capture is ready; `state()` is readable from
  any thread. A trigger requested before an arm does not fire it. `T` must be trivially copyable and `Capacity` at
  least 2 (static asserts). First user: Stacato SAF-28 (D-210); tested in Stacato `tests/test_fieldbus_policy.cpp`.
- **`Brezel::StallWatchdog`** (`StallWatchdog.hpp`, 2026-10-23): detects a stalled thread from outside it. A watched
  thread calls `beat(id)` at least once per timeout (wait-free, one atomic store); the watchdog's own thread checks
  every interval (default 100 ms) and calls the listener once when an entry goes stalled and once when it beats
  again. `watch()` counts as the first beat, so a thread that never beats is caught; a thread that stops on purpose
  calls `unwatch()`. Up to 16 entries. `watch()` and `unwatch()` take a mutex. The listener runs on the watchdog
  thread with no watchdog lock held and must not block. `check(now)` drives it by hand in tests.

### 9.2 Periodic Loop (`Brezel::PeriodicLoop`)
A background thread that calls a function at a fixed rate (`PeriodicLoop.hpp`):
```cpp
Brezel::PeriodicLoop loop("Worker");
loop.start(50.0, [](double dt) {
    // runs about every 20 ms; dt is the measured time since the previous call, in seconds
});
loop.stop();
```
- Wake-up times are absolute (`next += period`), so the rate does not drift with the callback's duration. If the loop
  falls more than one period behind, the next wake-up is reset to now plus one period instead of running a burst
  (`:73-77`). It uses `std::chrono::steady_clock` and `sleep_until`; it is not a real-time loop.
- `start()` names the thread (`setThreadName`, section 9.9; `:47`); a second `start()` while running does nothing.
  `stop()` joins the thread; the destructor calls it.
- Limits (ARCH-103): a rate of 0 or less is not rejected; an exception thrown by the callback ends the process; calling
  `stop()` from inside the callback joins the thread itself and throws `std::system_error`.

### 9.3 Asynchronous Mutation Queue (`Brezel::MutationQueue<T>`)
A queue for many producer threads and one consumer (`MutationQueue.hpp`):
- `push(m)` takes a mutex and appends to a `std::vector` (`:25-36`).
- `drain()` swaps that vector with an empty one under the mutex and returns all pending items in push order
  (`:42-49`). The swap is O(1); the producers then grow a new vector, so the queue allocates.
- Not for an RT thread (mutex and allocation).
- `MutationQueue` carries state changes into the ECS (section 5.1); `CommandStack` keeps an undo history (section 5.3).
  They are unrelated.

### 9.4 Type-Erased Mutation Envelope (`Brezel::AnyMutation`)
A fixed-size envelope that carries any mutation struct, so subsystems define their own mutations without a central
list of types (`AnyMutation.hpp`).
- Storage: 512 bytes inline (`AnyMutation::STORAGE_SIZE`, `:23`; raised from 256 on 2026-10-20, Stacato D-76), aligned
  to 64. The whole object is 576 bytes (measured 2026-11-11: 512 of storage and four function pointers, rounded up to
  the 64-byte alignment).
- Compile-time checks on the wrapped type `T` (`:58-61`): `sizeof(T) <= STORAGE_SIZE` and `alignof(T) <= 64`. Mutation
  headers assert against `Brezel::AnyMutation::STORAGE_SIZE`, not a literal.
- A trivially copyable `T` is copied with `memcpy` and needs no destructor. A `T` with a non-trivial copy, move or
  destructor (for example one holding a `std::shared_ptr` to bulk data) gets them through type-erased function
  pointers (`:69-85`). The envelope itself never allocates.
- `execute(dispatcher)` calls `dispatcher.trigger(m)` with the typed struct (`:65-67`, `:88-92`): the connected
  handlers run at once, on the calling thread. A type with no connected handler is dropped without a message.

### 9.5 Event Dispatcher (`Brezel::Dispatcher`)
`using Dispatcher = entt::dispatcher;` (`Core/Dispatcher.hpp:11`). Application code names `Brezel::Dispatcher` instead
of the EnTT type, but the header includes `<entt/entt.hpp>`, so including it still brings in EnTT. `trigger()` runs the
handlers at once; EnTT's `enqueue()` and `update()` queue events instead. Brezel's standard handlers are connected with
`dispatcher.sink<T>().connect<...>(project)` (section 9.10).

### 9.6 Entity Stream Component (`Brezel::StreamComponent<T, Tag = void>`)
An ECS component that points to a `TripleBuffer<T>` at a fixed address (`StreamComponent.hpp:26-56`), so that code
holding the entity can find the channel (section 8.3).
```cpp
template <typename T, typename Tag = void>
struct StreamComponent {
    std::shared_ptr<TripleBuffer<T>> buffer;  // owning
    TripleBuffer<T>* rawBuffer{nullptr};      // non-owning, used when buffer is empty
    uint64_t lastTimestampNs{0};
    uint32_t statusWord{0};
    bool isConnected{false};

    TripleBuffer<T>* get() const;             // buffer, else rawBuffer
    TripleBuffer<T>* getBuffer() const;       // same as get()
    bool isValid() const;                     // get() != nullptr
};

// Two streams of the same payload type on one entity, told apart by the Tag parameter
struct PrimaryLinkTag {};
struct BackupLinkTag {};
entity.add<Brezel::StreamComponent<Telemetry, PrimaryLinkTag>>(std::move(streamA));
entity.add<Brezel::StreamComponent<Telemetry, BackupLinkTag>>(std::move(streamB));
```
- `T` must be trivially copyable (`static_assert`, `:28`).
- `reflect` shows `isConnected`, `statusWord` (as an `int`) and `lastTimestampSec` (a `float` computed from
  `lastTimestampNs`), all `ReadOnly` (`:49-56`). The header does not say which clock `lastTimestampNs` uses. A
  `float` keeps 24 bits of precision, so the shown value is coarse (ARCH-103): steps of about 2 minutes for nanoseconds
  since the Unix epoch; for a since-boot clock, about 1 ms after 2.3 hours of uptime and about 8 ms after a day.
- Register it with `ComponentFlag::Transient`: a pointer is not saved.
- The `AttachStream<T, Tag>` mutation sets `rawBuffer` only (section 9.10).

### 9.7 Entity Queue Component (`Brezel::QueueComponent<T, Capacity = 64, Tag = void>`)
The same for a `RingBuffer<T, Capacity>` (`QueueComponent.hpp:27-86`): lossless, in-order delivery of discrete events
across a thread boundary, one producer and one consumer.
- Holds `std::shared_ptr<RingBuffer<T, Capacity>> queue` or a non-owning `rawQueue`; `get()` (and its alias
  `getQueue()`), `isValid()`, and pass-through `push`, `pop`, `empty` and `size` that return false, true or 0 when no
  queue is attached.
- `T` must be trivially copyable and `Capacity` above 0 (static asserts). `Capacity - 1` items fit (section 9.1).
- A `Tag` tells apart two queues of the same payload type (`QueueComponent<Event, 64, UrgentTag>`).
- `reflect` and registration as for `StreamComponent`.

### 9.8 Project Thread Safety & RAII Registry Accessors (`Brezel::Project`)
`Project` (`Core/Project.hpp`) owns the EnTT registry, the UUID index, the command stack, the ECS wall and an optional
dispatcher pointer. The registry is private; code reaches it through the guards or through `Entity` handles.
- **`read()`** returns a `ReadAccess` holding a shared lock (`:28-82`): `view<Components...>()` and
  `view<...>(entt::exclude<...>)` over const components, `get<T>(e)`, `try_get<T>(e)`, `all_of`, `any_of`, `valid`,
  `registry()`, `operator->`, and `wrap(e)` / `getEntity(e)` (an `Entity` handle; mutable, see section 8.2).
- **`write()`** returns a `WriteAccess` holding the exclusive lock (`:85-161`): mutable views, `get`, `try_get`,
  `emplace`, `emplace_or_replace`, `remove`, `destroy`, `clear`, `wrap`, `getEntity`.
- Entities: `createEntity(displayName)` (new UUID, sanitized name), `createEntityWithUUID(uuid, displayName)` (0
  generates one), `restoreEntity(name, displayName, uuid)` (name taken as given), `destroyEntity(entity)`, `clear()`
  (all entities and the UUID index).
- Lookups: `getEntityByUUID(uuid)` (an invalid `Entity` when unknown), `resolveEntity(uuid, fallback)` (by UUID, else
  the fallback handle if it is valid and belongs to this project), `getEntities()` (the UUID map),
  `forEachTopLevelEntity(f, args...)` (entities without a parent), `getEntityByPath(path)` (section 7.1).
- `setDispatcher(d)` and `getDispatcher()` (`:322-325`): the dispatcher the standard handlers trigger events on;
  `nullptr` means no events.
- `setWriterThread(id)` and `ecsWall()`: section 9.11. `getStack()`: section 5.3. `getName()` and the public `m_name`.

None of this is enforced at compile time. Which calls the ECS wall checks at run time and which it does not: section
8.2.

### 9.9 Cross-Platform Thread Naming (`Brezel::setThreadName`)
Located in `Brezel/Concurrency/ThreadUtils.hpp`:
- `Brezel::setThreadName(name)` labels the calling thread for debuggers and profilers: macOS
  `pthread_setname_np(name)` (63 characters), Linux `pthread_setname_np(pthread_self(), name)` (15 characters),
  Windows `SetThreadDescription`. Longer names are cut. `PeriodicLoop::start` and `StallWatchdog` call it.
- `Brezel::makeNonRealtimeThread(avoidCpu)`: makes the calling thread `SCHED_OTHER` (priority 0) with an affinity of
  every configured CPU except `avoidCpu` (negative: all). On Linux a thread inherits its creator's policy, priority and
  CPU set, so a helper started from a real-time thread must call this first or it runs at real-time priority on the
  real-time core. Returns false if a call failed; a no-op returning true on macOS and Windows.

### 9.9b Raw monotonic clock (`Brezel/Foundation/MonotonicRawClock.hpp`)
- `Brezel::monotonicRawNanoseconds()`: nanoseconds of a monotonic clock that no time correction steps or slews. Linux:
  `CLOCK_MONOTONIC_RAW`; `std::chrono::steady_clock` (`CLOCK_MONOTONIC`) never jumps but follows an NTP daemon's rate
  corrections, so a loop paced by it drifts against an external clock while the daemon corrects. macOS and Windows:
  `steady_clock`, already raw there.
- Unspecified epoch: compare its values only with each other. Wait-free, safe on a real-time thread. Use it to pace a
  loop that must stay locked to an external reference clock.

### 9.9c Remote clock estimate (`Brezel/Time/RemoteClockEstimator.hpp`)
- `Brezel::RemoteClockEstimator` estimates the offset of a remote monotonic clock from the local one out of one-way
  samples: each `addSample(remoteNs, localNs)` pairs the sender's clock at sending with the local clock at arrival.
  The offset (`offsetNs()`, local minus remote) is the smallest `localNs - remoteNs` over a sliding window of 5 s
  (`WINDOW_NS`): the fastest arrival carries the least transit, so jitter and bursts of queued samples do not move it,
  and a route that becomes slower for good takes over once the old minimum leaves the window. The result is the offset
  plus the fastest one-way transit (no two-way exchange, no rate estimate).
- `hasEstimate()` is true once the window holds at least 5 samples (`MIN_SAMPLES`) spread over at least 1 s of arrival
  time (`MIN_SPAN_NS`), so one burst alone gives no estimate; `offsetNs()` is 0 without one. `toLocalNs(remoteNs)`
  converts a remote stamp. `reset()` forgets everything: call it when the remote side may have a new clock (a new
  session, a restart).
- `remoteNsFromWrappedUs(remoteUs, localNs)` unwraps a remote stamp sent as a wrapping 32-bit microsecond count (71.6
  minutes) against a local time: it returns the full remote ns nearest the remote time of `localNs`; valid within
  about 35 minutes of it.
- Storage: a ring of 20 buckets of 250 ms keyed by arrival time, each holding its minimum; no allocation, O(20) per
  sample. The window ends at the newest sample, not at the caller's "now": without new samples the last estimate
  stays; a sample older than the window is ignored. After a gap longer than the window the next samples rebuild the
  estimate (no estimate for a second).
- Thread-agnostic, no internal synchronisation: one instance belongs to one thread. Header-only, C++20.
- First user: Stacato, to date samples sent by a remote node (Stacato T3.16, D-129, D-150).

### 9.10 Standard Lifecycle Mutations (`Brezel/Concurrency/StandardMutations.hpp`)
Ready-made mutations and their handlers. All structs except `UpdateComponent` and `SetComponent<T>` are trivially
copyable (`:180-186`). Each handler resolves the target with `project.resolveEntity(uuid, entity)` and does nothing if
it is not found.

| Mutation | What the handler does |
| :-- | :-- |
| `DestroyEntity{uuid, entity}` | `destroyEntity`, then `Events::EntityDestroyed` (`:194-203`). Children and parent links are not touched (section 2.2) |
| `ClearProject{resetState}` | `project.clear()`. No event; `resetState` is unused (`:205-208`) |
| `CreateEntity{uuid, name}` | `createEntityWithUUID` (or `createEntity` when the UUID is 0), then `Events::EntitySpawned` (`:210-220`). The name holds up to 63 characters |
| `CloneEntity{sourceUuid, source, newUuid, newName}` | no Brezel handler; the host connects one (section 4.4) |
| `RenameEntity{uuid, entity, newName}` | sets `name` and `displayName` to `newName`, not sanitized, unless `acceptsLocalRename` refuses. No event (`:222-230`) |
| `SetComponent<T>{uuid, entity, component}` | `add_or_replace<T>(component)`, `onUpdated`, `Events::ComponentChanged` (`ComponentRegistry.hpp:239-254`) |
| `UpdateComponent{uuid, entity, componentName, payload}` | creates the component if missing (`onCreated`), decodes the binary payload with the `All` filter, `onUpdated`, `Events::ComponentChanged` (`StandardMutations.hpp:268-301`; decode limits in section 6.3) |
| `AddComponent{uuid, entity, componentName}` | creates the component if missing, then `onCreated` (`StandardMutations.hpp:316-329`) |
| `RemoveComponent{uuid, entity, componentName}` | removes it, then `onRemoved` (`StandardMutations.hpp:303-314`) |
| `AttachStream<T, Tag>{uuid, entity, rawBuffer}` / `DetachStream<T, Tag>` | adds a `StreamComponent<T, Tag>` with `rawBuffer` set / removes it |
| `AttachQueue<T, Capacity, Tag>{uuid, entity, rawQueue}` / `DetachQueue<...>` | the same for `QueueComponent` |

Registration:
- `registerStandardMutationHandlers(project, dispatcher)` (`:336-355`): calls `project.setDispatcher(&dispatcher)`,
  registers `IdentityComponent`, connects the handlers above (except stream, queue and clone) and the
  `SetComponent<T>` handler of every registered type. It stores the project and dispatcher in two globals
  (`ComponentRegistry::s_activeProject`, `s_activeDispatcher`) so that types registered later get their
  `SetComponent<T>` handler too. With two projects, the last call wins.
- `registerStreamMutationHandlers<T, Tag>(project, dispatcher)` and
  `registerQueueMutationHandlers<T, Capacity, Tag>(project, dispatcher)` (`:360-373`): one call per payload type.

Events (`ComponentRegistry.hpp:21-47`), triggered on the project's dispatcher:
- `Events::ComponentChanged{uuid, entity, componentName}`
- `Events::EntitySpawned{uuid, entity}`
- `Events::EntityDestroyed{uuid}`

Which paths trigger them, and which hooks run: section 4.5.

### 9.11 ECS wall (`Brezel/Core/EcsWall.hpp`)
A runtime check of the one ECS access rule: **a thread touches entity components only while it holds a `read()` or
`write()` scope of the project, or when it is the project's writer thread.**
- **State**: each `Project` owns an `EcsWall`, published in its registry context (`ctx()`, type `EcsWall*`). The wall
  holds the writer thread id (`Project::setWriterThread(id)`, set by whoever runs the single-writer loop, on that
  thread; an empty id clears it). `ReadAccess` / `WriteAccess` record their scope in a thread-local table (up to 8
  projects per thread; a moved guard is counted once).
- **Checked accessors**: `Entity::add`, `add_or_replace` (up to 4 constructor arguments), `remove`, `has`, `try_get`,
  `get`, `isValid`, `operator bool`, `getParent`, `getName`, `getDisplayName`, `getUUID`, `getPath`, `setParent`, and
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
- The wall does not tell a read from a write, and does not check the `Project` calls listed in section 8.2.

---

## 10. Fitting Math (`Brezel/Math/`)

Header-only, C++20, thread-agnostic, no allocation and no storage of the input (Stacato D-73). Doubles throughout.
First user: an optical calibration in Stacato (D-71, D-74 point 6); tested through Stacato's `tests/test_calibration.cpp`
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

---

## 11. Foundation (`Brezel/Foundation/`)

### 11.1 Interned string IDs (`StringID.hpp`)
`Brezel::StringID` is a 64-bit FNV-1a hash of a string, compared in O(1). Component save strings, property labels and
console paths use it.
- `StringID("text")`: the implicit constructor from `const char*` hashes the string and stores it in a global table
  under a global mutex, allocating a `std::string`, on every call (`:43-45`, `:71-74`). Each `visit_property("label",
  ...)` call goes through it (ARCH-103).
- `StringID::from(std::string_view)`: the same for a view (`:49-53`).
- `"text"_sid`: a `consteval` literal (`:89-91`) that only hashes. It does **not** store the string, so `toString()`
  returns `"Unknown_SID_<hash>"` unless the same text was stored through another path (checked 2026-11-11). Use it for
  comparisons, not for names that must be printed or saved.
- `toString()` looks the hash up in the table (under the mutex). `hash()`, `isValid()` (hash not 0), `==`, `!=`, `<`.
  A collision between two strings is not detected.
- `std::hash<StringID>` and an `fmt` formatter (through spdlog's bundled `fmt`) are provided.
- Not for an RT thread (mutex and allocation), except comparing two existing IDs.

### 11.2 Signal (`Signal.hpp`)
`Brezel::Signal<Args...>`: a list of `std::function<void(Args...)>` slots. `connect(slot)` appends; `emit(args...)`
calls every slot in order on the calling thread. There is no disconnect and no lock: connect at setup, on one thread.
The console's `onOutput` is one (section 7.2).

---

## 12. Application (`Brezel/App/Application.hpp`)

`Brezel::Application` is a namespace with global state: a list of owned projects and the index of the active one
(`:16-17`). No lock.
- `createProject(name)`: adds a project and makes it active.
- `closeProject(index)`: removes it; the first remaining project (index 0) becomes active.
- `getActiveProject()`: the active project or `nullptr`.
- `saveProject(project, path)`: `Xml::saveProject` (section 6.1).
- `loadProject(path)`: loads into a new project, relinks references, refuses a file with a `Critical` error (logs the
  report, returns `nullptr`), logs warnings, adds the project and makes it active (`:40-63`). In a build without
  `NDEBUG` the ECS wall stops it (section 6.1).

Stacato does not use `Application`; it owns its `Project` and calls the `Xml::` functions.

---

## 13. Header index

Every public header under `include/Brezel/`. "First" names a header to include before it, because it does not compile
on its own (checked 2026-11-11 by compiling each header alone as C++20).

| Header | What it holds | Section | First |
| :-- | :-- | :-- | :-- |
| `App/Application.hpp` | global project list, load and save with a policy | 12 | `Core/ComponentRegistry.hpp` |
| `Command/Command.hpp` | the undoable command interface | 5.3 | |
| `Command/CommandStack.hpp` | undo and redo lists | 5.3 | |
| `Concurrency/AnyMutation.hpp` | type-erased 512-byte mutation envelope | 9.4 | |
| `Concurrency/MutationQueue.hpp` | many-producer, one-consumer queue with a mutex | 9.3 | |
| `Concurrency/PeriodicLoop.hpp` | fixed-rate background thread | 9.2 | |
| `Concurrency/QueueComponent.hpp` | component pointing to a `RingBuffer` | 9.7 | |
| `Concurrency/RingBuffer.hpp` | one-producer, one-consumer FIFO | 9.1 | |
| `Concurrency/StallWatchdog.hpp` | stalled-thread detector | 9.1 | |
| `Concurrency/StandardMutations.hpp` | standard mutations and their handlers | 9.10 | |
| `Concurrency/StreamComponent.hpp` | component pointing to a `TripleBuffer` | 9.6 | |
| `Concurrency/ThreadUtils.hpp` | `setThreadName`, `makeNonRealtimeThread` | 9.9 | |
| `Concurrency/TriggeredCapture.hpp` | single-shot pre- and post-trigger capture | 9.1 | |
| `Concurrency/TripleBuffer.hpp` | Simpson four-slot latest-value buffer | 9.1 | |
| `Console/Console.hpp` | `get`, `set`, `call`, `ls`, `undo` by path | 7 | `Core/ComponentRegistry.hpp` |
| `Console/InteractiveConsole.hpp` | readline prompt with completion | 7 | `Core/ComponentRegistry.hpp` |
| `Console/SearchVisitor.hpp` | visitor that finds one property or action by name | 7 | |
| `Core/ComponentRegistry.hpp` | registration, type info, authority, cloning, `SetComponent`, events | 4.3, 4.4, 9.10 | |
| `Core/CoreComponents.hpp` | `IdentityComponent`, `HierarchyComponent`, `RemoteMirrorTag` | 2.1, 2.2, 4.3 | `Core/Entity.hpp` (which includes it) |
| `Core/Dispatcher.hpp` | `Brezel::Dispatcher` alias | 9.5 | |
| `Core/EcsWall.hpp` | runtime check of the ECS access rule, waivers | 9.11 | |
| `Core/Entity.hpp` | the entity handle | 2, 9.11 | |
| `Core/EntityReference.hpp` | UUID plus handle reference | 2.3 | |
| `Core/Project.hpp` | registry owner, guards, entity creation and lookup | 9.8 | |
| `Core/UUID.hpp` | 64-bit UUID, FNV-1a derivation | 2.1 | |
| `Core/UUIDProvider.hpp` | random UUID generator | 2.1 | |
| `Foundation/MonotonicRawClock.hpp` | raw monotonic nanoseconds | 9.9b | |
| `Foundation/Signal.hpp` | minimal signal and slots | 11.2 | |
| `Foundation/StringID.hpp` | interned string hash, `_sid` | 11.1 | |
| `Math/CircleFit.hpp` | Kasa circle fit | 10.3 | |
| `Math/LeastSquares.hpp` | dense least squares, Cholesky and QR | 10.1 | |
| `Math/LineFit.hpp` | total least-squares line | 10.2 | |
| `Reflection/Visitor.hpp` | `Tag`, `ComponentVisitor`, `VectorAccessor` | 4.1, 4.2 | |
| `Serialization/BinaryVisitor.hpp` | binary encoder and decoder, filters | 6.3 | |
| `Serialization/ConsoleVisitor.hpp` | `ConsoleDebugVisitor`, `printEntity`, `printProject`: print entities and components to `std::cout`, no lock | 7 | `Core/ComponentRegistry.hpp`, `Core/EntityReference.hpp` |
| `Serialization/DeserializationValidation.hpp` | `Severity`, `ValidationError`, the load report | 6.1 | |
| `Serialization/EntityReferenceLinker.hpp` | post-load relink pass | 2.3 | `Core/ComponentRegistry.hpp` |
| `Serialization/XmlCommon.hpp` | XML tag names (`Project`, `Entity`, `Children`, `ListSize`, `E%zu`) | 6.2 | |
| `Serialization/XmlDeserializer.hpp` | XML load | 6.1 | `Core/ComponentRegistry.hpp` |
| `Serialization/XmlSerializer.hpp` | XML save | 6.1 | `Core/ComponentRegistry.hpp` |
| `Time/RemoteClockEstimator.hpp` | remote clock offset from one-way samples | 9.9c | |
