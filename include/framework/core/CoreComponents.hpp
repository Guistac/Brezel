#pragma once

#include <vector>
#include <entt/entt.hpp>

#include "UUID.hpp"

struct HierarchyComponent {
    entt::entity parent{ entt::null };
    std::vector<entt::entity> children;
};

struct IdentityComponent {
    std::string name;
    UUID uuid;
};