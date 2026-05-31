if(DEFINED PACKAGE_FILE)
    set(package_file "${PACKAGE_FILE}")
elseif(DEFINED PACKAGE_DIR)
    file(GLOB package_candidates LIST_DIRECTORIES FALSE
        "${PACKAGE_DIR}/ToolX-v*-*.zip"
        "${PACKAGE_DIR}/ToolX-v*-*.tar.gz")

    foreach(candidate IN LISTS package_candidates)
        get_filename_component(candidate_name "${candidate}" NAME)
        if(NOT candidate_name MATCHES "-source\\.")
            set(package_file "${candidate}")
            break()
        endif()
    endforeach()
else()
    message(FATAL_ERROR "PACKAGE_FILE or PACKAGE_DIR is required")
endif()

if(NOT DEFINED package_file OR package_file STREQUAL "")
    message(FATAL_ERROR "No binary release archive was found")
endif()

if(NOT EXISTS "${package_file}")
    message(FATAL_ERROR "Release archive does not exist: ${package_file}")
endif()

if(CMAKE_HOST_WIN32)
    set(exe_suffix ".exe")
else()
    set(exe_suffix "")
endif()

set(extract_root "${CMAKE_CURRENT_LIST_DIR}/../temp/release-archive-smoke")
if(DEFINED EXTRACT_ROOT AND NOT EXTRACT_ROOT STREQUAL "")
    set(extract_root "${EXTRACT_ROOT}")
endif()

file(REMOVE_RECURSE "${extract_root}")
file(MAKE_DIRECTORY "${extract_root}")
file(ARCHIVE_EXTRACT INPUT "${package_file}" DESTINATION "${extract_root}")

set(package_root "")
if(EXISTS "${extract_root}/lib/cmake/ToolX/ToolXConfig.cmake")
    set(package_root "${extract_root}")
else()
    file(GLOB extracted_entries LIST_DIRECTORIES TRUE "${extract_root}/*")
    foreach(entry IN LISTS extracted_entries)
        if(IS_DIRECTORY "${entry}" AND EXISTS "${entry}/lib/cmake/ToolX/ToolXConfig.cmake")
            set(package_root "${entry}")
            break()
        endif()
    endforeach()
endif()

if(package_root STREQUAL "")
    message(FATAL_ERROR "Unable to locate extracted package root under ${extract_root}")
endif()

foreach(required_path IN ITEMS
    "${package_root}/bin"
    "${package_root}/include"
    "${package_root}/lib"
    "${package_root}/lib/cmake/ToolX")
    if(NOT EXISTS "${required_path}")
        message(FATAL_ERROR "Expected installed path is missing from release archive: ${required_path}")
    endif()
endforeach()

set(cfgtool_exe "${package_root}/bin/cfgtool${exe_suffix}")
set(toolx_sync_exe "${package_root}/bin/toolx-sync${exe_suffix}")
foreach(tool IN ITEMS "${cfgtool_exe}" "${toolx_sync_exe}")
    if(NOT EXISTS "${tool}")
        message(FATAL_ERROR "Expected packaged tool is missing: ${tool}")
    endif()
endforeach()

function(run_checked case_name expected_code)
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

run_checked(CFGTOOL_HELP 0 "${cfgtool_exe}" --help)
assert_contains(CFGTOOL_HELP "${CFGTOOL_HELP_OUT}" "cfgtool - thin CLI over cfgx")

set(doctor_json "${extract_root}/doctor-app.json")
set(schema_json "${extract_root}/doctor-schema.json")
file(WRITE "${doctor_json}" [=[{"svc":{"host":"127.0.0.1","port":8080}}]=])
file(WRITE "${schema_json}" [=[{"type":"object","required":["svc"],"properties":{"svc":{"type":"object","required":["host","port"],"properties":{"host":{"type":"string","minLength":1},"port":{"type":"integer","minimum":1,"maximum":65535}}}}}]=])
run_checked(CFGTOOL_DOCTOR 0 "${cfgtool_exe}" doctor --file "${doctor_json}" --schema "${schema_json}" --require svc.host --expect svc.port=int --json)
assert_contains(CFGTOOL_DOCTOR "${CFGTOOL_DOCTOR_OUT}" "\"message\": \"doctor passed\"")
assert_contains(CFGTOOL_DOCTOR "${CFGTOOL_DOCTOR_OUT}" "\"schema_issues\": []")

run_checked(TOOLX_SYNC_HELP 0 "${toolx_sync_exe}" --help)
assert_contains(TOOLX_SYNC_HELP "${TOOLX_SYNC_HELP_OUT}" "toolx-sync - validate and atomically publish composed config")

set(sync_json "${extract_root}/resolved.json")
run_checked(TOOLX_SYNC_SCHEMA 0
    "${toolx_sync_exe}"
    --base "${doctor_json}"
    --out "${sync_json}"
    --schema "${schema_json}"
    --require svc.port
    --range svc.port=1:65535
    --json)
assert_contains(TOOLX_SYNC_SCHEMA "${TOOLX_SYNC_SCHEMA_OUT}" "\"schema\": \"toolx.sync.result\"")
assert_contains(TOOLX_SYNC_SCHEMA "${TOOLX_SYNC_SCHEMA_OUT}" "\"schema_issues\": []")

set(consumer_source_dir "${CMAKE_CURRENT_LIST_DIR}/../examples/install_consumer")
set(consumer_build_dir "${extract_root}/consumer-build")
file(REMOVE_RECURSE "${consumer_build_dir}")

set(configure_command
    "${CMAKE_COMMAND}"
    "-S" "${consumer_source_dir}"
    "-B" "${consumer_build_dir}"
    "-DCMAKE_PREFIX_PATH=${package_root}")
if(DEFINED VERIFY_CMAKE_GENERATOR AND NOT VERIFY_CMAKE_GENERATOR STREQUAL "")
    list(APPEND configure_command "-G" "${VERIFY_CMAKE_GENERATOR}")
endif()

execute_process(
    COMMAND ${configure_command}
    RESULT_VARIABLE consumer_configure_code
    OUTPUT_VARIABLE consumer_configure_out
    ERROR_VARIABLE consumer_configure_err
)
if(NOT consumer_configure_code EQUAL 0)
    message(FATAL_ERROR
        "Failed to configure install_consumer against packaged archive\n"
        "stdout:\n${consumer_configure_out}\n"
        "stderr:\n${consumer_configure_err}")
endif()

set(consumer_build_command "${CMAKE_COMMAND}" --build "${consumer_build_dir}" --parallel)
if(DEFINED VERIFY_BUILD_CONFIG AND NOT VERIFY_BUILD_CONFIG STREQUAL "")
    list(APPEND consumer_build_command --config "${VERIFY_BUILD_CONFIG}")
endif()

execute_process(
    COMMAND ${consumer_build_command}
    RESULT_VARIABLE consumer_build_code
    OUTPUT_VARIABLE consumer_build_out
    ERROR_VARIABLE consumer_build_err
)
if(NOT consumer_build_code EQUAL 0)
    message(FATAL_ERROR
        "Failed to build install_consumer against packaged archive\n"
        "stdout:\n${consumer_build_out}\n"
        "stderr:\n${consumer_build_err}")
endif()
