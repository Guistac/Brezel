#pragma once

#include <iostream>
#include <iomanip>
#include <string>

#include "framework/core/Visitor.hpp"


class ConsoleDebugProjectVisitor : public ProjectVisitor {
public:

    void printIndent() {
        std::cout << std::string(level * spacesPerLevel, ' ');
    }

    // --- Project ---
    virtual bool beginProject() override {
        std::cout << "--- PROJECT EXPLORER ---\n";
        // We don't increment level here if you want Project properties at the margin
        return true;
    }
    virtual void endProject() override {
        std::cout << "------------------------\n";
    }

    // --- Entities ---
    virtual bool beginEntity(Entity& entity) override {
        printIndent();
        IdentityComponent identity;
        if(entity.has<IdentityComponent>()) identity = entity.get<IdentityComponent>();
        std::cout << "[Entity] \"" << identity.name << "\" "
        << "UUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << identity.uuid.value << std::dec << "\n";
        level++;
        return true;
    }
    virtual void endEntity(Entity& entity) override {
        level--;
    }

    // --- Components ---
    virtual bool beginComponent(const char* componentName) override {
        printIndent();
        std::cout << "Component: " << componentName << "\n";
        level++;
        return true;
    }
    virtual void endComponent() override {
        level--;
    }

    // --- Children ---
    virtual bool beginEntityChildren() override {
        printIndent();
        std::cout << "Children:\n";
        level++;
        return true;
    }
    virtual void endEntityChildren() override {
        level--;
    }

    // --- Properties ---
    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << p.toString();
        std::cout << "\n";
    }

    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << str << "\n";
    }

    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << std::to_string(val) << "\n";
    }

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": " << std::to_string(val) << "\n";
    }

    virtual void visit_property(const char* label, UUID& uuid, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << label << ": 0x" << std::hex << std::setw(16) << std::setfill('0') << uuid.value << std::dec << "\n";
    }

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags) override {
        printIndent();
        std::string name = "[Unresolved]";
        if(ref.entity.has<IdentityComponent>()) name = ref.entity.get<IdentityComponent>().name;
        std::cout << label << ": \"" << name << "\" UUID: 0x" << std::hex << std::setw(16) << std::setfill('0') << ref.uuid.value << std::dec << "\n";
    }

    // --- Lists / Vectors ---
    virtual bool beginList(const char* name) override {
        printIndent();
        std::cout << name << "[]:\n";
        level++;
        return true;
    }

    virtual void endList() override {
        level--;
    }

    virtual void visit_property(const char* label, VectorAccessorBase& va, std::initializer_list<Tag> tags) override {
        // We trigger the grouping logic
        beginList(label);
        
        // Loop through elements. 
        // We use the format "[%i]" because this is a text UI and brackets look good here.
        for(size_t i = 0; i < va.size(); i++) {
            char label[64];
            std::snprintf(label, sizeof(label), "[%zu]", i);
            va.visit_element(i, label, *this);
        }
        
        endList();
    }

private:
    int level = 0;
    const int spacesPerLevel = 4;
};