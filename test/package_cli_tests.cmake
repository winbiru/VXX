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

function(run_vpp_expect_failure working_directory)
    execute_process(
        COMMAND "${VPP_EXEC}" ${ARGN}
        WORKING_DIRECTORY "${working_directory}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    if(result EQUAL 0)
        message(FATAL_ERROR
            "vpp command unexpectedly succeeded: ${ARGN}\nstdout:\n${stdout}\nstderr:\n${stderr}")
    endif()
    set(LAST_STDERR "${stderr}" PARENT_SCOPE)
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

set(LOCKFILE "${PROJECT_DIR}/vpp.lock")
if(NOT EXISTS "${LOCKFILE}")
    message(FATAL_ERROR "package install did not create vpp.lock")
endif()
file(READ "${LOCKFILE}" install_lock)

run_vpp("${PROJECT_DIR}" list)
string(FIND "${LAST_STDOUT}" "dep-demo" listed_package)
if(listed_package EQUAL -1)
    message(FATAL_ERROR "package list does not include dep-demo: ${LAST_STDOUT}")
endif()

run_vpp("${PROJECT_DIR}" lock)
file(READ "${LOCKFILE}" first_lock)
if(NOT install_lock STREQUAL first_lock)
    message(FATAL_ERROR "explicit lock changed the deterministic lock produced by install")
endif()
string(FIND "${first_lock}" "fnv1a64:" fingerprint_offset)
if(fingerprint_offset EQUAL -1)
    message(FATAL_ERROR "lockfile does not contain a package fingerprint")
endif()
file(GLOB first_cache_entries "${PROJECT_DIR}/.vpp/cache/fnv1a64-*")
list(LENGTH first_cache_entries first_cache_count)
if(first_cache_count LESS 1)
    message(FATAL_ERROR "lock command did not snapshot package content into .vpp/cache")
endif()

run_vpp("${PROJECT_DIR}" lock)
file(READ "${LOCKFILE}" repeated_lock)
if(NOT first_lock STREQUAL repeated_lock)
    message(FATAL_ERROR "repeated lock changed deterministic lockfile bytes")
endif()

file(WRITE "${PROJECT_DIR}/app.vi"
    "nhập \"dep-demo\";\n\n"
    "hàm main() { in ping(); };\n")
run_vpp("${PROJECT_DIR}" app.vi)
if(NOT LAST_STDOUT STREQUAL "[IN] 7\n")
    message(FATAL_ERROR "locked project did not run expected package bytes: ${LAST_STDOUT}")
endif()

# Offline restore can rebuild a missing cache entry from an already-installed
# package only when its bytes still match the exact lock fingerprint.
file(REMOVE_RECURSE "${PROJECT_DIR}/.vpp/cache")
run_vpp("${PROJECT_DIR}" restore --offline)
file(GLOB rebuilt_cache_entries "${PROJECT_DIR}/.vpp/cache/fnv1a64-*")
list(LENGTH rebuilt_cache_entries rebuilt_cache_count)
if(rebuilt_cache_count LESS 1)
    message(FATAL_ERROR "offline restore did not rebuild cache from exact installed bytes")
endif()

# A locked project must not execute against vendored bytes that drifted from
# vpp.lock. Restore must bring it back to the accepted fingerprint.
file(APPEND "${INSTALLED_MAIN}" "// local drift\n")
run_vpp_expect_failure("${PROJECT_DIR}" app.vi)
string(FIND "${LAST_STDERR}" "không khớp vpp.lock" lock_mismatch_diagnostic)
if(lock_mismatch_diagnostic EQUAL -1)
    message(FATAL_ERROR "run did not explain locked package fingerprint mismatch:\n${LAST_STDERR}")
endif()
run_vpp("${PROJECT_DIR}" restore --offline)
run_vpp("${PROJECT_DIR}" app.vi)
if(NOT LAST_STDOUT STREQUAL "[IN] 7\n")
    message(FATAL_ERROR "restore did not make locked project runnable again")
endif()

file(WRITE "${SOURCE_DIR}/main.vi" "hàm ping() { trả về 8; }\n")
run_vpp("${PROJECT_DIR}" update)
file(READ "${INSTALLED_MAIN}" updated_main)
if(NOT updated_main STREQUAL "hàm ping() { trả về 8; }\n")
    message(FATAL_ERROR "update did not materialize changed source package")
endif()
file(READ "${LOCKFILE}" updated_lock)
if(first_lock STREQUAL updated_lock)
    message(FATAL_ERROR "update did not refresh lockfile fingerprint")
endif()
run_vpp("${PROJECT_DIR}" app.vi)
if(NOT LAST_STDOUT STREQUAL "[IN] 8\n")
    message(FATAL_ERROR "updated lock/package graph did not run new package bytes")
endif()

# Restore uses the exact bytes recorded by the original lock. The source is
# removed before restore so this must come from the content-addressed cache.
file(WRITE "${LOCKFILE}" "${first_lock}")
file(REMOVE_RECURSE "${PROJECT_DIR}/gói/dep-demo")
file(REMOVE_RECURSE "${SOURCE_DIR}")
run_vpp("${PROJECT_DIR}" restore --offline)
if(NOT EXISTS "${INSTALLED_MAIN}")
    message(FATAL_ERROR "offline restore did not recreate locked package from cache")
endif()
file(READ "${INSTALLED_MAIN}" restored_main)
if(NOT restored_main STREQUAL "hàm ping() { trả về 7; }\n")
    message(FATAL_ERROR "offline restore did not recover locked package bytes: ${restored_main}")
endif()

# `install` without a source is the reproducible lockfile install path. Verify
# it also works with the original source gone, using only the cache snapshot.
file(REMOVE_RECURSE "${PROJECT_DIR}/gói/dep-demo")
run_vpp("${PROJECT_DIR}" install --offline)
file(READ "${INSTALLED_MAIN}" installed_from_lock)
if(NOT installed_from_lock STREQUAL "hàm ping() { trả về 7; }\n")
    message(FATAL_ERROR "install --offline did not restore exact lockfile bytes")
endif()
run_vpp("${PROJECT_DIR}" app.vi)
if(NOT LAST_STDOUT STREQUAL "[IN] 7\n")
    message(FATAL_ERROR "offline lock install did not produce a runnable locked project")
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
file(READ "${LOCKFILE}" removed_lock)
string(FIND "${removed_lock}" "\"name\": \"dep-demo\"" stale_lock_dependency)
if(NOT stale_lock_dependency EQUAL -1)
    message(FATAL_ERROR "package remove left dependency in lockfile:\n${removed_lock}")
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

# Git transport uses a real local repository so the workflow exercises clone,
# symbolic ref resolution, exact commit locking, update, restore and cache-only
# offline restore without depending on network access.
find_program(GIT_EXECUTABLE git)
if(GIT_EXECUTABLE)
    set(GIT_SOURCE "${TEST_ROOT}/git-source")
    set(GIT_PROJECT "${TEST_ROOT}/git-project")
    file(MAKE_DIRECTORY "${GIT_SOURCE}" "${GIT_PROJECT}")
    run_vpp("${GIT_SOURCE}" init git-demo)
    file(WRITE "${GIT_SOURCE}/main.vi" "hàm git_ping() { trả về 31; }\n")

    execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" config user.email vpp-test@example.invalid
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" config user.name "V++ Package Test"
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" add .
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" commit --quiet -m initial
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" tag stable
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    OUTPUT_VARIABLE git_first_revision
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    COMMAND_ERROR_IS_FATAL ANY)

    run_vpp("${GIT_PROJECT}" init git-app)
    run_vpp("${GIT_PROJECT}" install "git+${GIT_SOURCE}#stable" git-demo)
    set(GIT_INSTALLED "${GIT_PROJECT}/gói/git-demo/main.vi")
    if(NOT EXISTS "${GIT_INSTALLED}")
        message(FATAL_ERROR "Git install did not materialize package")
    endif()
    if(EXISTS "${GIT_PROJECT}/gói/git-demo/.git")
        message(FATAL_ERROR "Git install leaked repository metadata into package artifact")
    endif()
    file(READ "${GIT_PROJECT}/vpp.json" git_manifest)
    string(FIND "${git_manifest}" "\"source\": \"git\"" git_manifest_source)
    string(FIND "${git_manifest}" "\"ref\": \"stable\"" git_manifest_ref)
    if(git_manifest_source EQUAL -1 OR git_manifest_ref EQUAL -1)
        message(FATAL_ERROR "Git dependency manifest did not preserve source/ref:\n${git_manifest}")
    endif()
    file(READ "${GIT_PROJECT}/vpp.lock" git_first_lock)
    string(FIND "${git_first_lock}" "\"revision\": \"${git_first_revision}\"" git_lock_revision)
    if(git_lock_revision EQUAL -1)
        message(FATAL_ERROR "Git lock did not pin exact first commit:\n${git_first_lock}")
    endif()
    file(COPY "${CMAKE_CURRENT_LIST_DIR}/../src/tests/kiem_tra_package_git.vi"
         DESTINATION "${GIT_PROJECT}")
    run_vpp("${GIT_PROJECT}" kiem_tra_package_git.vi)
    string(REPLACE "\r\n" "\n" git_package_output "${LAST_STDOUT}")
    if(NOT git_package_output STREQUAL "[IN] 31\n")
        message(FATAL_ERROR "Git-installed package was not consumable from V++ source:\n${LAST_STDOUT}")
    endif()

    # Mixed transitive graph: a local path package can depend on a Git package.
    # The same solver/materializer pipeline must install and lock both nodes.
    set(MIXED_PARENT "${TEST_ROOT}/mixed-parent")
    set(MIXED_PROJECT "${TEST_ROOT}/mixed-project")
    file(MAKE_DIRECTORY "${MIXED_PARENT}" "${MIXED_PROJECT}")
    run_vpp("${MIXED_PARENT}" init mixed-parent)
    file(WRITE "${MIXED_PARENT}/main.vi" "hàm mixed_parent() { trả về 41; }\n")
    file(WRITE "${MIXED_PARENT}/vpp.json"
        "{\n"
        "  \"schema\": 1,\n"
        "  \"name\": \"mixed-parent\",\n"
        "  \"version\": \"0.1.0\",\n"
        "  \"dependencies\": [\n"
        "    {\"name\": \"git-demo\", \"version\": \"0.1.0\", \"source\": \"git\", \"location\": \"${GIT_SOURCE}\", \"ref\": \"stable\"}\n"
        "  ]\n"
        "}\n")
    run_vpp("${MIXED_PROJECT}" init mixed-app)
    run_vpp("${MIXED_PROJECT}" install "${MIXED_PARENT}")
    if(NOT EXISTS "${MIXED_PROJECT}/gói/mixed-parent/main.vi" OR
       NOT EXISTS "${MIXED_PROJECT}/gói/git-demo/main.vi")
        message(FATAL_ERROR "mixed path -> Git graph did not materialize both packages")
    endif()
    file(READ "${MIXED_PROJECT}/vpp.lock" mixed_lock)
    string(FIND "${mixed_lock}" "\"name\": \"git-demo\"" mixed_git_entry)
    string(FIND "${mixed_lock}" "\"revision\": \"${git_first_revision}\"" mixed_git_revision)
    if(mixed_git_entry EQUAL -1 OR mixed_git_revision EQUAL -1)
        message(FATAL_ERROR "mixed path -> Git graph did not lock exact Git revision:\n${mixed_lock}")
    endif()

    # Missing Git must surface a deterministic transport diagnostic instead of
    # looking like a package/version failure.
    set(NO_GIT_PROJECT "${TEST_ROOT}/no-git-project")
    file(MAKE_DIRECTORY "${NO_GIT_PROJECT}")
    run_vpp("${NO_GIT_PROJECT}" init no-git-app)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "PATH=" "${VPP_EXEC}"
                install "git+${GIT_SOURCE}#stable" git-demo
        WORKING_DIRECTORY "${NO_GIT_PROJECT}"
        RESULT_VARIABLE no_git_result
        OUTPUT_VARIABLE no_git_stdout
        ERROR_VARIABLE no_git_stderr)
    if(no_git_result EQUAL 0)
        message(FATAL_ERROR "Git install unexpectedly succeeded with empty PATH")
    endif()
    string(TOLOWER "${no_git_stderr}" no_git_stderr_lower)
    string(FIND "${no_git_stderr_lower}" "git" no_git_word)
    string(FIND "${no_git_stderr_lower}" "path" no_git_path_word)
    if(no_git_word EQUAL -1 OR no_git_path_word EQUAL -1)
        message(FATAL_ERROR "missing Git diagnostic does not mention git/PATH:\n${no_git_stderr}")
    endif()

    # Move the symbolic ref and verify `update` resolves it again while lock keeps
    # the newly selected immutable commit.
    file(WRITE "${GIT_SOURCE}/main.vi" "hàm git_ping() { trả về 32; }\n")
    execute_process(COMMAND "${GIT_EXECUTABLE}" add main.vi
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" commit --quiet -m second
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" tag --force stable
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse HEAD
                    WORKING_DIRECTORY "${GIT_SOURCE}"
                    OUTPUT_VARIABLE git_second_revision
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    COMMAND_ERROR_IS_FATAL ANY)

    # `lock` must never pair the moved Git revision with stale installed bytes.
    # It fails and leaves the previous lock untouched until update/sync
    # materializes the newly resolved source.
    run_vpp_expect_failure("${GIT_PROJECT}" lock)
    string(FIND "${LAST_STDERR}" "bytes đã cài không khớp source vừa resolve" stale_git_lock_diagnostic)
    if(stale_git_lock_diagnostic EQUAL -1)
        message(FATAL_ERROR "lock did not explain stale Git artifact/source mismatch:\n${LAST_STDERR}")
    endif()
    file(READ "${GIT_PROJECT}/vpp.lock" git_lock_after_rejected_lock)
    if(NOT git_lock_after_rejected_lock STREQUAL git_first_lock)
        message(FATAL_ERROR "failed Git lock mutated the previous valid lockfile")
    endif()
    file(READ "${GIT_INSTALLED}" git_installed_after_rejected_lock)
    if(NOT git_installed_after_rejected_lock STREQUAL "hàm git_ping() { trả về 31; }\n")
        message(FATAL_ERROR "failed Git lock mutated installed package bytes")
    endif()

    run_vpp("${GIT_PROJECT}" update)
    file(READ "${GIT_INSTALLED}" git_updated_main)
    if(NOT git_updated_main STREQUAL "hàm git_ping() { trả về 32; }\n")
        message(FATAL_ERROR "Git update did not materialize moved ref")
    endif()
    file(READ "${GIT_PROJECT}/vpp.lock" git_second_lock)
    string(FIND "${git_second_lock}" "\"revision\": \"${git_second_revision}\"" git_second_lock_revision)
    if(git_second_lock_revision EQUAL -1 OR git_first_lock STREQUAL git_second_lock)
        message(FATAL_ERROR "Git update did not refresh exact revision/fingerprint lock")
    endif()
    run_vpp("${GIT_PROJECT}" kiem_tra_package_git.vi)
    string(REPLACE "\r\n" "\n" git_updated_output "${LAST_STDOUT}")
    if(NOT git_updated_output STREQUAL "[IN] 32\n")
        message(FATAL_ERROR "updated Git package was not consumable from V++ source:\n${LAST_STDOUT}")
    endif()

    # Restore the original lock after its tag has moved. Removing cache for the
    # first fingerprint forces restore to fetch/check out the exact locked commit.
    file(WRITE "${GIT_PROJECT}/vpp.lock" "${git_first_lock}")
    file(REMOVE_RECURSE "${GIT_PROJECT}/gói/git-demo" "${GIT_PROJECT}/.vpp/cache")
    run_vpp("${GIT_PROJECT}" restore)
    file(READ "${GIT_INSTALLED}" git_restored_main)
    if(NOT git_restored_main STREQUAL "hàm git_ping() { trả về 31; }\n")
        message(FATAL_ERROR "Git restore did not checkout exact locked commit")
    endif()
    run_vpp("${GIT_PROJECT}" kiem_tra_package_git.vi)
    string(REPLACE "\r\n" "\n" git_restored_output "${LAST_STDOUT}")
    if(NOT git_restored_output STREQUAL "[IN] 31\n")
        message(FATAL_ERROR "restored Git package was not consumable from V++ source:\n${LAST_STDOUT}")
    endif()

    # Once restored, cache must be sufficient even if the original repository is
    # gone and all source checkouts are removed.
    file(REMOVE_RECURSE "${GIT_SOURCE}" "${GIT_PROJECT}/gói/git-demo" "${GIT_PROJECT}/.vpp/sources")
    run_vpp("${GIT_PROJECT}" restore --offline)
    file(READ "${GIT_INSTALLED}" git_offline_main)
    if(NOT git_offline_main STREQUAL "hàm git_ping() { trả về 31; }\n")
        message(FATAL_ERROR "Git offline restore did not use exact cached package bytes")
    endif()
    run_vpp("${GIT_PROJECT}" kiem_tra_package_git.vi)
    string(REPLACE "\r\n" "\n" git_offline_output "${LAST_STDOUT}")
    if(NOT git_offline_output STREQUAL "[IN] 31\n")
        message(FATAL_ERROR "offline-restored Git package was not consumable from V++ source:\n${LAST_STDOUT}")
    endif()

    # Invalid refs fail before manifest/lock mutation.
    set(BAD_GIT_SOURCE "${TEST_ROOT}/bad-git-source")
    file(MAKE_DIRECTORY "${BAD_GIT_SOURCE}")
    run_vpp("${BAD_GIT_SOURCE}" init bad-git)
    file(WRITE "${BAD_GIT_SOURCE}/main.vi" "hàm bad_git() { trả về 1; }\n")
    execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet
                    WORKING_DIRECTORY "${BAD_GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" config user.email vpp-test@example.invalid
                    WORKING_DIRECTORY "${BAD_GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" config user.name "V++ Package Test"
                    WORKING_DIRECTORY "${BAD_GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" add .
                    WORKING_DIRECTORY "${BAD_GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    execute_process(COMMAND "${GIT_EXECUTABLE}" commit --quiet -m initial
                    WORKING_DIRECTORY "${BAD_GIT_SOURCE}"
                    COMMAND_ERROR_IS_FATAL ANY)
    run_vpp_expect_failure("${GIT_PROJECT}" install "git+${BAD_GIT_SOURCE}#missing-ref" bad-git)
    string(FIND "${LAST_STDERR}" "git resolve ref 'missing-ref'" invalid_ref_diagnostic)
    if(invalid_ref_diagnostic EQUAL -1)
        message(FATAL_ERROR "invalid Git ref diagnostic is unclear:\n${LAST_STDERR}")
    endif()
else()
    message(STATUS "git executable not found; Git package transport smoke skipped")
endif()

# Filesystem registry v1: publish immutable versions, select the highest SemVer
# matching a range, pin exact version/fingerprint in lock, restore exact locked
# bytes, and remain installable from cache in offline mode.
set(REGISTRY_ROOT "${TEST_ROOT}/registry")
set(REGISTRY_SOURCE "${TEST_ROOT}/registry-source")
set(REGISTRY_PROJECT "${TEST_ROOT}/registry-project")
file(MAKE_DIRECTORY "${REGISTRY_SOURCE}" "${REGISTRY_PROJECT}")
run_vpp("${REGISTRY_SOURCE}" init registry-demo)
file(WRITE "${REGISTRY_SOURCE}/vpp.json"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"name\": \"registry-demo\",\n"
    "  \"version\": \"1.0.0\",\n"
    "  \"dependencies\": []\n"
    "}\n")
file(WRITE "${REGISTRY_SOURCE}/main.vi" "hàm registry_ping() { trả về 51; }\n")
run_vpp("${REGISTRY_SOURCE}" publish "${REGISTRY_ROOT}")
if(NOT EXISTS "${REGISTRY_ROOT}/vpp-registry.json" OR
   NOT EXISTS "${REGISTRY_ROOT}/registry-demo/1.0.0/main.vi")
    message(FATAL_ERROR "registry publish did not create marker/version artifact")
endif()

# Publishing the same bytes/version is idempotent, while replacing published
# bytes under the same version is rejected.
run_vpp("${REGISTRY_SOURCE}" publish "${REGISTRY_ROOT}")
file(WRITE "${REGISTRY_SOURCE}/main.vi" "hàm registry_ping() { trả về 52; }\n")
run_vpp_expect_failure("${REGISTRY_SOURCE}" publish "${REGISTRY_ROOT}")
string(FIND "${LAST_STDERR}" "version đã publish là bất biến" registry_immutable_diagnostic)
if(registry_immutable_diagnostic EQUAL -1)
    message(FATAL_ERROR "registry did not explain immutable version rejection:\n${LAST_STDERR}")
endif()

# A new version can be published and range install must select the highest
# compatible version rather than the first directory encountered.
file(WRITE "${REGISTRY_SOURCE}/vpp.json"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"name\": \"registry-demo\",\n"
    "  \"version\": \"1.2.0\",\n"
    "  \"dependencies\": []\n"
    "}\n")
run_vpp("${REGISTRY_SOURCE}" publish "${REGISTRY_ROOT}")
run_vpp("${REGISTRY_PROJECT}" init registry-app)
run_vpp("${REGISTRY_PROJECT}" install
        "registry+${REGISTRY_ROOT}#registry-demo@^1.0.0")
set(REGISTRY_INSTALLED "${REGISTRY_PROJECT}/gói/registry-demo/main.vi")
file(READ "${REGISTRY_INSTALLED}" registry_installed_main)
if(NOT registry_installed_main STREQUAL "hàm registry_ping() { trả về 52; }\n")
    message(FATAL_ERROR "registry install did not select highest ^1.0.0 version")
endif()
file(READ "${REGISTRY_PROJECT}/vpp.json" registry_project_manifest)
string(FIND "${registry_project_manifest}" "\"source\": \"registry\"" registry_manifest_source)
string(FIND "${registry_project_manifest}" "\"version\": \"^1.0.0\"" registry_manifest_range)
if(registry_manifest_source EQUAL -1 OR registry_manifest_range EQUAL -1)
    message(FATAL_ERROR "registry install did not preserve source/range in manifest:\n${registry_project_manifest}")
endif()
file(READ "${REGISTRY_PROJECT}/vpp.lock" registry_first_lock)
string(FIND "${registry_first_lock}" "\"version\": \"1.2.0\"" registry_lock_version)
if(registry_lock_version EQUAL -1)
    message(FATAL_ERROR "registry lock did not pin selected 1.2.0 version:\n${registry_first_lock}")
endif()
file(COPY "${CMAKE_CURRENT_LIST_DIR}/../src/tests/kiem_tra_package_registry.vi"
     DESTINATION "${REGISTRY_PROJECT}")
run_vpp("${REGISTRY_PROJECT}" kiem_tra_package_registry.vi)
string(REPLACE "\r\n" "\n" registry_package_output "${LAST_STDOUT}")
if(NOT registry_package_output STREQUAL "[IN] 52\n")
    message(FATAL_ERROR "registry-installed package was not consumable from V++ source:\n${LAST_STDOUT}")
endif()

# `registry:<name>@<range>` uses VPP_REGISTRY for a configurable default root.
set(REGISTRY_ENV_PROJECT "${TEST_ROOT}/registry-env-project")
file(MAKE_DIRECTORY "${REGISTRY_ENV_PROJECT}")
run_vpp("${REGISTRY_ENV_PROJECT}" init registry-env-app)
execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env "VPP_REGISTRY=${REGISTRY_ROOT}"
            "${VPP_EXEC}" install "registry:registry-demo@^1.0.0"
    WORKING_DIRECTORY "${REGISTRY_ENV_PROJECT}"
    RESULT_VARIABLE registry_env_result
    OUTPUT_VARIABLE registry_env_stdout
    ERROR_VARIABLE registry_env_stderr)
if(NOT registry_env_result EQUAL 0 OR
   NOT EXISTS "${REGISTRY_ENV_PROJECT}/gói/registry-demo/main.vi")
    message(FATAL_ERROR
        "registry install through VPP_REGISTRY failed:\n${registry_env_stdout}\n${registry_env_stderr}")
endif()

# No matching version must fail with a range-specific diagnostic before it can
# mutate the project manifest or lock.
set(REGISTRY_NO_MATCH_PROJECT "${TEST_ROOT}/registry-no-match")
file(MAKE_DIRECTORY "${REGISTRY_NO_MATCH_PROJECT}")
run_vpp("${REGISTRY_NO_MATCH_PROJECT}" init no-match-app)
run_vpp_expect_failure("${REGISTRY_NO_MATCH_PROJECT}" install
                       "registry+${REGISTRY_ROOT}#registry-demo@^9.0.0")
string(FIND "${LAST_STDERR}" "không có version" registry_no_match_diagnostic)
if(registry_no_match_diagnostic EQUAL -1)
    message(FATAL_ERROR "registry missing-version diagnostic is unclear:\n${LAST_STDERR}")
endif()

# Publishing a newer compatible version makes update select it. A plain lock
# must refuse the stale installed artifact/version until update materializes it.
file(WRITE "${REGISTRY_SOURCE}/vpp.json"
    "{\n"
    "  \"schema\": 1,\n"
    "  \"name\": \"registry-demo\",\n"
    "  \"version\": \"1.3.0\",\n"
    "  \"dependencies\": []\n"
    "}\n")
file(WRITE "${REGISTRY_SOURCE}/main.vi" "hàm registry_ping() { trả về 53; }\n")
run_vpp("${REGISTRY_SOURCE}" publish "${REGISTRY_ROOT}")
run_vpp_expect_failure("${REGISTRY_PROJECT}" lock)
string(FIND "${LAST_STDERR}" "bộ phân giải yêu cầu 1.3.0" registry_stale_lock_diagnostic)
if(registry_stale_lock_diagnostic EQUAL -1)
    message(FATAL_ERROR "registry lock did not reject stale installed version:\n${LAST_STDERR}")
endif()
file(READ "${REGISTRY_PROJECT}/vpp.lock" registry_lock_after_rejected_lock)
if(NOT registry_lock_after_rejected_lock STREQUAL registry_first_lock)
    message(FATAL_ERROR "failed registry lock mutated previous valid lockfile")
endif()

run_vpp("${REGISTRY_PROJECT}" update)
file(READ "${REGISTRY_INSTALLED}" registry_updated_main)
if(NOT registry_updated_main STREQUAL "hàm registry_ping() { trả về 53; }\n")
    message(FATAL_ERROR "registry update did not materialize newly selected 1.3.0")
endif()
file(READ "${REGISTRY_PROJECT}/vpp.lock" registry_second_lock)
string(FIND "${registry_second_lock}" "\"version\": \"1.3.0\"" registry_second_lock_version)
if(registry_second_lock_version EQUAL -1 OR registry_first_lock STREQUAL registry_second_lock)
    message(FATAL_ERROR "registry update did not refresh exact version/fingerprint lock")
endif()
run_vpp("${REGISTRY_PROJECT}" kiem_tra_package_registry.vi)
string(REPLACE "\r\n" "\n" registry_updated_output "${LAST_STDOUT}")
if(NOT registry_updated_output STREQUAL "[IN] 53\n")
    message(FATAL_ERROR "updated registry package was not consumable from V++ source")
endif()

# Restore the old exact version from registry after cache removal, then prove
# the refreshed cache is enough when the entire registry disappears.
file(WRITE "${REGISTRY_PROJECT}/vpp.lock" "${registry_first_lock}")
file(REMOVE_RECURSE "${REGISTRY_PROJECT}/gói/registry-demo" "${REGISTRY_PROJECT}/.vpp/cache")
run_vpp("${REGISTRY_PROJECT}" restore)
file(READ "${REGISTRY_INSTALLED}" registry_restored_main)
if(NOT registry_restored_main STREQUAL "hàm registry_ping() { trả về 52; }\n")
    message(FATAL_ERROR "registry restore did not materialize exact locked 1.2.0")
endif()
file(REMOVE_RECURSE "${REGISTRY_ROOT}" "${REGISTRY_PROJECT}/gói/registry-demo")
run_vpp("${REGISTRY_PROJECT}" restore --offline)
file(READ "${REGISTRY_INSTALLED}" registry_offline_main)
if(NOT registry_offline_main STREQUAL "hàm registry_ping() { trả về 52; }\n")
    message(FATAL_ERROR "registry offline restore did not use exact cached package bytes")
endif()
run_vpp("${REGISTRY_PROJECT}" kiem_tra_package_registry.vi)
string(REPLACE "\r\n" "\n" registry_offline_output "${LAST_STDOUT}")
if(NOT registry_offline_output STREQUAL "[IN] 52\n")
    message(FATAL_ERROR "offline-restored registry package was not consumable from V++ source")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
message(STATUS "package CLI workflow passed")
