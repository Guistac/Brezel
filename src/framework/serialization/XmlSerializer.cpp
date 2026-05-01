#include "framework/serialization/XmlSerializer.hpp"
#include "framework/serialization/ComponentRegistry.hpp"
#include "framework/core/Hierarchy.hpp"
#include "framework/core/Project.hpp"

bool XmlSerializer::save(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    XmlProjectVisitor visitor(project, doc);
    project.reflect(visitor);
    return doc.save_file(filepath.data());
}

bool XmlSerializer::load(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    if (!doc.load_file(filepath.data())) return false;

    auto root = doc.child("Project");
    for (auto node : root.children("Object")) {
        deserializeObject(project, node);
    }
    return true;
}



void XmlSerializer::deserializeObject(Project& project, pugi::xml_node& node) {
    Object obj = project.createObject("");

    XmlLoadVisitor visitor(node, &project.getStack());
    ComponentRegistry::reflectEntityComponents(obj.handle(), visitor);

    for (auto childNode : node.children()) {
        std::string_view nodeName = childNode.name();
        if(nodeName == "Children"){
            for(auto childObjectNode : childNode.children("Object")){
                deserializeObject(project, childObjectNode);
            }
        }
        else{
            if(const auto* info = ComponentRegistry::findInfoBySaveName(nodeName)){
                info->createComponent(obj.handle());
                info->reflect(obj.handle(), visitor);
            }
        }
    }
}