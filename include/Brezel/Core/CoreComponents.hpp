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