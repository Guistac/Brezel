# C++ Realtime Framework — Architecture Reference

> **Purpose of this document:** Complete architecture specification for a modular, realtime-capable C++20/23 cross-platform application framework. Intended as a context document for AI-assisted development sessions.

---

## 1. Project Goals & Constraints

- **Language:** C++20/23, cross-platform (Windows, Linux, macOS)
- **Primary use case:** General-purpose framework — domain TBD at application level
- **Key requirements:**
  - Tight realtime logic execution (automation, control loops, node graph processing)
  - Full modularity and runtime extensibility
  - Undo/redo on all mutations
  - Serialization (XML, human-readable, diff-friendly)
  - Lua scripting access (global, per-object, and realtime-capable)
  - Command-line access to the entire object/node graph
  - Remote GUI support (separate process or browser)
  - Safe, explicit memory ownership — minimal `std::shared_ptr`
  - Thread safety by architecture, not by locking

---

## 2. Foundational Design Rules

1. **Ownership is always unambiguous.** One thing owns each object. All other references use `ObjectHandle<T>` (ID-based) or raw non-owning `T*` with a documented lifetime and a naming convention suffix (`fooPtr`).
2. **Layers only depend downward.** Application → Service → Object Model → Foundation. No upward calls.
3. **Reflection drives everything.** Serialization, Lua binding, GUI, CLI, and undo all work automatically once a type is registered. No per-class boilerplate beyond a single macro.
4. **Thread safety by isolation, not locking.** Each thread owns its data exclusively. Threads communicate only through lock-free queues and atomic buffers. No mutexes anywhere.
5. **The realtime thread never touches the object tree.** It works on a compiled, flat data mirror. The object tree feeds it; it never reaches back.

---

## 3. Layer Overview

```
┌──────────────────────────────────────────────────────────┐
│  Application Layer                                        │
│  CLI  |  Local GUI  |  Remote GUI  |  Lua  |  Project IO │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Service Layer                                            │
│  CommandStack | ScriptEngine | XmlSerializer              │
│  Console | DeltaBroadcaster | RemoteCommandReceiver        │
│  GraphCompiler | GraphManager                             │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Object Model Layer                                       │
│  Object + ComponentSystem | ParameterSet | TypeRegistry   │
│  ObjectHandle | ObjectRegistry | NodeGraph (EnTT)         │
└────────────────────────┬─────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Foundation Layer                                         │
│  Signal<T> | StringID | Logger | Allocators               │
└──────────────────────────────────────────────────────────┘
                         │
┌────────────────────────▼─────────────────────────────────┐
│  Realtime Thread (isolated)                               │
│  CompiledGraph | RTBytecodeVM | SharedBuffers             │
└──────────────────────────────────────────────────────────┘
```

---

## 4. Foundation Layer

### 4.1 `Signal<Args...>` — Observer

Lightweight, header-only observer. No Qt dependency. Used by `Parameter::onChange`, `CommandStack::onStackChanged`, `Logger` sinks, and all custom events.

```cpp
template<typename... Args>
class Signal {
public:
    using Connection = uint32_t;
    Connection connect(std::function<void(Args...)> fn);
    void       disconnect(Connection c);
    void       emit(Args... args) const;
};

// RAII auto-disconnect
struct ScopedConnection {
    Signal<>* sig; Signal<>::Connection conn;
    ~ScopedConnection() { if (sig) sig->disconnect(conn); }
};
```

### 4.2 `StringID` — Interned Strings

Hashed, interned string used for type names, parameter names, command names. O(1) comparison, zero allocation on hot paths.

```cpp
struct StringID {
    uint64_t hash = 0;
    consteval StringID(const char* s) : hash(fnv1a_v(s)) {}
    static StringID from(std::string_view s); // runtime intern
    bool operator==(StringID) const = default;
};
```

### 4.3 `Logger`

Multi-sink logger with severity levels (Trace, Debug, Info, Warning, Error, Fatal). Pluggable sinks: stdout, file, in-app ring buffer for GUI console. Macros (`LOG_INFO`, `LOG_WARN`, etc.) compile out in release.

### 4.4 Allocators

Three-tier memory model:

| Tier | Mechanism | Use case |
|---|---|---|
| Tree ownership | `unique_ptr<Object>` children | Parent destroys children |
| Cross-references | `ObjectHandle<T>` (ID-based) | Any reference crossing the tree |
| Hot allocations | `PoolAllocator<T,N>` | Parameters, event nodes, parser temps |

Raw `T*` is permitted **only** as a non-owning observer with a documented lifetime. Suffix `Ptr` by convention (e.g. `cameraPtr`).

---

## 5. Object Model Layer

### 5.1 Object — Base Class

Minimal identity and tree node. All behaviors come from attached **Components**, not inheritance.

```cpp
class Object {
public:
    using ID = uint64_t;

    explicit Object(std::string_view name, Object* parent = nullptr);
    virtual ~Object();

    // Identity
    ID               id()    const;
    std::string_view name()  const;
    void             setName(std::string_view);
    std::string      path()  const;  // e.g. "root/scene/camera"

    // Tree (parent OWNS children via unique_ptr)
    Object*                 parent()                         const;
    Object*                 findChild(std::string_view name) const;
    Object*                 findByPath(std::string_view)     const;
    void                    addChild(std::unique_ptr<Object>);
    std::unique_ptr<Object> removeChild(Object*);

    // Component system
    template<typename C> C*    get()       const;
    template<typename C> C&    getOrAdd();
    template<typename C> bool  has()       const;
    template<typename C> void  remove();
    std::span<Component* const> components() const;

    // Reflection
    virtual const TypeInfo& typeInfo() const = 0;

    // Serialization hooks
    virtual void serialize(XmlWriter&)   const;
    virtual void deserialize(XmlReader&);

    // Extension hooks (optional overrides)
    virtual void onGui()                {}
    virtual void onLuaBind(sol::state&) {}

    // Events
    void fireEvent(StringID name, sol::object arg = {});

    // Parameter introspection (from ParameterSet component)
    std::span<ParameterBase* const> parameters() const;
};
```

### 5.2 Inheritance Policy

Use inheritance **shallowly and only for true is-a identity**:

```
Object
├── DataObject       (has ParameterSet + Serializable by default)
│   ├── Camera
│   ├── Light
│   └── Mesh
└── ContainerObject  (holds children only, no params)
    └── Scene
```

Maximum one level of domain-specific inheritance. Anything beyond is a design smell — use components instead.

### 5.3 TypeRegistry — Reflection

```cpp
struct TypeInfo {
    StringID         name;
    const TypeInfo*  parent  = nullptr;
    std::function<std::unique_ptr<Object>()> factory;
    bool isA(const TypeInfo& t) const;
};

// Place inside any Object subclass body:
#define REFLECT_TYPE(Self, Parent)                                   \
  static const TypeInfo& staticTypeInfo() {                          \
      static TypeInfo ti { StringID(#Self),                          \
          &Parent::staticTypeInfo(),                                 \
          []{ return std::make_unique<Self>(); } };                  \
      return ti;                                                     \
  }                                                                  \
  const TypeInfo& typeInfo() const override { return staticTypeInfo(); }
```

`TypeRegistry` maps `StringID → TypeInfo*`. Used by XML deserialization (reconstruct from tag name), Lua auto-binding, CLI help generation, and generic GUI inspector.

### 5.4 `Parameter<T>` — Typed Values

Declare as a member of any Object subclass. Self-registers with the parent on construction. Automatically serialized, undoable, Lua-accessible, CLI-accessible, and GUI-visible.

```cpp
class ParameterBase {
public:
    virtual StringID name()      const = 0;
    virtual StringID typeName()  const = 0;
    virtual void     serialize  (XmlWriter&)  const = 0;
    virtual void     deserialize(XmlReader&)        = 0;
    virtual std::any getAny()    const = 0;
    virtual void     setAny(const std::any&)        = 0;
    Signal<> onChange;
};

template<typename T>
class Parameter : public ParameterBase {
public:
    Parameter(Object* owner, StringID name, T defaultValue = {});
    const T& get() const;
    void     set(const T& v);  // fires onChange + pushes PropertyCommand to CommandStack
    T&       ref();             // direct ref for ImGui sliders
    operator const T&()         const { return get(); }
    Parameter& operator=(const T& v) { set(v); return *this; }
};

// Usage — one line per parameter, everything else is automatic:
class Camera : public DataObject {
    REFLECT_TYPE(Camera, DataObject)
public:
    Camera(Object* parent, std::string_view name)
      : DataObject(parent, name)
      , fov (this, "fov",  60.0f)
      , near(this, "near",  0.1f)
      , far (this, "far", 1000.0f) {}

    Parameter<float> fov, near, far;
};
```

### 5.5 Dynamic Parameters (Script-Added)

Scripts can add parameters at runtime via `object:addParam(name, type, default)`. `DynamicParameter` stores a `std::any` value but otherwise behaves identically to `Parameter<T>` — it serializes, fires `onChange`, appears in the inspector, and is undoable.

### 5.6 `ObjectHandle<T>` — Safe Cross-References

Never store a raw pointer to an Object you don't own. Use `ObjectHandle<T>` — stores only the target's `uint64` ID, resolves via `ObjectRegistry`, returns `nullptr` if the target was deleted.

```cpp
template<typename T>
class ObjectHandle {
public:
    ObjectHandle() = default;
    explicit ObjectHandle(T* obj);
    T*   get()        const;  // resolves via ObjectRegistry; nullptr if gone
    T*   operator->() const;
    explicit operator bool() const;
    // Serializes as path string; resolved in post-load fixup pass
    void serialize  (XmlWriter&) const;
    void deserialize(XmlReader&);
private:
    Object::ID m_id = 0;
};
```

### 5.7 `ObjectRegistry`

Flat map `ID → Object*`. Objects register on construction, unregister on destruction. Provides path-string lookup (`"root/scene/cam"`) for CLI, Lua, and handle fixup after XML load.

---

## 6. Service Layer

### 6.1 CommandStack — Undo / Redo

```cpp
struct Command {
    virtual void        execute()                 = 0;
    virtual void        undo()                    = 0;
    virtual std::string description()       const = 0;
    virtual bool        mergeWith(const Command&) { return false; }
};

class CommandStack {
public:
    void execute(std::unique_ptr<Command>);
    void undo();
    void redo();
    void beginGroup(std::string_view desc);  // compound undo step
    void endGroup();
    Signal<> onStackChanged;  // GUI refresh hook
};
```

`Parameter<T>::set()` automatically creates and pushes a `PropertyCommand<T>`. Structural changes (add/remove child, add/remove link) get explicit `Command` subclasses. All script-driven mutations go through `CommandStack` too — undo always works regardless of mutation source. Rapid slider edits collapse via `mergeWith()`.

### 6.2 XmlSerializer — Project Files

Uses **pugixml**. Save: recursive tree walk, each Object written as an XML element tagged with its type name, parameters as attributes. `ObjectHandle` values written as path strings. Load: reconstructs via `TypeRegistry` factories, then runs a post-load handle fixup pass through `ObjectRegistry`.

```xml
<Project version="1" name="my_project">
  <Scene name="main">
    <Camera name="cam" fov="60" near="0.1" far="1000">
      <Script src="scripts/camera_behavior.lua"/>
      <DynamicParam name="tag" type="string" value="main_camera"/>
    </Camera>
    <MeshInstance name="floor"
                  mesh="project/assets/plane"
                  material="project/mats/stone"/>
  </Scene>
</Project>
```

### 6.3 ScriptEngine — Lua via sol2 + LuaJIT

```cpp
class ScriptEngine {
public:
    void init();
    void bindObjectTree(Object* root);  // exposes "root" Lua global
    void exec(std::string_view code);
    void execFile(std::string_view path);
    void tick();                        // drives coroutines, per-object onTick
    sol::state& lua();
private:
    void registerAllTypes();  // iterates TypeRegistry, auto-binds all types
    sol::state m_lua;
};
```

Each per-object script runs in an isolated `sol::environment` (its own namespace, `self` bound to its owner). The global Lua state is shared but namespaces are isolated. Sandbox restrictions (no `io`, no `os.exit`) set per-environment.

### 6.4 Console / CLI — Full Graph Access

```cpp
class Console {
public:
    using Handler = std::function<void(std::span<std::string_view>)>;
    void registerCommand(std::string_view name, Handler fn, std::string_view help = "");
    void bindObjectTree(Object* root);   // auto-generates set/get/call for ALL paths
    void bindNodeGraph(GraphManager&);   // auto-generates graph commands
    void exec(std::string_view line);
    void execFile(std::string_view path);
    Signal<std::string> onOutput;        // connect to stdout or GUI console
};
```

**Built-in verbs:**

| Verb | Example | Effect |
|---|---|---|
| `get` | `get scene/cam/fov` | Read any parameter by path |
| `set` | `set scene/cam/fov 90` | Write any parameter (goes through CommandStack) |
| `call` | `call scene/cam/reset` | Call a registered method |
| `ls` | `ls scene` | List children of an object |
| `tree` | `tree scene` | Print subtree |
| `undo` | `undo` | Undo last command |
| `redo` | `redo` | Redo |
| `exec` | `exec my_script.lua` | Run a Lua file |
| `node.add` | `node.add PID "my_pid"` | Add a node to the graph |
| `node.remove` | `node.remove my_pid` | Remove a node |
| `node.connect` | `node.connect pid/output motor/input` | Connect pins by path |
| `node.disconnect` | `node.disconnect pid/output motor/input` | Disconnect a link |
| `node.set` | `node.set my_pid/kP 1.5` | Set an RT param on a node |
| `node.get` | `node.get my_pid/output` | Read RT feedback from a node |
| `node.ls` | `node.ls` | List all nodes |
| `node.tree` | `node.tree` | Print node graph topology |
| `help` | `help` | Auto-generated help from registered commands |

The CLI works headless (batch files, CI) and as an interactive REPL. The node graph is fully addressable by path, just like the object tree.

### 6.5 Remote GUI

The Command/Parameter model creates a natural protocol boundary.

```
Remote GUI process / browser
        │
        │   WebSocket / Unix socket / gRPC
        │
┌───────▼──────────────────────────┐
│  RemoteCommandReceiver           │  parses incoming JSON commands
│  DeltaBroadcaster                │  emits JSON diffs on Parameter onChange
│  StateSerializer                 │  sends full tree snapshot on connect
└──────────────────────────────────┘
        │
    CommandStack (all mutations go through here — undo always works)
```

The GUI never holds the source of truth. It is a window onto the object tree. All mutations flow back through `CommandStack`, which means undo works for GUI edits, Lua edits, and CLI edits identically.

**Delta format (Main → GUI):**
```json
{ "type": "param_changed", "path": "scene/cam", "param": "fov", "value": 90.0 }
{ "type": "child_added",   "parent": "scene", "child": { "type": "Camera", "name": "cam2" } }
```

**Command format (GUI → Main):**
```json
{ "type": "SetParam", "path": "scene/cam", "param": "fov", "value": 90.0 }
{ "type": "Undo" }
{ "type": "ExecLua", "code": "root:findByPath('scene/cam').fov = 45" }
```

**Transport options:**

| Scenario | Library |
|---|---|
| Same machine, separate process | Unix socket / named pipe |
| Browser GUI | WebSocket via **uWebSockets** |
| Remote machine | gRPC or TCP + msgpack |

---

## 7. Node Graph System

### 7.1 Two Representations

```
EDIT GRAPH  (main thread, EnTT, full fidelity)
  — source of truth for topology and all parameters
  — undoable, scriptable, serializable

COMPILED GRAPH  (RT thread, flat POD arrays)
  — generated from edit graph when topology changes
  — swapped atomically (pointer swap) — no stop-the-world
  — source of truth for what actually executes
```

Parameter value changes do **not** require recompilation — they write directly to atomic slots in `SharedBuffers`. Only topology changes (add/remove node/link) trigger a recompile.

### 7.2 EnTT Components (Main Thread)

```cpp
struct NodeType      { StringID typeId; };
struct NameComponent { std::string name; };
struct NodeEditorPos { float x, y; };        // edit-only, never RT

struct NodeEditParams  { ParameterSet params; }; // main thread only

struct NodeRTParams {
    struct Entry { StringID name; uint32_t mirrorSlot; float defaultValue; };
    std::vector<Entry> entries;
};

struct NodeRTFeedback {
    struct Entry { StringID name; uint32_t feedbackSlot; };
    std::vector<Entry> entries;
};

struct NodePins {
    struct Pin { StringID name; bool isOutput; StringID dataType; uint32_t pinId; };
    std::vector<Pin> pins;
};

struct Link {
    entt::entity fromNode; uint32_t fromPinId;
    entt::entity toNode;   uint32_t toPinId;
};

struct ScriptNodeTag  {};  // marks nodes whose execFn is script-generated
struct SerializableTag{};
```

### 7.3 Data Categories

| Category | Written by | Read by | Crosses thread boundary |
|---|---|---|---|
| Edit params (label, color, position) | Main (GUI/Lua/undo) | Main only | Never |
| RT params (gain, threshold, setpoint) | Main → atomic slot | RT via atomic read | Atomic write on `set()` |
| RT feedback (output, error, status) | RT → atomic slot | Main via atomic read | Atomic write each cycle |
| Pin signals (data flowing between nodes) | RT signal scratch | RT signal scratch | Never |
| Graph topology | Main (EnTT) → compiled | RT (compiled graph ptr) | Atomic pointer swap |
| Undo history | CommandStack (main) | Main only | Never |

### 7.4 SharedBuffers

```cpp
struct SharedBuffers {
    std::array<std::atomic<float>, 4096> rtParams;    // main writes, RT reads
    std::array<std::atomic<float>, 4096> rtFeedback;  // RT writes, main reads
    std::atomic<uint32_t> nextRTParamSlot   { 0 };
    std::atomic<uint32_t> nextFeedbackSlot  { 0 };
} g_buffers;
```

### 7.5 CompiledGraph and Node Execution

```cpp
using NodeExecFn = void(*)(
    const float* rtParams,
    float*       rtFeedback,
    const float* inputSignals,
    float*       outputSignals,
    float        dt
);

enum class NodeExecMode { CPP, LuaCompiled, LuaInterpreted };

struct CompiledNode {
    NodeExecMode    mode;
    NodeExecFn      execFn;           // mode: CPP
    RTBytecode*     bytecode;         // mode: LuaCompiled
    float*          nodeState;        // mode: LuaCompiled (persistent scratch)
    LuaRTNodeState* luaState;         // mode: LuaInterpreted
    uint32_t        rtParamBase;
    uint32_t        rtFeedbackBase;
    uint32_t        inputSignalSlots[8];
    uint32_t        outputSignalSlots[8];
    uint8_t         inputCount, outputCount;
};

struct CompiledGraph {
    std::vector<CompiledNode> nodes;   // topologically sorted
    std::vector<float>        signalScratch;
    uint32_t                  signalBufferSize;
};

std::atomic<CompiledGraph*> g_activeGraph { nullptr };
```

Compiler runs on main thread: topological sort → assign signal slots to links → build `CompiledNode` array → atomic pointer swap. Old graph goes into a deferred-delete queue drained the next main tick.

---

## 8. Threading Model

### 8.1 Three Threads, Exclusive Ownership

```
MAIN THREAD
  Owns: EnTT registry, Object tree, Lua state, CommandStack, GUI state
  Runs: scripts, serialization, user input, GUI, undo/redo
  Timing: non-deterministic, may block

REALTIME THREAD
  Owns: CompiledGraph* (read-only ptr), signal scratch buffer
  Reads: g_buffers.rtParams (atomic)
  Writes: g_buffers.rtFeedback (atomic)
  Runs: compiled node graph — no allocation, no locks, no Lua, no EnTT
  Timing: strict cycle time, never allocates

I/O THREAD
  Owns: file handles, network sockets
  Runs: disk serialization (on snapshots), remote GUI WebSocket
  Timing: blocking I/O is fine here
```

### 8.2 Cross-Thread Communication

```
Main → RT:    std::atomic<float> writes to g_buffers.rtParams (per-param, wait-free)
              std::atomic<CompiledGraph*> swap (topology changes only)

RT → Main:    std::atomic<float> writes to g_buffers.rtFeedback (per-feedback, wait-free)
              rigtorp::SPSCQueue<RTEvent> for discrete events (limit reached, error, etc.)

Main → I/O:   rigtorp::SPSCQueue<IOTask> (string snapshots, no raw pointers)
I/O → Main:   rigtorp::SPSCQueue<IOResult>
```

No mutexes. No shared mutable state. No data races possible by construction.

### 8.3 Realtime Loop

```cpp
void realtimeThreadLoop() {
    // INVARIANTS: no heap allocation, no EnTT, no Lua, no mutex, no I/O
    while (running) {
        auto cycleStart = Clock::now();

        auto* graph = g_activeGraph.load(std::memory_order_acquire);
        if (graph) {
            std::fill(graph->signalScratch.begin(),
                      graph->signalScratch.end(), 0.f);

            for (auto& node : graph->nodes) {
                // dispatch by mode: CPP / LuaCompiled / LuaInterpreted
                executeNode(node, graph->signalScratch, dt);
            }
        }

        // Emit discrete events to main thread
        RTEvent evt;
        while (pendingEvents.pop(evt))
            gRTToMain.push(evt);

        sleepUntil(cycleStart + cycleInterval);
    }
}
```

---

## 9. Scripting System

### 9.1 Three Script Scopes

| Scope | `self` | Lifetime | Typical use |
|---|---|---|---|
| Global | root object or nil | App lifetime | Startup, project automation, macros |
| Per-object | Owning Object | Object lifetime | Reactive behavior, event handling |
| Script-defined type | Prototype table | Permanent (type) | Fully scripted object types |

### 9.2 Per-Object Script Events

Called on the main thread (full Lua available):

| Function | When called |
|---|---|
| `onInit()` | Script attached or project loaded |
| `onTick(dt)` | Each main tick (opt-in) |
| `onEvent(name, arg)` | `object:fireEvent("name", data)` |
| `onParamChanged(name, value)` | Any parameter on the owner changes |
| `onChildAdded(child)` | Child added to owner |
| `onChildRemoved(child)` | Child removed |
| `onSerialize(writer)` | Custom serialization |
| `onDeserialize(reader)` | Custom deserialization |
| `onGui()` | GUI inspector rendering |

### 9.3 Script-Defined Types

```lua
defineType("Flare", "SceneNode", function(proto)
    proto.params = { intensity = 1.0, color = Color(1, 0.8, 0.2) }

    function proto:onInit()    self.intensity = self.params.intensity end
    function proto:onTick(dt)  self.intensity = self.intensity + math.sin(time()) * 0.01 end
    function proto:onGui()     gui.slider("Intensity", self, "intensity", 0, 10) end
end)
```

`defineType` registers a `LuaObjectType` in `TypeRegistry`. The type is fully first-class — addressable by CLI, serializable by XML, instantiable from Lua, GUI, or CLI exactly like a C++ type.

### 9.4 Realtime Scripting

Three RT modes selected by a pragma at the top of the script:

```lua
--@realtime compiled      -- Lua → RT bytecode VM, hard RT safe, no GC in RT thread
--@realtime interpreted   -- LuaJIT, GC frozen on RT thread, collected on main thread
--@realtime none          -- main thread only (default)
```

**Annotations declare RT data slots** (resolved at compile time, never dynamic):

```lua
--@realtime compiled

--@param    gain       float  1.0
--@param    threshold  float  10.0
--@feedback output     float
--@feedback clipping   float
--@state    integral   float  0.0   -- persistent across ticks, pre-allocated
--@input    value      float
--@output   control    float

function onTick(dt)
    -- param/feedback/input/output/state accessed as raw C float arrays via LuaJIT FFI
    -- Zero allocation, zero boxing, near-C performance after JIT warmup
    local v = input.value
    state.integral = state.integral + v * param.gain * dt
    state.integral = math.min(state.integral, param.threshold)
    output.control    = state.integral
    feedback.output   = state.integral
    feedback.clipping = state.integral >= param.threshold and 1.0 or 0.0
end

-- These run on the main thread — full Lua available:
function onParamChanged(name, value)  end
function onGui()                      end
```

**Compiled mode** translates `onTick` to `RTBytecode` on the main thread. The RT thread runs a tiny stack VM (~300 lines C++, O(1) per instruction, no allocation). Rejects unsafe Lua features with an explicit error.

**Interpreted mode** uses one isolated `lua_State` per node, GC stopped on the RT thread, GC steps amortized on the main thread. LuaJIT FFI accesses param/feedback/signal buffers as raw C float arrays — zero overhead after JIT warmup.

### 9.5 Dynamic Property Addition from Scripts

```lua
function onInit()
    self:addParam("tag",      "string",  "untagged")
    self:addParam("priority", "int",     0)
    self:addParam("visible",  "bool",    true)
end
```

Dynamic parameters immediately become serializable, GUI-visible, CLI-accessible, and undoable.

### 9.6 Scripting Capabilities Summary

| Operation | Allowed | Notes |
|---|---|---|
| Read any parameter | ✅ | Always |
| Write any parameter | ✅ | Auto-undoable via CommandStack |
| Add dynamic parameter | ✅ | Immediately serializable |
| Remove dynamic parameter | ✅ | C++ params are fixed at compile time |
| Add/remove/reparent children | ✅ | Pushes structural Command |
| Define new type | ✅ | Registered in TypeRegistry |
| Override serialize | ✅ | Via `onSerialize` hook |
| Access other objects by path | ✅ | Via ObjectRegistry |
| Fire events on other objects | ✅ | `obj:fireEvent("name", data)` |
| Bypass CommandStack | ⚠️ | `setDirect()` opt-in, disables undo for that call |
| Raw file I/O / `os` | ⚠️ | Disabled by default in per-object environments |

---

## 10. GUI Layer

Fully decoupled from the object model. Two mechanisms:

**Per-type binding** (preferred):
```cpp
GuiRegistry::instance().bind<Camera>([](Camera& cam) {
    ImGui::SliderFloat("FOV",  &cam.fov.ref(), 10.f, 180.f);
    ImGui::SliderFloat("Near", &cam.near.ref(), 0.001f, 10.f);
});
```

**Generic fallback** (automatic — works for any Object with no custom binding):
```cpp
void drawGenericInspector(Object& obj) {
    for (ParameterBase* p : obj.parameters())
        drawParameter(*p);  // type-switch on typeName()
}
```

GUI can be local (Dear ImGui recommended) or fully remote (see Section 6.5). The object tree never holds GUI state.

---

## 11. Serialization Details

All serialization is XML via **pugixml**. Type names are XML tag names — files are diff-friendly and hand-editable.

**Schema version** is stored on the root element for forward compatibility.

**`ObjectHandle` values** are serialized as path strings (`"root/scene/cam"`) and resolved in a post-load fixup pass through `ObjectRegistry`. No dangling pointer risk.

**Node graph serialization** stores edit-graph data (EnTT components) only. The compiled graph is always regenerated on load — never persisted.

---

## 12. Recommended Third-Party Libraries

| Need | Library | Notes |
|---|---|---|
| ECS / component system | **EnTT** | Main thread object/node model |
| Lua scripting | **sol2 + LuaJIT** | Best C++ binding, near-C RT perf |
| XML serialization | **pugixml** | Fast, single-header, MIT |
| Lock-free queue | **rigtorp/SPSCQueue** | Header-only, proven in production |
| GUI | **Dear ImGui** | Immediate mode, no opinion on data |
| WebSocket | **uWebSockets** | I/O thread remote GUI |
| Build system | **CMake + CPM.cmake** | Dependency management |
| Testing | **Catch2** or **doctest** | Header-only |
| Logging | **spdlog** | Optional replacement for custom Logger |

---

## 13. Recommended Source Structure

```
project/
├── CMakeLists.txt
├── foundation/
│   ├── Signal.hpp
│   ├── StringID.hpp
│   ├── Logger.hpp
│   └── Allocators.hpp
├── core/
│   ├── Object.hpp / .cpp
│   ├── Component.hpp
│   ├── TypeRegistry.hpp / .cpp
│   └── ObjectRegistry.hpp / .cpp
├── params/
│   ├── ParameterBase.hpp
│   ├── Parameter.hpp
│   └── DynamicParameter.hpp
├── commands/
│   ├── Command.hpp
│   ├── CommandStack.hpp / .cpp
│   └── PropertyCommand.hpp
├── serialization/
│   ├── XmlSerializer.hpp / .cpp
│   ├── XmlWriter.hpp
│   └── XmlReader.hpp
├── graph/
│   ├── NodeComponents.hpp      (EnTT component structs)
│   ├── GraphCompiler.hpp / .cpp
│   ├── GraphManager.hpp / .cpp
│   ├── CompiledGraph.hpp
│   ├── SharedBuffers.hpp
│   ├── NodeTypeRegistry.hpp / .cpp
│   └── RTBytecodeVM.hpp / .cpp
├── scripting/
│   ├── ScriptEngine.hpp / .cpp
│   ├── ScriptComponent.hpp / .cpp
│   ├── LuaRTNode.hpp / .cpp
│   └── LuaToRTCompiler.hpp / .cpp
├── console/
│   └── Console.hpp / .cpp
├── remote/
│   ├── DeltaBroadcaster.hpp / .cpp
│   ├── RemoteCommandReceiver.hpp / .cpp
│   └── StateSerializer.hpp / .cpp
├── gui/
│   ├── GuiRegistry.hpp
│   └── GenericInspector.hpp / .cpp
└── app/
    └── App.hpp / .cpp
```

---

## 14. Recommended Build Order

1. `StringID`, `Signal`, `Logger` — foundation, zero dependencies
2. `Object`, `TypeRegistry`, `ObjectRegistry` — the tree exists
3. `Parameter<T>` — values with change notification
4. `CommandStack` + `PropertyCommand` — undo works for everything
5. `XmlSerializer` — save/load round-trips
6. `Console` — full CLI without needing a GUI
7. `NodeComponents` + `GraphCompiler` + `SharedBuffers` — node graph topology
8. `RealtimeThread` + C++ node exec — first RT cycle running
9. `ScriptEngine` + `ScriptComponent` — Lua on main thread
10. `LuaToRTCompiler` + `RTBytecodeVM` — scripted RT nodes
11. `DeltaBroadcaster` + `RemoteCommandReceiver` — remote GUI wire-up
12. GUI layer — last, everything else is already wired and testable

---

## 15. Key Invariants to Never Violate

- The realtime thread **never** allocates heap memory
- The realtime thread **never** calls into EnTT
- The realtime thread **never** touches Lua
- The realtime thread **never** takes a mutex
- The realtime thread **never** does file or network I/O
- Cross-object references **always** use `ObjectHandle<T>`, never raw pointers
- All object mutations **always** go through `CommandStack` (except `setDirect()` opt-out)
- The I/O thread **never** holds a live pointer into EnTT — works on string snapshots only
- GUI **never** mutates state directly — sends commands through `RemoteCommandReceiver` or directly to `CommandStack`