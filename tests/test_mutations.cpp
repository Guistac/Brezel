// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Mutations: the envelope (AnyMutation), the queue (MutationQueue), the standard mutations and their handlers, which
// hooks run and which events fire and which do not (start plan section 4 row 11), what a handler can report (row 2),
// what one tick applies (row 3), the order of listeners (last connected runs first), the attach and detach of channel components.
// Headers: Concurrency/AnyMutation.hpp, Concurrency/MutationQueue.hpp, Concurrency/StandardMutations.hpp,
// Core/ComponentRegistry.hpp (SetComponent, events, hooks), Concurrency/StreamComponent.hpp,
// Concurrency/QueueComponent.hpp, Core/Dispatcher.hpp.

#include "BrezelTest.hpp"
#include "TestComponents.hpp"

#include "Brezel/Concurrency/MutationQueue.hpp"
#include "Brezel/Serialization/BinaryVisitor.hpp"

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

using namespace Brezel;
using namespace BrezelTest;
using namespace Brezel::Mutations;

namespace {

template <typename T>
std::vector<uint8_t> encode(T& component, const char* saveString) {
    BinarySerializer s(BinaryPropertyFilter::All);
    if (s.beginComponent(StringID::from(saveString))) {
        reflect(component, s);
        s.endComponent();
    }
    return s.takeBuffer();
}

/// Counts the registry hooks of Position and Label while it lives; re-registers them without hooks on destruction
struct HookCounters {
    int created = 0, updated = 0, removed = 0;
    bool positionPresentInRemoveHook = true;
    HookCounters() {
        namespace CR = ComponentRegistry;
        CR::ComponentOptions options;
        options.flags = CR::ComponentFlag::Networked;
        options.onCreated = [this](Entity) { ++created; };
        options.onUpdated = [this](Entity) { ++updated; };
        options.onRemoved = [this](Entity e) {
            ++removed;
            positionPresentInRemoveHook = e.has<Position>();
        };
        CR::registerComponent<Position>("Position", options);
        CR::registerComponent<Label>("Label", options);
    }
    ~HookCounters() { registerTestComponents(); }
};

struct Trivial {
    int a{0};
    char pad[100]{};
};
struct Owning {
    std::shared_ptr<int> data;
    std::vector<int> list;
};
struct Orphan {
    int x{0};
};
struct Sink {
    std::vector<int> seen;
    void onTrivial(const Trivial& t) { seen.push_back(t.a); }
    void onOwning(const Owning& o) { seen.push_back(*o.data + static_cast<int>(o.list.size())); }
};

struct LateType {
    int v{0};
};
template <typename V>
void reflect(LateType& t, V& v) {
    v.visit_property("v", t.v, {Tag::Persistent});
}
struct LaterType {
    int v{0};
};
template <typename V>
void reflect(LaterType& t, V& v) {
    v.visit_property("v", t.v, {Tag::Persistent});
}

struct StreamPayload {
    int sample{0};
};
struct BackupTag {};

template <typename Q>
constexpr bool hasSize = requires(Q& q) { q.size(); };
template <typename Q>
constexpr bool hasCapacity = requires(Q& q) { q.capacity(); };

/// Listener on RenameEntity that records the name it sees
struct NameSpy {
    Project* project = nullptr;
    std::string seen;
    void on(const RenameEntity& m) {
        Entity e = project->resolveEntity(m.uuid, m.entity);
        seen = std::string(e.getName());
    }
};

/// Listener on EntitySpawned that records what the first new entity holds when its event fires, and whether the
/// thread may access the world there
struct SpawnSpy {
    Project* project = nullptr;
    bool recorded = false;
    bool hadPosition = true;
    bool mayAccess = false;
    void on(const Events::EntitySpawned& e) {
        if (recorded) return;
        recorded = true;
        hadPosition = e.entity.has<Position>();
        mayAccess = project->ecsWall().callerMayAccess();
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// The envelope and the queue
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(anymutation_value_semantics, "Concurrency/AnyMutation.hpp",
            "AnyMutation holds a trivially copyable struct or one with owning members; copy, move and destruction "
            "act on the held value exactly once each; a moved-from or default envelope is empty and executes nothing") {
    static_assert(AnyMutation::STORAGE_SIZE == 512);
    Dispatcher d;
    Sink sink;
    d.sink<Trivial>().connect<&Sink::onTrivial>(sink);
    d.sink<Owning>().connect<&Sink::onOwning>(sink);

    AnyMutation t(Trivial{5, {}});
    CHECK(static_cast<bool>(t));
    t.execute(d);
    AnyMutation tCopy = t;
    tCopy.execute(d);
    AnyMutation tMoved = std::move(t);
    CHECK(!static_cast<bool>(t));
    t.execute(d);  // empty: nothing
    tMoved.execute(d);
    CHECK((sink.seen == std::vector<int>{5, 5, 5}));

    auto data = std::make_shared<int>(10);
    {
        AnyMutation o(Owning{data, {1, 2}});
        CHECK_EQ(data.use_count(), long(2));
        AnyMutation copy = o;
        CHECK_EQ(data.use_count(), long(3));
        AnyMutation moved = std::move(copy);
        CHECK_EQ(data.use_count(), long(3));
        CHECK(!static_cast<bool>(copy));
        moved.execute(d);
        AnyMutation assigned;
        assigned = moved;
        CHECK_EQ(data.use_count(), long(4));
        assigned.reset();
        CHECK_EQ(data.use_count(), long(3));
        CHECK(!static_cast<bool>(assigned));
    }
    CHECK_EQ(data.use_count(), long(1));
    CHECK_EQ(sink.seen.back(), 12);

    AnyMutation empty;
    CHECK(!static_cast<bool>(empty));
    empty.execute(d);
    CHECK_EQ(sink.seen.size(), size_t(4));
    d.sink<Trivial>().disconnect(static_cast<const void*>(&sink));
    d.sink<Owning>().disconnect(static_cast<const void*>(&sink));
}

BREZEL_TEST(known_defect_B07_no_result, "Concurrency/AnyMutation.hpp",
            "a mutation has no result: execute() and every standard handler return void; a mutation type with no "
            "connected handler and a handler whose target is missing do nothing and say nothing (09_BREZEL.md B-07)") {
    static_assert(std::is_void_v<decltype(std::declval<const AnyMutation&>().execute(std::declval<Dispatcher&>()))>);
    static_assert(std::is_void_v<decltype(Internal::handleCreateEntity(std::declval<Project&>(),
                                                                       std::declval<const CreateEntity&>()))>);
    static_assert(std::is_void_v<decltype(Internal::handleUpdateComponent(std::declval<Project&>(),
                                                                          std::declval<const UpdateComponent&>()))>);
    TestWorld world("Mutations");
    EventLog log;
    log.connect(world.dispatcher);
    CHECK(world.dispatcher.sink<Orphan>().empty());
    world.apply(Orphan{1});                      // no handler: dropped
    world.apply(DestroyEntity{UUID(4242), {}});  // no such entity: nothing
    world.apply(RenameEntity(UUID(4242), {}, "x"));
    CHECK(log.events.empty());
    CHECK(world.project.getEntities().empty());
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(mutation_queue_order_and_threads, "Concurrency/MutationQueue.hpp",
            "push keeps the order of each producer; drain returns everything pushed so far and leaves the queue "
            "empty; four producer threads and one draining consumer lose nothing") {
    MutationQueue<int> q;
    q.push(1);
    int two = 2;
    q.push(two);
    q.push(3);
    CHECK((q.drain() == std::vector<int>{1, 2, 3}));
    CHECK(q.drain().empty());

    struct Item {
        int producer;
        int seq;
    };
    MutationQueue<Item> items;
    constexpr int kProducers = 4;
    constexpr int kPerProducer = 5000;
    Watchdog watchdog("queue threads", std::chrono::seconds(30));
    std::vector<int> lastSeq(kProducers, -1);
    int received = 0;
    bool ordered = true;
    {
        std::vector<std::unique_ptr<ScopedThread>> producers;
        for (int p = 0; p < kProducers; ++p) {
            producers.push_back(std::make_unique<ScopedThread>([&items, p](const std::atomic<bool>&) {
                for (int i = 0; i < kPerProducer; ++i) items.push(Item{p, i});
            }));
        }
        while (received < kProducers * kPerProducer) {
            for (const Item& it : items.drain()) {
                if (it.seq != lastSeq[it.producer] + 1) ordered = false;
                lastSeq[it.producer] = it.seq;
                ++received;
            }
            std::this_thread::yield();
        }
    }
    CHECK(ordered);
    CHECK_EQ(received, kProducers * kPerProducer);
    CHECK(items.drain().empty());
}

BREZEL_TEST(known_defect_B08_queue_unbounded, "Concurrency/MutationQueue.hpp",
            "the queue has no capacity and no count: 100000 pushes without a drain are all kept, and nothing can "
            "see the backlog (09_BREZEL.md B-08)") {
    static_assert(!hasSize<MutationQueue<int>>);
    static_assert(!hasCapacity<MutationQueue<int>>);
    MutationQueue<int> q;
    for (int i = 0; i < 100000; ++i) q.push(i);
    CHECK_EQ(q.drain().size(), size_t(100000));
}

// ---------------------------------------------------------------------------------------------------------------------
// The standard mutations
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(create_entity_mutation, "Concurrency/StandardMutations.hpp",
            "CreateEntity makes an entity with the given UUID (or a random one for 0), a sanitized name, and fires "
            "EntitySpawned with its UUID; the name is cut at 63 bytes") {
    TestWorld world("Mutations");
    EventLog log;
    log.connect(world.dispatcher);
    world.apply(CreateEntity(UUID(100), "Created One"));
    Entity e = world.project.getEntityByUUID(UUID(100));
    REQUIRE(e.isValid());
    CHECK_EQ(e.getName(), std::string_view("Created_One"));
    CHECK_EQ(e.getDisplayName(), std::string_view("Created One"));
    world.apply(CreateEntity(std::string(70, 'n')));
    REQUIRE(log.events.size() == 2);
    CHECK_EQ(log.events[0], std::string("spawned 100"));
    const uint64_t randomId = std::stoull(log.events[1].substr(8));
    Entity longName = world.project.getEntityByUUID(UUID(randomId));
    REQUIRE(longName.isValid());
    CHECK_EQ(longName.getName().size(), size_t(63));
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(destroy_entity_mutation, "Concurrency/StandardMutations.hpp",
            "DestroyEntity removes the entity found by UUID or by handle and fires EntityDestroyed with its UUID") {
    TestWorld world("Mutations");
    EventLog log;
    log.connect(world.dispatcher);
    Entity a = world.project.createEntityWithUUID(UUID(110), "a");
    Entity b = world.project.createEntityWithUUID(UUID(111), "b");
    world.apply(DestroyEntity{UUID(110), {}});
    world.apply(DestroyEntity{UUID(0), b});
    CHECK(!a.isValid());
    CHECK(!b.isValid());
    CHECK((log.events == std::vector<std::string>{"destroyed 110", "destroyed 111"}));
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(rename_entity_mutation, "Concurrency/StandardMutations.hpp",
            "RenameEntity sets name and display name to the new name, unsanitized, cut at 63 bytes") {
    TestWorld world("Mutations");
    Entity a = world.project.createEntityWithUUID(UUID(120), "a");
    world.apply(RenameEntity(UUID(120), {}, "New Name!"));
    CHECK_EQ(a.getName(), std::string_view("New Name!"));
    CHECK_EQ(a.getDisplayName(), std::string_view("New Name!"));
    world.apply(RenameEntity(UUID(0), a, std::string(80, 'r')));
    CHECK_EQ(a.getName().size(), size_t(63));
}

BREZEL_TEST(update_component_mutation, "Concurrency/StandardMutations.hpp",
            "UpdateComponent creates the component if absent (running onCreated), decodes the payload, runs "
            "onUpdated and fires ComponentChanged; a payload without the component header is decoded as raw "
            "properties; an unknown component or entity does nothing") {
    registerTestComponents();
    TestWorld world("Mutations");
    HookCounters hooks;
    EventLog log;
    log.connect(world.dispatcher);
    Entity e = world.project.createEntityWithUUID(UUID(130), "e");
    Position source{1.5f, 2.5f, 3};
    world.apply(UpdateComponent(UUID(130), StringID::from("Position"), encode(source, "Position")));
    REQUIRE(e.has<Position>());
    CHECK_EQ(e.get<Position>().x, 1.5f);
    CHECK_EQ(e.get<Position>().layer, 3);
    CHECK_EQ(hooks.created, 1);
    CHECK_EQ(hooks.updated, 1);

    Position second{4.0f, 5.0f, 6};
    BinarySerializer raw;  // properties only, no component header
    reflect(second, raw);
    world.apply(UpdateComponent(UUID(130), StringID::from("Position"), raw.takeBuffer()));
    CHECK_EQ(e.get<Position>().layer, 6);
    CHECK_EQ(hooks.created, 1);
    CHECK_EQ(hooks.updated, 2);

    world.apply(UpdateComponent(UUID(130), StringID::from("NoSuchType"), encode(source, "NoSuchType")));
    world.apply(UpdateComponent(UUID(4242), StringID::from("Position"), encode(source, "Position")));
    CHECK((log.events == std::vector<std::string>{"changed 130 Position", "changed 130 Position"}));
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(known_defect_B02_update_ignores_decode_error, "Concurrency/StandardMutations.hpp",
            "UpdateComponent never checks the decoder's error: a payload cut short leaves the fields before the cut "
            "written and the rest unchanged, and still runs onUpdated and fires ComponentChanged (09_BREZEL.md B-02)") {
    registerTestComponents();
    TestWorld world("Mutations");
    HookCounters hooks;
    EventLog log;
    log.connect(world.dispatcher);
    Entity e = world.project.createEntityWithUUID(UUID(140), "e");
    e.add<Position>(0.0f, 0.0f, 0);
    Position source{7.0f, 8.0f, 9};
    std::vector<uint8_t> payload = encode(source, "Position");
    payload.resize(payload.size() - 6);  // keeps the header and x, cuts y in the middle, drops layer
    world.apply(UpdateComponent(UUID(140), StringID::from("Position"), payload));
    CHECK_EQ(e.get<Position>().x, 7.0f);    // written before the cut
    CHECK_EQ(e.get<Position>().y, 0.0f);    // not decoded
    CHECK_EQ(e.get<Position>().layer, 0);
    CHECK_EQ(hooks.updated, 1);             // the half-written value is announced as an update
    CHECK((log.events == std::vector<std::string>{"changed 140 Position"}));
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(add_component_mutation, "Concurrency/StandardMutations.hpp",
            "AddComponent creates a registered component by save string with its defaults and runs onCreated once; "
            "on an entity that has it, nothing happens") {
    registerTestComponents();
    TestWorld world("Mutations");
    HookCounters hooks;
    Entity e = world.project.createEntityWithUUID(UUID(150), "e");
    world.apply(AddComponent(UUID(150), StringID::from("Label")));
    REQUIRE(e.has<Label>());
    CHECK(e.get<Label>().visible);
    CHECK_EQ(hooks.created, 1);
    e.get<Label>().text = "kept";
    world.apply(AddComponent(UUID(150), StringID::from("Label")));
    CHECK_EQ(e.get<Label>().text, std::string("kept"));
    CHECK_EQ(hooks.created, 1);
    world.apply(AddComponent(UUID(150), StringID::from("NoSuchType")));
    CHECK_EQ(hooks.created, 1);
}

BREZEL_TEST(remove_component_mutation, "Concurrency/StandardMutations.hpp",
            "RemoveComponent removes the component by save string, then runs onRemoved (the hook sees it gone); the "
            "hook runs even when the entity did not have the component") {
    registerTestComponents();
    TestWorld world("Mutations");
    HookCounters hooks;
    Entity e = world.project.createEntityWithUUID(UUID(160), "e");
    e.add<Position>();
    world.apply(RemoveComponent(UUID(160), StringID::from("Position")));
    CHECK(!e.has<Position>());
    CHECK_EQ(hooks.removed, 1);
    CHECK(!hooks.positionPresentInRemoveHook);
    world.apply(RemoveComponent(UUID(160), StringID::from("Position")));
    CHECK_EQ(hooks.removed, 2);
}

BREZEL_TEST(clear_project_mutation, "Concurrency/StandardMutations.hpp",
            "ClearProject empties the project and its UUID index") {
    TestWorld world("Mutations");
    world.project.createEntity("a");
    world.project.createEntity("b");
    world.apply(ClearProject{});
    CHECK(world.project.getEntities().empty());
    CHECK(world.project.read()->view<IdentityComponent>().empty());
}

BREZEL_TEST(known_defect_B06_events_missing, "Concurrency/StandardMutations.hpp",
            "only CreateEntity, DestroyEntity, UpdateComponent and SetComponent fire an event; AddComponent, "
            "RemoveComponent, RenameEntity, ClearProject, Project::clear(), a direct Entity write and clone fire "
            "none (09_BREZEL.md B-06)") {
    registerTestComponents();
    TestWorld world("Mutations");
    Entity e = world.project.createEntityWithUUID(UUID(170), "e");
    EventLog log;
    log.connect(world.dispatcher);
    world.apply(AddComponent(UUID(170), StringID::from("Label")));
    world.apply(RemoveComponent(UUID(170), StringID::from("Label")));
    world.apply(RenameEntity(UUID(170), {}, "renamed"));
    e.add<Position>();
    e.get<Position>().x = 3.0f;
    (void)e.clone();
    world.project.clear();
    world.project.createEntity("again");
    world.apply(ClearProject{});
    CHECK(log.events.empty());

    world.apply(CreateEntity(UUID(171), "fires"));
    Position p{1.0f, 1.0f, 1};
    world.apply(UpdateComponent(UUID(171), StringID::from("Position"), encode(p, "Position")));
    world.apply(SetComponent<Position>(UUID(171), Position{2.0f, 2.0f, 2}));
    world.apply(DestroyEntity{UUID(171), {}});
    CHECK((log.events == std::vector<std::string>{"spawned 171", "changed 171 Position", "changed 171 Position",
                                                  "destroyed 171"}));
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(set_component_mutation, "Core/ComponentRegistry.hpp",
            "SetComponent<T> replaces or adds the component, runs onUpdated (also when it created it; never "
            "onCreated) and fires ComponentChanged with the target's UUID; for an unregistered type no handler is "
            "connected and the mutation is dropped; a type registered after the handlers is connected through the "
            "active project") {
    registerTestComponents();
    TestWorld world("Mutations");
    HookCounters hooks;
    EventLog log;
    log.connect(world.dispatcher);
    Entity e = world.project.createEntityWithUUID(UUID(180), "e");
    world.apply(SetComponent<Position>(UUID(0), e, Position{1.0f, 2.0f, 3}));
    REQUIRE(e.has<Position>());
    CHECK_EQ(e.get<Position>().layer, 3);
    CHECK_EQ(hooks.created, 0);
    CHECK_EQ(hooks.updated, 1);
    CHECK((log.events == std::vector<std::string>{"changed 180 Position"}));

    CHECK(world.dispatcher.sink<SetComponent<Unregistered>>().empty());
    world.apply(SetComponent<Unregistered>(UUID(180), Unregistered{5}));
    CHECK(!e.has<Unregistered>());

    ComponentRegistry::registerComponent<LateType>("LateType");
    CHECK(!world.dispatcher.sink<SetComponent<LateType>>().empty());
    world.apply(SetComponent<LateType>(UUID(180), LateType{9}));
    REQUIRE(e.has<LateType>());
    CHECK_EQ(e.get<LateType>().v, 9);
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(known_defect_B11_active_project_is_the_last, "Core/ComponentRegistry.hpp",
            "with two projects, the last one given registerStandardMutationHandlers is the process-wide active one: "
            "a type registered afterwards gets its SetComponent handler on that project's dispatcher only "
            "(09_BREZEL.md B-11)") {
    TestWorld first("First");
    TestWorld second("Second");
    CHECK(ComponentRegistry::s_activeProject == &second.project);
    ComponentRegistry::registerComponent<LaterType>("LaterType");
    Entity a = first.project.createEntityWithUUID(UUID(190), "a");
    Entity b = second.project.createEntityWithUUID(UUID(191), "b");
    first.apply(SetComponent<LaterType>(UUID(190), LaterType{1}));
    second.apply(SetComponent<LaterType>(UUID(191), LaterType{2}));
    CHECK(!a.has<LaterType>());  // dropped: no handler on the first dispatcher
    CHECK(b.has<LaterType>());
}

// ---------------------------------------------------------------------------------------------------------------------
// One tick, listeners, channels
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(one_tick_applies_in_push_order, "Concurrency/StandardMutations.hpp",
            "a writer's tick (drain, one write() scope, execute each) applies the mutations in push order; each "
            "event fires inside its handler, on the writer thread inside the scope, before the next mutation runs; "
            "a mutation that finds no target is skipped and nothing applied before it is undone (no unit)") {
    registerTestComponents();
    TestWorld world("Mutations");
    EventLog log;
    log.connect(world.dispatcher);
    SpawnSpy spy{&world.project};
    world.dispatcher.sink<Events::EntitySpawned>().connect<&SpawnSpy::on>(spy);

    MutationQueue<AnyMutation> queue;
    Position p{1.0f, 2.0f, 3};
    queue.push(CreateEntity(UUID(200), "a"));
    queue.push(UpdateComponent(UUID(200), StringID::from("Position"), encode(p, "Position")));
    queue.push(UpdateComponent(UUID(999), StringID::from("Position"), encode(p, "Position")));  // no target
    queue.push(RenameEntity(UUID(200), {}, "renamed"));
    queue.push(CreateEntity(UUID(201), "b"));
    queue.push(DestroyEntity{UUID(201), {}});
    {
        auto tick = world.project.write();
        for (const AnyMutation& m : queue.drain()) m.execute(world.dispatcher);
    }
    Entity a = world.project.getEntityByUUID(UUID(200));
    REQUIRE(a.isValid());
    CHECK(a.has<Position>());
    CHECK_EQ(a.getName(), std::string_view("renamed"));
    CHECK(!world.project.getEntityByUUID(UUID(201)).isValid());
    CHECK((log.events == std::vector<std::string>{"spawned 200", "changed 200 Position", "spawned 201",
                                                  "destroyed 201"}));
    for (auto id : log.threads) CHECK(id == std::this_thread::get_id());
    CHECK(spy.recorded);
    CHECK(!spy.hadPosition);  // the spawn event of a fired before its Update ran (events are not deferred)
    CHECK(spy.mayAccess);
    world.dispatcher.sink<Events::EntitySpawned>().disconnect(static_cast<const void*>(&spy));
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(listener_order_last_connected_first, "Core/Dispatcher.hpp",
            "the listeners of one mutation struct run in reverse connection order (EnTT's sigh::publish walks its "
            "list from the end): a listener connected after the standard handlers runs before them and sees the old "
            "name, one connected before them runs after and sees the new name") {
    TestWorld world("Mutations", false);
    NameSpy connectedBefore{&world.project};
    NameSpy connectedAfter{&world.project};
    world.dispatcher.sink<RenameEntity>().connect<&NameSpy::on>(connectedBefore);
    registerStandardMutationHandlers(world.project, world.dispatcher);
    world.dispatcher.sink<RenameEntity>().connect<&NameSpy::on>(connectedAfter);
    world.project.createEntityWithUUID(UUID(210), "old");
    world.apply(RenameEntity(UUID(210), {}, "new"));
    CHECK_EQ(connectedAfter.seen, std::string("old"));   // ran first
    CHECK_EQ(connectedBefore.seen, std::string("new"));  // ran last
    world.dispatcher.sink<RenameEntity>().disconnect(static_cast<const void*>(&connectedBefore));
    world.dispatcher.sink<RenameEntity>().disconnect(static_cast<const void*>(&connectedAfter));
}

BREZEL_TEST(stream_and_queue_attach_detach, "Concurrency/StandardMutations.hpp",
            "AttachStream and AttachQueue put a component pointing to the caller's buffer on the entity (a tag type "
            "keeps two of the same payload apart); Detach removes it; the component prefers its shared_ptr and a "
            "component with no buffer refuses push and pop") {
    TestWorld world("Mutations");
    registerStreamMutationHandlers<StreamPayload>(world.project, world.dispatcher);
    registerStreamMutationHandlers<StreamPayload, BackupTag>(world.project, world.dispatcher);
    registerQueueMutationHandlers<StreamPayload, 8>(world.project, world.dispatcher);
    Entity e = world.project.createEntityWithUUID(UUID(220), "e");
    TripleBuffer<StreamPayload> primary;
    TripleBuffer<StreamPayload> backup;
    RingBuffer<StreamPayload, 8> ring;
    world.apply(AttachStream<StreamPayload>{UUID(220), {}, &primary});
    world.apply(AttachStream<StreamPayload, BackupTag>{UUID(220), {}, &backup});
    world.apply(AttachQueue<StreamPayload, 8>{UUID(220), {}, &ring});
    using Primary = StreamComponent<StreamPayload>;
    using Backup = StreamComponent<StreamPayload, BackupTag>;
    using Queue = QueueComponent<StreamPayload, 8>;
    REQUIRE(e.has<Primary>());
    REQUIRE(e.has<Backup>());
    CHECK(e.get<Primary>().get() == &primary);
    CHECK(e.get<Backup>().get() == &backup);
    REQUIRE(e.has<Queue>());
    auto& qc = e.get<Queue>();
    CHECK(qc.push(StreamPayload{4}));
    StreamPayload out{};
    CHECK(ring.pop(out));
    CHECK_EQ(out.sample, 4);

    world.apply(DetachStream<StreamPayload>{UUID(220), {}});
    world.apply(DetachQueue<StreamPayload, 8>{UUID(220), {}});
    CHECK(!e.has<Primary>());
    CHECK(e.has<Backup>());
    CHECK(!e.has<Queue>());

    StreamComponent<StreamPayload> both;
    TripleBuffer<StreamPayload> raw;
    both.rawBuffer = &raw;
    both.buffer = std::make_shared<TripleBuffer<StreamPayload>>();
    CHECK(both.get() == both.buffer.get());
    QueueComponent<StreamPayload, 8> none;
    CHECK(!none.isValid());
    CHECK(!none.push(StreamPayload{1}));
    CHECK(!none.pop(out));
    CHECK(none.empty());
    CHECK_EQ(none.size(), size_t(0));
}

BREZEL_TEST_MAIN("mutations")
