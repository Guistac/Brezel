#pragma once
#include <vector>
#include <memory>
#include <optional>
#include "framework/core/Project.hpp"

class Application {
public:
    // Singleton access
    static Application& instance() {
        static Application inst;
        return inst;
    }

    // Project Management
    Project* createProject(std::string_view name) {
        m_projects.push_back(std::make_unique<Project>(name));
        m_activeProjectIndex = m_projects.size() - 1;
        return m_projects.back().get();
    }

    void closeProject(size_t index) {
        if (index < m_projects.size()) {
            m_projects.erase(m_projects.begin() + index);
            m_activeProjectIndex = m_projects.empty() ? std::nullopt : std::make_optional(0);
        }
    }

    Project* getActiveProject() {
        if (m_activeProjectIndex && *m_activeProjectIndex < m_projects.size()) {
            return m_projects[*m_activeProjectIndex].get();
        }
        return nullptr;
    }

private:
    Application() = default;
    std::vector<std::unique_ptr<Project>> m_projects;
    std::optional<size_t> m_activeProjectIndex;
};