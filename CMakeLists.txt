cmake_minimum_required(VERSION 3.20)
project(Brezel LANGUAGES CXX)

# --- Global Settings ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check if Brezel is built as a top-level project
if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME)
    set(BREZEL_STANDALONE ON)
else()
    set(BREZEL_STANDALONE OFF)
endif()

# --- Options ---
# Brezel's own tests (tests/, README.md "Tests"). On for a standalone build; off when Brezel is a subdirectory unless
# the parent sets BREZEL_BUILD_TESTS before add_subdirectory (policy CMP0077 lets the parent's variable win).
option(BREZEL_BUILD_TESTS "Build Brezel's tests (tests/) and register them with CTest" ${BREZEL_STANDALONE})
# The standalone testbed src/main.cpp does not compile (src/main.cpp:2 includes a header that was never committed).
# Off, so that a standalone build of the tests is not stopped by it; with it off, Brezel is an INTERFACE library.
option(BREZEL_BUILD_TESTBED "Standalone build only: build the testbed executable from src/*.cpp" OFF)

# --- Add Dependencies (Submodules) ---
if(BREZEL_STANDALONE)
    # EnTT (Header only)
    add_subdirectory(extern/entt)

    # Spdlog (Fast logging)
    add_subdirectory(extern/spdlog)

    # TinyXML2 (its own xmltest stays out of Brezel's CTest: tinyxml2 includes CTest, which turns BUILD_TESTING on)
    set(tinyxml2_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    add_subdirectory(extern/tinyxml2)
endif()

# --- Framework Library ---
if(BREZEL_STANDALONE AND BREZEL_BUILD_TESTBED)
    # Collect all source files
    file(GLOB_RECURSE SOURCES "src/*.cpp")
    add_executable(${PROJECT_NAME} ${SOURCES})
elseif(BREZEL_STANDALONE)
    # Standalone without the testbed: the header-only framework
    add_library(${PROJECT_NAME} INTERFACE)
else()
    # When built as a submodule in Stacato, it might be header-only
    file(GLOB_RECURSE SOURCES "src/*.cpp")
    list(REMOVE_ITEM SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

    if(SOURCES)
        add_library(${PROJECT_NAME} STATIC ${SOURCES})
    else()
        add_library(${PROJECT_NAME} INTERFACE)
    endif()
endif()

# --- Include Paths ---
if(BREZEL_STANDALONE AND BREZEL_BUILD_TESTBED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
elseif(BREZEL_STANDALONE)
    target_include_directories(${PROJECT_NAME} INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    )
else()
    if(SOURCES)
        target_include_directories(${PROJECT_NAME} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
    else()
        target_include_directories(${PROJECT_NAME} INTERFACE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
        )
    endif()
endif()

if(BREZEL_STANDALONE AND BREZEL_BUILD_TESTBED)
    target_include_directories(${PROJECT_NAME} PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/entt/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern/tinyxml2
    )
    # --- Link Libraries ---
    target_link_libraries(${PROJECT_NAME} PRIVATE
        EnTT
        spdlog::spdlog
        tinyxml2
        readline
    )
elseif(BREZEL_STANDALONE)
    # --- Link Libraries --- (standalone INTERFACE library: EnTT, spdlog and tinyxml2 from extern/)
    target_link_libraries(${PROJECT_NAME} INTERFACE
        EnTT
        spdlog::spdlog
        tinyxml2
    )
else()
    # --- Link Libraries ---
    if(SOURCES)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            spdlog
            tinyxml2
        )
    else()
        target_link_libraries(${PROJECT_NAME} INTERFACE
            spdlog
            tinyxml2
        )
    endif()
endif()

# --- Tests ---
if(BREZEL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
