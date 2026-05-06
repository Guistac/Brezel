#pragma once

#include <vector>
#include <functional>
#include "Brezel/Foundation/StringID.hpp"

namespace Brezel {

class ParameterBase;
struct UUID;
struct EntityReference;
class Entity;

/// @brief Tags used to annotate properties during reflection.
/// @details These tags control how the framework treats specific pieces of data.
enum class Tag {
    Persistent,     ///< Property should be serialized to/from disk (XML).
    Load_Critical,  ///< Fail loading if this property is missing.
    Load_Warn,      ///< Log warning if this property is missing.
    Hidden,         ///< Do not show in the CLI or Debug UI.
    ReadOnly,       ///< CLI cannot modify this property.
    Networked,      ///< Sync this property across the network (Reserved).
    CommandStack,   ///< Hook this property into the Undo/Redo system.
};

/// @brief Interface for accessing and modifying vector data during reflection.
class VectorAccessorBase {
public:
    virtual ~VectorAccessorBase() = default;
    virtual size_t size() const = 0;
    virtual void resize(size_t size) = 0;
    
    /// @brief Visits an individual element within the vector.
    virtual void visit_element(size_t index, StringID label, class ComponentVisitor& visitor, std::initializer_list<Tag> tags = {}) = 0;
};

/// @brief Base interface for the Visitor pattern used in Brezel's reflection.
/// @details Inherit from this to implement new functionality like Serializers, 
/// Debug UIs, or Search logic.
class ComponentVisitor {
public:
    virtual ~ComponentVisitor() = default;

    /// @name Property Visitors
    /// @{
    virtual void visit_property(StringID, int&,                  std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, float&,                std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, bool&,                 std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, std::string&,          std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, StringID&,             std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, ParameterBase&,        std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, EntityReference&,      std::initializer_list<Tag> tags = {}) {}
    virtual void visit_property(StringID, VectorAccessorBase&,   std::initializer_list<Tag> tags = {}) {}
    /// @}

    /// @brief Called when starting to reflect a component.
    /// @return Returning false may skip the rest of the component reflection.
    virtual bool beginComponent(StringID componentName) { return true; }
    virtual void endComponent() {}

    /// @brief Called when encountering a list/vector.
    virtual bool beginList(StringID name, size_t size) { return true; }
    virtual void endList() {}

    /// @brief Exposes a function that can be called from the CLI or Debug UI.
    virtual void visit_action(StringID, std::function<void()>) {}

    /// @brief Helper to check if the 'Persistent' tag is present.
    bool isPersistent(std::initializer_list<Tag> tags) {
        for (auto t : tags) if (t == Tag::Persistent) return true;
        return false;
    }

    /// @brief Helper to check if the 'CommandStack' tag is present.
    bool hasCommandStack(std::initializer_list<Tag> tags){
        for (auto t : tags) if (t == Tag::CommandStack) return true;
        return false;
    }
};

/// @brief Concrete implementation for reflecting std::vector<T>.
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
            // Nested reflection for complex types
            m_vec[index].reflect(visitor);
        }
    }
};

} // namespace Brezel
