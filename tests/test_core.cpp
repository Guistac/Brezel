// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Core: the lifetime of a project and of an entity, identity and UUIDs (a duplicate included), entity references,
// the hierarchy, the ECS wall and the component registry (registration, reflection over an entity, cloning).
// Headers: Core/Project.hpp, Core/Entity.hpp, Core/CoreComponents.hpp, Core/UUID.hpp, Core/UUIDProvider.hpp,
// Core/EntityReference.hpp, Core/EcsWall.hpp, Core/ComponentRegistry.hpp.

#include "BrezelTest.hpp"
#include "TestComponents.hpp"

#include <cstdlib>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>
#define BREZEL_TEST_HAS_FORK 1
#endif

using namespace Brezel;
using namespace BrezelTest;

namespace {

/// A visitor that records the components and property labels it is shown
struct RecordingVisitor : ComponentVisitor {
    std::vector<std::string> components;
    std::vector<std::string> properties;
    bool beginComponent(StringID name) override {
        components.push_back(name.toString());
        return true;
    }
    void visit_property(StringID label, int&, std::initializer_list<Tag>) override { properties.push_back(label.toString()); }
    void visit_property(StringID label, float&, std::initializer_list<Tag>) override { properties.push_back(label.toString()); }
    void visit_property(StringID label, bool&, std::initializer_list<Tag>) override { properties.push_back(label.toString()); }
    void visit_property(StringID label, std::string&, std::initializer_list<Tag>) override { properties.push_back(label.toString()); }
};

bool contains(const std::vector<std::string>& v, const std::string& s) {
    for (const auto& x : v) if (x == s) return true;
    return false;
}

/// Sets BREZEL_ECS_WALL=count for the projects constructed while it lives (the wall reads it at construction)
struct CountModeEnvironment {
    CountModeEnvironment() { ::setenv("BREZEL_ECS_WALL", "count", 1); }
    ~CountModeEnvironment() { ::unsetenv("BREZEL_ECS_WALL"); }
};

} // namespace

// ---------------------------------------------------------------------------------------------------------------------
// Project and entity lifetime, identity
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(project_create_entity_identity, "Core/Project.hpp",
            "createEntity gives a random valid UUID, a sanitized name, the display name as typed, an empty hierarchy, "
            "and indexes the entity by UUID") {
    TestWorld world("Core", false);
    Entity e = world.project.createEntity("My Thing #1");
    REQUIRE(e.isValid());
    const auto& id = e.get<IdentityComponent>();
    CHECK(id.uuid.isValid());
    CHECK_EQ(id.name, std::string("My_Thing__1"));
    CHECK_EQ(id.displayName, std::string("My Thing #1"));
    CHECK_EQ(e.getName(), std::string_view("My_Thing__1"));
    CHECK_EQ(e.getDisplayName(), std::string_view("My Thing #1"));
    CHECK(e.getUUID() == id.uuid);
    REQUIRE(e.has<HierarchyComponent>());
    CHECK(!e.get<HierarchyComponent>().parent.isValid());
    CHECK(e.get<HierarchyComponent>().children.empty());
    CHECK(world.project.getEntityByUUID(id.uuid) == e);
    CHECK_EQ(world.project.getEntities().size(), size_t(1));
    CHECK_EQ(world.project.getName(), std::string_view("Core"));
}

BREZEL_TEST(project_sanitize_name, "Core/Project.hpp",
            "sanitizeName keeps letters, digits, '_' and '-', turns every other byte into '_', and names an empty "
            "result 'Entity'; getDisplayName falls back to the name when the display name is empty") {
    CHECK_EQ(Project::sanitizeName("a-b_C9"), std::string("a-b_C9"));
    CHECK_EQ(Project::sanitizeName("a b/c.d"), std::string("a_b_c_d"));
    CHECK_EQ(Project::sanitizeName(""), std::string("Entity"));
    CHECK_EQ(Project::sanitizeName("\xC3\xA9"), std::string("__"));  // two bytes of one non-ASCII character
    TestWorld world("Core", false);
    Entity e = world.project.restoreEntity("strict", "", UUID(5));
    CHECK_EQ(e.getDisplayName(), std::string_view("strict"));
}

BREZEL_TEST(project_create_with_uuid, "Core/Project.hpp",
            "createEntityWithUUID keeps a valid UUID and draws a random one for UUID 0") {
    TestWorld world("Core", false);
    Entity a = world.project.createEntityWithUUID(UUID(42), "a");
    CHECK(a.getUUID() == UUID(42));
    CHECK(world.project.getEntityByUUID(UUID(42)) == a);
    Entity b = world.project.createEntityWithUUID(UUID(0), "b");
    CHECK(b.getUUID().isValid());
    CHECK(!(b.getUUID() == UUID(42)));
    CHECK(world.project.getEntityByUUID(b.getUUID()) == b);
}

BREZEL_TEST(known_defect_B20_restore_uuid_zero_indexed, "Core/Project.hpp",
            "restoreEntity keeps the name as given (no sanitizing) and, unlike createEntityWithUUID, keeps UUID 0: "
            "the first such entity is indexed under key 0, a second is not (09_BREZEL.md B-20)") {
    TestWorld world("Core", false);
    Entity a = world.project.restoreEntity("not sanitized", "A", UUID(0));
    CHECK_EQ(a.getName(), std::string_view("not sanitized"));
    CHECK(!a.getUUID().isValid());
    CHECK(world.project.getEntityByUUID(UUID(0)) == a);
    Entity b = world.project.restoreEntity("second", "B", UUID(0));
    CHECK(b.isValid());
    CHECK(world.project.getEntityByUUID(UUID(0)) == a);  // the second is not indexed
    CHECK_EQ(world.project.getEntities().size(), size_t(1));
}

BREZEL_TEST(project_destroy_entity, "Core/Project.hpp",
            "destroyEntity erases the UUID index entry and destroys the entity; the handle becomes invalid") {
    TestWorld world("Core", false);
    Entity e = world.project.createEntity("gone");
    const UUID id = e.getUUID();
    world.project.destroyEntity(e);
    CHECK(!e.isValid());
    CHECK(!world.project.getEntityByUUID(id).isValid());
    CHECK(world.project.getEntities().empty());
}

BREZEL_TEST(known_defect_B04_duplicate_uuid, "Core/Project.hpp",
            "a second entity with a UUID already present is created with the same UUID but not indexed; destroying "
            "it erases the index entry of the first, which then cannot be found by UUID (09_BREZEL.md B-04)") {
    TestWorld world("Core", false);
    Entity first = world.project.createEntityWithUUID(UUID(7), "first");
    Entity duplicate = world.project.createEntityWithUUID(UUID(7), "duplicate");
    CHECK(duplicate.isValid());
    CHECK(duplicate.getUUID() == UUID(7));
    CHECK(world.project.getEntityByUUID(UUID(7)) == first);  // the first keeps the slot
    CHECK_EQ(world.project.getEntities().size(), size_t(1));
    world.project.destroyEntity(duplicate);
    CHECK(first.isValid());                                   // the first still exists...
    CHECK(!world.project.getEntityByUUID(UUID(7)).isValid()); // ...but cannot be found by its UUID
}

BREZEL_TEST(project_clear, "Core/Project.hpp",
            "clear() empties the registry and the UUID index and fires no event") {
    TestWorld world("Core", true);
    EventLog log;
    log.connect(world.dispatcher);
    Entity a = world.project.createEntity("a");
    Entity b = world.project.createEntity("b");
    world.project.clear();
    CHECK(!a.isValid());
    CHECK(!b.isValid());
    CHECK(world.project.getEntities().empty());
    CHECK(log.events.empty());
    log.disconnect(world.dispatcher);
}

BREZEL_TEST(known_defect_B19_writeaccess_bypasses_index, "Core/Project.hpp",
            "WriteAccess::destroy(e) and WriteAccess::clear() leave the UUID index holding handles to dead entities "
            "(09_BREZEL.md B-19)") {
    TestWorld world("Core", false);
    Entity a = world.project.createEntityWithUUID(UUID(11), "a");
    Entity b = world.project.createEntityWithUUID(UUID(12), "b");
    {
        auto w = world.project.write();
        w.destroy(a.handle().entity());
    }
    CHECK(!a.isValid());
    CHECK_EQ(world.project.getEntities().size(), size_t(2));       // still two index entries
    CHECK(!world.project.getEntityByUUID(UUID(11)).isValid());     // one of them is a dead handle
    {
        auto w = world.project.write();
        w.clear();
    }
    CHECK(!b.isValid());
    CHECK_EQ(world.project.getEntities().size(), size_t(2));
}

BREZEL_TEST(project_resolve_entity, "Core/Project.hpp",
            "resolveEntity tries the UUID first, then a valid fallback handle of the same registry; a fallback from "
            "another registry is refused") {
    TestWorld world("Core", false);
    TestWorld other("Other", false);
    Entity a = world.project.createEntityWithUUID(UUID(21), "a");
    Entity b = world.project.createEntityWithUUID(UUID(22), "b");
    Entity foreign = other.project.createEntityWithUUID(UUID(23), "foreign");
    CHECK(world.project.resolveEntity(UUID(21), b) == a);          // the UUID wins over the fallback
    CHECK(world.project.resolveEntity(UUID(99), b) == b);          // unknown UUID: the fallback
    CHECK(world.project.resolveEntity(UUID(0), b) == b);
    CHECK(!world.project.resolveEntity(UUID(99), foreign).isValid());  // another registry: refused
    CHECK(!world.project.resolveEntity(UUID(99)).isValid());
}

// ---------------------------------------------------------------------------------------------------------------------
// Hierarchy
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(hierarchy_parent_children_path, "Core/Entity.hpp",
            "setParent links both sides; getParent, getPath, forEachChildEntity, getEntityByPath and "
            "forEachTopLevelEntity follow the links") {
    TestWorld world("Core", false);
    Entity root = world.project.createEntity("Root");
    Entity child = world.project.createEntity("Child");
    Entity grand = world.project.createEntity("Grand");
    Entity other = world.project.createEntity("Other");
    child.setParent(root);
    grand.setParent(child);
    CHECK(child.getParent() == root);
    CHECK(grand.getParent() == child);
    CHECK(!root.getParent().isValid());
    CHECK((grand.getPath() == std::vector<std::string>{"Root", "Child", "Grand"}));
    std::vector<Entity> children;
    root.forEachChildEntity([&](Entity& c) { children.push_back(c); });
    REQUIRE(children.size() == 1);
    CHECK(children[0] == child);
    CHECK(world.project.getEntityByPath("Root/Child/Grand") == grand);
    CHECK(world.project.getEntityByPath("Root/Child") == child);
    CHECK(!world.project.getEntityByPath("Root/Nobody").isValid());
    CHECK(!world.project.getEntityByPath("").isValid());
    std::set<std::string> topLevel;
    world.project.forEachTopLevelEntity([&](Entity& e) { topLevel.insert(std::string(e.getName())); });
    CHECK((topLevel == std::set<std::string>{"Root", "Other"}));
    (void)other;
}

BREZEL_TEST(hierarchy_path_first_token_any_depth, "Core/Project.hpp",
            "getEntityByPath matches its first token against every entity's name at any depth, not only the top "
            "level") {
    TestWorld world("Core", false);
    Entity root = world.project.createEntity("Root");
    Entity child = world.project.createEntity("Deep");
    child.setParent(root);
    CHECK(world.project.getEntityByPath("Deep") == child);
}

BREZEL_TEST(known_defect_B13_hierarchy_dangling_links, "Core/Entity.hpp",
            "setParent does not detach from a previous parent, and destroyEntity leaves the child in its parent's "
            "list; forEachChildEntity skips the dead handle (09_BREZEL.md B-13)") {
    TestWorld world("Core", false);
    Entity first = world.project.createEntity("First");
    Entity second = world.project.createEntity("Second");
    Entity child = world.project.createEntity("Child");
    child.setParent(first);
    child.setParent(second);
    CHECK(child.getParent() == second);
    CHECK_EQ(first.get<HierarchyComponent>().children.size(), size_t(1));  // still listed under the old parent
    CHECK_EQ(second.get<HierarchyComponent>().children.size(), size_t(1));
    world.project.destroyEntity(child);
    CHECK_EQ(second.get<HierarchyComponent>().children.size(), size_t(1));  // dangling entry
    int visited = 0;
    second.forEachChildEntity([&](Entity&) { ++visited; });
    CHECK_EQ(visited, 0);
}

// ---------------------------------------------------------------------------------------------------------------------
// UUIDs and references
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(uuid_value_and_derivation, "Core/UUID.hpp",
            "UUID 0 is invalid; deriveFromString and deriveFromBytes are 64-bit FNV-1a, deterministic, never 0, and "
            "empty or null input gives UUID(1); std::hash uses the value") {
    CHECK(!UUID().isValid());
    CHECK(UUID(1).isValid());
    CHECK(UUID(3) == UUID(3));
    CHECK(UUID(3) != UUID(4));
    CHECK(UUID(3) < UUID(4));
    CHECK_EQ(static_cast<uint64_t>(UUID(9)), uint64_t(9));
    // FNV-1a 64 of "a" is 0xaf63dc4c8601ec8c (published test vector)
    CHECK_EQ(UUID::deriveFromString("a").value, uint64_t(0xaf63dc4c8601ec8cULL));
    CHECK(UUID::deriveFromString("same") == UUID::deriveFromString("same"));
    CHECK(UUID::deriveFromString("one") != UUID::deriveFromString("two"));
    CHECK(UUID::deriveFromString("") == UUID(1));
    CHECK(UUID::deriveFromBytes(nullptr, 4) == UUID(1));
    const uint8_t bytes[] = {0x61};
    CHECK(UUID::deriveFromBytes(bytes, 1) == UUID::deriveFromString("a"));
    CHECK_EQ(std::hash<UUID>{}(UUID(77)), std::hash<uint64_t>{}(77));
}

BREZEL_TEST(uuid_provider_generate, "Core/UUIDProvider.hpp",
            "generate() never returns 0, and 10000 draws on one thread are distinct; a second thread draws a "
            "different sequence") {
    std::set<uint64_t> seen;
    for (int i = 0; i < 10000; ++i) {
        UUID id = UUIDProvider::generate();
        CHECK(id.isValid());
        seen.insert(id.value);
    }
    CHECK_EQ(seen.size(), size_t(10000));
    Watchdog watchdog("uuid thread", std::chrono::seconds(10));
    uint64_t otherFirst = 0;
    {
        ScopedThread t([&](const std::atomic<bool>&) { otherFirst = UUIDProvider::generate().value; });
    }
    CHECK(otherFirst != 0);
    CHECK(seen.count(otherFirst) == 0);
}

BREZEL_TEST(entity_reference_set, "Core/EntityReference.hpp",
            "set(entity) copies the handle and the entity's UUID; an entity without identity keeps the old UUID") {
    TestWorld world("Core", false);
    Entity a = world.project.createEntityWithUUID(UUID(31), "a");
    EntityReference ref;
    CHECK(!ref.uuid.isValid());
    ref.set(a);
    CHECK(ref.uuid == UUID(31));
    CHECK(ref.entity == a);
    Entity bare(world.project.write().registry().create(), a.registry());
    ref.set(bare);
    CHECK(ref.entity == bare);
    CHECK(ref.uuid == UUID(31));
}

// ---------------------------------------------------------------------------------------------------------------------
// Entity handle
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(entity_component_access, "Core/Entity.hpp",
            "add replaces an existing component (emplace_or_replace) and forwards up to four constructor arguments; "
            "has, try_get, get and remove act on the registry; a default Entity is invalid; equality is by handle") {
    TestWorld world("Core", false);
    Entity e = world.project.createEntity("e");
    CHECK(!e.has<Position>());
    CHECK(e.try_get<Position>() == nullptr);
    e.add<Position>(1.0f, 2.0f, 3);
    REQUIRE(e.has<Position>());
    CHECK_EQ(e.get<Position>().layer, 3);
    e.add<Position>(4.0f, 5.0f, 6);  // replaces
    CHECK_EQ(e.get<Position>().x, 4.0f);
    e.add_or_replace<Position>(Position{7.0f, 8.0f, 9});
    CHECK_EQ(e.get<Position>().layer, 9);
    e.remove<Position>();
    CHECK(!e.has<Position>());
    Entity none;
    CHECK(!none.isValid());
    CHECK(!static_cast<bool>(none));
    Entity copy = e;
    CHECK(copy == e);
    CHECK(copy != none);
    CHECK(&e.registry() == &copy.registry());
}

// ---------------------------------------------------------------------------------------------------------------------
// The ECS wall
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(ecs_wall_scope_writer_waiver, "Core/EcsWall.hpp",
            "an entity access off the writer thread with no read() or write() scope is a violation recorded with its "
            "site; a scope or the writer thread passes; a read scope passes like a write scope; under a waiver the "
            "access is counted as waived with the waiver's reason") {
    CountModeEnvironment countMode;
    Project project("Wall");
    CHECK(project.ecsWall().mode() == EcsWall::Mode::Count);
    Entity e;
    {
        auto w = project.write();
        e = project.createEntity("e");
        e.add<Position>();  // inside write(): passes
    }
    CHECK_EQ(project.ecsWall().totalViolations(), uint64_t(0));

    Watchdog watchdog("wall threads", std::chrono::seconds(10));
    unsigned violationLine = 0;
    {
        ScopedThread t([&](const std::atomic<bool>&) {
            violationLine = __LINE__ + 1;
            (void)e.has<Position>();  // no scope, not the writer
        });
    }
    CHECK_EQ(project.ecsWall().totalViolations(), uint64_t(1));
    REQUIRE(project.ecsWall().siteCount() == 1);
    EcsWall::SiteInfo site;
    REQUIRE(project.ecsWall().site(0, site));
    CHECK_EQ(site.line, violationLine);
    CHECK(std::string(site.file).find("test_core.cpp") != std::string::npos);
    CHECK_EQ(site.count, uint64_t(1));

    {
        ScopedThread t([&](const std::atomic<bool>&) {
            auto r = project.read();
            (void)e.has<Position>();
            (void)e.get<Position>();  // a mutable reference under a read scope: not told apart
        });
    }
    CHECK_EQ(project.ecsWall().totalViolations(), uint64_t(1));

    {
        ScopedThread t([&](const std::atomic<bool>&) {
            project.setWriterThread(std::this_thread::get_id());
            (void)e.has<Position>();
            project.setWriterThread(std::thread::id{});
        });
    }
    CHECK_EQ(project.ecsWall().totalViolations(), uint64_t(1));

    {
        ScopedThread t([&](const std::atomic<bool>&) {
            EcsWallWaiver waiver("test thread", "a known access", "none");
            (void)e.has<Position>();
            (void)e.has<Position>();
        });
    }
    CHECK_EQ(project.ecsWall().totalViolations(), uint64_t(1));
    CHECK_EQ(project.ecsWall().waivedAccesses(), uint64_t(2));
    REQUIRE(project.ecsWall().waivedSiteCount() == 1);
    EcsWall::SiteInfo waived;
    REQUIRE(project.ecsWall().waivedSite(0, waived));
    CHECK_EQ(std::string(waived.reason), std::string("a known access"));
    CHECK_EQ(waived.count, uint64_t(2));
    CHECK(EcsWallWaiver::current() == nullptr);
}

BREZEL_TEST(known_defect_B10_wall_overflow_passes_all, "Core/EcsWall.hpp",
            "a thread holding scopes of more than 8 projects at once passes the wall check of every project while the "
            "overflowing scope is held (09_BREZEL.md B-10)") {
    CountModeEnvironment countMode;
    std::vector<std::unique_ptr<Project>> held;
    for (int i = 0; i < 9; ++i) held.push_back(std::make_unique<Project>("Held"));
    Project unrelated("Unrelated");
    Entity e;
    {
        auto w = unrelated.write();
        e = unrelated.createEntity("e");
    }
    Watchdog watchdog("overflow thread", std::chrono::seconds(10));
    bool passedWhileOverflowing = false;
    bool passedAfter = true;
    {
        ScopedThread t([&](const std::atomic<bool>&) {
            {
                std::vector<Project::ReadAccess> scopes;
                scopes.reserve(held.size());
                for (auto& p : held) scopes.push_back(p->read());  // 9 projects: the ninth overflows
                passedWhileOverflowing = unrelated.ecsWall().callerMayAccess();
            }
            passedAfter = unrelated.ecsWall().callerMayAccess();
        });
    }
    CHECK(passedWhileOverflowing);
    CHECK(!passedAfter);
}

#if defined(BREZEL_TEST_HAS_FORK) && !defined(NDEBUG)
BREZEL_TEST(ecs_wall_assert_mode_aborts, "Core/EcsWall.hpp",
            "in a build without NDEBUG and without BREZEL_ECS_WALL=count, a violation prints the site and aborts") {
    Project project("Abort");
    if (project.ecsWall().mode() != EcsWall::Mode::Assert) {
        std::printf("    skipped: BREZEL_ECS_WALL=count is set in the environment\n");
        return;
    }
    Entity e;
    {
        auto w = project.write();
        e = project.createEntity("e");
    }
    std::fflush(nullptr);
    const pid_t child = ::fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        ::close(2);       // keep the expected abort message out of the test output
        (void)e.has<Position>();
        std::_Exit(0);    // not reached when the wall aborts
    }
    int status = 0;
    ::waitpid(child, &status, 0);
    CHECK(WIFSIGNALED(status));
    if (WIFSIGNALED(status)) CHECK_EQ(WTERMSIG(status), SIGABRT);
}
#endif

// ---------------------------------------------------------------------------------------------------------------------
// The component registry
// ---------------------------------------------------------------------------------------------------------------------

BREZEL_TEST(registry_register_component, "Core/ComponentRegistry.hpp",
            "registerComponent stores the type info by type id and by save string, with the display name defaulting "
            "to the save string, the category to General, isTag from the category Tags, the flags and the authority; "
            "registering again replaces the entry and keeps a custom drawer") {
    namespace CR = ComponentRegistry;
    registerTestComponents();
    const auto& byName = CR::componentInfoByTypeName.at(StringID::from("Position"));
    const auto& byId = CR::componentInfoById.at(entt::type_id<Position>().hash());
    CHECK(byName.saveString == StringID::from("Position"));
    CHECK(byId.saveString == byName.saveString);
    CHECK_EQ(byName.displayName, std::string("Position"));
    CHECK_EQ(byName.category, std::string("General"));
    CHECK(!byName.isTag);
    CHECK(CR::isNetworked(byName.flags));
    CHECK(!CR::isTransient(byName.flags));
    CHECK(byName.authority == CR::ReplicationAuthority::LocalOnly);
    const auto& marker = CR::componentInfoByTypeName.at(StringID::from("Marker"));
    CHECK(marker.isTag);
    CHECK_EQ(marker.category, std::string("Tags"));
    CHECK(CR::isTransient(CR::componentInfoByTypeName.at(StringID::from("Scratch")).flags));

    CR::setCustomDrawer<Position>([](Entity) { return true; });
    CR::registerComponent<Position>("Position", {.displayName = "Place", .category = "Space",
                                                 .flags = CR::ComponentFlag::Networked,
                                                 .authority = CR::ReplicationAuthority::ServerAuthoritative});
    const auto& again = CR::componentInfoByTypeName.at(StringID::from("Position"));
    CHECK_EQ(again.displayName, std::string("Place"));
    CHECK_EQ(again.category, std::string("Space"));
    CHECK(again.authority == CR::ReplicationAuthority::ServerAuthoritative);
    CHECK(static_cast<bool>(CR::componentInfoById.at(entt::type_id<Position>().hash()).customDrawer));
    registerTestComponents();  // back to the defaults for the other tests
}

BREZEL_TEST(registry_reflect_entity_components, "Core/ComponentRegistry.hpp",
            "reflectEntityComponents shows a visitor every registered component of an entity except IdentityComponent "
            "(and never an unregistered one); reflectEntityComponent reflects one by save string; "
            "addReflectEntityComponent creates it first") {
    registerTestComponents();
    registerBrezelCoreComponents();
    TestWorld world("Core", false);
    Entity e = world.project.createEntity("e");
    e.add<Position>(1.0f, 2.0f, 3);
    e.add<Label>();
    e.add<Unregistered>();
    e.add<Opaque>();
    RecordingVisitor all;
    ComponentRegistry::reflectEntityComponents(e, all);
    CHECK(contains(all.components, "Position"));
    CHECK(contains(all.components, "Label"));
    CHECK(contains(all.components, "Opaque"));          // registered, but its catch-all reflect visits nothing
    CHECK(!contains(all.components, "IdentityComponent"));
    CHECK_EQ(all.components.size(), size_t(3));
    CHECK(!contains(all.properties, "value"));          // Unregistered is never shown
    CHECK(!contains(all.properties, "hidden"));

    RecordingVisitor one;
    CHECK(ComponentRegistry::reflectEntityComponent(e, StringID::from("Position"), one));
    CHECK((one.properties == std::vector<std::string>{"x", "y", "layer"}));
    RecordingVisitor missing;
    CHECK(!ComponentRegistry::reflectEntityComponent(e, StringID::from("Nested"), missing));
    CHECK(!ComponentRegistry::reflectEntityComponent(e, StringID::from("NoSuchType"), missing));
    CHECK(missing.components.empty());

    RecordingVisitor adding;
    CHECK(!e.has<Nested>());
    CHECK(ComponentRegistry::addReflectEntityComponent(e, StringID::from("Nested"), adding));
    CHECK(e.has<Nested>());
    CHECK((adding.properties == std::vector<std::string>{"id", "a", "b"}));
    CHECK(!ComponentRegistry::addReflectEntityComponent(e, StringID::from("NoSuchType"), adding));
}

BREZEL_TEST(known_defect_B21_custom_drawer_before_register, "Core/ComponentRegistry.hpp",
            "setCustomDrawer before registerComponent creates an entry with an empty reflect; the next reflection of "
            "an entity holding the type throws std::bad_function_call (09_BREZEL.md B-21)") {
    struct DrawnFirst {
        int v{0};
    };
    ComponentRegistry::setCustomDrawer<DrawnFirst>([](Entity) { return false; });
    TestWorld world("Core", false);
    Entity e = world.project.createEntity("e");
    e.add<DrawnFirst>();
    RecordingVisitor visitor;
    bool threw = false;
    try {
        ComponentRegistry::reflectEntityComponents(e, visitor);
    } catch (const std::bad_function_call&) {
        threw = true;
    }
    CHECK(threw);
    ComponentRegistry::componentInfoById.erase(entt::type_id<DrawnFirst>().hash());  // leave no broken entry behind
}

BREZEL_TEST(known_defect_clone_outside_index, "Core/ComponentRegistry.hpp",
            "Entity::clone copies the registered, non-transient components into a new top-level entity with a new "
            "UUID and the name '<name> (Copy)', but does not enter the UUID index (Framework_Guide.md 4.4)") {
    registerTestComponents();
    TestWorld world("Core", false);
    Entity parent = world.project.createEntity("Parent");
    Entity source = world.project.createEntity("Source");
    source.setParent(parent);
    source.add<Position>(1.0f, 2.0f, 3);
    source.add<Scratch>(Scratch{5});
    source.add<Unregistered>();
    Entity copy = source.clone();
    REQUIRE(copy.isValid());
    CHECK_EQ(copy.getName(), std::string_view("Source (Copy)"));
    CHECK(copy.getUUID().isValid());
    CHECK(copy.getUUID() != source.getUUID());
    REQUIRE(copy.has<Position>());
    CHECK_EQ(copy.get<Position>().layer, 3);
    CHECK(!copy.has<Scratch>());        // transient: not copied
    CHECK(!copy.has<Unregistered>());   // unregistered: not copied
    CHECK(!copy.getParent().isValid()); // top level
    CHECK(!world.project.getEntityByUUID(copy.getUUID()).isValid());  // not in the index
    Entity named = source.clone("Named");
    CHECK_EQ(named.getName(), std::string_view("Named"));
}

BREZEL_TEST(core_registration_identity, "Core/ComponentRegistry.hpp",
            "registerBrezelCoreComponents registers IdentityComponent once, networked and server-authoritative; "
            "reflect(IdentityComponent) visits name and displayName, not the UUID") {
    registerBrezelCoreComponents();
    const auto& info = ComponentRegistry::componentInfoByTypeName.at(StringID::from("IdentityComponent"));
    CHECK(ComponentRegistry::isNetworked(info.flags));
    CHECK(info.authority == ComponentRegistry::ReplicationAuthority::ServerAuthoritative);
    IdentityComponent id{"n", "d", UUID(5)};
    RecordingVisitor v;
    reflect(id, v);
    CHECK((v.properties == std::vector<std::string>{"name", "displayName"}));
}

BREZEL_TEST_MAIN("core")
