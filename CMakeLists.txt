cmake_minimum_required(VERSION 3.20)
project(egg-mouse CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

find_package(PkgConfig REQUIRED)
pkg_check_modules(HIDAPI REQUIRED IMPORTED_TARGET hidapi)

add_library(EGGCore STATIC
  Sources/EGGCore/src/Protocol.cpp
  Sources/EGGCore/src/Device.cpp
  Sources/EGGCore/src/Transport.cpp
  Sources/EGGCore/src/Log.cpp
)
target_include_directories(EGGCore PUBLIC Sources/EGGCore/include)
target_link_libraries(EGGCore PUBLIC PkgConfig::HIDAPI)

# CLAUDE.md §3: two executables, never one. Config and flash have contradictory
# quit semantics (§4.2) and sharing a process makes "can the user quit now?" a
# conditional on internal state -- the exact bug class being designed out.
# §3 lists EGGConfigCore as "read-modify-write, diff-verify". Until 2026-09-05
# that logic lived inside the CLI translation unit, so §4.1's safety properties
# could only be exercised by owning the mouse. It is a library now, and
# Tests/test_config.cpp drives it against a device that lies.
add_library(EGGConfigCore STATIC
  Sources/EGGConfigCore/src/ConfigRecord.cpp
  Sources/EGGConfigCore/src/ConfigSession.cpp
  Sources/EGGConfigCore/src/MockConfigDevice.cpp
  Sources/EGGConfigCore/src/RecordVault.cpp
)
target_include_directories(EGGConfigCore PUBLIC Sources/EGGConfigCore/include)
target_link_libraries(EGGConfigCore PUBLIC EGGCore)

add_executable(egg-config Sources/egg-config/main.cpp)
target_link_libraries(egg-config PRIVATE EGGConfigCore)

add_library(EGGFlashCore STATIC
  Sources/EGGFlashCore/src/Firmware.cpp
  Sources/EGGFlashCore/src/FlashCommands.cpp
  Sources/EGGFlashCore/src/WritePhase.cpp
  Sources/EGGFlashCore/src/MockBootloader.cpp
)
target_include_directories(EGGFlashCore PUBLIC Sources/EGGFlashCore/include)
target_link_libraries(EGGFlashCore PUBLIC EGGCore)

add_executable(egg-flash Sources/egg-flash/main.cpp)
# EGGConfigCore too, and for a reason worth stating rather than hiding behind a
# link line: egg-flash's last command is A1 13, byte-for-byte the config tool's
# Factory Reset. Whether it wipes settings is [G], so the flasher has to know
# what a settings record is in order to report whether an undo exists. §3 keeps
# the two EXECUTABLES apart because they have contradictory quit semantics; it
# never said they may not share a library.
target_link_libraries(egg-flash PRIVATE EGGFlashCore EGGConfigCore)

# Tests. §4.3: "Assert invariants, not examples, over randomised runs." These
# need no hardware and no capture, which is the point -- they are the only
# check that exists before stage 4.
enable_testing()
add_executable(test-flash Tests/test_flash.cpp)
target_link_libraries(test-flash PRIVATE EGGFlashCore)

# EVERY test runs from the source root. test-flash opens the vendor .exe by a
# relative path, so under ctest's default working directory (the build tree) it
# reported "cannot open ... 1.10.exe" as three FAILURES -- a red result that
# meant nothing, which is worse than either a pass or a real failure. Set it
# once, for all of them, rather than teaching each test to find the root.
add_test(NAME flash COMMAND test-flash)
set_tests_properties(flash PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(test-config Tests/test_config.cpp)
target_link_libraries(test-config PRIVATE EGGConfigCore)
add_test(NAME config COMMAND test-config)
set_tests_properties(config PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# The rest of the suite was reachable only by knowing it existed and typing the
# right command, which for a project whose whole safety argument rests on its
# tests is not good enough. `ctest` is now the single gate.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_FOUND)
  # unittest-based.
  foreach(t config_replay fwfile_set golden_vendor golden_config_cmds undo_loadable usbpcap)
    add_test(NAME ${t}
             COMMAND ${Python3_EXECUTABLE} -m unittest -v Tests.test_${t})
    set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()
  # Plain scripts that print their own PASS/FAIL and exit non-zero on failure.
  # They are NOT unittest modules, which is why `unittest discover` reported
  # them as import errors -- a loader complaint, never a test result.
  foreach(t fieldmap score)
    add_test(NAME ${t} COMMAND ${Python3_EXECUTABLE} Tests/test_${t}.py)
    set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()
endif()

add_test(NAME config_set COMMAND Tests/test_config_set.sh)
set_tests_properties(config_set PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME flash_undo COMMAND Tests/test_flash_undo.sh)
set_tests_properties(flash_undo PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# Mutation testing: §6.2's check that the flash suite can produce a bad result.
# RUN_SERIAL because it edits the source tree, rebuilds, and restores -- nothing
# else may be compiling or running a built binary while it does. Its EXIT trap
# restores the tree and rebuilds, so it leaves no mutant behind even if killed.
add_test(NAME mutants COMMAND Tests/mutants.sh)
set_tests_properties(mutants PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} RUN_SERIAL TRUE TIMEOUT 900)
