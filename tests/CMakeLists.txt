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

add_executable(morok_test_math_primitives
    unit/model/test_math_primitives.cpp
)
target_link_libraries(morok_test_math_primitives PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_math_primitives)
add_test(NAME model.math_primitives COMMAND morok_test_math_primitives)

add_executable(morok_test_g1_parameters
    unit/model/test_g1_parameters.cpp
)
target_link_libraries(morok_test_g1_parameters PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_parameters)
add_test(NAME model.g1_parameters COMMAND morok_test_g1_parameters)

add_executable(morok_test_g1_forward
    unit/model/test_g1_forward.cpp
)
target_link_libraries(morok_test_g1_forward PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_forward)
add_test(NAME model.g1_forward COMMAND morok_test_g1_forward)

add_executable(morok_test_g1_bptt
    unit/model/test_g1_bptt.cpp
)
target_link_libraries(morok_test_g1_bptt PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_bptt)
add_test(NAME model.g1_bptt COMMAND morok_test_g1_bptt)

add_executable(morok_test_g1_optimizer
    unit/model/test_g1_optimizer.cpp
)
target_link_libraries(morok_test_g1_optimizer PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_optimizer)
add_test(NAME model.g1_optimizer COMMAND morok_test_g1_optimizer)

add_executable(morok_test_g1_training unit/model/test_g1_training.cpp)
target_link_libraries(morok_test_g1_training PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_training)
add_test(NAME model.g1_training COMMAND morok_test_g1_training)

add_executable(morok_test_g1_checkpoint unit/model/test_g1_checkpoint.cpp)
target_link_libraries(morok_test_g1_checkpoint PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_checkpoint)
add_test(NAME model.g1_checkpoint COMMAND morok_test_g1_checkpoint)

add_executable(morok_test_g1_benchmark unit/model/test_g1_benchmark.cpp)
target_link_libraries(morok_test_g1_benchmark PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_benchmark)
add_test(NAME model.g1_benchmark COMMAND morok_test_g1_benchmark)

add_executable(morok_test_g1_generation unit/model/test_g1_generation.cpp)
target_link_libraries(morok_test_g1_generation PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_generation)
add_test(NAME model.g1_generation COMMAND morok_test_g1_generation)

add_executable(morok_test_g1_evaluation unit/model/test_g1_evaluation.cpp)
target_link_libraries(morok_test_g1_evaluation PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_g1_evaluation)
add_test(NAME model.g1_evaluation COMMAND morok_test_g1_evaluation)

add_executable(morok_test_runtime_contracts unit/core/test_runtime_contracts.cpp)
target_link_libraries(morok_test_runtime_contracts PRIVATE Morok::Core morok_build_options)
morok_enable_sanitizers(morok_test_runtime_contracts)
add_test(NAME core.runtime_contracts COMMAND morok_test_runtime_contracts)
