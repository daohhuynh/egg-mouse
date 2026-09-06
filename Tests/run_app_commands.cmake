# run_app_commands.cmake -- compile and run Tests/test_app_commands.swift.
#
# A shim exists because Swift executes top-level code only from a file called
# main.swift. Copying the test to that name and COMPILING it is the difference
# between a test that runs and one that silently passes having executed nothing.
set(WORK "${BIN}/app-commands")
file(MAKE_DIRECTORY "${WORK}")
configure_file("${SRC}/Tests/test_app_commands.swift" "${WORK}/main.swift" COPYONLY)
configure_file("${SRC}/Sources/EGGApp/Commands.swift" "${WORK}/Commands.swift" COPYONLY)

execute_process(
  COMMAND "${SWIFTC}" -swift-version 5 -o "${WORK}/run"
          "${WORK}/Commands.swift" "${WORK}/main.swift"
  RESULT_VARIABLE built OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT built EQUAL 0)
  message(FATAL_ERROR "swiftc failed:\n${o}${e}")
endif()

execute_process(COMMAND "${WORK}/run" WORKING_DIRECTORY "${SRC}"
                RESULT_VARIABLE ran OUTPUT_VARIABLE o ERROR_VARIABLE e)
message(STATUS "${o}${e}")
if(NOT ran EQUAL 0)
  message(FATAL_ERROR "test_app_commands failed")
endif()
