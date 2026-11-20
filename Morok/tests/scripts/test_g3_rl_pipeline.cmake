if(NOT DEFINED RL OR NOT DEFINED DATASET OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "Missing G3 independent RL pipeline test arguments")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}/g3-rl-pipeline")
set(RL_MODEL "${WORK_DIR}/g3-rl-pipeline/rl.morok")
set(RL_REPORT "${WORK_DIR}/g3-rl-pipeline/rl-report.json")

execute_process(
    COMMAND "${RL}" "${DATASET}" "${RL_MODEL}" "${RL_REPORT}" 1 42
    RESULT_VARIABLE RL_RESULT
    OUTPUT_VARIABLE RL_OUTPUT
    ERROR_VARIABLE RL_ERROR
)
if(NOT RL_RESULT EQUAL 0)
    message(FATAL_ERROR "G3 independent RL fixture failed: ${RL_OUTPUT}\n${RL_ERROR}")
endif()

if(NOT EXISTS "${RL_MODEL}" OR NOT EXISTS "${RL_REPORT}")
    message(FATAL_ERROR "G3 independent RL did not produce both model and report artifacts")
endif()
file(READ "${RL_REPORT}" REPORT_CONTENT)
foreach(REQUIRED_TEXT IN ITEMS
    "morok-g3-rl-independent-report-v1"
    "independent_random_initialization"
    "mean_training_reward"
    "\"evaluation\""
)
    string(FIND "${REPORT_CONTENT}" "${REQUIRED_TEXT}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "G3 independent RL report is missing ${REQUIRED_TEXT}")
    endif()
endforeach()
message(STATUS "G3 independent initialization -> REINFORCE -> evaluation integration passed")
