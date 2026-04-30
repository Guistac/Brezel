cmake_minimum_required(VERSION 3.20)
project(Brezel LANGUAGES CXX)

# --- Global Settings ---
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Add Dependencies (Submodules) ---
# EnTT (Header only)
add_subdirectory(extern/entt)

# Spdlog (Fast logging)
add_subdirectory(extern/spdlog)

# Pugixml
add_subdirectory(extern/pugixml)

# --- Framework Library ---
# Collect all source files
file(GLOB_RECURSE SOURCES "src/*.cpp")

add_executable(${PROJECT_NAME} ${SOURCES})

# --- Include Paths ---
target_include_directories(${PROJECT_NAME} PUBLIC 
    include
    extern/entt/src
    extern/pugixml/src
)

# --- Link Libraries ---
target_link_libraries(${PROJECT_NAME} PRIVATE 
    EnTT
    spdlog::spdlog
    pugixml
)