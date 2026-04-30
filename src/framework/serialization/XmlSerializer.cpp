#include "framework/serialization/XmlSerializer.hpp"
#include "framework/serialization/ComponentRegistry.hpp"
#include "framework/core/Hierarchy.hpp"

bool XmlSerializer::save(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    auto root = doc.append_child("Project");
    root.append_attribute("name") = project.getName().data();

    auto& reg = project.getRegistry();
    auto view = reg.view<HierarchyComponent>();
    
    for(auto entity : view) {
        if(view.get<HierarchyComponent>(entity).parent == entt::null) {
            serializeObject(reg, entity, root);
        }
    }
    return doc.save_file(filepath.data());
}

void XmlSerializer::serializeObject(entt::registry& reg, entt::entity entity, pugi::xml_node& parentXml) {
    auto node = parentXml.append_child("Object");
    node.append_attribute("name") = reg.get<NameComponent>(entity).name.c_str();

    // Ask the Registry to save any components it finds on this entity
    for (const auto& type : ComponentRegistry::instance().getTypes()) {
        type.save(entity, reg, node);
    }

    // Recurse Children
    auto& hier = reg.get<HierarchyComponent>(entity);
    for (auto child : hier.children) {
        serializeObject(reg, child, node);
    }
}

bool XmlSerializer::load(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    if (!doc.load_file(filepath.data())) return false;

    auto root = doc.child("Project");
    for (auto node : root.children("Object")) {
        deserializeObject(project.getRegistry(), node, entt::null);
    }
    return true;
}

void XmlSerializer::deserializeObject(entt::registry& reg, pugi::xml_node& node, entt::entity parent) {
    auto entity = reg.create();
    reg.emplace<NameComponent>(entity, node.attribute("name").as_string());
    
    auto& hier = reg.emplace<HierarchyComponent>(entity);
    hier.parent = parent;
    // ... add to parent's children list logic here ...

    for (const auto& type : ComponentRegistry::instance().getTypes()) {
        type.load(entity, reg, node);
    }

    for (auto childNode : node.children("Object")) {
        deserializeObject(reg, childNode, entity);
    }
}