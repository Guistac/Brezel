#pragma once

#include "CommandStack.hpp"
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"

class CommandStackVisitor : public ComponentVisitor {
public:
    CommandStackVisitor(CommandStack* s) : m_commandStack(s) {}

    // Override for ParameterBase (and others as needed)
    void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if(hasCommandStack(tags)){
            p.setCommandStack(m_commandStack);
        }
    }

private:
    CommandStack* m_commandStack;
};