#pragma once

#include <stack>

#include <pugixml.hpp>
#include <entt/entt.hpp>

#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"
#include "framework/core/Project.hpp"

namespace XmlSerializer{

inline constexpr const char* projectTagString = "Project";
inline constexpr const char* entityTagString = "Entity";
inline constexpr const char* childrenTagString = "Children";
inline constexpr const char* listSizeTagString = "ListSize";
inline constexpr const char* listElementFormatString = "E%zu";

struct ProjectSaveVisitor : public ProjectVisitor {
    ProjectSaveVisitor(pugi::xml_node& root) { nodeStack.push(root); }

    void push(const char* name){
        auto newNode = nodeStack.top().append_child(name);
        nodeStack.push(newNode);
    }
    void pop(){
        nodeStack.pop();
    }

    virtual bool beginProject() override { push(projectTagString); return true; }
    virtual void endProject() override { pop(); }

    virtual bool beginEntity() override { push(entityTagString); return true; }
    virtual void endEntity() override { pop(); }

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

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags = {}) override {
        nodeStack.top().append_attribute(label) = val;
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
    std::stack<pugi::xml_node> nodeStack;
};


struct ProjectLoadVisitor : public ProjectVisitor{
public:
    ProjectLoadVisitor(Project& project, pugi::xml_node& root) : m_project(project) { nodeStack.push(root); }

    virtual bool beginProject() override {
        if(auto projectXml = nodeStack.top().child(projectTagString)){
            nodeStack.push(projectXml);
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
            Entity entity = m_project.createEntity("");
            if(parent) entity.setParent(*parent);
            loadEntity(entity);
            nodeStack.pop();
        }
    }

    void loadEntity(Entity& entity){
        entity.reflect(*this);
        for (auto childXml : nodeStack.top().children()) {
            std::string xmlName = childXml.name();
            nodeStack.push(childXml);
            if(xmlName == childrenTagString){
                loadChildEntities(&entity);
            }
            else{
                ComponentRegistry::addReflectEntityComponent(entity.handle(), xmlName.c_str(), *this);
            }
            nodeStack.pop();
        }
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

inline bool saveProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    ProjectSaveVisitor visitor(doc);
    project.reflect(visitor);
    return doc.save_file(filepath.data());
}

inline bool loadProject(Project& project, std::string_view filepath) {
    pugi::xml_document doc;
    if (!doc.load_file(filepath.data())) return false;
    ProjectLoadVisitor visitor(project, doc);
    project.reflect(visitor);
    return true;
}

}