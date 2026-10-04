# Stacato (Stage Control Automation Toolbox) - Master Design Document

## Executive Summary
Stacato is a comprehensive, high-performance stage automation and control toolbox designed for real-time machinery operation. Built on a modular, node-based architecture, Stacato unifies complex industrial fieldbuses, networking protocols, motion control, and visual scripting into a single ecosystem. It is engineered with real-time performance in mind, supporting macOS, Windows, and Linux (with RT-kernel optimizations).

## Core Architecture

### Application & Environment Backend (`AppBackend`, `Project`, `Core`)
- **Memory & Lifecycle Management:** Employs application-level memory locking (`mlockall`) on UNIX systems to prevent page faults and ensure deterministic execution, essential for real-time industrial control.
- **Workspace & Serialization:** A robust project system built around serialized `FileComponent` and `Serializable` components, handling complex layout and state saving/loading.
- **GUI Framework (`Gui`, `StacatoGui`):** Utilizes an extensive Dear ImGui and ImPlot-based interface to deliver high-performance, real-time feedback. It includes node graph editors, dashboards, plotters, and specialized configurators for various machinery.

## Major Feature Modules

### 1. Visual Scripting & Node Graph (`Environnement`, `Nodes`)
Stacato relies heavily on a node-based visual programming environment, allowing users to dynamically construct data flows and logic pipelines.
- **Node Varieties:**
  - **Processors:** Mathematical operations, Logic gating, Clock generators, Display components, and Plotters.
  - **Motion Nodes:** Interpolators, Safety constraints, Axis mappings.
  - **Network & I/O:** Nodes exposing network streams and hardware data directly to the visual logic.
- **Node Execution:** A streamlined input/output processing loop dynamically resolves execution order across the graph.

### 2. Motion Control & Machinery (`Machine`, `Motion`, `Animation`)
Provides a deep abstraction layer for kinetic systems, spanning from individual motors to multi-axis automated structures.
- **Machine Abstraction (`Machine`):** Defines high-level machinery (e.g., `PositionControlledMachine`, `VelocityControlledMachine`, `MecanumMachine`). It wraps state machines, emergency stops, and hardware enablement sequences.
- **Axis & Actuator Interfaces:** Standardized interfaces (`AxisInterface`, `ActuatorInterface`) abstracting homing, limit checking, following errors, and effort monitoring across diverse hardware.
- **Animation Engine:** Animate-able positions, velocities, and states driven by interpolation curves (Linear, Step, Kinematic) and maneuver sequencing (`Manoeuvre`, `SequenceAnimation`).
- **Safety Systems:** Integrated `DeadMansSwitch`, `Brake` nodes, and `SafetySignal` handling directly embedded into the motion flow.

### 3. Industrial Fieldbus Integration (`Fieldbus`)
A core pillar of Stacato is its native capability to interface with industrial machinery via EtherCAT, leveraging robust drivers and custom state handling.
- **EtherCAT Ecosystem (`SOEM` based):** Comprehensive support for discovering, configuring, and monitoring EtherCAT devices.
- **Device Support:** Native drivers for an extensive range of industrial hardware:
  - **Beckhoff:** EL/AX terminals, FSoE (Fail Safe over EtherCAT) modules.
  - **Schneider Electric:** Lexium32/32i, ATV320/340 series.
  - **Yaskawa, Kinco, Nanotec, ABB, Phoenix Contact, Leadshine:** Out-of-the-box support with custom GUI panels for configuration and diagnostics.
- **Utilities:** Support for CiA DS402 drive profiles, Modular Device Profiles (MDP), PDO mapping, and ESI (EtherCAT Slave Information) parsing.

### 4. Networking & Interoperability (`Networking`)
Built to communicate with various show-control and tracking systems using low-latency UDP/TCP implementations powered by ASIO.
- **OSC (Open Sound Control):** Full bidirectional OSC messaging for integration with media servers, audio consoles, and lighting desks.
- **PSN (Positiostage Net):** Native parsing and serving of PSN data for 3D positional tracking.
- **Art-Net:** DMX over IP for lighting control integration.
- **NatNet:** OptiTrack motion capture integration.
- **MavLink:** Communication protocol for unmanned systems and drones.

### 5. Scripting Engine (`Scripting`)
Integrates a Lua runtime to allow procedural automation and customization beyond the node graph capabilities.
- **Custom Bindings:** Exposes logging, canvas drawing, PSN, and Art-Net directly to the Lua environment (`EnvironnementLibrary`, `LoggingLibrary`, `CanvasLibrary`, etc.).
- **Extensibility:** Enables custom algorithms for show cues or dynamic calculations without recompiling the source code.

### 6. Consoles & Hardware Interfaces (`Console`)
Supports direct integration with physical control surfaces via serial and generic I/O.
- **Supported Consoles:** Handlers for custom surfaces like `Starmania`, `StacatoCompact`, and `StacatoV2`.
- **Serial Communication:** Non-blocking ASIO-based serial port management for generic microcontrollers and hardware panels.

## Summary
Stacato bridges the gap between industrial automation (EtherCAT, DS402) and entertainment show control (OSC, Art-Net, PSN). Its architecture ensures that highly complex logic, safety constraints, and multi-axis kinematic animations can be designed visually, monitored in real-time via advanced plotting/dashboards, and executed deterministically on robust fieldbus hardware.
