#pragma once
#include <vector>
#include <string>
#include "Brezel/Reflection/Visitor.hpp"
#include "Brezel/Core/EntityReference.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Serialization/DeserializationValidation.hpp"

namespace Brezel {

class EntityReferenceLinkerVisitor : public ComponentVisitor {
public:
    EntityReferenceLinkerVisitor(Project& project, DeserializationValidationReport& report) :
        m_project(project),
        m_report(report)
    {}

    virtual bool beginComponent(StringID componentTypeName) override {
        m_currentComponentTypeName = componentTypeName;
        return true;
    };

    virtual void visit_property(StringID label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        ref.entity = m_project.getEntityByUUID(ref.uuid);
        if(!ref.entity.isValid()){
            m_report.addError(
                Severity::Warning,
                m_currentEntity.getPath(),
                m_currentComponentTypeName.toString(),
                "Error linking EntityReference \"" + label.toString() + "\"",
                m_currentEntity
            );
        }
    }

    Entity m_currentEntity;

private:
    Project& m_project;
    DeserializationValidationReport& m_report;
    StringID m_currentComponentTypeName;
};


inline void relinkEntityReferences(Entity& entity, EntityReferenceLinkerVisitor& visitor, DeserializationValidationReport& report){
    visitor.m_currentEntity = entity;
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);
    entity.forEachChildEntity(relinkEntityReferences, visitor, report);
}

inline void relinkProjectReferences(Project& project, DeserializationValidationReport& report){
    EntityReferenceLinkerVisitor visitor(project,  report);
    project.forEachTopLevelEntity(relinkEntityReferences, visitor, report);
}

} // namespace Brezel
