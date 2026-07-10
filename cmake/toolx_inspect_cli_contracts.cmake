if(NOT DEFINED TOOLX_INSPECT_EXE)
    message(FATAL_ERROR "TOOLX_INSPECT_EXE is required")
endif()

if(NOT EXISTS "${TOOLX_INSPECT_EXE}")
    message(FATAL_ERROR "toolx-inspect executable not found: ${TOOLX_INSPECT_EXE}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/toolx_cli_contract_helpers.cmake")

set(test_root "${CMAKE_BINARY_DIR}/toolx_inspect_cli_contracts")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(app_json "${test_root}/app.json")
set(bad_app_json "${test_root}/bad-app.json")
set(schema_json "${test_root}/schema.json")
set(missing_file "${test_root}/missing.json")
set(manifest_json "${test_root}/inspect.json")
set(override_manifest_json "${test_root}/inspect-override.json")
set(unknown_manifest_json "${test_root}/unknown.json")
set(malformed_manifest_json "${test_root}/malformed.json")

file(WRITE "${app_json}" [=[{"svc":{"host":"127.0.0.1","port":8080},"tags":["base"]}]=])
file(WRITE "${bad_app_json}" [=[{"svc":{"host":"127.0.0.1","port":70000},"tags":["base"]}]=])
file(WRITE "${schema_json}" [=[{"type":"object","required":["svc"],"properties":{"svc":{"type":"object","required":["host","port"],"properties":{"host":{"type":"string","minLength":1},"port":{"type":"integer","minimum":1,"maximum":65535}}}}}]=])
file(WRITE "${manifest_json}" [=[{"file":"]=] "${app_json}" [=[","schema":"]=] "${schema_json}" [=[","contains":"svc","max_paths":10,"max_issues":10,"focus":"issues","width":80,"height":16}]=])
file(WRITE "${override_manifest_json}" [=[{"file":"]=] "${app_json}" [=[","contains":"svc","path":"svc.host"}]=])
file(WRITE "${unknown_manifest_json}" [=[{"file":"]=] "${app_json}" [=[","unknown":true}]=])
file(WRITE "${malformed_manifest_json}" [=[{"file":]=])

function(run_toolx_inspect case_name expected_code)
    execute_process(
        COMMAND "${TOOLX_INSPECT_EXE}" ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(NOT actual_code EQUAL expected_code)
        message(FATAL_ERROR
            "[${case_name}] expected exit ${expected_code}, got ${actual_code}\n"
            "command: ${TOOLX_INSPECT_EXE} ${ARGN}\n"
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

function(assert_inspect_data_fields case_name text)
    foreach(field IN ITEMS
        command
        file
        schema_file
        manifest
        format
        root_kind
        path_count
        matched_path_count
        scalar_count
        object_count
        array_count
        selected_path
        selected_kind
        selected_value
        schema_issue_count
        schema_issues
        paths
        frame
        capabilities
        warnings)
        toolx_assert_json_path("${case_name}" "${text}" data "${field}")
    endforeach()
endfunction()

run_toolx_inspect(HELP 0 --help)
assert_contains(HELP "${HELP_OUT}" "toolx-inspect - inspect config files and schema issues")
assert_contains(HELP "${HELP_OUT}" "report")
assert_contains(HELP "${HELP_OUT}" "render")

run_toolx_inspect(MISSING_INPUT 2 report --json)
assert_contains(MISSING_INPUT "${MISSING_INPUT_OUT}" "\"code\": 2")
toolx_assert_json_envelope(MISSING_INPUT "${MISSING_INPUT_OUT}" "toolx.inspect.result" false 2)

run_toolx_inspect(MISSING_CONFIG 3 report --file "${missing_file}" --json)
assert_contains(MISSING_CONFIG "${MISSING_CONFIG_OUT}" "\"code\": 3")
toolx_assert_json_envelope(MISSING_CONFIG "${MISSING_CONFIG_OUT}" "toolx.inspect.result" false 3)

run_toolx_inspect(MISSING_SCHEMA 3 report --file "${app_json}" --schema "${missing_file}" --json)
assert_contains(MISSING_SCHEMA "${MISSING_SCHEMA_OUT}" "\"code\": 3")
toolx_assert_json_envelope(MISSING_SCHEMA "${MISSING_SCHEMA_OUT}" "toolx.inspect.result" false 3)

run_toolx_inspect(MISSING_MANIFEST 3 report --manifest "${missing_file}" --json)
assert_contains(MISSING_MANIFEST "${MISSING_MANIFEST_OUT}" "\"code\": 3")
toolx_assert_json_envelope(MISSING_MANIFEST "${MISSING_MANIFEST_OUT}" "toolx.inspect.result" false 3)

run_toolx_inspect(MALFORMED_MANIFEST 4 report --manifest "${malformed_manifest_json}" --json)
assert_contains(MALFORMED_MANIFEST "${MALFORMED_MANIFEST_OUT}" "\"code\": 4")
toolx_assert_json_envelope(MALFORMED_MANIFEST "${MALFORMED_MANIFEST_OUT}" "toolx.inspect.result" false 4)

run_toolx_inspect(UNKNOWN_MANIFEST 4 report --manifest "${unknown_manifest_json}" --json)
assert_contains(UNKNOWN_MANIFEST "${UNKNOWN_MANIFEST_OUT}" "\"code\": 4")
toolx_assert_json_envelope(UNKNOWN_MANIFEST "${UNKNOWN_MANIFEST_OUT}" "toolx.inspect.result" false 4)

run_toolx_inspect(REPORT_JSON 0 report --file "${app_json}" --schema "${schema_json}" --json)
assert_contains(REPORT_JSON "${REPORT_JSON_OUT}" "\"schema\": \"toolx.inspect.result\"")
assert_contains(REPORT_JSON "${REPORT_JSON_OUT}" "\"root_kind\": \"object\"")
assert_contains(REPORT_JSON "${REPORT_JSON_OUT}" "\"schema_issue_count\": 0")
toolx_assert_json_envelope(REPORT_JSON "${REPORT_JSON_OUT}" "toolx.inspect.result" true 0)
toolx_assert_json_value(REPORT_JSON "${REPORT_JSON_OUT}" "report" data command)
toolx_assert_json_value(REPORT_JSON "${REPORT_JSON_OUT}" "${app_json}" data file)
toolx_assert_json_value(REPORT_JSON "${REPORT_JSON_OUT}" "${schema_json}" data schema_file)
toolx_assert_json_value(REPORT_JSON "${REPORT_JSON_OUT}" "object" data root_kind)
toolx_assert_json_value(REPORT_JSON "${REPORT_JSON_OUT}" "0" data schema_issue_count)
assert_inspect_data_fields(REPORT_JSON "${REPORT_JSON_OUT}")

run_toolx_inspect(REPORT_PLAIN 0 report --file "${app_json}" --schema "${schema_json}")
assert_contains(REPORT_PLAIN "${REPORT_PLAIN_OUT}" "file=${app_json}")
assert_contains(REPORT_PLAIN "${REPORT_PLAIN_OUT}" "schema_issues=0")

run_toolx_inspect(SCHEMA_FAIL 4 report --file "${bad_app_json}" --schema "${schema_json}" --json)
assert_contains(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}" "\"ok\": false")
assert_contains(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}" "\"schema_issue_count\": 1")
toolx_assert_json_envelope(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}" "toolx.inspect.result" false 4)
assert_inspect_data_fields(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}")

run_toolx_inspect(ALLOW_ISSUES 0 report --file "${bad_app_json}" --schema "${schema_json}" --allow-issues --json)
assert_contains(ALLOW_ISSUES "${ALLOW_ISSUES_OUT}" "\"ok\": true")
assert_contains(ALLOW_ISSUES "${ALLOW_ISSUES_OUT}" "\"schema_issue_count\": 1")
toolx_assert_json_envelope(ALLOW_ISSUES "${ALLOW_ISSUES_OUT}" "toolx.inspect.result" true 0)
assert_inspect_data_fields(ALLOW_ISSUES "${ALLOW_ISSUES_OUT}")

run_toolx_inspect(PATH_SELECT 0 report --file "${app_json}" --path svc.port --json)
assert_contains(PATH_SELECT "${PATH_SELECT_OUT}" "\"selected_path\": \"svc.port\"")
assert_contains(PATH_SELECT "${PATH_SELECT_OUT}" "\"selected_kind\": \"integer\"")
assert_contains(PATH_SELECT "${PATH_SELECT_OUT}" "\"selected_value\": \"8080\"")
toolx_assert_json_envelope(PATH_SELECT "${PATH_SELECT_OUT}" "toolx.inspect.result" true 0)
toolx_assert_json_value(PATH_SELECT "${PATH_SELECT_OUT}" "svc.port" data selected_path)
toolx_assert_json_value(PATH_SELECT "${PATH_SELECT_OUT}" "integer" data selected_kind)
toolx_assert_json_value(PATH_SELECT "${PATH_SELECT_OUT}" "8080" data selected_value)

run_toolx_inspect(PATH_NOT_FOUND 1 report --file "${app_json}" --path svc.missing --json)
assert_contains(PATH_NOT_FOUND "${PATH_NOT_FOUND_OUT}" "\"code\": 1")
assert_contains(PATH_NOT_FOUND "${PATH_NOT_FOUND_OUT}" "path not found: svc.missing")
toolx_assert_json_envelope(PATH_NOT_FOUND "${PATH_NOT_FOUND_OUT}" "toolx.inspect.result" false 1)

run_toolx_inspect(CONTAINS_FILTER 0 report --file "${app_json}" --contains tags --json)
assert_contains(CONTAINS_FILTER "${CONTAINS_FILTER_OUT}" "\"matched_path_count\": 2")
assert_contains(CONTAINS_FILTER "${CONTAINS_FILTER_OUT}" "\"selected_path\": \"tags\"")

run_toolx_inspect(RENDER_JSON 0 render --file "${app_json}" --schema "${schema_json}" --width 80 --height 16 --json)
assert_contains(RENDER_JSON "${RENDER_JSON_OUT}" "\"frame\"")
assert_contains(RENDER_JSON "${RENDER_JSON_OUT}" "toolx-inspect | file=")
assert_contains(RENDER_JSON "${RENDER_JSON_OUT}" "[schema]")
toolx_assert_json_envelope(RENDER_JSON "${RENDER_JSON_OUT}" "toolx.inspect.result" true 0)
toolx_assert_json_value(RENDER_JSON "${RENDER_JSON_OUT}" "render" data command)
assert_inspect_data_fields(RENDER_JSON "${RENDER_JSON_OUT}")

run_toolx_inspect(RENDER_SCHEMA_FAIL 4 render --file "${bad_app_json}" --schema "${schema_json}" --width 80 --height 16 --json)
assert_contains(RENDER_SCHEMA_FAIL "${RENDER_SCHEMA_FAIL_OUT}" "\"ok\": false")
assert_contains(RENDER_SCHEMA_FAIL "${RENDER_SCHEMA_FAIL_OUT}" "\"frame\"")
assert_contains(RENDER_SCHEMA_FAIL "${RENDER_SCHEMA_FAIL_OUT}" "\"schema_issue_count\": 1")
toolx_assert_json_envelope(RENDER_SCHEMA_FAIL "${RENDER_SCHEMA_FAIL_OUT}" "toolx.inspect.result" false 4)
assert_inspect_data_fields(RENDER_SCHEMA_FAIL "${RENDER_SCHEMA_FAIL_OUT}")

run_toolx_inspect(RENDER_PLAIN 0 render --file "${app_json}" --width 80 --height 16)
assert_contains(RENDER_PLAIN "${RENDER_PLAIN_OUT}" "toolx-inspect | file=")
assert_contains(RENDER_PLAIN "${RENDER_PLAIN_OUT}" "[paths]")

run_toolx_inspect(RUN_SCRIPTED 0 run --file "${app_json}" --script "tab,down,esc" --ticks 4 --no-ansi)

run_toolx_inspect(RUN_JSON_FRAME 0 run --file "${app_json}" --script "tab,down,esc" --ticks 4 --no-ansi --json)
assert_contains(RUN_JSON_FRAME "${RUN_JSON_FRAME_OUT}" "\"command\": \"run\"")
assert_contains(RUN_JSON_FRAME "${RUN_JSON_FRAME_OUT}" "\"frame\"")
toolx_assert_json_envelope(RUN_JSON_FRAME "${RUN_JSON_FRAME_OUT}" "toolx.inspect.result" true 0)
toolx_assert_json_value(RUN_JSON_FRAME "${RUN_JSON_FRAME_OUT}" "run" data command)
assert_inspect_data_fields(RUN_JSON_FRAME "${RUN_JSON_FRAME_OUT}")

run_toolx_inspect(MANIFEST_APPLIES 0 report --manifest "${manifest_json}" --json)
assert_contains(MANIFEST_APPLIES "${MANIFEST_APPLIES_OUT}" "\"manifest\": \"${manifest_json}\"")
assert_contains(MANIFEST_APPLIES "${MANIFEST_APPLIES_OUT}" "\"matched_path_count\": 3")
toolx_assert_json_envelope(MANIFEST_APPLIES "${MANIFEST_APPLIES_OUT}" "toolx.inspect.result" true 0)
toolx_assert_json_value(MANIFEST_APPLIES "${MANIFEST_APPLIES_OUT}" "${manifest_json}" data manifest)
toolx_assert_json_value(MANIFEST_APPLIES "${MANIFEST_APPLIES_OUT}" "3" data matched_path_count)

run_toolx_inspect(MANIFEST_CLI_OVERRIDE 0 report --manifest "${override_manifest_json}" --contains tags --path tags[0] --json)
assert_contains(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "\"matched_path_count\": 2")
assert_contains(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "\"selected_path\": \"tags[0]\"")
toolx_assert_json_envelope(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "toolx.inspect.result" true 0)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "2" data matched_path_count)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "tags[0]" data selected_path)
