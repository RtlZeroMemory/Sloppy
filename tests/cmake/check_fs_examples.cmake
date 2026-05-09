if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(PROJECT_SOURCE_DIR STREQUAL "" OR PROJECT_SOURCE_DIR STREQUAL "/")
    get_filename_component(PROJECT_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
else()
    get_filename_component(PROJECT_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
endif()

function(require_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

function(reject_substring haystack needle description)
    string(FIND "${haystack}" "${needle}" found_index)
    if(NOT found_index EQUAL -1)
        message(FATAL_ERROR "${description}: ${needle}")
    endif()
endfunction()

set(fs_examples
    fs-basic
    fs-roots-policy
    fs-streams
    fs-watch)

foreach(example_name IN LISTS fs_examples)
    set(app_path "${PROJECT_SOURCE_DIR}/examples/${example_name}/app.js")
    set(readme_path "${PROJECT_SOURCE_DIR}/examples/${example_name}/README.md")
    if(NOT EXISTS "${app_path}")
        message(FATAL_ERROR "Missing filesystem example app: ${app_path}")
    endif()
    if(NOT EXISTS "${readme_path}")
        message(FATAL_ERROR "Missing filesystem example README: ${readme_path}")
    endif()
    file(READ "${app_path}" app_source)
    file(READ "${readme_path}" readme_source)
    string(TOLOWER "${readme_source}" readme_source_lower)
    require_substring("${app_source}" "from \"sloppy/fs\"" "${example_name} must use sloppy/fs")
    require_substring("${readme_source_lower}" "example" "${example_name} README needs useful example text")
    foreach(forbidden_pattern IN ITEMS
            "node:fs"
            "require(\"fs\")"
            "npm install"
            "yarn add"
            "pnpm add"
            "console.log")
        reject_substring("${app_source}" "${forbidden_pattern}" "Filesystem examples must keep runtime boundaries clear")
        reject_substring("${readme_source}" "${forbidden_pattern}" "Filesystem README files must keep runtime boundaries clear")
    endforeach()
endforeach()

file(READ "${PROJECT_SOURCE_DIR}/examples/fs-basic/app.js" fs_basic)
require_substring("${fs_basic}" "Directory.create" "fs-basic is missing Directory.create")
require_substring("${fs_basic}" "File.writeJson" "fs-basic is missing File.writeJson")
require_substring("${fs_basic}" "File.readJson" "fs-basic is missing File.readJson")
require_substring("${fs_basic}" "Deadline.after" "fs-basic is missing deadline propagation")

file(READ "${PROJECT_SOURCE_DIR}/examples/fs-roots-policy/app.js" fs_roots)
require_substring("${fs_roots}" "data:/exports" "fs-roots-policy is missing a logical root path")
require_substring("${fs_roots}" "{ atomic: true }" "fs-roots-policy is missing atomic write shape")

file(READ "${PROJECT_SOURCE_DIR}/examples/fs-streams/app.js" fs_streams)
require_substring("${fs_streams}" "File.open" "fs-streams is missing File.open")
require_substring("${fs_streams}" "readLines()" "fs-streams is missing line iteration")
require_substring("${fs_streams}" "file.close()" "fs-streams is missing close")

file(READ "${PROJECT_SOURCE_DIR}/examples/fs-watch/app.js" fs_watch)
require_substring("${fs_watch}" "Directory.watch" "fs-watch is missing Directory.watch")
require_substring("${fs_watch}" "queueCapacity" "fs-watch is missing queue capacity")
require_substring("${fs_watch}" "watcher.close()" "fs-watch is missing close")
