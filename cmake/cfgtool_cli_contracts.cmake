if(NOT DEFINED CFGTOOL_EXE)
    message(FATAL_ERROR "CFGTOOL_EXE is required")
endif()

if(NOT EXISTS "${CFGTOOL_EXE}")
    message(FATAL_ERROR "cfgtool executable not found: ${CFGTOOL_EXE}")
endif()

set(test_root "${CMAKE_BINARY_DIR}/cfgtool_cli_contracts")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(app_json "${test_root}/app.json")
set(snapshot_json "${test_root}/snapshot.json")
set(restored_json "${test_root}/restored.json")
set(base_json "${test_root}/base.json")
set(overlay_json "${test_root}/overlay.json")
set(merged_json "${test_root}/merged.json")
set(candidate_json "${test_root}/candidate.json")
set(invalid_json "${test_root}/invalid.json")
set(schema_json "${test_root}/schema.json")
set(bad_schema_app_json "${test_root}/bad-schema-app.json")

file(WRITE "${app_json}" [=[{"svc":{"host":"127.0.0.1","port":8080},"tags":["base"]}]=])
file(WRITE "${base_json}" [=[{"svc":{"port":8080,"mode":"base"},"tags":["base"]}]=])
file(WRITE "${overlay_json}" [=[{"svc":{"mode":"overlay"},"tags":["overlay"]}]=])
file(WRITE "${candidate_json}" [=[{"svc":{"host":"127.0.0.1","port":8081},"tags":["base"]}]=])
file(WRITE "${invalid_json}" [=[{"svc":]=])
file(WRITE "${schema_json}" [=[{"type":"object","required":["svc"],"properties":{"svc":{"type":"object","required":["host","port"],"properties":{"host":{"type":"string"},"port":{"type":"integer","minimum":1,"maximum":65535}}}}}]=])
file(WRITE "${bad_schema_app_json}" [=[{"svc":{"host":"127.0.0.1","port":70000}}]=])

function(run_cfgtool case_name expected_code)
    execute_process(
        COMMAND "${CFGTOOL_EXE}" ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(NOT actual_code EQUAL expected_code)
        message(FATAL_ERROR
            "[${case_name}] expected exit ${expected_code}, got ${actual_code}\n"
            "command: ${CFGTOOL_EXE} ${ARGN}\n"
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

run_cfgtool(HELP 0 --help)
assert_contains(HELP "${HELP_OUT}" "cfgtool - thin CLI over cfgx")
assert_contains(HELP "${HELP_OUT}" "reload-dryrun")
assert_contains(HELP "${HELP_OUT}" "doctor")

run_cfgtool(ADAPTERS_PLAIN 0 adapters)
assert_contains(ADAPTERS_PLAIN "${ADAPTERS_PLAIN_OUT}" "count=")

run_cfgtool(ADAPTERS_JSON 0 adapters --json)
assert_contains(ADAPTERS_JSON "${ADAPTERS_JSON_OUT}" "\"schema\": \"cfgtool.result\"")
assert_contains(ADAPTERS_JSON "${ADAPTERS_JSON_OUT}" "\"schema_version\": 2")
assert_contains(ADAPTERS_JSON "${ADAPTERS_JSON_OUT}" "\"command\": \"adapters\"")

run_cfgtool(LOAD_PLAIN 0 load --file "${app_json}")
assert_contains(LOAD_PLAIN "${LOAD_PLAIN_OUT}" "format=json")
assert_contains(LOAD_PLAIN "${LOAD_PLAIN_OUT}" "root_kind=object")

run_cfgtool(LOAD_JSON 0 load --file "${app_json}" --json)
assert_contains(LOAD_JSON "${LOAD_JSON_OUT}" "\"schema\": \"cfgtool.result\"")
assert_contains(LOAD_JSON "${LOAD_JSON_OUT}" "\"config\"")

run_cfgtool(DOCTOR_PLAIN 0 doctor --file "${app_json}" --require svc.host --expect svc.port=int)
assert_contains(DOCTOR_PLAIN "${DOCTOR_PLAIN_OUT}" "doctor.ok=true")
assert_contains(DOCTOR_PLAIN "${DOCTOR_PLAIN_OUT}" "doctor.check[1].name=")

run_cfgtool(DOCTOR_JSON 0 doctor --file "${app_json}" --require svc.host --expect svc.port=int --json)
assert_contains(DOCTOR_JSON "${DOCTOR_JSON_OUT}" "\"message\": \"doctor passed\"")
assert_contains(DOCTOR_JSON "${DOCTOR_JSON_OUT}" "\"checks\"")

run_cfgtool(DOCTOR_SCHEMA_JSON 0 doctor --file "${app_json}" --schema "${schema_json}" --json)
assert_contains(DOCTOR_SCHEMA_JSON "${DOCTOR_SCHEMA_JSON_OUT}" "\"schema_issues\"")

run_cfgtool(DOCTOR_MISSING_FILE 3 doctor --file "${test_root}/missing.json" --json)
assert_contains(DOCTOR_MISSING_FILE "${DOCTOR_MISSING_FILE_OUT}" "\"code\": 3")
assert_contains(DOCTOR_MISSING_FILE "${DOCTOR_MISSING_FILE_OUT}" "\"recommendations\"")

run_cfgtool(DOCTOR_PARSE_FAIL 1 doctor --file "${invalid_json}" --json)
assert_contains(DOCTOR_PARSE_FAIL "${DOCTOR_PARSE_FAIL_OUT}" "\"code\": 1")
assert_contains(DOCTOR_PARSE_FAIL "${DOCTOR_PARSE_FAIL_OUT}" "\"checks\"")

run_cfgtool(DOCTOR_REQUIRE_FAIL 4 doctor --file "${app_json}" --require svc.missing --json)
assert_contains(DOCTOR_REQUIRE_FAIL "${DOCTOR_REQUIRE_FAIL_OUT}" "\"code\": 4")
assert_contains(DOCTOR_REQUIRE_FAIL "${DOCTOR_REQUIRE_FAIL_OUT}" "\"issues\"")

run_cfgtool(DOCTOR_EXPECT_FAIL 4 doctor --file "${app_json}" --expect svc.port=string --json)
assert_contains(DOCTOR_EXPECT_FAIL "${DOCTOR_EXPECT_FAIL_OUT}" "\"code\": 4")
assert_contains(DOCTOR_EXPECT_FAIL "${DOCTOR_EXPECT_FAIL_OUT}" "\"recommendations\"")

run_cfgtool(GET_PLAIN 0 get --file "${app_json}" --path svc.port)
assert_contains(GET_PLAIN "${GET_PLAIN_OUT}" "8080")

run_cfgtool(GET_JSON 0 get --file "${app_json}" --path svc.port --json)
assert_contains(GET_JSON "${GET_JSON_OUT}" "\"value\": 8080")

run_cfgtool(EXISTS_TRUE 0 exists --file "${app_json}" --path svc.host)
assert_contains(EXISTS_TRUE "${EXISTS_TRUE_OUT}" "true")

run_cfgtool(EXISTS_FALSE 3 exists --file "${app_json}" --path svc.missing)
assert_contains(EXISTS_FALSE "${EXISTS_FALSE_OUT}" "false")

run_cfgtool(SNAPSHOT_EXPORT 0 snapshot-export --file "${app_json}" --out "${snapshot_json}" --json)
assert_contains(SNAPSHOT_EXPORT "${SNAPSHOT_EXPORT_OUT}" "\"command\": \"snapshot-export\"")
if(NOT EXISTS "${snapshot_json}")
    message(FATAL_ERROR "snapshot-export did not create ${snapshot_json}")
endif()

run_cfgtool(SET_PLAIN 0 set --file "${app_json}" --path svc.port --value 9443 --type int)
assert_contains(SET_PLAIN "${SET_PLAIN_OUT}" "ok")

run_cfgtool(GET_AFTER_SET 0 get --file "${app_json}" --path svc.port)
assert_contains(GET_AFTER_SET "${GET_AFTER_SET_OUT}" "9443")

run_cfgtool(SNAPSHOT_RESTORE 0 snapshot-restore --file "${app_json}" --snapshot "${snapshot_json}" --out "${restored_json}" --json)
assert_contains(SNAPSHOT_RESTORE "${SNAPSHOT_RESTORE_OUT}" "\"command\": \"snapshot-restore\"")
run_cfgtool(GET_RESTORED 0 get --file "${restored_json}" --path svc.port)
assert_contains(GET_RESTORED "${GET_RESTORED_OUT}" "8080")

run_cfgtool(MERGE_JSON 0 merge --base "${base_json}" --overlay "${overlay_json}" --out "${merged_json}" --json)
assert_contains(MERGE_JSON "${MERGE_JSON_OUT}" "\"command\": \"merge\"")
run_cfgtool(GET_MERGED 0 get --file "${merged_json}" --path svc.mode)
assert_contains(GET_MERGED "${GET_MERGED_OUT}" "overlay")

run_cfgtool(VALIDATE_PASS 0 validate --file "${app_json}" --require svc.port --expect svc.port=int --range svc.port=1:65535 --json)
assert_contains(VALIDATE_PASS "${VALIDATE_PASS_OUT}" "\"message\": \"validation passed\"")

run_cfgtool(VALIDATE_FAIL 4 validate --file "${app_json}" --range svc.port=1:10)
assert_contains(VALIDATE_FAIL "${VALIDATE_FAIL_OUT}" "issues=1")

run_cfgtool(VALIDATE_SCHEMA_PASS 0 validate --file "${app_json}" --schema "${schema_json}" --json)
assert_contains(VALIDATE_SCHEMA_PASS "${VALIDATE_SCHEMA_PASS_OUT}" "\"schema_issues\"")

run_cfgtool(VALIDATE_SCHEMA_FAIL 4 validate --file "${bad_schema_app_json}" --schema "${schema_json}" --json)
assert_contains(VALIDATE_SCHEMA_FAIL "${VALIDATE_SCHEMA_FAIL_OUT}" "\"code\": \"maximum\"")

run_cfgtool(RELOAD_DRYRUN 0 reload-dryrun --current "${restored_json}" --candidate "${candidate_json}" --range svc.port=1:65535 --json)
assert_contains(RELOAD_DRYRUN "${RELOAD_DRYRUN_OUT}" "\"command\": \"reload-dryrun\"")
assert_contains(RELOAD_DRYRUN "${RELOAD_DRYRUN_OUT}" "\"changed\": true")
