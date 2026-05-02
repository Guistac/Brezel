#include "framework/app/Application.hpp"
#include "framework/core/Project.hpp"
#include "framework/commands/PropertyCommand.hpp"
#include "framework/serialization/XmlSerializer.hpp"
#include "framework/serialization/ComponentRegistry.hpp"
#include "framework/serialization/ConsoleVisitor.hpp"

#include <spdlog/spdlog.h>

struct Motor : BaseComponent {
    Parameter<float> speed  {"speed", 0.0f};
    Parameter<float> torque {"torque", 10.0f};
    
    virtual void reflect(ComponentVisitor& v) override {
        v.visit_property("speed",   speed,  {Tag::Persistent, Tag::CommandStack});
        v.visit_property("torque",  torque, {Tag::Persistent, Tag::CommandStack});
    }
};

struct TestComponent : BaseComponent{
    Parameter<float> haha{"haha", 123.4};
    Parameter<float> hehe{"hehe", 567.8};
    Parameter<float> hihi{"hihi", 987.6};
    std::string hoho = "hoho";
    std::string huhu = "huhu";
    virtual void reflect(ComponentVisitor& v) override {
        v.visit_property("haha",   haha, {Tag::Persistent});
        v.visit_property("hehe",   hehe, {Tag::Persistent});
        v.visit_property("hihi",   hihi, {Tag::Persistent});
        v.visit_property("hoho",   hoho);
        v.visit_property("huhu",   huhu);
    };
};


int main() {

    ComponentRegistry::registerComponent<Motor>("Motor");
    ComponentRegistry::registerComponent<TestComponent>("TestComponent");

    Project* proj = Application::createProject("DefaultProject");

    Entity motorObj = proj->createEntity("Motor");
    auto& motor = motorObj.add<Motor>();
    auto& test = motorObj.add<TestComponent>();

    Entity motorObjChild = proj->createEntity("Motor 2");
    motorObjChild.setParent(motorObj);
    motorObjChild.add<Motor>();

    Entity motorObjChild2 = proj->createEntity("Motor 3");
    motorObjChild2.setParent(motorObj);
    motorObjChild2.add<Motor>();

    Entity otherMotor = proj->createEntity("Other Motor !!!!");
    otherMotor.add<Motor>();

    motor.speed.onChange.connect([](const float& newSpeed) {
        spdlog::info("Speed updated to {}", newSpeed);
    });
    motor.speed.set(45.5f);

    IndentationProjectVisitor consoleVisitor;
    proj->reflect(consoleVisitor);

    proj->getStack().undo();

    if (XmlSerializer::save(*proj, "project_alpha.xml")) {
        spdlog::info("Successfully saved project to XML!");
    }


    Project* loadedProj = Application::createProject("Loaded");
    if(XmlSerializer::load(*loadedProj, "project_alpha.xml")){
        spdlog::info("Successfully loaded project from XML!");
    }

    //loadedProj->reflect(consoleVisitor);

    return 0;
}
