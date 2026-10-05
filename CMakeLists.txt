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

# --- Add Dependencies (Submodules) ---
if(BREZEL_STANDALONE)
    # EnTT (Header only)
    add_subdirectory(extern/entt)

    # Spdlog (Fast logging)
    add_subdirectory(extern/spdlog)

    # TinyXML2
    add_subdirectory(extern/tinyxml2)
endif()

# --- Framework Library ---
if(BREZEL_STANDALONE)
    # Collect all source files
    file(GLOB_RECURSE SOURCES "src/*.cpp")
    add_executable(${PROJECT_NAME} ${SOURCES})
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
if(BREZEL_STANDALONE)
    target_include_directories(${PROJECT_NAME} PUBLIC 
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

if(BREZEL_STANDALONE)
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