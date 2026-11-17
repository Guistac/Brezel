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

Brezel's tests are in `tests/`: seven programs, one per area (core, reflection, mutations, serialization,
concurrency, ownership, the rest), 101 tests in all, and a header check. Each program is built twice, as C++20 and as
C++23, and registered with CTest as `brezel_<area>_cxx20` and `brezel_<area>_cxx23`; the header check is
`brezel_headers_cxx20`. They need nothing beyond Brezel's own dependencies and the threads library: the small test
framework is `tests/BrezelTest.hpp`.

Standalone, from this folder:

```bash
git submodule update --init --recursive
cmake -S . -B build -G Ninja -DBREZEL_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

`BREZEL_BUILD_TESTS` is on by default in a standalone build. Since the tests were added, a standalone build makes the
testbed executable only with `-DBREZEL_BUILD_TESTBED=ON` (off by default, because `src/main.cpp` does not compile);
without it, `Brezel` is an `INTERFACE` library there too.

Inside a parent project, set the option before adding the folder; the tests then run in the parent's `ctest`
(Stacato does this in `cmake/Dependencies/brezel.cmake`):

```cmake
set(BREZEL_BUILD_TESTS ON)
add_subdirectory(deps/Brezel)
```

One program can be run alone; its first argument filters the tests by name (`./build/tests/brezel_core_cxx20 wall`).
A test that pins a known defect of today's code is named `known_defect_<row>`: it passes while the defect is there and
fails when the code is fixed, so the fix changes the test with it. What the tests cover, what they do not, and how to
add one: [docs/Framework_Guide.md](docs/Framework_Guide.md) section 14.

## License

Brezel is licensed under the MIT License - see the [LICENSE.md](LICENSE.md) file for details.
