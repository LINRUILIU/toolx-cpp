if(NOT DEFINED TOOLX_STAGE_PREFIX)
    message(FATAL_ERROR "TOOLX_STAGE_PREFIX is required")
endif()

if(CMAKE_HOST_WIN32)
    set(exe_suffix ".exe")
else()
    set(exe_suffix "")
endif()

set(cfgtool_exe "${TOOLX_STAGE_PREFIX}/bin/cfgtool${exe_suffix}")
set(toolx_sync_exe "${TOOLX_STAGE_PREFIX}/bin/toolx-sync${exe_suffix}")

foreach(tool IN ITEMS "${cfgtool_exe}" "${toolx_sync_exe}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "Installed tool is missing: ${tool}")
    endif()
endforeach()

set(smoke_root "${TOOLX_STAGE_PREFIX}/_release_smoke")
file(REMOVE_RECURSE "${smoke_root}")
file(MAKE_DIRECTORY "${smoke_root}")

set(app_json "${smoke_root}/app.json")
set(resolved_json "${smoke_root}/resolved.json")
file(WRITE "${app_json}" [=[{"svc":{"host":"127.0.0.1","port":8080}}]=])

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

run_smoke(CFGTOOL_HELP 0 "${cfgtool_exe}" --help)
assert_contains(CFGTOOL_HELP "${CFGTOOL_HELP_OUT}" "cfgtool - thin CLI over cfgx")

run_smoke(CFGTOOL_SET 0 "${cfgtool_exe}" set --file "${app_json}" --path svc.port --value 9090 --type int)
run_smoke(CFGTOOL_GET 0 "${cfgtool_exe}" get --file "${app_json}" --path svc.port)
assert_contains(CFGTOOL_GET "${CFGTOOL_GET_OUT}" "9090")

run_smoke(TOOLX_SYNC 0
    "${toolx_sync_exe}"
    --base "${app_json}"
    --out "${resolved_json}"
    --require svc.port
    --range svc.port=1:65535
    --json)
assert_contains(TOOLX_SYNC "${TOOLX_SYNC_OUT}" "\"schema\": \"toolx.sync.result\"")

if(NOT EXISTS "${resolved_json}")
    message(FATAL_ERROR "toolx-sync release smoke did not create ${resolved_json}")
endif()
