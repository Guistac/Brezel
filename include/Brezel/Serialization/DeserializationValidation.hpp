#pragma once
#include <vector>
#include <string>
#include <spdlog/spdlog.h>
#include "Brezel/Core/Entity.hpp"
#include "Brezel/Core/UUID.hpp"

namespace Brezel {

enum class Severity {
    Valid = 0,
    Warning = 1,
    Critical = 2
};

struct ValidationError {
    Severity severity;
    std::vector<std::string> entityPath;
    std::string componentType;
    std::string message;
    Entity entity;

    std::string getPath(const std::string& separator){
        std::string output;
        for(size_t i = 0; i < entityPath.size(); i++){
            output += entityPath[i];
            if (i < entityPath.size() - 1) output += separator;
        }
        return output;
    }
};

class DeserializationValidationReport {
public:
    void addError(Severity severity, const std::vector<std::string>& entityPath, const std::string& componentType, const std::string& message, Entity entity = Entity()){
        m_errorList.push_back({
            .severity = severity,
            .entityPath = entityPath,
            .componentType = componentType,
            .message = message,
            .entity = entity
        });
        if(static_cast<int>(severity) > static_cast<int>(m_severity)) m_severity = severity;
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

} // namespace Brezel