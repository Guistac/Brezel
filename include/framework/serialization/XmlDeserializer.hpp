#pragma once

#include <stack>

#include <pugixml.hpp>

#include "XmlCommon.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/core/Project.hpp"
#include "framework/core/EntityReference.hpp"
#include "framework/serialization/EntityReferenceLinker.hpp"

namespace Xml{


class ProjectLoadVisitor : public ProjectVisitor{
public:
    ProjectLoadVisitor(Project& project, pugi::xml_node& root) : m_project(project) { nodeStack.push(root); }

    virtual bool beginProject() override {
        if(auto projectXml = nodeStack.top().child(projectTagString)){
            nodeStack.push(projectXml);
            m_project.ids().loadState(projectXml);
            return true;
        }
        return false;
    }
    virtual void endProject() override {
        loadChildEntities(nullptr);
        nodeStack.pop();
    }

    void loadChildEntities(Entity* parent){
        for(auto childEntitytXml : nodeStack.top().children(entityTagString)){
            nodeStack.push(childEntitytXml);
            Entity loadedEntity = loadEntity();
            if(parent) loadedEntity.setParent(*parent);
            nodeStack.pop();
        }
    }

    Entity loadEntity(){
        IdentityComponent identity;
        if(auto attr = nodeStack.top().attribute("Name")) identity.name = attr.as_string();
        if(auto attr = nodeStack.top().attribute("UUID")) identity.uuid.value = attr.as_ullong();

        Entity loadedEntity = m_project.restoreEntity(identity.name, identity.uuid);

        for (auto childXml : nodeStack.top().children()) {
            std::string xmlTagName = childXml.name();
            nodeStack.push(childXml);
            if(xmlTagName == childrenTagString){
                loadChildEntities(&loadedEntity);
            }
            else{
                ComponentRegistry::addReflectEntityComponent(loadedEntity.handle(), xmlTagName.c_str(), *this);
            }
            nodeStack.pop();
        }
        CommandStackVisitor visitor(&m_project.getStack());
        ComponentRegistry::reflectEntityComponents(loadedEntity.handle(), visitor);

        return loadedEntity;
    }

    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if (auto attr = nodeStack.top().attribute(label)) {
            p.fromString(attr.as_string());
            if(hasCommandStack(tags)) p.setCommandStack(&m_project.getStack());
        }
    }
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override{
        if (auto attr = nodeStack.top().attribute(label)) {
            str = attr.as_string();
        }
    };

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override{
        if(auto attr = nodeStack.top().attribute(label)){
            val = attr.as_float();
        }
    };

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags = {}) override {
        if(auto attr = nodeStack.top().attribute(label)){
            val = attr.as_int();
        }
    }

    virtual void visit_property(const char* label, UUID& uuid, std::initializer_list<Tag> tags = {}) override {
        if(auto attr = nodeStack.top().attribute(label)){
            uuid.value = attr.as_ullong();
        }
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        if(auto attr = nodeStack.top().attribute(label)){
            ref.uuid.value = attr.as_ullong();
        }
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        if(beginList(label)){
            auto attr = nodeStack.top().attribute(listSizeTagString);
            if(!attr) {
                endList();
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

    virtual bool beginList(const char* name) override {
        if(auto listXml = nodeStack.top().child(name)){
            nodeStack.push(listXml);
            return true;
        }
        return false;
    }
    virtual void endList() override { nodeStack.pop(); }
    
private:
    Project& m_project;
    std::stack<pugi::xml_node> nodeStack;
};

inline bool loadProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    if (!doc.load_file(filepath.data())) return false;
    ProjectLoadVisitor loader(project, doc);
    project.reflect(loader);
    EntityReferenceLinkerVisitor linker(project);
    project.reflect(linker);
    return true;
}

}