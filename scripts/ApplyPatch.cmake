# Apply a git patch to a fetched source tree, unless it is already applied.
#
# Required variables:
#   PATCH_FILE - absolute path to the patch file

if(NOT DEFINED PATCH_FILE OR NOT EXISTS "${PATCH_FILE}")
    message(FATAL_ERROR "ApplyPatch: patch file not found: '${PATCH_FILE}'")
endif()

find_package(Git REQUIRED)

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --reverse --check "${PATCH_FILE}"
    RESULT_VARIABLE _already_applied
    OUTPUT_QUIET
    ERROR_QUIET)

if(_already_applied EQUAL 0)
    message(STATUS "ApplyPatch: patch already applied: ${PATCH_FILE}")
    return()
endif()

execute_process(
    COMMAND "${GIT_EXECUTABLE}" apply --ignore-whitespace "${PATCH_FILE}"
    RESULT_VARIABLE _apply_result)

if(NOT _apply_result EQUAL 0)
    message(FATAL_ERROR "ApplyPatch: failed to apply ${PATCH_FILE}")
endif()
