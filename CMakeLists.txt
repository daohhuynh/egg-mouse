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
add_executable(egg-config Sources/egg-config/main.cpp)
target_link_libraries(egg-config PRIVATE EGGCore)

add_library(EGGFlashCore STATIC
  Sources/EGGFlashCore/src/Firmware.cpp
  Sources/EGGFlashCore/src/FlashCommands.cpp
  Sources/EGGFlashCore/src/WritePhase.cpp
  Sources/EGGFlashCore/src/MockBootloader.cpp
)
target_include_directories(EGGFlashCore PUBLIC Sources/EGGFlashCore/include)
target_link_libraries(EGGFlashCore PUBLIC EGGCore)

add_executable(egg-flash Sources/egg-flash/main.cpp)
target_link_libraries(egg-flash PRIVATE EGGFlashCore)

# Tests. §4.3: "Assert invariants, not examples, over randomised runs." These
# need no hardware and no capture, which is the point -- they are the only
# check that exists before stage 4.
enable_testing()
add_executable(test-flash Tests/test_flash.cpp)
target_link_libraries(test-flash PRIVATE EGGFlashCore)
add_test(NAME flash COMMAND test-flash)
