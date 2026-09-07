cmake_minimum_required(VERSION 3.20)
project(egg-mouse CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wconversion)

# FORMAT MISMATCHES ARE ERRORS, NOT WARNINGS. 2026-09-05: a printf in
# cmdReadFirmware grew a third %s without a third argument, which is undefined
# behaviour -- it reads whatever happens to be in the next argument slot -- and
# it reached a commit because a warning scrolled past in the same command that
# made the commit. This tool's output is how a person decides whether to erase
# a mouse's firmware, so a garbled or crashing message is not cosmetic.
# -Wall already warns; only -Werror stops it being ignorable.
add_compile_options(-Werror=format -Werror=format-security -Werror=format-extra-args)

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

# engineering-rules.md §3: two executables, never one. Config and flash have contradictory
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
  Sources/EGGFlashCore/src/FirmwareManifest.cpp
  Sources/EGGFlashCore/src/FlashCommands.cpp
  Sources/EGGFlashCore/src/WritePhase.cpp
  Sources/EGGFlashCore/src/NoQuitDuringWrite.cpp
  Sources/EGGFlashCore/src/MockBootloader.cpp
  Sources/EGGFlashCore/src/BootloaderEntry.cpp
  Sources/EGGFlashCore/src/FlashPlan.cpp
  Sources/EGGFlashCore/src/HidBootloaderLink.cpp
  Sources/EGGFlashCore/src/Provenance.cpp
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
  foreach(t actionset adoption button_map citations config_replay coverage cpi crossrefs defaults freshclone gui_state handedness fwfile_set golden_vendor golden_config_cmds ingest_config ingest_errors ingest_row keymap lineage lod manifest recmap undo_loadable usbpcap)
    add_test(NAME ${t}
             COMMAND ${Python3_EXECUTABLE} -m unittest -v Tests.test_${t})
    set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()
  # coverage re-runs coverage.py over nine binaries, loading every export each
  # time; ~40s locally, so it needs more than ctest's default.
  set_tests_properties(coverage PROPERTIES TIMEOUT 600)
  # Plain scripts that print their own PASS/FAIL and exit non-zero on failure.
  # They are NOT unittest modules, which is why `unittest discover` reported
  # them as import errors -- a loader complaint, never a test result.
  foreach(t auditclaims fieldmap reconcile score)
    add_test(NAME ${t} COMMAND ${Python3_EXECUTABLE} Tests/test_${t}.py)
    set_tests_properties(${t} PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endforeach()

  # The audits themselves, not just the tests OF the audits. Both reached zero
  # on 2026-09-06 and both stay there only if something checks: auditclaims
  # --strict fails on an uncited [D] claim, a wrong address or a citation to
  # the quarantined log, and reconcile_scores fails on a tally that drifts from
  # the entries under it. A gate that is not run is a gate that is open.
  add_test(NAME audit_claims_strict
           COMMAND ${Python3_EXECUTABLE} Tools/ghidra-export/auditclaims.py --strict)
  set_tests_properties(audit_claims_strict PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 600
                       RESOURCE_LOCK notes_tree)

  # WHY THE LOCK. Tests/test_auditclaims.py measures the checker by PLANTING a
  # defective claim as a real file in notes/ -- it has to be the real tree, or
  # the tool would resolve cross-references against a copy and the plants would
  # test the copy. Under `ctest -j4` that put a deliberately-bad file in front
  # of audit_claims_strict, which duly failed. A gate that fails at random gets
  # ignored, which is worse than not having it, so the two are serialised
  # against each other by name. Add any future test that GLOBS notes/*.md to
  # this lock; tests that open a named file under notes/ do not need it.
  set_tests_properties(auditclaims PROPERTIES RESOURCE_LOCK notes_tree)

  add_test(NAME blob_census
           COMMAND ${Python3_EXECUTABLE} Tools/pe/blobcensus.py --check)
  set_tests_properties(blob_census PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

# The SwiftUI front end's command builders, driven against the real CLIs.
#
# COMPILED, not run through `swift` in script mode. Swift only executes
# top-level code from a file named main.swift, so `swift Commands.swift
# test.swift` compiles both, runs nothing, and exits 0 -- a test that passes by
# doing nothing at all, which is the exact failure this file's other comments
# warn about. It was written that way first and caught by running it by hand.
find_program(SWIFTC_EXECUTABLE swiftc)
if(SWIFTC_EXECUTABLE)
  add_test(NAME app_commands
           COMMAND ${CMAKE_COMMAND}
                   -DSWIFTC=${SWIFTC_EXECUTABLE}
                   -DSRC=${CMAKE_SOURCE_DIR}
                   -DBIN=${CMAKE_BINARY_DIR}
                   -P ${CMAKE_SOURCE_DIR}/Tests/run_app_commands.cmake)
  set_tests_properties(app_commands PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

  # ...and the views, which app_commands does not compile. See the comment in
  # the script: green tests while the app does not build is the worst of both.
  add_test(NAME app_typecheck
           COMMAND ${CMAKE_COMMAND}
                   -DSWIFTC=${SWIFTC_EXECUTABLE}
                   -DSRC=${CMAKE_SOURCE_DIR}
                   -P ${CMAKE_SOURCE_DIR}/Tests/typecheck_app.cmake)
  set_tests_properties(app_typecheck PROPERTIES
                       WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} TIMEOUT 300)
endif()

# Is a planted mutant sitting in the tree? Safe as a ctest entry because
# `mutants` is RUN_SERIAL -- nothing else runs while the source is mutated on
# purpose -- so a hit here means a run died, or one got staged by hand.
# NAMED `tree_clean`, NOT `no_mutants`, and the name is the whole point.
# `ctest -E mutants` is the fast loop everyone types, and -E is an unanchored
# regex -- so a guard called `no_mutants` is excluded by the very command that
# skips the long run, which is exactly when a leftover mutant is most likely to
# be sitting in the tree. Caught within the hour of writing the guard, by
# counting: 31 tests total, 29 under `-E mutants`, not 30.
add_test(NAME tree_clean COMMAND Tools/no-mutants.sh)
# notes_tree as well: the guard also refuses a leftover notes/zzz-planted-*.md,
# and test_auditclaims.py has one on disk for as long as it runs.
set_tests_properties(tree_clean PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     RESOURCE_LOCK notes_tree)

add_test(NAME config_set COMMAND Tests/test_config_set.sh)
set_tests_properties(config_set PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# SKIP_RETURN_CODE 77 on every test that needs a vendor .exe. Those files are
# gitignored and are on exactly one machine, so on a fresh clone these four used
# to `exit 0` and ctest printed four green passes for four tests that asserted
# nothing -- engineering-rules.md 6.2's "a harness that cannot produce a bad result is not
# evidence", in the four gates that stand between a mistake and a flash. They
# now report Skipped, which is the true statement (2026-09-07).
add_test(NAME flash_undo COMMAND Tests/test_flash_undo.sh)
set_tests_properties(flash_undo PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     SKIP_RETURN_CODE 77)

# The backup gate. Runs with no mouse attached, which is why cmdFlash does its
# host-side refusals before it enumerates -- see the comment there.
add_test(NAME flash_backup COMMAND Tests/test_flash_backup.sh)
set_tests_properties(flash_backup PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     SKIP_RETURN_CODE 77)

# restore-firmware's provenance gate. Same shape as flash_backup and for the
# same reason: the unit tests prove the loader refuses, and only a CLI test can
# prove the verb reaches the loader before it reaches the device.
add_test(NAME flash_restore COMMAND Tests/test_flash_restore.sh)
set_tests_properties(flash_restore PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     SKIP_RETURN_CODE 77)

# The GUI's button-mapping picker gets its list from `egg-config map --machine`.
# This runs every name that listing emits through the real CLI, so an action the
# app would offer and the tool would refuse fails the suite. Nothing asked that
# question before 2026-09-06 and five such entries were live.
add_test(NAME map_machine COMMAND Tests/test_map_machine.sh)
set_tests_properties(map_machine PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

add_test(NAME show COMMAND Tests/test_show.sh)

set_tests_properties(show PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

# The NON-DEFAULT firmware release row. Every other test drives kReleases[0];
# this one drives 1.07 and 1.04 -- selection, refusal, the unproven-release
# gate, and the approval token derived from the chosen image. Off-wire: no
# --confirm is ever passed, so nothing can reach a device.
add_test(NAME release_path COMMAND Tests/test_release_path.sh)
set_tests_properties(release_path PROPERTIES WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
                     SKIP_RETURN_CODE 77)

# Mutation testing: §6.2's check that the flash suite can produce a bad result.
# RUN_SERIAL because it edits the source tree, rebuilds, and restores -- nothing
# else may be compiling or running a built binary while it does. Its EXIT trap
# restores the tree and rebuilds, so it leaves no mutant behind even if killed.
add_test(NAME mutants COMMAND Tests/mutants.sh)
# TIMEOUT is 3600, not ctest's default and not the 900 it used to be. A short
# timeout on THIS test is not a mild inconvenience: mutants.sh edits the real
# source tree, so being killed part-way leaves a planted mutant behind. That
# happened on 2026-09-06 -- the suite had grown to 81 mutants and ~25 minutes,
# and 900s killed it with `// MUTANT: no high clamp` still in ConfigRecord.cpp,
# which removes the CPI ceiling. The run was ALSO reported as a failure, so the
# short timeout cost a red suite and a corrupted tree at once.
# Raise this before adding a batch of mutants, not after.
set_tests_properties(mutants PROPERTIES
                     WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} RUN_SERIAL TRUE TIMEOUT 3600)
