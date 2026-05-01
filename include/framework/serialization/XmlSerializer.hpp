#pragma once

#include <string_view>

#include <pugixml.hpp>
#include <entt/entt.hpp>

class Project;
class Object;
class XmlSaveVisitor;

class XmlSerializer {
public:
    static bool save(Project& project, std::string_view filepath);
    static bool load(Project& project, std::string_view filepath);

private:
    // Recursive Tree Walkers
    static void serializeObject(const Object& object, XmlSaveVisitor& visitor);
    static void deserializeObject(Project& project, pugi::xml_node& parentXml);
};

