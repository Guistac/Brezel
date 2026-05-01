#pragma once

#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"

#include <stack>

class Project;


struct XmlProjectVisitor : public ProjectVisitor {
    XmlProjectVisitor(const Project& project, pugi::xml_node& root) { nodeStack.push(root); }

    bool isPersistent(std::initializer_list<Tag> tags) {
        for (auto t : tags) if (t == Tag::Persistent) return true;
        return false;
    }

    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if (isPersistent(tags)) {
            nodeStack.top().append_attribute(label) = p.toString().c_str();
        }
    }
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override{
        nodeStack.top().append_attribute(label) = str.c_str();
    };

    virtual void beginComponent(const char* componentName) override {
        auto newNode = nodeStack.top().append_child(componentName);
        nodeStack.push(newNode);
    }
    virtual void endComponent() override { nodeStack.pop(); }

    virtual void beginEntity() override {
        auto newNode = nodeStack.top().append_child("Object");
        nodeStack.push(newNode);
    }
    virtual void endEntity() override { nodeStack.pop(); }

    virtual void beginEntityChildren() override {
        auto newNode = nodeStack.top().append_child("Children");
        nodeStack.push(newNode);
    }
    virtual void endEntityChildren() override { nodeStack.pop(); }

    virtual void beginProject() override {
        auto newNode = nodeStack.top().append_child("Project");
        nodeStack.push(newNode);
    }
    virtual void endProject() override { nodeStack.pop(); }

private:
    std::stack<pugi::xml_node> nodeStack;
};



struct XmlLoadVisitor : public ComponentVisitor {
    pugi::xml_node& node;
    CommandStack* stack;
    XmlLoadVisitor(pugi::xml_node& n, CommandStack* s) : node(n), stack(s) {}

    void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        p.setCommandStack(stack); // Inject undo/redo support
        if (auto attr = node.attribute(label)) {
            p.fromString(attr.value());
        }
    }
};