# Hybrid Unified Reflection System (HURS) — Design Document

## 1. Objectives

The system must provide a unified way to "interrogate" components (both hardcoded C++ and runtime-created) to support:

- **XML Persistence:** Saving and loading component state.
- **CommandStack Integration:** Automatically wiring `Parameter` objects to the Undo/Redo system.
- **Local/Remote UI:** Exposing schema and values to a local inspector or a network-based remote UI.
- **CLI Access:** Navigating the entity-component graph via a command line (e.g., `set entity1.motor.speed 5.0`).

---

## 2. Core Architecture: The "Universal Bridge"

The system relies on the **Visitor Pattern** combined with **Property Metadata (Tagging)**.

### 2.1 The Base Component

Every component, regardless of origin, implements two reflection entry points:

```cpp
struct BaseComponent {
    virtual ~BaseComponent() = default;
    virtual const char* get_type_name() const = 0;

    // Called once when the component is attached to an entity.
    // Intended for setup visitors (e.g., LinkerVisitor).
    virtual void on_attach(ComponentVisitor& v) { reflect(v); }

    // Called per-frame or on-demand for inspection, serialization, UI, etc.
    virtual void reflect(ComponentVisitor& v) = 0;
};
```

> **Why two entry points?**
> `on_attach` and `reflect` are architecturally the same operation but have very different lifecycles. Keeping them separate makes the contract explicit: a `LinkerVisitor` should only ever be passed to `on_attach`, never to `reflect`. This prevents setup-only visitors from being accidentally run every frame, and per-frame visitors from missing the attachment phase.

---

## 3. The Tagging System (Metadata)

Tags provide context to visitors, allowing a single `reflect` call to serve multiple systems. Each tag is independent; a property may carry multiple tags simultaneously.

```cpp
enum class Tag {
    Persistent,  // Include in XML save/load
    Hidden,      // Exclude from UI display (does not affect other visitors)
    ReadOnly,    // No setter access; value is observable but not writable
    Networked,   // Sync over network (independent of UI visibility)
};
```

### 3.1 Tag Interaction Reference

| Tag combination         | Behaviour                                                                 |
|-------------------------|---------------------------------------------------------------------------|
| `Persistent` + `ReadOnly` | Value is written on save. On load, the value is applied directly via the internal reference, bypassing any setter. |
| `Hidden` + `Networked`  | Not shown in local UI, but still included in network sync.               |
| `Hidden` + `Persistent` | Not shown in UI, but saved and restored (e.g., internal counters).       |
| `ReadOnly` + `Networked`| Synced over network as an observable value; remote clients cannot write it. |

### 3.2 Property Wrapper

```cpp
template<typename T>
struct Property {
    const char* name;
    T&          value;
    std::vector<Tag> tags;
};
```

---

## 4. The Visitor Interface

All visitors implement a common interface. Unknown property types (those with no matching overload) are silently ignored by default, allowing new visitors to be added without touching existing components.

```cpp
struct ComponentVisitor {
    virtual ~ComponentVisitor() = default;

    virtual void visit_property(const char* name, int&   value, std::span<const Tag> tags) {}
    virtual void visit_property(const char* name, float& value, std::span<const Tag> tags) {}
    virtual void visit_property(const char* name, bool&  value, std::span<const Tag> tags) {}
    virtual void visit_property(const char* name, std::string& value, std::span<const Tag> tags) {}
    virtual void visit_property(const char* name, ParameterBase& p, std::span<const Tag> tags) {}

    // Actions are named callables with no arguments or return value.
    virtual void visit_action(const char* name, std::function<void()> fn) {}
};
```

---

## 5. The Visitor Suite

### 5.1 XML Serialization — `PersistenceVisitor`

- **Lifecycle:** On save / on load.
- **Logic:** Only processes properties tagged `Tag::Persistent`.
- **ReadOnly on load:** Values are written through the direct reference (`value`), not through any setter, so `ReadOnly` does not block restoration.
- Overloads `visit_property` for `int`, `float`, `bool`, `string`, and `Parameter<T>`.
- Ignores `visit_action`.

### 5.2 Undo/Redo Wiring — `LinkerVisitor`

- **Lifecycle:** Called once via `on_attach`, never via `reflect`.
- **Logic:** Ignores all raw data types. Specifically handles `ParameterBase`, calling `p.set_command_stack(stack)`.
- Ignores `visit_action`.

### 5.3 UI & Remote UI — `SchemaVisitor` / `UiVisitor`

- **Lifecycle:** On demand (local inspector) or per-frame (remote UI push).
- **Logic:**
  - Skips properties tagged `Tag::Hidden`.
  - For `Tag::ReadOnly`, generates a non-editable display widget (label/readout).
  - Builds a type schema (JSON or Protobuf) from name, type, and metadata (label, range).
  - For remote UI, serializes current values into a flat buffer sent over a socket.
- Handles `visit_action` by generating a Button widget.

### 5.4 Network Delta Sync — `NetworkDeltaVisitor`

- **Lifecycle:** Per-frame.
- **Logic:** Only processes properties tagged `Tag::Networked`. On each visit, compares the current value against a **shadow copy** stored in the visitor. Only properties whose value has changed since the last frame are emitted.
- Dirty tracking is the visitor's responsibility, not the component's. The visitor maintains a `std::unordered_map<std::string, ValueSnapshot>` keyed by `"entity_id.component_type.property_name"`.
- `Tag::ReadOnly` + `Tag::Networked` properties are synced outbound only; remote write packets for such properties are rejected.
- Ignores `visit_action`.

### 5.5 Command Line Interface — `GraphVisitor` / `SearchVisitor`

- **Lifecycle:** On demand, triggered by CLI input.
- **Logic:** The `GraphVisitor` maintains a flat map of the full entity-component graph, rebuilt on structural changes (component attach/detach). The `SearchVisitor` traverses a single component looking for a property or action by name.

**CLI path resolution for `call entity1.Motor.EmergencyStop`:**

1. **Tokenize** the path: `["entity1", "Motor", "EmergencyStop"]`.
2. **Entity lookup:** Find `entity1` in the registry. → Error if not found: `"Unknown entity 'entity1'"`.
3. **Component resolution:** Find a component whose `get_type_name()` == `"Motor"`. → Error if not attached: `"Entity 'entity1' has no component 'Motor'"`.
4. **Property/action search:** Run a `SearchVisitor` on that component looking for the name `"EmergencyStop"`. → Error if not found: `"No property or action 'EmergencyStop' on 'Motor'"`.
5. **Type check:** For `get`/`set`, verify the target is a property, not an action. For `call`, verify it is an action. → Error on mismatch: `"'EmergencyStop' is an action, not a property"`.
6. **Execute:** Call the lambda / read or write the value.

---

## 6. Hybrid Component Implementation

### 6.1 Static (C++) Component

Used for performance-critical systems. All members are known at compile time.

```cpp
struct MotorComponent : public BaseComponent {
    COMPONENT_IDENTITY("Motor")  // Expands to get_type_name() returning "Motor"

    Parameter<float> speed{"Speed", 0.0f};
    int internal_cycles = 0;

    void reflect(ComponentVisitor& v) override {
        v.visit_property("speed",  speed,           {Tag::Persistent, Tag::Networked});
        v.visit_property("cycles", internal_cycles,  {Tag::ReadOnly});
        v.visit_action("EmergencyStop", [this]{ speed.set(0); });
    }
};
```

> `COMPONENT_IDENTITY(name)` is a convenience macro that expands to:
> ```cpp
> const char* get_type_name() const override { return name; }
> ```

### 6.2 Dynamic (Runtime) Component

Used for modding or live-editing. Members are defined at runtime and stored type-erased.

Because dynamic components must support heterogeneous value types, properties are stored as `std::variant` and resolved through a type-erased visitor dispatch:

```cpp
using DynValue = std::variant<int, float, bool, std::string, Parameter<float>>;

struct DynamicComponent : public BaseComponent {
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

> **Note:** `std::visit` dispatches to the correct `visit_property` overload based on the active variant type. Visitors that don't override an overload fall through to the no-op default in `ComponentVisitor`, so unknown types are safely ignored.

---

## 7. Workflow Examples

### Saving to XML

1. `PersistenceVisitor` visits `MotorComponent`.
2. `speed` has `Tag::Persistent` → serialized to XML.
3. `internal_cycles` lacks `Tag::Persistent` → skipped.
4. `EmergencyStop` is an action → ignored by `PersistenceVisitor`.

### UI Rendering (Local or Remote)

1. `UiVisitor` visits `MotorComponent`.
2. `speed` is writable → rendered as a **Slider**.
3. `internal_cycles` is `ReadOnly` → rendered as a **Label/Readout**.
4. `EmergencyStop` is an action → rendered as a **Button**.

### Network Sync

1. `NetworkDeltaVisitor` visits `MotorComponent` each frame.
2. `speed` has `Tag::Networked` → compared against shadow copy.
3. If changed, the new value is emitted in the delta packet.
4. `internal_cycles` lacks `Tag::Networked` → skipped entirely.

### CLI Access

```
> call entity1.Motor.EmergencyStop
[OK] EmergencyStop executed on entity1.Motor

> get entity1.Motor.speed
[OK] entity1.Motor.speed = 0.0

> set entity1.Motor.cycles 99
[ERROR] 'cycles' is ReadOnly on 'Motor'
```

---

## 8. Future Extensions

| Extension            | Mechanism                                                                                          |
|----------------------|----------------------------------------------------------------------------------------------------|
| **Documentation**    | `DocVisitor` iterates all components and emits a Markdown table of every property and action.      |
| **Scripting bridge** | A `ScriptVisitor` exposes the property graph to Lua/Python, using the same tag filtering.          |
| **Hot reload**       | On script/mod reload, detach the old `DynamicComponent`, construct a new one, re-run `on_attach`.  |
| **Replication roles**| Add `Tag::ServerOnly` / `Tag::ClientOnly` to extend `NetworkDeltaVisitor` with authority filtering.|