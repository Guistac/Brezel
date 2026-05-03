#pragma once

#include <stack>

#include <pugixml.hpp>

#include "XmlCommon.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/core/Project.hpp"
#include "framework/core/EntityReference.hpp"

namespace Xml{


class ProjectSaveVisitor : public ProjectVisitor {
public:
    ProjectSaveVisitor(Project& project, pugi::xml_node& root): m_project(project) { nodeStack.push(root); }

    void push(const char* name){
        auto newNode = nodeStack.top().append_child(name);
        nodeStack.push(newNode);
    }
    void pop(){
        nodeStack.pop();
    }

    virtual bool beginProject() override {
        push(projectTagString);
        m_project.ids().saveState(nodeStack.top());
        return true;
    }
    virtual void endProject() override { pop(); }

    virtual bool beginEntity(Entity& entity) override {
        push(entityTagString);
        IdentityComponent& identity = entity.get<IdentityComponent>();
        nodeStack.top().append_attribute("Name") = identity.name;
        nodeStack.top().append_attribute("UUID") = identity.uuid.value;
        return true;
    }
    virtual void endEntity(Entity& entity) override { pop(); }

    virtual bool beginEntityChildren() override { push(childrenTagString); return true; }
    virtual void endEntityChildren() override { pop(); }

    virtual bool beginComponent(const char* componentName) override { push(componentName); return true; }
    virtual void endComponent() override { pop(); }

    virtual bool beginList(const char* name) override { push(name); return true;  }
    virtual void endList() override { pop(); }

    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if (isPersistent(tags)) {
            nodeStack.top().append_attribute(label) = p.toString().c_str();
        }
    }
    
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override{
        nodeStack.top().append_attribute(label) = str.c_str();
    };

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override{
        nodeStack.top().append_attribute(label) = val;
    };

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags) override {
        nodeStack.top().append_attribute(label) = val;
    }

    virtual void visit_property(const char* label, UUID& uuid, std::initializer_list<Tag> tags = {}) override {
        nodeStack.top().append_attribute(label) = uuid.value;
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        nodeStack.top().append_attribute(label) = ref.uuid.value;
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        beginList(label);
        nodeStack.top().append_attribute(listSizeTagString) = va.size();
        for(size_t i = 0; i < va.size(); i++){
            char label[64];
            std::snprintf(label, sizeof(label), listElementFormatString, i);
            va.visit_element(i, label, *this);
        }
        endList();
    }

private:
    Project& m_project;
    std::stack<pugi::xml_node> nodeStack;
};


inline bool saveProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    ProjectSaveVisitor visitor(project, doc);
    project.reflect(visitor);
    return doc.save_file(filepath.data());
}

}