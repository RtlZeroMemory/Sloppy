if(NOT DEFINED PROJECT_SOURCE_DIR)
    message(FATAL_ERROR "PROJECT_SOURCE_DIR is required")
endif()
if(NOT DEFINED CMAKE_BINARY_DIR)
    message(FATAL_ERROR "CMAKE_BINARY_DIR is required")
endif()
if(NOT DEFINED SLOPPY_CLI)
    message(FATAL_ERROR "SLOPPY_CLI is required")
endif()
if(NOT DEFINED SLOPPYC_EXECUTABLE)
    message(FATAL_ERROR "SLOPPYC_EXECUTABLE is required")
endif()
if(NOT DEFINED SLOPPY_EXPECT_RUN_SUCCESS)
    set(SLOPPY_EXPECT_RUN_SUCCESS OFF)
endif()

set(work_dir "${CMAKE_BINARY_DIR}/npm-runtime-behavior")
set(fixtures_root "${PROJECT_SOURCE_DIR}/tests/fixtures/npm-runtime")
file(REMOVE_RECURSE "${work_dir}")
file(MAKE_DIRECTORY "${work_dir}")

# Fixtures whose build + package + outside-checkout run must succeed and whose
# stdout must match the matrix `expectedStdout`. Pairs are (fixture, expected).
set(supported_fixtures
    cjs-module-exports-function "module-exports-fn: hello sloppy"
    cjs-exports-mutation         "foo=foo-value bar=bar-value"
    cjs-exports-rebind-negative  "rebind: keeps original keys=[\"sticky\"]"
    cjs-requires-json-runtime    "config: name=sloppy version=1.2.3"
    esm-imports-cjs-default-runtime "default(): from-cjs-default 7"
    esm-imports-cjs-named-runtime   "named: alpha=A beta=B"
    package-self-reference-runtime  "self-ref: identity=true tag=root"
    imports-alias-runtime           "alias: name=internal value=42"
    exports-import-require-dual-runtime-esm "dual: chose=import value=esm-branch"
    exports-import-require-dual-runtime-cjs "dual: chose=require value=cjs-branch"
    module-create-require-runtime           "createRequire: json.kind=runtime-fixture subpath=value resolveOk=true"
    dirname-filename-runtime                "dirname: filenameEndsWith=index.js dirnameEndsWith=dirname-pkg"
    buffer-runtime                          "buffer: utf8=hello byteLen=5 hex=68656c6c6f base64=aGVsbG8= concat=abcdef"
    crypto-runtime                          "crypto: sha256=2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824 randomLen=16 timingEq=true"
    stream-runtime                          "stream: readable=alpha,beta,gamma passThrough=hello-stream"
    zlib-runtime                            "zlib: roundTripText=hello-zlib gzipLengthOk=true")

# Stubbed runtime fixtures: build + package succeed, but runtime is expected to
# throw the documented diagnostic until the backing shim/compiler work lands.
# Pairs are (fixture, expected stderr substring). When the backing work lands
# (e.g. compile-time sealed asset baking for fs.readFileSync) the fixture
# graduates to `supported_fixtures` and asserts a real expectedStdout instead.
set(stubbed_fixtures
    fs-sync-package-asset-runtime "SLOPPY_E_NODE_SYNC_FS_UNSEALED")

# Fixtures that must fail `sloppy build` with a documented diagnostic.
# Triplets are (fixture, expected diagnostic code, expected substring).
set(negative_build_fixtures
    neg-native-addon  "SLOPPYC_E_NATIVE_ADDON_UNSUPPORTED"  "native-addon-pkg"
    neg-child-process "SLOPPYC_E_UNSUPPORTED_NODE_BUILTIN"  "node:child_process")

# Fixtures that must build and package, then fail at runtime with a documented
# error substring on stderr. Pairs are (fixture, expected stderr substring).
set(negative_runtime_fixtures
    neg-computed-require-unsealed "SLOPPY_E_MODULE_NOT_FOUND"
    neg-missing-package-asset     "SLOPPY_E_NODE_SYNC_FS_UNSEALED"
    neg-create-require-unknown    "SLOPPY_E_MODULE_NOT_FOUND"
    neg-require-resolve-unknown   "SLOPPY_E_MODULE_NOT_FOUND"
    neg-fs-sync-outside-policy    "SLOPPY_E_NODE_SYNC_FS_UNSEALED")

function(run_sloppy fixture command)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}"
                "${SLOPPY_CLI}" ${command} ${ARGN}
        WORKING_DIRECTORY "${work_dir}/${fixture}"
        TIMEOUT 120
        RESULT_VARIABLE result
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr)
    set(${fixture}_${command}_result "${result}" PARENT_SCOPE)
    set(${fixture}_${command}_stdout "${stdout}" PARENT_SCOPE)
    set(${fixture}_${command}_stderr "${stderr}" PARENT_SCOPE)
endfunction()

function(prepare_fixture fixture)
    set(source_dir "${fixtures_root}/${fixture}")
    set(target_dir "${work_dir}/${fixture}")
    if(NOT EXISTS "${source_dir}/sloppy.json")
        message(FATAL_ERROR "${fixture}: sloppy.json missing from runtime fixture")
    endif()
    if(NOT EXISTS "${source_dir}/src/main.ts")
        message(FATAL_ERROR "${fixture}: src/main.ts missing from runtime fixture")
    endif()
    file(COPY "${source_dir}/" DESTINATION "${target_dir}")
endfunction()

function(assert_build_artifacts fixture target_dir)
    foreach(expected IN ITEMS app.js app.js.map app.plan.json deps.graph.json)
        if(NOT EXISTS "${target_dir}/.sloppy/${expected}")
            message(FATAL_ERROR "${fixture}: build did not emit ${expected}")
        endif()
    endforeach()
endfunction()

function(assert_package_artifacts fixture target_dir)
    foreach(expected IN ITEMS artifacts/app.js artifacts/deps.graph.json manifest.json)
        if(NOT EXISTS "${target_dir}/.sloppy/package/${expected}")
            message(FATAL_ERROR "${fixture}: package did not emit ${expected}")
        endif()
    endforeach()

    file(GLOB_RECURSE leaked_node_modules
         "${target_dir}/.sloppy/package/**/node_modules")
    if(leaked_node_modules)
        message(
            FATAL_ERROR
                "${fixture}: package contains node_modules paths: ${leaked_node_modules}"
        )
    endif()
endfunction()

function(assert_no_source_path_leak fixture target_dir)
    set(plan_path "${target_dir}/.sloppy/package/artifacts/app.plan.json")
    if(EXISTS "${plan_path}")
        file(READ "${plan_path}" plan_text)
        file(TO_CMAKE_PATH "${fixtures_root}/${fixture}" source_root)
        # Normalize comparisons to forward slashes.
        string(REPLACE "\\" "/" plan_text_norm "${plan_text}")
        string(REPLACE "\\" "/" source_root_norm "${source_root}")
        string(FIND "${plan_text_norm}" "${source_root_norm}" leak_pos)
        if(NOT leak_pos EQUAL -1)
            message(
                FATAL_ERROR
                    "${fixture}: app.plan.json leaks absolute source path: ${source_root_norm}"
            )
        endif()
    endif()
    set(graph_path "${target_dir}/.sloppy/package/artifacts/deps.graph.json")
    if(EXISTS "${graph_path}")
        file(READ "${graph_path}" graph_text)
        file(TO_CMAKE_PATH "${fixtures_root}/${fixture}" source_root)
        string(REPLACE "\\" "/" graph_text_norm "${graph_text}")
        string(REPLACE "\\" "/" source_root_norm "${source_root}")
        string(FIND "${graph_text_norm}" "${source_root_norm}" leak_pos)
        if(NOT leak_pos EQUAL -1)
            message(
                FATAL_ERROR
                    "${fixture}: deps.graph.json leaks absolute source path: ${source_root_norm}"
            )
        endif()
    endif()
endfunction()

# Supported runtime fixtures: must build + package + run-outside-checkout.
set(idx 0)
list(LENGTH supported_fixtures supported_len)
while(idx LESS supported_len)
    list(GET supported_fixtures ${idx} fixture)
    math(EXPR expected_idx "${idx} + 1")
    list(GET supported_fixtures ${expected_idx} expected_stdout)
    math(EXPR idx "${idx} + 2")

    set(target_dir "${work_dir}/${fixture}")
    prepare_fixture("${fixture}")

    run_sloppy("${fixture}" build)
    if(NOT ${fixture}_build_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy build failed (${${fixture}_build_result})\nstdout:\n${${fixture}_build_stdout}\nstderr:\n${${fixture}_build_stderr}"
        )
    endif()
    assert_build_artifacts("${fixture}" "${target_dir}")

    run_sloppy("${fixture}" package)
    if(NOT ${fixture}_package_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy package failed (${${fixture}_package_result})\nstdout:\n${${fixture}_package_stdout}\nstderr:\n${${fixture}_package_stderr}"
        )
    endif()
    assert_package_artifacts("${fixture}" "${target_dir}")
    assert_no_source_path_leak("${fixture}" "${target_dir}")

    set(outside_dir "${work_dir}/${fixture}-outside")
    file(REMOVE_RECURSE "${outside_dir}")
    file(MAKE_DIRECTORY "${outside_dir}")
    file(COPY "${target_dir}/.sloppy/package/" DESTINATION "${outside_dir}/package")
    # Delete the source copy to prove the package is fully self-contained.
    file(REMOVE_RECURSE "${target_dir}/src")
    file(REMOVE_RECURSE "${target_dir}/node_modules")

    if(SLOPPY_EXPECT_RUN_SUCCESS)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}"
                    "${SLOPPY_CLI}" run "${outside_dir}/package"
            TIMEOUT 60
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_stdout
            ERROR_VARIABLE run_stderr)
        if(NOT run_result EQUAL 0)
            message(
                FATAL_ERROR
                    "${fixture}: outside-checkout run failed (${run_result})\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}"
            )
        endif()
        string(FIND "${run_stdout}" "${expected_stdout}" match_pos)
        if(match_pos EQUAL -1)
            message(
                FATAL_ERROR
                    "${fixture}: stdout did not contain expected line.\nexpected substring:\n  ${expected_stdout}\nactual stdout:\n${run_stdout}\nstderr:\n${run_stderr}"
            )
        endif()
    endif()
endwhile()

# Stubbed runtime fixtures: must build + package, then fail at runtime with the
# documented stubbed-shim diagnostic when V8 is available.
set(idx 0)
list(LENGTH stubbed_fixtures stubbed_len)
while(idx LESS stubbed_len)
    list(GET stubbed_fixtures ${idx} fixture)
    math(EXPR substr_idx "${idx} + 1")
    list(GET stubbed_fixtures ${substr_idx} expected_substr)
    math(EXPR idx "${idx} + 2")

    set(target_dir "${work_dir}/${fixture}")
    prepare_fixture("${fixture}")

    run_sloppy("${fixture}" build)
    if(NOT ${fixture}_build_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy build failed for stubbed fixture.\nstdout:\n${${fixture}_build_stdout}\nstderr:\n${${fixture}_build_stderr}"
        )
    endif()
    assert_build_artifacts("${fixture}" "${target_dir}")
    run_sloppy("${fixture}" package)
    if(NOT ${fixture}_package_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy package failed for stubbed fixture.\nstdout:\n${${fixture}_package_stdout}\nstderr:\n${${fixture}_package_stderr}"
        )
    endif()
    assert_package_artifacts("${fixture}" "${target_dir}")
    assert_no_source_path_leak("${fixture}" "${target_dir}")

    set(outside_dir "${work_dir}/${fixture}-outside")
    file(REMOVE_RECURSE "${outside_dir}")
    file(MAKE_DIRECTORY "${outside_dir}")
    file(COPY "${target_dir}/.sloppy/package/" DESTINATION "${outside_dir}/package")
    file(REMOVE_RECURSE "${target_dir}/src")
    file(REMOVE_RECURSE "${target_dir}/node_modules")

    if(SLOPPY_EXPECT_RUN_SUCCESS)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}"
                    "${SLOPPY_CLI}" run "${outside_dir}/package"
            TIMEOUT 60
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_stdout
            ERROR_VARIABLE run_stderr)
        if(run_result EQUAL 0)
            message(
                FATAL_ERROR
                    "${fixture}: outside-checkout run unexpectedly succeeded; expected stubbed diagnostic ${expected_substr}\nstdout:\n${run_stdout}"
            )
        endif()
        string(FIND "${run_stderr}" "${expected_substr}" substr_pos)
        if(substr_pos EQUAL -1)
            string(FIND "${run_stdout}" "${expected_substr}" substr_pos_stdout)
            if(substr_pos_stdout EQUAL -1)
                message(
                    FATAL_ERROR
                        "${fixture}: stubbed runtime did not include expected substring ${expected_substr}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}"
                )
            endif()
        endif()
    endif()
endwhile()

# Negative build fixtures: must fail at compile time with a documented diagnostic.
set(idx 0)
list(LENGTH negative_build_fixtures neg_build_len)
while(idx LESS neg_build_len)
    list(GET negative_build_fixtures ${idx} fixture)
    math(EXPR code_idx "${idx} + 1")
    math(EXPR substr_idx "${idx} + 2")
    list(GET negative_build_fixtures ${code_idx} expected_code)
    list(GET negative_build_fixtures ${substr_idx} expected_substr)
    math(EXPR idx "${idx} + 3")

    prepare_fixture("${fixture}")
    run_sloppy("${fixture}" build)
    if(${fixture}_build_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy build unexpectedly succeeded; expected diagnostic ${expected_code}"
        )
    endif()
    string(FIND "${${fixture}_build_stderr}" "${expected_code}" code_pos)
    if(code_pos EQUAL -1)
        message(
            FATAL_ERROR
                "${fixture}: build stderr did not mention ${expected_code}\nstderr:\n${${fixture}_build_stderr}"
        )
    endif()
    string(FIND "${${fixture}_build_stderr}" "${expected_substr}" substr_pos)
    if(substr_pos EQUAL -1)
        message(
            FATAL_ERROR
                "${fixture}: build stderr did not include substring ${expected_substr}\nstderr:\n${${fixture}_build_stderr}"
        )
    endif()
endwhile()

# Negative runtime fixtures: must build + package, then fail at runtime with the
# documented stderr substring when V8 is available.
set(idx 0)
list(LENGTH negative_runtime_fixtures neg_runtime_len)
while(idx LESS neg_runtime_len)
    list(GET negative_runtime_fixtures ${idx} fixture)
    math(EXPR substr_idx "${idx} + 1")
    list(GET negative_runtime_fixtures ${substr_idx} expected_substr)
    math(EXPR idx "${idx} + 2")

    set(target_dir "${work_dir}/${fixture}")
    prepare_fixture("${fixture}")

    run_sloppy("${fixture}" build)
    if(NOT ${fixture}_build_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy build failed but the fixture expects a runtime error.\nstdout:\n${${fixture}_build_stdout}\nstderr:\n${${fixture}_build_stderr}"
        )
    endif()
    run_sloppy("${fixture}" package)
    if(NOT ${fixture}_package_result EQUAL 0)
        message(
            FATAL_ERROR
                "${fixture}: sloppy package failed but the fixture expects a runtime error.\nstdout:\n${${fixture}_package_stdout}\nstderr:\n${${fixture}_package_stderr}"
        )
    endif()

    set(outside_dir "${work_dir}/${fixture}-outside")
    file(REMOVE_RECURSE "${outside_dir}")
    file(MAKE_DIRECTORY "${outside_dir}")
    file(COPY "${target_dir}/.sloppy/package/" DESTINATION "${outside_dir}/package")
    file(REMOVE_RECURSE "${target_dir}/src")
    file(REMOVE_RECURSE "${target_dir}/node_modules")

    if(SLOPPY_EXPECT_RUN_SUCCESS)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "SLOPPY_SLOPPYC=${SLOPPYC_EXECUTABLE}"
                    "${SLOPPY_CLI}" run "${outside_dir}/package"
            TIMEOUT 60
            RESULT_VARIABLE run_result
            OUTPUT_VARIABLE run_stdout
            ERROR_VARIABLE run_stderr)
        if(run_result EQUAL 0)
            message(
                FATAL_ERROR
                    "${fixture}: outside-checkout run unexpectedly succeeded; expected stderr substring ${expected_substr}\nstdout:\n${run_stdout}"
            )
        endif()
        string(FIND "${run_stderr}" "${expected_substr}" substr_pos)
        if(substr_pos EQUAL -1)
            # Some runtime errors surface on stdout when the program logs the
            # error before throwing; allow either stream.
            string(FIND "${run_stdout}" "${expected_substr}" substr_pos_stdout)
            if(substr_pos_stdout EQUAL -1)
                message(
                    FATAL_ERROR
                        "${fixture}: runtime did not include expected substring ${expected_substr}\nstdout:\n${run_stdout}\nstderr:\n${run_stderr}"
                )
            endif()
        endif()
    endif()
endwhile()

message(STATUS "npm-runtime behavior fixtures: build + package + outside-checkout copy OK; runtime assertions gated on SLOPPY_EXPECT_RUN_SUCCESS=${SLOPPY_EXPECT_RUN_SUCCESS}")
