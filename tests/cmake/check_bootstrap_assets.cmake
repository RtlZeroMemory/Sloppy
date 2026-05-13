set(required_bootstrap_assets
    README.md
    app.js
    auth.js
    bootstrap.manifest.json
    codec.js
    config.js
    crypto.js
    data.js
    fs.js
    jobs.js
    net.js
    os.js
    problem-details.js
    request-id.js
    request-logging.js
    time.js
    workers.js
    index.js
    node/assert.js
    node/buffer.js
    node/console.js
    node/constants.js
    node/crypto.js
    node/diagnostics_channel.js
    node/events.js
    node/fs.js
    node/fs/promises.js
    node/assert/strict.js
    node/http.js
    node/https.js
    node/module.js
    node/os.js
    node/path.js
    node/perf_hooks.js
    node/process.js
    node/querystring.js
    node/stream.js
    node/stream/promises.js
    node/string_decoder.js
    node/timers.js
    node/tty.js
    node/url.js
    node/util.js
    node/zlib.js
    providers/sqlite.js
    results.js
    schema.js
    internal/capabilities.js
    internal/config.js
    internal/intrinsics.js
    internal/logging.js
    internal/modules.js
    internal/routes.js
    internal/services.js
    internal/shared.js
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
