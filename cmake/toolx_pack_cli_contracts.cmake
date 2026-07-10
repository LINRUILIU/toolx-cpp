if(NOT DEFINED TOOLX_PACK_EXE)
    message(FATAL_ERROR "TOOLX_PACK_EXE is required")
endif()

if(NOT EXISTS "${TOOLX_PACK_EXE}")
    message(FATAL_ERROR "toolx-pack executable not found: ${TOOLX_PACK_EXE}")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/toolx_cli_contract_helpers.cmake")

set(test_root "${CMAKE_BINARY_DIR}/toolx_pack_cli_contracts")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(src_dir "${test_root}/src")
set(stage_dir "${test_root}/stage")
set(plan_stage_dir "${test_root}/plan-stage")
set(archive_path "${test_root}/toolx-pack.tar")
set(plan_archive_path "${test_root}/plan.tar")
set(archive_only_path "${test_root}/archive-only.tar")
set(stage_dry_run_dir "${test_root}/stage-dry-run")
set(stage_dry_run_archive "${test_root}/stage-dry-run.tar")
set(stage_dry_run_journal "${test_root}/stage-dry-run.journal")
set(stage_dry_run_log "${test_root}/stage-dry-run.log")
set(archive_dry_run_path "${test_root}/archive-dry-run.tar")
set(archive_dry_run_log "${test_root}/archive-dry-run.log")
set(override_stage_dir "${test_root}/override-stage")
set(override_archive_path "${test_root}/override.tar")
set(journal_path "${test_root}/stage.journal")
set(manifest_json "${test_root}/pack.json")
set(override_manifest_json "${test_root}/override-pack.json")
set(bad_manifest_json "${test_root}/bad-pack.json")

file(MAKE_DIRECTORY
    "${src_dir}/bin"
    "${src_dir}/config"
    "${src_dir}/debug"
    "${src_dir}/tmp"
    "${stage_dir}/obsolete"
    "${stage_dry_run_dir}"
    "${plan_stage_dir}"
    "${override_stage_dir}")
file(WRITE "${src_dir}/bin/tool.exe" "tool binary\n")
file(WRITE "${src_dir}/README.md" "# Demo\n")
file(WRITE "${src_dir}/LICENSE" "MIT\n")
file(WRITE "${src_dir}/config/default.json" [=[{"enabled":true}]=])
file(WRITE "${src_dir}/debug/app.pdb" "debug symbols\n")
file(WRITE "${src_dir}/tmp/cache.tmp" "cache\n")
file(WRITE "${stage_dir}/obsolete/old.txt" "old\n")
file(WRITE "${stage_dir}/old.txt" "old\n")
file(WRITE "${stage_dry_run_dir}/sentinel.txt" "stage dry-run sentinel\n")
file(WRITE "${stage_dry_run_archive}" "stage dry-run archive sentinel\n")
file(WRITE "${stage_dry_run_journal}" "stage dry-run journal sentinel\n")
file(WRITE "${stage_dry_run_log}" "stage dry-run log sentinel\n")
file(WRITE "${archive_dry_run_path}" "archive dry-run sentinel\n")
file(WRITE "${archive_dry_run_log}" "archive dry-run log sentinel\n")
file(WRITE "${plan_stage_dir}/sentinel.txt" "plan stage sentinel\n")
file(WRITE "${plan_archive_path}" "plan archive sentinel\n")
file(WRITE "${override_stage_dir}/stale.txt" "stale\n")

file(WRITE "${manifest_json}" [=[
{
  "name": "demo",
  "version": "1.2.3",
  "source": "@SRC_DIR@",
  "stage": "@STAGE_DIR@",
  "archive": "@ARCHIVE_PATH@",
  "include": ["bin", "README.md", "LICENSE", "config", "debug"],
  "exclude": ["**/*.pdb"],
  "remove_extra": true
}
]=])
file(READ "${manifest_json}" manifest_text)
string(REPLACE "@SRC_DIR@" "${src_dir}" manifest_text "${manifest_text}")
string(REPLACE "@STAGE_DIR@" "${stage_dir}" manifest_text "${manifest_text}")
string(REPLACE "@ARCHIVE_PATH@" "${archive_path}" manifest_text "${manifest_text}")
file(WRITE "${manifest_json}" "${manifest_text}")

file(WRITE "${bad_manifest_json}" [=[{"source":"src","unexpected":true}]=])
file(WRITE "${override_manifest_json}" [=[
{
  "name": "demo-override",
  "version": "9.9.9",
  "source": "@TEST_ROOT@/manifest-src",
  "stage": "@TEST_ROOT@/manifest-stage",
  "archive": "@TEST_ROOT@/manifest.tar",
  "include": ["LICENSE"],
  "exclude": ["debug/**"],
  "remove_extra": false
}
]=])
file(READ "${override_manifest_json}" override_manifest_text)
string(REPLACE "@TEST_ROOT@" "${test_root}" override_manifest_text "${override_manifest_text}")
file(WRITE "${override_manifest_json}" "${override_manifest_text}")

function(run_toolx_pack case_name expected_code)
    execute_process(
        COMMAND "${TOOLX_PACK_EXE}" ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(NOT actual_code EQUAL expected_code)
        message(FATAL_ERROR
            "[${case_name}] expected exit ${expected_code}, got ${actual_code}\n"
            "command: ${TOOLX_PACK_EXE} ${ARGN}\n"
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

run_toolx_pack(HELP 0 --help)
assert_contains(HELP "${HELP_OUT}" "toolx-pack - stage release trees")
assert_contains(HELP "${HELP_OUT}" "archive")
assert_contains(HELP "${HELP_OUT}" "plan")

run_toolx_pack(MISSING_REQUIRED 2 stage --json)
assert_contains(MISSING_REQUIRED "${MISSING_REQUIRED_OUT}" "\"code\": 2")
assert_contains(MISSING_REQUIRED "${MISSING_REQUIRED_OUT}" "missing required option --src")
toolx_assert_json_envelope(MISSING_REQUIRED "${MISSING_REQUIRED_OUT}" "toolx.pack.result" false 2)

run_toolx_pack(MISSING_SOURCE 3 stage --src "${test_root}/missing" --out "${test_root}/missing-stage" --json)
assert_contains(MISSING_SOURCE "${MISSING_SOURCE_OUT}" "\"code\": 3")
assert_contains(MISSING_SOURCE "${MISSING_SOURCE_OUT}" "source directory not found")
toolx_assert_json_envelope(MISSING_SOURCE "${MISSING_SOURCE_OUT}" "toolx.pack.result" false 3)

run_toolx_pack(PARENT_INCLUDE_REJECTED 2
    stage
    --src "${src_dir}"
    --out "${test_root}/unsafe-stage"
    --include "../outside"
    --json)
assert_contains(PARENT_INCLUDE_REJECTED "${PARENT_INCLUDE_REJECTED_OUT}" "\"code\": 2")
assert_contains(PARENT_INCLUDE_REJECTED "${PARENT_INCLUDE_REJECTED_OUT}" "parent traversal is not allowed")
toolx_assert_json_envelope(PARENT_INCLUDE_REJECTED "${PARENT_INCLUDE_REJECTED_OUT}" "toolx.pack.result" false 2)

run_toolx_pack(ABSOLUTE_INCLUDE_REJECTED 2
    stage
    --src "${src_dir}"
    --out "${test_root}/absolute-include-stage"
    --include "${src_dir}/README.md"
    --json)
assert_contains(ABSOLUTE_INCLUDE_REJECTED "${ABSOLUTE_INCLUDE_REJECTED_OUT}" "\"code\": 2")
assert_contains(ABSOLUTE_INCLUDE_REJECTED "${ABSOLUTE_INCLUDE_REJECTED_OUT}" "absolute paths are not allowed")
toolx_assert_json_envelope(ABSOLUTE_INCLUDE_REJECTED "${ABSOLUTE_INCLUDE_REJECTED_OUT}" "toolx.pack.result" false 2)

run_toolx_pack(QUESTION_EXCLUDE_REJECTED 2
    stage
    --src "${src_dir}"
    --out "${test_root}/question-exclude-stage"
    --exclude "tmp/?.tmp"
    --json)
assert_contains(QUESTION_EXCLUDE_REJECTED "${QUESTION_EXCLUDE_REJECTED_OUT}" "\"code\": 2")
assert_contains(QUESTION_EXCLUDE_REJECTED "${QUESTION_EXCLUDE_REJECTED_OUT}" "the '?' wildcard is not supported")
toolx_assert_json_envelope(QUESTION_EXCLUDE_REJECTED "${QUESTION_EXCLUDE_REJECTED_OUT}" "toolx.pack.result" false 2)

run_toolx_pack(BAD_MANIFEST 4 stage --manifest "${bad_manifest_json}" --json)
assert_contains(BAD_MANIFEST "${BAD_MANIFEST_OUT}" "\"code\": 4")
assert_contains(BAD_MANIFEST "${BAD_MANIFEST_OUT}" "manifest validation failed")
toolx_assert_json_envelope(BAD_MANIFEST "${BAD_MANIFEST_OUT}" "toolx.pack.result" false 4)

file(READ "${stage_dry_run_dir}/sentinel.txt" stage_dry_run_file_before)
file(READ "${stage_dry_run_archive}" stage_dry_run_archive_before)
file(READ "${stage_dry_run_journal}" stage_dry_run_journal_before)
file(READ "${stage_dry_run_log}" stage_dry_run_log_before)
run_toolx_pack(STAGE_DRY_RUN_JSON 0
    stage
    --src "${src_dir}"
    --out "${stage_dry_run_dir}"
    --archive "${stage_dry_run_archive}"
    --journal "${stage_dry_run_journal}"
    --log-file "${stage_dry_run_log}"
    --include bin
    --dry-run
    --json)
toolx_assert_json_envelope(STAGE_DRY_RUN_JSON "${STAGE_DRY_RUN_JSON_OUT}" "toolx.pack.result" true 0)
toolx_assert_json_value(STAGE_DRY_RUN_JSON "${STAGE_DRY_RUN_JSON_OUT}" "stage" data command)
toolx_assert_json_value(STAGE_DRY_RUN_JSON "${STAGE_DRY_RUN_JSON_OUT}" true data dry_run)
toolx_assert_json_path(STAGE_DRY_RUN_JSON "${STAGE_DRY_RUN_JSON_OUT}" data planned_steps)
file(READ "${stage_dry_run_dir}/sentinel.txt" stage_dry_run_file_after)
file(READ "${stage_dry_run_archive}" stage_dry_run_archive_after)
file(READ "${stage_dry_run_journal}" stage_dry_run_journal_after)
file(READ "${stage_dry_run_log}" stage_dry_run_log_after)
if(NOT "${stage_dry_run_file_before}" STREQUAL "${stage_dry_run_file_after}")
    message(FATAL_ERROR "toolx-pack stage --dry-run modified stage sentinel")
endif()
if(EXISTS "${stage_dry_run_dir}/bin/tool.exe")
    message(FATAL_ERROR "toolx-pack stage --dry-run created staged file")
endif()
if(NOT "${stage_dry_run_archive_before}" STREQUAL "${stage_dry_run_archive_after}")
    message(FATAL_ERROR "toolx-pack stage --dry-run modified archive sentinel")
endif()
if(NOT "${stage_dry_run_journal_before}" STREQUAL "${stage_dry_run_journal_after}")
    message(FATAL_ERROR "toolx-pack stage --dry-run modified journal sentinel")
endif()
if(NOT "${stage_dry_run_log_before}" STREQUAL "${stage_dry_run_log_after}")
    message(FATAL_ERROR "toolx-pack stage --dry-run modified log sentinel")
endif()

file(READ "${plan_stage_dir}/sentinel.txt" plan_stage_before)
file(READ "${plan_archive_path}" plan_archive_before)
run_toolx_pack(PLAN_JSON 0
    plan
    --src "${src_dir}"
    --out "${plan_stage_dir}"
    --archive "${plan_archive_path}"
    --include bin
    --include README.md
    --json)
assert_contains(PLAN_JSON "${PLAN_JSON_OUT}" "\"schema\": \"toolx.pack.result\"")
assert_contains(PLAN_JSON "${PLAN_JSON_OUT}" "\"command\": \"plan\"")
assert_contains(PLAN_JSON "${PLAN_JSON_OUT}" "\"dry_run\": true")
assert_contains(PLAN_JSON "${PLAN_JSON_OUT}" "\"planned_steps\"")
toolx_assert_json_envelope(PLAN_JSON "${PLAN_JSON_OUT}" "toolx.pack.result" true 0)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" "plan" data command)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" "${src_dir}" data source)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" "${plan_stage_dir}" data stage)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" "${plan_archive_path}" data archive)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" true data dry_run)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" false data remove_extra)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" "2" data entries)
toolx_assert_json_path(PLAN_JSON "${PLAN_JSON_OUT}" data bytes)
toolx_assert_json_path(PLAN_JSON "${PLAN_JSON_OUT}" data planned_steps)
toolx_assert_json_path(PLAN_JSON "${PLAN_JSON_OUT}" data completed_steps)
toolx_assert_json_value(PLAN_JSON "${PLAN_JSON_OUT}" "tar" data archive_format)
toolx_assert_json_path(PLAN_JSON "${PLAN_JSON_OUT}" data capabilities)
toolx_assert_json_path(PLAN_JSON "${PLAN_JSON_OUT}" data warnings)
file(READ "${plan_stage_dir}/sentinel.txt" plan_stage_after)
file(READ "${plan_archive_path}" plan_archive_after)
if(NOT "${plan_stage_before}" STREQUAL "${plan_stage_after}")
    message(FATAL_ERROR "toolx-pack plan modified ${plan_stage_dir}/sentinel.txt")
endif()
if(EXISTS "${plan_stage_dir}/bin/tool.exe")
    message(FATAL_ERROR "toolx-pack plan created staged file")
endif()
if(NOT "${plan_archive_before}" STREQUAL "${plan_archive_after}")
    message(FATAL_ERROR "toolx-pack plan modified ${plan_archive_path}")
endif()

run_toolx_pack(STAGE_JSON 0 stage --manifest "${manifest_json}" --journal "${journal_path}" --json)
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"schema\": \"toolx.pack.result\"")
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"message\": \"staged\"")
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"archive_format\": \"tar\"")
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"entries\": 4")
toolx_assert_json_envelope(STAGE_JSON "${STAGE_JSON_OUT}" "toolx.pack.result" true 0)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "stage" data command)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "${src_dir}" data source)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "${stage_dir}" data stage)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "${archive_path}" data archive)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "${manifest_json}" data manifest)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" false data dry_run)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" true data remove_extra)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "4" data entries)
toolx_assert_json_path(STAGE_JSON "${STAGE_JSON_OUT}" data bytes)
toolx_assert_json_path(STAGE_JSON "${STAGE_JSON_OUT}" data planned_steps)
toolx_assert_json_path(STAGE_JSON "${STAGE_JSON_OUT}" data completed_steps)
toolx_assert_json_value(STAGE_JSON "${STAGE_JSON_OUT}" "tar" data archive_format)
toolx_assert_json_path(STAGE_JSON "${STAGE_JSON_OUT}" data capabilities)
toolx_assert_json_path(STAGE_JSON "${STAGE_JSON_OUT}" data warnings)

foreach(expected_path IN ITEMS
    "${stage_dir}/bin/tool.exe"
    "${stage_dir}/README.md"
    "${stage_dir}/LICENSE"
    "${stage_dir}/config/default.json")
    if(NOT EXISTS "${expected_path}")
        message(FATAL_ERROR "toolx-pack stage did not create ${expected_path}")
    endif()
endforeach()

foreach(unexpected_path IN ITEMS
    "${stage_dir}/debug/app.pdb"
    "${stage_dir}/old.txt"
    "${stage_dir}/obsolete/old.txt")
    if(EXISTS "${unexpected_path}")
        message(FATAL_ERROR "toolx-pack stage left unexpected path ${unexpected_path}")
    endif()
endforeach()

if(NOT EXISTS "${archive_path}")
    message(FATAL_ERROR "toolx-pack stage --archive did not create ${archive_path}")
endif()
if(NOT EXISTS "${journal_path}")
    message(FATAL_ERROR "toolx-pack stage did not create ${journal_path}")
endif()

run_toolx_pack(MANIFEST_CLI_OVERRIDE 0
    stage
    --manifest "${override_manifest_json}"
    --src "${src_dir}"
    --out "${override_stage_dir}"
    --archive "${override_archive_path}"
    --include README.md
    --exclude LICENSE
    --remove-extra
    --json)
toolx_assert_json_envelope(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "toolx.pack.result" true 0)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "stage" data command)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "${src_dir}" data source)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "${override_stage_dir}" data stage)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "${override_archive_path}" data archive)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "${override_manifest_json}" data manifest)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" true data remove_extra)
toolx_assert_json_value(MANIFEST_CLI_OVERRIDE "${MANIFEST_CLI_OVERRIDE_OUT}" "1" data entries)
if(NOT EXISTS "${override_stage_dir}/README.md")
    message(FATAL_ERROR "toolx-pack manifest override did not stage CLI include")
endif()
if(EXISTS "${override_stage_dir}/LICENSE")
    message(FATAL_ERROR "toolx-pack manifest override kept manifest include")
endif()
if(EXISTS "${override_stage_dir}/stale.txt")
    message(FATAL_ERROR "toolx-pack manifest override did not apply CLI --remove-extra")
endif()
if(NOT EXISTS "${override_archive_path}")
    message(FATAL_ERROR "toolx-pack manifest override did not create CLI archive")
endif()

run_toolx_pack(ARCHIVE_JSON 0 archive --src "${stage_dir}" --archive "${archive_only_path}" --json)
assert_contains(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "\"schema\": \"toolx.pack.result\"")
assert_contains(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "\"message\": \"archived\"")
assert_contains(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "\"archive_format\": \"tar\"")
toolx_assert_json_envelope(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "toolx.pack.result" true 0)
toolx_assert_json_value(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "archive" data command)
toolx_assert_json_value(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "${stage_dir}" data source)
toolx_assert_json_value(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "${archive_only_path}" data archive)
toolx_assert_json_value(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" false data dry_run)
toolx_assert_json_path(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" data entries)
toolx_assert_json_path(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" data bytes)
toolx_assert_json_path(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" data planned_steps)
toolx_assert_json_path(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" data completed_steps)
toolx_assert_json_value(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "tar" data archive_format)
if(NOT EXISTS "${archive_only_path}")
    message(FATAL_ERROR "toolx-pack archive did not create ${archive_only_path}")
endif()

file(READ "${archive_dry_run_path}" archive_dry_run_before)
file(READ "${archive_dry_run_log}" archive_dry_run_log_before)
run_toolx_pack(ARCHIVE_DRY_RUN_JSON 0
    archive
    --src "${stage_dir}"
    --archive "${archive_dry_run_path}"
    --log-file "${archive_dry_run_log}"
    --dry-run
    --json)
toolx_assert_json_envelope(ARCHIVE_DRY_RUN_JSON "${ARCHIVE_DRY_RUN_JSON_OUT}" "toolx.pack.result" true 0)
toolx_assert_json_value(ARCHIVE_DRY_RUN_JSON "${ARCHIVE_DRY_RUN_JSON_OUT}" "archive" data command)
toolx_assert_json_value(ARCHIVE_DRY_RUN_JSON "${ARCHIVE_DRY_RUN_JSON_OUT}" true data dry_run)
toolx_assert_json_path(ARCHIVE_DRY_RUN_JSON "${ARCHIVE_DRY_RUN_JSON_OUT}" data planned_steps)
file(READ "${archive_dry_run_path}" archive_dry_run_after)
file(READ "${archive_dry_run_log}" archive_dry_run_log_after)
if(NOT "${archive_dry_run_before}" STREQUAL "${archive_dry_run_after}")
    message(FATAL_ERROR "toolx-pack archive --dry-run modified archive sentinel")
endif()
if(NOT "${archive_dry_run_log_before}" STREQUAL "${archive_dry_run_log_after}")
    message(FATAL_ERROR "toolx-pack archive --dry-run modified log sentinel")
endif()
