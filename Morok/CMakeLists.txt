cmake_minimum_required(VERSION 3.25)
project(MorokAI VERSION 0.1.0 DESCRIPTION "Morok AI" LANGUAGES CXX)

option(MOROK_BUILD_TESTS "Build automated tests" ON)
option(MOROK_BUILD_APPS "Build user-facing applications" ON)
option(MOROK_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(MOROK_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(MOROK_BUILD_FUZZERS "Build libFuzzer parser and decoder targets (Clang only)" OFF)
option(MOROK_WARNINGS_AS_ERRORS "Treat warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(MorokCompilerOptions)
include(MorokSanitizers)

find_package(SQLite3 REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc)

add_library(morok_build_options INTERFACE)
morok_configure_compiler_options(morok_build_options)

add_library(morok_core STATIC
    src/common/build_info.cpp
    src/common/error.cpp
    src/common/ids.cpp
    src/common/model_version.cpp
    src/common/timestamp.cpp
    src/common/utf8_text.cpp
    src/lang/byte_tokenizer.cpp
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
target_link_libraries(morok_core PRIVATE morok_build_options SQLite::SQLite3 ICU::uc)
target_compile_definitions(morok_core PUBLIC MOROK_VERSION_STRING="${PROJECT_VERSION}")
morok_enable_sanitizers(morok_core)

# G1 is a private generation library; the public core contains only shared infrastructure.
add_library(morok_g1 STATIC
    Gerações/G1/shared/src/lang/language_subsystem.cpp
    Gerações/G1/shared/src/comm/communication_subsystem.cpp
    Gerações/G1/shared/src/learn/learning_subsystem.cpp
    Gerações/G1/shared/src/model/parameters/g1_parameters.cpp
    Gerações/G1/shared/src/model/g1_forward.cpp
    Gerações/G1/shared/src/model/g1_backend.cpp
    Gerações/G1/shared/src/model/learn/g1_bptt.cpp
    Gerações/G1/shared/src/model/learn/optimizer.cpp
    Gerações/G1/shared/src/model/learn/g1_training.cpp
    Gerações/G1/shared/src/model/learn/g1_checkpoint.cpp
    Gerações/G1/shared/src/model/learn/g1_benchmark.cpp
    Gerações/G1/shared/src/model/learn/g1_evaluation.cpp
    Gerações/G1/shared/src/model/learn/g1_generation.cpp
)
add_library(Morok::G1 ALIAS morok_g1)
target_compile_features(morok_g1 PUBLIC cxx_std_20)
target_include_directories(morok_g1 PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G1/shared/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g1 PUBLIC Morok::Core PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g1)

# G1 mantém somente Tsukuyomi Max como modelo selecionável.
add_library(morok_g1_tsukuyomimax STATIC Gerações/G1/TsukuyomiMax/src/profile.cpp)
add_library(Morok::G1::TsukuyomiMax ALIAS morok_g1_tsukuyomimax)
target_compile_features(morok_g1_tsukuyomimax PUBLIC cxx_std_20)
target_include_directories(morok_g1_tsukuyomimax PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G1/TsukuyomiMax/include>)
target_link_libraries(morok_g1_tsukuyomimax PUBLIC Morok::G1 PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g1_tsukuyomimax)

# G3 reinforcement learning is generation-owned and reuses the shared Core.
add_library(morok_g3_rl STATIC Gerações/G3/rl/src/reinforce.cpp)
add_library(Morok::G3::RL ALIAS morok_g3_rl)
target_compile_features(morok_g3_rl PUBLIC cxx_std_20)
target_include_directories(morok_g3_rl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/rl/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g3_rl PUBLIC Morok::Core PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g3_rl)

# G2 Tsukuyomi v0.1 is isolated from all G1 implementation sources.
add_library(morok_g2_tsukuyomi STATIC
    Gerações/G2/Tsukuyomi/src/model.cpp
)
add_library(Morok::G2::Tsukuyomi ALIAS morok_g2_tsukuyomi)
target_compile_features(morok_g2_tsukuyomi PUBLIC cxx_std_20)
target_include_directories(morok_g2_tsukuyomi PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G2/Tsukuyomi/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g2_tsukuyomi PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g2_tsukuyomi)
if(MOROK_BUILD_APPS)
    add_executable(morok_cli Gerações/G1/apps/cli/main.cpp)
    target_link_libraries(morok_cli PRIVATE Morok::G1 morok_build_options)
    set_target_properties(morok_cli PROPERTIES OUTPUT_NAME "morok-cli")
    morok_enable_sanitizers(morok_cli)

    add_executable(morok_convert apps/morok-convert/main.cpp)
    target_link_libraries(morok_convert PRIVATE Morok::Core morok_build_options)
    set_target_properties(morok_convert PROPERTIES OUTPUT_NAME "morok-convert")
    morok_enable_sanitizers(morok_convert)

    add_executable(morok_evaluate Gerações/G1/apps/evaluate/main.cpp)
    target_link_libraries(morok_evaluate PRIVATE Morok::G1 morok_build_options)
    set_target_properties(morok_evaluate PROPERTIES OUTPUT_NAME "morok-evaluate")
    morok_enable_sanitizers(morok_evaluate)

    add_executable(morok_train_cli Gerações/G1/apps/train.cpp)
    target_link_libraries(morok_train_cli PRIVATE Morok::G1 morok_build_options)
    set_target_properties(morok_train_cli PROPERTIES OUTPUT_NAME "morok-train")
    morok_enable_sanitizers(morok_train_cli)

    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_executable(morok_server Gerações/G1/apps/server/main.cpp)
        target_link_libraries(morok_server PRIVATE Morok::G1 morok_build_options)
        set_target_properties(morok_server PROPERTIES OUTPUT_NAME "morok-server")
        morok_enable_sanitizers(morok_server)
    endif()

endif()

if(MOROK_BUILD_APPS)

    add_executable(morok_g2_tsukuyomi_train Gerações/G2/Tsukuyomi/apps/train.cpp)
    target_link_libraries(morok_g2_tsukuyomi_train PRIVATE Morok::G2::Tsukuyomi morok_build_options)
    set_target_properties(morok_g2_tsukuyomi_train PROPERTIES OUTPUT_NAME "morok-g2-tsukuyomi-train")
    morok_enable_sanitizers(morok_g2_tsukuyomi_train)

    add_executable(morok_g2_tsukuyomi_generate Gerações/G2/Tsukuyomi/apps/generate.cpp)
    target_link_libraries(morok_g2_tsukuyomi_generate PRIVATE Morok::G2::Tsukuyomi morok_build_options)
    set_target_properties(morok_g2_tsukuyomi_generate PROPERTIES OUTPUT_NAME "morok-g2-tsukuyomi-generate")
    morok_enable_sanitizers(morok_g2_tsukuyomi_generate)

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
add_executable(morok_train Gerações/G1/apps/train.cpp)
target_link_libraries(morok_train PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_train)

add_executable(morok_benchmark Gerações/G1/apps/benchmark.cpp)
target_link_libraries(morok_benchmark PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_benchmark)

add_executable(morok_complete Gerações/G1/apps/complete.cpp)
target_link_libraries(morok_complete PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_complete)

add_executable(morok_generate Gerações/G1/apps/generate.cpp)
target_link_libraries(morok_generate PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_generate)

add_executable(morok_g1_profile_evaluate Gerações/G1/apps/evaluate_profiles.cpp)
target_link_libraries(morok_g1_profile_evaluate PRIVATE Morok::G1 morok_g1_tsukuyomimax morok_build_options)
set_target_properties(morok_g1_profile_evaluate PROPERTIES OUTPUT_NAME "morok-g1-profile-evaluate")
morok_enable_sanitizers(morok_g1_profile_evaluate)
if(MOROK_BUILD_TESTS)
    add_test(NAME g1_semantic_evaluator COMMAND morok_g1_profile_evaluate --self-test)
endif()


add_executable(morok_reinforce Gerações/G1/apps/reinforce.cpp)
target_link_libraries(morok_reinforce PRIVATE Morok::G1 morok_build_options)
set_target_properties(morok_reinforce PROPERTIES OUTPUT_NAME "morok-reinforce")
morok_enable_sanitizers(morok_reinforce)


endif()

if(MOROK_BUILD_APPS)
    install(TARGETS morok_cli morok_convert morok_evaluate morok_train_cli
        morok_train morok_benchmark morok_generate morok_reinforce morok_complete morok_g1_profile_evaluate
        morok_g2_tsukuyomi_train morok_g2_tsukuyomi_generate RUNTIME DESTINATION bin)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(TARGETS morok_server RUNTIME DESTINATION bin)
    endif()
    install(TARGETS
        morok_core
        morok_g1
        morok_g1_tsukuyomimax
        morok_g2_tsukuyomi
        ARCHIVE DESTINATION lib)
    install(DIRECTORY
        include/
        Gerações/G1/shared/include/
        Gerações/G1/TsukuyomiMax/include/
        Gerações/G2/Tsukuyomi/include/
        DESTINATION include)
endif()

if(MOROK_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "MOROK_BUILD_FUZZERS requires Clang/libFuzzer.")
    endif()
    target_compile_options(morok_core PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    target_compile_options(morok_g1 PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    target_compile_options(morok_g2_tsukuyomi PRIVATE
        -fsanitize=fuzzer-no-link,address,undefined -fno-omit-frame-pointer)
    foreach(_fuzz_target IN ITEMS utf8 rlah_decode)
        add_executable(morok_fuzz_${_fuzz_target} tests/fuzz/fuzz_${_fuzz_target}.cpp)
        target_link_libraries(morok_fuzz_${_fuzz_target} PRIVATE Morok::Core morok_build_options)
        target_compile_options(morok_fuzz_${_fuzz_target} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
        target_link_options(morok_fuzz_${_fuzz_target} PRIVATE
            -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    endforeach()
    add_executable(morok_fuzz_checkpoint Gerações/G1/shared/tests/fuzz/fuzz_checkpoint.cpp)
    target_link_libraries(morok_fuzz_checkpoint PRIVATE Morok::Core Morok::G1 morok_build_options)
    target_compile_options(morok_fuzz_checkpoint PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
    target_link_options(morok_fuzz_checkpoint PRIVATE
        -fsanitize=fuzzer,address,undefined -fno-omit-frame-pointer)
endif()
