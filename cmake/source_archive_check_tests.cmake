cmake_minimum_required(VERSION 3.20)
set(root "${CMAKE_CURRENT_BINARY_DIR}/source-archive-fixtures")
# Reset only this test's dedicated fixture directory, including a previous
# interrupted negative case, so repeat runs exercise the same archive contents.
get_filename_component(root "${root}" ABSOLUTE)
file(REMOVE_RECURSE "${root}")
file(MAKE_DIRECTORY "${root}/ToolX-source/docs" "${root}/ToolX-source/examples/product_chain_showcase/fixtures"
    "${root}/ToolX-source/experiments/recipe")
file(WRITE "${root}/ToolX-source/docs/build-and-compatibility.md" "build guide")
file(WRITE "${root}/ToolX-source/examples/product_chain_showcase/fixtures/app.log" "fixture")
file(WRITE "${root}/ToolX-source/experiments/recipe/prepare.py" "print('source script')")
file(WRITE "${root}/ToolX-source/experiments/recipe/README.md" "experiment recipe")
file(WRITE "${root}/ToolX-source/docs/cache.pyconfig.md" "similar text is allowed")
foreach(output_root IN ITEMS toolx_test_tmp release-docs-fixture source-archive-fixtures)
    file(MAKE_DIRECTORY "${root}/ToolX-source/${output_root}-notes"
        "${root}/ToolX-source/experiments/${output_root}")
    file(WRITE "${root}/ToolX-source/${output_root}.md" "similar source filename is allowed")
    file(WRITE "${root}/ToolX-source/${output_root}-notes/README.md" "similar directory is allowed")
    file(WRITE "${root}/ToolX-source/docs/${output_root}" "only the output root is reserved")
    file(WRITE "${root}/ToolX-source/experiments/${output_root}/prepare.py" "print('experiment source')")
endforeach()
file(REMOVE "${root}/ToolX-source/local.gcov"
    "${root}/ToolX-source/experiments/recipe/prepare.pyc"
    "${root}/ToolX-source/experiments/recipe/prepare.pyo")
file(REMOVE_RECURSE "${root}/ToolX-source/reference" "${root}/ToolX-source/.third_party"
    "${root}/ToolX-source/experiments/recipe/__pycache__")
function(check success)
    execute_process(COMMAND "${CMAKE_COMMAND}" -E tar czf "${root}/source.tar.gz" ToolX-source
        WORKING_DIRECTORY "${root}" RESULT_VARIABLE packed)
    if(NOT packed EQUAL 0)
        message(FATAL_ERROR "Cannot create archive fixture")
    endif()
    execute_process(COMMAND "${CMAKE_COMMAND}" "-DTOOLX_SOURCE_ARCHIVE=${root}/source.tar.gz"
        -P "${CMAKE_CURRENT_LIST_DIR}/source_archive_check.cmake"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(success AND NOT result EQUAL 0)
        message(FATAL_ERROR "Archive checker rejected source docs/log fixtures")
    elseif(NOT success AND result EQUAL 0)
        message(FATAL_ERROR "Archive checker accepted generated or local-only files")
    elseif(NOT success AND NOT "${output}${error}" MATCHES "Unwanted source archive entry:")
        message(FATAL_ERROR "Archive checker failed for an unexpected reason: ${output}${error}")
    endif()
endfunction()
check(TRUE)
file(MAKE_DIRECTORY "${root}/ToolX-source/reference")
file(WRITE "${root}/ToolX-source/reference/local.txt" "must not ship")
check(FALSE)

file(REMOVE_RECURSE "${root}/ToolX-source/reference")
file(MAKE_DIRECTORY "${root}/ToolX-source/.third_party")
file(WRITE "${root}/ToolX-source/.third_party/mbedtls.zip" "offline archive must not ship")
check(FALSE)
file(REMOVE_RECURSE "${root}/ToolX-source/.third_party")
check(TRUE)

file(WRITE "${root}/ToolX-source/local.gcov" "compiler intermediate must not ship")
check(FALSE)
file(REMOVE "${root}/ToolX-source/local.gcov")
check(TRUE)

file(MAKE_DIRECTORY "${root}/ToolX-source/experiments/recipe/__pycache__")
file(WRITE "${root}/ToolX-source/experiments/recipe/__pycache__/prepare.cpython-313.pyc" "cache must not ship")
check(FALSE)
file(REMOVE_RECURSE "${root}/ToolX-source/experiments/recipe/__pycache__")
check(TRUE)

foreach(extension IN ITEMS pyc pyo)
    file(WRITE "${root}/ToolX-source/experiments/recipe/prepare.${extension}" "bytecode must not ship")
    check(FALSE)
    file(REMOVE "${root}/ToolX-source/experiments/recipe/prepare.${extension}")
    check(TRUE)
endforeach()

# Reject exact known output roots both as directories and as plain files.
# Similar names and nested experiment sources above remain in every good case.
foreach(output_root IN ITEMS toolx_test_tmp release-docs-fixture source-archive-fixtures)
    file(MAKE_DIRECTORY "${root}/ToolX-source/${output_root}/generated-fixtures")
    file(WRITE "${root}/ToolX-source/${output_root}/generated-fixtures/record.txt" "runtime fixture must not ship")
    check(FALSE)
    file(REMOVE_RECURSE "${root}/ToolX-source/${output_root}")
    check(TRUE)
    file(WRITE "${root}/ToolX-source/${output_root}" "runtime output root must not ship")
    check(FALSE)
    file(REMOVE "${root}/ToolX-source/${output_root}")
    check(TRUE)
endforeach()
# Generated dependency/runtime directories must be excluded at any depth, while
# similarly named source directories remain valid.
foreach(runtime_dir IN ITEMS node_modules .runs .venv .pytest_cache .pytest-tmp)
    file(MAKE_DIRECTORY "${root}/ToolX-source/experiments/recipe/${runtime_dir}-notes")
    file(WRITE "${root}/ToolX-source/experiments/recipe/${runtime_dir}-notes/README.md" "source notes")
    check(TRUE)
    file(MAKE_DIRECTORY "${root}/ToolX-source/experiments/recipe/${runtime_dir}")
    file(WRITE "${root}/ToolX-source/experiments/recipe/${runtime_dir}/generated.txt" "must not ship")
    check(FALSE)
    file(REMOVE_RECURSE "${root}/ToolX-source/experiments/recipe/${runtime_dir}")
    check(TRUE)
endforeach()
message(STATUS "Source archive checker directory/file negatives and source-preservation controls passed")
