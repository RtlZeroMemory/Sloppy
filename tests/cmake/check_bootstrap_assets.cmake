set(required_bootstrap_assets
    README.md
    app.js
    bootstrap.manifest.json
    data.js
    fs.js
    time.js
    index.js
    providers/sqlite.js
    results.js
    schema.js
    internal/intrinsics.js
    internal/runtime-classic.js)

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
