set(bootstrap_dir "${PROJECT_SOURCE_DIR}/tests/bootstrap")
set(registration_file "${PROJECT_SOURCE_DIR}/cmake/SloppyExampleBootstrapTests.cmake")

if(NOT EXISTS "${registration_file}")
    message(FATAL_ERROR "Bootstrap CTest registration file is missing: ${registration_file}")
endif()

file(READ "${registration_file}" registration_text)
file(GLOB bootstrap_tests
     "${bootstrap_dir}/*.mjs"
     "${bootstrap_dir}/property/*.mjs")

set(helper_tests
    "realtime_backplane_conformance.mjs")

set(missing_tests "")
foreach(test_path IN LISTS bootstrap_tests)
    file(RELATIVE_PATH relative_test "${bootstrap_dir}" "${test_path}")
    get_filename_component(test_name "${relative_test}" NAME)
    if(test_name IN_LIST helper_tests)
        continue()
    endif()
    string(REPLACE "\\" "/" relative_test "${relative_test}")
    if(NOT registration_text MATCHES "tests/bootstrap/${relative_test}")
        list(APPEND missing_tests "${relative_test}")
    endif()
endforeach()

if(missing_tests)
    list(SORT missing_tests)
    string(REPLACE ";" "\n  - " missing_text "${missing_tests}")
    message(FATAL_ERROR "Bootstrap tests are missing CTest registration:\n  - ${missing_text}")
endif()
