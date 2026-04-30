#pragma once
#include <vector>
#include <memory>
#include <optional>
#include "framework/core/Project.hpp"

namespace Application {

    inline std::vector<std::unique_ptr<Project>> m_projects;
    inline std::optional<size_t> m_activeProjectIndex;

    // Project Management
    inline Project* createProject(std::string_view name) {
        m_projects.push_back(std::make_unique<Project>(name));
        m_activeProjectIndex = m_projects.size() - 1;
        return m_projects.back().get();
    }

    inline void closeProject(size_t index) {
        if (index < m_projects.size()) {
            m_projects.erase(m_projects.begin() + index);
            m_activeProjectIndex = m_projects.empty() ? std::nullopt : std::make_optional(0);
        }
    }

    inline Project* getActiveProject() {
        if (m_activeProjectIndex && *m_activeProjectIndex < m_projects.size()) {
            return m_projects[*m_activeProjectIndex].get();
        }
        return nullptr;
    }

};