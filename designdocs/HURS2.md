# Hybrid Unified Reflection System (HURS) — Design Document v2

## 1. Objectives

The system must provide a unified way to "interrogate" components (both hardcoded C++ and
runtime-created) to support:

- **XML Persistence:** Saving and loading component state.
- **CommandStack Integration:** Automatically wiring `Parameter` objects to the Undo/Redo system.
- **Local/Remote UI:** Exposing schema and values to a local inspector or a network-based remote UI.
- **CLI Access:** Navigating the entity-component graph via a command line (e.g., `set entity1.ELM7231.target_velocity 5.0`).

---

## 2. Architecture Overview

The system is built on three orthogonal layers that work together:

| Layer | Technology | Responsibility |
|---|---|---|
| **Entity management** | EnTT | Entity identity, component storage, iteration |
| **Reflection** | Visitor pattern + Tags | Expose component properties generically |
| **Discovery** | ComponentRegistry (hand-written) | Map runtime entity contents to reflect calls |

---

## 3. Why EnTT?

EnTT stores components in **packed typed pools** — one contiguous array per component type.
This gives you:

- **Cache-friendly iteration**: `view<EtherCatDevice, ELM7231>()` touches only entities
  that have both, in memory order. No pointer chasing.
- **Entity identity**: A lightweight `uint32_t` handle with no heap allocation.
- **Typed views**: The query language is C++ template arguments — no runtime type strings.
- **Signals**: `registry.on_construct<T>()` fires when a component is added, perfect for
  running setup visitors automatically.
- **`try_get`**: `registry.try_get<EL2008>(e)` returns null if the entity doesn't have that
  component — replaces `dynamic_cast` entirely.

```cpp
// Zero-overhead filtered iteration — only entities with BOTH components
registry.view<EtherCatDevice, ELM7231>().each([&](EtherCatDevice& dev, ELM7231& drive) {
    if (dev.state != EcState::OP) return;
    write_pdo(dev.pdo, drive.target_velocity);
    drive.actual_velocity = read_pdo(dev.pdo);
});
```

---

## 4. Component Architecture

### 4.1 Composition over inheritance

Devices are not modeled with class hierarchies. An entity **is** a specific device because
of which components it carries — not because of a base class or a type enum.

```
entity
  ├── EtherCatDevice     (always present — bus address, state, PDO buffers)
  └── EL2008             (present only if this slave is an EL2008 8ch digital output)
      or EL5001          (SSI encoder interface)
      or ELM7231         (servo drive)
```

The **only** place you switch on device type is in the factory function at bus-scan time.
Everywhere else in the codebase, you query by component type.

```cpp
entt::entity create_ethercat_entity(entt::registry& reg, const EcSlaveInfo& slave) {
    auto e = reg.create();
    reg.emplace<EtherCatDevice>(e, slave.address, slave.product_code, EcState::INIT);

    switch (slave.product_code) {
        case 0x07D83052: reg.emplace<EL2008> (e); break;
        case 0x13894052: reg.emplace<EL5001> (e); break;
        case 0x1C373052: reg.emplace<ELM7231>(e); break;
        default: break;  // unknown type — EtherCatDevice alone is sufficient
    }
    return e;
}
```

### 4.2 Identifying a device type

```cpp
// Is this entity an EL2008?
if (registry.all_of<EL2008>(e)) { ... }

// Get a pointer — null if the entity doesn't have this component
if (auto* drive = registry.try_get<ELM7231>(e)) {
    drive->target_velocity = 5.0f;
}
```

No enum, no string comparison, no casting. The component type IS the identity.

### 4.3 Processing by type — views

```cpp
// Motion loop — only servo drives
void motion_update(entt::registry& reg, float dt) {
    reg.view<EtherCatDevice, ELM7231>().each([](EtherCatDevice& dev, ELM7231& drive) {
        if (dev.state != EcState::OP) return;
        write_pdo(dev.pdo, drive.target_velocity);
        drive.actual_velocity = read_pdo(dev.pdo);
    });
}

// Encoder read — only SSI interfaces
void encoder_update(entt::registry& reg) {
    reg.view<EtherCatDevice, EL5001>().each([](EtherCatDevice& dev, EL5001& enc) {
        enc.position   = read_pdo_position(dev.pdo);
        enc.data_valid = read_pdo_valid(dev.pdo);
    });
}

// Digital outputs — only 8ch DO boards
void io_update(entt::registry& reg) {
    reg.view<EtherCatDevice, EL2008>().each([](EtherCatDevice& dev, EL2008& io) {
        write_pdo_outputs(dev.pdo, io.output_bits);
    });
}
```

---

## 5. Reflection System

### 5.1 The Base Component

Every component implements a single `reflect()` method. There is no `on_attach` virtual —
setup logic (e.g. `LinkerVisitor`) is triggered via EnTT's `on_construct` signal instead,
which is a cleaner separation.

```cpp
struct BaseComponent {
    virtual ~BaseComponent() = default;
    virtual const char* get_type_name() const = 0;
    virtual void reflect(ComponentVisitor& v) = 0;
};
```

> **Why a single reflect()?**  
> `on_attach` and `reflect` are architecturally the same operation. Rather than a second
> virtual, use EnTT's signal to fire setup visitors exactly once on attachment — the
> contract is enforced by the signal mechanism, not by call-site discipline.

```cpp
// Wire LinkerVisitor automatically on attach — no second virtual needed
registry.on_construct<ELM7231>().connect<[](entt::registry& reg, entt::entity e) {
    auto& drive = reg.get<ELM7231>(e);
    LinkerVisitor linker{command_stack};
    drive.reflect(linker);
}>();
```

### 5.2 The Tagging System

Tags give visitors context. A property may carry multiple tags simultaneously.

```cpp
enum class Tag {
    Persistent,  // Include in XML save/load
    Hidden,      // Exclude from UI display
    ReadOnly,    // Observable but not writable
    Networked,   // Sync over network
};
```

**Tag interaction reference:**

| Combination | Behaviour |
|---|---|
| `Persistent` + `ReadOnly` | Saved on export. On load, value written through the direct reference, bypassing any setter. |
| `Hidden` + `Networked` | Not shown in local UI, but still synced over network. |
| `Hidden` + `Persistent` | Not shown in UI, but saved and restored (e.g. internal counters). |
| `ReadOnly` + `Networked` | Synced outbound only; remote write packets rejected. |

### 5.3 The Visitor Interface

```cpp
struct ComponentVisitor {
    virtual ~ComponentVisitor() = default;

    virtual void visit_property(const char*, int&,          std::span<const Tag>) {}
    virtual void visit_property(const char*, float&,        std::span<const Tag>) {}
    virtual void visit_property(const char*, bool&,         std::span<const Tag>) {}
    virtual void visit_property(const char*, std::string&,  std::span<const Tag>) {}
    virtual void visit_property(const char*, ParameterBase&,std::span<const Tag>) {}

    // Named callables with no arguments — rendered as buttons in UI
    virtual void visit_action(const char*, std::function<void()>) {}
};
```

Unknown types fall through to the no-op default. New visitors can be added without
touching existing components.

---

## 6. Concrete Component Examples

```cpp
struct EtherCatDevice : BaseComponent {
    uint16_t    address;
    uint32_t    product_code;
    EcState     state;
    ProcessData pdo;

    const char* get_type_name() const override { return "EtherCatDevice"; }

    void reflect(ComponentVisitor& v) override {
        v.visit_property("address",      address,      {Tag::ReadOnly});
        v.visit_property("product_code", product_code, {Tag::ReadOnly});
    }
};

// Beckhoff 8-channel digital output
struct EL2008 : BaseComponent {
    uint8_t output_bits = 0x00;

    const char* get_type_name() const override { return "EL2008"; }

    void reflect(ComponentVisitor& v) override {
        v.visit_property("output_bits", output_bits, {Tag::Networked});
        v.visit_action("AllOff", [this]{ output_bits = 0x00; });
        v.visit_action("AllOn",  [this]{ output_bits = 0xFF; });
    }
};

// Beckhoff SSI encoder interface
struct EL5001 : BaseComponent {
    int32_t position   = 0;
    bool    data_valid = false;
    bool    frame_error= false;

    const char* get_type_name() const override { return "EL5001"; }

    void reflect(ComponentVisitor& v) override {
        v.visit_property("position",    position,    {Tag::ReadOnly, Tag::Networked});
        v.visit_property("data_valid",  data_valid,  {Tag::ReadOnly});
        v.visit_property("frame_error", frame_error, {Tag::ReadOnly});
    }
};

// Beckhoff servo drive
struct ELM7231 : BaseComponent {
    float   target_velocity = 0.0f;
    float   actual_velocity = 0.0f;
    float   max_current     = 5.0f;
    int32_t encoder_counts  = 0;

    const char* get_type_name() const override { return "ELM7231"; }

    void reflect(ComponentVisitor& v) override {
        v.visit_property("target_velocity", target_velocity, {Tag::Persistent, Tag::Networked});
        v.visit_property("actual_velocity", actual_velocity, {Tag::ReadOnly,   Tag::Networked});
        v.visit_property("max_current",     max_current,     {Tag::Persistent});
        v.visit_property("encoder_counts",  encoder_counts,  {Tag::ReadOnly,   Tag::Networked});
        v.visit_action("Stop", [this]{ target_velocity = 0.0f; });
    }
};
```

---

## 7. The ComponentRegistry

### 7.1 Why it exists

EnTT knows what components an entity has at compile time, through typed views and
`try_get`. This is sufficient for all hot-path processing loops. However, some systems
must iterate components **without knowing their types at compile time**:

- Inspector UI: "show all components on this selected entity"
- XML serializer: "save all persistent components on this entity"
- CLI: resolve `"entity1.ELM7231.target_velocity"` from a string at runtime

Without a registry, these systems are forced to enumerate every possible type manually
and must be edited every time a new device type is added. The registry removes that
coupling.

### 7.2 When you do NOT need the registry

| System | Needs registry? | Reason |
|---|---|---|
| Motion update loop | No | `view<EtherCatDevice, ELM7231>()` — types known at compile time |
| IO update loop | No | Same |
| Inspector / UI | Yes | "show all components on this entity" — types unknown |
| XML serializer | Yes | "save all persistent components" — types unknown |
| CLI path resolution | Yes | Component name comes in as a runtime string |
| Network delta sync | Yes | "iterate all networked properties across all entities" |

### 7.3 Implementation

This is hand-written — approximately 30 lines. EnTT's built-in `entt::meta` system exists
but has its own DSL and type-erased value system that does not map onto the visitor pattern.
Writing it yourself gives you full control with minimal code.

```cpp
struct ComponentMeta {
    const char* type_name;
    std::function<void(entt::registry&, entt::entity, ComponentVisitor&)> reflect;
    std::function<bool(entt::registry&, entt::entity)>                    has;
};

class ComponentRegistry {
public:
    template<typename T>
    void register_type(const char* name) {
        entries_.push_back({
            name,
            [](entt::registry& reg, entt::entity e, ComponentVisitor& v) {
                if (auto* c = reg.try_get<T>(e)) c->reflect(v);
            },
            [](entt::registry& reg, entt::entity e) {
                return reg.all_of<T>(e);
            }
        });
    }

    // Returns meta for every component the entity actually has
    std::vector<const ComponentMeta*> get_components(entt::registry& reg,
                                                      entt::entity e) const {
        std::vector<const ComponentMeta*> result;
        for (auto& m : entries_)
            if (m.has(reg, e)) result.push_back(&m);
        return result;
    }

    // Look up by name string — used by CLI
    const ComponentMeta* find(const char* name) const {
        for (auto& m : entries_)
            if (strcmp(m.type_name, name) == 0) return &m;
        return nullptr;
    }

private:
    std::vector<ComponentMeta> entries_;
};
```

### 7.4 Registration at startup

```cpp
ComponentRegistry comp_registry;

void register_all_components() {
    comp_registry.register_type<EtherCatDevice>("EtherCatDevice");
    comp_registry.register_type<EL2008>        ("EL2008");
    comp_registry.register_type<EL5001>        ("EL5001");
    comp_registry.register_type<ELM7231>       ("ELM7231");
}
```

Adding a new device type means one line here and nothing else.

---

## 8. The Visitor Suite

### 8.1 XML Serialization — `PersistenceVisitor`

- **Lifecycle:** On save / on load.
- **Logic:** Only processes properties tagged `Tag::Persistent`.
- **ReadOnly on load:** Values written through the direct reference, not any setter.
- Ignores `visit_action`.

### 8.2 Undo/Redo Wiring — `LinkerVisitor`

- **Lifecycle:** Called once via `registry.on_construct` signal — never inside a per-frame loop.
- **Logic:** Ignores raw data types. Handles `ParameterBase`, calling `p.set_command_stack(stack)`.
- Ignores `visit_action`.

### 8.3 UI & Remote UI — `UiVisitor`

- **Lifecycle:** On demand (local inspector) or per-frame (remote UI push).
- **Logic:**
  - Skips `Tag::Hidden` properties.
  - `Tag::ReadOnly` → non-editable readout widget.
  - `visit_action` → Button widget.
  - Builds JSON/Protobuf schema from name, type, and metadata.

### 8.4 Network Delta Sync — `NetworkDeltaVisitor`

- **Lifecycle:** Per-frame.
- **Logic:** Only processes `Tag::Networked` properties. Compares against a shadow copy
  stored in the visitor (`std::unordered_map<std::string, ValueSnapshot>`). Only changed
  values are emitted.
- Dirty tracking is the visitor's responsibility, not the component's.
- `ReadOnly` + `Networked`: synced outbound only; remote writes rejected.
- Ignores `visit_action`.

### 8.5 Command Line Interface — `SearchVisitor`

- **Lifecycle:** On demand.
- **Path resolution for `call entity1.ELM7231.Stop`:**

1. Tokenize: `["entity1", "ELM7231", "Stop"]`
2. Entity lookup → error if not found
3. `comp_registry.find("ELM7231")` + check `meta->has(reg, e)` → error if not attached
4. Run `SearchVisitor` on the component for `"Stop"` → error if not found
5. Type check: `call` requires action, `get`/`set` require property → error on mismatch
6. Execute

---

## 9. Generic Systems Using the Registry

```cpp
// Inspector: show all components on a selected entity
void inspect_entity(entt::registry& reg, entt::entity e, ComponentVisitor& v) {
    for (auto* meta : comp_registry.get_components(reg, e)) {
        printf("=== %s ===\n", meta->type_name);
        meta->reflect(reg, e, v);
    }
}

// XML save: persist all components on an entity
void save_entity(entt::registry& reg, entt::entity e) {
    PersistenceVisitor v;
    for (auto* meta : comp_registry.get_components(reg, e))
        meta->reflect(reg, e, v);
    v.flush_to_xml();
}

// CLI get
void cli_get(entt::registry& reg, entt::entity e,
             const char* comp_name, const char* prop_name) {
    auto* meta = comp_registry.find(comp_name);
    if (!meta || !meta->has(reg, e)) { puts("[ERROR] component not found"); return; }
    SearchVisitor v{prop_name};
    meta->reflect(reg, e, v);
}
```

---

## 10. Workflow Examples

### Bus scan and entity creation

```
EcBusScan() returns [{address=3, product=0x1C373052}, {address=5, product=0x07D83052}]
  → entity A: EtherCatDevice(addr=3) + ELM7231
  → entity B: EtherCatDevice(addr=5) + EL2008
```

### Motion loop (no registry needed)

```
registry.view<EtherCatDevice, ELM7231>()
  → touches only entity A
  → writes target_velocity into PDO, reads actual_velocity back
```

### Inspector: user selects entity A

```
comp_registry.get_components(reg, entityA)
  → [EtherCatDevice, ELM7231]
  → UiVisitor renders:
       EtherCatDevice: address (readout), product_code (readout)
       ELM7231:        target_velocity (slider), actual_velocity (readout),
                       max_current (slider), encoder_counts (readout), Stop (button)
```

### CLI

```
> get entity1.ELM7231.actual_velocity
[OK] entity1.ELM7231.actual_velocity = 1.230

> set entity1.ELM7231.target_velocity 5.0
[OK] entity1.ELM7231.target_velocity = 5.0

> call entity1.ELM7231.Stop
[OK] Stop executed on entity1.ELM7231

> set entity1.ELM7231.actual_velocity 0.0
[ERROR] 'actual_velocity' is ReadOnly on 'ELM7231'
```

### XML save

```
PersistenceVisitor on entity A:
  EtherCatDevice: no Persistent properties → nothing written
  ELM7231: target_velocity (Persistent) → written
           max_current     (Persistent) → written
           actual_velocity (ReadOnly, no Persistent) → skipped
```

---

## 11. Dynamic (Runtime) Components

For modding or live device profiles loaded from config files, members can be defined at
runtime using `std::variant`:

```cpp
using DynValue = std::variant<int, float, bool, std::string>;

struct DynamicComponent : BaseComponent {
    std::string type_name;
    std::map<std::string, std::pair<DynValue, std::vector<Tag>>> props;

    const char* get_type_name() const override { return type_name.c_str(); }

    void reflect(ComponentVisitor& v) override {
        for (auto& [name, entry] : props) {
            auto& [value, tags] = entry;
            std::visit([&](auto& val) {
                v.visit_property(name.c_str(), val, tags);
            }, value);
        }
    }
};
```

`std::visit` dispatches to the correct `visit_property` overload. Visitors that don't
override an overload fall through to the no-op default.

---

## 12. Design Decisions Summary

| Decision | Rationale |
|---|---|
| Single `reflect()` instead of `reflect()` + `on_attach()` | EnTT `on_construct` signals enforce setup-only visitors being run once, without a second virtual |
| Virtual base component | Acceptable cost for tool/editor systems; use plain structs + free-function registry if hot-path iteration over thousands of components is required |
| Device type = component presence, not enum | No switch outside factory; `try_get` and `view` replace `dynamic_cast` and type checks |
| Hand-written ComponentRegistry over `entt::meta` | 30 lines, maps directly onto the visitor pattern, no impedance mismatch |
| Tags on properties, not on components | A single `reflect()` call serves all systems simultaneously; each visitor filters what it cares about |

---

## 13. Future Extensions

| Extension | Mechanism |
|---|---|
| **Documentation** | `DocVisitor` iterates all registered components and emits a Markdown table of every property and action |
| **Scripting bridge** | `ScriptVisitor` exposes the property graph to Lua/Python using the same tag filtering |
| **Hot reload** | Detach old `DynamicComponent`, construct new one, re-run `on_construct` signal |
| **Replication roles** | Add `Tag::ServerOnly` / `Tag::ClientOnly` to extend `NetworkDeltaVisitor` with authority filtering |
| **New EtherCAT device** | Add struct with `reflect()`, one line in `register_all_components()` — nothing else changes |