#pragma once
#include <vector>
#include <string>
#include "Brezel/Core/UUID.hpp"
#include "Brezel/Core/Entity.hpp"

namespace Brezel {

struct HierarchyComponent {
    Entity parent;
    std::vector<Entity> children;
};

struct IdentityComponent {
    std::string name;
    std::string displayName;
    UUID uuid;
};

/**
 * @brief Marker: this entity is the local mirror of an entity hosted by a remote client (a proxy).
 *
 * Added and removed by the application code that binds the mirror (on the world's single writer). Read by
 * ComponentRegistry::acceptsLocalEdit(): on a mirror, a component that an authority rule gives to the client is
 * authored by the remote host only, so a local edit is refused instead of being kept locally and reverted at the next
 * session up. Runtime state: deliberately not registered with ComponentRegistry, so never saved, replicated, cloned or
 * shown.
 *
 * Interim: one case of a fully featured authoring model that does not exist yet (local edit policy for every
 * component, edits routed to the owner so a remote edit is applied there, the node's role, ownership declaration and
 * transfer, conflicts, persistence). See Framework_Guide.md "Guard on the local side".
 */
struct RemoteMirrorTag {};

} // namespace Brezel

#include "Brezel/Reflection/Visitor.hpp"

namespace Brezel {

template <typename V>
void reflect(IdentityComponent& id, V& v) {
    v.visit_property(StringID::from("name"), id.name, {Tag::Persistent, Tag::Networked});
    v.visit_property(StringID::from("displayName"), id.displayName, {Tag::Persistent, Tag::Networked});
}

} // namespace Brezel