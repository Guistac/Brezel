#pragma once

#include <stack>

#include <pugixml.hpp>

#include "XmlCommon.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/core/Project.hpp"
#include "framework/core/EntityReference.hpp"

namespace Xml{

class EntityComponentNameVisitor : public ComponentVisitor{
public:
    std::vector<std::string> componentNames;
    virtual bool beginComponent(const char* componentName) override {
        componentNames.push_back(componentName);
        return false;
    }
};

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
        IdentityComponent& identity = entity.get<IdentityComponent>();

        pugi::xml_node comment = nodeStack.top().append_child(pugi::node_comment);
        std::string str = " Entity with ";
        EntityComponentNameVisitor visitor;
        ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);
        for(auto& name : visitor.componentNames) str += "[" + name + "] ";
        comment.set_value(str.c_str());

        push(entityTagString);
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
        if (!isPersistent(tags)) return;
        push(label);
        nodeStack.top().append_attribute("val") = p.toString().c_str();
        pop();
    }
    
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        nodeStack.top().append_attribute("val") = str.c_str();
        pop();
    };

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        nodeStack.top().append_attribute("val") = val;
        pop();
    };

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        nodeStack.top().append_attribute("val") = val;
        pop();
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        if (!isPersistent(tags)) return;
        push(label);
        nodeStack.top().append_attribute("UUID") = ref.uuid.value;
        pop();
        pugi::xml_node comment = nodeStack.top().append_child(pugi::node_comment);
        std::string commentstr = "Entity Name: ";
        if(auto identity = ref.entity.try_get<IdentityComponent>()){
            commentstr += "\"" + identity->name + "\"";
        }
        else commentstr += "[Unresolved]";
        comment.set_value(commentstr.c_str());
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        beginList(label);
        nodeStack.top().append_attribute(listSizeTagString) = va.size();
        for(size_t i = 0; i < va.size(); i++){
            char label[64];
            std::snprintf(label, sizeof(label), listElementFormatString, i);
            va.visit_element(i, label, *this, tags);
        }
        endList();
    }

private:
    Project& m_project;
    std::stack<pugi::xml_node> nodeStack;
};


//Not intended for user, use Application::saveProject()
inline bool saveProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    ProjectSaveVisitor visitor(project, doc);
    project.reflect(visitor);
    return doc.save_file(filepath.data());
}

}