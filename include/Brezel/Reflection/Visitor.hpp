#pragma once

#include <vector>
#include <functional>
#include "Brezel/Foundation/StringID.hpp"

namespace Brezel {

class ParameterBase;
struct UUID;
struct EntityReference;
class Entity;

enum class Tag {
    Persistent,
    Load_Critical,
    Load_Warn,
    Hidden,
    ReadOnly,
    Networked,
    CommandStack,
};

class VectorAccessorBase {
public:
    virtual ~VectorAccessorBase() = default;
    virtual size_t size() const = 0;
    virtual void resize(size_t size) = 0;
    virtual void visit_element(size_t index, StringID label, class ComponentVisitor& visitor, std::initializer_list<Tag> tags = {}) = 0;
};

class ComponentVisitor {
public:
    virtual ~ComponentVisitor() = default;

    virtual void visit_property(StringID, int&,                  std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, float&,                std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, bool&,                 std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, std::string&,          std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, StringID&,             std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, ParameterBase&,        std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, EntityReference&,      std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, VectorAccessorBase&,   std::initializer_list<Tag> tags = {}) {}

    virtual bool beginComponent(StringID componentName) { return true; }
    virtual void endComponent() {}

    virtual bool beginList(StringID name, size_t size) { return true; }
    virtual void endList() {}

    virtual void visit_action(StringID, std::function<void()>) {}

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

    virtual size_t size() const override { return m_vec.size(); }
    virtual void resize(size_t size) override {
        m_vec.clear();
        m_vec.resize(size);
    }

    virtual void visit_element(size_t index, StringID label, ComponentVisitor& visitor, std::initializer_list<Tag> tags = {}) override {        
        if constexpr (std::is_fundamental_v<T> || std::is_same_v<T, std::string> || std::is_same_v<T, StringID>) {
            visitor.visit_property(label, m_vec[index], tags);
        } else {
            m_vec[index].reflect(visitor);
        }
    }
};

} // namespace Brezel
