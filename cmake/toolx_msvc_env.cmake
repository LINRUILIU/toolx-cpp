set(TOOLX_MSVC_ROOT
    ""
    CACHE PATH "Optional MSVC toolset root, for example C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231")
option(TOOLX_REQUIRE_MSVC_ROOT "Require TOOLX_MSVC_ROOT when using a Visual Studio generator on Windows." OFF)

if(WIN32)
    set(_toolx_msvc_issues)
    set(_toolx_msvc_context FALSE)
    if(CMAKE_GENERATOR MATCHES "Visual Studio")
        set(_toolx_msvc_context TRUE)
    elseif(NOT TOOLX_MSVC_ROOT STREQUAL "")
        set(_toolx_msvc_context TRUE)
    elseif(DEFINED CMAKE_CXX_COMPILER AND CMAKE_CXX_COMPILER MATCHES "(^|[/\\\\])cl(\\.exe)?$")
        set(_toolx_msvc_context TRUE)
    endif()

    if(NOT TOOLX_MSVC_ROOT STREQUAL "")
        file(TO_CMAKE_PATH "${TOOLX_MSVC_ROOT}" _toolx_msvc_root)
        set(_toolx_msvc_cl "${_toolx_msvc_root}/bin/Hostx64/x64/cl.exe")
        if(NOT EXISTS "${_toolx_msvc_cl}")
            message(FATAL_ERROR
                "TOOLX_MSVC_ROOT does not point to a usable x64 MSVC toolset.\n"
                "Expected compiler: ${_toolx_msvc_cl}")
        endif()

        set(TOOLX_MSVC_CL "${_toolx_msvc_cl}" CACHE FILEPATH "MSVC cl.exe selected by TOOLX_MSVC_ROOT." FORCE)

        if(CMAKE_GENERATOR MATCHES "Ninja")
            if(NOT DEFINED CMAKE_CXX_COMPILER)
                set(CMAKE_CXX_COMPILER "${_toolx_msvc_cl}" CACHE FILEPATH "C++ compiler selected by TOOLX_MSVC_ROOT." FORCE)
            endif()
            if(NOT DEFINED CMAKE_C_COMPILER)
                set(CMAKE_C_COMPILER "${_toolx_msvc_cl}" CACHE FILEPATH "C compiler selected by TOOLX_MSVC_ROOT." FORCE)
            endif()
        endif()
    elseif(TOOLX_REQUIRE_MSVC_ROOT AND CMAKE_GENERATOR MATCHES "Visual Studio")
        list(APPEND _toolx_msvc_issues
            "TOOLX_REQUIRE_MSVC_ROOT is ON, but TOOLX_MSVC_ROOT was not provided.")
    endif()

    set(_toolx_env_path_value "")

    if(DEFINED ENV{Path})
        set(_toolx_env_path_value "$ENV{Path}")
    endif()

    foreach(_toolx_env_path IN ITEMS "${_toolx_env_path_value}")
        if(NOT _toolx_env_path STREQUAL "")
            file(TO_CMAKE_PATH "${_toolx_env_path}" _toolx_env_path_normalized)
            string(FIND "${_toolx_env_path_normalized}" "/VC/Tools/MSVC/14.50.35717/" _toolx_old_msvc_at)
            if(NOT _toolx_old_msvc_at EQUAL -1)
                list(APPEND _toolx_msvc_issues
                    "PATH contains removed MSVC toolset 14.50.35717. Remove that entry or replace it with the installed toolset.")
            endif()
        endif()
    endforeach()

    if(_toolx_msvc_issues)
        list(REMOVE_DUPLICATES _toolx_msvc_issues)
    endif()

    if(_toolx_msvc_issues AND CMAKE_GENERATOR MATCHES "Visual Studio")
        string(REPLACE ";" "\n  - " _toolx_msvc_issue_text "${_toolx_msvc_issues}")
        message(FATAL_ERROR
            "ToolX detected a broken Windows/MSVC environment before compiler detection:\n"
            "  - ${_toolx_msvc_issue_text}\n\n"
            "Remove stale MSVC bin entries, then configure in a fresh build directory.\n"
            "To pin the local toolset explicitly, pass:\n"
            "  -DTOOLX_MSVC_ROOT=\"C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.51.36231\"\n"
            "For Visual Studio generators, also use a matching toolset version, for example:\n"
            "  -T v145,version=14.51.36231")
    elseif(_toolx_msvc_issues AND _toolx_msvc_context)
        string(REPLACE ";" "\n  - " _toolx_msvc_issue_text "${_toolx_msvc_issues}")
        message(WARNING
            "ToolX detected a suspicious Windows/MSVC environment:\n"
            "  - ${_toolx_msvc_issue_text}")
    endif()
endif()

unset(_toolx_msvc_issues)
unset(_toolx_msvc_context)
unset(_toolx_msvc_root)
unset(_toolx_msvc_cl)
unset(_toolx_env_path_value)
unset(_toolx_env_path)
unset(_toolx_env_path_normalized)
unset(_toolx_old_msvc_at)
unset(_toolx_msvc_issue_text)
