#pragma once

#include <string_view>

#include <pugixml.hpp>
#include <entt/entt.hpp>

class Project;
class Entity;
class XmlSaveVisitor;

class XmlSerializer {
public:
    static bool save(Project& project, std::string_view filepath);
    static bool load(Project& project, std::string_view filepath);

private:
    // Recursive Tree Walkers
    static void serializeEntity(const Entity& entity, XmlSaveVisitor& visitor);
    static void deserializeEntity(Project& project, pugi::xml_node& parentXml);
};

