#pragma once

#include <iostream>
#include <iomanip>
#include <string>

#include "framework/core/Visitor.hpp"


class ConsoleDebugVisitor : public ComponentVisitor {
public:
    void printIndent() {
        std::cout << std::string(level * spacesPerLevel, ' ');
    }

    // Helpers for external functions to manage hierarchy
    void pushLevel() { level++; }
    void popLevel()  { level--; }

    // --- Components ---
    virtual bool beginComponent(const char* componentName) override {
        printIndent();
        std::cout << "Component: " << componentName << "\n";
        pushLevel();
        return true;
    }
    virtual void endComponent() override {
        popLevel();
    }

    // --- Properties ---
    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << p.toString() << "\n";
    }

    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << str << "\n";
    }

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << val << "\n";
    }

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << val << "\n";
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags) override {
        printIndent();
        std::string name = "[Unresolved]";
        if(ref.entity.isValid() && ref.entity.has<IdentityComponent>()) {
            name = ref.entity.get<IdentityComponent>().name;
        }
        std::cout << label << ": \"" << name << "\" UUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << ref.uuid.value << std::dec << "\n";
    }

    // --- Lists / Vectors ---
    virtual bool beginList(const char* name, size_t size) override {
        printIndent();
        std::cout << name << "[" << std::to_string(0) << "]:\n";
        pushLevel();
        return true;
    }

    virtual void endList() override {
        popLevel();
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override {
        beginList(label, va.size());
        for(size_t i = 0; i < va.size(); i++) {
            char idxLabel[64];
            std::snprintf(idxLabel, sizeof(idxLabel), "[%zu]", i);
            va.visit_element(i, idxLabel, *this);
        }
        endList();
    }

private:
    int level = 0;
    const int spacesPerLevel = 4;
};


void printEntity(Entity& entity, ConsoleDebugVisitor& visitor) {
    // 1. Print Entity Header
    visitor.printIndent();
    
    std::string name = "Unnamed Entity";
    if (entity.has<IdentityComponent>()) {
        name = entity.get<IdentityComponent>().name;
    }
    
    std::cout << "[Entity] \"" << name << "\"\n";

    // 2. Reflect Components (Indented)
    visitor.pushLevel();
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);
    
    visitor.printIndent();
    std::cout << "Children:\n";        
    visitor.pushLevel();
    entity.forEachChildEntity(printEntity, visitor);
    visitor.popLevel();

    visitor.popLevel();
}

void printProject(Project& project) {
    ConsoleDebugVisitor visitor;
    std::cout << "--- PROJECT EXPLORER ---\n";
    project.forEachTopLevelEntity(printEntity, visitor);    
    std::cout << "------------------------\n";
}