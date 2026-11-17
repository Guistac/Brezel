// SPDX-License-Identifier: MIT
// Brezel tests. Copyright (c) 2026 Leo Becker. See LICENSE.md.
//
// Ownership: the replication ownership primitives as they are built (09_BREZEL.md section 3.5): the per-type authority,
// the per-entity rules, the receiving guard acceptsRemoteDelta, the local guards acceptsLocalEdit and acceptsLocalRename,
// RemoteMirrorTag, and the RenameEntity handler's use of the local guard. The model has two sides, "the server" and "a
// client"; these tests pin that model, not a future one. Headers: Core/ComponentRegistry.hpp, Core/CoreComponents.hpp,
// Concurrency/StandardMutations.hpp.

#include "BrezelTest.hpp"
#include "TestComponents.hpp"

#include <optional>
#include <string>
#include <vector>

using namespace Brezel;
using namespace BrezelTest;
using Authority = ComponentRegistry::ReplicationAuthority;

namespace {

/// Clears the process-wide rule list on construction and on destruction
struct RulesGuard {
    RulesGuard() { ComponentRegistry::authorityRules.clear(); }
    ~RulesGuard() { ComponentRegistry::authorityRules.clear(); }
};

/// A rule that gives every component of an entity holding a Marker to `owner`
void addMarkerRule(Authority owner) {
    ComponentRegistry::addAuthorityRule(
        [owner](const Entity& e, const ComponentRegistry::ComponentTypeInfo&) -> std::optional<Authority> {
            if (e.has<Marker>()) return owner;
            return std::nullopt;
        });
}

ComponentRegistry::ComponentTypeInfo typeInfo(Authority authority) {
    ComponentRegistry::ComponentTypeInfo info;
    info.authority = authority;
    return info;
}

const char* name(Authority a) {
    switch (a) {
        case Authority::LocalOnly: return "LocalOnly";
        case Authority::ServerAuthoritative: return "Server";
        case Authority::ClientAuthoritative: return "Client";
    }
    return "?";
}

} // namespace

BREZEL_TEST(authority_type_and_rules, "Core/ComponentRegistry.hpp",
            "authorityFor is the type's authority when no rule answers; rules run in registration order and the "
            "first that answers wins; a rule reads the entity's own components; an empty rule is not added") {
    RulesGuard guard;
    registerTestComponents();
    TestWorld world("Ownership", false);
    Entity plain = world.project.createEntity("plain");
    Entity marked = world.project.createEntity("marked");
    marked.add<Marker>();
    const auto serverType = typeInfo(Authority::ServerAuthoritative);
    CHECK(ComponentRegistry::authorityFor(marked, serverType) == Authority::ServerAuthoritative);

    ComponentRegistry::addAuthorityRule(nullptr);
    CHECK(ComponentRegistry::authorityRules.empty());
    ComponentRegistry::addAuthorityRule(
        [](const Entity&, const ComponentRegistry::ComponentTypeInfo&) -> std::optional<Authority> { return std::nullopt; });
    addMarkerRule(Authority::ClientAuthoritative);
    addMarkerRule(Authority::LocalOnly);  // never reached for a marked entity
    CHECK(ComponentRegistry::authorityFor(marked, serverType) == Authority::ClientAuthoritative);
    CHECK(ComponentRegistry::authorityFor(plain, serverType) == Authority::ServerAuthoritative);
}

BREZEL_TEST(accepts_remote_delta_matrix, "Core/ComponentRegistry.hpp",
            "acceptsRemoteDelta for every sender, type authority and rule: a LocalOnly sender is never refused; from "
            "a client, refused when the server owns the component (type or rule); from the server, refused only when "
            "a rule gives the component to the client and the type is not client-authoritative") {
    registerTestComponents();
    TestWorld world("Ownership", false);
    Entity marked = world.project.createEntity("marked");
    marked.add<Marker>();
    struct Row {
        Authority type;
        std::optional<Authority> rule;
        Authority sender;
        bool accepted;
    };
    const Authority L = Authority::LocalOnly, S = Authority::ServerAuthoritative, C = Authority::ClientAuthoritative;
    const std::vector<Row> rows = {
        // type LocalOnly
        {L, std::nullopt, L, true}, {L, std::nullopt, S, true}, {L, std::nullopt, C, true},
        {L, C, L, true}, {L, C, S, false}, {L, C, C, true},
        {L, S, L, true}, {L, S, S, true}, {L, S, C, false},
        // type ServerAuthoritative
        {S, std::nullopt, L, true}, {S, std::nullopt, S, true}, {S, std::nullopt, C, false},
        {S, C, L, true}, {S, C, S, false}, {S, C, C, true},
        {S, S, L, true}, {S, S, S, true}, {S, S, C, false},
        // type ClientAuthoritative: the server's edits are still taken (its remote control panel)
        {C, std::nullopt, L, true}, {C, std::nullopt, S, true}, {C, std::nullopt, C, true},
        {C, C, L, true}, {C, C, S, true}, {C, C, C, true},
        {C, S, L, true}, {C, S, S, true}, {C, S, C, false},
    };
    for (const Row& row : rows) {
        RulesGuard guard;
        if (row.rule) addMarkerRule(*row.rule);
        const bool accepted = ComponentRegistry::acceptsRemoteDelta(marked, typeInfo(row.type), row.sender);
        if (accepted != row.accepted) {
            BrezelTest::fail(__FILE__, __LINE__,
                             std::string("type ") + name(row.type) + ", rule " + (row.rule ? name(*row.rule) : "none") +
                                 ", sender " + name(row.sender) + ": expected " + (row.accepted ? "accepted" : "refused"));
        }
    }
}

BREZEL_TEST(accepts_local_edit_on_mirror, "Core/ComponentRegistry.hpp",
            "acceptsLocalEdit refuses only on an entity carrying RemoteMirrorTag when a rule gives the component to "
            "the client and the type is not client-authoritative; every other entity, and an invalid one, is "
            "editable") {
    RulesGuard guard;
    registerTestComponents();
    TestWorld world("Ownership", false);
    Entity mirror = world.project.createEntity("mirror");
    mirror.add<Marker>();
    mirror.add<RemoteMirrorTag>();
    Entity local = world.project.createEntity("local");
    local.add<Marker>();
    const auto serverType = typeInfo(Authority::ServerAuthoritative);
    const auto clientType = typeInfo(Authority::ClientAuthoritative);
    CHECK(ComponentRegistry::acceptsLocalEdit(mirror, serverType));  // no rule yet
    addMarkerRule(Authority::ClientAuthoritative);
    CHECK(!ComponentRegistry::acceptsLocalEdit(mirror, serverType));
    CHECK(ComponentRegistry::acceptsLocalEdit(mirror, clientType));
    CHECK(ComponentRegistry::acceptsLocalEdit(local, serverType));  // not a mirror
    CHECK(ComponentRegistry::acceptsLocalEdit(Entity{}, serverType));
    CHECK(!ComponentRegistry::componentInfoById.contains(entt::type_id<RemoteMirrorTag>().hash()));  // never registered
}

BREZEL_TEST(accepts_local_rename_and_rename_handler, "Core/ComponentRegistry.hpp",
            "acceptsLocalRename applies acceptsLocalEdit to IdentityComponent (true while IdentityComponent is not "
            "registered); the RenameEntity handler leaves a refused name unchanged and says nothing") {
    RulesGuard guard;
    registerTestComponents();
    TestWorld world("Ownership");  // registers IdentityComponent, server-authoritative
    Entity mirror = world.project.createEntityWithUUID(UUID(3001), "mirror");
    mirror.add<Marker>();
    mirror.add<RemoteMirrorTag>();
    addMarkerRule(Authority::ClientAuthoritative);
    CHECK(!ComponentRegistry::acceptsLocalRename(mirror));
    EventLog log;
    log.connect(world.dispatcher);
    world.apply(Mutations::RenameEntity(UUID(3001), {}, "renamed"));
    CHECK_EQ(mirror.getName(), std::string_view("mirror"));
    CHECK(log.events.empty());
    log.disconnect(world.dispatcher);

    mirror.remove<RemoteMirrorTag>();
    CHECK(ComponentRegistry::acceptsLocalRename(mirror));
    world.apply(Mutations::RenameEntity(UUID(3001), {}, "renamed"));
    CHECK_EQ(mirror.getName(), std::string_view("renamed"));

    mirror.add<RemoteMirrorTag>();
    const StringID identity = StringID::from("IdentityComponent");
    auto saved = ComponentRegistry::componentInfoByTypeName.at(identity);
    ComponentRegistry::componentInfoByTypeName.erase(identity);
    CHECK(ComponentRegistry::acceptsLocalRename(mirror));  // not registered: allowed
    ComponentRegistry::componentInfoByTypeName[identity] = saved;
}

BREZEL_TEST(authority_is_fixed_at_registration, "Core/ComponentRegistry.hpp",
            "a type's authority is the one given to registerComponent, LocalOnly by default; the three values are "
            "local (0), server (1) and client (2), two sides and local") {
    registerTestComponents();
    CHECK(ComponentRegistry::componentInfoByTypeName.at(StringID::from("Position")).authority == Authority::LocalOnly);
    ComponentRegistry::registerComponent<Position>("Position", ComponentRegistry::ComponentFlag::Networked,
                                                   Authority::ClientAuthoritative);
    CHECK(ComponentRegistry::componentInfoByTypeName.at(StringID::from("Position")).authority ==
          Authority::ClientAuthoritative);
    registerTestComponents();
    static_assert(static_cast<int>(Authority::LocalOnly) == 0);
    static_assert(static_cast<int>(Authority::ServerAuthoritative) == 1);
    static_assert(static_cast<int>(Authority::ClientAuthoritative) == 2);
}

BREZEL_TEST_MAIN("ownership")
