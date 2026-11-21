# Apply (or verify) the Qlementine combo box recursion fix.
#
# FetchContent runs PATCH_COMMAND in the dependency's source directory.
# The patch is idempotent: if it is already applied, the reverse check
# succeeds and this script exits without touching the tree.
if(NOT DEFINED PATCH_FILE)
    message(FATAL_ERROR "PATCH_FILE is required")
endif()

execute_process(
    COMMAND git apply --reverse --check "${PATCH_FILE}"
    RESULT_VARIABLE already_applied
    OUTPUT_QUIET
    ERROR_QUIET)
if(already_applied EQUAL 0)
    message(STATUS "qlementine patch already applied")
    return()
endif()

execute_process(
    COMMAND git apply --whitespace=nowarn "${PATCH_FILE}"
    RESULT_VARIABLE apply_result
    OUTPUT_VARIABLE apply_output
    ERROR_VARIABLE apply_error)
if(NOT apply_result EQUAL 0)
    message(FATAL_ERROR "failed to apply qlementine patch: ${apply_error}${apply_output}")
endif()
message(STATUS "qlementine patch applied")
