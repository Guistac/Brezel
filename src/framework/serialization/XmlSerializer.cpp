#include "framework/serialization/XmlSerializer.hpp"
#include "framework/serialization/ComponentRegistry.hpp"
#include "framework/core/Hierarchy.hpp"
#include "framework/core/Project.hpp"

bool XmlSerializer::save(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    ProjectXmlSaveVisitor visitor(project, doc);
    project.reflect(visitor);
    return doc.save_file(filepath.data());
}

bool XmlSerializer::load(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    if (!doc.load_file(filepath.data())) return false;

    auto projectXml = doc.child("Project");
    if(!projectXml) return false;

    if (auto attr = projectXml.attribute("Name")) {
        project.m_name = attr.value();
    }

    for(auto childEntityXml : projectXml.children("Entity")){
        deserializeEntity(project, childEntityXml);
    }

    return true;
}

void XmlSerializer::deserializeEntity(Project& project, pugi::xml_node& entityXml) {
    std::string name = "";
    if(auto attr = entityXml.attribute("Name")){
        name = attr.value();
    }
    Entity entity = project.createEntity(name);

    ComponentXmlLoadVisitor visitor(entityXml);

    for (auto childXml : entityXml.children()) {
        std::string_view xmlName = childXml.name();
        if(xmlName == "Children"){
            for(auto childEntitytXml : childXml.children("Entity")){
                deserializeEntity(project, childEntitytXml);
            }
        }
        else{
            if(const auto* info = ComponentRegistry::findInfoBySaveName(xmlName)){
                info->createComponent(entity.handle());
                info->reflect(entity.handle(), visitor);
            }
        }
    }
}