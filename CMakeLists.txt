cmake_minimum_required(VERSION 3.25)
project(MorokAI VERSION 0.1.0 DESCRIPTION "Morok AI" LANGUAGES CXX)

option(MOROK_BUILD_TESTS "Build automated tests" ON)
option(MOROK_BUILD_G2 "Build the G2 model library, applications and tests" ON)
option(MOROK_BUILD_APPS "Build user-facing applications" ON)
option(MOROK_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MOROK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MOROK_BUILD_FUZZERS "Build libFuzzer parser and decoder targets (Clang only)" OFF)
option(MOROK_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MorokCompilerOptions)
include(MorokSanitizers)

find_package(SQLite3 REQUIRED)

add_library(morok_build_options INTERFACE)
morok_configure_compiler_options(morok_build_options)

add_library(morok_core STATIC
    src/common/build_info.cpp
    src/common/error.cpp
    src/common/ids.cpp
    src/common/model_version.cpp
    src/common/timestamp.cpp
    src/common/utf8_text.cpp
    src/memory/memory_store.cpp
    src/lang/rlah/segmenter.cpp
    src/reason/reasoning_subsystem.cpp
    src/tools/tool_subsystem.cpp
    src/security/security_subsystem.cpp
)
add_library(Morok::Core ALIAS morok_core)

target_compile_features(morok_core PUBLIC cxx_std_20)
target_include_directories(morok_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
target_link_libraries(morok_core PRIVATE morok_build_options SQLite::SQLite3)
target_compile_definitions(morok_core PUBLIC MOROK_VERSION_STRING="${PROJECT_VERSION}")
morok_enable_sanitizers(morok_core)

# G1 is a private generation library; the public core contains only shared infrastructure.
add_library(morok_g1 STATIC
    src/lang/language_subsystem.cpp
    src/comm/communication_subsystem.cpp
    src/learn/learning_subsystem.cpp
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
add_library(Morok::G1 ALIAS morok_g1)
target_compile_features(morok_g1 PUBLIC cxx_std_20)
target_include_directories(morok_g1 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g1 PUBLIC Morok::Core PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g1)

# G2 is an optional, separate library. G1-only builds do not compile G2 sources.
if(MOROK_BUILD_G2)
    add_library(morok_g2 STATIC
        src/model/parameters/g2_parameters.cpp
        src/model/g2_forward.cpp
        src/model/learn/g2_checkpoint.cpp
        src/model/learn/g2_training.cpp
        src/model/learn/g2_generation.cpp
        G2/engine/src/parameters.cpp
        G2/engine/src/forward.cpp
        G2/engine/src/bptt.cpp
        G2/engine/src/optimizer.cpp
        G2/engine/src/checkpoint.cpp
        G2/engine/src/training.cpp
        G2/engine/src/generation.cpp
    )
    add_library(Morok::G2 ALIAS morok_g2)
    target_compile_features(morok_g2 PUBLIC cxx_std_20)
    target_include_directories(morok_g2 PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/G2/engine/include>
        $<INSTALL_INTERFACE:include>)
    target_link_libraries(morok_g2 PUBLIC Morok::Core PRIVATE morok_build_options)
    morok_enable_sanitizers(morok_g2)
endif()

if(MOROK_BUILD_APPS)
    add_executable(morok_cli apps/morok-cli/main.cpp)
    target_link_libraries(morok_cli PRIVATE Morok::G1 morok_build_options)
    set_target_properties(morok_cli PROPERTIES OUTPUT_NAME "morok-cli")
    morok_enable_sanitizers(morok_cli)

    add_executable(morok_convert apps/morok-convert/main.cpp)
    target_link_libraries(morok_convert PRIVATE Morok::Core morok_build_options)
    set_target_properties(morok_convert PROPERTIES OUTPUT_NAME "morok-convert")
    morok_enable_sanitizers(morok_convert)

    add_executable(morok_evaluate apps/morok-evaluate/main.cpp)
    target_link_libraries(morok_evaluate PRIVATE Morok::G1 morok_build_options)
    set_target_properties(morok_evaluate PROPERTIES OUTPUT_NAME "morok-evaluate")
    morok_enable_sanitizers(morok_evaluate)

    add_executable(morok_train_cli apps/train_g1.cpp)
    target_link_libraries(morok_train_cli PRIVATE Morok::G1 morok_build_options)
    set_target_properties(morok_train_cli PROPERTIES OUTPUT_NAME "morok-train")
    morok_enable_sanitizers(morok_train_cli)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(morok_server apps/morok-server/main.cpp)
        target_link_libraries(morok_server PRIVATE Morok::G1 morok_build_options)
        set_target_properties(morok_server PROPERTIES OUTPUT_NAME "morok-server")
        morok_enable_sanitizers(morok_server)
    endif()

endif()

if(MOROK_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "Morok AI version: ${PROJECT_VERSION}")
message(STATUS "C++ standard: C++20")
message(STATUS "Tests: ${MOROK_BUILD_TESTS}")

if(MOROK_BUILD_APPS)
add_executable(morok_train apps/train_g1.cpp)
target_link_libraries(morok_train PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_train)

add_executable(morok_benchmark apps/benchmark_g1.cpp)
target_link_libraries(morok_benchmark PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_benchmark)

add_executable(morok_complete apps/complete_g1.cpp)
target_link_libraries(morok_complete PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_complete)

add_executable(morok_generate apps/generate_g1.cpp)
target_link_libraries(morok_generate PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_generate)

if(MOROK_BUILD_G2)
add_executable(morok_train_g2 apps/train_g2.cpp)
target_link_libraries(morok_train_g2 PRIVATE Morok::G2 morok_build_options)
set_target_properties(morok_train_g2 PROPERTIES OUTPUT_NAME "morok-train-g2")
morok_enable_sanitizers(morok_train_g2)

add_executable(morok_generate_g2 apps/generate_g2.cpp)
target_link_libraries(morok_generate_g2 PRIVATE Morok::G2 morok_build_options)
set_target_properties(morok_generate_g2 PROPERTIES OUTPUT_NAME "morok-generate-g2")
morok_enable_sanitizers(morok_generate_g2)
endif()

add_executable(morok_reinforce apps/reinforce_g1.cpp)
target_link_libraries(morok_reinforce PRIVATE Morok::G1 morok_build_options)
set_target_properties(morok_reinforce PROPERTIES OUTPUT_NAME "morok-reinforce")
morok_enable_sanitizers(morok_reinforce)

if(MOROK_BUILD_G2)
add_executable(morok_reinforce_g2 apps/reinforce_g2.cpp)
target_link_libraries(morok_reinforce_g2 PRIVATE Morok::G2 morok_build_options)
set_target_properties(morok_reinforce_g2 PROPERTIES OUTPUT_NAME "morok-reinforce-g2")
morok_enable_sanitizers(morok_reinforce_g2)
endif()
endif()

if(MOROK_BUILD_APPS)
    install(TARGETS morok_cli morok_convert morok_evaluate morok_train_cli
        morok_train morok_benchmark morok_generate morok_reinforce morok_complete
        RUNTIME DESTINATION bin)
    if(MOROK_BUILD_G2)
        install(TARGETS morok_g2 morok_train_g2 morok_generate_g2 morok_reinforce_g2
            ARCHIVE DESTINATION lib RUNTIME DESTINATION bin)
    endif()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(TARGETS morok_server RUNTIME DESTINATION bin)
    endif()
    install(TARGETS morok_core morok_g1 ARCHIVE DESTINATION lib)
    install(DIRECTORY include/ DESTINATION include)
endif()

if(MOROK_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MOROK_BUILD_FUZZERS requires Clang/libFuzzer.")
    endif()
    target_compile_options(morok_core PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    target_compile_options(morok_g1 PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    foreach(_fuzz_target IN ITEMS utf8 rlah_decode checkpoint)
        add_executable(morok_fuzz_${_fuzz_target} tests/fuzz/fuzz_${_fuzz_target}.cpp)
        target_link_libraries(morok_fuzz_${_fuzz_target} PRIVATE Morok::Core morok_build_options)
        if(_fuzz_target STREQUAL "checkpoint")
            target_link_libraries(morok_fuzz_${_fuzz_target} PRIVATE Morok::G1)
        endif()
        target_compile_options(morok_fuzz_${_fuzz_target} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(morok_fuzz_${_fuzz_target} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    endforeach()
endif()
