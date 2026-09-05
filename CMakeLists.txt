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
