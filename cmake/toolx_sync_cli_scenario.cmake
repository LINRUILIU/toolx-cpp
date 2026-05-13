if(NOT DEFINED TOOLX_SYNC_EXE)
    message(FATAL_ERROR "TOOLX_SYNC_EXE is required")
endif()

if(NOT EXISTS "${TOOLX_SYNC_EXE}")
    message(FATAL_ERROR "toolx-sync executable not found: ${TOOLX_SYNC_EXE}")
endif()

set(test_root "${CMAKE_BINARY_DIR}/toolx_sync_cli_scenario")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(base_json "${test_root}/base.json")
set(out_json "${test_root}/resolved.json")
set(snapshot_json "${test_root}/snapshot.json")
set(journal_path "${test_root}/resolved.journal")
set(log_path "${test_root}/audit.log")

file(WRITE "${base_json}" [=[{"svc":{"host":"127.0.0.1","port":8080},"feature":{"enabled":true}}]=])

function(run_toolx_sync case_name expected_code)
    execute_process(
        COMMAND "${TOOLX_SYNC_EXE}" ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(NOT actual_code EQUAL expected_code)
        message(FATAL_ERROR
            "[${case_name}] expected exit ${expected_code}, got ${actual_code}\n"
            "command: ${TOOLX_SYNC_EXE} ${ARGN}\n"
            "stdout:\n${stdout}\n"
            "stderr:\n${stderr}")
    endif()

    set("${case_name}_OUT" "${stdout}" PARENT_SCOPE)
    set("${case_name}_ERR" "${stderr}" PARENT_SCOPE)
endfunction()

function(assert_contains case_name text needle)
    string(FIND "${text}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "[${case_name}] expected output to contain: ${needle}\nactual:\n${text}")
    endif()
endfunction()

run_toolx_sync(HELP 0 --help)
assert_contains(HELP "${HELP_OUT}" "toolx-sync - validate and atomically publish composed config")

run_toolx_sync(PUBLISH_JSON 0
    --base "${base_json}"
    --out "${out_json}"
    --snapshot "${snapshot_json}"
    --journal "${journal_path}"
    --log-file "${log_path}"
    --require svc.port
    --range svc.port=1:65535
    --json)
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"schema\": \"toolx.sync.result\"")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"ok\": true")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"out\": \"${out_json}\"")

if(NOT EXISTS "${out_json}")
    message(FATAL_ERROR "toolx-sync did not create ${out_json}")
endif()
if(NOT EXISTS "${snapshot_json}")
    message(FATAL_ERROR "toolx-sync did not create ${snapshot_json}")
endif()
if(NOT EXISTS "${log_path}")
    message(FATAL_ERROR "toolx-sync did not create ${log_path}")
endif()

file(READ "${out_json}" resolved_text)
assert_contains(PUBLISH_JSON_FILE "${resolved_text}" "\"port\": 8080")

run_toolx_sync(VALIDATION_FAIL 4
    --base "${base_json}"
    --out "${test_root}/invalid.json"
    --range svc.port=9000:9999
    --json)
assert_contains(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" "\"ok\": false")
assert_contains(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" "\"code\": 4")
