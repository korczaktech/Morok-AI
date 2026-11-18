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
find_package(Threads REQUIRED)

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
add_library(morok_g3_rl STATIC Gerações/G3/rl/src/reinforce.cpp Gerações/G3/rl/src/g3_policy.cpp
    Gerações/G3/rl/src/reference_evaluator.cpp)
add_library(Morok::G3::RL ALIAS morok_g3_rl)
target_compile_features(morok_g3_rl PUBLIC cxx_std_20)
target_include_directories(morok_g3_rl PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/rl/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g3_rl PUBLIC Morok::Core PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g3_rl)

# G3 neural architecture is generation-owned and reuses the shared Core.
add_library(morok_g3_neural STATIC Gerações/G3/neural/src/gru_backend.cpp)
add_library(Morok::G3::Neural ALIAS morok_g3_neural)
target_compile_features(morok_g3_neural PUBLIC cxx_std_20)
target_include_directories(morok_g3_neural PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/neural/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g3_neural PUBLIC Morok::Core PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g3_neural)
target_link_libraries(morok_g3_rl PUBLIC Morok::G3::Neural)

# G3 supervised training extends the generation-owned neural backend.
add_library(morok_g3_training STATIC
    Gerações/G3/training/src/dataset.cpp
    Gerações/G3/training/src/trainer.cpp
    Gerações/G3/training/src/checkpoint.cpp
    Gerações/G3/training/src/evaluation.cpp
)
add_library(Morok::G3::Training ALIAS morok_g3_training)
target_compile_features(morok_g3_training PUBLIC cxx_std_20)
target_include_directories(morok_g3_training PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/training/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g3_training
    PUBLIC Morok::G3::Neural Morok::Core
    PRIVATE ICU::uc morok_build_options)
morok_enable_sanitizers(morok_g3_training)
target_include_directories(morok_g3_training PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/rl/include>)
target_link_libraries(morok_g3_rl PUBLIC Morok::G3::Training)

# G3 inference/export utilities reuse the public Morok Core contracts.
add_library(morok_g3_inference STATIC
    Gerações/G3/inference/src/quantization.cpp
    Gerações/G3/inference/src/model_artifact.cpp
    Gerações/G3/inference/src/generator.cpp
    Gerações/G3/inference/src/session_manager.cpp
    Gerações/G3/inference/src/int8_backend.cpp
    Gerações/G3/inference/src/benchmark.cpp
)
add_library(Morok::G3::Inference ALIAS morok_g3_inference)
target_compile_features(morok_g3_inference PUBLIC cxx_std_20)
target_include_directories(morok_g3_inference PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/inference/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g3_inference PUBLIC Morok::Core Morok::G3::Neural PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g3_inference)

# G3 evaluation reads the validated FP32 artifact format from inference.
target_include_directories(morok_g3_training PRIVATE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/inference/include>)
target_link_libraries(morok_g3_training PRIVATE Morok::G3::Inference)

# G3 profiles are policies over the same inference engine and backend.
add_library(morok_g3_profiles STATIC Gerações/G3/profiles/src/profile.cpp)
add_library(Morok::G3::Profiles ALIAS morok_g3_profiles)
target_compile_features(morok_g3_profiles PUBLIC cxx_std_20)
target_include_directories(morok_g3_profiles PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/Gerações/G3/profiles/include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)
target_link_libraries(morok_g3_profiles PUBLIC Morok::G3::Inference PRIVATE morok_build_options)
morok_enable_sanitizers(morok_g3_profiles)

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

    add_executable(morok_g3_train Gerações/G3/training/apps/train.cpp)
    target_link_libraries(morok_g3_train PRIVATE Morok::G3::Training Morok::G3::Inference morok_build_options)
    set_target_properties(morok_g3_train PROPERTIES OUTPUT_NAME "morok-g3-train")
    morok_enable_sanitizers(morok_g3_train)

    add_executable(morok_g3_evaluate Gerações/G3/training/apps/evaluate.cpp)
    target_link_libraries(morok_g3_evaluate PRIVATE Morok::G3::Training Morok::G3::Inference morok_build_options)
    set_target_properties(morok_g3_evaluate PROPERTIES OUTPUT_NAME "morok-g3-evaluate")
    morok_enable_sanitizers(morok_g3_evaluate)

    add_executable(morok_g3_analyze Gerações/G3/training/apps/analyze_answers.cpp)
    target_link_libraries(morok_g3_analyze PRIVATE Morok::G3::Training Morok::G3::Inference Morok::G3::Neural morok_build_options)
    set_target_properties(morok_g3_analyze PROPERTIES OUTPUT_NAME "morok-g3-analyze")
    morok_enable_sanitizers(morok_g3_analyze)

    add_executable(morok_g3_reinforce Gerações/G3/rl/apps/reinforce.cpp)
    target_link_libraries(morok_g3_reinforce PRIVATE Morok::G3::RL Morok::G3::Training Morok::G3::Inference morok_build_options)
    set_target_properties(morok_g3_reinforce PROPERTIES OUTPUT_NAME "morok-g3-reinforce")
    morok_enable_sanitizers(morok_g3_reinforce)

    add_executable(morok_g3_generate Gerações/G3/inference/apps/generate.cpp)
    target_link_libraries(morok_g3_generate PRIVATE Morok::G3::Inference Morok::G3::Neural Morok::G3::Training Morok::G3::Profiles morok_build_options)
    set_target_properties(morok_g3_generate PROPERTIES OUTPUT_NAME "morok-g3-generate")
    morok_enable_sanitizers(morok_g3_generate)

    add_executable(morok_g3_quantize Gerações/G3/inference/apps/quantize.cpp)
    target_link_libraries(morok_g3_quantize PRIVATE Morok::G3::Inference Morok::G3::Neural Morok::G3::Training morok_build_options)
    set_target_properties(morok_g3_quantize PROPERTIES OUTPUT_NAME "morok-g3-quantize")
    morok_enable_sanitizers(morok_g3_quantize)

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
        morok_g2_tsukuyomi_train morok_g2_tsukuyomi_generate morok_g3_train morok_g3_evaluate morok_g3_analyze morok_g3_reinforce morok_g3_generate morok_g3_quantize RUNTIME DESTINATION bin)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        install(TARGETS morok_server RUNTIME DESTINATION bin)
    endif()
    install(TARGETS
        morok_core
        morok_g1
        morok_g1_tsukuyomimax
        morok_g2_tsukuyomi
        morok_g3_rl
        morok_g3_neural
        morok_g3_training
        morok_g3_inference
        ARCHIVE DESTINATION lib)
    install(DIRECTORY
        include/
        Gerações/G1/shared/include/
        Gerações/G1/TsukuyomiMax/include/
        Gerações/G2/Tsukuyomi/include/
        Gerações/G3/rl/include/
        Gerações/G3/neural/include/
        Gerações/G3/training/include/
        Gerações/G3/inference/include/
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
