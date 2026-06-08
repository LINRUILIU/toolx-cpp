if(NOT DEFINED TOOLX_LOG_EXE)
    message(FATAL_ERROR "TOOLX_LOG_EXE is required")
endif()

if(NOT EXISTS "${TOOLX_LOG_EXE}")
    message(FATAL_ERROR "toolx-log executable not found: ${TOOLX_LOG_EXE}")
endif()

set(test_root "${CMAKE_BINARY_DIR}/toolx_log_cli_contracts")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(text_log "${test_root}/app.log")
set(jsonl_log "${test_root}/app.jsonl")
set(manifest_json "${test_root}/log-manifest.json")
set(level_manifest_json "${test_root}/level-manifest.json")
set(unknown_manifest_json "${test_root}/unknown-manifest.json")
set(malformed_manifest_json "${test_root}/malformed-manifest.json")

file(WRITE "${text_log}" [=[
2026-06-04 10:00:00.000 INFO boot ready
2026-06-04 10:05:00.000 WARNING latency high
2026-06-04 10:10:00.000 ERROR db down
FATAL fatal without timestamp
not a log line
]=])

file(WRITE "${jsonl_log}" [=[
{"time":"2026-06-04 10:00:00.000","level":"INFO","msg":"ready"}
{"time":"2026-06-04 10:15:00.000","level":"ERROR","msg":"boom"}
{"level":"WARNING","msg":"missing time"}
not-json
]=])

file(WRITE "${manifest_json}" [=[
{
  "files": ["@TEXT_LOG@"],
  "format": "logsys-text",
  "min_level": "error",
  "max_samples": 5
}
]=])
file(READ "${manifest_json}" manifest_text)
string(REPLACE "@TEXT_LOG@" "${text_log}" manifest_text "${manifest_text}")
file(WRITE "${manifest_json}" "${manifest_text}")

file(WRITE "${level_manifest_json}" [=[
{
  "files": ["@TEXT_LOG@"],
  "format": "logsys-text",
  "level": ["error"]
}
]=])
file(READ "${level_manifest_json}" level_manifest_text)
string(REPLACE "@TEXT_LOG@" "${text_log}" level_manifest_text "${level_manifest_text}")
file(WRITE "${level_manifest_json}" "${level_manifest_text}")

file(WRITE "${unknown_manifest_json}" [=[
{
  "files": ["@TEXT_LOG@"],
  "unexpected": true
}
]=])
file(READ "${unknown_manifest_json}" unknown_manifest_text)
string(REPLACE "@TEXT_LOG@" "${text_log}" unknown_manifest_text "${unknown_manifest_text}")
file(WRITE "${unknown_manifest_json}" "${unknown_manifest_text}")

file(WRITE "${malformed_manifest_json}" "{")

function(run_toolx_log case_name expected_code)
    execute_process(
        COMMAND "${TOOLX_LOG_EXE}" ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(NOT actual_code EQUAL expected_code)
        message(FATAL_ERROR
            "[${case_name}] expected exit ${expected_code}, got ${actual_code}\n"
            "command: ${TOOLX_LOG_EXE} ${ARGN}\n"
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

run_toolx_log(HELP 0 --help)
assert_contains(HELP "${HELP_OUT}" "toolx-log - summarize runtime logs")
assert_contains(HELP "${HELP_OUT}" "summarize")

run_toolx_log(MISSING_INPUT 2 summarize --json)
assert_contains(MISSING_INPUT "${MISSING_INPUT_OUT}" "\"code\": 2")
assert_contains(MISSING_INPUT "${MISSING_INPUT_OUT}" "missing required option --file or --manifest")

run_toolx_log(MISSING_FILE 3 summarize --file "${test_root}/missing.log" --json)
assert_contains(MISSING_FILE "${MISSING_FILE_OUT}" "\"code\": 3")
assert_contains(MISSING_FILE "${MISSING_FILE_OUT}" "log file not found")

run_toolx_log(MISSING_MANIFEST 3 summarize --manifest "${test_root}/missing-manifest.json" --json)
assert_contains(MISSING_MANIFEST "${MISSING_MANIFEST_OUT}" "\"code\": 3")
assert_contains(MISSING_MANIFEST "${MISSING_MANIFEST_OUT}" "manifest file not found")

run_toolx_log(MALFORMED_MANIFEST 4 summarize --manifest "${malformed_manifest_json}" --json)
assert_contains(MALFORMED_MANIFEST "${MALFORMED_MANIFEST_OUT}" "\"code\": 4")
assert_contains(MALFORMED_MANIFEST "${MALFORMED_MANIFEST_OUT}" "manifest parse failed")

run_toolx_log(UNKNOWN_MANIFEST 4 summarize --manifest "${unknown_manifest_json}" --json)
assert_contains(UNKNOWN_MANIFEST "${UNKNOWN_MANIFEST_OUT}" "\"code\": 4")
assert_contains(UNKNOWN_MANIFEST "${UNKNOWN_MANIFEST_OUT}" "manifest validation failed")

run_toolx_log(TEXT_SUMMARY 0 summarize --file "${text_log}" --format logsys-text --json)
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"schema\": \"toolx.log.result\"")
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"lines_read\": 5")
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"matched\": 4")
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"parse_failures\": 1")
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"warning\": 1")
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"error\": 1")
assert_contains(TEXT_SUMMARY "${TEXT_SUMMARY_OUT}" "\"fatal\": 1")

run_toolx_log(JSONL_SUMMARY 0 summarize --file "${jsonl_log}" --format jsonl --json)
assert_contains(JSONL_SUMMARY "${JSONL_SUMMARY_OUT}" "\"schema\": \"toolx.log.result\"")
assert_contains(JSONL_SUMMARY "${JSONL_SUMMARY_OUT}" "\"matched\": 3")
assert_contains(JSONL_SUMMARY "${JSONL_SUMMARY_OUT}" "\"parse_failures\": 1")

run_toolx_log(AUTO_JSONL 0 summarize --file "${jsonl_log}" --json)
assert_contains(AUTO_JSONL "${AUTO_JSONL_OUT}" "\"matched\": 3")

run_toolx_log(LEVEL_FILTER 0 summarize --file "${text_log}" --level warning --level fatal --json)
assert_contains(LEVEL_FILTER "${LEVEL_FILTER_OUT}" "\"matched\": 2")
assert_contains(LEVEL_FILTER "${LEVEL_FILTER_OUT}" "\"warning\": 1")
assert_contains(LEVEL_FILTER "${LEVEL_FILTER_OUT}" "\"fatal\": 1")

run_toolx_log(MIN_LEVEL_FILTER 0 summarize --file "${text_log}" --min-level error --json)
assert_contains(MIN_LEVEL_FILTER "${MIN_LEVEL_FILTER_OUT}" "\"matched\": 2")
assert_contains(MIN_LEVEL_FILTER "${MIN_LEVEL_FILTER_OUT}" "\"error\": 1")
assert_contains(MIN_LEVEL_FILTER "${MIN_LEVEL_FILTER_OUT}" "\"fatal\": 1")

run_toolx_log(CONTAINS_FILTER 0 summarize --file "${text_log}" --contains db --json)
assert_contains(CONTAINS_FILTER "${CONTAINS_FILTER_OUT}" "\"matched\": 1")
assert_contains(CONTAINS_FILTER "${CONTAINS_FILTER_OUT}" "\"error\": 1")

run_toolx_log(TIME_FILTER 0
    summarize
    --file "${text_log}"
    --since "2026-06-04 10:01:00.000"
    --until "2026-06-04 10:11:00.000"
    --json)
assert_contains(TIME_FILTER "${TIME_FILTER_OUT}" "\"matched\": 2")
assert_contains(TIME_FILTER "${TIME_FILTER_OUT}" "\"time_missing\": 1")

run_toolx_log(MANIFEST 0 summarize --manifest "${manifest_json}" --json)
assert_contains(MANIFEST "${MANIFEST_OUT}" "\"manifest\": \"${manifest_json}\"")
assert_contains(MANIFEST "${MANIFEST_OUT}" "\"matched\": 2")

run_toolx_log(MANIFEST_OVERRIDE 0 summarize --manifest "${manifest_json}" --min-level warning --json)
assert_contains(MANIFEST_OVERRIDE "${MANIFEST_OVERRIDE_OUT}" "\"matched\": 3")

run_toolx_log(MANIFEST_MIN_LEVEL_TO_LEVEL_OVERRIDE 0 summarize --manifest "${manifest_json}" --level fatal --json)
assert_contains(MANIFEST_MIN_LEVEL_TO_LEVEL_OVERRIDE "${MANIFEST_MIN_LEVEL_TO_LEVEL_OVERRIDE_OUT}" "\"matched\": 1")
assert_contains(MANIFEST_MIN_LEVEL_TO_LEVEL_OVERRIDE "${MANIFEST_MIN_LEVEL_TO_LEVEL_OVERRIDE_OUT}" "\"fatal\": 1")

run_toolx_log(MANIFEST_LEVEL_TO_MIN_LEVEL_OVERRIDE 0 summarize --manifest "${level_manifest_json}" --min-level warning --json)
assert_contains(MANIFEST_LEVEL_TO_MIN_LEVEL_OVERRIDE "${MANIFEST_LEVEL_TO_MIN_LEVEL_OVERRIDE_OUT}" "\"matched\": 3")

run_toolx_log(LEVEL_CONFLICT 2 summarize --file "${text_log}" --level error --min-level warning --json)
assert_contains(LEVEL_CONFLICT "${LEVEL_CONFLICT_OUT}" "\"code\": 2")
assert_contains(LEVEL_CONFLICT "${LEVEL_CONFLICT_OUT}" "use either --level or --min-level")

run_toolx_log(FAIL_ON_LEVEL 4 summarize --file "${text_log}" --fail-on-level error --json)
assert_contains(FAIL_ON_LEVEL "${FAIL_ON_LEVEL_OUT}" "\"code\": 4")
assert_contains(FAIL_ON_LEVEL "${FAIL_ON_LEVEL_OUT}" "log gate failed")

run_toolx_log(MAX_PARSE_ERRORS 4 summarize --file "${text_log}" --max-parse-errors 0 --json)
assert_contains(MAX_PARSE_ERRORS "${MAX_PARSE_ERRORS_OUT}" "\"code\": 4")
assert_contains(MAX_PARSE_ERRORS "${MAX_PARSE_ERRORS_OUT}" "parse failures exceed max_parse_errors")

run_toolx_log(PLAIN_OUTPUT 0 summarize --file "${text_log}" --contains db)
assert_contains(PLAIN_OUTPUT "${PLAIN_OUTPUT_OUT}" "files=1")
assert_contains(PLAIN_OUTPUT "${PLAIN_OUTPUT_OUT}" "matched=1")
assert_contains(PLAIN_OUTPUT "${PLAIN_OUTPUT_OUT}" "errors=1")
