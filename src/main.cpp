#include "framework/app/Application.hpp"
#include "framework/core/Project.hpp"
#include "framework/commands/PropertyCommand.hpp"
#include "framework/serialization/XmlSerializer.hpp"
#include "framework/serialization/ComponentRegistry.hpp"

#include <spdlog/spdlog.h>

struct Motor : BaseComponent {
    COMPONENT_IDENTITY("Motor")
    
    Parameter<float> speed  {"speed", 0.0f};
    Parameter<float> torque {"torque", 10.0f};
    

/*
    void reflect(Visitor& v) {
        v.visit_property("speed",   speed,  {Tag::Persistent});
        v.visit_property("torque",  torque, {Tag::Persistent});
    }
*/
    
    REFLECT_PARAMS(speed, torque)
};

int main() {
    ComponentRegistry::instance().registerComponent<Motor>();

    auto& app = Application::instance();
    Project* proj = app.createProject("Factory_Alpha");

    Object motorObj = proj->createObject("MainConveyorMotor");
    auto& motor = motorObj.add<Motor>();

    motor.speed.onChange.connect([](const float& newSpeed) {
        spdlog::info("Speed updated to {}", newSpeed);
    });
    motor.speed.set(45.5f);
    proj->getStack().undo();

    if (XmlSerializer::save(*proj, "project_alpha.xml")) {
        spdlog::info("Successfully saved project to XML!");
    }

    Project* loadedProj = app.createProject("Loaded");
    if(XmlSerializer::load(*loadedProj, "project_alpha.xml")){
        spdlog::info("Successfully loaded project from XML!");
    }

    return 0;
}
