#include "framework/app/Application.hpp"
#include "framework/core/Project.hpp"
#include "framework/commands/PropertyCommand.hpp"
#include "framework/core/Object.hpp"
#include <spdlog/spdlog.h>

struct MotorComponent {
    Parameter<float> speed;
    MotorComponent(CommandStack* stack) : speed("speed", 0.0f, stack) {}
};

int main() {
    auto& app = Application::instance();
    Project* proj = app.createProject("MySystem");
    Object motor = proj->createObject("Motor_1");
    auto& comp = motor.add<MotorComponent>();

    // --- SIGNAL EXAMPLE ---
    // We connect a "lambda" function to the speed change signal.
    // This could be a GUI update, a safety check, or a logger.
    comp.speed.onChange.connect([](const float& newSpeed) {
        if (newSpeed > 80.0f) {
            spdlog::warn("SAFETY: High speed detected! Current: {} units", newSpeed);
        } else {
            spdlog::info("Telemetry: Speed updated to {}", newSpeed);
        }
    });

    // --- TEST EXECUTION ---
    spdlog::info("Setting speed to 50...");
    comp.speed.set(50.0f); // Fires Signal (Info)

    spdlog::info("Setting speed to 95...");
    comp.speed.set(95.0f); // Fires Signal (Warning!)

    spdlog::info("Performing Undo...");
    proj->getStack().undo(); // Reverts to 50, Fires Signal (Info)
    
    spdlog::info("Final Speed: {}", comp.speed.get());

    return 0;
}