#pragma once
#include <vector>
#include <string>
#include <entt/entt.hpp>
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

struct HierarchyComponent {
    entt::entity parent{ entt::null };
    std::vector<entt::entity> children;
};

struct IdentityComponent {
    std::string name;
    std::string displayName;
    UUID uuid;
};

} // namespace Brezel