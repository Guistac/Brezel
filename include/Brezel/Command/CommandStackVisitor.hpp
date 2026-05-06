#pragma once
#include "Brezel/Command/CommandStack.hpp"
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Reflection/Parameter.hpp"

namespace Brezel {

class CommandStackVisitor : public ComponentVisitor {
public:
    CommandStackVisitor(CommandStack* s) : m_commandStack(s) {}

    void visit_property(StringID label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        if(hasCommandStack(tags)){
            p.setCommandStack(m_commandStack);
        }
    }

private:
    CommandStack* m_commandStack;
};

} // namespace Brezel