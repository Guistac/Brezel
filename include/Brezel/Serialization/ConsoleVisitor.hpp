#pragma once
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Core/Project.hpp"

namespace Brezel {

class ConsoleDebugVisitor : public ComponentVisitor {
public:
    void printIndent() {
        std::cout << std::string(level * spacesPerLevel, ' ');
    }

    void pushLevel() { level++; }
    void popLevel()  { level--; }

    virtual bool beginComponent(StringID componentName) override {
        printIndent();
        std::cout << "Component: " << componentName.toString() << "\n";
        pushLevel();
        return true;
    }
    virtual void endComponent() override {
        popLevel();
    }

    virtual void visit_property(StringID label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label.toString() << ": " << p.toString() << "\n";
    }

    virtual void visit_property(StringID label, std::string& str, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label.toString() << ": " << str << "\n";
    }

    virtual void visit_property(StringID label, StringID& sid, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label.toString() << ": " << sid.toString() << "\n";
    }

    virtual void visit_property(StringID label, float& val, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label.toString() << ": " << val << "\n";
    }

    virtual void visit_property(StringID label, int& val, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label.toString() << ": " << val << "\n";
    }

    virtual void visit_property(StringID label, EntityReference& ref, std::initializer_list<Tag> tags) override {
        printIndent();
        std::string name = "[Unresolved]";
        if(ref.entity.isValid() && ref.entity.has<IdentityComponent>()) {
            name = ref.entity.get<IdentityComponent>().name;
        }
        std::cout << label.toString() << ": \"" << name << "\" UUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << ref.uuid.value << std::dec << "\n";
    }

    virtual bool beginList(StringID name, size_t size) override {
        printIndent();
        std::cout << name.toString() << "[" << std::to_string(size) << "]:\n";
        pushLevel();
        return true;
    }

    virtual void endList() override {
        popLevel();
    }

    virtual void visit_property(StringID label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override {
        beginList(label, va.size());
        for(size_t i = 0; i < va.size(); i++) {
            char idxLabel[64];
            std::snprintf(idxLabel, sizeof(idxLabel), "[%zu]", i);
            va.visit_element(i, StringID::from(idxLabel), *this);
        }
        endList();
    }

private:
    int level = 0;
    const int spacesPerLevel = 4;
};

inline void printEntity(Entity& entity, ConsoleDebugVisitor& visitor) {
    visitor.printIndent();
    
    std::string name = "Unnamed Entity";
    if (entity.has<IdentityComponent>()) {
        name = entity.get<IdentityComponent>().name;
    }
    
    std::cout << "[Entity] \"" << name << "\"\n";

    visitor.pushLevel();
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);
    
    visitor.printIndent();
    std::cout << "Children:\n";        
    visitor.pushLevel();
    entity.forEachChildEntity(printEntity, visitor);
    visitor.popLevel();

    visitor.popLevel();
}

inline void printProject(Project& project) {
    ConsoleDebugVisitor visitor;
    std::cout << "--- PROJECT EXPLORER ---\n";
    project.forEachTopLevelEntity(printEntity, visitor);    
    std::cout << "------------------------\n";
}

} // namespace Brezel