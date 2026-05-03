#pragma once

#include "framework/core/Entity.hpp"
#include "framework/core/UUID.hpp"

#include <spdlog/spdlog.h>

enum class Severity{
    Valid = 0,
    Warning = 1,
    Critical = 2
};

struct ValidationError{
    Severity severity;
    std::vector<std::string> entityPath;
    std::string componentType;
    std::string message;
    Entity entity;
    std::string getPath(const std::string& separator){
        std::string output;
        for(int i = 0; i < entityPath.size(); i++){
            output += entityPath[i] + separator;
        }
        return output;
    }
};

class DeserializationValidationReport{
public:
    void addError(Severity severity, const std::vector<std::string>& entityPath, const std::string& componentType, const std::string& message, Entity entity = Entity()){
        m_errorList.push_back({
            .severity = severity,
            .entityPath = entityPath,
            .componentType = componentType,
            .message = message,
            .entity = entity
        });
        if(severity > m_severity) m_severity = severity;
    }
    Severity getSeverity(){ return m_severity; }
    const std::vector<ValidationError>& getErrors(){ return m_errorList; }

    void log(){
        for(auto& error : m_errorList){
            spdlog::info("{}[Comp:{}] {}", error.getPath("/"), error.componentType, error.message);
        }
    }

private:
    std::vector<ValidationError> m_errorList;
    Severity m_severity = Severity::Valid;
};