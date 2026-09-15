if(NOT DEFINED VPP_EXEC OR VPP_EXEC STREQUAL "")
    message(FATAL_ERROR "VPP_EXEC is required")
endif()
if(NOT DEFINED TEST_ROOT OR TEST_ROOT STREQUAL "")
    message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
set(SOURCE_DIR "${TEST_ROOT}/source-package")
set(PROJECT_DIR "${TEST_ROOT}/project")
file(MAKE_DIRECTORY "${SOURCE_DIR}" "${PROJECT_DIR}")
file(WRITE "${SOURCE_DIR}/main.vi" "hàm ping() { trả về 7; }\n")

function(run_vpp working_directory)
    execute_process(
        COMMAND "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR
            "vpp command failed (${result}): ${ARGN}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(LAST_STDOUT "${stdout}" PARENT_SCOPE)
endfunction()

run_vpp("${SOURCE_DIR}" init dep-demo)
run_vpp("${PROJECT_DIR}" init app-demo)
run_vpp("${PROJECT_DIR}" install "${SOURCE_DIR}")

set(MANIFEST "${PROJECT_DIR}/vpp.json")
if(NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "package install did not create vpp.json")
endif()
file(READ "${MANIFEST}" manifest_text)
string(FIND "${manifest_text}" "\"name\": \"dep-demo\"" dependency_name)
string(FIND "${manifest_text}" "\"version\": \"0.1.0\"" dependency_version)
if(dependency_name EQUAL -1 OR dependency_version EQUAL -1)
    message(FATAL_ERROR "manifest does not contain installed package metadata:\n${manifest_text}")
endif()

set(INSTALLED_MAIN "${PROJECT_DIR}/gói/dep-demo/main.vi")
if(NOT EXISTS "${INSTALLED_MAIN}")
    message(FATAL_ERROR "installed package main.vi is missing")
endif()

run_vpp("${PROJECT_DIR}" list)
string(FIND "${LAST_STDOUT}" "dep-demo" listed_package)
if(listed_package EQUAL -1)
    message(FATAL_ERROR "package list does not include dep-demo: ${LAST_STDOUT}")
endif()

run_vpp("${PROJECT_DIR}" lock)
set(LOCKFILE "${PROJECT_DIR}/vpp.lock")
if(NOT EXISTS "${LOCKFILE}")
    message(FATAL_ERROR "lock command did not create vpp.lock")
endif()
file(READ "${LOCKFILE}" first_lock)
string(FIND "${first_lock}" "fnv1a64:" fingerprint_offset)
if(fingerprint_offset EQUAL -1)
    message(FATAL_ERROR "lockfile does not contain a package fingerprint")
endif()

run_vpp("${PROJECT_DIR}" lock)
file(READ "${LOCKFILE}" repeated_lock)
if(NOT first_lock STREQUAL repeated_lock)
    message(FATAL_ERROR "repeated lock changed deterministic lockfile bytes")
endif()

file(APPEND "${INSTALLED_MAIN}" "// changed\n")
run_vpp("${PROJECT_DIR}" lock)
file(READ "${LOCKFILE}" changed_lock)
if(first_lock STREQUAL changed_lock)
    message(FATAL_ERROR "package content change did not update lockfile fingerprint")
endif()

# Restore uses the exact bytes recorded by the original lock. Reinstalling from
# an unchanged path source must recover a deleted package deterministically.
file(WRITE "${LOCKFILE}" "${first_lock}")
file(REMOVE_RECURSE "${PROJECT_DIR}/gói/dep-demo")
run_vpp("${PROJECT_DIR}" restore)
if(NOT EXISTS "${INSTALLED_MAIN}")
    message(FATAL_ERROR "restore did not recreate locked package")
endif()
file(READ "${INSTALLED_MAIN}" restored_main)
if(NOT restored_main STREQUAL "hàm ping() { trả về 7; }\n")
    message(FATAL_ERROR "restore did not recover locked package bytes: ${restored_main}")
endif()

run_vpp("${PROJECT_DIR}" remove dep-demo)
if(EXISTS "${PROJECT_DIR}/gói/dep-demo")
    message(FATAL_ERROR "package remove left installed directory behind")
endif()
file(READ "${MANIFEST}" removed_manifest)
string(FIND "${removed_manifest}" "\"name\": \"dep-demo\"" stale_dependency)
if(NOT stale_dependency EQUAL -1)
    message(FATAL_ERROR "package remove left dependency in manifest:\n${removed_manifest}")
endif()

# Transitive local path graph: parent -> shared. `install parent` must resolve and
# materialize both packages; sync and restore must operate on the same graph.
set(SHARED_DIR "${TEST_ROOT}/shared-package")
set(PARENT_DIR "${TEST_ROOT}/parent-package")
set(TRANSITIVE_PROJECT "${TEST_ROOT}/transitive-project")
file(MAKE_DIRECTORY "${SHARED_DIR}" "${PARENT_DIR}" "${TRANSITIVE_PROJECT}")
file(WRITE "${SHARED_DIR}/main.vi" "hàm shared() { trả về 11; }\n")
file(WRITE "${PARENT_DIR}/main.vi" "hàm parent() { trả về 22; }\n")
run_vpp("${SHARED_DIR}" init shared)
run_vpp("${PARENT_DIR}" init parent)
file(WRITE "${PARENT_DIR}/vpp.json"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"name\": \"parent\",\n"
    "  \"version\": \"0.1.0\",\n"
    "  \"dependencies\": [\n"
    "    {\"name\": \"shared\", \"version\": \"^0.1.0\", \"source\": \"path\", \"location\": \"../shared-package\"}\n"
    "  ]\n"
    "}\n")

run_vpp("${TRANSITIVE_PROJECT}" init transitive-app)
run_vpp("${TRANSITIVE_PROJECT}" install "${PARENT_DIR}")
if(NOT EXISTS "${TRANSITIVE_PROJECT}/gói/parent/main.vi" OR
   NOT EXISTS "${TRANSITIVE_PROJECT}/gói/shared/main.vi")
    message(FATAL_ERROR "transitive install did not materialize parent and shared packages")
endif()

# Run a real V++ consumer against the materialized graph. This verifies that
# packages installed by the 0.9 workflow are immediately resolvable by normal
# `nhập <package>` source code, rather than only checking copied files on disk.
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../src/tests/kiem_tra_package_09.vi"
     DESTINATION "${TRANSITIVE_PROJECT}")
run_vpp("${TRANSITIVE_PROJECT}" kiem_tra_package_09.vi)
string(REPLACE "\r\n" "\n" package_smoke_output "${LAST_STDOUT}")
if(NOT package_smoke_output STREQUAL "[IN] 22\n[IN] 11\n")
    message(FATAL_ERROR
        "installed package graph could not be consumed from V++ source:\n${LAST_STDOUT}")
endif()

run_vpp("${TRANSITIVE_PROJECT}" lock)
file(READ "${TRANSITIVE_PROJECT}/vpp.lock" transitive_lock)
string(FIND "${transitive_lock}" "\"name\": \"parent\"" parent_lock_entry)
string(FIND "${transitive_lock}" "\"name\": \"shared\"" shared_lock_entry)
if(parent_lock_entry EQUAL -1 OR shared_lock_entry EQUAL -1)
    message(FATAL_ERROR "transitive lockfile is missing graph packages:\n${transitive_lock}")
endif()

file(REMOVE_RECURSE "${TRANSITIVE_PROJECT}/gói/shared")
run_vpp("${TRANSITIVE_PROJECT}" sync)
if(NOT EXISTS "${TRANSITIVE_PROJECT}/gói/shared/main.vi")
    message(FATAL_ERROR "sync did not restore transitive dependency from manifest graph")
endif()

file(REMOVE_RECURSE "${TRANSITIVE_PROJECT}/gói/parent" "${TRANSITIVE_PROJECT}/gói/shared")
run_vpp("${TRANSITIVE_PROJECT}" restore)
if(NOT EXISTS "${TRANSITIVE_PROJECT}/gói/parent/main.vi" OR
   NOT EXISTS "${TRANSITIVE_PROJECT}/gói/shared/main.vi")
    message(FATAL_ERROR "lock restore did not recreate transitive dependency graph")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
message(STATUS "package CLI workflow passed")
