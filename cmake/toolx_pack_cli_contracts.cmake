if(NOT DEFINED TOOLX_PACK_EXE)
    message(FATAL_ERROR "TOOLX_PACK_EXE is required")
endif()

if(NOT EXISTS "${TOOLX_PACK_EXE}")
    message(FATAL_ERROR "toolx-pack executable not found: ${TOOLX_PACK_EXE}")
endif()

set(test_root "${CMAKE_BINARY_DIR}/toolx_pack_cli_contracts")
file(REMOVE_RECURSE "${test_root}")
file(MAKE_DIRECTORY "${test_root}")

set(src_dir "${test_root}/src")
set(stage_dir "${test_root}/stage")
set(plan_stage_dir "${test_root}/plan-stage")
set(archive_path "${test_root}/toolx-pack.tar")
set(plan_archive_path "${test_root}/plan.tar")
set(archive_only_path "${test_root}/archive-only.tar")
set(manifest_json "${test_root}/pack.json")
set(bad_manifest_json "${test_root}/bad-pack.json")

file(MAKE_DIRECTORY
    "${src_dir}/bin"
    "${src_dir}/config"
    "${src_dir}/debug"
    "${src_dir}/tmp"
    "${stage_dir}/obsolete")
file(WRITE "${src_dir}/bin/tool.exe" "tool binary\n")
file(WRITE "${src_dir}/README.md" "# Demo\n")
file(WRITE "${src_dir}/LICENSE" "MIT\n")
file(WRITE "${src_dir}/config/default.json" [=[{"enabled":true}]=])
file(WRITE "${src_dir}/debug/app.pdb" "debug symbols\n")
file(WRITE "${src_dir}/tmp/cache.tmp" "cache\n")
file(WRITE "${stage_dir}/obsolete/old.txt" "old\n")
file(WRITE "${stage_dir}/old.txt" "old\n")

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

run_toolx_pack(MISSING_SOURCE 3 stage --src "${test_root}/missing" --out "${test_root}/missing-stage" --json)
assert_contains(MISSING_SOURCE "${MISSING_SOURCE_OUT}" "\"code\": 3")
assert_contains(MISSING_SOURCE "${MISSING_SOURCE_OUT}" "source directory not found")

run_toolx_pack(PARENT_INCLUDE_REJECTED 2
    stage
    --src "${src_dir}"
    --out "${test_root}/unsafe-stage"
    --include "../outside"
    --json)
assert_contains(PARENT_INCLUDE_REJECTED "${PARENT_INCLUDE_REJECTED_OUT}" "\"code\": 2")
assert_contains(PARENT_INCLUDE_REJECTED "${PARENT_INCLUDE_REJECTED_OUT}" "parent traversal is not allowed")

run_toolx_pack(BAD_MANIFEST 4 stage --manifest "${bad_manifest_json}" --json)
assert_contains(BAD_MANIFEST "${BAD_MANIFEST_OUT}" "\"code\": 4")
assert_contains(BAD_MANIFEST "${BAD_MANIFEST_OUT}" "manifest validation failed")

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
if(EXISTS "${plan_stage_dir}")
    message(FATAL_ERROR "toolx-pack plan created ${plan_stage_dir}")
endif()
if(EXISTS "${plan_archive_path}")
    message(FATAL_ERROR "toolx-pack plan created ${plan_archive_path}")
endif()

run_toolx_pack(STAGE_JSON 0 stage --manifest "${manifest_json}" --json)
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"schema\": \"toolx.pack.result\"")
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"message\": \"staged\"")
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"archive_format\": \"tar\"")
assert_contains(STAGE_JSON "${STAGE_JSON_OUT}" "\"entries\": 4")

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

run_toolx_pack(ARCHIVE_JSON 0 archive --src "${stage_dir}" --archive "${archive_only_path}" --json)
assert_contains(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "\"schema\": \"toolx.pack.result\"")
assert_contains(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "\"message\": \"archived\"")
assert_contains(ARCHIVE_JSON "${ARCHIVE_JSON_OUT}" "\"archive_format\": \"tar\"")
if(NOT EXISTS "${archive_only_path}")
    message(FATAL_ERROR "toolx-pack archive did not create ${archive_only_path}")
endif()
