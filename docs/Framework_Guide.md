# Brezel Framework Guide

Brezel is a C++23 Real-Time (RT) capable application framework built on top of EnTT ECS. It provides a robust architecture for separating Non-Real-Time (NRT) systems (like UIs, networks, and serialization) from hard Real-Time execution loops.

## 1. Core Philosophy: The Air-Gap

The most critical architectural constraint in Brezel is the strict separation between NRT and RT layers.

- **Non-Real-Time (NRT)**: UI, Serialization, File I/O, Network. This layer interacts heavily with `entt::registry`, creates/destroys entities, and manages the Command Stack (Undo/Redo).
- **Real-Time (RT)**: High-frequency control loops (e.g., 1kHz motion planning). The RT loop **MUST NEVER** interact with `entt::registry`, trigger heap allocations, or block on mutexes.

To achieve this, all data crossing the boundary must be strictly **Plain Old Data (POD)**.

## 2. Component Definition (Pure PODs)

Components in Brezel must **NOT** inherit from any base classes and must **NOT** contain heavy heap-allocating wrappers. They are pure C++ structs.

```cpp
struct Motor {
    float speed{0.0f};
    float torque{10.0f};
    std::vector<float> test;
};
```

### Static Reflection
Instead of intrusive virtual methods, Brezel uses static free-functions for reflection. This allows the UI and Serializer to discover properties without polluting the POD structs, keeping them lightweight and RT-safe.

```cpp
template<typename V>
void reflect(Motor& m, V& v) {
    v.visit_property("speed", m.speed, {Tag::Persistent, Tag::CommandStack});
    v.visit_property("torque", m.torque, {Tag::Persistent, Tag::CommandStack});
    
    // Vectors require a VectorAccessor
    VectorAccessor va(m.test);
    v.visit_property("test", va, {Tag::Persistent});
}
```

Components must be registered in your application startup:
```cpp
ComponentRegistry::registerComponent<Motor>("Motor");
```

## 3. State Mutation & The Command Stack

Because NRT and RT layers are separated, you must be careful how you mutate data in the NRT layer to ensure the rest of the application (like the GUI or network) reacts correctly.

### Modifying Values (Undo/Redo)
When the UI modifies a component, use `ValueChangeCommand<T>` to push the change onto the Undo/Redo stack. This command operates non-intrusively on raw memory addresses.

```cpp
auto cmd = std::make_unique<ValueChangeCommand<float>>(
    &motor.speed,       // Pointer to raw memory
    motor.speed,        // Old value
    45.5f,              // New value
    "Set Motor Speed"   // Description
);
project.getStack().pushAndExecute(std::move(cmd));
```

### EnTT Change Callbacks
EnTT native signals replace classic observer patterns (like `onChange`). When a component is modified, you must notify EnTT using `patch` so it can emit `on_update` signals to any NRT listeners.

```cpp
// 1. Listen for updates (e.g. GUI refresh or Network Replication)
void onMotorUpdated(entt::registry& reg, entt::entity entity) {
    const auto& m = reg.get<Motor>(entity);
    // ... update GUI ...
}
registry.on_update<Motor>().connect<&onMotorUpdated>();

// 2. Notify EnTT after modification (e.g. after a ValueChangeCommand executes)
registry.patch<Motor>(entity);
```

## 4. Multi-threading & Synchronization
The UI/NRT threads write via EnTT registry/commands. The RT thread reads/writes raw pointers securely passed to it (e.g., via a synchronization phase, lock-free queues, or double-buffering), **entirely bypassing EnTT** during the RT cycle.
