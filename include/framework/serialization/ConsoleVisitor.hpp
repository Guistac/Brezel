#pragma once

#include <iostream>
#include <string>

#include "framework/core/Visitor.hpp"

struct ConsoleProjectVisitor : public ProjectVisitor {
    std::vector<bool> lineStack;

    void printIndent() {
        for (bool hasLine : lineStack) {
            std::cout << (hasLine ? "|   " : "    ");
        }
    }

    // --- Project ---
    virtual void beginProject() override {
        std::cout << "\n--- PROJECT EXPLORER ---\n";
        // We leave lineStack EMPTY here so the root has no prefix
    }
    
    virtual void endProject() override {
        std::cout << "------------------------\n";
    }

    // --- Entities ---
    virtual void beginEntity() override {
        printIndent();
        std::cout << "|-- [E] Entity\n";
        // Now we push to start the vertical line for the entity's contents
        lineStack.push_back(true); 
    }
    
    virtual void endEntity() override {
        lineStack.pop_back();
    }

    // --- Components ---
    virtual void beginComponent(const char* name) override {
        printIndent();
        std::cout << "|-- [C] " << name << "\n";
        lineStack.push_back(true); 
    }
    
    virtual void endComponent() override {
        lineStack.pop_back();
    }

    // --- Children ---
    virtual void beginEntityChildren() override {
        printIndent();
        std::cout << "`-- Children\n";
        // Push FALSE to indent the children without a vertical pipe
        lineStack.push_back(false); 
    }
    
    virtual void endEntityChildren() override {
        lineStack.pop_back();
    }

    // --- Properties ---
    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << "|-- " << label << ": " << p.toString() << "\n";
    }
    
    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << "|-- " << label << ": " << str << "\n";
    }
};


struct IndentationProjectVisitor : public ProjectVisitor {
    int level = 0;
    const int spacesPerLevel = 4;

    void printIndent() {
        std::cout << std::string(level * spacesPerLevel, ' ');
    }

    // --- Project ---
    virtual void beginProject() override {
        std::cout << "--- PROJECT EXPLORER ---\n";
        // We don't increment level here if you want Project properties at the margin
    }
    virtual void endProject() override {
        std::cout << "------------------------\n";
    }

    // --- Entities ---
    virtual void beginEntity() override {
        printIndent();
        std::cout << "[Entity]\n";
        level++;
    }
    virtual void endEntity() override {
        level--;
    }

    // --- Components ---
    virtual void beginComponent(const char* componentName) override {
        printIndent();
        std::cout << "Component: " << componentName << "\n";
        level++;
    }
    virtual void endComponent() override {
        level--;
    }

    // --- Children ---
    virtual void beginEntityChildren() override {
        printIndent();
        std::cout << "Children:\n";
        level++;
    }
    virtual void endEntityChildren() override {
        level--;
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
};