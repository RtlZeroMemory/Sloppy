# Native unit, fuzz seed, data-provider, assertion, benchmark, and V8 smoke tests. Included by cmake/SloppyTests.cmake.

    add_test(
        NAME docs.test_platform_contract
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_test_platform_contract.cmake")
    set_tests_properties(docs.test_platform_contract PROPERTIES LABELS "docs;test-platform")
    add_test(
        NAME docs.provider_evidence_policy
        COMMAND "${CMAKE_COMMAND}" "-DPROJECT_SOURCE_DIR=${PROJECT_SOURCE_DIR}" -P
                "${PROJECT_SOURCE_DIR}/tests/cmake/check_provider_evidence_policy.cmake")
    set_tests_properties(
        docs.provider_evidence_policy PROPERTIES LABELS "docs;provider;live-provider")

    sloppy_add_c_unit_test(core_status_basic core.status.basic tests/unit/core/test_status.c)
    sloppy_add_c_unit_test(
        core_source_loc_current core.source_loc.current tests/unit/core/test_source_loc.c)
    sloppy_add_c_unit_test(core_str_views core.str.views tests/unit/core/test_string.c)
    sloppy_add_c_unit_test(core_bytes_views core.bytes.views tests/unit/core/test_bytes.c)
    sloppy_add_c_unit_test(
        core_cancellation_token core.cancellation.token tests/unit/core/test_cancellation.c)
    sloppy_add_c_unit_test(
        core_checked_math_overflow core.checked_math.overflow tests/unit/core/test_checked_math.c)
    sloppy_add_c_unit_test(core_alloc_boundary core.alloc.boundary tests/unit/core/test_alloc.c)
    sloppy_add_c_unit_test(core_arena_foundation core.arena.foundation tests/unit/core/test_arena.c)
    sloppy_add_c_unit_test(core_alloc_heap_buffer core.alloc.heap_buffer tests/unit/core/test_alloc.c)
    sloppy_add_c_unit_test(core_container_primitives core.container.primitives
                           tests/unit/core/test_container.c)
    sloppy_add_c_unit_test(core_builder_foundation core.builder.foundation
                           tests/unit/core/test_builder.c)
    sloppy_add_c_unit_test(core_intern_table core.intern.table tests/unit/core/test_intern.c)
    sloppy_add_c_unit_test(core_scope_lifetime core.scope.lifetime tests/unit/core/test_scope.c)
    sloppy_add_c_unit_test(
        core_resource_lifecycle core.resource.lifecycle tests/unit/core/test_resource.c)
    sloppy_add_c_unit_test(
        core_capability_registry core.capability.registry tests/unit/core/test_capability.c)
    sloppy_add_c_unit_test(
        core_execution_domain core.execution_domain tests/unit/core/test_execution_domain.c)
    sloppy_add_c_unit_test(core_runtime_features core.runtime_features
                           tests/unit/core/test_features.c)
    set_tests_properties(core.runtime_features PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    add_library(sloppy_ffi_test_library SHARED tests/fixtures/ffi/sloppy_ffi_test.c)
    target_compile_features(sloppy_ffi_test_library PRIVATE c_std_17)
    sloppy_apply_warnings(sloppy_ffi_test_library)
    add_executable(runtime_ffi_registry tests/unit/runtime/test_ffi_registry.c)
    target_link_libraries(runtime_ffi_registry PRIVATE sloppy_core)
    target_include_directories(runtime_ffi_registry PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_compile_features(runtime_ffi_registry PRIVATE c_std_17)
    sloppy_apply_warnings(runtime_ffi_registry)
    sloppy_apply_sanitizers(runtime_ffi_registry)
    add_test(NAME runtime.ffi.registry
             COMMAND runtime_ffi_registry $<TARGET_FILE:sloppy_ffi_test_library>)
    set_tests_properties(runtime.ffi.registry PROPERTIES LABELS "runtime;ffi;native")
    sloppy_add_c_unit_test(core_crypto_foundation core.crypto.foundation
                           tests/unit/core/test_crypto.c)
    if(CMAKE_CXX_COMPILER)
        sloppy_add_cxx_unit_test(
            core_net_tcp_client core.net.tcp_client tests/unit/core/test_net_tcp_client.cc)
        target_link_libraries(core_net_tcp_client PRIVATE ${SLOPPY_LIBUV_TARGET})
        add_test(NAME conformance.net.tcp_client_loopback COMMAND $<TARGET_FILE:core_net_tcp_client>)
        set_tests_properties(
            conformance.net.tcp_client_loopback PROPERTIES LABELS "conformance;net;tcp;smoke")
        if(NOT WIN32)
            sloppy_add_cxx_unit_test(
                core_net_local_posix core.net.local_posix
                tests/unit/core/test_net_local_posix.cc)
            add_test(
                NAME conformance.net.local_ipc_posix
                COMMAND $<TARGET_FILE:core_net_local_posix>)
            set_tests_properties(
                conformance.net.local_ipc_posix
                PROPERTIES LABELS "conformance;net;local-ipc;posix")
        else()
            sloppy_add_cxx_unit_test(
                core_net_local_win32 core.net.local_win32
                tests/unit/core/test_net_local_win32.cc)
            add_test(
                NAME conformance.net.local_ipc_win32
                COMMAND $<TARGET_FILE:core_net_local_win32>)
            set_tests_properties(
                conformance.net.local_ipc_win32
                PROPERTIES LABELS "conformance;net;local-ipc;win32")
        endif()
    endif()
    sloppy_add_c_unit_test(core_filesystem core.filesystem tests/unit/core/test_fs.c)
    sloppy_add_c_unit_test(core_os_system_environment core.os.system_environment
                           tests/unit/core/test_os.c)
    sloppy_add_c_unit_test(core_layout_contract core.layout.contract tests/unit/core/test_layout.c)
    sloppy_add_c_unit_test(core_logging_structured core.logging.structured
                           tests/unit/core/test_logging.c)
    sloppy_add_c_unit_test(core_ops_metrics core.ops_metrics tests/unit/core/test_ops_metrics.c)
    sloppy_add_c_unit_test(core_platform_thread core.platform.thread
                           tests/unit/core/test_platform_thread.c)
    add_test(NAME stress.logging.structured COMMAND $<TARGET_FILE:core_logging_structured> --stress)
    set_tests_properties(stress.logging.structured PROPERTIES LABELS "stress;logging")
    sloppy_add_c_unit_test(core_app_host_hardening core.app_host.hardening
                           tests/unit/core/test_app_host.c)
    sloppy_add_c_unit_test(
        core_request_validation core.request_validation tests/unit/core/test_request_validation.c)
    sloppy_add_c_unit_test(
        core_loop_completion_queue core.loop.completion_queue tests/unit/core/test_loop.c)
    sloppy_add_c_unit_test(
        core_async_settlement core.async.settlement tests/unit/core/test_async.c)
    sloppy_add_c_unit_test(
        core_async_backend core.async.backend tests/unit/core/test_async_backend.c)
    add_test(NAME conformance.async.native_settlement COMMAND $<TARGET_FILE:core_async_settlement>)
    set_tests_properties(
        conformance.async.native_settlement PROPERTIES LABELS "conformance;async")
    add_test(NAME conformance.async.backend_completion COMMAND $<TARGET_FILE:core_async_backend>)
    set_tests_properties(
        conformance.async.backend_completion PROPERTIES LABELS "conformance;async")
    sloppy_add_c_unit_test(
        core_provider_executor core.provider_executor tests/unit/core/test_provider_executor.c)
    add_test(
        NAME conformance.capability.provider_executor
        COMMAND $<TARGET_FILE:core_provider_executor>)
    set_tests_properties(
        conformance.capability.provider_executor
        PROPERTIES LABELS "conformance;capability;smoke")
    add_test(NAME stress.provider_executor COMMAND $<TARGET_FILE:core_provider_executor>)
    set_tests_properties(
        stress.provider_executor PROPERTIES LABELS "stress;provider;executor")
    if(CMAKE_CXX_COMPILER)
        sloppy_add_cxx_unit_test(
            core_async_backend_libuv core.async.backend_libuv
            tests/unit/core/test_async_backend_libuv.cc)
        add_test(
            NAME conformance.async.backend_libuv
            COMMAND $<TARGET_FILE:core_async_backend_libuv>)
        set_tests_properties(
            conformance.async.backend_libuv PROPERTIES LABELS "conformance;async;smoke")
    endif()
    sloppy_add_c_unit_test(
        core_worker_pool_inline core.worker_pool.inline tests/unit/core/test_worker_pool.c)
    sloppy_add_c_unit_test(
        core_stream_foundation core.stream.foundation tests/unit/core/test_stream.c)
    sloppy_add_c_unit_test(core_http_parser core.http.parser tests/unit/core/test_http.c)
    sloppy_add_c_unit_test(core_http_profile core.http.profile tests/unit/core/test_http_profile.c)
    sloppy_add_c_unit_test(
        core_http2_dispatch core.http2.dispatch tests/unit/core/test_http2_dispatch.c)
    sloppy_add_c_unit_test(core_http2_frame core.http2.frame tests/unit/core/test_http2_frame.c)
    sloppy_add_c_unit_test(core_http2_hpack core.http2.hpack tests/unit/core/test_http2_hpack.c)
    sloppy_add_c_unit_test(
        core_http2_mapping core.http2.mapping tests/unit/core/test_http2_mapping.c)
    sloppy_add_c_unit_test(
        core_http2_session core.http2.session tests/unit/core/test_http2_session.c)
    sloppy_add_c_unit_test(
        core_http_backend core.http.backend tests/unit/core/test_http_backend.c)
    if(CMAKE_CXX_COMPILER)
        add_executable(core_http_transport tests/unit/core/test_http_transport.cc)
        target_link_libraries(core_http_transport PRIVATE sloppy_core)
        target_include_directories(core_http_transport PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(core_http_transport PRIVATE cxx_std_17)
        sloppy_apply_warnings(core_http_transport)
        sloppy_apply_sanitizers(core_http_transport)
        target_link_libraries(core_http_transport PRIVATE ${SLOPPY_LIBUV_TARGET})
        function(sloppy_add_http_transport_case test_name case_name)
            add_test(
                NAME ${test_name}
                COMMAND $<TARGET_FILE:core_http_transport> --case ${case_name})
            set_tests_properties(${test_name} PROPERTIES LABELS "conformance;transport;http")
        endfunction()
        sloppy_add_http_transport_case(conformance.transport.localhost_basic localhost_basic)
        sloppy_add_http_transport_case(conformance.transport.keep_alive keep_alive)
        sloppy_add_http_transport_case(
            conformance.transport.keep_alive_idle_timeout keep_alive_idle_timeout)
        sloppy_add_http_transport_case(
            conformance.transport.keep_alive_max_requests keep_alive_max_requests)
        sloppy_add_http_transport_case(conformance.transport.lifecycle_reset lifecycle_reset)
        sloppy_add_http_transport_case(conformance.transport.chunked_request chunked_request)
        sloppy_add_http_transport_case(
            conformance.transport.streaming_response streaming_response)
        sloppy_add_http_transport_case(conformance.transport.backpressure backpressure)
        sloppy_add_http_transport_case(conformance.transport.https_loopback https_loopback)
        sloppy_add_http_transport_case(conformance.transport.http2_h2c http2_h2c)
        sloppy_add_http_transport_case(conformance.transport.http2_h2c_upgrade http2_h2c_upgrade)
        sloppy_add_http_transport_case(conformance.transport.websocket_upgrade websocket_upgrade)
        sloppy_add_http_transport_case(conformance.transport.http2_tls_alpn http2_tls_alpn)
        sloppy_add_http_transport_case(conformance.transport.https_tls_negative https_tls_negative)
        sloppy_add_http_transport_case(conformance.transport.shutdown_cancel shutdown_cancel)
        add_test(
            NAME smoke.transport.keep_alive_streaming_bounded
            COMMAND $<TARGET_FILE:core_http_transport> --bounded-smoke-only)
        set_tests_properties(
            smoke.transport.keep_alive_streaming_bounded PROPERTIES LABELS "smoke;transport;http")
    endif()
    sloppy_add_c_unit_test(
        core_http_context core.http.context tests/unit/core/test_http_context.c)
    sloppy_add_c_unit_test(
        core_http_dispatch core.http.dispatch tests/unit/core/test_http_dispatch.c)
    add_test(NAME conformance.http.default_dispatch COMMAND $<TARGET_FILE:core_http_dispatch>)
    set_tests_properties(
        conformance.http.default_dispatch PROPERTIES LABELS "conformance;http")
    sloppy_add_c_unit_test(
        core_http_response core.http.response tests/unit/core/test_http_response.c)
    sloppy_add_c_unit_test(core_websocket core.websocket tests/unit/core/test_websocket.c)
    sloppy_add_c_unit_test(core_json_profile core.json_profile tests/unit/core/test_json_profile.c)
    sloppy_add_c_unit_test(core_json_writer core.json_writer tests/unit/core/test_json_writer.c)
    sloppy_add_c_unit_test(core_route_pattern core.route.pattern tests/unit/core/test_route.c)
    sloppy_add_c_unit_test(
        core_route_artifact core.route.artifact tests/unit/core/test_route_artifact.c)
    sloppy_add_c_unit_test(core_plan_contract core.plan.contract tests/unit/core/test_plan.c)
    set_tests_properties(core.plan.contract PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(
        core_plan_parse_json core.plan.parse_json tests/unit/core/test_plan_parse.c)
    set_tests_properties(
        core.plan.parse_json PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(
        core_runtime_contract core.runtime_contract tests/unit/core/test_runtime_contract.c)
    sloppy_add_c_unit_test(core_engine_abi core.engine.abi tests/unit/core/test_engine.c)
    if(SLOPPY_ENABLE_V8)
        sloppy_add_c_unit_test(
            engine_v8_smoke engine.v8.smoke tests/unit/engine/test_v8_smoke.c)
        add_test(NAME conformance.v8.runtime_bridge COMMAND $<TARGET_FILE:engine_v8_smoke>)
        set_tests_properties(
            conformance.v8.runtime_bridge PROPERTIES LABELS "conformance;v8;http;async;net;local-ipc")
        add_executable(engine_v8_ffi tests/unit/engine/test_v8_ffi.c)
        target_link_libraries(engine_v8_ffi PRIVATE sloppy_core)
        target_include_directories(engine_v8_ffi PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(engine_v8_ffi PRIVATE c_std_17)
        sloppy_apply_warnings(engine_v8_ffi)
        sloppy_apply_sanitizers(engine_v8_ffi)
        add_test(
            NAME conformance.v8.ffi_native
            COMMAND $<TARGET_FILE:engine_v8_ffi> $<TARGET_FILE:sloppy_ffi_test_library>)
        set_tests_properties(
            conformance.v8.ffi_native PROPERTIES LABELS "conformance;v8;ffi;native")
        sloppy_add_cxx_unit_test(
            engine_v8_owner_thread engine.v8.owner_thread
            tests/unit/engine/test_v8_owner_thread.cc)
        target_link_libraries(engine_v8_owner_thread PRIVATE Sloppy::V8)
        target_compile_features(engine_v8_owner_thread PRIVATE cxx_std_20)
        add_test(NAME conformance.v8.owner_thread COMMAND $<TARGET_FILE:engine_v8_owner_thread>)
        set_tests_properties(
            conformance.v8.owner_thread PROPERTIES LABELS "conformance;v8;async")
        sloppy_add_cxx_unit_test(
            engine_v8_async_scheduler engine.v8.async_scheduler
            tests/unit/engine/test_v8_async_scheduler.cc)
        target_link_libraries(engine_v8_async_scheduler PRIVATE Sloppy::V8)
        target_compile_features(engine_v8_async_scheduler PRIVATE cxx_std_20)
        add_test(
            NAME conformance.v8.native_async_scheduler
            COMMAND $<TARGET_FILE:engine_v8_async_scheduler>)
        set_tests_properties(
            conformance.v8.native_async_scheduler PROPERTIES LABELS "conformance;v8;async")
        add_executable(engine_v8_fast_api_probe tests/unit/engine/test_v8_fast_api_probe.cc)
        target_link_libraries(engine_v8_fast_api_probe PRIVATE Sloppy::V8)
        target_compile_features(engine_v8_fast_api_probe PRIVATE cxx_std_20)
        sloppy_apply_warnings(engine_v8_fast_api_probe)
        sloppy_apply_sanitizers(engine_v8_fast_api_probe)
        sloppy_add_c_unit_test(
            execution_handwritten_artifact execution.handwritten_artifact
            tests/integration/execution/test_handwritten_artifact_execution.c)
        set_tests_properties(
            execution.handwritten_artifact PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        sloppy_add_c_unit_test(
            http_dispatch_execution http.dispatch.execution
            tests/integration/http_dispatch/test_http_dispatch_execution.c)
        set_tests_properties(
            http.dispatch.execution PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME conformance.v8.http_dispatch_execution
            COMMAND $<TARGET_FILE:http_dispatch_execution>)
        set_tests_properties(
            conformance.v8.http_dispatch_execution
            PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}" LABELS "conformance;v8;http")
    endif()
    sloppy_add_c_unit_test(
        core_diagnostics_foundation core.diagnostics.foundation tests/unit/core/test_diagnostics.c)
    set_tests_properties(
        core.diagnostics.foundation PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    sloppy_add_c_unit_test(core_runtime_diagnostics core.runtime_diagnostics
                           tests/unit/core/test_runtime_diagnostics.c)
    set_tests_properties(
        core.runtime_diagnostics PROPERTIES WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}")
    add_test(
        NAME conformance.foundation.resource_lifecycle
        COMMAND $<TARGET_FILE:core_resource_lifecycle>)
    set_tests_properties(
        conformance.foundation.resource_lifecycle
        PROPERTIES LABELS "conformance;resource-lifecycle;shutdown")
    add_test(
        NAME conformance.foundation.app_host_lifecycle
        COMMAND $<TARGET_FILE:core_app_host_hardening>)
    set_tests_properties(
        conformance.foundation.app_host_lifecycle
        PROPERTIES LABELS "conformance;resource-lifecycle;shutdown")
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_plan_parse fuzz.plan_parse.seed_replay tests/fuzz/fuzz_plan_parse.c plan)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_route_pattern fuzz.route_pattern.seed_replay tests/fuzz/fuzz_route_pattern.c
        route-pattern)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_http2_frame fuzz.http2_frame.seed_replay tests/fuzz/fuzz_http2_frame.c
        http2-frame)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_http2_hpack fuzz.http2_hpack.seed_replay tests/fuzz/fuzz_http2_hpack.c
        http2-hpack)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_http2_session fuzz.http2_session.seed_replay tests/fuzz/fuzz_http2_session.c
        http2-session)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_http_request fuzz.http_request.seed_replay tests/fuzz/fuzz_http_request.c
        http-request)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_http_route_dispatch fuzz.http_route_dispatch.seed_replay
        tests/fuzz/fuzz_http_route_dispatch.c http-route-dispatch)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_json_request fuzz.json_request.seed_replay tests/fuzz/fuzz_json_request.c
        json-request)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_json_response fuzz.json_response.seed_replay tests/fuzz/fuzz_json_response.c
        json-response)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_http_query fuzz.http_query.seed_replay tests/fuzz/fuzz_http_query.c http-query)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_diagnostics_render fuzz.diagnostics_render.seed_replay
        tests/fuzz/fuzz_diagnostics_render.c diagnostics-render)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_memory_primitives fuzz.memory_primitives.seed_replay
        tests/fuzz/fuzz_memory_primitives.c memory-primitives)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_stream fuzz.stream.seed_replay tests/fuzz/fuzz_stream.c stream)
    sloppy_add_fuzz_seed_replay(
        fuzz_seed_websocket_frame fuzz.websocket_frame.seed_replay
        tests/fuzz/fuzz_websocket_frame.c websocket-frame)
    sloppy_add_libfuzzer_target(fuzz_plan_parse_libfuzzer tests/fuzz/fuzz_plan_parse.c)
    sloppy_add_libfuzzer_target(fuzz_route_pattern_libfuzzer tests/fuzz/fuzz_route_pattern.c)
    sloppy_add_libfuzzer_target(fuzz_http2_frame_libfuzzer tests/fuzz/fuzz_http2_frame.c)
    sloppy_add_libfuzzer_target(fuzz_http2_hpack_libfuzzer tests/fuzz/fuzz_http2_hpack.c)
    sloppy_add_libfuzzer_target(fuzz_http2_session_libfuzzer tests/fuzz/fuzz_http2_session.c)
    sloppy_add_libfuzzer_target(fuzz_http_request_libfuzzer tests/fuzz/fuzz_http_request.c)
    sloppy_add_libfuzzer_target(
        fuzz_http_route_dispatch_libfuzzer tests/fuzz/fuzz_http_route_dispatch.c)
    sloppy_add_libfuzzer_target(fuzz_json_request_libfuzzer tests/fuzz/fuzz_json_request.c)
    sloppy_add_libfuzzer_target(fuzz_json_response_libfuzzer tests/fuzz/fuzz_json_response.c)
    sloppy_add_libfuzzer_target(fuzz_http_query_libfuzzer tests/fuzz/fuzz_http_query.c)
    sloppy_add_libfuzzer_target(
        fuzz_diagnostics_render_libfuzzer tests/fuzz/fuzz_diagnostics_render.c)
    sloppy_add_libfuzzer_target(
        fuzz_memory_primitives_libfuzzer tests/fuzz/fuzz_memory_primitives.c)
    sloppy_add_libfuzzer_target(fuzz_stream_libfuzzer tests/fuzz/fuzz_stream.c)
    sloppy_add_libfuzzer_target(fuzz_websocket_frame_libfuzzer tests/fuzz/fuzz_websocket_frame.c)
    sloppy_add_c_unit_test(
        data_common_contract data.common.contract tests/unit/data/test_data_common.c)
    add_test(NAME conformance.data.common_contract COMMAND $<TARGET_FILE:data_common_contract>)
    set_tests_properties(
        conformance.data.common_contract PROPERTIES LABELS "conformance;data;provider")
    sloppy_add_c_unit_test(
        data_postgres_provider data.postgres.provider tests/unit/data/test_postgres.c)
    add_test(NAME conformance.postgres.native_provider COMMAND $<TARGET_FILE:data_postgres_provider>)
    set_tests_properties(
        conformance.postgres.native_provider PROPERTIES LABELS "conformance;postgres;provider")
    add_test(NAME data.postgres.live_provider COMMAND data_postgres_provider --live)
    set_tests_properties(
        data.postgres.live_provider PROPERTIES SKIP_RETURN_CODE 77 LABELS "live-provider;postgres")
    add_test(NAME conformance.postgres.native_live COMMAND data_postgres_provider --live)
    set_tests_properties(
        conformance.postgres.native_live
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "conformance;postgres;live-provider")
    sloppy_add_c_unit_test(
        data_sqlserver_provider data.sqlserver.provider tests/unit/data/test_sqlserver.c)
    add_test(NAME conformance.sqlserver.native_provider COMMAND $<TARGET_FILE:data_sqlserver_provider>)
    set_tests_properties(
        conformance.sqlserver.native_provider PROPERTIES LABELS "conformance;sqlserver;provider")
    add_test(NAME data.sqlserver.live_provider COMMAND data_sqlserver_provider --live)
    set_tests_properties(
        data.sqlserver.live_provider PROPERTIES SKIP_RETURN_CODE 77 LABELS "live-provider;sqlserver")
    add_test(NAME conformance.sqlserver.native_live COMMAND data_sqlserver_provider --live)
    set_tests_properties(
        conformance.sqlserver.native_live
        PROPERTIES SKIP_RETURN_CODE 77 LABELS "conformance;sqlserver;live-provider")
    sloppy_add_c_unit_test(data_sqlite_provider data.sqlite.provider tests/unit/data/test_sqlite.c)
    add_test(NAME conformance.sqlite.native_provider COMMAND $<TARGET_FILE:data_sqlite_provider>)
    set_tests_properties(
        conformance.sqlite.native_provider PROPERTIES LABELS "conformance;sqlite")
    add_test(
        NAME conformance.capability.native_registry
        COMMAND $<TARGET_FILE:core_capability_registry>)
    set_tests_properties(
        conformance.capability.native_registry PROPERTIES LABELS "conformance;capability")
    sloppy_add_c_unit_test(core_assert_compiles core.assert.compiles tests/unit/core/test_assert.c)

    add_executable(
        core_assert_enabled_under_ndebug tests/unit/core/test_assert_enabled_under_ndebug.c)
    target_include_directories(
        core_assert_enabled_under_ndebug PRIVATE "${PROJECT_SOURCE_DIR}/include")
    target_link_libraries(core_assert_enabled_under_ndebug PRIVATE sloppy_core)
    target_compile_features(core_assert_enabled_under_ndebug PRIVATE c_std_17)
    target_compile_definitions(core_assert_enabled_under_ndebug PRIVATE SL_ENABLE_ASSERTS=1 NDEBUG)
    sloppy_apply_warnings(core_assert_enabled_under_ndebug)
    sloppy_apply_sanitizers(core_assert_enabled_under_ndebug)
    add_test(NAME core.assert.enabled_under_ndebug COMMAND core_assert_enabled_under_ndebug)
    set_tests_properties(core.assert.enabled_under_ndebug PROPERTIES WILL_FAIL TRUE)

    if(CMAKE_CXX_COMPILER)
        add_executable(core_source_loc_cpp_syntax tests/unit/core/test_source_loc_cpp.cpp)
        target_include_directories(
            core_source_loc_cpp_syntax PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_compile_features(core_source_loc_cpp_syntax PRIVATE cxx_std_17)
        sloppy_apply_warnings(core_source_loc_cpp_syntax)
        sloppy_apply_sanitizers(core_source_loc_cpp_syntax)
        add_test(NAME core.source_loc.cpp_syntax COMMAND core_source_loc_cpp_syntax)
        add_executable(core_public_headers_cpp_syntax tests/unit/core/test_public_headers_cpp.cpp)
        target_include_directories(
            core_public_headers_cpp_syntax PRIVATE "${PROJECT_SOURCE_DIR}/include")
        target_link_libraries(core_public_headers_cpp_syntax PRIVATE sloppy_core)
        target_compile_features(core_public_headers_cpp_syntax PRIVATE cxx_std_17)
        sloppy_apply_warnings(core_public_headers_cpp_syntax)
        sloppy_apply_sanitizers(core_public_headers_cpp_syntax)
        add_test(NAME core.public_headers.cpp_syntax COMMAND core_public_headers_cpp_syntax)
    endif()

    add_test(NAME sloppy.cli.version COMMAND sloppy --version)
    set_tests_properties(sloppy.cli.version PROPERTIES PASS_REGULAR_EXPRESSION "Sloppy")

    add_test(NAME sloppy.cli.help COMMAND sloppy --help)
    set_tests_properties(sloppy.cli.help PROPERTIES PASS_REGULAR_EXPRESSION "Pre-alpha runtime")

    add_executable(cli_dev_watch_plan tests/unit/cli/test_dev_watch_plan.c src/cli/dev_watch_plan.c)
    target_include_directories(cli_dev_watch_plan PRIVATE "${PROJECT_SOURCE_DIR}/include"
                                                          "${PROJECT_SOURCE_DIR}/src")
    target_compile_features(cli_dev_watch_plan PRIVATE c_std_17)
    sloppy_apply_warnings(cli_dev_watch_plan)
    sloppy_apply_sanitizers(cli_dev_watch_plan)
    add_test(NAME sloppy.cli.dev_watch_plan COMMAND cli_dev_watch_plan)
    set_tests_properties(sloppy.cli.dev_watch_plan PROPERTIES LABELS "cli;dev;watch")

    add_test(NAME benchmarks.sloppy_bench.list COMMAND sloppy_bench --list)
    set_tests_properties(
        benchmarks.sloppy_bench.list PROPERTIES PASS_REGULAR_EXPRESSION "memory.bytes.find_any")

    add_test(NAME benchmarks.sloppy_bench.smoke_json COMMAND sloppy_bench --smoke --format json)
    set_tests_properties(
        benchmarks.sloppy_bench.smoke_json
        PROPERTIES PASS_REGULAR_EXPRESSION "\"sloppyBenchmarkVersion\": 1")

    add_test(
        NAME benchmarks.sloppy_bench.json_profile_smoke
        COMMAND
            sloppy_bench --smoke --format json --bench
            json.request.native_schema.valid.payload_validate_only)
    set_tests_properties(
        benchmarks.sloppy_bench.json_profile_smoke
        PROPERTIES ENVIRONMENT "SLOPPY_JSON_PROFILE=1"
                   PASS_REGULAR_EXPRESSION "\"jsonProfile\"")

    add_test(
        NAME benchmarks.sloppy_bench.logging_smoke_json
        COMMAND sloppy_bench --smoke --format json --bench logging.enabled.five_fields)
    set_tests_properties(
        benchmarks.sloppy_bench.logging_smoke_json
        PROPERTIES PASS_REGULAR_EXPRESSION "\"name\": \"logging.enabled.five_fields\"")

    add_test(
        NAME benchmarks.sloppy_bench.ops_metrics_smoke_json
        COMMAND sloppy_bench --smoke --format json --bench ops.metrics.counter.inc)
    set_tests_properties(
        benchmarks.sloppy_bench.ops_metrics_smoke_json
        PROPERTIES PASS_REGULAR_EXPRESSION "\"name\": \"ops.metrics.counter.inc\"")

    if(SLOPPY_ENABLE_V8)
        add_test(
            NAME benchmarks.sloppy_bench.v8_bridge_smoke_json
            COMMAND
                sloppy_bench --include-v8 --smoke --format json --bench
                v8.bridge.call.noop_proxy)
        set_tests_properties(
            benchmarks.sloppy_bench.v8_bridge_smoke_json
            PROPERTIES PASS_REGULAR_EXPRESSION "\"name\": \"v8.bridge.call.noop_proxy\"")
    else()
        add_test(
            NAME benchmarks.sloppy_bench.v8_unavailable
            COMMAND sloppy_bench --include-v8 --bench v8.bridge.call.noop_proxy)
        set_tests_properties(
            benchmarks.sloppy_bench.v8_unavailable
            PROPERTIES PASS_REGULAR_EXPRESSION
                       "benchmark skipped/deferred: v8.bridge.call.noop_proxy")
    endif()

    if(WIN32)
        add_test(
            NAME benchmarks.windows_wrapper.smoke_json
            COMMAND
                powershell -NoProfile -ExecutionPolicy Bypass -File
                "${PROJECT_SOURCE_DIR}/tests/scripts/test_bench_wrapper_json.ps1" -RepoRoot
                "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME benchmarks.local_neutral.contract
            COMMAND
                powershell -NoProfile -ExecutionPolicy Bypass -File
                "${PROJECT_SOURCE_DIR}/tests/scripts/test_local_neutral_benchmark_contract.ps1"
                -RepoRoot "${PROJECT_SOURCE_DIR}")
        add_test(
            NAME test_engine.windows.contract
            COMMAND
                powershell -NoProfile -ExecutionPolicy Bypass -File
                "${PROJECT_SOURCE_DIR}/tests/scripts/test_test_engine_contract.ps1" -RepoRoot
                "${PROJECT_SOURCE_DIR}")
        set_tests_properties(
            test_engine.windows.contract PROPERTIES LABELS "meta;test-engine")
    endif()
