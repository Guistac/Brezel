#pragma once

#include <iostream>
#include <string>

#include "framework/core/Visitor.hpp"

struct ConsoleProjectVisitor : public ProjectVisitor {
    int depth = 0;

    // Use strings that VS Code handles gracefully
    const char* kVertical   = "|   ";
    const char* kBranch     = "|-- ";
    const char* kLastBranch = "`-- "; // Standard ASCII "last leaf"
    const char* kEntityNode = "[E] ";
    const char* kCompNode   = "[C] ";

    void printIndent() {
        for (int i = 0; i < depth; ++i) {
            std::cout << kVertical;
        }
    }

    // Properties
    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << kBranch << label << ": " << p.toString() << "\n";
    }

    virtual void visit_property(const char* label, std::string& str, std::initializer_list<Tag> tags) override {
        printIndent();
        std::cout << kBranch << label << ": " << str << "\n";
    };

    // Components
    virtual void beginComponent(const char* name) override {
        printIndent();
        std::cout << kBranch << kCompNode << name << "\n";
        depth++;
    }
    virtual void endComponent() override { depth--; }

    // Entities
    virtual void beginEntity() override {
        printIndent();
        std::cout << kEntityNode << "Entity\n";
        depth++;
    }
    virtual void endEntity() override { depth--; }

    // Structural Markers
    virtual void beginEntityChildren() override {
        printIndent();
        std::cout << kLastBranch << "Children\n";
    }
    virtual void endEntityChildren() override {}

    virtual void beginProject() override {
        std::cout << "\n--- PROJECT EXPLORER ---\n";
    }
    virtual void endProject() override {
        std::cout << "------------------------\n\n";
    }
};