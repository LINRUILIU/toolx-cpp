if(NOT DEFINED TOOLX_SYNC_EXE)
    message(FATAL_ERROR "TOOLX_SYNC_EXE is required")
endif()

if(NOT EXISTS "${TOOLX_SYNC_EXE}")
    message(FATAL_ERROR "toolx-sync executable not found: ${TOOLX_SYNC_EXE}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/toolx_cli_contract_helpers.cmake")

set(test_root "${CMAKE_BINARY_DIR}/toolx_sync_cli_scenario")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(base_json "${test_root}/base.json")
set(overlay_json "${test_root}/overlay.json")
set(schema_json "${test_root}/schema.json")
set(schema_fail_json "${test_root}/schema-fail.json")
set(out_json "${test_root}/resolved.json")
set(dry_run_json "${test_root}/dry-run-resolved.json")
set(dry_run_snapshot_json "${test_root}/dry-run-snapshot.json")
set(dry_run_journal_path "${test_root}/dry-run.journal")
set(snapshot_json "${test_root}/snapshot.json")
set(journal_path "${test_root}/resolved.journal")
set(log_path "${test_root}/audit.log")

file(WRITE "${base_json}" [=[{"svc":{"host":"127.0.0.1","port":8080},"feature":{"enabled":true},"tags":["base"]}]=])
file(WRITE "${overlay_json}" [=[{"svc":{"port":9090},"tags":["overlay"]}]=])
file(WRITE "${schema_json}" [=[{"type":"object","required":["svc"],"properties":{"svc":{"type":"object","required":["port"],"properties":{"port":{"type":"integer","minimum":1,"maximum":65535}}}}}]=])
file(WRITE "${schema_fail_json}" [=[{"svc":{"host":"127.0.0.1","port":70000}}]=])

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
assert_contains(HELP "${HELP_OUT}" "--no-proxy-from-env")

run_toolx_sync(MISSING_BASE 2 --out "${out_json}" --json)
toolx_assert_json_envelope(MISSING_BASE "${MISSING_BASE_OUT}" "toolx.sync.result" false 2)

run_toolx_sync(MISSING_OUT 2 --base "${base_json}" --json)
toolx_assert_json_envelope(MISSING_OUT "${MISSING_OUT_OUT}" "toolx.sync.result" false 2)

run_toolx_sync(MISSING_BASE_FILE 1 --base "${test_root}/missing-base.json" --out "${test_root}/missing-base-out.json" --json)
toolx_assert_json_envelope(MISSING_BASE_FILE "${MISSING_BASE_FILE_OUT}" "toolx.sync.result" false 1)

# JSON must remain readable through help/parse early exits in either order.
run_toolx_sync(HELP_JSON_LAST 0 --help --json)
toolx_assert_json_envelope(HELP_JSON_LAST "${HELP_JSON_LAST_OUT}" "toolx.sync.result" true 0)
run_toolx_sync(PARSE_JSON_LAST 2 --unknown-option --json)
toolx_assert_json_envelope(PARSE_JSON_LAST "${PARSE_JSON_LAST_OUT}" "toolx.sync.result" false 2)
run_toolx_sync(DASH_OPTION_VALUE_JSON 1 --base -- --out "${test_root}/dash-value.json" --json)
toolx_assert_json_envelope(DASH_OPTION_VALUE_JSON "${DASH_OPTION_VALUE_JSON_OUT}" "toolx.sync.result" false 1)
run_toolx_sync(JSON_OPTION_VALUE 1 --base --json --out "${test_root}/literal-json-value.json")
if(NOT JSON_OPTION_VALUE_OUT STREQUAL "")
    message(FATAL_ERROR "[JSON_OPTION_VALUE] literal input path was mistaken for an output flag")
endif()

run_toolx_sync(PUBLISH_JSON 0
    --base "${base_json}"
    --overlay "${overlay_json}"
    --out "${out_json}"
    --snapshot "${snapshot_json}"
    --journal "${journal_path}"
    --log-file "${log_path}"
    --schema "${schema_json}"
    --require svc.port
    --range svc.port=1:65535
    --append-arrays
    --json)
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"schema\": \"toolx.sync.result\"")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"ok\": true")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"out\": \"${out_json}\"")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"overlays\"")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"append_arrays\": true")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"dry_run\": false")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"proxy_from_environment\": true")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"planned_steps\"")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"source_trace\"")
assert_contains(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "\"schema_issues\": []")
toolx_assert_json_envelope(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "toolx.sync.result" true 0)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "${base_json}" data base)
toolx_assert_json_path(PUBLISH_JSON "${PUBLISH_JSON_OUT}" data overlays)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "" data remote_url)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" true data proxy_from_environment)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "${out_json}" data out)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "${snapshot_json}" data snapshot)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "${journal_path}" data journal)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "${log_path}" data log_file)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "${schema_json}" data schema)
toolx_assert_json_path(PUBLISH_JSON "${PUBLISH_JSON_OUT}" data schema_issues)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" true data append_arrays)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" false data dry_run)
toolx_assert_json_path(PUBLISH_JSON "${PUBLISH_JSON_OUT}" data steps)
toolx_assert_json_path(PUBLISH_JSON "${PUBLISH_JSON_OUT}" data planned_steps)
toolx_assert_json_path(PUBLISH_JSON "${PUBLISH_JSON_OUT}" data source_trace)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" true data validation_passed)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" true data publish_attempted)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" "publish" data stage)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" 2 data execution completed_steps)
toolx_assert_json_value(PUBLISH_JSON "${PUBLISH_JSON_OUT}" false data execution rollback_attempted)

if(NOT EXISTS "${out_json}")
    message(FATAL_ERROR "toolx-sync did not create ${out_json}")
endif()
if(NOT EXISTS "${snapshot_json}")
    message(FATAL_ERROR "toolx-sync did not create ${snapshot_json}")
endif()
if(NOT EXISTS "${log_path}")
    message(FATAL_ERROR "toolx-sync did not create ${log_path}")
endif()
if(NOT EXISTS "${journal_path}")
    message(FATAL_ERROR "toolx-sync did not create ${journal_path}")
endif()

file(READ "${out_json}" resolved_text)
assert_contains(PUBLISH_JSON_FILE "${resolved_text}" "\"port\": 9090")
assert_contains(PUBLISH_JSON_FILE "${resolved_text}" "\"base\"")
assert_contains(PUBLISH_JSON_FILE "${resolved_text}" "\"overlay\"")

file(WRITE "${dry_run_json}" "sentinel out\n")
file(WRITE "${dry_run_snapshot_json}" "sentinel snapshot\n")
file(WRITE "${dry_run_journal_path}" "sentinel journal\n")
file(READ "${dry_run_json}" dry_run_out_before)
file(READ "${dry_run_snapshot_json}" dry_run_snapshot_before)
file(READ "${dry_run_journal_path}" dry_run_journal_before)
run_toolx_sync(DRY_RUN_JSON 0
    --base "${base_json}"
    --overlay "${overlay_json}"
    --out "${dry_run_json}"
    --snapshot "${dry_run_snapshot_json}"
    --journal "${dry_run_journal_path}"
    --schema "${schema_json}"
    --require svc.port
    --dry-run
    --json)
assert_contains(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "\"message\": \"dry run passed\"")
assert_contains(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "\"dry_run\": true")
assert_contains(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "\"planned_steps\"")
toolx_assert_json_envelope(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "toolx.sync.result" true 0)
toolx_assert_json_value(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" true data dry_run)
toolx_assert_json_value(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" false data publish_attempted)
toolx_assert_json_value(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "dry_run" data stage)
toolx_assert_json_value(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "${dry_run_json}" data out)
toolx_assert_json_value(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "${dry_run_snapshot_json}" data snapshot)
toolx_assert_json_value(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" "${dry_run_journal_path}" data journal)
toolx_assert_json_path(DRY_RUN_JSON "${DRY_RUN_JSON_OUT}" data planned_steps)
file(READ "${dry_run_json}" dry_run_out_after)
file(READ "${dry_run_snapshot_json}" dry_run_snapshot_after)
file(READ "${dry_run_journal_path}" dry_run_journal_after)
if(NOT "${dry_run_out_before}" STREQUAL "${dry_run_out_after}")
    message(FATAL_ERROR "toolx-sync dry-run modified ${dry_run_json}")
endif()
if(NOT "${dry_run_snapshot_before}" STREQUAL "${dry_run_snapshot_after}")
    message(FATAL_ERROR "toolx-sync dry-run modified ${dry_run_snapshot_json}")
endif()
if(NOT "${dry_run_journal_before}" STREQUAL "${dry_run_journal_after}")
    message(FATAL_ERROR "toolx-sync dry-run modified ${dry_run_journal_path}")
endif()

set(no_proxy_out_json "${test_root}/no-proxy-resolved.json")
run_toolx_sync(NO_PROXY_DRY_RUN 0
    --base "${base_json}"
    --out "${no_proxy_out_json}"
    --no-proxy-from-env
    --dry-run
    --json)
toolx_assert_json_envelope(NO_PROXY_DRY_RUN "${NO_PROXY_DRY_RUN_OUT}" "toolx.sync.result" true 0)
toolx_assert_json_value(NO_PROXY_DRY_RUN "${NO_PROXY_DRY_RUN_OUT}" false data proxy_from_environment)
toolx_assert_json_value(NO_PROXY_DRY_RUN "${NO_PROXY_DRY_RUN_OUT}" true data dry_run)
if(EXISTS "${no_proxy_out_json}")
    message(FATAL_ERROR "toolx-sync --no-proxy-from-env without --remote-url wrote ${no_proxy_out_json}")
endif()

run_toolx_sync(VALIDATION_FAIL 4
    --base "${base_json}"
    --out "${test_root}/invalid.json"
    --range svc.port=9000:9999
    --json)
assert_contains(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" "\"ok\": false")
assert_contains(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" "\"code\": 4")
toolx_assert_json_envelope(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" "toolx.sync.result" false 4)
toolx_assert_json_value(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" false data validation_passed)
toolx_assert_json_value(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" false data publish_attempted)
toolx_assert_json_path(VALIDATION_FAIL "${VALIDATION_FAIL_OUT}" data source_trace)

run_toolx_sync(SCHEMA_FAIL 4
    --base "${schema_fail_json}"
    --out "${test_root}/schema-invalid.json"
    --schema "${schema_json}"
    --json)
assert_contains(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}" "\"schema_issues\"")
assert_contains(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}" "\"code\": \"maximum\"")
toolx_assert_json_envelope(SCHEMA_FAIL "${SCHEMA_FAIL_OUT}" "toolx.sync.result" false 4)

if(EXISTS "${test_root}/schema-invalid.json")
    message(FATAL_ERROR "toolx-sync created output for schema-invalid config")
endif()

set(bad_schema "${test_root}/unsupported-schema.json")
file(WRITE "${bad_schema}" [=[{"$schema":"https://json-schema.org/draft/2020-12/schema","type":"object"}]=])
run_toolx_sync(SCHEMA_COMPILE_FAIL 1 --base "${base_json}" --out "${test_root}/not-published.json"
    --schema "${bad_schema}" --json)
toolx_assert_json_value(SCHEMA_COMPILE_FAIL "${SCHEMA_COMPILE_FAIL_OUT}" "schema_load" data stage)
toolx_assert_json_value(SCHEMA_COMPILE_FAIL "${SCHEMA_COMPILE_FAIL_OUT}" false data publish_attempted)
toolx_assert_json_path(SCHEMA_COMPILE_FAIL "${SCHEMA_COMPILE_FAIL_OUT}" data source_trace)

# Verify actual old bytes as well as the failure's recovery facts.
set(rollback_out "${test_root}/rollback-current.json")
set(blocker "${test_root}/not-a-directory")
file(WRITE "${rollback_out}" "old accepted bytes\n")
file(WRITE "${blocker}" "blocker\n")
run_toolx_sync(PARTIAL_PUBLISH 1 --base "${base_json}" --out "${rollback_out}"
    --snapshot "${blocker}/snapshot.json" --journal "${test_root}/rollback.journal" --json)
toolx_assert_json_envelope(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" "toolx.sync.result" false 1)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" true data publish_attempted)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" true data validation_passed)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" "publish" data stage)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" true data execution rollback_attempted)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" true data execution rollback_complete)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" 1 data execution completed_steps)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" true data execution steps 0 rolled_back)
toolx_assert_json_value(PARTIAL_PUBLISH "${PARTIAL_PUBLISH_OUT}" false data execution steps 1 ok)
file(READ "${rollback_out}" actual_rollback)
if(NOT actual_rollback STREQUAL "old accepted bytes\n")
    message(FATAL_ERROR "[PARTIAL_PUBLISH] old output was not restored")
endif()
if(EXISTS "${test_root}/rollback.journal" OR EXISTS "${blocker}/snapshot.json")
    message(FATAL_ERROR "[PARTIAL_PUBLISH] unexpected residual path after complete rollback")
endif()

# Journal storage must never overwrite or append COMMIT to a published artifact.
set(alias_out "${test_root}/journal-alias.json")
run_toolx_sync(JOURNAL_ALIAS 1 --base "${base_json}" --out "${alias_out}"
    --journal "${test_root}/./journal-alias.json" --json)
toolx_assert_json_envelope(JOURNAL_ALIAS "${JOURNAL_ALIAS_OUT}" "toolx.sync.result" false 1)
toolx_assert_json_value(JOURNAL_ALIAS "${JOURNAL_ALIAS_OUT}" 0 data execution completed_steps)
if(EXISTS "${alias_out}")
    message(FATAL_ERROR "[JOURNAL_ALIAS] preflight rejection created or damaged an artifact")
endif()

# Format discovery must respect known option values even after a parse error.
run_toolx_sync(PARSE_DASH_VALUE_JSON 2 --base -- --unknown-option --json)
toolx_assert_json_envelope(PARSE_DASH_VALUE_JSON "${PARSE_DASH_VALUE_JSON_OUT}" "toolx.sync.result" false 2)
run_toolx_sync(PARSE_SHORT_DASH_VALUE_JSON 2 -b -- --unknown-option --json)
toolx_assert_json_envelope(PARSE_SHORT_DASH_VALUE_JSON "${PARSE_SHORT_DASH_VALUE_JSON_OUT}" "toolx.sync.result" false 2)
run_toolx_sync(PARSE_JSON_VALUE 2 --base --json --unknown-option)
if(NOT PARSE_JSON_VALUE_OUT STREQUAL "")
    message(FATAL_ERROR "[PARSE_JSON_VALUE] literal option value was mistaken for JSON output selection")
endif()
run_toolx_sync(HELP_JSON_VALUE 0 --help --base --json)
if(HELP_JSON_VALUE_OUT MATCHES "\"schema\": \"toolx.sync.result\"")
    message(FATAL_ERROR "[HELP_JSON_VALUE] literal option value was mistaken for JSON output selection")
endif()
run_toolx_sync(PARSE_TERMINATED_JSON 2 --unknown-option -- --json)
if(NOT PARSE_TERMINATED_JSON_OUT STREQUAL "")
    message(FATAL_ERROR "[PARSE_TERMINATED_JSON] token after -- was mistaken for JSON output selection")
endif()

# Sequential overlays can replace a subtree and later rebuild it. Pre-merging
# the overlays would incorrectly resurrect the old base subtree.
set(sequence_base "${test_root}/sequence-base.json")
set(sequence_first "${test_root}/sequence-first.json")
set(sequence_second "${test_root}/sequence-second.json")
set(sequence_out "${test_root}/sequence-out.json")
file(WRITE "${sequence_base}" [=[{"a":{"old":1},"keep":2,"items":["base"]}]=])
file(WRITE "${sequence_first}" [=[{"a":0,"items":["first"],"local_keep":3}]=])
file(WRITE "${sequence_second}" [=[{"a":{"new":4},"items":["second"]}]=])
run_toolx_sync(SEQUENTIAL_OVERLAYS 0 --base "${sequence_base}" --overlay "${sequence_first}"
    --overlay "${sequence_second}" --out "${sequence_out}" --append-arrays --json)
file(READ "${sequence_out}" sequence_bytes)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS_FILE "${sequence_bytes}" 4 a new)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS_FILE "${sequence_bytes}" 2 keep)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS_FILE "${sequence_bytes}" "base" items 0)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS_FILE "${sequence_bytes}" "first" items 1)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS_FILE "${sequence_bytes}" "second" items 2)
string(JSON removed_old ERROR_VARIABLE old_error GET "${sequence_bytes}" a old)
if(old_error STREQUAL "NOTFOUND")
    message(FATAL_ERROR "[SEQUENTIAL_OVERLAYS] old subtree was resurrected")
endif()
toolx_assert_json_value(SEQUENTIAL_OVERLAYS "${SEQUENTIAL_OVERLAYS_OUT}" "a.new" data source_trace 0 path)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS "${SEQUENTIAL_OVERLAYS_OUT}" "local" data source_trace 0 layer)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS "${SEQUENTIAL_OVERLAYS_OUT}" "base" data source_trace 1 layer)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS "${SEQUENTIAL_OVERLAYS_OUT}" "base" data source_trace 4 layer)
toolx_assert_json_value(SEQUENTIAL_OVERLAYS "${SEQUENTIAL_OVERLAYS_OUT}" "local" data source_trace 5 layer)

# Bounds must be finite; malformed bounds reject before any publish attempt.
foreach(bounds "nan:nan" "0:inf" "-inf:100" "0:1e999")
    run_toolx_sync(NONFINITE_RANGE 2 --base "${base_json}" --out "${test_root}/range-rejected.json"
        --range "svc.port=${bounds}" --json)
    toolx_assert_json_envelope(NONFINITE_RANGE "${NONFINITE_RANGE_OUT}" "toolx.sync.result" false 2)
endforeach()
if(EXISTS "${test_root}/range-rejected.json")
    message(FATAL_ERROR "[NONFINITE_RANGE] invalid bounds caused publication")
endif()
set(overflow_base "${test_root}/overflow-base.json")
file(WRITE "${overflow_base}" [=[{"number":1e999}]=])
run_toolx_sync(OVERFLOW_JSON 1 --base "${overflow_base}" --out "${test_root}/overflow-out.json" --json)
toolx_assert_json_envelope(OVERFLOW_JSON "${OVERFLOW_JSON_OUT}" "toolx.sync.result" false 1)
if(EXISTS "${test_root}/overflow-out.json")
    message(FATAL_ERROR "[OVERFLOW_JSON] unrepresentable number caused invalid JSON publication")
endif()

# An oversized decimal index must not wrap to an existing array element.
set(index_base "${test_root}/index-base.json")
file(WRITE "${index_base}" [=[{"items":[123]}]=])
foreach(index "18446744073709551616" "999999999999999999999999999999999999999999999999999999")
    run_toolx_sync(OVERFLOW_INDEX 4 --base "${index_base}" --out "${test_root}/index-out.json"
        --require "items[${index}]" --dry-run --json)
    toolx_assert_json_envelope(OVERFLOW_INDEX "${OVERFLOW_INDEX_OUT}" "toolx.sync.result" false 4)
    toolx_assert_json_value(OVERFLOW_INDEX "${OVERFLOW_INDEX_OUT}" false data validation_passed)
    toolx_assert_json_value(OVERFLOW_INDEX "${OVERFLOW_INDEX_OUT}" false data publish_attempted)
endforeach()
if(EXISTS "${test_root}/index-out.json")
    message(FATAL_ERROR "[OVERFLOW_INDEX] nonexistent path was accepted or published")
endif()

# FileSink opens before composition; reject aliases before even configuring it.
set(log_alias_root "${test_root}/log-alias")
file(MAKE_DIRECTORY "${log_alias_root}")
file(WRITE "${log_alias_root}/base.json" [=[{"value":1}]=])
file(WRITE "${log_alias_root}/overlay.json" [=[{"added":2}]=])
file(WRITE "${log_alias_root}/schema.json" [=[{"type":"object"}]=])
file(WRITE "${log_alias_root}/out.json" "previous output\n")
file(WRITE "${log_alias_root}/snapshot.json" "previous snapshot\n")
# Use a real producer: CMake's text writer emits CRLF on Windows, while J4
# records use canonical LF bytes and are deliberately parsed strictly.
run_toolx_sync(LOG_COMMITTED_SEED 0 --base "${log_alias_root}/base.json"
    --out "${log_alias_root}/out.json" --journal "${log_alias_root}/run.journal" --json)
toolx_assert_json_envelope(LOG_COMMITTED_SEED "${LOG_COMMITTED_SEED_OUT}" "toolx.sync.result" true 0)
file(WRITE "${log_alias_root}/out.json" "previous output\n")
set(log_alias_files base.json overlay.json schema.json out.json snapshot.json run.journal)
foreach(name IN LISTS log_alias_files)
    file(READ "${log_alias_root}/${name}" "log_alias_before_${name}")
endforeach()

function(assert_log_alias case_name log_file)
    run_toolx_sync(${case_name} 2 --base "${log_alias_root}/base.json"
        --overlay "${log_alias_root}/overlay.json" --schema "${log_alias_root}/schema.json"
        --out "${log_alias_root}/out.json" --snapshot "${log_alias_root}/snapshot.json"
        --journal "${log_alias_root}/run.journal" --log-file "${log_file}" --json)
    toolx_assert_json_envelope(${case_name} "${${case_name}_OUT}" "toolx.sync.result" false 2)
    assert_contains(${case_name} "${${case_name}_OUT}" "--log-file")
    foreach(name IN LISTS log_alias_files)
        file(READ "${log_alias_root}/${name}" after)
        if(NOT after STREQUAL "${log_alias_before_${name}}")
            message(FATAL_ERROR "[${case_name}] preflight changed ${name}")
        endif()
    endforeach()
endfunction()

foreach(name IN LISTS log_alias_files)
    assert_log_alias(LOG_ALIAS "${log_alias_root}/./${name}")
endforeach()
assert_log_alias(LOG_ANCESTOR "${log_alias_root}")
assert_log_alias(LOG_DESCENDANT "${log_alias_root}/out.json/child.log")
if(WIN32)
    assert_log_alias(LOG_CASE_ALIAS "${log_alias_root}/BASE.JSON")
endif()

file(CREATE_LINK "${log_alias_root}/base.json" "${log_alias_root}/hardlink.log" RESULT hardlink_result)
if(hardlink_result STREQUAL "0")
    assert_log_alias(LOG_HARDLINK_ALIAS "${log_alias_root}/hardlink.log")
else()
    message(STATUS "log alias hardlink test skipped: ${hardlink_result}")
endif()
file(CREATE_LINK "${log_alias_root}" "${test_root}/log-alias-link" SYMBOLIC RESULT symlink_result)
if(symlink_result STREQUAL "0")
    assert_log_alias(LOG_SYMLINK_PARENT "${test_root}/log-alias-link/base.json")
else()
    message(STATUS "log alias symlink test skipped: ${symlink_result}")
endif()

set(new_log_journal "${log_alias_root}/not-created.journal")
run_toolx_sync(LOG_NEW_JOURNAL_ALIAS 2 --base "${log_alias_root}/base.json"
    --out "${log_alias_root}/not-created.json" --journal "${new_log_journal}"
    --log-file "${log_alias_root}/./not-created.journal" --json)
toolx_assert_json_envelope(LOG_NEW_JOURNAL_ALIAS "${LOG_NEW_JOURNAL_ALIAS_OUT}" "toolx.sync.result" false 2)
if(EXISTS "${new_log_journal}" OR EXISTS "${log_alias_root}/not-created.json")
    message(FATAL_ERROR "[LOG_NEW_JOURNAL_ALIAS] preflight created an output or log/journal")
endif()
file(RELATIVE_PATH relative_new_journal "${CMAKE_BINARY_DIR}" "${new_log_journal}")
run_toolx_sync(LOG_RELATIVE_JOURNAL_ALIAS 2 --base "${log_alias_root}/base.json"
    --out "${log_alias_root}/not-created.json" --journal "${new_log_journal}"
    --log-file "${relative_new_journal}" --json)
toolx_assert_json_envelope(LOG_RELATIVE_JOURNAL_ALIAS "${LOG_RELATIVE_JOURNAL_ALIAS_OUT}" "toolx.sync.result" false 2)
if(EXISTS "${new_log_journal}" OR EXISTS "${log_alias_root}/not-created.json")
    message(FATAL_ERROR "[LOG_RELATIVE_JOURNAL_ALIAS] relative/absolute alias escaped preflight")
endif()

# The refused committed-journal alias did not poison journal reuse. A truly
# separate audit file remains supported and preserves every input byte.
run_toolx_sync(LOG_SEPARATE_CONTROL 0 --base "${log_alias_root}/base.json"
    --overlay "${log_alias_root}/overlay.json" --schema "${log_alias_root}/schema.json"
    --out "${log_alias_root}/out.json" --snapshot "${log_alias_root}/snapshot.json"
    --journal "${log_alias_root}/run.journal" --log-file "${log_alias_root}/audit.log" --json)
toolx_assert_json_envelope(LOG_SEPARATE_CONTROL "${LOG_SEPARATE_CONTROL_OUT}" "toolx.sync.result" true 0)
foreach(name base.json overlay.json schema.json)
    file(READ "${log_alias_root}/${name}" after)
    if(NOT after STREQUAL "${log_alias_before_${name}}")
        message(FATAL_ERROR "[LOG_SEPARATE_CONTROL] changed input ${name}")
    endif()
endforeach()
run_toolx_sync(LOG_JOURNAL_REUSE 0 --base "${log_alias_root}/base.json"
    --out "${log_alias_root}/out.json" --journal "${log_alias_root}/run.journal" --json)
toolx_assert_json_envelope(LOG_JOURNAL_REUSE "${LOG_JOURNAL_REUSE_OUT}" "toolx.sync.result" true 0)

# FileSink also owns its size-rotation archive namespace. Reserve the actual
# default .1 through .5 slots before opening the sink, even below the threshold.
# Keep one shared exact-threshold log; hash bytes instead of repeatedly copying
# 10 MiB fixtures or including their contents in failure messages.
set(rolling_root "${test_root}/log-rolling")
set(rolling_log "${rolling_root}/audit.log")
file(MAKE_DIRECTORY "${rolling_root}")
foreach(role base overlay schema out snapshot journal)
    set("rolling_file_${role}" "${rolling_root}/${role}.json")
endforeach()
set(rolling_file_journal "${rolling_root}/run.journal")
file(WRITE "${rolling_file_base}" [=[{"value":1}]=])
file(WRITE "${rolling_file_overlay}" [=[{"added":2}]=])
file(WRITE "${rolling_file_schema}" [=[{"type":"object"}]=])
file(WRITE "${rolling_file_out}" "previous output\n")
file(WRITE "${rolling_file_snapshot}" "previous snapshot\n")
# Produce canonical J4 bytes with the CLI, including a real COMMIT record.
run_toolx_sync(ROLLING_COMMITTED_SEED 0 --base "${rolling_file_base}"
    --out "${rolling_file_out}" --journal "${rolling_file_journal}" --json)
toolx_assert_json_envelope(ROLLING_COMMITTED_SEED "${ROLLING_COMMITTED_SEED_OUT}"
    "toolx.sync.result" true 0)
file(READ "${rolling_file_journal}" rolling_seed_journal)
assert_contains(ROLLING_COMMITTED_SEED "${rolling_seed_journal}" "FSXJ4\n")
assert_contains(ROLLING_COMMITTED_SEED "${rolling_seed_journal}" "COMMIT\n")
foreach(role base overlay schema)
    file(SHA256 "${rolling_file_${role}}" "rolling_input_hash_${role}")
endforeach()
foreach(slot RANGE 1 5)
    file(WRITE "${rolling_log}.${slot}" "previous archive ${slot}\n")
endforeach()

function(capture_rolling_state result_var)
    # Do not recurse through the optional directory symlink back into this tree.
    cmake_policy(PUSH)
    cmake_policy(SET CMP0009 NEW)
    file(GLOB_RECURSE entries LIST_DIRECTORIES true RELATIVE "${rolling_root}"
        "${rolling_root}/*")
    cmake_policy(POP)
    list(SORT entries)
    set(state "")
    foreach(entry IN LISTS entries)
        set(path "${rolling_root}/${entry}")
        if(IS_SYMLINK "${path}")
            file(READ_SYMLINK "${path}" target)
            list(APPEND state "symlink:${entry}:${target}")
        elseif(IS_DIRECTORY "${path}")
            list(APPEND state "directory:${entry}")
        else()
            file(SHA256 "${path}" hash)
            list(APPEND state "file:${entry}:${hash}")
        endif()
    endforeach()
    set("${result_var}" "${state}" PARENT_SCOPE)
endfunction()

function(assert_rolling_rejected case_name role protected_path)
    foreach(arg base overlay schema out snapshot journal)
        set("rolling_arg_${arg}" "${rolling_file_${arg}}")
    endforeach()
    set("rolling_arg_${role}" "${protected_path}")
    capture_rolling_state(before)
    run_toolx_sync(${case_name} 2 --base "${rolling_arg_base}"
        --overlay "${rolling_arg_overlay}" --schema "${rolling_arg_schema}"
        --out "${rolling_arg_out}" --snapshot "${rolling_arg_snapshot}"
        --journal "${rolling_arg_journal}" --log-file "${rolling_log}" --json)
    toolx_assert_json_envelope(${case_name} "${${case_name}_OUT}"
        "toolx.sync.result" false 2)
    assert_contains(${case_name} "${${case_name}_OUT}" "--log-file")
    capture_rolling_state(after)
    if(NOT "${after}" STREQUAL "${before}")
        message(FATAL_ERROR
            "[${case_name}] preflight changed bytes, rotated logs, or created/deleted a path\n"
            "before: ${before}\nafter: ${after}")
    endif()
endfunction()

file(WRITE "${rolling_log}" "small audit log\n")
assert_rolling_rejected(ROLLING_BELOW_THRESHOLD out "${rolling_log}.5")
math(EXPR rolling_limit "10 * 1024 * 1024")
string(REPEAT "L" ${rolling_limit} rolling_threshold_bytes)
file(WRITE "${rolling_log}" "${rolling_threshold_bytes}")
unset(rolling_threshold_bytes)
file(SIZE "${rolling_log}" rolling_initial_size)
if(NOT rolling_initial_size EQUAL rolling_limit)
    message(FATAL_ERROR "[ROLLING_FIXTURE] log must be exactly 10 MiB")
endif()
file(SHA256 "${rolling_log}" rolling_threshold_hash)

# Input slots contain JSON bytes, but their .N suffixes are not supported by
# automatic input format detection. They assert preflight role coverage, not
# runtime pollution. Each rejection preserves the main log, all five archives,
# all input/output bytes, the committed journal, and the complete path inventory.
foreach(role base overlay schema out snapshot journal)
    foreach(slot RANGE 1 5)
        configure_file("${rolling_file_${role}}" "${rolling_log}.${slot}" COPYONLY)
        string(TOUPPER "${role}" upper_role)
        assert_rolling_rejected("ROLLING_${upper_role}_${slot}" "${role}"
            "${rolling_log}.${slot}")
    endforeach()
endforeach()

# Absent writable archive paths must be rejected without creating artifacts.
foreach(role out snapshot journal)
    if(role STREQUAL "out")
        set(slot 5)
    elseif(role STREQUAL "snapshot")
        set(slot 3)
    else()
        set(slot 1)
    endif()
    file(REMOVE "${rolling_log}.${slot}")
    string(TOUPPER "${role}" upper_role)
    assert_rolling_rejected("ROLLING_NEW_${upper_role}" "${role}" "${rolling_log}.${slot}")
    file(WRITE "${rolling_log}.${slot}" "restored archive ${slot}\n")
endforeach()

configure_file("${rolling_file_snapshot}" "${rolling_log}.2" COPYONLY)
assert_rolling_rejected(ROLLING_NORMALIZED_ALIAS snapshot "${rolling_root}/./audit.log.2")
file(RELATIVE_PATH rolling_relative_archive "${CMAKE_BINARY_DIR}" "${rolling_log}.3")
assert_rolling_rejected(ROLLING_RELATIVE_ALIAS out "${rolling_relative_archive}")
if(WIN32)
    configure_file("${rolling_file_base}" "${rolling_log}.4" COPYONLY)
    assert_rolling_rejected(ROLLING_CASE_ALIAS base "${rolling_root}/AUDIT.LOG.4")
endif()

configure_file("${rolling_file_out}" "${rolling_log}.3" COPYONLY)
set(rolling_hardlink "${rolling_root}/archive-hardlink.json")
file(CREATE_LINK "${rolling_log}.3" "${rolling_hardlink}" RESULT rolling_hardlink_result)
if(NOT rolling_hardlink_result STREQUAL "0")
    message(FATAL_ERROR "[ROLLING_HARDLINK_ALIAS] cannot create same-tree hardlink: ${rolling_hardlink_result}")
endif()
assert_rolling_rejected(ROLLING_HARDLINK_ALIAS out "${rolling_hardlink}")
file(REMOVE "${rolling_hardlink}")

# A supported .json name also protects an otherwise loadable input hardlink.
configure_file("${rolling_file_base}" "${rolling_log}.4" COPYONLY)
set(rolling_input_hardlink "${rolling_root}/base-hardlink.json")
file(CREATE_LINK "${rolling_log}.4" "${rolling_input_hardlink}" RESULT rolling_input_hardlink_result)
if(NOT rolling_input_hardlink_result STREQUAL "0")
    message(FATAL_ERROR "[ROLLING_INPUT_HARDLINK_ALIAS] cannot create same-tree hardlink: ${rolling_input_hardlink_result}")
endif()
assert_rolling_rejected(ROLLING_INPUT_HARDLINK_ALIAS base "${rolling_input_hardlink}")
file(REMOVE "${rolling_input_hardlink}")

configure_file("${rolling_file_overlay}" "${rolling_log}.2" COPYONLY)
set(rolling_symlink "${rolling_root}/archive-symlink.json")
file(CREATE_LINK "${rolling_log}.2" "${rolling_symlink}" SYMBOLIC RESULT rolling_symlink_result)
if(rolling_symlink_result STREQUAL "0")
    assert_rolling_rejected(ROLLING_SYMLINK_ALIAS overlay "${rolling_symlink}")
    file(REMOVE "${rolling_symlink}")
elseif(WIN32)
    message(STATUS "rolling archive symlink test skipped: ${rolling_symlink_result}")
else()
    message(FATAL_ERROR "[ROLLING_SYMLINK_ALIAS] cannot create symlink: ${rolling_symlink_result}")
endif()
set(rolling_parent_link "${rolling_root}/archive-parent-link")
file(CREATE_LINK "${rolling_root}" "${rolling_parent_link}" SYMBOLIC RESULT rolling_parent_link_result)
if(rolling_parent_link_result STREQUAL "0")
    assert_rolling_rejected(ROLLING_SYMLINK_PARENT overlay "${rolling_parent_link}/audit.log.2")
    file(REMOVE "${rolling_parent_link}")
elseif(WIN32)
    message(STATUS "rolling archive parent symlink test skipped: ${rolling_parent_link_result}")
else()
    message(FATAL_ERROR "[ROLLING_SYMLINK_PARENT] cannot create symlink: ${rolling_parent_link_result}")
endif()

# Archive candidates must also protect their descendants, including an
# existing directory containing a valid input and a not-yet-created output.
assert_rolling_rejected(ROLLING_ARCHIVE_DESCENDANT out "${rolling_log}.1/child.json")
file(REMOVE "${rolling_log}.2")
file(MAKE_DIRECTORY "${rolling_log}.2")
configure_file("${rolling_file_base}" "${rolling_log}.2/base.json" COPYONLY)
assert_rolling_rejected(ROLLING_ARCHIVE_ANCESTOR base "${rolling_log}.2/base.json")
file(REMOVE_RECURSE "${rolling_log}.2")

# The default sink touches only the main path and .1 through .5. At the exact
# rotation threshold, .6, .0 and a sibling prefix such as .1-safe stay usable.
# Assert real rotation so disabling rolling cannot satisfy the guard tests.
set(rolling_control_out "${rolling_log}.6")
set(rolling_control_snapshot "${rolling_log}.1-safe")
set(rolling_control_journal "${rolling_log}.0")
configure_file("${rolling_file_journal}" "${rolling_control_journal}" COPYONLY)
foreach(slot RANGE 1 5)
    file(WRITE "${rolling_log}.${slot}" "control archive ${slot}\n")
    file(SHA256 "${rolling_log}.${slot}" "rolling_control_before_${slot}")
endforeach()
file(SIZE "${rolling_log}" rolling_pre_control_size)
file(SHA256 "${rolling_log}" rolling_pre_control_hash)
if(NOT rolling_pre_control_size EQUAL rolling_limit OR
   NOT rolling_pre_control_hash STREQUAL rolling_threshold_hash)
    message(FATAL_ERROR "[ROLLING_SEPARATE_CONTROL] rejection cases changed the threshold fixture")
endif()
run_toolx_sync(ROLLING_SEPARATE_CONTROL 0 --base "${rolling_file_base}"
    --overlay "${rolling_file_overlay}" --schema "${rolling_file_schema}"
    --out "${rolling_control_out}" --snapshot "${rolling_control_snapshot}"
    --journal "${rolling_control_journal}" --log-file "${rolling_log}" --json)
toolx_assert_json_envelope(ROLLING_SEPARATE_CONTROL "${ROLLING_SEPARATE_CONTROL_OUT}"
    "toolx.sync.result" true 0)
toolx_assert_json_value(ROLLING_SEPARATE_CONTROL "${ROLLING_SEPARATE_CONTROL_OUT}"
    true data publish_attempted)
toolx_assert_json_value(ROLLING_SEPARATE_CONTROL "${ROLLING_SEPARATE_CONTROL_OUT}"
    2 data execution completed_steps)
file(SHA256 "${rolling_log}.1" rolling_first_archive_hash)
if(NOT rolling_first_archive_hash STREQUAL rolling_threshold_hash)
    message(FATAL_ERROR "[ROLLING_SEPARATE_CONTROL] exact-threshold log was not rotated intact into .1")
endif()
foreach(slot RANGE 2 5)
    math(EXPR previous_slot "${slot} - 1")
    file(SHA256 "${rolling_log}.${slot}" after)
    if(NOT after STREQUAL "${rolling_control_before_${previous_slot}}")
        message(FATAL_ERROR "[ROLLING_SEPARATE_CONTROL] archive ${previous_slot} did not move intact into .${slot}")
    endif()
endforeach()
file(SHA256 "${rolling_log}.5" rolling_last_archive_hash)
if(rolling_last_archive_hash STREQUAL rolling_control_before_5)
    message(FATAL_ERROR "[ROLLING_SEPARATE_CONTROL] oldest .5 archive was not discarded")
endif()
file(READ "${rolling_log}" rolling_new_log)
assert_contains(ROLLING_SEPARATE_CONTROL "${rolling_new_log}" "toolx-sync published")
file(SIZE "${rolling_log}" rolling_new_log_size)
if(rolling_new_log_size LESS_EQUAL 0 OR NOT rolling_new_log_size LESS rolling_limit)
    message(FATAL_ERROR "[ROLLING_SEPARATE_CONTROL] new main log has an unexpected size")
endif()
foreach(path "${rolling_control_out}" "${rolling_control_snapshot}")
    file(READ "${path}" resolved)
    toolx_assert_json_value(ROLLING_SEPARATE_CONTROL_FILE "${resolved}" 1 value)
    toolx_assert_json_value(ROLLING_SEPARATE_CONTROL_FILE "${resolved}" 2 added)
endforeach()
foreach(role base overlay schema)
    file(SHA256 "${rolling_file_${role}}" after)
    if(NOT after STREQUAL "${rolling_input_hash_${role}}")
        message(FATAL_ERROR "[ROLLING_SEPARATE_CONTROL] changed input ${role}")
    endif()
endforeach()
file(READ "${rolling_control_journal}" rolling_control_journal_bytes)
assert_contains(ROLLING_SEPARATE_CONTROL "${rolling_control_journal_bytes}" "FSXJ4\n")
assert_contains(ROLLING_SEPARATE_CONTROL "${rolling_control_journal_bytes}" "COMMIT\n")

run_toolx_sync(ROLLING_JOURNAL_REUSE 0 --base "${rolling_file_base}"
    --overlay "${rolling_file_overlay}" --schema "${rolling_file_schema}"
    --out "${rolling_control_out}" --snapshot "${rolling_control_snapshot}"
    --journal "${rolling_control_journal}" --log-file "${rolling_log}" --json)
toolx_assert_json_envelope(ROLLING_JOURNAL_REUSE "${ROLLING_JOURNAL_REUSE_OUT}"
    "toolx.sync.result" true 0)
file(READ "${rolling_control_journal}" rolling_reused_journal)
assert_contains(ROLLING_JOURNAL_REUSE "${rolling_reused_journal}" "FSXJ4\n")
assert_contains(ROLLING_JOURNAL_REUSE "${rolling_reused_journal}" "COMMIT\n")
file(SHA256 "${rolling_log}.1" rolling_reused_first_hash)
if(NOT rolling_reused_first_hash STREQUAL rolling_threshold_hash)
    message(FATAL_ERROR "[ROLLING_JOURNAL_REUSE] unexpected second rotation")
endif()

# A dangling symlink is an existing filesystem entry, not an ordinary new path.
# With logging enabled, reject an unresolved boundary before opening FileSink.
set(dangling_root "${test_root}/log-dangling")
file(MAKE_DIRECTORY "${dangling_root}")
file(CREATE_LINK "missing-capability-target" "${dangling_root}/capability-link"
    SYMBOLIC RESULT dangling_capability_result)
if(dangling_capability_result STREQUAL "0")
    if(NOT IS_SYMLINK "${dangling_root}/capability-link")
        message(FATAL_ERROR "[R3_DANGLING_CAPABILITY] symbolic link was not created")
    endif()
    file(REMOVE "${dangling_root}/capability-link")

    function(capture_dangling_state root result_var)
        cmake_policy(PUSH)
        cmake_policy(SET CMP0009 NEW)
        file(GLOB_RECURSE entries LIST_DIRECTORIES true RELATIVE "${root}" "${root}/*")
        cmake_policy(POP)
        list(SORT entries)
        set(state "")
        foreach(entry IN LISTS entries)
            set(path "${root}/${entry}")
            if(IS_SYMLINK "${path}")
                file(READ_SYMLINK "${path}" target)
                list(APPEND state "symlink:${entry}:${target}")
            elseif(IS_DIRECTORY "${path}")
                list(APPEND state "directory:${entry}")
            else()
                file(SHA256 "${path}" hash)
                list(APPEND state "file:${entry}:${hash}")
            endif()
        endforeach()
        set("${result_var}" "${state}" PARENT_SCOPE)
    endfunction()

    function(make_dangling_case case_name log_kind)
        set(root "${dangling_root}/${case_name}")
        file(MAKE_DIRECTORY "${root}")
        foreach(role base overlay schema out snapshot journal)
            set("path_${role}" "${root}/${role}.json")
        endforeach()
        set(path_journal "${root}/run.journal")
        file(WRITE "${path_base}" [=[{"value":1}]=])
        file(WRITE "${path_overlay}" [=[{"added":2}]=])
        file(WRITE "${path_schema}" [=[{"type":"object"}]=])
        file(WRITE "${path_out}" "previous output\n")
        file(WRITE "${path_snapshot}" "previous snapshot\n")
        configure_file("${rolling_file_journal}" "${path_journal}" COPYONLY)
        if(log_kind STREQUAL "full")
            # R2 already proved these bytes are the original exact 10 MiB log.
            configure_file("${rolling_log}.1" "${root}/audit.log" COPYONLY)
            file(SIZE "${root}/audit.log" log_size)
            if(NOT log_size EQUAL rolling_limit)
                message(FATAL_ERROR "[${case_name}] full log is not exactly 10 MiB")
            endif()
        elseif(log_kind STREQUAL "small")
            file(WRITE "${root}/audit.log" "small audit log\n")
        elseif(NOT log_kind STREQUAL "absent")
            message(FATAL_ERROR "[${case_name}] unknown log fixture: ${log_kind}")
        endif()
        foreach(slot 2 3 4)
            file(WRITE "${root}/audit.log.${slot}" "untouched archive ${slot}\n")
        endforeach()
        set(dangling_case_root "${root}" PARENT_SCOPE)
        set(dangling_log "${root}/audit.log" PARENT_SCOPE)
        set(dangling_extra_overlays "" PARENT_SCOPE)
        foreach(role base overlay schema out snapshot journal)
            set("dangling_file_${role}" "${path_${role}}" PARENT_SCOPE)
        endforeach()
    endfunction()

    function(make_dangling_link target path)
        file(CREATE_LINK "${target}" "${path}" SYMBOLIC RESULT result)
        if(NOT result STREQUAL "0" OR NOT IS_SYMLINK "${path}")
            message(FATAL_ERROR "[R3_DANGLING_LINK] cannot create ${path} -> ${target}: ${result}")
        endif()
    endfunction()

    function(assert_dangling_rejected case_name role protected_path)
        foreach(arg base overlay schema out snapshot journal)
            set("arg_${arg}" "${dangling_file_${arg}}")
        endforeach()
        if(NOT role STREQUAL "none")
            set("arg_${role}" "${protected_path}")
        endif()
        capture_dangling_state("${dangling_case_root}" before)
        run_toolx_sync(${case_name} 2 --base "${arg_base}"
            --overlay "${arg_overlay}" ${dangling_extra_overlays} --schema "${arg_schema}"
            --out "${arg_out}" --snapshot "${arg_snapshot}" --journal "${arg_journal}"
            --log-file "${dangling_log}" --json)
        toolx_assert_json_envelope(${case_name} "${${case_name}_OUT}" "toolx.sync.result" false 2)
        assert_contains(${case_name} "${${case_name}_OUT}" "--log-file")
        capture_dangling_state("${dangling_case_root}" after)
        if(NOT "${before}" STREQUAL "${after}")
            message(FATAL_ERROR "[${case_name}] rejection changed bytes, link targets, or path inventory\n"
                "before: ${before}\nafter: ${after}")
        endif()
    endfunction()

    # The main log target must be absent to make this journal link dangling.
    make_dangling_case(R3_JOURNAL_MAIN absent)
    make_dangling_link("audit.log" "${dangling_case_root}/journal-link.journal")
    assert_dangling_rejected(R3_JOURNAL_MAIN journal "${dangling_case_root}/journal-link.journal")

    # Reserve archive boundaries even when the current log is below threshold.
    foreach(kind small full)
        foreach(slot 1 5)
            set(case_name "R3_JOURNAL_ARCHIVE_${kind}_${slot}")
            make_dangling_case("${case_name}" "${kind}")
            make_dangling_link("${dangling_log}.${slot}" "${dangling_case_root}/journal-link.journal")
            assert_dangling_rejected("${case_name}" journal "${dangling_case_root}/journal-link.journal")
        endforeach()
    endforeach()
    make_dangling_case(R3_JOURNAL_RELATIVE full)
    make_dangling_link("audit.log.1" "${dangling_case_root}/relative.journal")
    file(RELATIVE_PATH relative_journal "${CMAKE_BINARY_DIR}" "${dangling_case_root}/relative.journal")
    assert_dangling_rejected(R3_JOURNAL_RELATIVE journal "${relative_journal}")
    make_dangling_case(R3_JOURNAL_CHAIN full)
    make_dangling_link("audit.log.5" "${dangling_case_root}/inner-link")
    make_dangling_link("inner-link" "${dangling_case_root}/outer.journal")
    assert_dangling_rejected(R3_JOURNAL_CHAIN journal "${dangling_case_root}/outer.journal")

    # Test the reverse direction before a sink or archive can create the target.
    make_dangling_case(R3_LOG_TO_JOURNAL absent)
    file(REMOVE "${dangling_file_journal}")
    make_dangling_link("run.journal" "${dangling_log}")
    assert_dangling_rejected(R3_LOG_TO_JOURNAL none "")
    # A missing component followed by '..' must not hide the later link. The
    # publish step could otherwise create that component before FileSink retries.
    make_dangling_case(R3_MISSING_PARENT_DOTDOT absent)
    file(REMOVE "${dangling_file_journal}")
    make_dangling_link("run.journal" "${dangling_case_root}/journal-link.log")
    set(dangling_log "${dangling_case_root}/new-dir/../journal-link.log")
    assert_dangling_rejected(R3_MISSING_PARENT_DOTDOT out "${dangling_case_root}/new-dir/out.json")
    foreach(slot 1 5)
        set(case_name "R3_ARCHIVE_TO_JOURNAL_${slot}")
        make_dangling_case("${case_name}" full)
        file(REMOVE "${dangling_file_journal}")
        make_dangling_link("run.journal" "${dangling_log}.${slot}")
        assert_dangling_rejected("${case_name}" none "")
    endforeach()

    # A valid directory link can become dangling before it is used as a parent.
    foreach(side journal log archive)
        set(case_name "R3_DANGLING_PARENT_${side}")
        make_dangling_case("${case_name}" small)
        file(MAKE_DIRECTORY "${dangling_case_root}/missing-parent")
        if(side STREQUAL "archive")
            set(parent_link "${dangling_log}.5")
        else()
            set(parent_link "${dangling_case_root}/parent-link")
        endif()
        make_dangling_link("${dangling_case_root}/missing-parent" "${parent_link}")
        file(REMOVE_RECURSE "${dangling_case_root}/missing-parent")
        if(side STREQUAL "journal")
            assert_dangling_rejected("${case_name}" journal "${parent_link}/run.journal")
        elseif(side STREQUAL "log")
            set(dangling_log "${parent_link}/audit.log")
            assert_dangling_rejected("${case_name}" none "")
        else()
            assert_dangling_rejected("${case_name}" none "")
        endif()
    endforeach()

    # All protected roles, including later overlays, must fail before loading.
    foreach(role base overlay schema out snapshot journal)
        set(case_name "R3_PROTECTED_${role}")
        make_dangling_case("${case_name}" small)
        make_dangling_link("missing-${role}.json" "${dangling_case_root}/protected-link.json")
        assert_dangling_rejected("${case_name}" "${role}" "${dangling_case_root}/protected-link.json")
    endforeach()
    make_dangling_case(R3_SECOND_OVERLAY small)
    make_dangling_link("missing-overlay.json" "${dangling_case_root}/second-overlay.json")
    set(dangling_extra_overlays --overlay "${dangling_case_root}/second-overlay.json")
    assert_dangling_rejected(R3_SECOND_OVERLAY none "")

    # Conservative logging preflight also rejects an unresolved separate link.
    make_dangling_case(R3_SEPARATE_DANGLING_WITH_LOG small)
    make_dangling_link("new-separate.journal" "${dangling_case_root}/journal-link.journal")
    assert_dangling_rejected(R3_SEPARATE_DANGLING_WITH_LOG journal "${dangling_case_root}/journal-link.journal")

    function(assert_dangling_control case_name journal use_log)
        foreach(role base overlay schema)
            file(SHA256 "${dangling_file_${role}}" "input_${role}")
        endforeach()
        set(log_args "")
        if(use_log)
            set(log_args --log-file "${dangling_log}")
        endif()
        if(IS_SYMLINK "${journal}")
            file(READ_SYMLINK "${journal}" link_before)
        endif()
        foreach(attempt first reuse)
            run_toolx_sync("${case_name}_${attempt}" 0 --base "${dangling_file_base}"
                --overlay "${dangling_file_overlay}" --schema "${dangling_file_schema}"
                --out "${dangling_file_out}" --snapshot "${dangling_file_snapshot}"
                --journal "${journal}" ${log_args} --json)
            toolx_assert_json_envelope("${case_name}_${attempt}" "${${case_name}_${attempt}_OUT}"
                "toolx.sync.result" true 0)
            file(READ "${journal}" journal_bytes)
            assert_contains("${case_name}_${attempt}" "${journal_bytes}" "FSXJ4\n")
            assert_contains("${case_name}_${attempt}" "${journal_bytes}" "COMMIT\n")
            foreach(role out snapshot)
                file(READ "${dangling_file_${role}}" resolved)
                toolx_assert_json_value("${case_name}_${attempt}" "${resolved}" 1 value)
                toolx_assert_json_value("${case_name}_${attempt}" "${resolved}" 2 added)
            endforeach()
        endforeach()
        foreach(role base overlay schema)
            file(SHA256 "${dangling_file_${role}}" after)
            if(NOT after STREQUAL "${input_${role}}")
                message(FATAL_ERROR "[${case_name}] changed input ${role}")
            endif()
        endforeach()
        if(DEFINED link_before)
            if(NOT IS_SYMLINK "${journal}")
                message(FATAL_ERROR "[${case_name}] replaced the journal symlink")
            endif()
            file(READ_SYMLINK "${journal}" link_after)
            if(NOT link_after STREQUAL link_before)
                message(FATAL_ERROR "[${case_name}] changed the journal link target")
            endif()
        endif()
    endfunction()

    # No log means the existing journal behaviour remains available: creation
    # through a separate dangling journal link, followed by strict J4 reuse.
    make_dangling_case(R3_SEPARATE_DANGLING_NO_LOG small)
    make_dangling_link("new-separate.journal" "${dangling_case_root}/journal-link.journal")
    assert_dangling_control(R3_SEPARATE_DANGLING_NO_LOG "${dangling_case_root}/journal-link.journal" false)

    # Existing target links remain supported with a genuinely separate logger.
    make_dangling_case(R3_VALID_JOURNAL_LINK small)
    make_dangling_link("run.journal" "${dangling_case_root}/journal-link.journal")
    assert_dangling_control(R3_VALID_JOURNAL_LINK "${dangling_case_root}/journal-link.journal" true)

    # Ordinary missing paths still support first publication and journal reuse.
    make_dangling_case(R3_ORDINARY_NEW_PATHS absent)
    file(REMOVE "${dangling_file_out}" "${dangling_file_snapshot}" "${dangling_file_journal}")
    set(dangling_file_out "${dangling_case_root}/new-dir/out.json")
    set(dangling_file_snapshot "${dangling_case_root}/new-dir/snapshot.json")
    assert_dangling_control(R3_ORDINARY_NEW_PATHS "${dangling_file_journal}" true)
    message(STATUS "R3 dangling-link boundary scenarios passed")
elseif(WIN32)
    message(STATUS "R3 dangling-link boundary scenarios skipped: ${dangling_capability_result}")
else()
    message(FATAL_ERROR "[R3_DANGLING_CAPABILITY] cannot create symlink: ${dangling_capability_result}")
endif()

# Windows stream syntax can alias a file even when neither spelling exists.
# Keep '$DATA' literal, and never require creation of an NTFS stream to test
# early rejection. This group does not depend on symbolic-link privileges.
set(stream_root "${test_root}/log-stream-boundary")
set(stream_default_suffix [=[::$DATA]=])
set(stream_lower_suffix [=[::$data]=])
file(MAKE_DIRECTORY "${stream_root}")

function(capture_stream_state root result_var)
    cmake_policy(PUSH)
    cmake_policy(SET CMP0009 NEW)
    file(GLOB_RECURSE entries LIST_DIRECTORIES true RELATIVE "${root}" "${root}/*")
    cmake_policy(POP)
    list(SORT entries)
    set(state "")
    foreach(entry IN LISTS entries)
        set(path "${root}/${entry}")
        if(IS_SYMLINK "${path}")
            file(READ_SYMLINK "${path}" target)
            list(APPEND state "symlink:${entry}:${target}")
        elseif(IS_DIRECTORY "${path}")
            list(APPEND state "directory:${entry}")
        else()
            file(SHA256 "${path}" hash)
            list(APPEND state "file:${entry}:${hash}")
        endif()
    endforeach()
    set("${result_var}" "${state}" PARENT_SCOPE)
endfunction()

function(make_stream_case case_name log_kind)
    set(root "${stream_root}/${case_name}")
    file(MAKE_DIRECTORY "${root}")
    foreach(role base overlay schema out snapshot journal)
        set("path_${role}" "${root}/${role}.json")
    endforeach()
    set(path_journal "${root}/run.journal")
    file(WRITE "${path_base}" [=[{"value":1}]=])
    file(WRITE "${path_overlay}" [=[{"added":2}]=])
    file(WRITE "${path_schema}" [=[{"type":"object"}]=])
    file(WRITE "${path_out}" "previous output\n")
    file(WRITE "${path_snapshot}" "previous snapshot\n")
    configure_file("${rolling_file_journal}" "${path_journal}" COPYONLY)
    if(log_kind STREQUAL "full")
        configure_file("${rolling_log}.1" "${root}/audit.log" COPYONLY)
        file(SIZE "${root}/audit.log" log_size)
        if(NOT log_size EQUAL rolling_limit)
            message(FATAL_ERROR "[${case_name}] full log is not exactly 10 MiB")
        endif()
    elseif(log_kind STREQUAL "small")
        file(WRITE "${root}/audit.log" "small audit log\n")
    elseif(NOT log_kind STREQUAL "absent")
        message(FATAL_ERROR "[${case_name}] unknown log fixture: ${log_kind}")
    endif()
    # .1 is deliberately missing; other existing archives must remain intact.
    foreach(slot 2 3 4 5)
        file(WRITE "${root}/audit.log.${slot}" "untouched stream archive ${slot}\n")
    endforeach()
    set(stream_case_root "${root}" PARENT_SCOPE)
    set(stream_log "${root}/audit.log" PARENT_SCOPE)
    set(stream_extra_overlays "" PARENT_SCOPE)
    foreach(role base overlay schema out snapshot journal)
        set("stream_file_${role}" "${path_${role}}" PARENT_SCOPE)
    endforeach()
endfunction()

function(assert_stream_rejected case_name role protected_path)
    foreach(arg base overlay schema out snapshot journal)
        set("arg_${arg}" "${stream_file_${arg}}")
    endforeach()
    if(NOT role STREQUAL "none")
        set("arg_${role}" "${protected_path}")
    endif()
    capture_stream_state("${stream_case_root}" before)
    run_toolx_sync(${case_name} 2 --base "${arg_base}"
        --overlay "${arg_overlay}" ${stream_extra_overlays} --schema "${arg_schema}"
        --out "${arg_out}" --snapshot "${arg_snapshot}" --journal "${arg_journal}"
        --log-file "${stream_log}" --json)
    toolx_assert_json_envelope(${case_name} "${${case_name}_OUT}" "toolx.sync.result" false 2)
    assert_contains(${case_name} "${${case_name}_OUT}" "--log-file")
    capture_stream_state("${stream_case_root}" after)
    if(NOT "${before}" STREQUAL "${after}")
        message(FATAL_ERROR "[${case_name}] rejection changed bytes or path inventory\n"
            "before: ${before}\nafter: ${after}")
    endif()
endfunction()

function(assert_stream_control case_name journal)
    foreach(role base overlay schema)
        file(SHA256 "${stream_file_${role}}" "input_${role}")
    endforeach()
    foreach(attempt first reuse)
        run_toolx_sync("${case_name}_${attempt}" 0 --base "${stream_file_base}"
            --overlay "${stream_file_overlay}" --schema "${stream_file_schema}"
            --out "${stream_file_out}" --snapshot "${stream_file_snapshot}"
            --journal "${journal}" --log-file "${stream_log}" --json)
        toolx_assert_json_envelope("${case_name}_${attempt}" "${${case_name}_${attempt}_OUT}"
            "toolx.sync.result" true 0)
        file(READ "${journal}" journal_bytes)
        assert_contains("${case_name}_${attempt}" "${journal_bytes}" "FSXJ4\n")
        assert_contains("${case_name}_${attempt}" "${journal_bytes}" "COMMIT\n")
        if(NOT journal_bytes MATCHES "^FSXJ4\n" OR NOT journal_bytes MATCHES "COMMIT\n$")
            message(FATAL_ERROR "[${case_name}_${attempt}] journal framing was changed or polluted")
        endif()
        foreach(role out snapshot)
            file(READ "${stream_file_${role}}" resolved)
            toolx_assert_json_value("${case_name}_${attempt}" "${resolved}" 1 value)
            toolx_assert_json_value("${case_name}_${attempt}" "${resolved}" 2 added)
        endforeach()
    endforeach()
    foreach(role base overlay schema)
        file(SHA256 "${stream_file_${role}}" after)
        if(NOT after STREQUAL "${input_${role}}")
            message(FATAL_ERROR "[${case_name}] changed input ${role}")
        endif()
    endforeach()
    file(READ "${stream_log}" log_bytes)
    assert_contains("${case_name}" "${log_bytes}" "toolx-sync published")
endfunction()

if(WIN32)
    # Absolute drive-qualified spellings are used throughout these cases.
    make_stream_case(R4_JOURNAL_MAIN_ABSENT absent)
    assert_stream_rejected(R4_JOURNAL_MAIN_ABSENT journal "${stream_log}${stream_default_suffix}")
    foreach(kind small full)
        set(case_name "R4_JOURNAL_ARCHIVE_${kind}")
        make_stream_case("${case_name}" "${kind}")
        assert_stream_rejected("${case_name}" journal "${stream_log}.1${stream_default_suffix}")
    endforeach()
    make_stream_case(R4_LOG_TO_NEW_JOURNAL absent)
    file(REMOVE "${stream_file_journal}")
    set(stream_log "${stream_file_journal}${stream_default_suffix}")
    assert_stream_rejected(R4_LOG_TO_NEW_JOURNAL none "")
    foreach(role base overlay schema out snapshot journal)
        set(case_name "R4_PROTECTED_${role}")
        make_stream_case("${case_name}" small)
        assert_stream_rejected("${case_name}" "${role}"
            "${stream_file_${role}}${stream_default_suffix}")
    endforeach()
    make_stream_case(R4_SECOND_OVERLAY small)
    set(stream_extra_overlays --overlay "${stream_case_root}/second.json${stream_default_suffix}")
    assert_stream_rejected(R4_SECOND_OVERLAY none "")
    make_stream_case(R4_LOWERCASE_STREAM absent)
    assert_stream_rejected(R4_LOWERCASE_STREAM journal "${stream_log}${stream_lower_suffix}")
    make_stream_case(R4_RELATIVE_STREAM full)
    file(RELATIVE_PATH relative_stream "${CMAKE_BINARY_DIR}" "${stream_log}.1${stream_default_suffix}")
    assert_stream_rejected(R4_RELATIVE_STREAM journal "${relative_stream}")
    make_stream_case(R4_DRIVE_RELATIVE_STREAM full)
    file(RELATIVE_PATH relative_stream "${CMAKE_BINARY_DIR}" "${stream_log}.1${stream_default_suffix}")
    string(REGEX MATCH "^[A-Za-z]:" stream_drive "${stream_case_root}")
    if(stream_drive STREQUAL "")
        message(FATAL_ERROR "[R4_DRIVE_RELATIVE_STREAM] expected a Windows drive root")
    endif()
    assert_stream_rejected(R4_DRIVE_RELATIVE_STREAM journal "${stream_drive}${relative_stream}")
    make_stream_case(R4_STREAM_PARENT small)
    assert_stream_rejected(R4_STREAM_PARENT journal "${stream_case_root}/missing:stream/run.journal")
    make_stream_case(R4_LOG_STREAM_PARENT absent)
    set(stream_log "${stream_case_root}/missing:stream/audit.log")
    assert_stream_rejected(R4_LOG_STREAM_PARENT none "")
    make_stream_case(R4_NAMED_STREAM small)
    assert_stream_rejected(R4_NAMED_STREAM out "${stream_case_root}/new.json:named")

    # A normal drive root colon remains valid; ordinary absent paths still work.
    make_stream_case(R4_SEPARATE_NEW_PATHS absent)
    file(REMOVE "${stream_file_out}" "${stream_file_snapshot}" "${stream_file_journal}")
    set(stream_file_out "${stream_case_root}/new-dir/out.json")
    set(stream_file_snapshot "${stream_case_root}/new-dir/snapshot.json")
    assert_stream_control(R4_SEPARATE_NEW_PATHS "${stream_file_journal}")

    # A Windows extended-drive prefix has a drive-designator colon that is
    # valid at this root position. It does not permit a later stream suffix.
    set(stream_extended_prefix [=[\\?\]=])
    make_stream_case(R4_EXTENDED_DRIVE_LOG absent)
    file(TO_NATIVE_PATH "${stream_log}" stream_native_path)
    set(stream_log "${stream_extended_prefix}${stream_native_path}")
    assert_stream_control(R4_EXTENDED_DRIVE_LOG "${stream_file_journal}")
    make_stream_case(R4_EXTENDED_DRIVE_JOURNAL absent)
    file(TO_NATIVE_PATH "${stream_file_journal}" stream_native_path)
    set(stream_extended_journal "${stream_extended_prefix}${stream_native_path}")
    assert_stream_control(R4_EXTENDED_DRIVE_JOURNAL "${stream_extended_journal}")
    make_stream_case(R4_EXTENDED_DRIVE_STREAM absent)
    file(TO_NATIVE_PATH "${stream_log}" stream_native_path)
    set(stream_extended_alias "${stream_extended_prefix}${stream_native_path}${stream_default_suffix}")
    assert_stream_rejected(R4_EXTENDED_DRIVE_STREAM journal "${stream_extended_alias}")
    message(STATUS "R4 Windows stream-boundary scenarios passed")
else()
    # On POSIX these spellings name distinct ordinary files, so the Windows
    # syntax restriction must not reject them. Verify actual publication/reuse.
    make_stream_case(R4_POSIX_COLON_JOURNAL absent)
    file(REMOVE "${stream_file_journal}")
    assert_stream_control(R4_POSIX_COLON_JOURNAL "${stream_log}${stream_default_suffix}")
    make_stream_case(R4_POSIX_COLON_LOG absent)
    file(REMOVE "${stream_file_journal}")
    set(stream_log "${stream_file_journal}${stream_default_suffix}")
    assert_stream_control(R4_POSIX_COLON_LOG "${stream_file_journal}")
    message(STATUS "R4 POSIX colon-filename compatibility scenarios passed")
endif()
