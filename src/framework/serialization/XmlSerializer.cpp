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

    for(auto childObjectXml : projectXml.children("Object")){
        deserializeObject(project, childObjectXml);
    }

    return true;
}

void XmlSerializer::deserializeObject(Project& project, pugi::xml_node& objectXml) {
    std::string objectName = "";
    if(auto attr = objectXml.attribute("Name")){
        objectName = attr.value();
    }
    Object object = project.createObject(objectName);

    ComponentXmlLoadVisitor visitor(objectXml);

    for (auto childXml : objectXml.children()) {
        std::string_view xmlName = childXml.name();
        if(xmlName == "Children"){
            for(auto childObjectNode : childXml.children("Object")){
                deserializeObject(project, childObjectNode);
            }
        }
        else{
            if(const auto* info = ComponentRegistry::findInfoBySaveName(xmlName)){
                info->createComponent(object.handle());
                info->reflect(object.handle(), visitor);
            }
        }
    }
}