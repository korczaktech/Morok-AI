if(NOT DEFINED TRAIN OR NOT DEFINED RL OR NOT DEFINED DATASET OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "Missing G3 RL pipeline test arguments")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}/g3-rl-pipeline")
set(SFT_MODEL "${WORK_DIR}/g3-rl-pipeline/sft.morok")
set(SFT_METRICS "${WORK_DIR}/g3-rl-pipeline/sft-metrics.jsonl")
set(RL_MODEL "${WORK_DIR}/g3-rl-pipeline/rl.morok")
set(RL_REPORT "${WORK_DIR}/g3-rl-pipeline/rl-report.json")

execute_process(
    COMMAND "${TRAIN}" "${DATASET}" "${SFT_METRICS}" "${SFT_MODEL}" 1 42
    RESULT_VARIABLE TRAIN_RESULT
    OUTPUT_VARIABLE TRAIN_OUTPUT
    ERROR_VARIABLE TRAIN_ERROR
)
if(NOT TRAIN_RESULT EQUAL 0)
    message(FATAL_ERROR "G3 SFT fixture failed: ${TRAIN_OUTPUT}\n${TRAIN_ERROR}")
endif()

execute_process(
    COMMAND "${RL}" "${DATASET}" "${SFT_MODEL}" "${RL_MODEL}" "${RL_REPORT}" 1
    RESULT_VARIABLE RL_RESULT
    OUTPUT_VARIABLE RL_OUTPUT
    ERROR_VARIABLE RL_ERROR
)
if(NOT RL_RESULT EQUAL 0)
    message(FATAL_ERROR "G3 RL fixture failed: ${RL_OUTPUT}\n${RL_ERROR}")
endif()

if(NOT EXISTS "${RL_MODEL}" OR NOT EXISTS "${RL_REPORT}")
    message(FATAL_ERROR "G3 RL did not produce both model and report artifacts")
endif()
file(READ "${RL_REPORT}" REPORT_CONTENT)
foreach(REQUIRED_TEXT IN ITEMS
    "morok-g3-rl-report-v1"
    "mean_training_reward"
    "\"sft\""
    "\"rl\""
    "reward_delta"
)
    string(FIND "${REPORT_CONTENT}" "${REQUIRED_TEXT}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "G3 RL report is missing ${REQUIRED_TEXT}")
    endif()
endforeach()
message(STATUS "G3 SFT -> REINFORCE -> comparison integration passed")
