if(NOT DEFINED RL OR NOT DEFINED DATASET OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "Missing G3 independent RL pipeline test arguments")
endif()
file(MAKE_DIRECTORY "${WORK_DIR}/g3-rl-pipeline")
set(RL_MODEL "${WORK_DIR}/g3-rl-pipeline/rl.morok")
set(RL_REPORT "${WORK_DIR}/g3-rl-pipeline/rl-report.json")

execute_process(
    COMMAND "${RL}" "${DATASET}" "${RL_MODEL}" "${RL_REPORT}" 2 42
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
    "\"reference_policy_loaded\": false"
    "g3-independent-reference-rubric"
    "g3-reference-v2-signed"
    "dataset-reference-answer-v1"
    "mean_training_reward"
    "\"kl_beta\""
    "\"evaluation\""
)
    string(FIND "${REPORT_CONTENT}" "${REQUIRED_TEXT}" FOUND)
    if(FOUND EQUAL -1)
        message(FATAL_ERROR "G3 independent RL report is missing ${REQUIRED_TEXT}")
    endif()
endforeach()
string(REGEX MATCH "\"parameters\"[ \\t]*:[ \\t]*([0-9]+)" PARAMETER_MATCH "${REPORT_CONTENT}")
if(NOT CMAKE_MATCH_1 STREQUAL "100000")
    message(FATAL_ERROR "Independent RL artifact report must declare exactly 100000 parameters")
endif()
string(REGEX MATCH "\"optimizer_updates\"[ \\t]*:[ \\t]*([0-9]+)" UPDATE_MATCH "${REPORT_CONTENT}")
if(NOT UPDATE_MATCH OR NOT CMAKE_MATCH_1 STREQUAL "2")
    message(FATAL_ERROR "Two RL epochs must perform exactly two optimizer updates")
endif()
string(REGEX MATCH "\"training_prompt_visits\"[ \\t]*:[ \\t]*([0-9]+)" PROMPT_MATCH "${REPORT_CONTENT}")
if(NOT PROMPT_MATCH OR CMAKE_MATCH_1 LESS 1)
    message(FATAL_ERROR "Independent RL report omitted total training prompt visits")
endif()
set(TRAINING_PROMPT_VISITS "${CMAKE_MATCH_1}")
string(REGEX MATCH "\"training_prompts\"[ \\t]*:[ \\t]*([0-9]+)" UNIQUE_PROMPT_MATCH "${REPORT_CONTENT}")
math(EXPR EXPECTED_UNIQUE_PROMPTS "${TRAINING_PROMPT_VISITS} / 2")
if(NOT UNIQUE_PROMPT_MATCH OR NOT CMAKE_MATCH_1 EQUAL EXPECTED_UNIQUE_PROMPTS)
    message(FATAL_ERROR "RL report must distinguish unique training prompts from epoch visits")
endif()
string(REGEX MATCH "\"candidates\"[ \\t]*:[ \\t]*([0-9]+)" CANDIDATE_MATCH "${REPORT_CONTENT}")
math(EXPR EXPECTED_CANDIDATES "${TRAINING_PROMPT_VISITS} * 4")
if(NOT CANDIDATE_MATCH OR NOT CMAKE_MATCH_1 EQUAL EXPECTED_CANDIDATES)
    message(FATAL_ERROR "RL report candidate total must aggregate all prompts across both epochs")
endif()
string(REGEX MATCH "\"mean_kl\"[ \\t]*:[ \\t]*([0-9.]+)" KL_MATCH "${REPORT_CONTENT}")
if(NOT KL_MATCH OR (NOT CMAKE_MATCH_1 STREQUAL "0" AND
        NOT CMAKE_MATCH_1 STREQUAL "0.0"))
    message(FATAL_ERROR "Independent RL must run without a frozen reference KL term")
endif()
string(REGEX MATCH "\"examples\"[ \\t]*:[ \\t]*([0-9]+)" EVALUATION_MATCH "${REPORT_CONTENT}")
if(NOT EVALUATION_MATCH OR CMAKE_MATCH_1 LESS 1)
    message(FATAL_ERROR "Independent RL test split was not evaluated")
endif()
message(STATUS "G3 independent initialization -> REINFORCE -> evaluation integration passed")
