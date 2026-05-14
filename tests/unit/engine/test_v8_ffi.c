#include "sloppy/engine.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static void print_diag(const char* label, SlStatus status, const SlDiag* diag)
{
    fprintf(stderr, "%s: status=%d diag=%d message=%.*s\n", label, (int)sl_status_code(status),
            diag == NULL ? 0 : (int)diag->code, diag == NULL ? 0 : (int)diag->message.length,
            diag == NULL ? "" : diag->message.ptr);
}

int main(int argc, char** argv)
{
    static unsigned char engine_storage[1048576];
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
    SlPlanFfiType counter_create_params[1] = {SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType counter_add_params[2] = {SL_PLAN_FFI_TYPE_PTR, SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType counter_value_params[1] = {SL_PLAN_FFI_TYPE_PTR};
    SlPlanFfiType counter_destroy_params[1] = {SL_PLAN_FFI_TYPE_PTR};
    SlPlanFfiType callback_params[3] = {SL_PLAN_FFI_TYPE_PTR, SL_PLAN_FFI_TYPE_PTR,
                                        SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType callback_i32_params[2] = {SL_PLAN_FFI_TYPE_PTR, SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType callback_u32_params[2] = {SL_PLAN_FFI_TYPE_PTR, SL_PLAN_FFI_TYPE_U32};
    SlPlanFfiType callback_void_params[2] = {SL_PLAN_FFI_TYPE_PTR, SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType resolve_params[1] = {SL_PLAN_FFI_TYPE_CSTRING};
    SlPlanFfiFunction functions[28] = {
        {.id = sl_str_from_cstr("ffi:ffi-test:addI32"),
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
         .parameter_count = 1U},
        {.id = sl_str_from_cstr("ffi:ffi-test:sizeofMatrix"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("sizeofMatrix"),
         .symbol = sl_str_from_cstr("sloppy_ffi_sizeof_matrix"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:offsetofMatrixValues"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("offsetofMatrixValues"),
         .symbol = sl_str_from_cstr("sloppy_ffi_offsetof_matrix_values"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:sizeofNested"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("sizeofNested"),
         .symbol = sl_str_from_cstr("sloppy_ffi_sizeof_nested"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:offsetofNestedOrigin"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("offsetofNestedOrigin"),
         .symbol = sl_str_from_cstr("sloppy_ffi_offsetof_nested_origin"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:offsetofNestedSize"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("offsetofNestedSize"),
         .symbol = sl_str_from_cstr("sloppy_ffi_offsetof_nested_size"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:offsetofNestedFlags"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("offsetofNestedFlags"),
         .symbol = sl_str_from_cstr("sloppy_ffi_offsetof_nested_flags"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:sizeofTaggedPoint"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("sizeofTaggedPoint"),
         .symbol = sl_str_from_cstr("sloppy_ffi_sizeof_tagged_point"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:offsetofTaggedPointTag"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("offsetofTaggedPointTag"),
         .symbol = sl_str_from_cstr("sloppy_ffi_offsetof_tagged_point_tag"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:offsetofTaggedPointPoint"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("offsetofTaggedPointPoint"),
         .symbol = sl_str_from_cstr("sloppy_ffi_offsetof_tagged_point_point"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_USIZE,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:nullPointer"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("nullPointer"),
         .symbol = sl_str_from_cstr("sloppy_ffi_null_pointer"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_PTR,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:staticPointPointer"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("staticPointPointer"),
         .symbol = sl_str_from_cstr("sloppy_ffi_static_point_pointer"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_PTR,
         .parameters = NULL,
         .parameter_count = 0U},
        {.id = sl_str_from_cstr("ffi:ffi-test:createCounter"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("createCounter"),
         .symbol = sl_str_from_cstr("sloppy_ffi_counter_create"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_PTR,
         .parameters = counter_create_params,
         .parameter_count = 1U},
        {.id = sl_str_from_cstr("ffi:ffi-test:addCounter"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("addCounter"),
         .symbol = sl_str_from_cstr("sloppy_ffi_counter_add"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_I32,
         .parameters = counter_add_params,
         .parameter_count = 2U},
        {.id = sl_str_from_cstr("ffi:ffi-test:counterValue"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("counterValue"),
         .symbol = sl_str_from_cstr("sloppy_ffi_counter_value"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_I32,
         .parameters = counter_value_params,
         .parameter_count = 1U},
        {.id = sl_str_from_cstr("ffi:ffi-test:destroyCounter"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("destroyCounter"),
         .symbol = sl_str_from_cstr("sloppy_ffi_counter_destroy"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_VOID,
         .parameters = counter_destroy_params,
         .parameter_count = 1U},
        {.id = sl_str_from_cstr("ffi:ffi-test:callCallback"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("callCallback"),
         .symbol = sl_str_from_cstr("sloppy_ffi_call_callback"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_I32,
         .parameters = callback_params,
         .parameter_count = 3U},
        {.id = sl_str_from_cstr("ffi:ffi-test:callI32Callback"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("callI32Callback"),
         .symbol = sl_str_from_cstr("sloppy_ffi_call_i32_callback"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_I32,
         .parameters = callback_i32_params,
         .parameter_count = 2U},
        {.id = sl_str_from_cstr("ffi:ffi-test:callU32Callback"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("callU32Callback"),
         .symbol = sl_str_from_cstr("sloppy_ffi_call_u32_callback"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_U32,
         .parameters = callback_u32_params,
         .parameter_count = 2U},
        {.id = sl_str_from_cstr("ffi:ffi-test:callVoidCallback"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("callVoidCallback"),
         .symbol = sl_str_from_cstr("sloppy_ffi_call_void_callback"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_VOID,
         .parameters = callback_void_params,
         .parameter_count = 2U},
        {.id = sl_str_from_cstr("ffi:ffi-test:resolveSymbol"),
         .library = sl_str_from_cstr("ffi-test"),
         .name = sl_str_from_cstr("resolveSymbol"),
         .symbol = sl_str_from_cstr("sloppy_ffi_resolve_symbol"),
         .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
         .return_type = SL_PLAN_FFI_TYPE_PTR,
         .parameters = resolve_params,
         .parameter_count = 1U}};
    SlPlanFfiLibrary library = {.name = sl_str_from_cstr("ffi-test"),
                                .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                .functions = functions,
                                .function_count = 28U};
    SlPlanCapability capability = {.token = sl_str_from_cstr("ffi"),
                                   .kind = sl_str_from_cstr("ffi"),
                                   .access = sl_str_from_cstr("use")};
    SlPlan plan = {.kind = SL_PLAN_KIND_PROGRAM,
                   .ffi_libraries = &library,
                   .ffi_library_count = 1U,
                   .capabilities = &capability,
                   .capability_count = 1U};
    const char* library_path = argv[1];
#ifdef _WIN32
    static char library_path_storage[4096];
    if (_fullpath(library_path_storage, argv[1], sizeof(library_path_storage)) != NULL) {
        library_path = library_path_storage;
    }
#endif
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
        "pointSum:{kind:'ffi.fn',returnType:'i32',parameters:['ptr']},"
        "sizeofMatrix:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "offsetofMatrixValues:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "sizeofNested:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "offsetofNestedOrigin:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "offsetofNestedSize:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "offsetofNestedFlags:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "sizeofTaggedPoint:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "offsetofTaggedPointTag:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "offsetofTaggedPointPoint:{kind:'ffi.fn',returnType:'usize',parameters:[]},"
        "nullPointer:{kind:'ffi.fn',returnType:'ptr',parameters:[]},"
        "staticPointPointer:{kind:'ffi.fn',returnType:'ptr',parameters:[]},"
        "createCounter:{kind:'ffi.fn',returnType:'ptr',parameters:['i32']},"
        "addCounter:{kind:'ffi.fn',returnType:'i32',parameters:['ptr','i32']},"
        "counterValue:{kind:'ffi.fn',returnType:'i32',parameters:['ptr']},"
        "destroyCounter:{kind:'ffi.fn',returnType:'void',parameters:['ptr']},"
        "callCallback:{kind:'ffi.fn',returnType:'i32',parameters:['ptr','ptr','i32']},"
        "callI32Callback:{kind:'ffi.fn',returnType:'i32',parameters:['ptr','i32']},"
        "callU32Callback:{kind:'ffi.fn',returnType:'u32',parameters:['ptr','u32']},"
        "callVoidCallback:{kind:'ffi.fn',returnType:'void',parameters:['ptr','i32']},"
        "resolveSymbol:{kind:'ffi.fn',returnType:'ptr',parameters:['cstring']}"
        "});"
        "const ref = ffi.ref('u32', 0);"
        "const bytes = new Uint8Array([1, 2, 3, 4]);"
        "const writable = new Uint8Array([0, 0, 0]);"
        "const mut = ffi.buffer(3);"
        "const Point = ffi.struct('Point', { x: 'i32', y: 'i32' }, { layout: 'sequential' });"
        "const Matrix = ffi.struct('Matrix', { values: { kind: 'array', element: 'f32', length: 16 "
        "} });"
        "const Nested = ffi.struct('Nested', { origin: Point, size: Point, flags: 'u32' });"
        "const TaggedPoint = ffi.struct('TaggedPoint', { tag: 'u8', point: Point });"
        "const point = Point.alloc({ x: 19, y: 23 });"
        "const Padded = ffi.struct('Padded', { tag: 'u8', value: 'i32' });"
        "const padded = Padded.alloc();"
        "lib.fill(mut, 3, 7);"
        "lib.fill(writable, writable.length, 9);"
        "lib.writeU32(ref);"
        "const nullPointer = lib.nullPointer();"
        "const nativePointer = lib.staticPointPointer();"
        "const counter = lib.createCounter(3);"
        "const callback = ffi.callback({returnType:'i32',parameters:['i32']},"
        "(value) => value + 5);"
        "const churn = Array.from({ length: 256 }, (_, index) => ({ index }));"
        "const callbackValue = lib.callI32Callback(callback, churn.length - 249);"
        "const callbackU32 = ffi.callback({returnType:'u32',parameters:['u32']},"
        "(value) => value + 6);"
        "const callbackVoid = ffi.callback({returnType:'void',parameters:['i32']},"
        "(_value) => undefined);"
        "lib.callVoidCallback(callbackVoid, 3);"
        "const dispatch = ffi.dispatchTable('ffi-dispatch', { resolver: lib.resolveSymbol,"
        "symbols: { addI32:{kind:'ffi.fn',returnType:'i32',parameters:['i32','i32'],"
        "symbol:'sloppy_ffi_add_i32'} } });"
        "const dispatchValue = dispatch.addI32(10, 5);"
        "return [lib.addI32(40,2), String(lib.addU64(40n,2n)), lib.addF64(40,2.5),"
        "lib.strlen('sloppy'), lib.sumBytes(bytes, bytes.length),"
        "Array.from(mut.read()).join(','), Array.from(writable).join(','), ref.get(),"
        "lib.pointSum(point), padded.byteLength, nullPointer === null, typeof nativePointer, "
        "nativePointer.isNull(),"
        "typeof nativePointer.dispose, lib.pointSum(nativePointer), lib.addCounter(counter, 4),"
        "lib.counterValue(counter), callbackValue, lib.callU32Callback(callbackU32, 9),"
        "dispatchValue, Matrix.byteLength === Number(lib.sizeofMatrix()),"
        "Nested.byteLength === Number(lib.sizeofNested()),"
        "TaggedPoint.byteLength === Number(lib.sizeofTaggedPoint()),"
        "Number(lib.offsetofMatrixValues()), Number(lib.offsetofNestedOrigin()),"
        "Number(lib.offsetofNestedSize()), Number(lib.offsetofNestedFlags()),"
        "Number(lib.offsetofTaggedPointTag()), Number(lib.offsetofTaggedPointPoint())].join(':');"
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
        "'SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE'),"
        "capture(() => { const buffer = ffi.buffer(4); buffer.dispose(); lib.writeU32(buffer); },"
        "'SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE'),"
        "capture(() => { const Point = ffi.struct('Point', { x: 'i32', y: 'i32' });"
        "const point = Point.alloc({ x: 1, y: 2 }); point.dispose(); lib.writeU32(point); },"
        "'SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE'),"
        "capture(() => { const ref = ffi.ref('u32', 1); ref.dispose(); ref.dispose(); },"
        "'NO_THROW'),"
        "capture(() => ffi.cstringBuffer('bad\\u0000text'), 'SLOPPY_E_FFI_STRING_NUL'),"
        "capture(() => ffi.utf16Buffer('bad\\u0000text'), 'SLOPPY_E_FFI_STRING_NUL')"
        ",capture(() => ffi.callback({returnType:'i32',parameters:['ptr']}, () => 0),"
        "'SLOPPY_E_FFI_UNSUPPORTED_CALLBACK')"
        ",capture(() => ffi.callback({returnType:'ptr',parameters:['i32']}, () => null),"
        "'SLOPPY_E_FFI_UNSUPPORTED_CALLBACK')"
        ",capture(() => ffi.struct('Huge', { values: { kind: 'array', element: 'f64',"
        "length: 18446744073709551615n } }), 'SLOPPY_E_FFI_LAYOUT_OVERFLOW')"
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
    override.path = sl_str_from_cstr(library_path);
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

    SlStatus create_status = sl_engine_create(&options, &engine_arena, &engine);
    if (expect_status(create_status, SL_STATUS_OK) != 0) {
        fprintf(stderr, "failed to create V8 FFI test engine: status=%d\n",
                (int)sl_status_code(create_status));
        return 4;
    }
    SlStatus eval_status =
        sl_engine_eval_source(engine, sl_str_from_cstr("ffi-smoke.js"),
                              sl_str_from_parts(source, sizeof(source) - 1U), &diag);
    if (expect_status(eval_status, SL_STATUS_OK) != 0) {
        print_diag("failed to evaluate FFI smoke source", eval_status, &diag);
        sl_engine_destroy(engine);
        return 5;
    }
    SlStatus smoke_status = sl_engine_call_function0(engine, &result_arena,
                                                     sl_str_from_cstr("ffiSmoke"), &result, &diag);
    if (expect_status(smoke_status, SL_STATUS_OK) != 0) {
        print_diag("failed to call FFI smoke function", smoke_status, &diag);
        sl_engine_destroy(engine);
        return 6;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(
            result.text,
            sl_str_from_cstr("42:42:42.5:6:10:7,7,7:9,9,9:3737844653:42:8:true:object:false:"
                             "undefined:42:7:7:12:15:15:true:true:true:0:0:8:16:0:4")))
    {
        sl_engine_destroy(engine);
        fprintf(stderr, "unexpected FFI smoke result\n");
        return 7;
    }
    SlStatus negative_status = sl_engine_call_function0(
        engine, &result_arena, sl_str_from_cstr("ffiNegative"), &result, &diag);
    if (expect_status(negative_status, SL_STATUS_OK) != 0) {
        print_diag("failed to call FFI negative function", negative_status, &diag);
        sl_engine_destroy(engine);
        return 8;
    }
    if (result.kind != SL_ENGINE_RESULT_TEXT ||
        !sl_str_equal(result.text,
                      sl_str_from_cstr(
                          "SLOPPY_E_FFI_INVALID_ARGUMENT_COUNT:SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE:"
                          "SLOPPY_E_FFI_INTEGER_OUT_OF_RANGE:SLOPPY_E_FFI_STRING_NUL:"
                          "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE:"
                          "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE:"
                          "SLOPPY_E_FFI_INVALID_ARGUMENT_TYPE:NO_THROW:"
                          "SLOPPY_E_FFI_STRING_NUL:SLOPPY_E_FFI_STRING_NUL:"
                          "SLOPPY_E_FFI_UNSUPPORTED_CALLBACK:"
                          "SLOPPY_E_FFI_UNSUPPORTED_CALLBACK:"
                          "SLOPPY_E_FFI_LAYOUT_OVERFLOW")))
    {
        sl_engine_destroy(engine);
        fprintf(stderr, "unexpected FFI negative result\n");
        return 9;
    }

    sl_engine_destroy(engine);
    return 0;
}
