#include "sloppy/engine.h"

#include <stdint.h>
#include <stdio.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

int main(int argc, char** argv)
{
    unsigned char engine_storage[65536];
    unsigned char result_storage[4096];
    SlArena engine_arena = {0};
    SlArena result_arena = {0};
    SlPlanFfiType add_i32_params[2] = {SL_PLAN_FFI_TYPE_I32, SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType add_u64_params[2] = {SL_PLAN_FFI_TYPE_U64, SL_PLAN_FFI_TYPE_U64};
    SlPlanFfiType add_f64_params[2] = {SL_PLAN_FFI_TYPE_F64, SL_PLAN_FFI_TYPE_F64};
    SlPlanFfiType strlen_params[1] = {SL_PLAN_FFI_TYPE_CSTRING};
    SlPlanFfiType sum_bytes_params[2] = {SL_PLAN_FFI_TYPE_BYTES, SL_PLAN_FFI_TYPE_USIZE};
    SlPlanFfiType fill_params[3] = {SL_PLAN_FFI_TYPE_MUT_BYTES, SL_PLAN_FFI_TYPE_USIZE,
                                    SL_PLAN_FFI_TYPE_U8};
    SlPlanFfiType write_u32_params[1] = {SL_PLAN_FFI_TYPE_PTR};
    SlPlanFfiType point_sum_params[1] = {SL_PLAN_FFI_TYPE_PTR};
    SlPlanFfiFunction functions[8] = {{.id = sl_str_from_cstr("ffi:ffi-test:addI32"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("addI32"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_add_i32"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_I32,
                                       .parameters = add_i32_params,
                                       .parameter_count = 2U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:addU64"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("addU64"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_add_u64"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_U64,
                                       .parameters = add_u64_params,
                                       .parameter_count = 2U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:addF64"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("addF64"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_add_f64"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_F64,
                                       .parameters = add_f64_params,
                                       .parameter_count = 2U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:strlen"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("strlen"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_strlen"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_U32,
                                       .parameters = strlen_params,
                                       .parameter_count = 1U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:sumBytes"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("sumBytes"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_sum_bytes"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_U32,
                                       .parameters = sum_bytes_params,
                                       .parameter_count = 2U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:fill"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("fill"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_fill"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_VOID,
                                       .parameters = fill_params,
                                       .parameter_count = 3U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:writeU32"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("writeU32"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_write_u32"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_VOID,
                                       .parameters = write_u32_params,
                                       .parameter_count = 1U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:pointSum"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("pointSum"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_point_sum"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_I32,
                                       .parameters = point_sum_params,
                                       .parameter_count = 1U}};
    SlPlanFfiLibrary library = {.name = sl_str_from_cstr("ffi-test"),
                                .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                .functions = functions,
                                .function_count = 8U};
    SlPlanCapability capability = {.token = sl_str_from_cstr("ffi"),
                                   .kind = sl_str_from_cstr("ffi"),
                                   .access = sl_str_from_cstr("use")};
    SlPlan plan = {.kind = SL_PLAN_KIND_PROGRAM,
                   .ffi_libraries = &library,
                   .ffi_library_count = 1U,
                   .capabilities = &capability,
                   .capability_count = 1U};
    SlRuntimeFeatureAvailability availability = sl_runtime_feature_default_availability();
    SlRuntimeFeatureSet features = {0};
    SlCapabilityRegistry capabilities = {0};
    SlFfiLibraryOverride override = {0};
    SlEngineOptions options = {0};
    SlEngine* engine = NULL;
    SlEngineResult result = {0};
    SlDiag diag = {0};
    static const char source[] =
        "globalThis.ffiSmoke = function () {"
        "const ffi = __sloppy.ffi;"
        "const lib = ffi.library('ffi-test', {"
        "addI32:{kind:'ffi.fn',returnType:'i32',parameters:['i32','i32']},"
        "addU64:{kind:'ffi.fn',returnType:'u64',parameters:['u64','u64']},"
        "addF64:{kind:'ffi.fn',returnType:'f64',parameters:['f64','f64']},"
        "strlen:{kind:'ffi.fn',returnType:'u32',parameters:['cstring']},"
        "sumBytes:{kind:'ffi.fn',returnType:'u32',parameters:['bytes','usize']},"
        "fill:{kind:'ffi.fn',returnType:'void',parameters:['mutBytes','usize','u8']},"
        "writeU32:{kind:'ffi.fn',returnType:'void',parameters:['ptr']},"
        "pointSum:{kind:'ffi.fn',returnType:'i32',parameters:['ptr']}"
        "});"
        "const ref = ffi.ref('u32', 0);"
        "const bytes = new Uint8Array([1, 2, 3, 4]);"
        "const mut = ffi.buffer(3);"
        "const Point = ffi.struct('Point', { x: 'i32', y: 'i32' }, { layout: 'sequential' });"
        "const point = Point.alloc({ x: 19, y: 23 });"
        "lib.fill(mut, 3, 7);"
        "lib.writeU32(ref);"
        "return [lib.addI32(40,2), String(lib.addU64(40n,2n)), lib.addF64(40,2.5),"
        "lib.strlen('sloppy'), lib.sumBytes(bytes, bytes.length),"
        "Array.from(mut.read()).join(','), ref.get(), lib.pointSum(point)].join(':');"
        "};"
        "globalThis.ffiNegative = function () {"
        "const ffi = __sloppy.ffi;"
        "const lib = ffi.library('ffi-test', {"
        "addI32:{kind:'ffi.fn',returnType:'i32',parameters:['i32','i32']},"
        "strlen:{kind:'ffi.fn',returnType:'u32',parameters:['cstring']},"
        "writeU32:{kind:'ffi.fn',returnType:'void',parameters:['ptr']}"
        "});"
        "const capture = (fn, code) => { try { fn(); return 'NO_THROW'; }"
        "catch (e) { const text = String((e && e.message) || e);"
        "return text.includes(code) ? code : text; } };"
        "return ["
        "capture(() => lib.addI32(1), 'SLOPPY_E_FFI_INVALID_ARGUMENT_COUNT'),"
        "capture(() => lib.addI32('1', 2), 'SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE'),"
        "capture(() => lib.addI32(2147483648, 0), 'SLOPPY_E_FFI_INTEGER_OUT_OF_RANGE'),"
        "capture(() => lib.strlen('bad\\u0000text'), 'SLOPPY_E_FFI_STRING_NUL'),"
        "capture(() => { const ref = ffi.ref('u32', 1); ref.dispose(); lib.writeU32(ref); },"
        "'SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE')"
        "].join(':');"
        "};";

    if (argc < 2 || argv[1] == NULL) {
        return 1;
    }
    if (expect_status(sl_arena_init(&engine_arena, engine_storage, sizeof(engine_storage)),
                      SL_STATUS_OK) != 0 ||
        expect_status(sl_arena_init(&result_arena, result_storage, sizeof(result_storage)),
                      SL_STATUS_OK) != 0)
    {
        return 2;
    }

    availability.v8 = true;
    availability.stdlib_ffi = true;
    if (expect_status(
            sl_runtime_feature_activate_plan(&plan, &availability, &engine_arena, &features, &diag),
            SL_STATUS_OK) != 0 ||
        expect_status(sl_capability_registry_init_from_plan(&plan, &capabilities), SL_STATUS_OK) !=
            0)
    {
        return 3;
    }

    override.name = sl_str_from_cstr("ffi-test");
    override.path = sl_str_from_cstr(argv[1]);
    options.kind = SL_ENGINE_KIND_V8;
    options.runtime_name = sl_str_from_cstr("sloppy-v8-ffi-test");
    options.runtime_version = sl_str_from_cstr("0.2.0-test");
    options.target_platform = sl_str_from_cstr(SL_PLAN_TARGET_PLATFORM_WINDOWS_X64);
    options.target_engine = sl_str_from_cstr(SL_PLAN_TARGET_ENGINE_V8);
    options.plan = &plan;
    options.runtime_features = &features;
    options.capabilities = &capabilities;
    options.ffi_library_overrides = &override;
    options.ffi_library_override_count = 1U;

    if (expect_status(sl_engine_create(&options, &engine_arena, &engine), SL_STATUS_OK) != 0) {
        return 4;
    }
    if (expect_status(sl_engine_eval_source(engine, sl_str_from_cstr("ffi-smoke.js"),
                                            sl_str_from_parts(source, sizeof(source) - 1U), &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 5;
    }
    if (expect_status(sl_engine_call_function0(engine, &result_arena, sl_str_from_cstr("ffiSmoke"),
                                               &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 6;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text, sl_str_from_cstr("42:42:42.5:6:10:7,7,7:3737844653:42")))
    {
        sl_engine_destroy(engine);
        fprintf(stderr, "unexpected FFI smoke result\n");
        return 7;
    }
    if (expect_status(sl_engine_call_function0(engine, &result_arena,
                                               sl_str_from_cstr("ffiNegative"), &result, &diag),
                      SL_STATUS_OK) != 0)
    {
        sl_engine_destroy(engine);
        return 8;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text,
                      sl_str_from_cstr(
                          "SLOPPY_E_FFI_INVALID_ARGUMENT_COUNT:SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE:"
                          "SLOPPY_E_FFI_INTEGER_OUT_OF_RANGE:SLOPPY_E_FFI_STRING_NUL:"
                          "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE")))
    {
        sl_engine_destroy(engine);
        fprintf(stderr, "unexpected FFI negative result\n");
        return 9;
    }

    sl_engine_destroy(engine);
    return 0;
}
