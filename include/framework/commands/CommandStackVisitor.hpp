#pragma once

#include "CommandStack.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"

struct CommandStackVisitor : public ComponentVisitor {
    CommandStack* commandStack;
    CommandStackVisitor(CommandStack* s) : commandStack(s) {}

    bool isCommandStack(std::initializer_list<Tag> tags) {
        for (auto t : tags) if (t == Tag::CommandStack) return true;
        return false;
    }

    // Override for ParameterBase (and others as needed)
    void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if(isCommandStack(tags)) p.setCommandStack(commandStack);
    }
};