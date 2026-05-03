#pragma once

#include "framework/core/Visitor.hpp"
#include "framework/core/EntityReference.hpp"
#include "framework/core/Project.hpp"
#include "framework/serialization/DeserializationValidation.hpp"

class EntityReferenceLinkerVisitor : public ProjectVisitor{
public:
    EntityReferenceLinkerVisitor(Project& project, DeserializationValidationReport& report) :
        m_project(project),
        m_report(report)
    {}

    virtual bool beginEntity(Entity& entity) override {
        std::string name;
        if(auto identity = entity.try_get<IdentityComponent>()){
            name = identity->name;
        }
        m_entityPath.push_back(name);
        m_currentEntity = entity;
        return true;
    }
    virtual void endEntity(Entity& entity) override {
        m_entityPath.pop_back();
    }

    virtual bool beginComponent(const char* componentTypeName) override {
        m_currentComponentType = componentTypeName;
        return true;
    }
    virtual void endComponent() override {}

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        ref.entity = m_project.getEntityByUUID(ref.uuid);
        if(!ref.entity.isValid()){
            m_report.addError(Severity::Warning, m_entityPath, m_currentComponentType, "Error linking EntityReference \"" + std::string(label) + "\"", m_currentEntity);
        }
    }

private:
    Project& m_project;

    DeserializationValidationReport& m_report;
    std::vector<std::string> m_entityPath;
    std::string m_currentComponentType;
    Entity m_currentEntity;
};
