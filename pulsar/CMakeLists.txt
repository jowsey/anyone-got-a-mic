cmake_minimum_required(VERSION 3.28...4.4)
project(pulsar VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(pulsar
        src/main.cpp
        src/env/BatchedEnvironment.cpp
        src/env/BatchedEnvironment.h
        src/env/components/Environment.h
        src/env/components/EnvironmentId.h
        src/env/components/MeshRenderer.h
        src/env/components/RigidBody.h
        src/env/components/Transform.h
        src/env/systems/Physics.cpp
        src/env/systems/Physics.h
        src/env/systems/Transform.cpp
        src/env/systems/Transform.h
        src/renderer/Primitives.h
        src/renderer/RenderWindow.cpp
        src/renderer/RenderWindow.h
        src/util/FormatConversion.cpp
        src/util/FormatConversion.h
        src/util/Fmt.cpp
        src/util/Fmt.h
)

target_compile_definitions(pulsar PRIVATE PULSAR_VERSION="${PROJECT_VERSION}")

# Release build optimizations
# avx2
if (CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if (MSVC)
        target_compile_options(pulsar PRIVATE "$<$<CONFIG:Release>:/arch:AVX2>")
    else ()
        target_compile_options(pulsar PRIVATE "$<$<CONFIG:Release>:-march=x86-64-v3>")
    endif ()
endif ()

# fast math
if (MSVC)
    add_compile_options("$<$<CONFIG:Release>:/fp:contract>")
else ()
    add_compile_options("$<$<CONFIG:Release>:-ffp-contract=fast;-fno-math-errno>")
endif ()

# dead code elimination
if (MSVC)
    add_compile_options("$<$<CONFIG:Release>:/Gy>")
    add_link_options("$<$<CONFIG:Release>:/OPT:REF>")
elseif (APPLE)
    add_link_options("$<$<CONFIG:Release>:-Wl,-dead_strip>")
else ()
    add_compile_options("$<$<CONFIG:Release>:-ffunction-sections;-fdata-sections>")
    add_link_options("$<$<CONFIG:Release>:-Wl,--gc-sections>")
endif ()

# Static linking
set(BUILD_SHARED_LIBS OFF)

if (MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT APPLE)
    if (MINGW)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    else ()
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libgcc -static-libstdc++")
    endif ()
endif ()

# link-time optimization
include(CheckIPOSupported)
check_ipo_supported(RESULT ipoSupported OUTPUT error)
if (ipoSupported)
    message(STATUS "LTO available!")
    set_target_properties(pulsar PROPERTIES INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
else ()
    message(STATUS "LTO unavailable: ${error}")
endif ()

# Strict warnings
set_target_properties(pulsar PROPERTIES COMPILE_WARNING_AS_ERROR ON)

if (MSVC)
    target_compile_options(pulsar PRIVATE /W4)
else ()
    target_compile_options(pulsar PRIVATE -Wall -Wextra -pedantic -Wno-missing-field-initializers)
endif ()

include(FetchContent)

# spdlog
FetchContent_Declare(
        spdlog
        URL "https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz"
        SYSTEM
)

FetchContent_MakeAvailable(spdlog)
target_link_libraries(pulsar PRIVATE spdlog::spdlog)

# glm
FetchContent_Declare(
        glm
        URL "https://github.com/g-truc/glm/archive/refs/tags/1.0.3.tar.gz"
        SYSTEM
)

FetchContent_MakeAvailable(glm)
target_compile_definitions(glm PUBLIC
        GLM_FORCE_DEPTH_ZERO_TO_ONE
        GLM_FORCE_LEFT_HANDED
)
target_link_libraries(pulsar PRIVATE glm)

# flecs
FetchContent_Declare(
        flecs
        URL "https://github.com/SanderMertens/flecs/archive/refs/tags/v4.2.0.tar.gz"
        SYSTEM
)

set(FLECS_SHARED OFF CACHE BOOL "" FORCE)
set(FLECS_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(flecs)
target_link_libraries(pulsar PRIVATE flecs::flecs_static)

# box3d
FetchContent_Declare(
        box3d
        URL "https://github.com/erincatto/box3d/archive/be8c7318901dbb14b9fbb26fe0a7a48850f53b0e.tar.gz"
        SYSTEM
)

FetchContent_MakeAvailable(box3d)
target_compile_definitions(box3d PUBLIC B3_MAX_WORLDS=4096)
target_link_libraries(pulsar PRIVATE box3d::box3d)

# CLI11
FetchContent_Declare(
        CLI11
        URL "https://github.com/CLIUtils/CLI11/releases/download/v2.7.2/CLI11-2.7.2-Source.tar.gz"
        SYSTEM
)

FetchContent_MakeAvailable(CLI11)
target_link_libraries(pulsar PRIVATE CLI11::CLI11)

# SDL3
FetchContent_Declare(
        SDL3
        URL "https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.20.tar.gz"
        SYSTEM
)

FetchContent_MakeAvailable(SDL3)
target_link_libraries(pulsar PRIVATE SDL3::SDL3)

# Shader compilation
# todo allow headless build
find_program(SHADERCROSS_EXE shadercross REQUIRED)

if (APPLE)
    set(SHADER_EXT ".msl")
elseif (WIN32 OR UNIX)
    set(SHADER_EXT ".spv")
endif ()

set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/shaders")
file(MAKE_DIRECTORY ${SHADER_OUT_DIR})

function(compile_hlsl SOURCE_FILE STAGE ENTRY_POINT OUTPUT_NAME)
    set(INPUT_PATH "${CMAKE_SOURCE_DIR}/${SOURCE_FILE}")
    set(OUTPUT_PATH "${SHADER_OUT_DIR}/${OUTPUT_NAME}${SHADER_EXT}")

    add_custom_command(
            OUTPUT "${OUTPUT_PATH}"
            COMMAND "${SHADERCROSS_EXE}" "${INPUT_PATH}"
            -t "${STAGE}"
            -e "${ENTRY_POINT}"
            -o "${OUTPUT_PATH}"
            DEPENDS "${INPUT_PATH}"
            COMMENT "Compiling ${SOURCE_FILE} (${STAGE}, ${ENTRY_POINT}) to ${OUTPUT_NAME}${SHADER_EXT}"
            VERBATIM
    )

    list(APPEND COMPILED_SHADERS "${OUTPUT_PATH}")
    set(COMPILED_SHADERS ${COMPILED_SHADERS} PARENT_SCOPE)
endfunction()

compile_hlsl("src/renderer/shaders/lit.hlsl" "vertex" "VSMain" "lit_vert")
compile_hlsl("src/renderer/shaders/lit.hlsl" "fragment" "PSMain" "lit_frag")

add_custom_target(compile_shaders ALL DEPENDS ${COMPILED_SHADERS})
add_dependencies(pulsar compile_shaders)

## nanobind
#set(Python_EXECUTABLE "${CMAKE_CURRENT_SOURCE_DIR}/../trainer/.venv/bin/python")
#find_package(Python 3.10 COMPONENTS Interpreter Development.Module REQUIRED)
#
#execute_process(
#        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
#        OUTPUT_VARIABLE nanobind_ROOT
#        OUTPUT_STRIP_TRAILING_WHITESPACE
#)
#find_package(nanobind CONFIG REQUIRED)
