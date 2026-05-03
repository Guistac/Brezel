#pragma once

#include <vector>

class ParameterBase;
struct UUID;
struct EntityReference;
class Entity;

enum class Tag {
    Persistent,  // Include in XML save/load
    Load_Critical,
    Load_Warn,
    Hidden,      // Exclude from UI display
    ReadOnly,    // Observable but not writable
    Networked,   // Sync over network
    CommandStack,
};

class VectorAccessorBase {
public:
    virtual size_t size() const = 0;
    virtual void resize(size_t size) = 0;
    virtual void visit_element(size_t index, const char* label, class ComponentVisitor& visitor, std::initializer_list<Tag> tags = {}) = 0;
};

class ComponentVisitor {
public:

    virtual void visit_property(const char*, int&,                  std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, float&,                std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, bool&,                 std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, std::string&,          std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, ParameterBase&,        std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, EntityReference&,      std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(const char*, VectorAccessorBase&,   std::initializer_list<Tag> tags = {}) {}


    virtual bool beginComponent(const char* componentName) { return true; }
    virtual void endComponent() {}

    virtual bool beginList(const char* name) { return true; }
    virtual void endList() {}

    // Named callables with no arguments — rendered as buttons in UI
    virtual void visit_action(const char*, std::function<void()>) {}

    bool isPersistent(std::initializer_list<Tag> tags) {
        for (auto t : tags) if (t == Tag::Persistent) return true;
        return false;
    }
    bool hasCommandStack(std::initializer_list<Tag> tags){
        for (auto t : tags) if (t == Tag::CommandStack) return true;
        return false;
    }
};


template<typename T>
class VectorAccessor : public VectorAccessorBase {
public:
    VectorAccessor(std::vector<T>& vec) : m_vec(vec) {}
    std::vector<T>& m_vec;
    std::string m_fmt;

    virtual size_t size() const override { return m_vec.size(); }
    virtual void resize(size_t size) override {
        m_vec.clear();
        m_vec.resize(size);
    }

    virtual void visit_element(size_t index, const char* label, ComponentVisitor& visitor, std::initializer_list<Tag> tags = {}) override {        
        // If T is a primitive, this calls the standard visit_property
        // If T is a struct, you'd call m_vec[index].reflect(visitor)
        if constexpr (std::is_fundamental_v<T> || std::is_same_v<T, std::string>) {
            visitor.visit_property(label, m_vec[index], tags);
        } else {
            m_vec[index].reflect(visitor);
        }
    }

};




class EntityVisitor : public ComponentVisitor{
public:
    virtual bool beginEntity(Entity& entity){ return true; }
    virtual void endEntity(Entity& entity){}
    virtual bool beginEntityChildren(){ return true; }
    virtual void endEntityChildren(){}
};

class ProjectVisitor : public EntityVisitor{
public:
    virtual bool beginProject(){ return true; }
    virtual void endProject(){}
};