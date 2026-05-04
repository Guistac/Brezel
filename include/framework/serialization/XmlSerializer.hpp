#pragma once

#include <stack>

#include <pugixml.hpp>

#include "XmlCommon.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/core/Project.hpp"
#include "framework/core/EntityReference.hpp"

namespace Xml{

/*
class EntityComponentNameVisitor : public ComponentVisitor{
public:
    std::vector<std::string> componentNames;
    virtual bool beginComponent(const char* componentName) override {
        componentNames.push_back(componentName);
        return false;
    }
};
*/

class ComponentSaveVisitor : public ComponentVisitor {
public:

    ComponentSaveVisitor(pugi::xml_node& root) { nodeStack.push(root); }

    void push(const char* name){
        auto newNode = nodeStack.top().append_child(name);
        nodeStack.push(newNode);
    }
    void pop(){ nodeStack.pop(); }
    pugi::xml_node top(){ return nodeStack.top(); }

    virtual bool beginComponent(const char* componentTypeName) override {
        push(componentTypeName);
        return true;
    }
    virtual void endComponent() override { pop(); };

    virtual bool beginList(const char* name, size_t listSize) override { push(name); return true;  }
    virtual void endList() override { pop(); }

    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = p.toString().c_str();
        pop();
    }
    
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = str.c_str();
        pop();
    };

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = val;
        pop();
    };

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = val;
        pop();
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("UUID") = ref.uuid.value;
        pop();
        pugi::xml_node comment = top().append_child(pugi::node_comment);
        std::string commentstr = "Entity Name: ";
        if(auto identity = ref.entity.try_get<IdentityComponent>()){
            commentstr += "\"" + identity->name + "\"";
        }
        else commentstr += "[Unresolved]";
        comment.set_value(commentstr.c_str());
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        beginList(label, va.size());
        top().append_attribute(listSizeTagString) = va.size();
        for(size_t i = 0; i < va.size(); i++){
            char label[64];
            std::snprintf(label, sizeof(label), listElementFormatString, i);
            va.visit_element(i, label, *this, tags);
        }
        endList();
    }

private:
    std::stack<pugi::xml_node> nodeStack;
};


bool saveEntity(Entity& entity, pugi::xml_node parentXmlNode){
    
    auto entityXml = parentXmlNode.append_child("Entity");

    if(auto identity = entity.try_get<IdentityComponent>()){
        entityXml.append_attribute("Name") = identity->name.c_str();
        entityXml.append_attribute("DisplayName") = identity->displayName.c_str();
        entityXml.append_attribute("UUID") = identity->uuid.value;
    }

    /*
    pugi::xml_node comment = nodeStack.top().append_child(pugi::node_comment);
    std::string str = " Entity with ";
    EntityComponentNameVisitor visitor;
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);
    for(auto& name : visitor.componentNames) str += "[" + name + "] ";
    comment.set_value(str.c_str());
    */

    ComponentSaveVisitor visitor(entityXml);
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);

    auto childrenXml = entityXml.append_child(childrenTagString);
    entity.forEachChildEntity(saveEntity, childrenXml);
    
    return true;
}

//Not intended for user, use Application::saveProject()
inline bool saveProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;

    auto projectXml = doc.append_child(projectTagString);
    projectXml.append_attribute("Name") = project.getName();
    project.ids().saveState(projectXml);

    project.forEachTopLevelEntity(saveEntity, projectXml);

    return doc.save_file(filepath.data());
}

}