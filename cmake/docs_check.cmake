cmake_minimum_required(VERSION 3.20)

get_filename_component(TOOLX_REPOSITORY_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(TO_CMAKE_PATH "${TOOLX_REPOSITORY_ROOT}" TOOLX_REPOSITORY_ROOT)

function(toolx_docs_error message_text)
    set_property(GLOBAL APPEND PROPERTY TOOLX_DOC_ERRORS "${message_text}")
endfunction()

function(toolx_require_file relative_path)
    if(NOT EXISTS "${TOOLX_REPOSITORY_ROOT}/${relative_path}")
        toolx_docs_error("missing required file: ${relative_path}")
    endif()
endfunction()

function(toolx_require_contains relative_path expected_text)
    if(NOT EXISTS "${TOOLX_REPOSITORY_ROOT}/${relative_path}")
        toolx_docs_error("cannot inspect missing file: ${relative_path}")
        return()
    endif()
    file(READ "${TOOLX_REPOSITORY_ROOT}/${relative_path}" content)
    string(FIND "${content}" "${expected_text}" found_at)
    if(found_at EQUAL -1)
        toolx_docs_error("${relative_path} does not contain required text: ${expected_text}")
    endif()
endfunction()

function(toolx_validate_markdown relative_path)
    file(READ "${TOOLX_REPOSITORY_ROOT}/${relative_path}" content)
    if(NOT content MATCHES "^#[ ]+[^\r\n]+")
        toolx_docs_error("${relative_path} is missing a top-level H1")
    endif()
    foreach(field IN ITEMS "Audience:" "Status:" "Applies to:" "Source of truth for:")
        string(FIND "${content}" "${field}" field_at)
        if(field_at EQUAL -1)
            toolx_docs_error("${relative_path} is missing metadata field ${field}")
        endif()
    endforeach()

endfunction()

function(toolx_validate_local_links relative_path)
    file(READ "${TOOLX_REPOSITORY_ROOT}/${relative_path}" content)
    get_filename_component(document_dir
        "${TOOLX_REPOSITORY_ROOT}/${relative_path}" DIRECTORY)
    string(REGEX MATCHALL "\\[[^]]*\\]\\([^\r\n)]+\\)" local_links "${content}")
    foreach(link IN LISTS local_links)
        string(REGEX REPLACE "^.*\\]\\(([^)]+)\\)$" "\\1" target "${link}")
        string(REGEX REPLACE "[ \t]+\"[^\"]*\"$" "" target "${target}")
        string(REGEX REPLACE "^<([^>]+)>$" "\\1" target "${target}")
        if(target MATCHES "^(https?://|mailto:|#)")
            continue()
        endif()
        string(REGEX REPLACE "#.*$" "" target "${target}")
        if(target STREQUAL "")
            continue()
        endif()
        get_filename_component(resolved_target
            "${document_dir}/${target}" ABSOLUTE)
        if(NOT EXISTS "${resolved_target}")
            file(RELATIVE_PATH reported_target
                "${TOOLX_REPOSITORY_ROOT}" "${resolved_target}")
            toolx_docs_error("${relative_path} has a missing local link: ${target} (${reported_target})")
        endif()
    endforeach()
endfunction()

set(TOOLX_MODULES
    argtool asyncx cfgx fsx hashx httpx logsys resultx schemax sysx
    textcodec tuix utils)
set(TOOLX_CLIS
    toolx-config toolx-sync toolx-pack toolx-http toolx-log toolx-inspect)

set(TOOLX_PUBLIC_DOCUMENTS
    docs/architecture.md
    docs/build-and-compatibility.md
    docs/dependencies.md
    docs/stability.md
    docs/roadmap.md
    docs/cli/matrix.md
    docs/examples/showcase.md
    docs/examples/module-cookbooks.md
    docs/examples/product-chain.md
    docs/releases/v0.1.0.md
    docs/releases/v0.2.0.md
    docs/releases/v0.3.0.md
    docs/releases/v0.3.1.md
    docs/releases/v0.3.2.md)
foreach(module IN LISTS TOOLX_MODULES)
    list(APPEND TOOLX_PUBLIC_DOCUMENTS "docs/modules/${module}.md")
endforeach()
foreach(cli IN LISTS TOOLX_CLIS)
    list(APPEND TOOLX_PUBLIC_DOCUMENTS "docs/cli/${cli}.md")
endforeach()

set(TOOLX_INTERNAL_DOCUMENTS
    docs/development/maintaining.md
    docs/development/cli-productization.md
    docs/development/audits/api_inventory.md
    docs/development/audits/api_product_gap_audit.md
    docs/development/audits/api_test_matrix.md
    docs/development/audits/security-v032.md
    docs/development/design/toolx-http-design.md
    docs/development/design/toolx-pack-design.md
    docs/development/design/toolx-log-design.md
    docs/development/design/toolx-inspect-design.md
    docs/development/retrospectives/productization-retro.md)

foreach(required_file IN ITEMS
        README.md README.dev.md CHANGELOG.md
        docs/index.md docs/cli/README.md docs/modules/README.md
        docs/development/index.md docs/releases/template.md
        ${TOOLX_PUBLIC_DOCUMENTS} ${TOOLX_INTERNAL_DOCUMENTS})
    toolx_require_file("${required_file}")
endforeach()

foreach(module IN LISTS TOOLX_MODULES)
    toolx_require_contains("docs/index.md" "modules/${module}.md")
    if(module STREQUAL "resultx")
        toolx_require_contains("CMakeLists.txt" "add_library(resultx INTERFACE)")
    else()
        toolx_require_contains("CMakeLists.txt" "toolx_add_module(${module})")
    endif()
endforeach()
foreach(cli IN LISTS TOOLX_CLIS)
    toolx_require_contains("docs/index.md" "cli/${cli}.md")
    string(REPLACE "-" "_" cli_target "${cli}")
    toolx_require_contains("CMakeLists.txt" "add_executable(${cli_target}")
    toolx_require_contains("CMakeLists.txt" "OUTPUT_NAME \"${cli}\"")
endforeach()
toolx_require_contains("docs/index.md" "modules/")
toolx_require_contains("docs/index.md" "cli/")
toolx_require_contains("docs/index.md" "development/index.md")
foreach(document IN LISTS TOOLX_PUBLIC_DOCUMENTS)
    if(document MATCHES "^docs/(modules|cli)/")
        continue()
    endif()
    string(REGEX REPLACE "^docs/" "" index_target "${document}")
    toolx_require_contains("docs/index.md" "${index_target}")
endforeach()
foreach(document IN LISTS TOOLX_INTERNAL_DOCUMENTS)
    string(REGEX REPLACE "^docs/development/" "" index_target "${document}")
    toolx_require_contains("docs/development/index.md" "${index_target}")
endforeach()

set(TOOLX_RELOCATIONS
    "docs/toolx-config.md|cli/toolx-config.md"
    "docs/toolx-sync.md|cli/toolx-sync.md"
    "docs/toolx-pack.md|cli/toolx-pack.md"
    "docs/toolx-http.md|cli/toolx-http.md"
    "docs/toolx-log.md|cli/toolx-log.md"
    "docs/toolx-inspect.md|cli/toolx-inspect.md"
    "docs/product_cli_matrix.md|cli/matrix.md"
    "docs/toolx-http-design.md|development/design/toolx-http-design.md"
    "docs/toolx-pack-design.md|development/design/toolx-pack-design.md"
    "docs/toolx-log-design.md|development/design/toolx-log-design.md"
    "docs/toolx-inspect-design.md|development/design/toolx-inspect-design.md"
    "docs/api_inventory.md|development/audits/api_inventory.md"
    "docs/api_product_gap_audit.md|development/audits/api_product_gap_audit.md"
    "docs/api_test_matrix.md|development/audits/api_test_matrix.md"
    "docs/productization-retro.md|development/retrospectives/productization-retro.md"
    "docs/cli_productization.md|development/cli-productization.md"
    "docs/product_tools.md|architecture.md")
foreach(relocation IN LISTS TOOLX_RELOCATIONS)
    string(REPLACE "|" ";" relocation_parts "${relocation}")
    list(GET relocation_parts 0 old_path)
    list(GET relocation_parts 1 new_path)
    toolx_require_file("${old_path}")
    toolx_require_contains("${old_path}" "${new_path}")
    toolx_require_contains("${old_path}" "Relocation notice; not a source of truth")
endforeach()

foreach(showcase_file IN ITEMS
        examples/product_chain_showcase/server.cpp
        examples/product_chain_showcase/run.ps1
        examples/product_chain_showcase/run.sh
        examples/product_chain_showcase/fixtures/app.base.json
        examples/product_chain_showcase/fixtures/app.local.json
        examples/product_chain_showcase/fixtures/schema.json
        examples/product_chain_showcase/fixtures/app.log
        examples/product_chain_showcase/fixtures/package-src/README.md
        examples/product_chain_showcase/fixtures/package-src/bin/tool.txt
        docs/assets/showcase/toolx-inspect-frame.txt
        docs/assets/showcase/toolx-inspect-frame.svg)
    toolx_require_file("${showcase_file}")
endforeach()

set(TOOLX_COOKBOOK_SCENARIO_COUNT 0)
foreach(module IN LISTS TOOLX_MODULES)
    set(cookbook_path
        "${TOOLX_REPOSITORY_ROOT}/examples/${module}_cookbook.cpp")
    if(NOT EXISTS "${cookbook_path}")
        toolx_docs_error("missing module cookbook: examples/${module}_cookbook.cpp")
        continue()
    endif()
    file(READ "${cookbook_path}" cookbook_content)
    string(REGEX MATCHALL "// Scenario [0-9]+:" cookbook_scenarios
        "${cookbook_content}")
    list(LENGTH cookbook_scenarios cookbook_scenario_count)
    math(EXPR TOOLX_COOKBOOK_SCENARIO_COUNT
        "${TOOLX_COOKBOOK_SCENARIO_COUNT} + ${cookbook_scenario_count}")
endforeach()
if(NOT TOOLX_COOKBOOK_SCENARIO_COUNT EQUAL 59)
    toolx_docs_error(
        "cookbook report says 59 scenarios, but source contains ${TOOLX_COOKBOOK_SCENARIO_COUNT}")
endif()
toolx_require_contains("docs/examples/product-chain.md" "../assets/showcase/toolx-inspect-frame.txt")
toolx_require_contains("docs/examples/product-chain.md" "../assets/showcase/toolx-inspect-frame.svg")
toolx_require_contains("examples/product_chain_showcase/run.ps1" "--no-proxy-from-env")
toolx_require_contains("examples/product_chain_showcase/run.sh" "--no-proxy-from-env")

toolx_require_contains("CMakeLists.txt" "project(ToolX VERSION 0.3.2")
toolx_require_contains("README.md" "v0.3.2")
toolx_require_contains("README.md" "release candidate")
toolx_require_contains("README.md" "v0.3.1")
toolx_require_contains("CHANGELOG.md" "## [Unreleased]")
toolx_require_contains("CHANGELOG.md" "Target: `v0.3.2` release candidate")
toolx_require_contains("docs/releases/v0.3.2.md" "Status: Candidate; not yet tagged")
foreach(release_note IN ITEMS
        docs/releases/template.md
        docs/releases/v0.1.0.md
        docs/releases/v0.2.0.md
        docs/releases/v0.3.0.md
        docs/releases/v0.3.1.md
        docs/releases/v0.3.2.md)
    foreach(section IN ITEMS
            "## Highlights"
            "## Compatibility"
            "## Build and dependency impact"
            "## Migration"
            "## Known limitations")
        toolx_require_contains("${release_note}" "${section}")
    endforeach()
endforeach()
foreach(dependency_class IN ITEMS
        "| Required |"
        "| Fetched |"
        "| Optional |"
        "| System |"
        "| Development-only |"
        "| Packaged |")
    toolx_require_contains("docs/dependencies.md" "${dependency_class}")
endforeach()

file(GLOB_RECURSE TOOLX_MARKDOWN_FILES
    RELATIVE "${TOOLX_REPOSITORY_ROOT}"
    "${TOOLX_REPOSITORY_ROOT}/docs/*.md")
set(TOOLX_CLASSIFIED_DOCUMENTS
    docs/index.md
    docs/cli/README.md
    docs/modules/README.md
    docs/development/index.md
    docs/releases/template.md
    ${TOOLX_PUBLIC_DOCUMENTS}
    ${TOOLX_INTERNAL_DOCUMENTS})
foreach(relocation IN LISTS TOOLX_RELOCATIONS)
    string(REPLACE "|" ";" relocation_parts "${relocation}")
    list(GET relocation_parts 0 old_path)
    list(APPEND TOOLX_CLASSIFIED_DOCUMENTS "${old_path}")
endforeach()
foreach(markdown_file IN LISTS TOOLX_MARKDOWN_FILES)
    list(FIND TOOLX_CLASSIFIED_DOCUMENTS "${markdown_file}" classified_at)
    if(classified_at EQUAL -1)
        toolx_docs_error("unclassified/orphan Markdown document: ${markdown_file}")
    endif()
    toolx_validate_markdown("${markdown_file}")
    toolx_validate_local_links("${markdown_file}")
endforeach()
foreach(root_markdown IN ITEMS README.md README.dev.md CHANGELOG.md)
    toolx_validate_local_links("${root_markdown}")
endforeach()

get_property(TOOLX_DOC_ERRORS GLOBAL PROPERTY TOOLX_DOC_ERRORS)
if(TOOLX_DOC_ERRORS)
    list(REMOVE_DUPLICATES TOOLX_DOC_ERRORS)
    list(JOIN TOOLX_DOC_ERRORS "\n  - " TOOLX_DOC_ERROR_TEXT)
    message(FATAL_ERROR "ToolX documentation check failed:\n  - ${TOOLX_DOC_ERROR_TEXT}")
endif()

list(LENGTH TOOLX_MARKDOWN_FILES TOOLX_MARKDOWN_COUNT)
list(LENGTH TOOLX_MODULES TOOLX_MODULE_COUNT)
list(LENGTH TOOLX_CLIS TOOLX_CLI_COUNT)
message(STATUS
    "ToolX documentation check passed: ${TOOLX_MARKDOWN_COUNT} Markdown files, "
    "${TOOLX_MODULE_COUNT} modules, ${TOOLX_CLI_COUNT} CLIs")
