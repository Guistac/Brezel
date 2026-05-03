#pragma once
#include <vector>
#include <memory>
#include <optional>
#include "framework/core/Project.hpp"
#include "framework/serialization/XmlDeserializer.hpp"
#include "framework/serialization/XmlSerializer.hpp"

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

    inline Project* loadProject(std::string_view path){
        auto loadedProject = std::make_unique<Project>("");

        if(!Xml::loadProject(*loadedProject.get(), path)) return nullptr;

        EntityReferenceLinkerVisitor linker(*loadedProject.get());
        loadedProject->reflect(linker);

        m_projects.push_back(std::move(loadedProject));
        m_activeProjectIndex = m_projects.size() - 1;
        return m_projects.back().get();
    }

    inline bool saveProject(Project* project, std::string_view path){
        return Xml::saveProject(*project, path);
    }

};