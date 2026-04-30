#pragma once

#include <string_view>

#include <pugixml.hpp>
#include <entt/entt.hpp>

class Project;

class XmlSerializer {
public:
    static bool save(Project& project, std::string_view filepath);
    static bool load(Project& project, std::string_view filepath);

private:
    // Recursive Tree Walkers
    static void serializeObject(entt::registry& reg, entt::entity entity, pugi::xml_node& parentXml);
    static void deserializeObject(entt::registry& reg, pugi::xml_node& node, entt::entity parent);
};

