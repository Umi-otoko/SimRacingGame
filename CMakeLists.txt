cmake_minimum_required(VERSION 3.22)
project(SimRacingGame VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# =============================================================================
# Opciones de build
# =============================================================================

option(SIMRACING_BUILD_SERVER   "Build dedicated server"       ON)
option(SIMRACING_BUILD_CLIENT   "Build client"                 ON)
option(SIMRACING_BUILD_TESTS    "Build unit tests"             OFF)
option(SIMRACING_ENABLE_ASAN    "Enable Address Sanitizer"     OFF)

# =============================================================================
# Dependencias via vcpkg (requiere -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/buildsystems/vcpkg.cmake)
# =============================================================================

find_package(OpenSSL   REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog    REQUIRED)
find_package(ZLIB      REQUIRED)

# =============================================================================
# JoltPhysics (incluido como extern)
# =============================================================================

set(JOLT_DIR "${CMAKE_SOURCE_DIR}/extern/JoltPhysics" CACHE PATH "JoltPhysics root")

if(EXISTS "${JOLT_DIR}/CMakeLists.txt")
    set(DOUBLE_PRECISION OFF CACHE BOOL "" FORCE)
    set(GENERATE_DEBUG_SYMBOLS ON CACHE BOOL "" FORCE)
    set(CROSS_PLATFORM_DETERMINISTIC ON CACHE BOOL "" FORCE)  # Crítico para netcode
    set(INTERPROCEDURAL_OPTIMIZATION ON CACHE BOOL "" FORCE)
    add_subdirectory("${JOLT_DIR}/Build" jolt_build)
else()
    message(WARNING "JoltPhysics no encontrado en ${JOLT_DIR}. Ejecuta: scripts/setup_environment.ps1")
endif()

# =============================================================================
# Librería Core (compartida entre cliente y servidor)
# =============================================================================

add_library(SimRacingCore STATIC
    src/Core/Setup/VehicleSetup.cpp
)

target_include_directories(SimRacingCore PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${JOLT_DIR}
)

target_link_libraries(SimRacingCore PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    spdlog::spdlog
    ZLIB::ZLIB
)

if(TARGET Jolt)
    target_link_libraries(SimRacingCore PUBLIC Jolt)
endif()

# Optimizaciones de compilación para física (SIMD)
if(MSVC)
    target_compile_options(SimRacingCore PRIVATE
        /O2 /Oi /Ot /GL /arch:AVX2
        /W4 /WX
    )
    target_compile_definitions(SimRacingCore PRIVATE
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(SimRacingCore PRIVATE
        -O3 -march=native -ffast-math
        -Wall -Wextra -Werror
    )
endif()

if(SIMRACING_ENABLE_ASAN)
    target_compile_options(SimRacingCore PUBLIC -fsanitize=address)
    target_link_options(SimRacingCore PUBLIC -fsanitize=address)
endif()

# =============================================================================
# Ejecutable del Servidor Dedicado
# =============================================================================

if(SIMRACING_BUILD_SERVER)
    add_executable(SimRacingServer
        src/Server/main_server.cpp  # Por implementar
    )
    target_link_libraries(SimRacingServer PRIVATE SimRacingCore)
    set_target_properties(SimRacingServer PROPERTIES
        OUTPUT_NAME "simracing-server"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
endif()

# =============================================================================
# Tests (Google Test via vcpkg)
# =============================================================================

if(SIMRACING_BUILD_TESTS)
    find_package(GTest REQUIRED)
    enable_testing()

    add_executable(SimRacingTests
        tests/test_vehicle_setup.cpp
        tests/test_tire_model.cpp
        tests/test_client_prediction.cpp
    )
    target_link_libraries(SimRacingTests PRIVATE
        SimRacingCore
        GTest::gtest_main
    )
    include(GoogleTest)
    gtest_discover_tests(SimRacingTests)
endif()

# =============================================================================
# Info de build
# =============================================================================

message(STATUS "")
message(STATUS "=== SimRacingGame Build Configuration ===")
message(STATUS "  Version:        ${PROJECT_VERSION}")
message(STATUS "  Build type:     ${CMAKE_BUILD_TYPE}")
message(STATUS "  C++ standard:   ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build server:   ${SIMRACING_BUILD_SERVER}")
message(STATUS "  Build client:   ${SIMRACING_BUILD_CLIENT}")
message(STATUS "  Build tests:    ${SIMRACING_BUILD_TESTS}")
message(STATUS "  Jolt Physics:   ${JOLT_DIR}")
message(STATUS "=========================================")
message(STATUS "")
