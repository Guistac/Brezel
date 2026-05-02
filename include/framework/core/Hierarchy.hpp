#pragma once

#include <vector>
#include <entt/entt.hpp>

struct HierarchyComponent {
    entt::entity parent{ entt::null };
    std::vector<entt::entity> children;
};

struct NameComponent {
    std::string name;
};