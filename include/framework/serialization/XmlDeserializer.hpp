#pragma once

#include <stack>

#include <pugixml.hpp>

#include "DeserializationValidation.hpp"
#include "XmlCommon.hpp"
#include "framework/core/EntityReference.hpp"
#include "framework/core/Project.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/serialization/EntityReferenceLinker.hpp"

namespace Xml {

class ComponentLoadVisitor : public ComponentVisitor {
public:
  ComponentLoadVisitor(Entity& entity, pugi::xml_node &root, DeserializationValidationReport &report)
      : m_entity(entity), m_report(report)
  {
    nodeStack.push(root);
  }

  pugi::xml_attribute getAttribute(const char *xmlTagName, const char *attributeName) {
    if (auto tag = nodeStack.top().child(xmlTagName)) {
      return tag.attribute(attributeName);
    }
    return pugi::xml_attribute();
  }

  virtual bool beginComponent(const char* componentTypeName) override {
    m_currentComponentType = componentTypeName;
    return true;
  }

  virtual void visit_property(const char *label, ParameterBase &p, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags))
      return;
    if (auto attr = getAttribute(label, "val")) {
      p.fromString(attr.as_string());
    } else
      m_report.addError(
        Severity::Warning,
        m_entity.getPath(),
        m_currentComponentType,
        "Error resolving Parameter \"" + std::string(label) + "\"",
        m_entity);
  }
  virtual void visit_property(const char *label, std::string &str, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;
    if (auto attr = getAttribute(label, "val")) str = attr.as_string();
    else m_report.addError(
      Severity::Warning,
      m_entity.getPath(),
      m_currentComponentType,
      "Error resolving string attribute \"" + std::string(label) + "\"",
      m_entity);
  };

  virtual void visit_property(const char *label, float &val, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;
    if (auto attr = getAttribute(label, "val")) val = attr.as_float();
    else m_report.addError(
      Severity::Warning,
      m_entity.getPath(),
      m_currentComponentType,
      "Error resolving float attribute \"" + std::string(label) + "\"",
      m_entity);
  };

  virtual void visit_property(const char *label, int &val, std::initializer_list<Tag> tags = {}) override {
    if (!isPersistent(tags)) return;
    if (auto attr = getAttribute(label, "val")) val = attr.as_int();
    else m_report.addError(
      Severity::Warning,
      m_entity.getPath(),
      m_currentComponentType,
      "Error resolving int attribute \"" + std::string(label) + "\"",
      m_entity);
  }

  virtual void visit_property(const char *label, EntityReference &ref, std::initializer_list<Tag> tags = {}) override {
    if (!isPersistent(tags)) return;
    if (auto attr = getAttribute(label, "UUID")) ref.uuid.value = attr.as_ullong();
    else m_report.addError(
      Severity::Warning,
      m_entity.getPath(),
      m_currentComponentType,
      "Error resolving EntityReference attribute \"" + std::string(label) + "\"",
      m_entity);
  }

  virtual void visit_property(const char *label, VectorAccessorBase &va, std::initializer_list<Tag> tags) override {
    if (!isPersistent(tags)) return;

    auto listXmlNode = nodeStack.top().child(label);
    if(!listXmlNode) {
      m_report.addError(
        Severity::Warning,
        m_entity.getPath(),
        m_currentComponentType,
        "Error resolving xml node of list attribute '" + std::string(label) + "'",
        m_entity);
      return;
    }


    auto attr = listXmlNode.attribute(listSizeTagString);
    if (!attr) {
      m_report.addError(
        Severity::Critical,
        m_entity.getPath(),
        m_currentComponentType, 
        "Error resolving size of list attribute '" + std::string(label) + "'",
        m_entity);
      return;
    }
    int listSize = attr.as_int();
    if(listSize < 0){
      m_report.addError(
        Severity::Critical,
        m_entity.getPath(),
        m_currentComponentType, 
        "Size of list attribute '" + std::string(label) + "' is negative: " + std::to_string(listSize),
        m_entity);
      return;
    }
    va.resize(listSize);

    nodeStack.push(listXmlNode);
    for (size_t i = 0; i < listSize; i++) {
      char label[64];
      std::snprintf(label, sizeof(label), listElementFormatString, i);
      va.visit_element(i, label, *this);
    }
    nodeStack.pop();
  }

private:
  std::stack<pugi::xml_node> nodeStack;

  DeserializationValidationReport &m_report;
  Entity m_entity;
  std::string m_currentComponentType;
};








  void loadEntity(Project& project, pugi::xml_node entityXmlNode, DeserializationValidationReport& report, Entity *parentEntity) {
    IdentityComponent identity;
    bool b_nameLoaded = false;
    if (auto attr = entityXmlNode.attribute("Name")){
      identity.name = attr.as_string();
      b_nameLoaded = true;
    }
    bool b_displayNameLoaded = false;
    if (auto attr = entityXmlNode.attribute("DisplayName")){
      identity.displayName = attr.as_string();
      b_displayNameLoaded = true;
    }
    bool b_uuidLoaded = false;
    if (auto attr = entityXmlNode.attribute("UUID")){
      identity.uuid.value = attr.as_ullong();
      b_uuidLoaded = true;
    }
    std::string sanitizedName = Project::sanitizeName(identity.name);

    Entity loadedEntity = project.restoreEntity(sanitizedName, identity.displayName, identity.uuid);
    if (parentEntity) loadedEntity.setParent(*parentEntity);

    if (!b_nameLoaded) report.addError(
      Severity::Warning,
      loadedEntity.getPath(),
      "IdentityComponent",
      "Error resolving entity Name",
      loadedEntity);
    else if (identity.name.empty()) report.addError(
      Severity::Warning,
      loadedEntity.getPath(),
      "IdentityComponent",
      "Entity name is empty",
      loadedEntity);
    if(identity.name != sanitizedName) report.addError(
      Severity::Warning,
      loadedEntity.getPath(),
      "IdentityComponent",
      "Entity name was sanitized from '" + identity.name + "' to '" + sanitizedName + "'",
      loadedEntity);

    if (!b_uuidLoaded) report.addError(
      Severity::Critical,
      loadedEntity.getPath(),
      "IdentityComponent",
      "Error resolving entity UUID",
      loadedEntity);
    else if (!identity.uuid.isValid()) report.addError(
      Severity::Critical,
      loadedEntity.getPath(),
      "IdentityComponent",
      "Invalid UUID value",
      loadedEntity);

    if(!b_displayNameLoaded) report.addError(
      Severity::Warning,
      loadedEntity.getPath(),
      "IdentityComponent",
      "Error resolving entity DisplayName",
      loadedEntity);
    else if(identity.displayName.empty()) report.addError(
      Severity::Warning,
      loadedEntity.getPath(),
      "IdentifyComponent",
      "Entity DisplayName is empty",
      loadedEntity);


    for (auto childXmlNode : entityXmlNode.children()) {
      std::string xmlTagName = childXmlNode.name();
      if (xmlTagName == childrenTagString) {
          for(auto childEntityXml : childXmlNode.children(entityTagString)){
            loadEntity(project, childEntityXml, report, &loadedEntity);
          }
      } else {
        ComponentLoadVisitor visitor(loadedEntity, childXmlNode, report);
        if (!ComponentRegistry::addReflectEntityComponent( loadedEntity.handle(), xmlTagName.c_str(), visitor))
          report.addError(
            Severity::Critical,
            loadedEntity.getPath(),
            xmlTagName,
            "Component Type does not exist in registry",
            loadedEntity);
      }
    }
    CommandStackVisitor visitor(&project.getStack());
    ComponentRegistry::reflectEntityComponents(loadedEntity.handle(), visitor);
  }



// Not intended for user, use Application::loadProject()
inline bool loadProject(Project &project, std::string_view filepath, DeserializationValidationReport &report) {
  pugi::xml_document doc;
  if (!doc.load_file(filepath.data())) return false;

  auto projectXml = doc.child(projectTagString);
  if(!projectXml) report.addError(
    Severity::Critical,
    {"Project"},
    "",
    "<Project> root xml tag is missing.");

  if(auto nameAttribute = projectXml.attribute("Name")){
    project.m_name = nameAttribute.as_string();
    if(project.m_name.empty()) report.addError(
      Severity::Warning,
      {"Project"},
      "ProjectIdentity",
      "ProjectName is empty");
  }
  else report.addError(
    Severity::Warning,
    {"Project"},
    "ProjectIdentify",
    "Error resolving ProjectName");

  if(!project.ids().loadState(projectXml)) report.addError(
    Severity::Critical,
    {"Project"},
    "IDGeneratorState",
    "Error Loading UUID Generator properties");

  for(auto childEntityXml : projectXml.children(entityTagString)){
    loadEntity(project, childEntityXml, report, nullptr);
  }

  return true;
}

} // namespace Xml