# 00 - Motion Control & ECS Bridging Architecture Master Specification

**Document ID**: `docs/motion_control/00_MOTION_CONTROL_AND_BRIDGING_ARCHITECTURE.md`
**Status**: Authoritative Master Architecture

This document is the absolute source of truth for the StageWorld ECS <-> Real-Time Air Gap, the Mecanum motion control pipeline, the mathematical capabilities, and the immediate execution sprint.

---
## PART 1: The Air Gap Architecture & Commandments
# Stacato Architecture & Real-Time Bridging Specification

## 1. Executive Summary & Goals
The Stacato system merges modular show programming, high-level GUI interaction, and hard real-time robotics control. 
The core architectural challenge is that UI/Show Orchestration (which is dynamic, asynchronous, and stateful) and Robotics Fieldbus loops (which require sub-millisecond, deterministic, zero-allocation execution) are fundamentally incompatible if run in the same paradigm.

**The Goal:** Implement a **Hybrid ECS / Data-Oriented Architecture** that strictly decouples the Non-Real-Time (NRT) show execution from the Hard Real-Time (RT) 1 kHz motor loop using lock-free data structures. This achieves maximum modularity for the show editor without compromising the physical safety or determinism of the stage machinery.

---

## 2. Global Architecture Topology

The system is divided by a strict "Air Gap" into two execution domains, connected only by lock-free data bridges.

### 2.1 The Non-Real-Time Domain (NRT) - ~60 Hz
*   **Technologies:** EnTT (ECS), Node-Graph DAGs, UDP Network loops.
*   **Responsibilities:** Show timelines, GUI widgets, MavLink/MachineLink ingestion, dynamic graph topological sorting, fleet coordination, and spline compilation.
*   **Paradigm:** Pure ECS. Entities represent physical hardware. Components hold the *intent* (e.g., `MotionCommand`, `MecanumConfig`).

### 2.2 The Hard Real-Time Domain (RT) - 1 kHz (1 ms cycle)
*   **Technologies:** C++20 standard lock-free atomic primitives, pre-allocated cyclical arrays, EtherCAT/CANopen fieldbus threads.
*   **Responsibilities:** 4-wheel inverse kinematics, cyclic PDO data generation, localized safety state machines, and motion profiling.
*   **Paradigm:** Flat, data-oriented C++ classes (`MecanumMachine`, `MecanumController`) executing bounded, closed-form mathematics. **No ECS systems run here.**

---

## 3. The "Air Gap" Data Exchange Mechanism

All data crossing the NRT / RT boundary must use a two-lane lock-free highway. Mutexes, locks, and `std::shared_ptr` are strictly prohibited across this boundary.

### Lane 1: The Event Ring Buffer (For Discrete Verbs)
*   **Use Case:** Procedure calls, triggers, E-Stops, and Cues (Events that *must not be missed or overwritten*).
*   **Mechanism:** A Single-Producer, Single-Consumer (SPSC) Lock-Free Ring Buffer.
*   **Data Structure:** A `std::variant` of trivial POD structs.
    ```cpp
    using RtCommandEvent = std::variant<std::monostate, EventTriggerCue, EventEnableDrive>;
    ```
*   **Execution:** The NRT thread pushes to the tail; the RT thread drains the queue every 1 ms cycle and processes via `std::visit`.
    > [!NOTE]
    > **Sprint Status:** The `EventRingBuffer` is deferred for the current sprint (Option B). We currently route all requests through `MotionCommand` over the Triple Buffer, relying on the fact that the RT loop is faster than the NRT thread, avoiding missed events.

### Lane 2: The Templated State Blackboard (For Continuous Nouns)
*   **Use Case:** Telemetry, odometry, active targets, configurations (Data where only the *latest* value matters).
*   **Mechanism:** Simpson 4-Slot Triple Buffers (Wait-Free, < 20 ns latency).
*   **Data Structure:** Trivial PODs aligned to cache lines (`alignas(64)`).
*   **Execution:** Wrap Triple Buffers in a templated bridge. The NRT thread `writes()` new configurations; the RT thread `reads()` them into local `active*` variables at the top of its cycle.

---

## 4. Architectural Rules & Directives (The "Commandments")

These rules must be explicitly followed by all developers and AI agents touching the codebase.

**1. Total EnTT Quarantine:**
The 1 kHz RT loop must NEVER interact with the `entt::registry`. No `get()`, `view()`, `emplace()`, or `try_get()`. The RT thread only interacts with its wait-free buffers.

**2. Zero Dynamic Allocation in RT:**
The 1 kHz loop must never trigger a heap allocation. Ban `new`, `malloc`, `std::vector::push_back`, and `std::string` inside cyclic callbacks. All memory must be pre-allocated during initialization.

**3. Pure POD Bridge Payloads:**
Data crossing the boundary MUST be Plain Old Data. Enforce this via `static_assert(std::is_trivially_copyable_v<T>)`. Do not pass pointers across the thread boundary.

**4. Unidirectional State Ownership:**
The ECS (NRT) owns the **Intent** (Targets, Show Cues, Parameters). The RT loop owns the **Physical Truth** (Odometry, Motor Load, Active Kinematics). If the ECS needs to "snap" to the physical position (bumpless transfer), it must read the RT telemetry and overwrite its own intent.

**5. Autonomous RT Safety (Heartbeat Fallback):**
The RT loop must be capable of surviving a complete freeze of the OS scheduler or NRT thread. If the RT loop stops receiving new commands, it must autonomously degrade to a safe state (e.g., executing a controlled deceleration ramp).

**6. Single-Responsibility Profiling:**
A motion vector must be rate-limited exactly ONCE. Do not profile in the NRT thread and then profile again in the RT thread (which causes phase lag). Pass raw intent to the RT thread, and let the RT thread execute the single `VectorProfile2D` deceleration ramp.

---

## 5. The Continuous Preemption Principle (Trajectory Handoff)

To prevent latency-induced jerks (micro-gaps) when preempting a moving machine with a new command, execution must follow these rules:

*   **Strategy A: Rapids & Manual Jogging (RT-Side Generation)**
    *   *Rule:* The NRT thread sends ONLY the target spatial intent.
    *   *Execution:* The RT thread detects the command, samples its own instantaneous `(p, v, a)` state, and computes the $C^2$ continuous bridging polynomial (Quintic Hermite) inside the 1 kHz loop. Because initial boundary conditions are taken at the exact millisecond of preemption, there is zero gap.
*   **Strategy B: Show Splines (NRT Forward-Prediction)**
    *   *Rule:* The NRT thread handles heavy compilation, but must never generate a spline starting from "current" odometry.
    *   *Execution:* Evaluate the *currently executing* spline at $t_{\text{future}} = t_{\text{now}} + \Delta t_{\text{buffer}}$. Compile the new spline using that predicted state as the start boundary. Send it to the RT loop tagged for execution at exactly $t_{\text{future}}$. The RT loop flips buffers exactly on the synchronized clock tick.

---

## 6. Code Structuring & Naming Conventions

To clearly separate ECS representations from RT real-world state, enforce the following naming conventions:

1.  **Shared Boundary Structs:** Define once, purely as PODs. (e.g., `MecanumConfig`, `MotionCommand`).
2.  **ECS Side:** Handled as attached components on the `ecsEntity`.
3.  **RT Side (Active Latch):** Inside the RT machine class, maintain private copies prefixed with `active` (e.g., `activeConfig_`, `activeCommand_`).
4.  **Sync Functions:** Confine thread-crossing to explicit functions named for their data direction (e.g., `dispatchCommandToRT()`, `syncEcsFromOdometryFeedback()`). Core mathematical functions like `resolveTargets()` should never contain thread-sync logic.

---
## PART 2: Tactical ECS Ingestion Bridge (Option B Plan)
# Option B Detailed Implementation Plan: Tactical ECS Ingestion Bridge

**Document ID**: `docs/motion_control/03_OPTION_B_IMPLEMENTATION_PLAN.md`  
**Status**: Approved Architecture & Implementation Blueprint  
**Subsystem**: StageWorld ECS, Mecanum Machine, AnimatablePose, MachineLink Networking  
**Target Milestone**: Road to Task 2.6 (2D SAT Collision Avoidance)  

---

## Table of Contents
1. [Executive Objective & Design Principles](#1-executive-objective--design-principles)
2. [Component Architecture: The MotionCommand Component](#2-component-architecture-the-motioncommand-component)
3. [Thinning AnimatablePose: Exact Class Contract](#3-thinning-animatablepose-exact-class-contract)
4. [Unified Ingestion & Motion Arbiter Pipeline](#4-unified-ingestion--motion-arbiter-pipeline)
5. [MachineLink Telemetry, Reflection & Remote Commands](#5-machinelink-telemetry-reflection--remote-commands)
6. [Roadmap Synergy: Enabling Task 2.6 Collision Avoidance](#6-roadmap-synergy-enabling-task-26-collision-avoidance)
7. [Step-by-Step Execution Plan & Verification Checkpoints](#7-step-by-step-execution-plan--verification-checkpoints)

---

## 1. Executive Objective & Design Principles

The goal of this implementation plan is to transition the Stacato motion control subsystem to **Option B (Tactical ECS Ingestion Bridge)**.

### Core Constraints & Invariants:
1. **100% Backward Compatibility with the Animation Engine**:
   - `SequenceAnimation`, `Curve`, `Interpolation`, `SpatialEditorGui`, and `CurveEditorGui` must continue operating without a single line changed.
   - `AnimatablePose` remains an `Animatable` (type `POSE_6D`), serving as the lightweight bridge between the timeline and the vehicle.
2. **Single Ingestion Pipeline**:
   - Eliminate scattered, competing input checks (`localControl`, `joystickXpin`, `external_vx`, `remoteManual_vx`).
   - Every input source writes to a standardized, Plain Old Data (POD) component: `MotionCommand`.
3. **Single Profiling Stage (Zero Double-Profiling)**:
   - Strip duplicate 1D rate profilers from `AnimatablePose`.
   - `VectorProfile2D` in `MecanumController` becomes the sole translational rate limiter.
4. **Bumpless Transfers (Zero Jerk / No Rubber-Banding)**:
   - Velocity jog release smoothly decelerates, then locks `targetValue = actualValue` upon reaching zero speed.
   - Velocity jog engagement seeds `VectorProfile2D` with instantaneous moving velocity $(v_x, v_y)$.
5. **Deterministic Server-Proxy Reflection**:
   - The Server Proxy never runs local motion simulation or arbiters; it strictly ingests remote telemetry.
   - Live telemetry reflects actual wheel velocities, motor torques, tracking errors, and active control modes.

---

## 2. Component Architecture: The MotionCommand Component

We define `MotionCommand` in [`src/Stage/StageComponents.h`](file:///Users/leobecker/Stacato/src/Stage/StageComponents.h). It is a contiguous, heap-free POD struct attached to the machine's `entt::entity`:

```cpp
namespace Stacato::Stage {

/**
 * @brief High-level motion command types accepted by the vehicle motion arbiter.
 */
enum class MotionCommandType : uint8_t {
    IDLE_HOLD = 0,         // Closed-loop position hold at current odometry pose
    VELOCITY_JOG,          // Continuous manual velocity jog (rate-limited via VectorProfile2D)
    RAPID_TO_POSE,         // Point-to-point 2-point quintic Hermite move
    FOLLOW_TRAJECTORY,     // Synchronized spline playback (feedforward + closed-loop Kp)
    CONTROLLED_STOP,       // Deceleration ramp to zero speed, then automatically switches to IDLE_HOLD
    EMERGENCY_STOP         // STO emergency braking at maximum deceleration limit
};

/**
 * @brief Priority levels governing command preemption.
 */
namespace CommandPriority {
    constexpr uint8_t IDLE        = 0;
    constexpr uint8_t TRAJECTORY  = 10;
    constexpr uint8_t RAPID       = 20;
    constexpr uint8_t MANUAL_JOG  = 30;
    constexpr uint8_t CONTROL_STOP= 40;
    constexpr uint8_t SAFETY_STOP = 50; // Collision avoidance / link timeout
    constexpr uint8_t ESTOP       = 100;// Hardware STO / Operator E-stop
}

/**
 * @brief Unified Plain Old Data (POD) motion command component.
 */
struct MotionCommand {
    MotionCommandType type{MotionCommandType::IDLE_HOLD};
    uint8_t priority{CommandPriority::IDLE};

    // Coordinate Frame: True = World Frame (+X East, +Y North); False = Body Frame (+X Forward, +Y Left)
    bool isWorldFrame{true};

    // Commanded velocities (used when type == VELOCITY_JOG)
    glm::dvec2 linearVelocity{0.0, 0.0};  // m/s
    double angularVelocity{0.0};          // rad/s

    // Target coordinates (used when type == RAPID_TO_POSE or IDLE_HOLD)
    glm::dvec2 targetPosition{0.0, 0.0};  // meters (world coordinates)
    double targetHeading{0.0};            // radians (world coordinates)

    // Optional velocity overrides for Rapids (0.0 = use vehicle defaults)
    double customLinearSpeed{0.0};        // m/s
    double customAngularSpeed{0.0};       // rad/s

    // Trajectory Playback tracking
    uint32_t trajectoryId{0};
    double trajectoryTime{0.0};

    // Source tracking for telemetry and UI display
    uint8_t sourceId{0};                  // 0: None, 1: GUI, 2: IO, 3: MavLink, 4: MachineLink UDP, 5: Timeline
    uint64_t commandTimestampNs{0};       // Monotonic timestamp when command was issued
};

} // namespace Stacato::Stage
```

---

## 3. Thinning AnimatablePose: Exact Class Contract

`AnimatablePose` is reduced to an adapter that satisfies the [`Animatable`](file:///Users/leobecker/Stacato/src/Animation/Animatable.h) virtual interface and holds the state pointers.

```
+-----------------------------------------------------------------------------------------+
|                                    AnimatablePose                                       |
+-----------------------------------------------------------------------------------------+
|  VIRTUAL ANIMATABLE INTERFACE (Preserved for SpatialEditorGui & SequenceAnimation):     |
|  - getType() -> AnimatableType::POSE_6D                                                 |
|  - getCurveCount() -> 3 (MASK_XY_YAW) or 6 (MASK_ALL_6DOF)                             |
|  - getCurveNames() -> ["Position X", "Position Y", "Heading"]                           |
|  - getCurveUnit(idx) -> Meter or Degree                                                 |
|  - getValueAtAnimationTime(anim, t) -> Samples curves into AnimatablePoseValue         |
|  - validateAnimation(anim) -> Exact polynomial root extrema checking                     |
|                                                                                         |
|  STATE ACCESSORS (Read/written by MecanumMachine):                                      |
|  - targetValue: std::shared_ptr<AnimatablePoseValue> (world pos, vel, acc)               |
|  - actualValue: std::shared_ptr<AnimatablePoseValue> (measured physical odometry)       |
|                                                                                         |
|  ACTION HOOKS (Dispatches to Machine's MotionCommand):                                   |
|  - onRapidToValue(val) -> machine->commandRapid(targetPose)                             |
|  - onPlaybackStart(), onPlaybackPause(), onPlaybackStop()                               |
|  - stopMovement() -> machine->commandControlledStop()                                   |
+-----------------------------------------------------------------------------------------+
```

### Methods & Members Removed from `AnimatablePose`:
1. **Removed**: `Motion::PoseMotionArbiter arbiter;` (the internal arbiter).
2. **Removed**: `manualProfileX_`, `manualProfileY_`, `manualProfileYaw_`.
3. **Removed**: `commandedVelocityLinear_world`, `commandedVelocityAngular_deg`.
4. **Removed**: `manualJoystickInput`, `manualHeadingInput`.
5. **Removed**: `controlMode` (becomes a read-only helper querying the vehicle's `MotionCommand.type`).
6. **Removed**: Direct calls to `m->setActualPoseDirect(...)`.

### Refactored `AnimatablePose::updateTargetValue()`:
```cpp
void AnimatablePose::updateTargetValue(double time_seconds, double deltaTime_seconds) {
    std::lock_guard<std::recursive_mutex> lock(mutex);

    // If an animation is playing, sample the spline setpoint
    if (currentAnimation && currentAnimation->isPlaying()) {
        auto val = currentAnimation->getValueAtPlaybackTime();
        if (val) {
            auto p = val->toPose();
            if (p) {
                targetValue->position = p->position;
                targetValue->velocityLinear = p->velocityLinear;
                targetValue->accelerationLinear = p->accelerationLinear;
                targetValue->rotation = p->rotation;
                targetValue->velocityAngular = p->velocityAngular;
                targetValue->accelerationAngular = p->accelerationAngular;

                if (auto m = getMachine()) {
                    // Update ECS MotionCommand
                    m->commandFollowTrajectory(
                        glm::dvec2(p->position.x, p->position.y),
                        glm::dvec2(p->velocityLinear.x, p->velocityLinear.y),
                        Units::degreesToRadians(p->rotation.z),
                        Units::degreesToRadians(p->velocityAngular.z),
                        currentAnimation->getTrajectoryId(),
                        time_seconds
                    );
                }
            }
        }
    }
}
```

---

## 4. Unified Ingestion & Motion Arbiter Pipeline

All command sources are unified into a single ingestion step executed inside `MecanumMachine::commonOutputProcess()` or `MecanumController`:

```
+-----------------------------------------------------------------------------------------------+
|                                    INPUT INGESTION PIPELINE                                   |
+-----------------------------------------------------------------------------------------------+

  [Local GUI]       [Hardware IO]       [MavLink]       [MachineLink UDP]       [Timeline]
  localControl_X    joystickXpin        external_vx     remoteManual_vx         updateTargetValue
       |                  |                  |                  |                    |
       +------------------+------------------+------------------+--------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |         Input Ingestion & Priority          |
                      |   (Safety > Manual > Rapid > Trajectory)    |
                      +---------------------------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |           ecsEntity.MotionCommand           |
                      +---------------------------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |         MecanumController::update()         |
                      |  1. Evaluates MotionCommand.type            |
                      |  2. If VELOCITY: VectorProfile2D rate-limit |
                      |  3. If POSITION: P-gain error injection     |
                      |  4. World -> Body Coordinate Transformation |
                      |  5. 4-Wheel Inverse Kinematics              |
                      |  6. Proportional Saturation Scaling         |
                      +---------------------------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |      Actuators / Odometry Simulation        |
                      +---------------------------------------------+
```

### Ingestion Logic in `MecanumController::resolveTargets()`:

```cpp
void MecanumController::resolveTargets(
    double& targetVX, double& targetVY, double& targetVR, 
    double& accX, double& accY, double& accR,
    double& decX, double& decY, double& decR,
    float& torqueLim, bool& b_reset,
    uint64_t currentClockNs)
{
    // Read strictly from the Lane 2 bridge (Zero EnTT queries)
    Stacato::Stage::MotionCommand activeCmd;
    if (machine->motionCommandSlot_.hasNew()) {
        machine->motionCommandSlot_.read(activeCmd);
    } else {
        machine->motionCommandSlot_.readLatest(activeCmd);
    }
    auto* cmd = &activeCmd;

    // 0. Heartbeat Safety Check
    if (cmd->type != Stage::MotionCommandType::IDLE_HOLD && 
        (currentClockNs - cmd->commandTimestampNs) > 100'000'000) { // 100ms timeout
        // NRT thread froze or network disconnected. Autonomous downgrade.
        cmd->type = Stage::MotionCommandType::CONTROLLED_STOP;
    }

    // 1. Safety Checks (E-Stop / Disabling)
    if (machine->b_emergencyStopActive || (cmd && cmd->type == Stage::MotionCommandType::EMERGENCY_STOP)) {
        targetVX = targetVY = targetVR = 0.0;
        accX = decX = (config.stoLinearDecel_mps2 > 0.0) ? config.stoLinearDecel_mps2 : xy_acc_rated;
        accY = decY = accX;
        accR = decR = (config.stoAngularDecel_radps2 > 0.0) ? config.stoAngularDecel_radps2 : r_acc_rated;
        torqueLim = 100.0f;
        return;
    }

    if (machine->state == DeviceState::DISABLING) {
        targetVX = targetVY = targetVR = 0.0;
        accX = decX = (config.xy_decel_disable_mps2 > 0.0f) ? config.xy_decel_disable_mps2 : xy_acc_rated;
        accY = decY = accX;
        accR = decR = (config.r_decel_disable_radps2 > 0.0f) ? config.r_decel_disable_radps2 : r_acc_rated;
        torqueLim = config.torque_decel_disable;
        return;
    }

    // 2. Active Limits
    double activeLinVel = (config.maxLinearVelocity_mps > 0.0f) ? config.maxLinearVelocity_mps : xy_vel_rated;
    double activeRotVel = (config.maxAngularVelocity_radps > 0.0f) ? config.maxAngularVelocity_radps : r_vel_rated;
    double activeLinAcc = (config.maxLinearAccel_mps2 > 0.0f) ? config.maxLinearAccel_mps2 : xy_acc_rated;
    double activeRotAcc = (config.maxAngularAccel_radps2 > 0.0f) ? config.maxAngularAccel_radps2 : r_acc_rated;

    if (!b_highSpeedMode && cmd && cmd->type == Stage::MotionCommandType::VELOCITY_JOG) {
        activeLinVel *= config.lowSpeedMultiplier;
        activeRotVel *= config.lowSpeedMultiplier;
    }
    activeLinAcc *= config.globalAccelScaling;
    activeRotAcc *= config.globalAccelScaling;

    accX = accY = activeLinAcc;
    accR = activeRotAcc;
    decX = decY = activeLinAcc * config.decelRampMultiplier;
    decR = activeRotAcc * config.decelRampMultiplier;
    torqueLim = config.maxTorquePercent;

    if (!cmd) return;

    // 3. Command Processing by Type
    switch (cmd->type) {
        case Stage::MotionCommandType::IDLE_HOLD: {
            // Closed-loop position hold to prevent drift
            b_targetInWorldFrame = false;
            computeClosedLoopCorrection(cmd->targetPosition.x, cmd->targetPosition.y, cmd->targetHeading,
                                        0.0, 0.0, 0.0,
                                        targetVX, targetVY, targetVR);
            break;
        }

        case Stage::MotionCommandType::VELOCITY_JOG: {
            // Manual velocity jog: zero out tracking errors
            trackingErrorX_mm = trackingErrorY_mm = trackingErrorHeading_deg = trackingErrorRadial_mm = 0.0;
            b_targetInWorldFrame = cmd->isWorldFrame;

            // Clamping with 2D vector normalization to preserve direction
            double mag = glm::length(cmd->linearVelocity);
            if (mag > activeLinVel && mag > 1e-6) {
                targetVX = (cmd->linearVelocity.x / mag) * activeLinVel;
                targetVY = (cmd->linearVelocity.y / mag) * activeLinVel;
            } else {
                targetVX = cmd->linearVelocity.x;
                targetVY = cmd->linearVelocity.y;
            }
            targetVR = std::clamp(cmd->angularVelocity, -activeRotVel, activeRotVel);
            break;
        }

        case Stage::MotionCommandType::RAPID_TO_POSE: {
            b_targetInWorldFrame = false;
            // Evaluates active 2-point quintic Hermite rapid polynomial
            evaluateRapidTrajectory(targetVX, targetVY, targetVR);
            break;
        }

        case Stage::MotionCommandType::FOLLOW_TRAJECTORY: {
            b_targetInWorldFrame = false;
            if (machine->animatablePose && machine->animatablePose->targetValue) {
                auto& p = machine->animatablePose->targetValue;
                computeClosedLoopCorrection(p->position.x, p->position.y, Units::degreesToRadians(p->rotation.z),
                                            p->velocityLinear.x, p->velocityLinear.y, Units::degreesToRadians(p->velocityAngular.z),
                                            targetVX, targetVY, targetVR);
            }
            break;
        }

        case Stage::MotionCommandType::CONTROLLED_STOP: {
            targetVX = targetVY = targetVR = 0.0;
            b_targetInWorldFrame = false;
            // Check if vehicle has come to a complete stop
            if (linearProfile.getSpeed() < 0.005 && std::abs(rProfile.getVelocity()) < 0.005) {
                // Bumpless transfer: Lock position hold at exact physical odometry
                cmd->type = Stage::MotionCommandType::IDLE_HOLD;
                cmd->targetPosition = glm::dvec2(machine->odometry.getPX(), machine->odometry.getPY());
                cmd->targetHeading = machine->odometry.getPR();
                if (machine->animatablePose) {
                    machine->animatablePose->setActualPoseDirect(cmd->targetPosition.x, cmd->targetPosition.y, machine->odometry.getPR_deg());
                }
            }
            break;
        }

        default:
            break;
    }
}
```

---

## 5. MachineLink Telemetry, Reflection & Remote Commands

### Telemetry Generation on Client AGV:
In `MecanumMachine::streamTelemetry()`, the telemetry payload is populated directly from the authoritative physical and controller states:
```cpp
void MecanumMachine::streamTelemetry() {
    MachineLink::MecanumTelemetryPayload telem{};
    telem.timestampNs = Stacato::Stage::StageWorld::getMonotonicTimeNs();

    // Actual Odometry
    telem.px = static_cast<float>(odometry.getPX());
    telem.py = static_cast<float>(odometry.getPY());
    telem.pr = static_cast<float>(odometry.getPR());
    telem.vx = static_cast<float>(odometry.getData().vx);
    telem.vy = static_cast<float>(odometry.getData().vy);
    telem.vr = static_cast<float>(odometry.getData().vr);

    // Commanded / Profiled Setpoints
    telem.targetPx = static_cast<float>(animatablePose ? animatablePose->targetValue->position.x : odometry.getPX());
    telem.targetPy = static_cast<float>(animatablePose ? animatablePose->targetValue->position.y : odometry.getPY());
    telem.targetPr = static_cast<float>(animatablePose ? Units::degreesToRadians(animatablePose->targetValue->rotation.z) : odometry.getPR());
    telem.targetVx = static_cast<float>(controller.lastBodyVx);
    telem.targetVy = static_cast<float>(controller.lastBodyVy);
    telem.targetVr = static_cast<float>(controller.lastBodyVr);

    // Wheel Velocities & Torques
    for (size_t i = 0; i < 4; ++i) {
        if (i < axisMappings.size() && axisMappings[i] && axisMappings[i]->axis) {
            telem.wheelVelocity[i] = static_cast<float>(axisMappings[i]->axis->getVelocity());
            telem.wheelTargetVelocity[i] = static_cast<float>(axisMappings[i]->axis->actuatorProcessData.velocityTarget);
            telem.motorTorque[i] = static_cast<float>(axisMappings[i]->axis->feedbackProcessData.forceActual);
        }
    }

    // Map Active MotionCommand to MecanumControlMode
    auto& registry = Stacato::Stage::StageWorld::getInstance().getRegistry();
    if (auto* cmd = registry.try_get<Stacato::Stage::MotionCommand>(ecsEntity)) {
        switch (cmd->type) {
            case Stage::MotionCommandType::IDLE_HOLD:
                telem.controlMode = static_cast<uint8_t>(MachineLink::MecanumControlMode::IDLE); break;
            case Stage::MotionCommandType::VELOCITY_JOG:
                telem.controlMode = static_cast<uint8_t>(MachineLink::MecanumControlMode::MANUAL_VELOCITY); break;
            case Stage::MotionCommandType::RAPID_TO_POSE:
                telem.controlMode = static_cast<uint8_t>(MachineLink::MecanumControlMode::POSITION_GOTO); break;
            case Stage::MotionCommandType::FOLLOW_TRAJECTORY:
                telem.controlMode = static_cast<uint8_t>(MachineLink::MecanumControlMode::SPLINE_TRACKING); break;
            default:
                telem.controlMode = static_cast<uint8_t>(MachineLink::MecanumControlMode::IDLE); break;
        }
    }

    // Status word bits
    telem.statusWord = 0;
    if (isEnabled()) telem.statusWord |= MachineLink::MecanumStatusBits::ENABLED;
    if (isMoving())  telem.statusWord |= MachineLink::MecanumStatusBits::MOVING;
    if (b_emergencyStopActive) telem.statusWord |= MachineLink::MecanumStatusBits::ESTOP_ACTIVE;

    MachineLink::getCyclicDataSender()->sendMecanumTelemetry(telem);
}
```

### Pure Reader Ingestion on Server Proxy:
In `MecanumMachine::proxyInputProcess()`, the Server Proxy ingests the UDP telemetry payload into its `animatablePose->actualValue`, `animatablePose->targetValue`, and ECS components with **zero local mathematical execution**:
```cpp
void MecanumMachine::proxyInputProcess() {
    // Ingest telemetry from cyclic receiver
    auto receiver = MachineLink::getCyclicDataReceiver();
    MachineLink::MecanumTelemetryPayload telem{};
    bool isStale = false;

    if (receiver && receiver->getLatestMecanumTelemetry(getRemoteInstanceUuid(), getRemoteMachineId(), telem, &isStale)) {
        if (!isStale) {
            // Update Odometry and State directly
            odometry.data.px = telem.px;
            odometry.data.py = telem.py;
            odometry.data.pr = telem.pr;
            odometry.data.vx = telem.vx;
            odometry.data.vy = telem.vy;
            odometry.data.vr = telem.vr;

            // Reflect on AnimatablePose for SpatialEditorGui
            if (animatablePose) {
                animatablePose->actualValue->position = glm::dvec3(telem.px, telem.py, 0.0);
                animatablePose->actualValue->rotation = glm::dvec3(0.0, 0.0, Units::radiansToDegrees(telem.pr));
                animatablePose->targetValue->position = glm::dvec3(telem.targetPx, telem.targetPy, 0.0);
                animatablePose->targetValue->rotation = glm::dvec3(0.0, 0.0, Units::radiansToDegrees(telem.targetPr));
            }

            // Reflect on ECS StageWorld
            auto& reg = Stacato::Stage::StageWorld::getInstance().getRegistry();
            if (auto* pose = reg.try_get<Stacato::Stage::Pose2D>(ecsEntity)) {
                pose->x = telem.px; pose->y = telem.py; pose->yaw = telem.pr;
            }
            if (auto* target = reg.try_get<Stacato::Stage::TargetPose2D>(ecsEntity)) {
                target->x = telem.targetPx; target->y = telem.targetPy; target->yaw = telem.targetPr;
            }
        }
    }
}
```

---

## 6. Roadmap Synergy: Enabling Task 2.6 Collision Avoidance

By centralizing all vehicle movement into the `MotionCommand` ECS component, upcoming **Task 2.6 (2D SAT Collision Detection & Dynamic Stopping Envelope)** slots in with zero refactoring:

```
+-----------------------------------------------------------------------------------------------+
|                             TASK 2.6 INTEGRATION (ZERO-FRICTION)                              |
+-----------------------------------------------------------------------------------------------+

                      +---------------------------------------------+
                      |         Input Ingestion & Priority          |
                      |  (Writes Raw MotionCommand to entity)       |
                      +---------------------------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |       CollisionDetectionSystem (Task 2.6)   |
                      |  - Sweeps PolygonHull along velocity vector |
                      |  - If distance < DynamicStoppingEnvelope:   |
                      |      motionCommand.type = CONTROLLED_STOP;  |
                      |      safetyOverride.stopRequested = true;   |
                      +---------------------------------------------+
                                             |
                                             v
                      +---------------------------------------------+
                      |         MecanumController::update()         |
                      |  (Executes safe, verified command)          |
                      +---------------------------------------------+
```

Because `CollisionDetectionSystem` intercepts `MotionCommand` directly in the ECS registry:
- Collision avoidance automatically protects the robot across **all input sources** (GUI, Gamepad, MavLink, Remote UDP, and Timelines).
- No custom safety logic is needed inside individual input handlers or `AnimatablePose`.

---

## 7. Step-by-Step Execution Plan & Verification Checkpoints

### Granular Execution Steps:

1. **Step 1: Declare `MotionCommand` in `src/Stage/StageComponents.h`**
   - Add `MotionCommandType`, `CommandPriority`, and `struct MotionCommand`.
   - Update `StageComponents.h` and verify with `ninja -C Build`.

2. **Step 2: Bind `MotionCommand` to `MecanumMachine` Lifecycle**
   - In `MecanumMachine::onAddToNodeGraph()`, attach `MotionCommand` to `ecsEntity`.
   - Add command helpers to `MecanumMachine`:
     `commandVelocityJog(...)`, `commandRapid(...)`, `commandControlledStop(...)`, `commandEmergencyStop(...)`.

3. **Step 3: Strip `AnimatablePose` to a Thin Adapter**
   - Remove `PoseMotionArbiter` and 1D manual profilers from `AnimatablePose.h` and `.cpp`.
   - Route `onRapidToValue()` and `updateTargetValue()` directly to the machine's `commandRapid` and `commandFollowTrajectory`.
   - Update `AnimatablePoseGui.cpp` so joystick widgets call `machine->commandVelocityJog(...)`.

4. **Step 4: Unify Ingestion in `MecanumController::resolveTargets()`**
   - Ingest GUI (`localControl_X/Y/R`), IO pins (`joystickXpin`), MavLink (`external_vx`), and MachineLink UDP (`remoteManual_vx`) into `MotionCommand`.
   - Evaluate `MotionCommand.type` in `resolveTargets()` as specified in Section 4.
   - Implement bumpless transfer from `CONTROLLED_STOP` to `IDLE_HOLD`.

5. **Step 5: MachineLink Telemetry & Proxy Verification**
   - Ensure `streamTelemetry()` sets `controlMode`, `wheelVelocity`, `wheelTargetVelocity`, and target poses accurately.
   - Ensure `proxyInputProcess()` strictly reads telemetry and never advances local motion logic.

6. **Step 6: Verification & Test Runs**
   - Run `test_spline` and `test_stage_ecs`.
   - Run dual-instance localhost test (Server Proxy + Client AGV) to verify zero rubber-banding, responsive remote jogging, and 100% telemetry reflection.
   - Update [`docs/agv_ecs/02_AGV_ROADMAP.md`](file:///Users/leobecker/Stacato/docs/agv_ecs/02_AGV_ROADMAP.md) and [`docs/agv_ecs/06_AGV_DEV_JOURNAL.md`](file:///Users/leobecker/Stacato/docs/agv_ecs/06_AGV_DEV_JOURNAL.md).

---

*Document compiled and ready for execution under Task 2.5 refinement / Road to Task 2.6.*


---
## PART 3: Simpson Triple Buffer Details
# 04 - ECS <-> Hard Real-Time Simpson Bridge Implementation Plan

## 1. Executive Summary & Objective

In **Option B**, we successfully unified input ingestion in `MecanumController::resolveTargets()`, eliminated double-profiling lag, and trimmed `AnimatablePose` into a thin adapter. However, the direct query of `entt::registry` inside the 1 kHz cyclic real-time loop was a tactical shortcut that violates our hard real-time and thread-safety standards:
1. `entt::registry` is **not thread-safe** for concurrent operations across the GUI/Network threads and the 1 kHz RT fieldbus thread.
2. `get_or_emplace` performs dynamic heap allocations (`std::vector::push_back`) when sparse sets expand.
3. Placing a mutex around the registry would cause priority inversion, risking missed 1 ms EtherCAT deadlines.

**Objective**: Decouple `StageWorld` (EnTT ECS) from `MecanumController` (1 kHz Hard RT Fieldbus Loop) using lock-free, wait-free **Simpson 4-Slot Triple Buffers** (`MachineLink::TripleBuffer<T>`), achieving:
- **100% Wait-Free Reads and Writes** (< 20 ns latency, zero retries, zero locks).
- **Zero Heap Allocations** inside the 1 kHz cyclic callback.
- **Full Thread Safety** between GUI/Network threads and the isolated RT fieldbus core.
- **Bi-directional State Sync**: ECS `MotionCommand` flows into the RT Controller; RT odometry flows back into ECS `Pose2D` / `Velocity2D`.

---

## 2. Architecture & Data Flow

```text
 ┌────────────────────────────────────────────────────────┐
 │            NON-REAL-TIME THREADS (50 - 100 Hz)         │
 │            (GUI, Timeline, MachineLink, Fleet)         │
 │                                                        │
 │  StageWorld (EnTT Registry)                            │
 │  └── Robot Entity (ecsEntity)                          │
 │      ├── Pose2D, Velocity2D (Read from Outbound)       │
 │      ├── PolygonHull (Geometry for SAT Sweeper)        │
 │      ├── MotionCommand (Updated by UI / Fleet Logic)   │
 │      └── MecanumConfig (Authoritative Parameters)      │
 └──────────────────────┬─────────────────────────────────┘
                        │
                        │ Wait-Free Write (< 20 ns)
                        ▼
 ┌────────────────────────────────────────────────────────┐
 │           SIMPSON TRIPLE BUFFER BOUNDARY BRIDGE        │
 │                                                        │
 │  Inbound:  TripleBuffer<MotionCommand>  commandSlot_   │
 │  Outbound: TripleBuffer<OdometryState>  feedbackSlot_  │
 │  Config:   TripleBuffer<MecanumConfig>  configSlot_    │
 └──────────────────────┬─────────────────────────────────┘
                        │
                        │ Wait-Free Read (< 20 ns)
                        ▼
 ┌────────────────────────────────────────────────────────┐
 │         HARD REAL-TIME THREAD (1 kHz EtherCAT Loop)    │
 │         (Runner / EtherCatFieldbus Process)            │
 │                                                        │
 │  MecanumMachine::inputProcess()                        │
 │  ├── Reads commandSlot_.read(activeCommand)            │
 │  ├── Reads configSlot_.read(activeConfig_)             │
 │  ├── MecanumController::resolveTargets()               │
 │  │   └── Single-stage VectorProfile2D rate limiting    │
 │  ├── Computes Inverse Kinematics (Wheel Setpoints)     │
 │  ├── Integrates Local Odometry                         │
 │  └── Writes feedbackSlot_.write(odometrySnapshot)      │
 └────────────────────────────────────────────────────────┘
```

---

## 3. Data Structures

### 3.1 Odometry Feedback Snapshot (POD)
Add to [`src/Stage/StageComponents.h`](file:///Users/leobecker/Stacato/src/Stage/StageComponents.h):
```cpp
struct OdometryFeedback {
    double px{0.0};       // World X (meters)
    double py{0.0};       // World Y (meters)
    double pr{0.0};       // Heading (radians)
    double vx{0.0};       // Linear velocity X (m/s)
    double vy{0.0};       // Linear velocity Y (m/s)
    double vr{0.0};       // Angular velocity (rad/s)
    double ax{0.0};       // Acceleration X (m/s^2)
    double ay{0.0};       // Acceleration Y (m/s^2)
    double ar_degps2{0.0};// Angular acceleration (deg/s^2)
    uint64_t timestampNs{0};
};
```

### 3.2 SPSC Buffers in `MecanumMachine`
Add to [`src/Machine/Machines/Mecanum/Mecanum.h`](file:///Users/leobecker/Stacato/src/Machine/Machines/Mecanum/Mecanum.h):
```cpp
#include "Networking/MachineLink/CyclicDataSlot.h"

// Wait-Free SPSC Triple Buffers (Simpson's Algorithm)
MachineLink::TripleBuffer<Stacato::Stage::MotionCommand> motionCommandSlot_;
MachineLink::TripleBuffer<Stacato::Stage::OdometryFeedback> odometryFeedbackSlot_;
MachineLink::TripleBuffer<Mecanum::MecanumConfig> configSlot_;
```

> [!NOTE]
> **Why Triple Buffers for Commands?**
> While Triple Buffers are naturally designed for continuous state (where only the latest matters), we are using them for `MotionCommand` (which can contain transient intents like Rapids). This is architecturally safe *only* because the RT Consumer (1 kHz) strictly outpaces the NRT Producer (~60 Hz). The NRT thread cannot physically overwrite a command before the RT thread has sampled it.

### 3.3 Hardware Configuration Channel (`MecanumConfig`)
[`MecanumConfig`](file:///Users/leobecker/Stacato/src/Machine/Machines/Mecanum/MecanumConfig.h) is an `alignas(64)` 192-byte POD struct (exactly 3 CPU cache lines) attached to `ecsEntity` in the ECS.
- **Authoritative Edit-Time State**: Owned and modified by the GUI / Fleet Supervisor thread in the ECS.
- **Local Active Execution State**: `MecanumController` maintains a private `MecanumConfig activeConfig_` on the RT core.
- **Tear-Free Hand-off**: Parameter updates (e.g. tuning $K_p$ gains or speed limits in ImGui) are written to `configSlot_.write(newConfig)` and swapped into `activeConfig_` at the 1 ms cycle boundary, guaranteeing zero data tearing, zero false sharing, and zero cache bouncing into the RT core.

---

## 4. Execution Steps

### Step 1: Declare `OdometryFeedback` in `StageComponents.h`
- Define `struct OdometryFeedback` as pure POD with default member initializers.
- Verify memory alignment and trivial copyability (`std::is_trivially_copyable_v<OdometryFeedback>`).

### Step 2: Equip `MecanumMachine` with Simpson Triple Buffers
- In `Mecanum.h`, add `motionCommandSlot_` and `odometryFeedbackSlot_`.
- Initialize `motionCommandSlot_` with `IDLE_HOLD` in `MecanumMachine::init()`.

### Step 3: Route Command Dispatch through the Inbound Simpson Buffer
- In `MecanumMachine::commandVelocityJog`, `commandRapid`, `commandFollowTrajectory`, `commandControlledStop`, `commandEmergencyStop`, `commandIdleHold`:
  1. Build the `MotionCommand` POD struct.
  2. Write to `motionCommandSlot_.write(cmd)` (**Wait-Free, < 20 ns**).
  3. (Optional) If on the non-RT thread, update the `MotionCommand` component on `ecsEntity` for UI inspection.
  4. **Do NOT touch EnTT registry inside RT callbacks**.

### Step 4: Decouple `MecanumController::resolveTargets()` from EnTT
- In `Controller.h` / `Controller.cpp`:
  - Replace `registry.try_get<MotionCommand>(machine->ecsEntity)` with a local active command buffer:
    ```cpp
    Stacato::Stage::MotionCommand activeCmd;
    if (machine->motionCommandSlot_.hasNew()) {
        machine->motionCommandSlot_.read(activeCmd_);
    }
    ```
  - `resolveTargets()` operates strictly on `activeCmd_`.
  - When transitioning from `CONTROLLED_STOP` to rest, mutate `activeCmd_.type = IDLE_HOLD` locally in the controller.
  - Zero calls to `StageWorld::getRegistry()` in `resolveTargets()`.

### Step 5: Decouple Cyclic Odometry Feedback in `Process.cpp`
- In `MecanumMachine::commonInputProcess()`:
  - Package current odometry into `OdometryFeedback`.
  - Write `odometryFeedbackSlot_.write(feedback)` (**Wait-Free, < 20 ns**).
  - Remove direct `registry.try_get<Pose2D>` / `registry.try_get<Velocity2D>` calls from `commonInputProcess()`.
- Provide an asynchronous sync method `syncEcsFromFeedback()` (callable from `Environnement::updateSimulation()` or UI tick):
  - Reads `odometryFeedbackSlot_.readLatest(feedback)`.
  - Updates `Pose2D`, `Velocity2D`, and `TargetPose2D` in the `StageWorld` registry safely outside the 1 kHz hard RT loop.

### Step 6: Decouple `streamTelemetry()` from EnTT
- In `MecanumMachine::streamTelemetry()`, query `controller.getActiveMotionCommand()` or read the latest command snapshot directly instead of `reg.try_get<MotionCommand>(ecsEntity)`.

### Step 7: Build & Comprehensive Test Suite Verification
1. `ninja -C Build`: Verify 0 compile/link warnings or errors.
2. `./Build/test_spline`: Verify all 13 spline suites pass.
3. `./Build/test_stage_ecs`: Verify all 9 Stage ECS suites pass.
4. `./Build/test_machinelink`: Verify all 25 MachineLink suites pass.
5. `launch_dual_instances.sh`: Verify Server + AGV Client dual-instance live test with remote manual jogging and bumpless transitions.

---

## 5. Definition of Done
- Zero `getRegistry()` calls inside `MecanumController::resolveTargets()`, `commonInputProcess()`, or `commonOutputProcess()`.
- 100% wait-free reads and writes (< 20 ns) across the thread boundary via `MachineLink::TripleBuffer`.
- Zero heap allocations (`malloc`/`new`) in the cyclic 1 kHz RT loop.
- All unit test suites passing 100%.
- Living roadmap (`02_AGV_ROADMAP.md`), Dev Journal (`06_AGV_DEV_JOURNAL.md`), and Architecture Map (`REPOSITORY_MAP.md`) updated.

---

## 6. NRT Thread-Safety & Responsibilities (Adopted Strategy)

To resolve the lack of thread-safety within the EnTT registry across the Non-Real-Time (NRT) domain (GUI, Network, Collision threads), the following strategy has been locked in:

### 6.1 The NRT Concurrency Solution: Option A (The Reader-Writer Lock)
We add a `std::shared_mutex` to `StageWorld` (the Synchronous approach).
- **Readers** (Network Telemetry Packers, GUI Stage View, SAT Sweep) grab a `std::shared_lock`. Multiple threads can iterate the ECS simultaneously in parallel.
- **Writers** (Creating vehicles, adding components from the GUI, applying Network configuration payloads) grab a `std::unique_lock`.
- *Why it works here*: Because no thread in this group is the 1 kHz EtherCAT loop, a few microseconds of lock contention won't break the physical machine.

### 6.2 Thread Responsibility Matrix (Untangling the "God Thread")
We enforce this exact division of labor across the five system threads:

1. **The Hard RT Control Thread (1 kHz)**
   - **Driver**: EtherCAT Distributed Clock (`SCHED_FIFO` priority 99).
   - **Responsibilities**: Runs `MecanumMachine::inputProcess/outputProcess`, Closed-Loop PID tracking, and Odometry generation.
   - **Safety Status**: Unstoppable. Touches zero locks. Only interacts with the hardware and the Triple Buffers.

2. **The Master Show Clock Thread (Variable Hz)**
   - **Driver**: `Environnement::Runner` (or EtherCAT DC callbacks, depending on hardware presence).
   - **Responsibilities**: Runs `PlaybackManager`, evaluates $C^2$ Quintic Splines, and advances the playhead (`T_show`).
   - **Safety Status**: Non-critical. If the show pauses or stutters, the robot simply coasts to a stop via its local trajectory evaluator.

3. **[NEW] The Dedicated ECS Safety Supervisor Thread (100 Hz)**
   - **Driver**: A dedicated, standalone `std::jthread` owned by `StageWorld` that sleeps for exactly 10 ms.
   - **Responsibilities**:
     - Grabs a `shared_lock` on the `StageWorld` registry.
     - Runs the `CollisionDetectionSystem` (SAT polygon intersections).
     - Calculates the Dynamic Stopping Envelope ($D_{stop}$).
     - Automatically flags the `SafetyOverride` component if an obstacle is breached, which syncs immediately to the Triple Buffer.
   - **Safety Status**: Mission Critical Watchdog. It never pauses. It doesn't care if the timeline is playing or if the UI is saving a file. It just loops at 100 Hz protecting the vehicle.

4. **The Network ASIO Threads (Asynchronous)**
   - **Driver**: Boost.Asio context.
   - **Responsibilities**: Unpacks 50 Hz UDP telemetry bursts, grabs a `unique_lock` on `StageWorld`, and writes remote vehicle positions into the ECS.

5. **The GUI Main Thread (60 Hz)**
   - **Driver**: OS Windowing system (GLFW / OpenGL).
   - **Responsibilities**: Grabs `unique_lock` or `shared_lock` to draw the 2D Stage View and create new entities when the operator clicks.

---

## 7. Architectural Considerations for Future Evaluation (Deferred Scope)

> [!NOTE]
> **Scope Boundary & Anti-Feature-Creep Policy**:  
> The immediate execution scope of Plan 04 is strictly focused on **decoupling the 1 kHz RT loop from EnTT** via `motionCommandSlot_` and `odometryFeedbackSlot_`.  
> The advanced multi-reader / multi-rate concurrency patterns below are formally recorded for **future evaluation** once fleet scale or multi-frequency networking demands it, avoiding premature complexity now.

### Consideration A: Decoupled Multi-Rate Snapshot Publishing
- **Context**: If high-frequency network services (e.g. 200 Hz diagnostics, 700 Hz fast UDP telemetry) or background analytical tools require querying the fleet state concurrently with UI rendering.
- **Topics for Study**:
  1. **Flat Snapshot Publishing**: Having `StageWorld` publish an immutable POD struct (`FleetSnapshot { uint8_t count; RobotState robots[32]; }`) via a Simpson triple buffer at the end of each world tick.
  2. **RCU / Atomic Pointer Swapping**: Swapping between double-buffered component arrays or registry pointers so readers never block writers.
  3. **Seqlocks**: Evaluating 64-bit sequence counters per entity pool to detect concurrent structural mutation without locking.

### Consideration B: Deferred Command Queues for Entity Lifecycle
- **Context**: If dynamic addition or removal of stage entities (e.g. live obstacle placement, dynamic laser keep-out zones, runtime robot spawning/despawning) begins happening concurrently from multiple network or script sources.
- **Topics for Study**:
  1. Implementing a lock-free SPSC command buffer (`entt::basic_command_buffer` or custom queue) for `createVehicleEntity` and `destroyObstacle`.
  2. Flushing structural additions/removals exclusively at the sync boundary at the start of `StageWorld::update()`.



---
## PART 4: Motion Control Capabilities & Mathematics
# Stacato Motion Control Capabilities & Architectural Specification

**Document ID**: `docs/motion_control/01_MOTION_CONTROL_CAPABILITIES.md`  
**Status**: Living Architecture & Refactor Specification  
**Subsystem**: Motion Control, Kinematics, Trajectory Generation & ECS Arbiters  
**Target Platform**: Mecanum AGV, Cartesian Gantry & Multi-Axis Stage Automation  

---

## Table of Contents
1. [Executive Summary & Purpose](#1-executive-summary--purpose)
2. [Input Sources & Control Ingestion](#2-input-sources--control-ingestion)
   - [2.1 Input Sources Overview](#21-input-sources-overview)
   - [2.2 Ingestion Mechanisms & Multiplexing](#22-ingestion-mechanisms--multiplexing)
   - [2.3 Priority Hierarchy & Preemption](#23-priority-hierarchy--preemption)
3. [Control Modes & State Machine](#3-control-modes--state-machine)
   - [3.1 Device & Safety States](#31-device--safety-states)
   - [3.2 Operational Motion Modes](#32-operational-motion-modes)
   - [3.3 Mode Transitions & Bumpless Transfer](#33-mode-transitions--bumpless-transfer)
4. [Manual Controls & Field-Oriented Jogging](#4-manual-controls--field-oriented-jogging)
   - [4.1 Body-Frame vs. World-Frame Jogging](#41-body-frame-vs-world-frame-jogging)
   - [4.2 User Forward Angle Remapping](#42-user-forward-angle-remappng)
   - [4.3 Vector Deadzones & Non-Linear Shaping](#43-vector-deadzones--non-linear-shaping)
   - [4.4 Speed Derating & Precision Shifting](#44-speed-derating--precision-shifting)
5. [Rapids (Point-to-Point Hermite Moves)](#5-rapids-point-to-point-hermite-moves)
   - [5.1 Mathematical Foundation (Quintic Hermite)](#51-mathematical-foundation-quintic-hermite)
   - [5.2 Boundary Seeding & C2 Continuity](#52-boundary-seeding--c2-continuity)
   - [5.3 Coupled Multi-Axis Duration Planning](#53-coupled-multi-axis-duration-planning)
   - [5.4 Turnaround & Overshoot Penalties](#54-turnaround--overshoot-penalties)
   - [5.5 Mid-Flight Re-Targeting & Abort](#55-mid-flight-re-targeting--abort)
6. [Trajectories, Animations & Splines](#6-trajectories-animations--splines)
   - [6.1 Trajectory Data Model & Spatial Waypoints](#61-trajectory-data-model--spatial-waypoints)
   - [6.2 Piecewise Quintic Hermite Engine](#62-piecewise-quintic-hermite-engine)
   - [6.3 Time-Constrained vs. Velocity-Constrained Solvers](#63-time-constrained-vs-velocity-constrained-solvers)
   - [6.4 Network Compilation & Binary Decompilation](#64-network-compilation--binary-decompilation)
   - [6.5 Master Show Timeline Synchronization](#65-master-show-timeline-synchronization)
7. [Mecanum Kinematics & Odometry](#7-mecanum-kinematics--odometry)
   - [7.1 Chassis Geometric Parameters](#71-chassis-geometric-parameters)
   - [7.2 Inverse Kinematics (IK)](#72-inverse-kinematics-ik)
   - [7.3 Forward Kinematics (FK) & Dead-Reckoning Odometry](#73-forward-kinematics-fk--dead-reckoning-odometry)
   - [7.4 Surface Calibration & Gear Ratio Scaling](#74-surface-calibration--gear-ratio-scaling)
   - [7.5 Isotropic Limits vs. Diagonal Kinematics](#75-isotropic-limits-vs-diagonal-kinematics)
8. [Curve Validation & Analytical Verification](#8-curve-validation--analytical-verification)
   - [8.1 Geometric Soft Workspace Limits](#81-geometric-soft-workspace-limits)
   - [8.2 Derivative Extrema via Closed-Form Polynomial Roots](#82-derivative-extrema-via-closed-form-polynomial-roots)
   - [8.3 C2 Boundary Continuity Verification](#83-c2-boundary-continuity-verification)
   - [8.4 Curvature & Centripetal Acceleration Constraints](#84-curvature--centripetal-acceleration-constraints)
9. [Motor Saturation Mitigation Strategies](#9-motor-saturation-mitigation-strategies)
   - [9.1 The Mecanum Over-Actuation Dilemma](#91-the-mecanum-over-actuation-dilemma)
   - [9.2 Proportional Vector Scaling (Current Implementation)](#92-proportional-vector-scaling-current-implementation)
   - [9.3 Priority-Based Dynamic De-rating (Heading vs. Translation)](#93-priority-based-dynamic-de-rating-heading-vs-translation)
   - [9.4 Time-Dilation / Trajectory Staging](#94-time-dilation--trajectory-staging)
   - [9.5 Torque & Current Limit Saturation](#95-torque--current-limit-saturation)
10. [Ramps, Profilers & Dynamic Limiting](#10-ramps-profilers--dynamic-limiting)
    - [10.1 2D Coupled Collinear Vector Profiler](#101-2d-coupled-collinear-vector-profiler)
    - [10.2 Asymmetric Acceleration / Deceleration Ramps](#102-asymmetric-acceleration--deceleration-ramps)
    - [10.3 STO & Safe Stopping Ramps](#103-sto--safe-stopping-ramps)
    - [10.4 Closed-Loop P-Gain Tracking Error Injection](#104-closed-loop-p-gain-tracking-error-injection)
11. [Current Implementation vs. Target Requirements Matrix](#11-current-implementation-vs-target-requirements-matrix)
    - [11.1 Architectural Anti-Patterns Identified](#111-architectural-anti-patterns-identified)
    - [11.2 What the System CAN Do (Today)](#112-what-the-system-can-do-today)
    - [11.3 What the System MUST Do (Target ECS Spec)](#113-what-the-system-must-do-target-ecs-spec)
12. [ECS Refactoring Blueprints](#12-ecs-refactoring-blueprints)

---

## 1. Executive Summary & Purpose

The motion control system of Stacato drives omnidirectional Mecanum AGVs, multi-axis gantry stages, and winch automation rigs in live stage entertainment environments. In this domain, motion control must fulfill two strict, non-negotiable requirements simultaneously:
1. **Absolute Real-Time Safety & Mechanical Smoothness**: No velocity spikes, infinite jerk, or unexpected direction shifts that could destabilize stage scenery, endanger performers, or trigger hardware drive trips.
2. **Deterministic Artistic Repeatability**: Strict adherence to time-coded show cues, sub-millimeter trajectory tracking, smooth live manual joystick overrides, and instantaneous rapid repositioning between show numbers.

Over successive sprints, the codebase accumulated powerful mathematical modules (`QuinticHermiteSpline`, `VectorProfile2D`, `Kinematics`, `PoseMotionArbiter`), but distributed their invocation across disparate classes (`AnimatablePose`, `MecanumController`, `Process.cpp`, `StateMachine`).

This document provides a comprehensive, exhaustive catalogue of **all functions, mathematical models, input pipelines, and mitigation strategies** currently in the codebase, alongside the target specifications required for the unified ECS motion refactor.

```
+----------------------------------------------------------------------------------------------------+
|                                    STACATO MOTION CONTROL STACK                                    |
+----------------------------------------------------------------------------------------------------+
|                                           INPUT SOURCES                                            |
|   [GUI Widgets]   [Hardware IO / Gamepad]   [MavLink GCS]   [MachineLink UDP]   [Show Timelines]   |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
|                                      MOTION ARBITER & PROFILER                                     |
|   - Priority Arbitration & Deadman Switch Safety Interlock                                         |
|   - Mode Selection: POSITION_SETPOINT vs. VELOCITY_SETPOINT                                        |
|   - 2D Collinear Vector Profiling (Straight-line Braking)                                         |
|   - 2-Point Quintic Hermite Rapid Generator (C2 Boundary Seeding)                                  |
|   - Closed-Loop P-Gain Tracking Error Injection: v_cmd = v_ff + Kp * (p_target - p_actual)         |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
|                                  MECANUM KINEMATICS & DERATING                                     |
|   - Coordinate Frame Transformation: World (East/North) -> Body (Forward/Left)                     |
|   - 4-Wheel Inverse Kinematics (vx, vy, omega -> w0, w1, w2, w3)                                   |
|   - Motor Saturation Mitigation: Proportional Vector Derating (Preserving Curvature)               |
|   - Torque Clamping & Gear Ratio Scaling                                                           |
+----------------------------------------------------------------------------------------------------+
                                                  |
                                                  v
+----------------------------------------------------------------------------------------------------+
|                                      PHYSICAL ACTUATION / SIM                                      |
|   [CIA402 Servo Drives]               [Hardware Brakes]             [Simulated Odometry Engine]    |
+----------------------------------------------------------------------------------------------------+
```

---

## 2. Input Sources & Control Ingestion

### 2.1 Input Sources Overview

The motion engine handles five distinct command input streams:

1. **Local GUI Control Panel**:
   - Touchscreen/mouse virtual joysticks: `localControl_X`, `localControl_Y`, `localControl_R` normalized in range `[-1.0, 1.0]`.
   - Discrete UI toggles: Speed mode switch (High vs. Low precision), Absolute (Field-Oriented) vs. Body-Oriented move mode, Invert Direction toggle, Controlled Stop button, Wheel Test mode.
   - Setpoint entry widgets: Direct coordinate target input (X, Y, Heading) for rapid navigation.

2. **NodeGraph & Physical Hardware IO**:
   - Real-time pins: `joystickXpin`, `joystickYpin`, `joystickRpin` receiving values from USB/Bluetooth gamepads, industrial analog pendants, or DMX/ArtNet lighting consoles.
   - Digital safety signals: `enablePin`, `stoInPin`, `brakeFeedbackPin`, `brakeOverrideFeedbackPin`, and discrete mode pins `speedModePin`, `moveModePin`, `invertDirectionPin`.
   - Cyclic frequency: Evaluated at the environment loop rate (typically 50 Hz to 250 Hz).

3. **MavLink Drone / Autonomous GCS Stream**:
   - Protocols: MavLink v2 over UDP socket (`CONTROL_MAVLINK_IP`) or RS-422/UART serial bus (`CONTROL_MAVLINK_SERIAL`).
   - Ingested fields: `SET_POSITION_TARGET_LOCAL_NED` giving velocity targets (`external_vx`, `external_vy`, `external_vr`) and explicit acceleration caps (`external_ax`, `external_ay`, `external_ar`).
   - Operational context: Used when the AGV is guided by external automated tracking systems or mission planners.

4. **MachineLink Low-Latency Network Stream**:
   - UDP datagram protocol operating at 50 Hz.
   - Server-to-client manual jogging packets: Transmitting `remoteManual_vx` (m/s), `remoteManual_vy` (m/s), `remoteManual_vr` (rad/s), and `remoteManual_worldFrame` flag.
   - Spline trajectory network packets: Binary stream of compiled quintic Hermite segments (`MSG_SPLINE_TRAJECTORY`) for synchronized theatrical show playback.
   - Remote rapid dispatch: Point-to-point target packets targeting world coordinates with explicit linear/angular speed limits.

5. **SequenceAnimation & Show Timelines**:
   - Keyframe spline timelines managed by the `AnimationEngine`.
   - Multi-curve spatial coordinates: Curves for Position X (meters), Position Y (meters), and Heading (unwrapped degrees).
   - Timebase: Interpolated continuously based on SMPTE timecode or internal monotonic clock.

### 2.2 Ingestion Mechanisms & Multiplexing

Currently, control source selection is governed by the `ControlSource` enum in `MecanumMachine`:

```cpp
enum ControlSource : uint8_t {
    CONTROL_NONE = 0,
    CONTROL_IO,
    CONTROL_GUI,
    CONTROL_MAVLINK_IP,
    CONTROL_MAVLINK_SERIAL,
    CONTROL_PLAYBACK,
    CONTROL_WHEEL_TESTING
};
```

In the current code, ingestion is multiplexed inside `MecanumController::resolveTargets()` using conditional cascades:
- If `b_isEmergencyStopped`: All targets forced to zero; maximum STO deceleration applied.
- Else if `b_remoteManualActive`: UDP remote jog overrides local manual inputs.
- Else if `b_isPlaybackActive`: Timeline trajectory setpoint or active Rapid polynomial is sampled; feedforward velocity plus closed-loop position correction is injected.
- Else (Manual mode): Local GUI, IO pins, or MavLink targets are normalized, clamped to active rated limits, and fed into the profilers.

### 2.3 Priority Hierarchy & Preemption

| Priority Level | Command Source | Preemption Behavior | Deceleration / Transition Dynamics |
|---|---|---|---|
| **P0 (Critical Safety)** | Hardware STO / E-Stop | Immediate preemption of all modes; drives disabled or braked | Asymmetric emergency deceleration (`stoLinearDecel_mps2`, `stoAngularDecel_radps2`) |
| **P1 (System State)** | Machine Disabling / Fault | Preempts active motion; initiates controlled stop before opening brake | Controlled disable deceleration (`xy_decel_disable_mps2`, `r_decel_disable_radps2`) |
| **P2 (Operator Override)** | Manual Jog (Local / Remote) | Preempts active Rapids or Trajectory playback | C2 continuous seed from current moving state (no snap back) |
| **P3 (Direct Rapids)** | Rapid Position Command | Preempts Static Hold; cancels prior Rapid | Generates fresh quintic polynomial from instantaneous (p, v, a) |
| **P4 (Timeline Playback)** | Show Spline Playback | Executes only when Machine is enabled and no manual override is active | Closed-loop tracking with feedforward velocity injection |
| **P5 (Idle Hold)** | Closed-Loop Position Hold | Default state when no command is active | Holds target pose; corrects mechanical drift or external push |

---

## 3. Control Modes & State Machine

### 3.1 Device & Safety States

The machine operates under an 8-state deterministic finite state machine (`Mecanum::StateMachine`):

```
 [OFFLINE] <------------+
     |                  |
     v                  |
 [INITIALIZING]         |
     |                  |
     v                  |
 [NOT_READY] <----------+ (Drive Fault / STO Triggered)
     |                  |
     v (All axes ready) |
  [READY]               |
     |                  |
     v (Enable cmd)     |
 [ENABLING]             |
     |                  |
     v (Brakes open)    |
 [ENABLED]              |
     |                  |
     v (Disable cmd)    |
 [DISABLING]            |
     | (Speed <= 0)     |
     +------------------+
```

1. **OFFLINE**: Network connection lost, proxy disconnected, or hardware EtherCAT bus not initialized.
2. **INITIALIZING**: Actuator instances instantiating; axis configuration uploading.
3. **NOT_READY**: Safety loop open, drive fault detected, or hardware STO active. Brakes engaged.
4. **READY**: Drives powered and healthy; waiting for high-level enable request.
5. **ENABLING**: Safety valves energized; pneumatic/spring-applied brakes opening; waiting for brake release feedback.
6. **ENABLED**: Active closed-loop control. Drives modulating torque/velocity. Full motion capabilities available.
7. **DISABLING**: Controlled deceleration ramp active. When vehicle comes to complete rest, brakes are engaged and drive power is removed.
8. **FAULT**: Hardware error, over-current, tracking error limit exceeded, or thermal trip.

### 3.2 Operational Motion Modes

At the motion control layer, the system distinguishes two operational execution paradigms:

#### A. Position Control Mode (`MotionMode::POSITION` / `ControlMode::POSITION_SETPOINT`)
- **Intent**: The vehicle's exact physical coordinates $(X, Y, \Theta)$ in the world coordinate system are commanded and tracked.
- **Used by**:
  - Trajectory / Timeline Playback.
  - Point-to-Point Rapid moves.
  - Closed-loop Zero-Velocity Position Hold (preventing drift under stage vibration or external pushing).
- **Control law**:
  $$v_{\text{cmd}} = v_{\text{ff}} + K_p \cdot (p_{\text{target}} - p_{\text{actual}})$$
  Where $v_{\text{ff}}$ is the analytical velocity derivative of the spline, and the second term is a bounded proportional tracking correction.

#### B. Velocity Control Mode (`MotionMode::VELOCITY` / `ControlMode::VELOCITY_SETPOINT`)
- **Intent**: The operator directly commands target translation and rotation rates $(v_x, v_y, \omega)$. The absolute position is not constrained.
- **Used by**:
  - Manual joystick jogging (GUI, IO gamepad, remote MachineLink).
  - Controlled deceleration to stop (when joystick is released).
- **Control law**: The commanded input is rate-limited via a 2D collinear vector acceleration/deceleration filter, then converted into wheel velocities via inverse kinematics.

### 3.3 Mode Transitions & Bumpless Transfer

A critical historical flaw in the motion control stack was **mode transition jerk**:

1. **Velocity to Position Transition (Release Jog -> Hold)**:
   - *Problem*: While jogging under velocity control, odometry drifts slightly from the internal integration model. If the system abruptly enters position hold without resetting the target, the vehicle snaps violently backwards to eliminate the accumulated tracking error ("rubber-banding").
   - *Solution (Bumpless Transfer)*: Upon switching from `VELOCITY` to `POSITION`, the target pose is instantaneously snapped to the current measured physical odometry pose:
     ```cpp
     if (oldMode == ControlMode::VELOCITY_SETPOINT && newMode == ControlMode::POSITION_SETPOINT) {
         targetValue->position = actualValue->position;
         targetValue->rotation = actualValue->rotation;
         targetValue->velocityLinear = glm::dvec3(0.0);
         targetValue->velocityAngular = glm::dvec3(0.0);
     }
     ```

2. **Position to Velocity Transition (Hold / Rapid -> Manual Jog)**:
   - *Problem*: If the vehicle is moving mid-rapid and the operator deflects the joystick, an unseeded profiler would start rate-limiting from $0$, causing a severe deceleration spike.
   - *Solution*: The manual velocity profiler (`VectorProfile2D`) is seeded directly with the active moving velocity $(v_x, v_y, \omega)$ and acceleration $(a_x, a_y, \alpha)$ sampled at the exact millisecond of preemption.

---

## 4. Manual Controls & Field-Oriented Jogging

### 4.1 Body-Frame vs. World-Frame Jogging

The controller supports two distinct spatial frames for manual jogging:

1. **Body-Frame Jogging (`b_absoluteMoveMode = false`)**:
   - Deflecting the joystick $+Y$ drives the AGV forward along its physical chassis heading.
   - Deflecting $+X$ drives the AGV directly sideways to its right/left.
   - Ideal for maintenance, docking into narrow transport dollies, and fine mechanical alignment.

2. **World-Frame / Field-Oriented Jogging (`b_absoluteMoveMode = true`)**:
   - Deflecting the joystick North ($+Y$) moves the AGV toward the back of the stage (World $+Y$), regardless of which direction the robot chassis is currently facing.
   - The linear velocity vector $(v_{x,\text{world}}, v_{y,\text{world}})$ is smoothly rate-limited in world coordinates, then transformed dynamically into instantaneous body coordinates $(v_{x,\text{body}}, v_{y,\text{body}})$ using the AGV's live heading $\theta$:
     ```
     v_x_body =  cos(theta) * v_x_world + sin(theta) * v_y_world
     v_y_body = -sin(theta) * v_x_world + cos(theta) * v_y_world
     ```
   - Enables intuitive operator control: pushing the stick "away from front-of-house" always moves the machine away from the audience, even while the robot is spinning at 60 deg/s.

### 4.2 User Forward Angle Remapping

When operators stand at an angle relative to the stage grid, the coordinate system can feel inverted. The controller provides an arbitrary angular offset parameter:
```cpp
remapForward(targetVX, targetVY, config.userForwardAngle_rad);
```
This mathematically rotates the manual joystick command vector by an operator-defined angle $\phi_{\text{user}}$, aligning joystick $+Y$ with the operator's personal line of sight.

### 4.3 Vector Deadzones & Non-Linear Shaping

Raw analog joystick inputs suffer from mechanical center-spring hysteresis and electrical noise. The manual input pipeline provides:
- **Circular Deadzone**: Rejects small inputs where $\sqrt{X^2 + Y^2} < \text{threshold}$ (typically 5% to 8%). Eliminates actuator hum and unintended micro-creeping at rest.
- **Direction-Preserving Vector Normalization**: When combined inputs exceed unit length ($X^2 + Y^2 > 1.0$), the vector is normalized by its Euclidean magnitude rather than clipping axes independently:
  ```cpp
  double mag = std::hypot(targetVX, targetVY);
  if (mag > maxLinearVel && mag > 1e-6) {
      targetVX = (targetVX / mag) * maxLinearVel;
      targetVY = (targetVY / mag) * maxLinearVel;
  }
  ```
  This guarantees that driving diagonally at maximum stick deflection does not skew the travel angle from $45^\circ$ to an distorted axis-clamped trajectory.

### 4.4 Speed Derating & Precision Shifting

- **Low-Speed Mode (`b_highSpeedMode = false`)**: Scales maximum linear and rotational velocity caps by `config.lowSpeedMultiplier` (default $0.2$, or 20% of rated speed). Used during rehearsal, near scenery, or when docking.
- **High-Speed Mode (`b_highSpeedMode = true`)**: Unlocks full rated chassis velocity ($1.5\text{ m/s}$ linear, $60^\circ/\text{s}$ rotation).
- **Invert Direction Toggle (`b_invertDirection = true`)**: Instantly flips the commanded translational direction vector by $180^\circ$ without altering the rotational sense.

---

## 5. Rapids (Point-to-Point Hermite Moves)

### 5.1 Mathematical Foundation (Quintic Hermite)

Point-to-point "Rapid" repositioning uses a 1D Quintic Hermite polynomial for each active dimension ($X, Y, \Theta$):
$$s(\tau) = a_0 + a_1 \tau + a_2 \tau^2 + a_3 \tau^3 + a_4 \tau^4 + a_5 \tau^5, \quad \tau = \frac{t - t_0}{T} \in [0, 1]$$

The six coefficients are solved analytically from the six boundary constraints:
- Start boundary at $\tau = 0$: Position $p_0$, Velocity $v_0$, Acceleration $a_0$.
- End boundary at $\tau = 1$: Target Position $p_1$, Target Velocity $v_1 = 0$, Target Acceleration $a_1 = 0$.

```
a0 = p0
a1 = v0 * T
a2 = 0.5 * a0_acc * T^2
a3 = -10*p0 + 10*p1 - 6*v0*T - 4*v1*T - 1.5*a0_acc*T^2 + 0.5*a1_acc*T^2
a4 =  15*p0 - 15*p1 + 8*v0*T + 7*v1*T + 1.5*a0_acc*T^2 - a1_acc*T^2
a5 =  -6*p0 +  6*p1 - 3*v0*T - 3*v1*T - 0.5*a0_acc*T^2 + 0.5*a1_acc*T^2
```

### 5.2 Boundary Seeding & C2 Continuity

Because the polynomial solver accepts non-zero initial boundary conditions $(v_0, a_0)$, Rapids are **$C^2$ continuous** (continuous in position, velocity, and acceleration):
- If the AGV is stationary, $v_0 = 0$ and $a_0 = 0$. The resulting profile is a smooth minimum-jerk S-curve.
- If the AGV is already traveling at $1.2\text{ m/s}$ when a new rapid target is received, $v_0$ is set to $1.2\text{ m/s}$ and $a_0$ to current acceleration. The polynomial naturally bends the trajectory toward the new target without stopping or jolting the mechanics.

### 5.3 Coupled Multi-Axis Duration Planning

To ensure that the 4 Mecanum wheels do not exceed their combined velocity and acceleration envelopes during a multi-axis rapid (combining $X, Y,$ and rotation $\Theta$), `PoseMotionArbiter::planCoupledRapidDuration()` computes the minimum required execution duration $T_{\text{rapid}}$:

1. **Linear Translation Duration**:
   $$T_{\text{lin,vel}} = \frac{15}{8} \cdot \frac{\|\Delta p\|}{v_{\text{max}}}, \quad T_{\text{lin,acc}} = \sqrt{\frac{10}{\sqrt{3}} \cdot \frac{\|\Delta p\|}{a_{\text{max}}}}$$
2. **Rotational Duration**:
   $$T_{\text{rot,vel}} = \frac{15}{8} \cdot \frac{|\Delta \theta|}{\omega_{\text{max}}}, \quad T_{\text{rot,acc}} = \sqrt{\frac{10}{\sqrt{3}} \cdot \frac{|\Delta \theta|}{\alpha_{\text{max}}}}$$
3. **Mecanum Wheel Coupling Constraint**:
   Because Mecanum wheel velocities sum translation and rotation linearly:
   $$w_i \propto |v_x| + |v_y| + (L + W)|\omega|$$
   A coupled duration estimate accounts for simultaneous translation and rotation:
   $$T_{\text{coupled}} = \max\left(T_{\text{lin}}, T_{\text{rot}}, \frac{\|\Delta p\| + (L+W)|\Delta \theta|}{v_{\text{wheel,max}}}\right)$$
4. The final duration is the maximum of all physical constraints:
   $$T_{\text{rapid}} = \max(T_{\text{lin,vel}}, T_{\text{lin,acc}}, T_{\text{rot,vel}}, T_{\text{rot,acc}}, T_{\text{coupled}}, 0.2\text{ s})$$

### 5.4 Turnaround & Overshoot Penalties

If the robot is moving rapidly in $+X$ ($v_0 > 0$) and a rapid is commanded in $-X$ ($\Delta p < 0$), the polynomial must decelerate to zero before reversing direction. If the duration $T$ is planned solely on distance $|\Delta p|$, the peak acceleration required to reverse would spike beyond the physical drive limits.

The planner computes a **turnaround duration penalty**:
$$d_{\text{stop}} = \frac{v_0^2}{2 \cdot a_{\text{max}}}, \quad T_{\text{turnaround}} = \frac{|v_0|}{a_{\text{max}}} + \text{Duration}(d_{\text{stop}} + |\Delta p|)$$
This guarantees that reversing rapids smoothly arrest prior kinetic energy before accelerating toward the new target.

### 5.5 Mid-Flight Re-Targeting & Abort

- **Re-Targeting**: Calling `startRapid()` while a rapid is already underway samples the instantaneous moving state at elapsed time $t_{\text{current}}$, computes a new polynomial, and resets $t_{\text{elapsed}} = 0$. The transition has zero velocity or acceleration error.
- **Rapid Cancel**: Calling `cancelRapid()` locks the hold target at the current commanded pose and transitions to `POSITION` mode. If moving, `triggerControlledStop()` executes a controlled deceleration ramp to zero speed.

---

## 6. Trajectories, Animations & Splines

### 6.1 Trajectory Data Model & Spatial Waypoints

The spline engine operates on 6-DOF spatial waypoints (`Motion::SpatialWaypoint6D`):
```cpp
struct SpatialWaypoint6D {
    double time = 0.0;                       // Time along trajectory (seconds)
    glm::dvec3 position{0.0, 0.0, 0.0};       // World translation (meters)
    glm::dvec3 velocityLinear{0.0, 0.0, 0.0}; // m/s
    glm::dvec3 accelerationLinear{0.0};       // m/s^2
    glm::dvec3 rotation{0.0, 0.0, 0.0};       // Euler angles (Roll, Pitch, Yaw in degrees)
    glm::dvec3 velocityAngular{0.0};          // deg/s
    glm::dvec3 accelerationAngular{0.0};      // deg/s^2
    TangentMode tangentMode = TangentMode::SMOOTH;
};
```
For Mecanum AGVs, the active dimension mask is `MASK_XY_YAW` (X, Y, and Heading Yaw).

### 6.2 Piecewise Quintic Hermite Engine

A full show trajectory consists of $N-1$ piecewise polynomial segments connecting $N$ waypoints. For each segment $k \in [0, N-2]$:
- Segment duration $\Delta t_k = t_{k+1} - t_k$.
- Normalized parameter $\tau = (t - t_k) / \Delta t_k$.
- Position, velocity, acceleration, and jerk are evaluated in closed form in $< 50\text{ ns}$ without numerical approximation:
  $$\begin{aligned}
  p(\tau) &= a_0 + a_1 \tau + a_2 \tau^2 + a_3 \tau^3 + a_4 \tau^4 + a_5 \tau^5 \\
  v(\tau) &= \frac{1}{\Delta t} (a_1 + 2 a_2 \tau + 3 a_3 \tau^2 + 4 a_4 \tau^3 + 5 a_5 \tau^4) \\
  a(\tau) &= \frac{1}{\Delta t^2} (2 a_2 + 6 a_3 \tau + 12 a_4 \tau^2 + 20 a_5 \tau^3) \\
  j(\tau) &= \frac{1}{\Delta t^3} (6 a_3 + 24 a_4 \tau + 60 a_5 \tau^2)
  \end{aligned}$$

### 6.3 Time-Constrained vs. Velocity-Constrained Solvers

1. **Time-Constrained Solver (`solveTimeConstrained()`)**:
   - Waypoint timestamps $t_k$ are fixed by the show timeline or musical cues.
   - Tangents (velocities and accelerations) at interior waypoints are computed using Catmull-Rom or finite-difference formulations to ensure continuous curvature.
   - If waypoints are too close together in time, velocity and acceleration will spike. These are flagged during validation.

2. **Velocity-Constrained Solver (`solveVelocityConstrained()`)**:
   - The user defines spatial geometry (path waypoints) and a target cruise speed (e.g., $1.0\text{ m/s}$ linear, $45^\circ/\text{s}$ angular).
   - The solver numerically integrates the arc-length $S_k$ of each segment using 16-point Gauss-Legendre quadrature.
   - Timestamps $t_k$ are synthesized automatically:
     $$\Delta t_k = \max\left(\frac{S_{k,\text{lin}}}{v_{\text{target}}}, \frac{|\Delta \theta_k|}{\omega_{\text{target}}}\right)$$
   - Guarantees that the robot traverses the user's spatial curve at a constant linear speed without exceeding actuator limits.

### 6.4 Network Compilation & Binary Decompilation

Because theatrical show files can be large, Stacato compiles spline trajectories into a compact, cacheable binary format (`SplineCompiler`) for transmission over MachineLink UDP:

- **Structure**:
  - `SplineTrajectoryPayloadHeader`: Unique `trajectoryId`, segment count, waypoint count, content CRC32 hash.
  - Array of `SplineSegmentData`: Packed polynomial coefficients ($a_0 \dots a_5$) for X, Y, and Yaw.
  - Array of `PackedSpatialWaypoint`: Uncompressed boundary poses for keyframe visualization.
- **Decompilation**: The AGV client receives the binary payload, reconstructs the `SequenceAnimation`, verifies the CRC32 checksum, and arms the trajectory for synchronized playback.

### 6.5 Master Show Timeline Synchronization

During show playback:
1. The server or timecode reader broadcasts a synchronized clock `t_show`.
2. The AGV evaluates the spline at `t_show`, yielding feedforward setpoints:
   $$\mathbf{p}_{\text{target}}, \quad \mathbf{v}_{\text{ff}}, \quad \mathbf{a}_{\text{ff}}$$
3. Closed-loop P-gain tracking adds corrective velocity based on odometry error (see Section 10.4).

---

## 7. Mecanum Kinematics & Odometry

### 7.1 Chassis Geometric Parameters

Standard Stacato 4-wheel Mecanum chassis configuration:
- **Wheelbase ($2L$)**: Distance along $+X$ (front-to-back) between front and rear axles (`wheelSpacingY_m`). Half-wheelbase $L = \text{wheelSpacingY\_m} / 2$.
- **Track Width ($2W$)**: Distance along $+Y$ (left-to-right) between left and right wheel centerlines (`wheelSpacingX_m`). Half-track $W = \text{wheelSpacingX\_m} / 2$.
- **Effective Lever Arm**: $L + W = (\text{wheelSpacingX\_m} + \text{wheelSpacingY\_m}) / 2$.
- **Wheel Diameter ($D$)**: Wheel outer diameter (`wheelDiameter_m`). Circumference $C = \pi \cdot D$.
- **Roller Orientation (X-Pattern)**:
  - Wheel 0 (Front-Left, FL): Position $(+L, +W)$, roller contacts ground pointing at $+45^\circ$.
  - Wheel 1 (Front-Right, FR): Position $(+L, -W)$, roller contacts ground pointing at $-45^\circ$.
  - Wheel 2 (Rear-Left, RL): Position $(-L, +W)$, roller contacts ground pointing at $-45^\circ$.
  - Wheel 3 (Rear-Right, RR): Position $(-L, -W)$, roller contacts ground pointing at $+45^\circ$.

```
           +X (Forward)
                 ^
                 |
   FL [//] (0)       FR [\\\] (1)
        +-----------------+
        |                 |
 +Y <---|     Chassis     |---> -Y
 (Left) |     Center      |   (Right)
        |                 |
        +-----------------+
   RL [\\\] (2)       RR [//] (3)
                 |
                 v
            -X (Rear)
```

### 7.2 Inverse Kinematics (IK)

Given target body-frame velocities:
- $v_x$: Forward linear velocity ($+X$, m/s)
- $v_y$: Leftward linear velocity ($+Y$, m/s)
- $\omega$: CCW angular velocity ($+Z$, rad/s)

The surface velocity of each wheel is given by the kinematic projection:
$$\begin{aligned}
v_{w,0} &= v_x - v_y - (L + W) \cdot \omega \quad &&\text{(FL, Wheel 0)} \\
v_{w,1} &= v_x + v_y + (L + W) \cdot \omega \quad &&\text{(FR, Wheel 1)} \\
v_{w,2} &= v_x + v_y - (L + W) \cdot \omega \quad &&\text{(RL, Wheel 2)} \\
v_{w,3} &= v_x - v_y + (L + W) \cdot \omega \quad &&\text{(RR, Wheel 3)}
\end{aligned}$$

Wheel rotational speed in revolutions per second ($\text{rev/s}$):
$$\omega_{w,i} = \frac{v_{w,i}}{C} = \frac{v_{w,i}}{\pi \cdot D}$$

Motor shaft speed in $\text{rev/s}$ (accounting for gear ratio and motor direction):
$$\omega_{\text{motor},i} = \omega_{w,i} \cdot \text{gearRatio}_i \cdot \text{invertDirection}_i$$

### 7.3 Forward Kinematics (FK) & Dead-Reckoning Odometry

Given measured wheel rotational speeds $[\omega_{w,0}, \omega_{w,1}, \omega_{w,2}, \omega_{w,3}]$ in $\text{rev/s}$:
$$\begin{aligned}
v_{x,\text{body}} &= \frac{C}{4} \cdot (\omega_{w,0} + \omega_{w,1} + \omega_{w,2} + \omega_{w,3}) \\
v_{y,\text{body}} &= \frac{C}{4} \cdot (-\omega_{w,0} + \omega_{w,1} + \omega_{w,2} - \omega_{w,3}) \\
\omega_{\text{body}} &= \frac{C}{4(L + W)} \cdot (-\omega_{w,0} + \omega_{w,1} - \omega_{w,2} + \omega_{w,3})
\end{aligned}$$

**Planar Odometry Integration over cycle time $\Delta t$**:
To minimize integration drift during simultaneous translation and high-speed rotation, Stacato uses **midpoint angle integration** (2nd-order Runge-Kutta equivalent):
$$\begin{aligned}
\theta_{\text{mid}} &= \theta(t) + \frac{1}{2} \omega_{\text{body}} \Delta t \\
\Delta x_{\text{world}} &= (v_{x,\text{body}} \cos\theta_{\text{mid}} - v_{y,\text{body}} \sin\theta_{\text{mid}}) \Delta t \\
\Delta y_{\text{world}} &= (v_{x,\text{body}} \sin\theta_{\text{mid}} + v_{y,\text{body}} \cos\theta_{\text{mid}}) \Delta t \\
x(t + \Delta t) &= x(t) + \Delta x_{\text{world}} \\
y(t + \Delta t) &= y(t) + \Delta y_{\text{world}} \\
\theta(t + \Delta t) &= \text{wrap\_to\_pi}(\theta(t) + \omega_{\text{body}} \Delta t)
\end{aligned}$$

### 7.4 Surface Calibration & Gear Ratio Scaling

Floor surfaces vary in friction, roller compliance, and micro-slippage. The controller incorporates three independent calibration scalars:
- `calX`: Longitudinal traction scalar (typically $0.98 - 1.02$).
- `calY`: Lateral traction scalar (Mecanum rollers slip more laterally; typically $0.90 - 0.96$).
- `calR`: Effective rotational footprint scalar.

During forward kinematics, measured velocities are divided by calibration constants. During inverse kinematics, commanded velocities are pre-multiplied by calibration constants to achieve true ground-speed parity.

### 7.5 Isotropic Limits vs. Diagonal Kinematics

A key mathematical property of Mecanum drives is that wheel speed demand is **anisotropic**:
- Driving purely along $+X$ ($v_x = V, v_y = 0$): Each wheel rotates at $V / C$.
- Driving diagonally at $45^\circ$ ($v_x = V/\sqrt{2}, v_y = V/\sqrt{2}$):
  $$v_{w,0} = \frac{V}{\sqrt{2}} - \frac{V}{\sqrt{2}} = 0, \quad v_{w,1} = \frac{V}{\sqrt{2}} + \frac{V}{\sqrt{2}} = \sqrt{2} V$$
  Wheel 1 must spin $\sqrt{2} \approx 1.414$ times faster than in pure forward motion!

To guarantee that the machine can travel at its rated linear speed $v_{\text{rated}}$ in **any arbitrary heading ($0^\circ$ to $360^\circ$)** without triggering motor saturation, Stacato computes the **isotropic rated limit**:
$$v_{\text{linear,rated}} = \frac{C \cdot \omega_{\text{wheel,max}}}{\sqrt{2}}$$
This guarantees circular velocity capability without directional clipping.

---

## 8. Curve Validation & Analytical Verification

Before a trajectory or animation can be executed on stage, it passes through analytical safety validation (`AnimatablePose::validateAnimation` and `SplineCompiler::verifyContinuity`).

### 8.1 Geometric Soft Workspace Limits

Waypoints and trajectory bounds are validated against configurable bounding boxes (`lowerPositionLimit`, `upperPositionLimit`):
$$X_{\text{min}} \le p_x(t) \le X_{\text{max}}, \quad Y_{\text{min}} \le p_y(t) \le Y_{\text{max}}$$
Violations raise `ValidationError::CONTROL_POINT_POSITION_OUT_OF_RANGE`.

### 8.2 Derivative Extrema via Closed-Form Polynomial Roots

Unlike naive tools that check limits by sampling discretely every few milliseconds (which can easily miss narrow inter-sample spikes), Stacato evaluates **exact analytical extrema**:

For each 1D quintic polynomial $p(\tau) = a_0 + \dots + a_5 \tau^5$:
1. Velocity is a 4th-degree polynomial:
   $$v(\tau) = b_0 + b_1 \tau + b_2 \tau^2 + b_3 \tau^3 + b_4 \tau^4$$
2. Acceleration is a 3rd-degree (cubic) polynomial:
   $$a(\tau) = c_0 + c_1 \tau + c_2 \tau^2 + c_3 \tau^3$$
3. Critical points of velocity occur where $a(\tau) = 0$. Because this is a cubic equation, its roots $\tau \in [0, 1]$ are solved analytically using Cardano's formula.
4. Evaluating $v(\tau)$ at $\tau = 0$, $\tau = 1$, and all real roots of $a(\tau) = 0$ yields the **exact mathematical global maximum and minimum velocity** over the segment:
   $$v_{\text{max}} = \max(\{v(0), v(1)\} \cup \{v(\tau_i) \mid a(\tau_i) = 0, \tau_i \in [0, 1]\})$$
5. The same analytical procedure is applied to acceleration by solving roots of jerk $j(\tau) = 0$ (a 2nd-degree quadratic equation).

If $v_{\text{max}} > v_{\text{limit}}$ or $a_{\text{max}} > a_{\text{limit}}$, validation fails with `ValidationError::INTERPOLATION_VELOCITY_LIMIT_EXCEEDED` or `INTERPOLATION_ACCELERATION_LIMIT_EXCEEDED`.

### 8.3 C2 Boundary Continuity Verification

Between adjacent spline segments $k$ and $k+1$, `SplineCompiler::verifyContinuity()` checks boundary continuity across segment seams:
$$\begin{aligned}
|p_k(1) - p_{k+1}(0)| &\le \epsilon_{\text{pos}} \quad (0.005\text{ m}) \\
|v_k(1) - v_{k+1}(0)| &\le \epsilon_{\text{vel}} \quad (0.05\text{ m/s}) \\
|a_k(1) - a_{k+1}(0)| &\le \epsilon_{\text{acc}} \quad (0.5\text{ m/s}^2)
\end{aligned}$$
This guarantees that the trajectory contains no acceleration steps (jerk impulses) that would cause physical drive resonance or audible mechanical thumps.

### 8.4 Curvature & Centripetal Acceleration Constraints

When following a curved 2D path at speed $V$, the vehicle experiences lateral centripetal acceleration:
$$a_{\text{centripetal}} = \frac{V^2}{\rho} = V^2 \cdot \kappa$$
Where $\kappa = \frac{|x' y'' - y' x''|}{(x'^2 + y'^2)^{3/2}}$ is the spatial path curvature. Actuator limits require that combined tangential and normal acceleration remain within the friction circle of the Mecanum wheels.

---

## 9. Motor Saturation Mitigation Strategies

### 9.1 The Mecanum Over-Actuation Dilemma

Because a Mecanum AGV couples translation and rotation onto the same 4 wheels, demanding high linear velocity while simultaneously rotating will demand wheel speeds that exceed the physical motor limit $\omega_{\text{motor,max}}$.

If no mitigation is applied:
- One or two motors saturate (clip at their maximum speed).
- The remaining motors continue to accelerate.
- **Catastrophic Failure Result**: The kinematic velocity ratio is broken. The robot veers violently off its programmed trajectory, rotating unintentionally or drifting off path into stage scenery.

### 9.2 Proportional Vector Scaling (Current Implementation)

To prevent trajectory deformation, Stacato currently implements **Proportional Vector Scaling**:

1. After computing unconstrained wheel velocity demands $w_i$ for all wheels $i \in [0, 3]$, the controller checks each motor's velocity load:
   $$\text{load}_i = \frac{|w_i \cdot \text{gearRatio}_i|}{\omega_{\text{limit},i}}$$
2. The maximum load across all actuators is determined:
   $$\text{maxVelocityLoad} = \max_{i=0}^3 (\text{load}_i)$$
3. If $\text{maxVelocityLoad} > 1.0$, the entire kinematic state is scaled down uniformly by $1 / \text{maxVelocityLoad}$:
   $$w_i \leftarrow \frac{w_i}{\text{maxVelocityLoad}}, \quad v_{x,\text{body}} \leftarrow \frac{v_{x,\text{body}}}{\text{maxVelocityLoad}}, \quad v_{y,\text{body}} \leftarrow \frac{v_{y,\text{body}}}{\text{maxVelocityLoad}}, \quad \omega \leftarrow \frac{\omega}{\text{maxVelocityLoad}}$$
4. The internal state of the 2D vector profiler and rotation profiler are also scaled back so the integrator does not experience windup.

**Advantage**: The instantaneous motion direction and path curvature are **100% preserved**. The AGV stays exactly on its intended geometric path, simply slowing down its progression.

### 9.3 Priority-Based Dynamic De-rating (Heading vs. Translation)

In certain operational scenarios, proportional scaling is insufficient. The refactor design introduces **Priority-Based Dynamic Allocation**:

- **Scenario A (Camera / Follow-Spot Tracking)**: Heading orientation must remain locked onto the performer at all costs; linear translational speed can be sacrificed.
  - Allocation: Allocate wheel speed budget to $\omega$ first:
    $$w_{\text{rot}} = \frac{(L + W)|\omega|}{C}$$
    The remaining wheel velocity capacity $w_{\text{remain}} = \omega_{\text{max}} - w_{\text{rot}}$ is allocated to translation $(v_x, v_y)$.
- **Scenario B (Collision Avoidance / Escape)**: Linear escape velocity must be maximized; rotation can lag behind.
  - Allocation: Allocate wheel capacity to linear vector $(v_x, v_y)$ first; clamp $\omega$ to remaining headroom.

### 9.4 Time-Dilation / Trajectory Staging

During synchronized show spline playback, scaling down velocity at runtime causes the AGV to fall behind the show timeline, accumulating tracking error.
- **Offline Time-Dilation**: The trajectory compiler checks wheel loads during offline compilation. If any segment saturates wheels, the compiler dilates the segment time $\Delta t$ until peak wheel demand drops below $95\%$ of rated speed.
- **Online Timecode Pause**: If an obstacle or external force slows the robot down, the AGV can signal the master timecode controller to slow or hold the cue until the robot catches up.

### 9.5 Torque & Current Limit Saturation

In addition to velocity saturation, high accelerations demand excessive motor torque. The controller sets the drive torque limit dynamically:
```cpp
axis->setForceLimit(torqueLim / 100.0);
```
During emergency stops or controlled disabling, torque limits are clamped to `config.torque_decel_disable` to protect drive gearboxes from mechanical shock.

---

## 10. Ramps, Profilers & Dynamic Limiting

### 10.1 2D Coupled Collinear Vector Profiler

A common flaw in multi-axis motion controllers is **independent axis profiling** (profiling $X$ with one ramp and $Y$ with another). When a robot traveling diagonally at $(1.0, 0.2)\text{ m/s}$ stops, if both axes decelerate at the same scalar rate, the $Y$ axis reaches zero long before the $X$ axis, causing the robot to hook sideways rather than stopping in a straight line.

Stacato uses `Motion::VectorProfile2D` to guarantee **100% collinear braking**:
1. Delta velocity vector is computed: $\Delta \mathbf{v} = \mathbf{v}_{\text{target}} - \mathbf{v}_{\text{current}}$.
2. The unit direction vector is extracted: $\hat{\mathbf{u}} = \Delta \mathbf{v} / \|\Delta \mathbf{v}\|$.
3. Acceleration/deceleration rate limiting is applied strictly along the collinear vector $\hat{\mathbf{u}}$:
   $$\mathbf{v}(t + \Delta t) = \mathbf{v}(t) + \hat{\mathbf{u}} \cdot \min(a_{\text{rate}} \Delta t, \|\Delta \mathbf{v}\|)$$
4. The resulting deceleration path is a mathematically straight line in physical space.

### 10.2 Asymmetric Acceleration / Deceleration Ramps

Vehicles can typically decelerate much faster than they accelerate without slipping (due to weight transfer and motor regenerative braking). The profiler supports independent rates:
- Acceleration limit: $a_{\text{acc}} = \text{config.maxLinearAccel\_mps2} \cdot \text{globalAccelScaling}$.
- Deceleration limit: $a_{\text{dec}} = a_{\text{acc}} \cdot \text{config.decelRampMultiplier}$ (typically $1.5\times$ to $2.0\times$ acceleration).
- The profiler automatically detects whether current kinetic energy is increasing or decreasing ($\|\mathbf{v}_{\text{target}}\| < \|\mathbf{v}_{\text{current}}\|$), selecting the appropriate ramp rate.

### 10.3 STO & Safe Stopping Ramps

When an emergency stop or safe zone violation occurs:
- The controller switches immediately to emergency braking parameters:
  $$a_{\text{stop,lin}} = \text{config.stoLinearDecel\_mps2}, \quad a_{\text{stop,rot}} = \text{config.stoAngularDecel\_radps2}$$
- Targets are forced to $(0, 0, 0)$.
- Drives execute an aggressive controlled deceleration ramp down to zero speed before the physical holding brakes drop. This prevents mechanical wear and brake disc glazing caused by dropping friction brakes at full speed.

### 10.4 Closed-Loop P-Gain Tracking Error Injection

During trajectory playback, mechanical slip, floor irregularities, and compliance introduce tracking error between the theoretical spline setpoint $\mathbf{p}_{\text{target}}$ and the physical odometry $\mathbf{p}_{\text{actual}}$.

The controller runs a body-frame proportional feedback loop:
1. World tracking error is computed:
   $$\mathbf{e}_{\text{world}} = \mathbf{p}_{\text{target}} - \mathbf{p}_{\text{actual}}, \quad e_{\theta} = \text{wrap\_to\_pi}(\theta_{\text{target}} - \theta_{\text{actual}})$$
2. Error is transformed into the instantaneous body frame:
   $$\begin{aligned}
   e_{x,\text{body}} &=  \cos\theta \cdot e_{x,\text{world}} + \sin\theta \cdot e_{y,\text{world}} \\
   e_{y,\text{body}} &= -\sin\theta \cdot e_{x,\text{world}} + \cos\theta \cdot e_{y,\text{world}}
   \end{aligned}$$
3. Proportional corrective velocities are computed and clamped:
   $$\begin{aligned}
   v_{\text{corr},x} &= \text{clamp}(K_{p,\text{lin}} \cdot e_{x,\text{body}}, -v_{\text{corr,max}}, v_{\text{corr,max}}) \\
   v_{\text{corr},y} &= \text{clamp}(K_{p,\text{lin}} \cdot e_{y,\text{body}}, -v_{\text{corr,max}}, v_{\text{corr,max}}) \\
   \omega_{\text{corr}} &= \text{clamp}(K_{p,\text{ang}} \cdot e_{\theta}, -\omega_{\text{corr,max}}, \omega_{\text{corr,max}})
   \end{aligned}$$
4. Corrective velocity is added directly to feedforward spline velocity:
   $$\mathbf{v}_{\text{cmd,body}} = \mathbf{v}_{\text{ff,body}} + \mathbf{v}_{\text{corr,body}}$$
5. If tracking error exceeds safety thresholds (`maxPositionTrackingLimit_mm` or `maxHeadingTrackingLimit_deg`), a safety fault is triggered to abort the move.

---

## 11. Current Implementation vs. Target Requirements Matrix

### 11.1 Architectural Anti-Patterns Identified

1. **The "God-Wrapper" Problem (`AnimatablePose`)**:
   `AnimatablePose` currently handles:
   - Timeline keyframe curves and serialization.
   - Live ImGui rendering widgets.
   - Manual joystick input buffering.
   - Internal instantiation of `PoseMotionArbiter`.
   - Rapid move generation.
   *Impact*: Heavy coupling, impossible to unit test without the entire animation GUI stack, and obscure data ownership.

2. **Double Profiling**:
   - `PoseMotionArbiter` rate-limits joystick inputs using internal profiles (`manualProfileX_`, `manualProfileY_`).
   - The resulting velocity is passed to `MecanumController`, which passes it through `VectorProfile2D` *again*.
   *Impact*: Sluggish joystick response, phase lag, and double-smoothed cornering.

3. **Scattered Input Arbitration**:
   - GUI touches `localControl_X`.
   - MavLink touches `external_vx`.
   - MachineLink touches `remoteManual_vx`.
   - AnimatablePose touches `commandedVelocityLinear_world`.
   *Impact*: High bug density, race conditions, and difficult debugging.

4. **Proxy Telemetry Contention**:
   The server proxy historically tried to advance motion arbiters locally, resulting in phantom target positions that fought the physical client AGV.

### 11.2 What the System CAN Do (Today)

| Feature / Capability | Current Implementation Status | Location | Notes / Limitations |
|---|---|---|---|
| **Mecanum Inverse Kinematics** | Complete & Tested | `Kinematics.cpp` | Accurate 4-wheel equations with rectangular geometry. |
| **Dead-Reckoning Odometry** | Complete & Tested | `Kinematics.cpp` | Midpoint Runge-Kutta integration with calibration factors. |
| **Proportional Saturation Derating** | Complete & Tested | `Controller.cpp` | Preserves vector direction and path curvature. |
| **Quintic Hermite Rapids** | Complete & Tested | `PoseMotionArbiter.cpp` | C2 continuous boundary seeding from active (p, v, a). |
| **Coupled Rapid Duration Planning** | Complete & Tested | `PoseMotionArbiter.cpp` | Accounts for turnaround penalties and multi-axis coupling. |
| **Analytical Spline Validation** | Complete & Tested | `AnimatablePose.cpp`, `Spline.cpp` | Exact polynomial extrema via cubic/quadratic derivative roots. |
| **Collinear Vector Profiling** | Complete & Tested | `VectorProfile2D.h` | Straight-line braking without lateral drift. |
| **Spline Network Compilation** | Complete & Tested | `SplineCompiler.cpp` | Binary serialization with CRC32 verification. |
| **Bumpless Mode Transitions** | Partially Complete | `Process.cpp`, `Controller.cpp` | Transition from Velocity to Position snaps target; needs unification. |
| **Proxy Telemetry Sync** | Complete & Tested | `Process.cpp` | Server proxy bypasses local target evaluation; displays remote AGV. |

### 11.3 What the System MUST Do (Target ECS Spec)

| Requirement | Target Architecture | Implementation Mechanism |
|---|---|---|
| **Single Ingestion Pipeline** | Unified `MotionCommand` Component | All input sources write an explicit `MotionCommand` ECS component onto the entity. |
| **Eliminate Double Profiling** | Single Profiler in `MotionArbiterSystem` | Input commands are profiled exactly ONCE before kinematic transformation. |
| **Decouple AnimatablePose** | Pure ECS Components | Decompose `AnimatablePose` into `TargetPose2D`, `Velocity2D`, `SplineTrajectoryComponent`. |
| **Priority-Based Saturation** | Configurable Saturation Policy | Support both Proportional Derating and Heading-Priority Allocation. |
| **Deterministic Telemetry Mirroring** | Pure Reader Proxies | Proxy machines never run motion systems; they strictly ingest UDP state into ECS components. |
| **Zero-Allocation RT Cycle** | Fixed-Buffer ECS Iterators | No dynamic allocations (`std::vector`, `std::shared_ptr`) inside the cyclic motion loop. |

---

## 12. ECS Refactoring Blueprints

The refactored motion architecture replaces the monolithic class interactions with clean, decoupled EnTT ECS components and systems:

```
+-----------------------------------------------------------------------------------------------+
|                                    TARGET ECS MOTION PIPELINE                                 |
+-----------------------------------------------------------------------------------------------+

  [GUI]      [Gamepad]     [MavLink]     [MachineLink UDP]     [Show Timeline]
    |            |             |                 |                    |
    +------------+-------------+-----------------+--------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |           MotionCommand           |
                     |  - Mode (Jog, Rapid, Trajectory)  |
                     |  - Frame (Body, World)            |
                     |  - Target Vector / Waypoints      |
                     |  - Override Priority              |
                     +-----------------------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |        MotionArbiterSystem        |
                     |  - Evaluates priority & deadman   |
                     |  - Generates Rapid polynomials    |
                     |  - Applies 2D collinear profiling |
                     |  - Computes P-gain tracking error |
                     +-----------------------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |         KinematicSetpoint2D       |
                     |  - target_px, target_py, target_pr|
                     |  - cmd_vx_body, cmd_vy_body, omega|
                     +-----------------------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |       MecanumKinematicsSystem     |
                     |  - 4-Wheel Inverse Kinematics     |
                     |  - Saturation Derating (Scaling)  |
                     |  - Wheel Gear Ratio & Inversion   |
                     +-----------------------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |        WheelActuatorOutputs       |
                     |  - w0, w1, w2, w3 (rev/s)         |
                     |  - Torque limits (%)              |
                     +-----------------------------------+
                                       |
                                       v
                     +-----------------------------------+
                     |      Physical Drives / EtherCAT   |
                     |      or Simulation Odometry       |
                     +-----------------------------------+
```

### Proposed ECS Components

```cpp
namespace Stacato::Motion {

enum class CommandType : uint8_t {
    IDLE_HOLD = 0,
    VELOCITY_JOG,
    RAPID_TO_POSE,
    FOLLOW_TRAJECTORY,
    CONTROLLED_STOP
};

struct MotionCommand {
    CommandType type{CommandType::IDLE_HOLD};
    bool worldFrame{true};
    glm::dvec2 linearVelocity{0.0, 0.0};     // m/s
    double angularVelocity{0.0};             // rad/s
    glm::dvec2 targetPosition{0.0, 0.0};     // meters
    double targetHeading{0.0};               // radians
    uint32_t trajectoryId{0};
    double timelineTime{0.0};
    uint8_t priority{0};
};

struct KinematicSetpoint2D {
    // World coordinates
    glm::dvec2 worldPosition{0.0, 0.0};
    glm::dvec2 worldVelocity{0.0, 0.0};
    double worldHeading{0.0};
    double worldAngularVelocity{0.0};

    // Body-frame demands for Inverse Kinematics
    double bodyVx{0.0};                      // m/s (+X Forward)
    double bodyVy{0.0};                      // m/s (+Y Left)
    double bodyOmega{0.0};                   // rad/s (+Z CCW)
};

struct WheelDemands4W {
    double wheelSpeed_revps[4]{0.0, 0.0, 0.0, 0.0};
    float torqueLimitPercent[4]{100.0f, 100.0f, 100.0f, 100.0f};
    bool saturated{false};
    float saturationLoad{1.0f};
};

} // namespace Stacato::Motion
```

---

*Document compiled and verified against Stacato C++20 real-time motion source base.*


---
## PART 5: Sprint Execution Plan
**Monday: Foundation & ECS "Stage World" Boilerplate**
* Establish `StageWorld` (wrapping `entt::registry`).
* Define POD components (`MotionCommand`, `Pose2D`, `MecanumConfig`).

**Tuesday: Building the Air Gap Abstractions**
* Implement `CyclicStateBridge<T>` (Simpson Triple Buffers).
* Implement `EventRingBuffer<T>` (SPSC Lock-Free Queue).

**Wednesday: Quarantining the Real-Time Core**
* Inject the bridges into `MecanumMachine`.
* Refactor `MecanumController::resolveTargets()` to strictly use the Lane 2 bridge snapshot (Zero EnTT queries).

**Thursday: Legacy GUI & AnimatablePose Adapter**
* Sync ECS `Pose2D` from the RT `OdometryFeedback` on NRT tick.
* Strip `AnimatablePose` to a thin adapter: It simply reads ECS `Pose2D` for the UI, avoiding a costly UI rewrite.

**Friday: Integration, Synchronization & Verification**
* Run `test_machinelink`, `test_stage_ecs`.
* Verify bumpless transfers and the absence of RT heap allocations.

---
## APPENDIX: Historical Refactor Study (Option A vs B vs C)
# Architectural Study: Motion Control Unification & AnimatablePose Thin Interface

**Document ID**: `docs/motion_control/02_MOTION_CONTROL_REFACTOR_STUDY.md`  
**Target Milestone**: Road to Task 2.6 (SAT Collision Avoidance) & Multi-AGV Coordination  
**Primary Constraints**:
1. **Preserve Urgent Roadmap Velocity**: Do NOT jeopardize upcoming critical deliverables (Task 2.6 2D SAT Collision Detection, Task 2.7 StageEditor Speculative Sweep).
2. **Zero Breaking Changes to Animation Subsystem**: Do NOT rewrite `Animation`, `SequenceAnimation`, `SpatialEditorGui`, or `CurveEditorGui`.
3. **Thin Interface**: Retain `AnimatablePose` as a lightweight, clean adapter between the timeline/curve editor and the motion engine.

---

## 1. Executive Summary

The Stacato motion control codebase contains high-grade, battle-tested mathematics:
- 6-DOF piecewise quintic Hermite splines with analytical Cardano derivative root solving (`Spline.cpp`).
- Collinear 2D vector rate-limiting guaranteeing straight-line stopping (`VectorProfile2D.h`).
- 4-wheel Mecanum inverse/forward kinematics with isotropic rated limit protection (`Kinematics.cpp`).
- Proportional vector saturation derating preserving trajectory curvature (`Controller.cpp`).

However, the **control and ingestion plumbing** has become tangled:
- `AnimatablePose` evolved into a "God-Wrapper", mixing timeline curve evaluation with an internal `PoseMotionArbiter`, 1D velocity profilers, virtual joystick UI widgets, and direct axis overrides.
- Manual velocity jogging is handled across **four competing pathways** (`localControl_X/Y/R`, `joystickXpin`, `external_vx`, `remoteManual_vx`), with overlapping profiling and mode-switching quirks.
- Profiling occurs twice in some paths (first in `PoseMotionArbiter`, then in `VectorProfile2D`), causing sluggish stick response.

By reducing `AnimatablePose` to a **Thin Interface** and consolidating motion ingestion into a single, unambiguous pipeline, we can resolve these issues, eliminate mode-transition jerks, and position the system cleanly for Task 2.6 (Collision Avoidance) without rewriting the animation engine.

---

## 2. "Keep vs. Replace" Audit

| Subsystem / File | Component | Decision | Rationale & Action |
|---|---|---|---|
| **Animation** | `SequenceAnimation`, `Curve`, `Interpolation` | **KEEP 100%** | Stable, feature-complete timeline authoring engine. Zero changes required. |
| **Animation** | `AnimatablePose` | **THIN OUT** | **Keep** the `Animatable` virtual interface (`getCurveCount`, `getValueAtAnimationTime`, `validateAnimation`, `targetValue`, `actualValue`).<br>**Remove** internal `PoseMotionArbiter`, duplicate 1D profilers, and direct velocity jogging state. |
| **Animation** | `AnimatablePoseGui.cpp` | **KEEP / ROUTE** | Keep the visual widgets (`2D Jog`, `Go-To`, `Error Bars`), but route their actions through the unified command interface instead of mutating internal profilers. |
| **Motion/Curve** | `QuinticHermiteSpline`, `SplineCompiler` | **KEEP 100%** | High-performance C^2 spline math and network serialization. |
| **Motion/Curve** | `VectorProfile2D` | **KEEP 100%** | Flawless 2D collinear vector profiling with straight-line deceleration. Standardize on this across all manual modes. |
| **Motion** | `PoseMotionArbiter` | **ENCAPSULATE / REPURPOSE** | Keep its quintic Hermite Rapid generator and coupled duration budgeting; eliminate its redundant manual velocity profiles (delegate manual jogging to `VectorProfile2D`). |
| **Mecanum** | `Kinematics.cpp` / `Odometry.cpp` | **KEEP 100%** | Exact rectangular geometry IK/FK, isotropic speed derating, Runge-Kutta odometry integration. |
| **Mecanum** | `MecanumController` | **CONSOLIDATE** | Unify the 4 disparate input sources (`localControl`, `IO pin`, `MavLink`, `MachineLink UDP`) into a single input ingestion step. |
| **Stage (ECS)** | `StageComponents.h`, `StageWorld` | **LEVERAGE** | Utilize existing `Pose2D`, `TargetPose2D`, `Velocity2D`. Add a concise `MotionCommand` POD struct to serve as the unified bridge. |

---

## 3. The "Thin Interface" Anatomy for `AnimatablePose`

To avoid rewriting `Animation`, `SpatialEditorGui`, or `CurveEditorGui`, `AnimatablePose` must fulfill the `Animatable` contract without taking on vehicle controller duties:

```
+-----------------------------------------------------------------------------+
|                     AnimatablePose (Thin Adapter Interface)                  |
+-----------------------------------------------------------------------------+
|  Animatable Virtual Contract:                                               |
|  - getType() -> POSE_6D                                                     |
|  - getCurveCount() -> 3 (MASK_XY_YAW) or 6                                  |
|  - getCurveNames(), getCurveUnit()                                          |
|  - getValueAtAnimationTime(anim, t)                                         |
|  - validateAnimation(anim)                                                  |
|                                                                             |
|  State Holders (POD pointers):                                              |
|  - targetValue: std::shared_ptr<AnimatablePoseValue> (world pos, vel, acc)  |
|  - actualValue: std::shared_ptr<AnimatablePoseValue> (measured from odom)   |
|                                                                             |
|  Timeline Hooks (Delegates directly to Machine / Motion Engine):             |
|  - onPlaybackStart(), onPlaybackPause(), onPlaybackStop()                    |
|  - onRapidToValue(val) -> dispatches Rapid command to Machine               |
+-----------------------------------------------------------------------------+
```

### What Gets Stripped from `AnimatablePose`:
1. **No internal manual profilers**: Remove `manualProfileX_`, `manualProfileY_`, `manualProfileYaw_`.
2. **No joystick command state**: Remove `commandedVelocityLinear_world`, `manualJoystickInput`, `manualHeadingInput`.
3. **No direct axis manipulation**: Remove calls that bypass the machine's controller.
4. **No local duplicate state machine**: `controlMode` becomes a read-only mirror of the vehicle's active motion mode.

---

## 4. The Unified Ingestion Pipeline

Currently, inputs are resolved haphazardly across multiple classes. The unified pipeline channels all inputs into an explicit priority hierarchy:

```
[Local GUI]   [Hardware IO]   [MavLink]   [MachineLink UDP]   [Timeline Playback / Rapids]
     |              |             |               |                        |
     +--------------+-------------+---------------+------------------------+
                                  |
                                  v
                +------------------------------------+
                |       Unified Ingestion Step       |
                |   (Evaluate Priority & Deadman)    |
                +------------------------------------+
                                  |
               +------------------+------------------+
               |                                     |
               v (If Mode == VELOCITY)               v (If Mode == POSITION)
+-------------------------------+     +-----------------------------------+
|     VectorProfile2D (2D)      |     |  Spline Playback / Hermite Rapid  |
| - Collinear straight-line dec |     | - C^2 continuous setpoints        |
| - Asymmetric accel/decel      |     | - P-gain tracking error injection |
+-------------------------------+     +-----------------------------------+
               |                                     |
               +------------------+------------------+
                                  |
                                  v
              +---------------------------------------+
              |    Target Kinematics in Body Frame    |
              |       (vx_body, vy_body, omega)       |
              +---------------------------------------+
                                  |
                                  v
              +---------------------------------------+
              |   Mecanum 4-Wheel Inverse Kinematics  |
              |     & Proportional Vector Derating    |
              +---------------------------------------+
                                  |
                                  v
              +---------------------------------------+
              | Actuator Velocities (rev/s) & Torques |
              +---------------------------------------+
```

### Bumpless Transfer Rule:
- **Velocity Jog -> Position Hold**: When joystick is released and vehicle velocity drops below threshold ($< 0.005\text{ m/s}$), snap `targetValue = actualValue` and enter `POSITION` hold. **Zero rubber-banding.**
- **Position Hold -> Velocity Jog**: When joystick is deflected, seed `VectorProfile2D` with current moving velocity $(v_x, v_y)$ and acceleration. **Zero takeoff jolt.**

---

## 5. Architectural Options Comparison

### Option A: Direct Controller Unification (Fastest, Localized)
- **Concept**:
  - Keep `AnimatablePose` as a thin setpoint provider.
  - Consolidate all manual input resolution and rate-limiting inside `MecanumController::resolveTargets()`.
  - Delete duplicate profilers from `AnimatablePose`.
- **Roadmap Impact**: Completed in 1 day; immediately unblocks Task 2.6 (Collision Avoidance).
- **Pros**: Zero architectural churn; touches very few files; 100% backward compatible.
- **Cons**: Motion logic remains inside `MecanumController.cpp` rather than an isolated ECS system.

### Option B: Tactical ECS Ingestion Bridge (Recommended Balanced Option)
- **Concept**:
  - Keep `AnimatablePose` as a thin setpoint provider for timeline playback and rapid requests.
  - Define a lightweight POD component `MotionCommand` in `src/Stage/StageComponents.h`:
    ```cpp
    struct MotionCommand {
        enum class Type : uint8_t { IDLE_HOLD = 0, VELOCITY_JOG, RAPID, TIMELINE } type{Type::IDLE_HOLD};
        bool worldFrame{true};
        glm::dvec2 linearVelocity{0.0, 0.0};
        double angularVelocity{0.0};
        glm::dvec2 targetPosition{0.0, 0.0};
        double targetHeading{0.0};
        uint8_t priority{0};
    };
    ```
  - Input sources (GUI, IO, MavLink, UDP) write to `MotionCommand`.
  - A clean `MotionArbiter` processes `MotionCommand`, runs `VectorProfile2D` or Rapid, injects P-gain tracking, and outputs body $(v_x, v_y, \omega)$.
  - **Direct Synergies with Task 2.6**: The upcoming 2D SAT Collision Detection System can directly inspect and clamp `MotionCommand` or force a `CONTROLLED_STOP` before commands hit kinematics!
- **Roadmap Impact**: 1.5 - 2 days; establishes the exact data foundation required for Task 2.6 and Task 2.7.
- **Pros**: Perfectly fulfills the Tactical ECS Bridge mandate from `01_AGV_ARCHITECTURE.md`; cleanly separates ingestion from kinematics; leaves `AnimationEngine` untouched.
- **Cons**: Requires touching `StageComponents.h` and bridging `MecanumController`.

### Option C: Complete ECS Motion Engine Rewrite (High Risk)
- **Concept**:
  - Completely replace `MecanumController` and `AnimatablePose` with pure EnTT systems (`MotionInputSystem`, `MotionArbiterSystem`, `MecanumKinematicsSystem`).
- **Roadmap Impact**: 2+ weeks of refactoring; high regression risk; halts roadmap velocity.
- **Verdict**: **REJECTED** (Violates user's explicit directive to avoid deep rewrites and protect urgent roadmap deliverables).

---

## 6. Evaluation Matrix

| Metric | Option A (Direct Controller) | Option B (Tactical ECS Bridge) | Option C (Full Rewrite) |
|---|:---:|:---:|:---:|
| **Roadmap Delivery Speed** | High (1 day) | **High (1.5 - 2 days)** | Low (2+ weeks) |
| **Preserves Animation System** | 100% | **100%** | Risk of breakage |
| **Maintains Thin AnimatablePose** | Yes | **Yes** | N/A (deletes it) |
| **Preparation for Task 2.6 (SAT Collision)** | Moderate | **Superior** (Clean hook point) | High (delayed) |
| **Solves Double Profiling & Jerks** | Yes | **Yes** | Yes |
| **Code Maintainability & Clarity** | Good | **Excellent** | Excellent |
| **Recommended Choice** | Fallback | **PRIMARY RECOMMENDATION** | Do Not Pursue |

---

## 7. Recommended Implementation Plan (Option B)

### Step 1: Define `MotionCommand` POD Component
- Add `MotionCommand` to `src/Stage/StageComponents.h`.
- Attach `MotionCommand` to `MecanumMachine::ecsEntity`.

### Step 2: Strip `AnimatablePose` to a Thin Adapter
- Remove `arbiter`'s manual velocity profilers from `AnimatablePose`.
- In `AnimatablePose::onRapidToValue()`, write `CommandType::RAPID` to the machine's `MotionCommand`.
- In `AnimatablePose::updateTargetValue()`, strictly sample the active `Animation` spline setpoint.

### Step 3: Unify Input Ingestion in `MecanumController`
- Centralize GUI, IO pins, MavLink, and MachineLink UDP into writing `MotionCommand`.
- Evaluate commands with strict priority (Safety > Manual Override > Rapid > Playback > Hold).
- Profile manual inputs strictly once using `linearProfile` (`VectorProfile2D`).

### Step 4: Validate Bumpless Transitions & Telemetry
- Verify smooth transitions between jog, rapid, and playback without rubber-banding.
- Run `test_spline`, `test_stage_ecs`, and dual-instance localhost tests.
- Proceed immediately to **Task 2.6 (2D SAT Collision Detection)**.

