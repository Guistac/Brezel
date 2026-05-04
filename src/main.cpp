#include "framework/app/Application.hpp"
#include "framework/commands/PropertyCommand.hpp"
#include "framework/console/Console.hpp"
#include "framework/core/Project.hpp"
#include "framework/serialization/ConsoleVisitor.hpp"
#include "framework/serialization/XmlDeserializer.hpp"
#include "framework/serialization/XmlSerializer.hpp"

#include <spdlog/spdlog.h>
#include <iostream>
#include "framework/console/InteractiveConsole.hpp"

struct Motor : BaseComponent {
  Parameter<float> speed{"speed", 0.0f};
  Parameter<float> torque{"torque", 10.0f};
  std::vector<float> test = {0.0, 1.0, 2.0, 3.0, 4.0};

  virtual void reflect(ComponentVisitor &v) override {
    v.visit_property("speed", speed, {Tag::Persistent, Tag::CommandStack});
    v.visit_property("torque", torque, {Tag::Persistent, Tag::CommandStack});
    VectorAccessor va(test);
    v.visit_property("test", va, {Tag::Persistent});
  }
};

struct TestComponent : BaseComponent {
  Parameter<float> haha{"haha", 123.4};
  Parameter<float> hehe{"hehe", 567.8};
  Parameter<float> hihi{"hihi", 987.6};
  std::string hoho = "hoho";
  std::string huhu = "huhu";
  EntityReference motorRef;
  virtual void reflect(ComponentVisitor &v) override {
    v.visit_property("motorRef", motorRef, {Tag::Persistent});
    v.visit_property("haha", haha, {Tag::Persistent});
    v.visit_property("hehe", hehe, {Tag::Persistent, Tag::CommandStack});
    v.visit_property("hihi", hihi);
    v.visit_property("hoho", hoho, {Tag::Persistent});
    v.visit_property("huhu", huhu);
  };
};

int main() {

  ComponentRegistry::registerComponent<Motor>("Motor");
  ComponentRegistry::registerComponent<TestComponent>("TestComponent");

  ConsoleDebugProjectVisitor consoleVisitor;

  Project* proj = Application::createProject("DefaultProject");

  Entity motorObj = proj->createEntity("Motor");
  auto& motor = motorObj.add<Motor>();
  auto& test = motorObj.add<TestComponent>();

  Entity motorObjChild = proj->createEntity("Motor_2");
  motorObjChild.get<IdentityComponent>().displayName = "Motor 2";
  motorObjChild.setParent(motorObj);
  motorObjChild.add<Motor>();

  Entity motorObjChild2 = proj->createEntity("Motor_3");
  motorObjChild2.get<IdentityComponent>().displayName = "Motor 3";
  motorObjChild2.setParent(motorObj);
  motorObjChild2.add<Motor>();

  Entity otherMotor = proj->createEntity("Other_Motor");
  otherMotor.get<IdentityComponent>().displayName = "Other Motor !!!!";
  otherMotor.add<Motor>();

  test.motorRef.set(motorObjChild2);

  motor.speed.onChange.connect([](const float& newSpeed) {
      spdlog::info("Speed updated to {}", newSpeed);
  });
  motor.speed.set(45.5f);

  if(Application::saveProject(proj, "project_alpha.xml")){
      spdlog::info("Successfully saved project to XML!");
  }

  if (Project *loadedProj = Application::loadProject("project_alpha.xml")) {
    spdlog::info("Successfully loaded project from XML!");
    loadedProj->reflect(consoleVisitor);

    Console console(*loadedProj);
    console.onOutput.connect(
        [](const std::string &msg) { spdlog::info("{}", msg); });

    spdlog::info("--- Interactive Console Started. Type 'exit' to quit. ---");
    
    CLI::runInteractive(console);
  }

  return 0;
}
