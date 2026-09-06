cmake_minimum_required(VERSION 3.20)
if(NOT EXISTS "${TOOLX_SOURCE_ARCHIVE}")
    message(FATAL_ERROR "TOOLX_SOURCE_ARCHIVE must exist")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E tar tf "${TOOLX_SOURCE_ARCHIVE}"
    RESULT_VARIABLE result OUTPUT_VARIABLE listing ERROR_VARIABLE error)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "Cannot list source archive: ${error}")
endif()
string(REPLACE "\n" ";" entries "${listing}")
foreach(entry IN LISTS entries)
    string(STRIP "${entry}" entry)
    if(entry MATCHES "[.]gc(ov|da|no)$" OR
       entry MATCHES "^[^/]+/(reference|temp|build[^/]*|stage|[.]git|[.]vscode|[.]third_party)(/|$)" OR
       (entry MATCHES "[.]log$" AND NOT entry MATCHES "/examples/product_chain_showcase/fixtures/[^/]+[.]log$"))
        message(FATAL_ERROR "Unwanted source archive entry: ${entry}")
    endif()
endforeach()
message(STATUS "Source archive contents verified")
