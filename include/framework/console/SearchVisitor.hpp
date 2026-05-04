#pragma once
#include <string>
#include <string_view>
#include <functional>
#include "framework/core/Visitor.hpp"
#include "framework/params/Parameter.hpp"

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

    SearchVisitor(std::string_view searchName) : m_searchName(searchName) {}

    bool isFound() const { return m_result.type != FoundType::None; }
    const FoundResult& getResult() const { return m_result; }

    virtual void visit_property(const char* label, int& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::Int, &val};
    }
    virtual void visit_property(const char* label, float& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::Float, &val};
    }
    virtual void visit_property(const char* label, bool& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::Bool, &val};
    }
    virtual void visit_property(const char* label, std::string& val, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::String, &val};
    }
    virtual void visit_property(const char* label, ParameterBase& p, std::initializer_list<Tag> tags = {}) override {
        if (m_searchName == label) m_result = {FoundType::ParameterBase, &p};
    }
    virtual void visit_action(const char* label, std::function<void()> fn) override {
        if (m_searchName == label) {
            m_result.type = FoundType::Action;
            m_result.action = fn;
        }
    }

private:
    std::string m_searchName;
    FoundResult m_result;
};
