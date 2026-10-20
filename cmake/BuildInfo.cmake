find_package(Git QUIET)
set(PORT_GIT_COMMIT DIRTY)
if(GIT_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
  execute_process(COMMAND "${GIT_EXECUTABLE}" describe --always --dirty --abbrev=10
                  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                  OUTPUT_VARIABLE git_commit OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  if(git_commit)
    set(PORT_GIT_COMMIT "${git_commit}")
  endif()
endif()

string(TIMESTAMP PORT_BUILD_ID "%Y%m%d-%H%M%S" UTC)
set(PORT_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
configure_file("${CMAKE_CURRENT_LIST_DIR}/expected.h.in" "${PORT_GENERATED_DIR}/expected.h" @ONLY)
