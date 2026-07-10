function(toolx_assert_json_path case_name json_text)
    string(JSON _toolx_json_value ERROR_VARIABLE _toolx_json_error GET "${json_text}" ${ARGN})
    if(NOT _toolx_json_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR
            "[${case_name}] expected JSON path to exist: ${ARGN}\n"
            "error: ${_toolx_json_error}\n"
            "actual:\n${json_text}")
    endif()
endfunction()

function(toolx_assert_json_value case_name json_text expected_value)
    set(_toolx_expected "${expected_value}")
    if(_toolx_expected STREQUAL "true")
        set(_toolx_expected "ON")
    elseif(_toolx_expected STREQUAL "false")
        set(_toolx_expected "OFF")
    endif()

    string(JSON _toolx_json_value ERROR_VARIABLE _toolx_json_error GET "${json_text}" ${ARGN})
    if(NOT _toolx_json_error STREQUAL "NOTFOUND")
        message(FATAL_ERROR
            "[${case_name}] expected JSON path to exist: ${ARGN}\n"
            "error: ${_toolx_json_error}\n"
            "actual:\n${json_text}")
    endif()
    if(NOT "${_toolx_json_value}" STREQUAL "${_toolx_expected}")
        message(FATAL_ERROR
            "[${case_name}] expected JSON path ${ARGN} to equal '${expected_value}', got '${_toolx_json_value}'\n"
            "actual:\n${json_text}")
    endif()
endfunction()

function(toolx_assert_json_envelope case_name json_text expected_schema expected_ok expected_code)
    toolx_assert_json_value("${case_name}" "${json_text}" "${expected_schema}" schema)
    toolx_assert_json_value("${case_name}" "${json_text}" "1" schema_version)
    toolx_assert_json_value("${case_name}" "${json_text}" "${expected_ok}" ok)
    toolx_assert_json_value("${case_name}" "${json_text}" "${expected_code}" code)
    toolx_assert_json_path("${case_name}" "${json_text}" issues)
    toolx_assert_json_path("${case_name}" "${json_text}" data)
endfunction()
