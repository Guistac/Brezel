#pragma once

#include "framework/core/Visitor.hpp"
#include "framework/core/EntityReference.hpp"
#include "framework/core/Project.hpp"
#include "framework/serialization/DeserializationValidation.hpp"


class EntityReferenceLinkerVisitor : public ComponentVisitor{
public:
    EntityReferenceLinkerVisitor(Project& project, DeserializationValidationReport& report) :
        m_project(project),
        m_report(report)
    {}

    virtual bool beginComponent(const char* componentTypeName) override {
        m_currentComponentTypeName = componentTypeName;
        return true;
    };

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        ref.entity = m_project.getEntityByUUID(ref.uuid);
        if(!ref.entity.isValid())
            m_report.addError(
                Severity::Warning,
                m_currentEntity.getPath(),
                m_currentComponentTypeName,
                "Error linking EntityReference \"" + std::string(label) + "\"",
                m_currentEntity
            );
    }

    Entity m_currentEntity;

private:
    Project& m_project;
    DeserializationValidationReport& m_report;
    
    std::string m_currentComponentTypeName;
};





void relinkEntityReferences(Entity& entity, EntityReferenceLinkerVisitor& visitor, DeserializationValidationReport& report){
    visitor.m_currentEntity = entity;
    ComponentRegistry::reflectEntityComponents(entity.handle(), visitor);
    entity.forEachChildEntity(relinkEntityReferences, visitor, report);
}

void relinkProjectReferences(Project& project, DeserializationValidationReport& report){
    EntityReferenceLinkerVisitor visitor(project,  report);
    project.forEachTopLevelEntity(relinkEntityReferences, visitor, report);
}
