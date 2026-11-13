cmake_minimum_required(VERSION 3.25)
project(MorokAI VERSION 0.1.0 DESCRIPTION "Morok AI" LANGUAGES CXX)

option(MOROK_BUILD_TESTS "Build automated tests" ON)
option(MOROK_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MOROK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MOROK_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MorokCompilerOptions)
include(MorokSanitizers)

add_library(morok_build_options INTERFACE)
morok_configure_compiler_options(morok_build_options)

add_library(morok_core STATIC
    src/common/build_info.cpp
    src/common/error.cpp
    src/common/ids.cpp
    src/common/model_version.cpp
    src/common/timestamp.cpp
    src/common/utf8_text.cpp
    src/model/parameters/g1_parameters.cpp
    src/model/g1_forward.cpp
    src/model/learn/g1_bptt.cpp
    src/model/learn/optimizer.cpp
    src/model/learn/g1_training.cpp
    src/model/learn/g1_checkpoint.cpp
    src/model/learn/g1_benchmark.cpp
    src/model/learn/g1_evaluation.cpp
    src/model/learn/g1_generation.cpp
)
add_library(Morok::Core ALIAS morok_core)

target_compile_features(morok_core PUBLIC cxx_std_20)
target_include_directories(morok_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(morok_core PRIVATE morok_build_options)
target_compile_definitions(morok_core PUBLIC MOROK_VERSION_STRING="${PROJECT_VERSION}")
morok_enable_sanitizers(morok_core)

if(MOROK_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Morok AI version: ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++20")
message(STATUS "Tests: ${MOROK_BUILD_TESTS}")

add_executable(morok_train apps/train_g1.cpp)
target_link_libraries(morok_train PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_train)

add_executable(morok_benchmark apps/benchmark_g1.cpp)
target_link_libraries(morok_benchmark PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_benchmark)

add_executable(morok_complete apps/complete_g1.cpp)
target_link_libraries(morok_complete PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_complete)

add_executable(morok_generate apps/generate_g1.cpp)
target_link_libraries(morok_generate PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_generate)
