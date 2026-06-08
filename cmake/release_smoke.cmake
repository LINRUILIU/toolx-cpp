if(NOT DEFINED TOOLX_STAGE_PREFIX)
    message(FATAL_ERROR "TOOLX_STAGE_PREFIX is required")
endif()

if(CMAKE_HOST_WIN32)
    set(exe_suffix ".exe")
else()
    set(exe_suffix "")
endif()

set(toolx_config_exe "${TOOLX_STAGE_PREFIX}/bin/toolx-config${exe_suffix}")
set(toolx_sync_exe "${TOOLX_STAGE_PREFIX}/bin/toolx-sync${exe_suffix}")
set(toolx_pack_exe "${TOOLX_STAGE_PREFIX}/bin/toolx-pack${exe_suffix}")
set(toolx_http_exe "${TOOLX_STAGE_PREFIX}/bin/toolx-http${exe_suffix}")
set(toolx_log_exe "${TOOLX_STAGE_PREFIX}/bin/toolx-log${exe_suffix}")

foreach(tool IN ITEMS "${toolx_config_exe}" "${toolx_sync_exe}" "${toolx_pack_exe}" "${toolx_http_exe}" "${toolx_log_exe}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "Installed tool is missing: ${tool}")
    endif()
endforeach()

set(smoke_root "${TOOLX_STAGE_PREFIX}/_release_smoke")
file(REMOVE_RECURSE "${smoke_root}")
file(MAKE_DIRECTORY "${smoke_root}")

set(app_json "${smoke_root}/app.json")
set(schema_json "${smoke_root}/schema.json")
set(resolved_json "${smoke_root}/resolved.json")
file(WRITE "${app_json}" [=[{"svc":{"host":"127.0.0.1","port":8080}}]=])
file(WRITE "${schema_json}" [=[{"type":"object","required":["svc"],"properties":{"svc":{"type":"object","required":["host","port"],"properties":{"host":{"type":"string","minLength":1},"port":{"type":"integer","minimum":1,"maximum":65535}}}}}]=])

function(run_smoke case_name expected_code)
    execute_process(
        COMMAND ${ARGN}
        RESULT_VARIABLE actual_code
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )

    if(NOT actual_code EQUAL expected_code)
        message(FATAL_ERROR
            "[${case_name}] expected exit ${expected_code}, got ${actual_code}\n"
            "command: ${ARGN}\n"
            "stdout:\n${stdout}\n"
            "stderr:\n${stderr}")
    endif()

    set("${case_name}_OUT" "${stdout}" PARENT_SCOPE)
endfunction()

function(assert_contains case_name text needle)
    string(FIND "${text}" "${needle}" found_at)
    if(found_at EQUAL -1)
        message(FATAL_ERROR "[${case_name}] expected output to contain: ${needle}\nactual:\n${text}")
    endif()
endfunction()

run_smoke(TOOLX_CONFIG_HELP 0 "${toolx_config_exe}" --help)
assert_contains(TOOLX_CONFIG_HELP "${TOOLX_CONFIG_HELP_OUT}" "toolx-config - thin CLI over cfgx")

run_smoke(TOOLX_CONFIG_DOCTOR 0 "${toolx_config_exe}" doctor --file "${app_json}" --schema "${schema_json}" --require svc.host --expect svc.port=int --json)
assert_contains(TOOLX_CONFIG_DOCTOR "${TOOLX_CONFIG_DOCTOR_OUT}" "\"message\": \"doctor passed\"")
assert_contains(TOOLX_CONFIG_DOCTOR "${TOOLX_CONFIG_DOCTOR_OUT}" "\"schema_issues\": []")

run_smoke(TOOLX_CONFIG_SET 0 "${toolx_config_exe}" set --file "${app_json}" --path svc.port --value 9090 --type int)
run_smoke(TOOLX_CONFIG_GET 0 "${toolx_config_exe}" get --file "${app_json}" --path svc.port)
assert_contains(TOOLX_CONFIG_GET "${TOOLX_CONFIG_GET_OUT}" "9090")

run_smoke(TOOLX_SYNC 0
    "${toolx_sync_exe}"
    --base "${app_json}"
    --out "${resolved_json}"
    --schema "${schema_json}"
    --require svc.port
    --range svc.port=1:65535
    --json)
assert_contains(TOOLX_SYNC "${TOOLX_SYNC_OUT}" "\"schema\": \"toolx.sync.result\"")
assert_contains(TOOLX_SYNC "${TOOLX_SYNC_OUT}" "\"schema_issues\": []")

if(NOT EXISTS "${resolved_json}")
    message(FATAL_ERROR "toolx-sync release smoke did not create ${resolved_json}")
endif()

run_smoke(TOOLX_PACK_HELP 0 "${toolx_pack_exe}" --help)
assert_contains(TOOLX_PACK_HELP "${TOOLX_PACK_HELP_OUT}" "toolx-pack - stage release trees")

run_smoke(TOOLX_HTTP_HELP 0 "${toolx_http_exe}" --help)
assert_contains(TOOLX_HTTP_HELP "${TOOLX_HTTP_HELP_OUT}" "toolx-http - preflight runtime HTTP endpoints")

run_smoke(TOOLX_LOG_HELP 0 "${toolx_log_exe}" --help)
assert_contains(TOOLX_LOG_HELP "${TOOLX_LOG_HELP_OUT}" "toolx-log - summarize runtime logs")

set(log_file "${smoke_root}/app.log")
file(WRITE "${log_file}" "2026-06-04 10:00:00.000 INFO ready\n2026-06-04 10:01:00.000 ERROR failed\n")
run_smoke(TOOLX_LOG_SUMMARY 0 "${toolx_log_exe}" summarize --file "${log_file}" --min-level error --json)
assert_contains(TOOLX_LOG_SUMMARY "${TOOLX_LOG_SUMMARY_OUT}" "\"schema\": \"toolx.log.result\"")
assert_contains(TOOLX_LOG_SUMMARY "${TOOLX_LOG_SUMMARY_OUT}" "\"matched\": 1")

set(pack_src "${smoke_root}/pack-src")
set(pack_stage "${smoke_root}/pack-stage")
set(pack_archive "${smoke_root}/pack.tar")
file(MAKE_DIRECTORY "${pack_src}/bin" "${pack_src}/debug")
file(WRITE "${pack_src}/bin/tool.txt" "tool\n")
file(WRITE "${pack_src}/README.md" "demo\n")
file(WRITE "${pack_src}/debug/tool.pdb" "debug\n")

run_smoke(TOOLX_PACK_STAGE 0
    "${toolx_pack_exe}"
    stage
    --src "${pack_src}"
    --out "${pack_stage}"
    --archive "${pack_archive}"
    --include bin
    --include README.md
    --exclude "**/*.pdb"
    --json)
assert_contains(TOOLX_PACK_STAGE "${TOOLX_PACK_STAGE_OUT}" "\"schema\": \"toolx.pack.result\"")
assert_contains(TOOLX_PACK_STAGE "${TOOLX_PACK_STAGE_OUT}" "\"message\": \"staged\"")

if(NOT EXISTS "${pack_stage}/bin/tool.txt")
    message(FATAL_ERROR "toolx-pack release smoke did not stage ${pack_stage}/bin/tool.txt")
endif()
if(NOT EXISTS "${pack_archive}")
    message(FATAL_ERROR "toolx-pack release smoke did not create ${pack_archive}")
endif()
