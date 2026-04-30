#pragma once

#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"

struct XmlSaveVisitor : public ComponentVisitor {
    pugi::xml_node& node;
    XmlSaveVisitor(pugi::xml_node& n) : node(n) {}

    // Helper to check if we should save this property
    bool isPersistent(std::initializer_list<Tag> tags) {
        for (auto t : tags) if (t == Tag::Persistent) return true;
        return false;
    }

    // Override for ParameterBase (and others as needed)
    void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if (isPersistent(tags)) {
            node.append_attribute(label) = p.toString().c_str();
        }
    }
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