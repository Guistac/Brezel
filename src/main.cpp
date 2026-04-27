#include <iostream>
#include <spdlog/spdlog.h>
#include <entt/entt.hpp>
#include <pugixml.hpp>

// Your "Day 1" Foundation
struct TagComponent {
    std::string name;
};

int main() {
    // 1. Test Logging
    spdlog::info("Starting Realtime Framework...");

    // 2. Test EnTT (ECS)
    entt::registry registry;
    auto entity = registry.create();
    registry.emplace<TagComponent>(entity, "MainProcessor");

    auto& tag = registry.get<TagComponent>(entity);
    spdlog::info("Created Entity with Tag: {}", tag.name);

    // 3. Test Pugixml
    pugi::xml_document doc;
    auto root = doc.append_child("Project");
    root.append_attribute("version") = "1.0";
    
    spdlog::info("Framework Skeleton Initialized Successfully.");

    return 0;
}