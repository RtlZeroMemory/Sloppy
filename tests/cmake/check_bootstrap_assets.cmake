file(READ "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/bootstrap.manifest.json" bootstrap_manifest_json)
string(JSON bootstrap_module_count LENGTH "${bootstrap_manifest_json}" modules)

set(required_bootstrap_assets
    README.md
    bootstrap.manifest.json)

math(EXPR last_bootstrap_module_index "${bootstrap_module_count} - 1")
foreach(module_index RANGE 0 ${last_bootstrap_module_index})
    string(JSON module_path GET "${bootstrap_manifest_json}" modules ${module_index})
    if(NOT module_path MATCHES "^sloppy/.+\\.js$")
        message(FATAL_ERROR "Invalid bootstrap manifest module path: ${module_path}")
    endif()
    string(REGEX REPLACE "^sloppy/" "" asset "${module_path}")
    list(APPEND required_bootstrap_assets "${asset}")
endforeach()

list(REMOVE_DUPLICATES required_bootstrap_assets)

foreach(asset IN LISTS required_bootstrap_assets)
    set(source_asset "${SLOPPY_BOOTSTRAP_SOURCE_DIR}/${asset}")
    if(NOT EXISTS "${source_asset}")
        message(FATAL_ERROR "Missing bootstrap stdlib source asset: ${source_asset}")
    endif()

    set(build_asset "${SLOPPY_BOOTSTRAP_BUILD_DIR}/${asset}")
    if(NOT EXISTS "${build_asset}")
        message(FATAL_ERROR "Missing copied bootstrap stdlib build asset: ${build_asset}")
    endif()
endforeach()
