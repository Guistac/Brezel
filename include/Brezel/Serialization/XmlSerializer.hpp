#pragma once
#include <stack>
#include <pugixml.hpp>
#include "Brezel/Serialization/XmlCommon.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Reflection/Parameter.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Core/EntityReference.hpp"

namespace Brezel {

namespace Xml {

class ComponentSaveVisitor : public ComponentVisitor {
public:
    ComponentSaveVisitor(pugi::xml_node& root) { nodeStack.push(root); }

    void push(StringID name){
        auto newNode = nodeStack.top().append_child(name.toString().c_str());
        nodeStack.push(newNode);
    }
    void pop(){ nodeStack.pop(); }
    pugi::xml_node top(){ return nodeStack.top(); }

    virtual bool beginComponent(StringID componentTypeName) override {
        push(componentTypeName);
        return true;
    }
    virtual void endComponent() override { pop(); };

    virtual bool beginList(StringID name, size_t listSize) override { push(name); return true;  }
    virtual void endList() override { pop(); }

    virtual void visit_property(StringID label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = p.toString().c_str();
        pop();
    }
    
    virtual void visit_property(StringID label, std::string& str, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = str.c_str();
        pop();
    };

    virtual void visit_property(StringID label, StringID& sid, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = sid.toString().c_str();
        pop();
    }

    virtual void visit_property(StringID label, float& val, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = val;
        pop();
    };

    virtual void visit_property(StringID label, int& val, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top().append_attribute("val") = val;
        pop();
    }

    virtual void visit_property(StringID label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
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

    virtual void visit_property(StringID label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        beginList(label, va.size());
        top().append_attribute(listSizeTagString) = va.size();
        for(size_t i = 0; i < va.size(); i++){
            char buf[64];
            std::snprintf(buf, sizeof(buf), listElementFormatString, i);
            va.visit_element(i, StringID::from(buf), *this, tags);
        }
        endList();
    }

private:
    std::stack<pugi::xml_node> nodeStack;
};


inline bool saveEntity(Entity& entity, pugi::xml_node parentXmlNode){
    auto entityXml = parentXmlNode.append_child(entityTagString);

    if(auto identity = entity.try_get<IdentityComponent>()){
        entityXml.append_attribute("Name") = identity->name.c_str();
        entityXml.append_attribute("DisplayName") = identity->displayName.c_str();
        entityXml.append_attribute("UUID") = identity->uuid.value;
    }

    ComponentSaveVisitor visitor(entityXml);
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);

    auto childrenXml = entityXml.append_child(childrenTagString);
    entity.forEachChildEntity(saveEntity, childrenXml);
    
    return true;
}

inline bool saveProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    auto projectXml = doc.append_child(projectTagString);
    projectXml.append_attribute("Name") = project.getName().data();

    project.ids().saveState(projectXml);
    project.forEachTopLevelEntity(saveEntity, projectXml);

    return doc.save_file(filepath.data());
}

} // namespace Xml

} // namespace Brezel