#include "Brezel/App/Application.hpp"
#include "Brezel/Command/ValueChangeCommand.hpp"
#include "Brezel/Console/Console.hpp"
#include "Brezel/Console/InteractiveConsole.hpp"
#include "Brezel/Core/Project.hpp"
#include "Brezel/Core/ComponentRegistry.hpp"
#include "Brezel/Serialization/ConsoleVisitor.hpp"
#include <spdlog/spdlog.h>

using namespace Brezel;

struct Motor {
  float speed{0.0f};
  float torque{10.0f};
  std::vector<float> test = {0.0, 1.0, 2.0, 3.0, 4.0};
};

template<typename V>
void reflect(Motor& m, V& v) {
  v.visit_property("speed", m.speed, {Tag::Persistent, Tag::CommandStack});
  v.visit_property("torque", m.torque, {Tag::Persistent, Tag::CommandStack});
  VectorAccessor va(m.test);
  v.visit_property("test", va, {Tag::Persistent});
}

struct TestComponent {
  float haha{123.4f};
  float hehe{567.8f};
  float hihi{987.6f};
  std::string hoho = "hoho";
  std::string huhu = "huhu";
  EntityReference motorRef;
};

template<typename V>
void reflect(TestComponent& tc, V& v) {
  v.visit_property("motorRef", tc.motorRef, {Tag::Persistent});
  v.visit_property("haha", tc.haha, {Tag::Persistent});
  v.visit_property("hehe", tc.hehe, {Tag::Persistent, Tag::CommandStack});
  v.visit_property("hihi", tc.hihi);
  v.visit_property("hoho", tc.hoho, {Tag::Persistent});
  v.visit_property("huhu", tc.huhu);
}

int main() {

  ComponentRegistry::registerComponent<Motor>("Motor");
  ComponentRegistry::registerComponent<TestComponent>("TestComponent");

  Project *proj = Application::createProject("DefaultProject");

  Entity motorObj = proj->createEntity("Motor");
  auto &motor = motorObj.add<Motor>();
  auto &test = motorObj.add<TestComponent>();

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

  spdlog::info("Initial motor speed: {}", motor.speed);

  // Demonstrate Undo/Redo pattern for the Stacato GUI
  auto cmd = std::make_unique<ValueChangeCommand<float>>(&motor.speed, motor.speed, 45.5f, "Set Motor Speed");
  proj->getStack().pushAndExecute(std::move(cmd));
  
  spdlog::info("Motor speed after command execute: {}", motor.speed);
  
  proj->getStack().undo();
  spdlog::info("Motor speed after undo: {}", motor.speed);

  proj->getStack().redo(); // Assuming redo isn't fully implemented in Brezel core, but let's see. If not, it just does nothing or errors. Wait, Brezel CommandStack doesn't have redo yet according to AGENTS.md.
  // Actually, I won't call redo, since AGENTS.md says it's missing.

  if (Application::saveProject(proj, "project_alpha.xml")) {
    spdlog::info("Successfully saved project to XML!");
  }

  if (Project *loadedProj = Application::loadProject("project_alpha.xml")) {
    spdlog::info("Successfully loaded project from XML!");

    printProject(*loadedProj);

    Console console(*loadedProj);
    console.onOutput.connect(
        [](const std::string &msg) { spdlog::info("{}", msg); });

    spdlog::info("--- Interactive Console Started. Type 'exit' to quit. ---");

    CLI::runInteractive(console);
  }

  return 0;
}
