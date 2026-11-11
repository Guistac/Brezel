# Brezel

Brezel is a header-only C++ application framework on top of the **EnTT** entity component system (ECS).

It gives an application an ECS world with identity, hierarchy, reflection and XML and binary serialization, and keeps
real-time loops apart from it: real-time code never touches the ECS and exchanges plain data with the rest of the
application only through wait-free channels.

## Architecture & Documentation

Components are plain structs. Reflection is a free function per component, not a virtual method. There are no
inheritance trees and no wrapper types around fields.

How to write components, change them from other threads, serialize them and pass data to a real-time loop:

**[docs/Framework_Guide.md](docs/Framework_Guide.md)**

## Requirements

- A C++20 compiler. `CMakeLists.txt` sets C++23 for a standalone build, but the headers are written and checked as
  C++20, the standard of the project that uses them.
- EnTT, spdlog and tinyxml2. They are git submodules in `extern/`:
  ```bash
  git submodule update --init --recursive
  ```
- GNU readline, only for `include/Brezel/Console/InteractiveConsole.hpp` and the standalone testbed.

## Using Brezel in a project

Add Brezel as a subdirectory of the parent CMake project. In that mode `CMakeLists.txt` leaves out `src/main.cpp`
and, with no other source file, makes `Brezel` an `INTERFACE` library (`CMakeLists.txt:32-42`). It adds `include/` to
the include path and links targets named `spdlog` and `tinyxml2` (`CMakeLists.txt:80-85`), which the parent must
define. It does not add EnTT: add EnTT's `src` folder yourself.

```cmake
add_subdirectory(deps/Brezel/extern/spdlog)
add_subdirectory(deps/Brezel/extern/tinyxml2)
add_subdirectory(deps/Brezel)
target_include_directories(Brezel INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/deps/Brezel/extern/entt/src)

target_link_libraries(my_app PRIVATE Brezel)
```

This was checked on 2026-11-11 with a small consumer built with CMake and Ninja. Seven headers need
`Brezel/Core/ComponentRegistry.hpp` included before them; the guide's header index (section 13) lists them.

## Standalone testbed (does not build today)

Built as the top-level project, `CMakeLists.txt` compiles `src/*.cpp` into an executable named `Brezel` and links
EnTT, spdlog, tinyxml2 and readline (`CMakeLists.txt:28-31`, `:61-72`). The commands are:

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja
cmake --build build
./build/Brezel
```

The build fails today: `src/main.cpp:2` includes `Brezel/Command/ValueChangeCommand.hpp`, which was never committed.
Until `src/main.cpp` is fixed, use Brezel as a subdirectory (above).

## Tests

Brezel has no test target yet. Its primitives are tested through the tests of the project that uses it (Stacato's
`tests/`).

## License

Brezel is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.
