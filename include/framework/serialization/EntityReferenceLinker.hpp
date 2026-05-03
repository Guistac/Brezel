#pragma once

#include "framework/core/Visitor.hpp"
#include "framework/core/EntityReference.hpp"
#include "framework/core/Project.hpp"

class EntityReferenceLinkerVisitor : public ProjectVisitor{
public:
    EntityReferenceLinkerVisitor(Project& project): m_project(project) {}

    virtual void visit_property(const char* label, EntityReference& ref, std::initializer_list<Tag> tags = {}) override {
        ref.entity = m_project.getEntityByUUID(ref.uuid);
    }

private:
    Project& m_project;
};
