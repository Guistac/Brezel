#pragma once

class ParameterBase;

enum class Tag {
    Persistent,  // Include in XML save/load
    Hidden,      // Exclude from UI display
    ReadOnly,    // Observable but not writable
    Networked,   // Sync over network
    CommandStack,
};

struct ComponentVisitor {
public:

    virtual void visit_property(const char*, int&,          std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, float&,        std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, bool&,         std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, std::string&,  std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, ParameterBase&,std::initializer_list<Tag> tags = {}) {}
    virtual void beginComponent(const char* componentName) {}
    virtual void endComponent() {}

    // Named callables with no arguments — rendered as buttons in UI
    virtual void visit_action(const char*, std::function<void()>) {}
};

struct EntityVisitor : public ComponentVisitor{
public:
    virtual void beginEntity(){}
    virtual void endEntity(){}
    virtual void beginEntityChildren(){}
    virtual void endEntityChildren(){}
};

struct ProjectVisitor : public EntityVisitor{
public:
    virtual void beginProject(){}
    virtual void endProject(){}
};