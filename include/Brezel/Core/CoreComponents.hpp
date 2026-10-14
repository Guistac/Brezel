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

} // namespace Brezel

#include "Brezel/Reflection/Visitor.hpp"

namespace Brezel {

template <typename V>
void reflect(IdentityComponent& id, V& v) {
    v.visit_property(StringID::from("name"), id.name, {Tag::Persistent, Tag::Networked});
    v.visit_property(StringID::from("displayName"), id.displayName, {Tag::Persistent, Tag::Networked});
}

} // namespace Brezel