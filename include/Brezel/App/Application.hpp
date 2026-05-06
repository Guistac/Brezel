#pragma once
#include <memory>
#include <optional>
#include <vector>
#include <string_view>

#include "Brezel/Core/Project.hpp"
#include "Brezel/Serialization/DeserializationValidation.hpp"
#include "Brezel/Serialization/XmlDeserializer.hpp"
#include "Brezel/Serialization/XmlSerializer.hpp"

namespace Brezel {

namespace Application {

inline std::vector<std::unique_ptr<Project>> m_projects;
inline std::optional<size_t> m_activeProjectIndex;

inline Project *createProject(std::string_view name) {
  m_projects.push_back(std::make_unique<Project>(name));
  m_activeProjectIndex = m_projects.size() - 1;
  return m_projects.back().get();
}

inline void closeProject(size_t index) {
  if (index < m_projects.size()) {
    m_projects.erase(m_projects.begin() + index);
    m_activeProjectIndex =
        m_projects.empty() ? std::nullopt : std::make_optional<size_t>(0);
  }
}

inline Project *getActiveProject() {
  if (m_activeProjectIndex && *m_activeProjectIndex < m_projects.size()) {
    return m_projects[*m_activeProjectIndex].get();
  }
  return nullptr;
}

inline Project *loadProject(std::string_view path) {
  DeserializationValidationReport report;

  auto loadedProject = std::make_unique<Project>("");
  if (!Xml::loadProject(*loadedProject.get(), path, report))
    return nullptr;

  relinkProjectReferences(*loadedProject, report);

  switch (report.getSeverity()) {
  case Severity::Critical:
    report.log();
    return nullptr;
  case Severity::Warning:
    report.log();
    break;
  case Severity::Valid:
    break;
  }

  m_projects.push_back(std::move(loadedProject));
  m_activeProjectIndex = m_projects.size() - 1;
  return m_projects.back().get();
}

inline bool saveProject(Project *project, std::string_view path) {
  return Xml::saveProject(*project, path);
}

} // namespace Application

} // namespace Brezel