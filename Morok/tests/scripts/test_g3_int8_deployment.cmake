if(NOT DEFINED TRAIN OR NOT DEFINED QUANTIZE OR NOT DEFINED GENERATE OR
   NOT DEFINED DATASET OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "G3 INT8 deployment test arguments are incomplete")
endif()

file(MAKE_DIRECTORY "\${WORK_DIR}")
set(metrics "\${WORK_DIR}/g3-int8-deployment-metrics.jsonl")
set(fp32_model "\${WORK_DIR}/g3-int8-deployment-fp32.morok")
set(int8_model "\${WORK_DIR}/g3-int8-deployment-int8.morok")

execute_process(
    COMMAND "\${TRAIN}" "\${DATASET}" "\${metrics}" "\${fp32_model}" 1 17
    RESULT_VARIABLE train_result
    OUTPUT_VARIABLE train_output
    ERROR_VARIABLE train_error
)
if(NOT train_result EQUAL 0)
    message(FATAL_ERROR "G3 SFT smoke training failed: \${train_error}")
endif()

execute_process(
    COMMAND "\${QUANTIZE}" "\${fp32_model}" "\${int8_model}"
    RESULT_VARIABLE quantize_result
    OUTPUT_VARIABLE quantize_output
    ERROR_VARIABLE quantize_error
)
if(NOT quantize_result EQUAL 0)
    message(FATAL_ERROR "G3 FP32 to INT8 export failed: \${quantize_error}")
endif()
if(NOT quantize_output MATCHES "\"precision\":\"INT8\"")
    message(FATAL_ERROR "G3 quantizer did not report an INT8 artifact: \${quantize_output}")
endif()
if(NOT quantize_output MATCHES "\"int8_payload_bytes\":107983")
    message(FATAL_ERROR "G3 INT8 payload size does not match the compact tensor layout: \${quantize_output}")
endif()

execute_process(
    COMMAND "\${GENERATE}" "\${int8_model}" --benchmark --max-output=8 --top-k=1 "Morok G3"
    RESULT_VARIABLE generate_result
    OUTPUT_VARIABLE generate_output
    ERROR_VARIABLE generate_error
)
if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR "G3 INT8 inference/benchmark failed: \${generate_error}")
endif()
if(NOT generate_output MATCHES "\"precision\":\"INT8\"")
    message(FATAL_ERROR "G3 benchmark did not execute the INT8 backend: \${generate_output}")
endif()
if(NOT generate_output MATCHES "\"median_latency_ns\":[1-9][0-9]*")
    message(FATAL_ERROR "G3 benchmark did not report measured latency: \${generate_output}")
endif()
if(NOT generate_output MATCHES "\"peak_rss_bytes\":[0-9]+")
    message(FATAL_ERROR "G3 benchmark did not report process memory: \${generate_output}")
endif()

message(STATUS "G3 INT8 deployment smoke test passed")
