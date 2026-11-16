# Keep assert-based test checks active in optimized Release test binaries.
add_compile_options($<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>>:-UNDEBUG>)

add_executable(morok_test_build_info
    unit/common/test_build_info.cpp
)
target_link_libraries(morok_test_build_info PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_build_info)
add_test(NAME common.build_info COMMAND morok_test_build_info)

add_executable(morok_test_shared_types
    unit/common/test_shared_types.cpp
)
target_link_libraries(morok_test_shared_types PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_shared_types)
add_test(NAME common.shared_types COMMAND morok_test_shared_types)

add_executable(morok_test_byte_tokenizer
    unit/lang/test_byte_tokenizer.cpp
)
target_link_libraries(morok_test_byte_tokenizer PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_byte_tokenizer)
add_test(NAME lang.byte_tokenizer COMMAND morok_test_byte_tokenizer)

add_executable(morok_test_g3_reinforcement
    ../Gerações/G3/rl/tests/test_reinforce.cpp)
target_link_libraries(morok_test_g3_reinforcement
    PRIVATE Morok::G3::RL Morok::G3::Neural morok_build_options)
morok_enable_sanitizers(morok_test_g3_reinforcement)
add_test(NAME g3.reinforcement COMMAND morok_test_g3_reinforcement)
set_tests_properties(g3.reinforcement PROPERTIES LABELS "g3;reinforcement;policy" TIMEOUT 180)

add_executable(morok_test_math_primitives
    unit/model/test_math_primitives.cpp
)
target_link_libraries(morok_test_math_primitives PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_math_primitives)
add_test(NAME model.math_primitives COMMAND morok_test_math_primitives)



add_executable(morok_test_g1_parameters
    ../Gerações/G1/shared/tests/unit/model/test_g1_parameters.cpp
)
target_link_libraries(morok_test_g1_parameters PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_parameters)
add_test(NAME model.g1_parameters COMMAND morok_test_g1_parameters)

add_executable(morok_test_g1_forward
    ../Gerações/G1/shared/tests/unit/model/test_g1_forward.cpp
)
target_link_libraries(morok_test_g1_forward PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_forward)
add_test(NAME model.g1_forward COMMAND morok_test_g1_forward)

add_executable(morok_test_g1_backend
    ../Gerações/G1/shared/tests/unit/model/test_g1_backend.cpp
)
target_link_libraries(morok_test_g1_backend PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_backend)
add_test(NAME model.g1_backend COMMAND morok_test_g1_backend)

add_executable(morok_test_model_session unit/model/test_model_session.cpp)
target_link_libraries(morok_test_model_session PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_model_session)
add_test(NAME model.session COMMAND morok_test_model_session)

add_executable(morok_test_g1_bptt
    ../Gerações/G1/shared/tests/unit/model/test_g1_bptt.cpp
)
target_link_libraries(morok_test_g1_bptt PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_bptt)
add_test(NAME model.g1_bptt COMMAND morok_test_g1_bptt)

add_executable(morok_test_g1_reinforcement ../Gerações/G1/shared/tests/unit/model/test_g1_reinforcement.cpp)
target_link_libraries(morok_test_g1_reinforcement PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_reinforcement)
add_test(NAME model.g1_reinforcement COMMAND morok_test_g1_reinforcement)

add_executable(morok_test_g1_optimizer
    ../Gerações/G1/shared/tests/unit/model/test_g1_optimizer.cpp
)
target_link_libraries(morok_test_g1_optimizer PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_optimizer)
add_test(NAME model.g1_optimizer COMMAND morok_test_g1_optimizer)

add_executable(morok_test_g1_training ../Gerações/G1/shared/tests/unit/model/test_g1_training.cpp)
target_link_libraries(morok_test_g1_training PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_training)
add_test(NAME model.g1_training COMMAND morok_test_g1_training)

add_executable(morok_test_g1_checkpoint ../Gerações/G1/shared/tests/unit/model/test_g1_checkpoint.cpp)
target_link_libraries(morok_test_g1_checkpoint PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_checkpoint)
add_test(NAME model.g1_checkpoint COMMAND morok_test_g1_checkpoint)

add_executable(morok_test_g1_benchmark ../Gerações/G1/shared/tests/unit/model/test_g1_benchmark.cpp)
target_link_libraries(morok_test_g1_benchmark PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_benchmark)
add_test(NAME model.g1_benchmark COMMAND morok_test_g1_benchmark)

add_executable(morok_test_g1_generation ../Gerações/G1/shared/tests/unit/model/test_g1_generation.cpp)
target_link_libraries(morok_test_g1_generation PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_generation)
add_test(NAME model.g1_generation COMMAND morok_test_g1_generation)

add_executable(morok_test_g1_evaluation ../Gerações/G1/shared/tests/unit/model/test_g1_evaluation.cpp)
target_link_libraries(morok_test_g1_evaluation PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_g1_evaluation)
add_test(NAME model.g1_evaluation COMMAND morok_test_g1_evaluation)

add_executable(morok_test_runtime_contracts unit/core/test_runtime_contracts.cpp)
target_link_libraries(morok_test_runtime_contracts PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_runtime_contracts)
add_test(NAME core.runtime_contracts COMMAND morok_test_runtime_contracts)

add_executable(morok_test_memory_store unit/memory/test_memory_store.cpp)
target_link_libraries(morok_test_memory_store PRIVATE Morok::Core morok_build_options SQLite::SQLite3)
morok_enable_sanitizers(morok_test_memory_store)
add_test(NAME memory.store COMMAND morok_test_memory_store)

add_executable(morok_test_language_subsystem ../Gerações/G1/shared/tests/unit/lang/test_language_subsystem.cpp)
target_link_libraries(morok_test_language_subsystem PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_language_subsystem)
add_test(NAME lang.language_subsystem COMMAND morok_test_language_subsystem)

add_executable(morok_test_communication_subsystem
    ../Gerações/G1/shared/tests/unit/comm/test_communication_subsystem.cpp
)
target_link_libraries(morok_test_communication_subsystem PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_communication_subsystem)
add_test(NAME comm.communication_subsystem COMMAND morok_test_communication_subsystem)

add_executable(morok_test_reasoning_subsystem
    unit/reason/test_reasoning_subsystem.cpp
)
target_link_libraries(morok_test_reasoning_subsystem PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_reasoning_subsystem)
add_test(NAME reason.reasoning_subsystem COMMAND morok_test_reasoning_subsystem)

add_executable(morok_test_learning_subsystem ../Gerações/G1/shared/tests/unit/learn/test_learning_subsystem.cpp)
target_link_libraries(morok_test_learning_subsystem PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_learning_subsystem)
add_test(NAME learn.learning_subsystem COMMAND morok_test_learning_subsystem)

add_executable(morok_test_rlah_segmenter unit/lang/test_rlah_segmenter.cpp)
target_link_libraries(morok_test_rlah_segmenter PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_rlah_segmenter)
add_test(NAME lang.rlah_segmenter COMMAND morok_test_rlah_segmenter)

add_executable(morok_test_tool_subsystem unit/tools/test_tool_subsystem.cpp)
target_link_libraries(morok_test_tool_subsystem PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_tool_subsystem)
add_test(NAME tools.tool_subsystem COMMAND morok_test_tool_subsystem)

add_executable(morok_test_security_subsystem unit/security/test_security_subsystem.cpp)
target_link_libraries(morok_test_security_subsystem PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_security_subsystem)
add_test(NAME security.security_subsystem COMMAND morok_test_security_subsystem)

if(MOROK_BUILD_APPS)
    add_executable(morok_test_application_runtime ../Gerações/G1/shared/tests/unit/apps/test_application_runtime.cpp)
    target_link_libraries(morok_test_application_runtime PRIVATE Morok::G1 morok_build_options)
    morok_enable_sanitizers(morok_test_application_runtime)
    add_test(NAME apps.application_runtime COMMAND morok_test_application_runtime)

    add_test(NAME apps.cli_help COMMAND morok_cli --help)
    add_test(NAME apps.cli_version COMMAND morok_cli version)
    add_test(NAME apps.cli_unknown_command
        COMMAND morok_cli command-that-does-not-exist)
    set_tests_properties(apps.cli_unknown_command PROPERTIES WILL_FAIL TRUE)
    add_test(NAME apps.convert_usage COMMAND morok_convert)
    add_test(NAME apps.evaluate_usage COMMAND morok_evaluate)
    add_test(NAME apps.train_usage COMMAND morok_train_cli)
    add_test(NAME apps.reinforce_usage COMMAND morok_reinforce)
    set(_morok_usage_tests apps.convert_usage apps.evaluate_usage apps.train_usage apps.reinforce_usage)
    set_tests_properties(${_morok_usage_tests} PROPERTIES WILL_FAIL TRUE)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        add_test(NAME apps.server_usage COMMAND morok_server)
        set_tests_properties(apps.server_usage PROPERTIES WILL_FAIL TRUE)
    endif()
endif()

# Stage 15: property, integration and regression coverage.
add_executable(morok_test_model_properties ../Gerações/G1/shared/tests/property/test_model_properties.cpp)
target_link_libraries(morok_test_model_properties PRIVATE Morok::G1 morok_build_options)
morok_enable_sanitizers(morok_test_model_properties)
add_test(NAME property.model_invariants COMMAND morok_test_model_properties)
set_tests_properties(property.model_invariants PROPERTIES LABELS "property;fast" TIMEOUT 30)

add_executable(morok_test_training_generation_pipeline
    ../Gerações/G1/shared/tests/integration/model_training_generation_pipeline.cpp)
target_link_libraries(morok_test_training_generation_pipeline PRIVATE Morok::G1 morok_build_options)
target_compile_definitions(morok_test_training_generation_pipeline PRIVATE
    MOROK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Gerações/G1/shared")
morok_enable_sanitizers(morok_test_training_generation_pipeline)
add_test(NAME integration.training_generation_pipeline
    COMMAND morok_test_training_generation_pipeline)
set_tests_properties(integration.training_generation_pipeline
    PROPERTIES LABELS "integration;model" TIMEOUT 180)

add_executable(morok_test_runtime_rollback regression/test_runtime_rollback.cpp)
target_link_libraries(morok_test_runtime_rollback PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_runtime_rollback)
add_test(NAME regression.runtime_rollback COMMAND morok_test_runtime_rollback)
set_tests_properties(regression.runtime_rollback PROPERTIES LABELS "regression;core" TIMEOUT 30)

# Stage 19: acceptance coverage across the G1 artifact flow and seven subsystems.
add_executable(morok_acceptance_g1 ../Gerações/G1/shared/tests/acceptance/test_g1_acceptance.cpp)
target_link_libraries(morok_acceptance_g1 PRIVATE Morok::G1 morok_build_options)
target_compile_definitions(morok_acceptance_g1 PRIVATE
    MOROK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Gerações/G1/shared")
morok_enable_sanitizers(morok_acceptance_g1)
add_test(NAME acceptance.g1_training_checkpoint_generation
    COMMAND morok_acceptance_g1)
set_tests_properties(acceptance.g1_training_checkpoint_generation
    PROPERTIES LABELS "acceptance;g1" TIMEOUT 180)

add_executable(morok_acceptance_seven_subsystems
    ../Gerações/G1/shared/tests/acceptance/test_seven_subsystems.cpp)
target_link_libraries(morok_acceptance_seven_subsystems
    PRIVATE Morok::G1 morok_build_options SQLite::SQLite3)
target_compile_definitions(morok_acceptance_seven_subsystems PRIVATE
    MOROK_TEST_SOURCE_DIR="${CMAKE_SOURCE_DIR}/Gerações/G1/shared")
morok_enable_sanitizers(morok_acceptance_seven_subsystems)
add_test(NAME acceptance.seven_subsystems
    COMMAND morok_acceptance_seven_subsystems)
set_tests_properties(acceptance.seven_subsystems
    PROPERTIES LABELS "acceptance;subsystems" TIMEOUT 120)



find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME architecture.generation_isolation
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/tests/scripts/test_generation_isolation.py)
set_tests_properties(architecture.generation_isolation PROPERTIES LABELS "architecture;isolation" TIMEOUT 30)
add_test(NAME reinforcement.feedback_database
    COMMAND Python3::Interpreter ${CMAKE_SOURCE_DIR}/Gerações/G1/shared/tests/scripts/test_g1_reinforcement.py)
set_tests_properties(reinforcement.feedback_database PROPERTIES LABELS "reinforcement;python" TIMEOUT 30)


# G2 Tsukuyomi v0.1: exact parameter budget, learning, inference and checkpoint.
add_executable(morok_test_g2_tsukuyomi
    ../Gerações/G2/Tsukuyomi/tests/test_model.cpp)
target_link_libraries(morok_test_g2_tsukuyomi PRIVATE Morok::G2::Tsukuyomi morok_build_options)
morok_enable_sanitizers(morok_test_g2_tsukuyomi)
add_test(NAME g2.tsukuyomi.model COMMAND morok_test_g2_tsukuyomi)
set_tests_properties(g2.tsukuyomi.model PROPERTIES LABELS "g2;model" TIMEOUT 60)

get_property(_morok_tests DIRECTORY PROPERTY TESTS)
set_tests_properties(${_morok_tests} PROPERTIES TIMEOUT 120)

# G3 neural architecture, initialization and parameter-count contracts.
add_executable(morok_test_g3_neural
    ../Gerações/G3/neural/tests/test_gru_backend.cpp)
target_link_libraries(morok_test_g3_neural
    PRIVATE Morok::G3::Neural morok_build_options)
target_compile_options(morok_test_g3_neural PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-UNDEBUG>)
morok_enable_sanitizers(morok_test_g3_neural)
add_test(NAME g3.neural COMMAND morok_test_g3_neural)
set_tests_properties(g3.neural PROPERTIES LABELS "g3;neural;architecture" TIMEOUT 60)

# G3 inference, INT8 export and artifact validation.
add_executable(morok_test_g3_inference
    ../Gerações/G3/inference/tests/test_inference.cpp)
target_link_libraries(morok_test_g3_inference
    PRIVATE Morok::G3::Inference Threads::Threads morok_build_options)
morok_enable_sanitizers(morok_test_g3_inference)
add_test(NAME g3.inference COMMAND morok_test_g3_inference)
set_tests_properties(g3.inference PROPERTIES LABELS "g3;inference;export" TIMEOUT 60)


# G3 supervised dataset pipeline, causal loss, truncated BPTT and AdamW.
add_executable(morok_test_g3_training
    ../Gerações/G3/training/tests/test_training.cpp)
target_link_libraries(morok_test_g3_training
    PRIVATE Morok::G3::Training morok_build_options)
morok_enable_sanitizers(morok_test_g3_training)
add_test(NAME g3.training COMMAND morok_test_g3_training)
set_tests_properties(g3.training PROPERTIES LABELS "g3;training;dataset" TIMEOUT 180)

# G3 versioned, checksummed and atomically persisted training checkpoints.
add_executable(morok_test_g3_checkpoint
    ../Gerações/G3/training/tests/test_checkpoint.cpp)
target_link_libraries(morok_test_g3_checkpoint
    PRIVATE Morok::G3::Training morok_build_options)
morok_enable_sanitizers(morok_test_g3_checkpoint)
add_test(NAME g3.checkpoint COMMAND morok_test_g3_checkpoint)
set_tests_properties(g3.checkpoint PROPERTIES
    LABELS "g3;checkpoint;recovery" TIMEOUT 60)

add_executable(morok_test_g3_evaluation
    ../Gerações/G3/training/tests/test_evaluation.cpp)
target_link_libraries(morok_test_g3_evaluation
    PRIVATE Morok::G3::Training Morok::G3::Inference morok_build_options)
morok_enable_sanitizers(morok_test_g3_evaluation)
add_test(NAME g3.evaluation COMMAND morok_test_g3_evaluation)
set_tests_properties(g3.evaluation PROPERTIES LABELS "g3;evaluation;generalization" TIMEOUT 180)

if(TARGET morok_g3_train)
    add_test(NAME g3.training_cli
        COMMAND morok_g3_train
            ${CMAKE_CURRENT_SOURCE_DIR}/../Gerações/G3/training/tests/fixtures/smoke.jsonl
            ${CMAKE_CURRENT_BINARY_DIR}/g3-training-smoke-metrics.jsonl
            ${CMAKE_CURRENT_BINARY_DIR}/g3-training-smoke.morok
            1 17)
    set_tests_properties(g3.training_cli PROPERTIES
        LABELS "g3;training;artifact" TIMEOUT 180)
endif()

if(TARGET morok_g3_reinforce)
    add_test(NAME g3.reinforcement_cli COMMAND morok_g3_reinforce)
    set_tests_properties(g3.reinforcement_cli PROPERTIES WILL_FAIL TRUE
        LABELS "g3;reinforcement;cli" TIMEOUT 30)
endif()

if(TARGET morok_g3_reinforce AND TARGET morok_g3_train)
    add_test(NAME g3.rl_pipeline
        COMMAND ${CMAKE_COMMAND}
            -DTRAIN=$<TARGET_FILE:morok_g3_train>
            -DRL=$<TARGET_FILE:morok_g3_reinforce>
            -DDATASET=${CMAKE_CURRENT_SOURCE_DIR}/../Gerações/G3/training/tests/fixtures/smoke.jsonl
            -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/scripts/test_g3_rl_pipeline.cmake)
    set_tests_properties(g3.rl_pipeline PROPERTIES
        LABELS "g3;reinforcement;integration" TIMEOUT 180)
endif()
