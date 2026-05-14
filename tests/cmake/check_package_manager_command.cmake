if(NOT DEFINED CMAKE_BINARY_DIR)
  message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
  message(FATAL_ERROR "SLOPPY_CLI is required")
endif()

set(case_dir "${CMAKE_BINARY_DIR}/package-manager-command")
set(source_dir "${case_dir}/source")
set(app_dir "${case_dir}/app")
set(package_source_dir "${case_dir}/packages")
set(cache_dir "${case_dir}/cache")

file(REMOVE_RECURSE "${case_dir}")
file(MAKE_DIRECTORY "${source_dir}/lib" "${app_dir}" "${package_source_dir}" "${cache_dir}")

file(WRITE "${source_dir}/lib/index.ts" "export const value = 1;\n")
file(WRITE "${source_dir}/sloppy.json" [=[
{
  "id": "Sloppy.Example",
  "version": "0.1.0",
  "targets": {
    "sloppy1.0": {
      "compile": ["lib/index.ts"],
      "runtime": []
    }
  }
}
]=])

execute_process(
  COMMAND "${SLOPPY_CLI}" pack
  WORKING_DIRECTORY "${source_dir}"
  RESULT_VARIABLE pack_result
  OUTPUT_VARIABLE pack_output
  ERROR_VARIABLE pack_error)
if(NOT pack_result EQUAL 0)
  message(FATAL_ERROR "sloppy pack failed: ${pack_output}${pack_error}")
endif()

set(package_path "${source_dir}/artifacts/packages/sloppy.example.0.1.0.slpkg")
if(NOT EXISTS "${package_path}")
  message(FATAL_ERROR "sloppy pack did not create ${package_path}")
endif()
file(COPY "${package_path}" DESTINATION "${package_source_dir}")
file(COPY "${package_path}" DESTINATION "${app_dir}/packages")

file(WRITE "${app_dir}/sloppy.json" [=[
{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": [
    "packages"
  ],
  "dependencies": {
    "Sloppy.Example": "[0.1.0]"
  }
}
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" restore
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE restore_result
  OUTPUT_VARIABLE restore_output
  ERROR_VARIABLE restore_error)
if(NOT restore_result EQUAL 0)
  message(FATAL_ERROR "sloppy restore failed: ${restore_output}${restore_error}")
endif()
if(NOT EXISTS "${app_dir}/sloppy.lock.json")
  message(FATAL_ERROR "sloppy restore did not write sloppy.lock.json")
endif()
if(NOT EXISTS "${app_dir}/.sloppy/obj/project.assets.json")
  message(FATAL_ERROR "sloppy restore did not write project.assets.json")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" restore --locked
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE locked_result
  OUTPUT_VARIABLE locked_output
  ERROR_VARIABLE locked_error)
if(NOT locked_result EQUAL 0)
  message(FATAL_ERROR "sloppy restore --locked failed: ${locked_output}${locked_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" list packages
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE list_packages_result
  OUTPUT_VARIABLE list_packages_output
  ERROR_VARIABLE list_packages_error)
if(NOT list_packages_result EQUAL 0 OR NOT "${list_packages_output}" MATCHES "Sloppy.Example 0.1.0")
  message(FATAL_ERROR "sloppy list packages failed: ${list_packages_output}${list_packages_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" why Sloppy.Example
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE why_result
  OUTPUT_VARIABLE why_output
  ERROR_VARIABLE why_error)
if(NOT why_result EQUAL 0 OR NOT "${why_output}" MATCHES "root -> Sloppy.Example")
  message(FATAL_ERROR "sloppy why failed: ${why_output}${why_error}")
endif()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" cache list
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE cache_list_result
  OUTPUT_VARIABLE cache_list_output
  ERROR_VARIABLE cache_list_error)
if(NOT cache_list_result EQUAL 0 OR NOT "${cache_list_output}" MATCHES "sloppy.example 0.1.0")
  message(FATAL_ERROR "sloppy cache list failed: ${cache_list_output}${cache_list_error}")
endif()

execute_process(
  COMMAND "${SLOPPY_CLI}" source list
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE source_list_result
  OUTPUT_VARIABLE source_list_output
  ERROR_VARIABLE source_list_error)
if(NOT source_list_result EQUAL 0 OR NOT "${source_list_output}" MATCHES "packages")
  message(FATAL_ERROR "sloppy source list failed: ${source_list_output}${source_list_error}")
endif()

execute_process(
  COMMAND "${SLOPPY_CLI}" feed index packages
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE feed_index_result
  OUTPUT_VARIABLE feed_index_output
  ERROR_VARIABLE feed_index_error)
if(NOT feed_index_result EQUAL 0)
  message(FATAL_ERROR "sloppy feed index failed: ${feed_index_output}${feed_index_error}")
endif()
if(NOT EXISTS "${app_dir}/packages/v3/index.json")
  message(FATAL_ERROR "sloppy feed index did not write v3/index.json")
endif()

file(WRITE "${app_dir}/sloppy.json" [=[
{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": [
    "packages"
  ],
  "dependencies": {
    "Sloppy.Example": "[0.2.0]"
  }
}
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" restore --locked
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE locked_drift_result
  OUTPUT_VARIABLE locked_drift_output
  ERROR_VARIABLE locked_drift_error)
if(locked_drift_result EQUAL 0)
  message(FATAL_ERROR "sloppy restore --locked unexpectedly accepted changed dependencies")
endif()
if(NOT "${locked_drift_error}" MATCHES "SLOPPY_E_PACKAGE_")
  message(FATAL_ERROR "locked restore failure did not include stable package diagnostic: ${locked_drift_error}")
endif()

file(WRITE "${app_dir}/sloppy.json" [=[
{
  "name": "app",
  "entry": "src/main.ts",
  "target": "sloppy1.0",
  "runtimeIdentifier": "win-x64",
  "packageSources": [
    "packages"
  ],
  "dependencies": {
    "Sloppy.Missing": "[0.1.0]"
  }
}
]=])

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_PACKAGE_CACHE=${cache_dir}" "${SLOPPY_CLI}" restore
  WORKING_DIRECTORY "${app_dir}"
  RESULT_VARIABLE missing_result
  OUTPUT_VARIABLE missing_output
  ERROR_VARIABLE missing_error)
if(missing_result EQUAL 0)
  message(FATAL_ERROR "sloppy restore unexpectedly accepted missing package")
endif()
if(NOT "${missing_error}" MATCHES "SLOPPY_E_PACKAGE_NOT_FOUND")
  message(FATAL_ERROR "missing package failure did not include SLOPPY_E_PACKAGE_NOT_FOUND: ${missing_error}")
endif()
