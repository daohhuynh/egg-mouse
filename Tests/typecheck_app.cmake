# typecheck_app.cmake -- does the SwiftUI front end still compile?
#
# app_commands compiles Commands.swift ONLY, because that is the file with
# logic worth asserting on. The consequence went unnoticed until 2026-09-06:
# the four view files were in no gate at all. A rename in Commands.swift, or a
# SwiftUI signature change, would break the app while `ctest` stayed green --
# and the app is the part a person other than its author is meant to use.
#
# This does not assert behaviour; SwiftUI views are not testable without a
# harness this project has no reason to build. It asserts the strictly weaker
# and still worth having thing: the code the user runs is code that compiles.
execute_process(COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE SDK OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE sdkok ERROR_QUIET)
if(NOT sdkok EQUAL 0)
  message(STATUS "no macOS SDK; skipping the app type-check")
  return()
endif()

file(GLOB APP "${SRC}/Sources/EGGApp/*.swift")
execute_process(
  COMMAND "${SWIFTC}" -typecheck -swift-version 5 -sdk "${SDK}" ${APP}
  RESULT_VARIABLE ok OUTPUT_VARIABLE o ERROR_VARIABLE e)
if(NOT ok EQUAL 0)
  message(FATAL_ERROR "the app does not compile:\n${o}${e}")
endif()
message(STATUS "EGGApp type-checks")
