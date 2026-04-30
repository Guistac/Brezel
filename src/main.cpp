#include "framework/app/Application.hpp"
#include "framework/core/Project.hpp"
#include "framework/commands/PropertyCommand.hpp"
#include "framework/serialization/XmlSerializer.hpp"
#include "framework/serialization/ComponentRegistry.hpp"

#include <spdlog/spdlog.h>

struct Motor : BaseComponent {
    Parameter<float> speed  {"speed", 0.0f};
    Parameter<float> torque {"torque", 10.0f};
    
    virtual void reflect(ComponentVisitor& v) override {
        v.visit_property("speed",   speed,  {Tag::Persistent, Tag::CommandStack});
        v.visit_property("torque",  torque, {Tag::Persistent, Tag::CommandStack});
    }
};


int main() {

    ComponentRegistry::registerComponent<Motor>("Motor");

    Project* proj = Application::createProject("DefaultProject");

    Object motorObj = proj->createObject("Motor");
    auto& motor = motorObj.add<Motor>();

    motor.speed.onChange.connect([](const float& newSpeed) {
        spdlog::info("Speed updated to {}", newSpeed);
    });
    motor.speed.set(45.5f);
    proj->getStack().undo();

    if (XmlSerializer::save(*proj, "project_alpha.xml")) {
        spdlog::info("Successfully saved project to XML!");
    }


    Project* loadedProj = Application::createProject("Loaded");
    if(XmlSerializer::load(*loadedProj, "project_alpha.xml")){
        spdlog::info("Successfully loaded project from XML!");
    }

    return 0;
}
