#pragma once
#include <stack>
#include <tinyxml2.h>
#include "Brezel/Serialization/DeserializationValidation.hpp"
#include "Brezel/Serialization/XmlCommon.hpp"
#include "Brezel/Core/EntityReference.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Serialization/EntityReferenceLinker.hpp"

namespace Brezel {

namespace Xml {

class ComponentLoadVisitor : public ComponentVisitor {
public:
  ComponentLoadVisitor(Entity& entity, tinyxml2::XMLElement *root, DeserializationValidationReport &report)
      : m_entity(entity), m_report(report)
  {
    nodeStack.push(root);
  }

  const char* getAttribute(StringID xmlTagName, const char *attributeName) {
    if (auto tag = nodeStack.top()->FirstChildElement(xmlTagName.toString().c_str())) {
      return tag->Attribute(attributeName);
    }
    return nullptr;
  }

  virtual bool beginComponent(StringID componentTypeName) override {
    m_currentComponentType = componentTypeName;
    return true;
  }


  virtual void visit_property(StringID label, std::string &str, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;
    if (auto attr = getAttribute(label, "val")) str = attr;
    else {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Warning,
            path,
            m_currentComponentType.toString(),
            "Error resolving string attribute \"" + label.toString() + "\"",
            m_entity);
    }
  };

  virtual void visit_property(StringID label, StringID &sid, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;
    if (auto attr = getAttribute(label, "val")) sid = StringID::from(attr);
    else {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Warning,
            path,
            m_currentComponentType.toString(),
            "Error resolving StringID attribute \"" + label.toString() + "\"",
            m_entity);
    }
  }

  virtual void visit_property(StringID label, float &val, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;
    if (auto tag = nodeStack.top()->FirstChildElement(label.toString().c_str())) {
        tag->QueryFloatAttribute("val", &val);
    }
    else {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Warning,
            path,
            m_currentComponentType.toString(),
            "Error resolving float attribute \"" + label.toString() + "\"",
            m_entity);
    }
  };

  virtual void visit_property(StringID label, int &val, std::initializer_list<Tag> tags = {}) override {
    if (!isPersistent(tags)) return;
    if (auto tag = nodeStack.top()->FirstChildElement(label.toString().c_str())) {
        tag->QueryIntAttribute("val", &val);
    }
    else {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Warning,
            path,
            m_currentComponentType.toString(),
            "Error resolving int attribute \"" + label.toString() + "\"",
            m_entity);
    }
  }

  virtual void visit_property(StringID label, EntityReference &ref, std::initializer_list<Tag> tags = {}) override {
    if (!isPersistent(tags)) return;
    if (auto tag = nodeStack.top()->FirstChildElement(label.toString().c_str())) {
        uint64_t val = 0;
        if(tag->QueryUnsigned64Attribute("UUID", &val) == tinyxml2::XML_SUCCESS) ref.uuid.value = val;
    }
    else {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Warning,
            path,
            m_currentComponentType.toString(),
            "Error resolving EntityReference attribute \"" + label.toString() + "\"",
            m_entity);
    }
  }

  virtual void visit_property(StringID label, VectorAccessorBase &va, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;

    auto listXmlNode = nodeStack.top()->FirstChildElement(label.toString().c_str());
    if(!listXmlNode) {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Warning,
            path,
            m_currentComponentType.toString(),
            "Error resolving xml node of list attribute '" + label.toString() + "'",
            m_entity);
        return;
    }

    int listSize = 0;
    if (listXmlNode->QueryIntAttribute(listSizeTagString, &listSize) != tinyxml2::XML_SUCCESS) {
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Critical,
            path,
            m_currentComponentType.toString(), 
            "Error resolving size of list attribute '" + label.toString() + "'",
            m_entity);
        return;
    }
    if(listSize < 0){
        std::vector<std::string> path;
        for(auto& s : m_entity.getPath()) path.push_back(s);
        m_report.addError(
            Severity::Critical,
            path,
            m_currentComponentType.toString(), 
            "Size of list attribute '" + label.toString() + "' is negative: " + std::to_string(listSize),
            m_entity);
        return;
    }
    va.resize(listSize);

    nodeStack.push(listXmlNode);
    for (size_t i = 0; i < (size_t)listSize; i++) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), listElementFormatString, i);
      va.visit_element(i, StringID::from(buf), *this);
    }
    nodeStack.pop();
  }

private:
  std::stack<tinyxml2::XMLElement*> nodeStack;

  DeserializationValidationReport &m_report;
  Entity m_entity;
  StringID m_currentComponentType;
};


inline void loadEntity(Project& project, tinyxml2::XMLElement* entityXmlNode, DeserializationValidationReport& report, Entity *parentEntity) {
    std::string name = "";
    std::string displayName = "";
    UUID uuid(0);

    bool b_nameLoaded = false;
    if (auto attr = entityXmlNode->Attribute("Name")){
      name = attr;
      b_nameLoaded = true;
    }
    bool b_displayNameLoaded = false;
    if (auto attr = entityXmlNode->Attribute("DisplayName")){
      displayName = attr;
      b_displayNameLoaded = true;
    }
    bool b_uuidLoaded = false;
    uint64_t val = 0;
    if (entityXmlNode->QueryUnsigned64Attribute("UUID", &val) == tinyxml2::XML_SUCCESS){
      uuid.value = val;
      b_uuidLoaded = true;
    }
    std::string sanitizedName = Project::sanitizeName(name);

    Entity loadedEntity = project.restoreEntity(sanitizedName, displayName, uuid);
    if (parentEntity) loadedEntity.setParent(*parentEntity);

    std::vector<std::string> entityPath = loadedEntity.getPath();

    if (!b_nameLoaded) report.addError(
      Severity::Warning,
      entityPath,
      "IdentityComponent",
      "Error resolving entity Name",
      loadedEntity);
    else if (name.empty()) report.addError(
      Severity::Warning,
      entityPath,
      "IdentityComponent",
      "Entity name is empty",
      loadedEntity);
    
    if(name != sanitizedName) report.addError(
      Severity::Warning,
      entityPath,
      "IdentityComponent",
      "Entity name was sanitized from '" + name + "' to '" + sanitizedName + "'",
      loadedEntity);

    if (!b_uuidLoaded) report.addError(
      Severity::Critical,
      entityPath,
      "IdentityComponent",
      "Error resolving entity UUID",
      loadedEntity);
    else if (!uuid.isValid()) report.addError(
      Severity::Critical,
      entityPath,
      "IdentityComponent",
      "Invalid UUID value",
      loadedEntity);

    if(!b_displayNameLoaded) report.addError(
      Severity::Warning,
      entityPath,
      "IdentityComponent",
      "Error resolving entity DisplayName",
      loadedEntity);
    else if(displayName.empty()) report.addError(
      Severity::Warning,
      entityPath,
      "IdentityComponent",
      "Entity DisplayName is empty",
      loadedEntity);

    for (auto childXmlNode = entityXmlNode->FirstChildElement(); childXmlNode; childXmlNode = childXmlNode->NextSiblingElement()) {
      std::string xmlTagName = childXmlNode->Name();
      if (xmlTagName == childrenTagString) {
          for(auto childEntityXml = childXmlNode->FirstChildElement(entityTagString); childEntityXml; childEntityXml = childEntityXml->NextSiblingElement(entityTagString)){
            loadEntity(project, childEntityXml, report, &loadedEntity);
          }
      } else {
        ComponentLoadVisitor visitor(loadedEntity, childXmlNode, report);
        if (!ComponentRegistry::addReflectEntityComponent( loadedEntity, StringID::from(xmlTagName), visitor))
          report.addError(
            Severity::Warning,
            entityPath,
            xmlTagName,
            "Component Type does not exist in registry",
            loadedEntity);
      }
    }

}


inline bool loadProject(Project &project, std::string_view filepath, DeserializationValidationReport &report) {
  tinyxml2::XMLDocument doc;
  if (doc.LoadFile(filepath.data()) != tinyxml2::XML_SUCCESS) return false;

  auto projectXml = doc.FirstChildElement(projectTagString);
  if(!projectXml) report.addError(
    Severity::Critical,
    {"Project"},
    "",
    "<Project> root xml tag is missing.");

  if(auto nameAttribute = projectXml->Attribute("Name")){
    project.m_name = nameAttribute;
    if(project.m_name.empty()) report.addError(
      Severity::Warning,
      {"Project"},
      "ProjectIdentity",
      "ProjectName is empty");
  }
  else report.addError(
    Severity::Warning,
    {"Project"},
    "ProjectIdentity",
    "Error resolving ProjectName");

  if(!project.ids().loadState(projectXml)) report.addError(
    Severity::Critical,
    {"Project"},
    "IDGeneratorState",
    "Error Loading UUID Generator properties");

  for(auto childEntityXml = projectXml->FirstChildElement(entityTagString); childEntityXml; childEntityXml = childEntityXml->NextSiblingElement(entityTagString)){
    loadEntity(project, childEntityXml, report, nullptr);
  }

  return true;
}

} // namespace Xml

} // namespace Brezel