#pragma once

#include <iostream>
#include <string>

#include "framework/core/Visitor.hpp"


struct IndentationProjectVisitor : public ProjectVisitor {
    int level = 0;
    const int spacesPerLevel = 4;

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
    virtual bool beginEntity() override {
        printIndent();
        std::cout << "[Entity]\n";
        level++;
        return true;
    }
    virtual void endEntity() override {
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

};