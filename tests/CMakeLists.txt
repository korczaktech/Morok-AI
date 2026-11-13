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
