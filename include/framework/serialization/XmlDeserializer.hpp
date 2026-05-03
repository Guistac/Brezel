#pragma once

#include <stack>

#include <pugixml.hpp>

#include "XmlCommon.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/core/Project.hpp"
#include "framework/core/EntityReference.hpp"
#include "framework/serialization/EntityReferenceLinker.hpp"
#include "DeserializationValidation.hpp"

namespace Xml{


class ProjectLoadVisitor : public ProjectVisitor{
public:
    ProjectLoadVisitor(Project& project, pugi::xml_node& root, DeserializationValidationReport& report) :
        m_project(project),
        m_report(report)
    { nodeStack.push(root); }

    void pathPush(const std::string& name){ m_pathStack.push_back(name); }
    void pathPop(){ if(!m_pathStack.empty()) m_pathStack.pop_back(); }

    virtual bool beginProject() override {
        if(auto projectXml = nodeStack.top().child(projectTagString)){
            pathPush("Project");
            nodeStack.push(projectXml);
            m_project.ids().loadState(projectXml);
            return true;
        }

        m_report.addError(Severity::Critical, m_pathStack, "", "<Project> root xml tag is missing.");
        return false;
    }
    virtual void endProject() override {
        loadChildEntities(nullptr);
        nodeStack.pop();
        pathPop();
    }

    void loadChildEntities(Entity* parent){
        for(auto childEntitytXml : nodeStack.top().children(entityTagString)){
            nodeStack.push(childEntitytXml);
            Entity loadedEntity = loadEntity(parent);
            nodeStack.pop();
        }
    }

    Entity loadEntity(Entity* parent){
        IdentityComponent identity;
        bool b_uuidLoaded = true;
        bool b_nameLoaded = true;
        if(auto attr = nodeStack.top().attribute("Name")) identity.name = attr.as_string();
        else b_nameLoaded = false;
        if(auto attr = nodeStack.top().attribute("UUID")) identity.uuid.value = attr.as_ullong();
        else b_uuidLoaded = false;

        Entity loadedEntity = m_project.restoreEntity(identity.name, identity.uuid);
        if(b_nameLoaded) pathPush(identity.name);
        else pathPush("?????");
        m_currentEntity = loadedEntity;

        if(!b_nameLoaded){
            m_report.addError(Severity::Warning, m_pathStack, "IdentityComponent", "Error resolving entity Name", loadedEntity);
        }
        else if(identity.name.empty()){
            m_report.addError(Severity::Warning, m_pathStack, "IdentityComponent", "Entity name is empty", loadedEntity);
        }
        if(!b_uuidLoaded){
            m_report.addError(Severity::Critical, m_pathStack, "IdentityComponent", "Error resolving entity UUID", loadedEntity);
        }
        else if(!identity.uuid.isValid()){
            m_report.addError(Severity::Critical, m_pathStack, "IdentityComponent", "Invalid UUID value", loadedEntity);
        } 

        for (auto childXml : nodeStack.top().children()) {
            std::string xmlTagName = childXml.name();
            nodeStack.push(childXml);
            if(xmlTagName == childrenTagString){
                loadChildEntities(&loadedEntity);
            }
            else{
                m_currentComponentType = xmlTagName;
                if(!ComponentRegistry::addReflectEntityComponent(loadedEntity.handle(), xmlTagName.c_str(), *this)){
                    m_report.addError(Severity::Critical, m_pathStack, m_currentComponentType, "Component Type does not exist in registry", loadedEntity);
                }
            }
            nodeStack.pop();
        }
        CommandStackVisitor visitor(&m_project.getStack());
        ComponentRegistry::reflectEntityComponents(loadedEntity.handle(), visitor);

        if(parent) loadedEntity.setParent(*parent);

        pathPop();
        return loadedEntity;
    }

    pugi::xml_attribute getAttribute(const char* xmlTagName, const char* attributeName){
        if(auto tag = nodeStack.top().child(xmlTagName)){
            return tag.attribute(attributeName);
        }
        return pugi::xml_attribute();
    }

    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if(!isPersistent(tags)) return;
        if(auto attr = getAttribute(label, "val")){
            p.fromString(attr.as_string());
            if(hasCommandStack(tags)) p.setCommandStack(&m_project.getStack());
        }
        else m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving Parameter \"" + std::string(label) + "\"", m_currentEntity);
    }
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override{
        if(!isPersistent(tags)) return;
        if(auto attr = getAttribute(label, "val")){
            str = attr.as_string();
        }
        else m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving string attribute \"" + std::string(label) + "\"", m_currentEntity);
    };

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override{
        if(!isPersistent(tags)) return;
        if(auto attr = getAttribute(label, "val")){
            val = attr.as_float();
        }
        else m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving float attribute \"" + std::string(label) + "\"", m_currentEntity);
    };

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags = {}) override {
        if(!isPersistent(tags)) return;
        if(auto attr = getAttribute(label, "val")){
            val = attr.as_int();
        }
        else m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving int attribute \"" + std::string(label) + "\"", m_currentEntity);
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        if(!isPersistent(tags)) return;
        if(auto attr = getAttribute(label, "UUID")){
            ref.uuid.value = attr.as_ullong();
        }
        else m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving EntityReference attribute \"" + std::string(label) + "\"", m_currentEntity);
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        if(!isPersistent(tags)) return;
        if(beginList(label)){
            auto attr = nodeStack.top().attribute(listSizeTagString);
            if(!attr) {
                endList();
                m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving List Size \"" + std::string(label) + "\"", m_currentEntity);
                return;
            }
            int listSize = attr.as_int();
            va.resize(listSize);
            for(size_t i = 0; i < listSize; i++){
                char label[64];
                std::snprintf(label, sizeof(label), listElementFormatString, i);
                va.visit_element(i, label, *this);
            }
            endList();
        }
    }

    virtual bool beginList(const char* label) override {
        if(auto listXml = nodeStack.top().child(label)){
            nodeStack.push(listXml);
            return true;
        }
        m_report.addError(Severity::Warning, m_pathStack, m_currentComponentType, "Error resolving List \"" + std::string(label) + "\"", m_currentEntity);
        return false;
    }
    virtual void endList() override { nodeStack.pop(); }
    
private:
    Project& m_project;
    std::stack<pugi::xml_node> nodeStack;

    DeserializationValidationReport& m_report;
    std::vector<std::string> m_pathStack;
    Entity m_currentEntity;
    std::string m_currentComponentType;
};

//Not intended for user, use Application::loadProject()
inline bool loadProject(Project& project, std::string_view filepath, DeserializationValidationReport& report) {
    pugi::xml_document doc;
    if (!doc.load_file(filepath.data())) return false;
    ProjectLoadVisitor loader(project, doc, report);
    project.reflect(loader);
    return true;
}

}