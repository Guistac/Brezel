# Brezel

Brezel is a high-performance, C++23 real-time (RT) application framework built heavily on top of the **EnTT** Entity Component System (ECS). 

It is designed to cleanly separate Non-Real-Time (NRT) systems (like UIs, serialization, and networking) from hard Real-Time execution loops (like 1kHz motion planning) using a strict "Air-Gap" architecture.

## Architecture & Documentation

The architecture of Brezel relies strictly on **Pure POD Components** and **Static Free-Function Reflection**. There are no heavy inheritance trees, no virtual methods on components, and no intrusive `Parameter<T>` wrappers.

For detailed instructions on how to use Brezel, write components, trigger UI updates, and respect the RT constraints, please read the primary documentation:

👉 **[docs/Framework_Guide.md](docs/Framework_Guide.md)**

## Build Instructions

```bash
git submodule update --init --recursive
cmake -S . -B .
make
./Brezel
```