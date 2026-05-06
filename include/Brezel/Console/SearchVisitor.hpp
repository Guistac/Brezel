#pragma once
#include <string>
#include <string_view>
#include <functional>
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Reflection/Parameter.hpp"

namespace Brezel {

class SearchVisitor : public ComponentVisitor {
public:
    enum class FoundType {
        None,
        Int, Float, Bool, String, Action, ParameterBase
    };

    struct FoundResult {
        FoundType type = FoundType::None;
        void* ptr = nullptr;
        std::function<void()> action;
    };

    SearchVisitor(std::string_view searchName) : m_searchName(StringID::from(searchName)) {}

    bool isFound() const { return m_result.type != FoundType::None; }
    const FoundResult& getResult() const { return m_result; }

    virtual void visit_property(StringID label, int& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::Int, &val};
    }
    virtual void visit_property(StringID label, float& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::Float, &val};
    }
    virtual void visit_property(StringID label, bool& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::Bool, &val};
    }
    virtual void visit_property(StringID label, std::string& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::String, &val};
    }
    virtual void visit_property(StringID label, StringID& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::None, &val};
    }
    virtual void visit_property(StringID label, ParameterBase& p, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::ParameterBase, &p};
    }
    virtual void visit_action(StringID label, std::function<void()> fn) override {
        if (m_searchName == label) {
            m_result.type = FoundType::Action;
            m_result.action = fn;
        }
    }

private:
    StringID m_searchName;
    FoundResult m_result;
};

} // namespace Brezel
