if(NOT DEFINED PATCH_WORKING_DIRECTORY
    OR NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR
        "PATCH_WORKING_DIRECTORY and PATCH_FILE are required")
endif()

execute_process(
    COMMAND git apply --unidiff-zero --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${PATCH_WORKING_DIRECTORY}"
    RESULT_VARIABLE PATCH_CAN_APPLY
    OUTPUT_QUIET
    ERROR_QUIET
)
if(PATCH_CAN_APPLY EQUAL 0)
    execute_process(
        COMMAND git apply --unidiff-zero --whitespace=nowarn "${PATCH_FILE}"
        WORKING_DIRECTORY "${PATCH_WORKING_DIRECTORY}"
        RESULT_VARIABLE PATCH_APPLY_RESULT
    )
    if(NOT PATCH_APPLY_RESULT EQUAL 0)
        message(FATAL_ERROR "Could not apply ${PATCH_FILE}")
    endif()
    return()
endif()

execute_process(
    COMMAND git apply --unidiff-zero --reverse --check "${PATCH_FILE}"
    WORKING_DIRECTORY "${PATCH_WORKING_DIRECTORY}"
    RESULT_VARIABLE PATCH_ALREADY_APPLIED
    OUTPUT_QUIET
    ERROR_QUIET
)
if(NOT PATCH_ALREADY_APPLIED EQUAL 0)
    message(FATAL_ERROR
        "${PATCH_FILE} neither applies nor matches the current source")
endif()
