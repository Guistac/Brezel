#pragma once
#include <stack>
#include <tinyxml2.h>
#include "Brezel/Serialization/XmlCommon.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Core/EntityReference.hpp"

namespace Brezel {

namespace Xml {

class ComponentSaveVisitor : public ComponentVisitor {
public:
    ComponentSaveVisitor(tinyxml2::XMLElement* root, tinyxml2::XMLDocument* doc) : m_doc(doc) { 
        nodeStack.push(root); 
    }

    void push(StringID name){
        tinyxml2::XMLElement* newNode = m_doc->NewElement(name.toString().c_str());
        nodeStack.top()->InsertEndChild(newNode);
        nodeStack.push(newNode);
    }
    void pop(){ nodeStack.pop(); }
    tinyxml2::XMLElement* top(){ return nodeStack.top(); }

    virtual bool beginComponent(StringID componentTypeName) override {
        push(componentTypeName);
        return true;
    }
    virtual void endComponent() override { pop(); };

    virtual bool beginList(StringID name, size_t listSize) override { push(name); return true;  }
    virtual void endList() override { pop(); }

    
    virtual void visit_property(StringID label, std::string& str, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        top()->SetAttribute("val", str.c_str());
        pop();
    };

    virtual void visit_property(StringID label, StringID& sid, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top()->SetAttribute("val", sid.toString().c_str());
        pop();
    }

    virtual void visit_property(StringID label, float& val, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        push(label);
        top()->SetAttribute("val", val);
        pop();
    };

    virtual void visit_property(StringID label, int& val, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top()->SetAttribute("val", val);
        pop();
    }

    virtual void visit_property(StringID label, bool& val, std::initializer_list<Tag> tags) override {
        if (!isPersistent(tags)) return;
        push(label);
        top()->SetAttribute("val", val);
        pop();
    }

    virtual void visit_property(StringID label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        if (!isPersistent(tags)) return;
        push(label);
        top()->SetAttribute("UUID", (uint64_t)ref.uuid.value);
        pop();
        
        std::string commentstr = "Entity Name: ";
        if(auto identity = ref.entity.try_get<IdentityComponent>()){
            commentstr += "\"" + identity->name + "\"";
        }
        else commentstr += "[Unresolved]";
        
        tinyxml2::XMLComment* comment = m_doc->NewComment(commentstr.c_str());
        top()->InsertEndChild(comment);
    }

    virtual void visit_property(StringID label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override{
        if (!isPersistent(tags)) return;
        beginList(label, va.size());
        top()->SetAttribute(listSizeTagString, (uint64_t)va.size());
        for(size_t i = 0; i < va.size(); i++){
            char buf[64];
            std::snprintf(buf, sizeof(buf), listElementFormatString, i);
            va.visit_element(i, StringID::from(buf), *this, tags);
        }
        endList();
    }

private:
    std::stack<tinyxml2::XMLElement*> nodeStack;
    tinyxml2::XMLDocument* m_doc;
};


inline bool saveEntity(Entity& entity, tinyxml2::XMLElement* parentXmlNode, tinyxml2::XMLDocument* doc){
    tinyxml2::XMLElement* entityXml = doc->NewElement(entityTagString);
    parentXmlNode->InsertEndChild(entityXml);

    if(auto identity = entity.try_get<IdentityComponent>()){
        entityXml->SetAttribute("Name", identity->name.c_str());
        entityXml->SetAttribute("DisplayName", identity->displayName.c_str());
        entityXml->SetAttribute("UUID", (uint64_t)identity->uuid.value);
    }

    ComponentSaveVisitor visitor(entityXml, doc);
    ComponentRegistry::reflectEntityComponents(entity, visitor);

    tinyxml2::XMLElement* childrenXml = doc->NewElement(childrenTagString);
    entityXml->InsertEndChild(childrenXml);
    
    entity.forEachChildEntity([childrenXml, doc](Entity& child) { saveEntity(child, childrenXml, doc); });
    
    return true;
}

inline bool saveProject(Project& project, std::string_view filepath) {
    tinyxml2::XMLDocument doc;
    
    tinyxml2::XMLElement* projectXml = doc.NewElement(projectTagString);
    doc.InsertEndChild(projectXml);
    projectXml->SetAttribute("Name", project.getName().data());

    project.ids().saveState(&doc, projectXml); // We need to update UUIDProvider::saveState
    project.forEachTopLevelEntity([projectXml, &doc](Entity& child) { saveEntity(child, projectXml, &doc); });

    return doc.SaveFile(filepath.data()) == tinyxml2::XML_SUCCESS;
}

} // namespace Xml

} // namespace Brezel