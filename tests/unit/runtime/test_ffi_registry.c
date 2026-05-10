#include "sloppy/ffi.h"
#include "sloppy/platform.h"

#include <stdint.h>

static int expect_true(bool condition)
{
    return condition ? 0 : 1;
}

static int expect_status(SlStatus status, SlStatusCode code)
{
    return expect_true(sl_status_code(status) == code);
}

static int call_add_i32(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    int32_t left = 40;
    int32_t right = 2;
    int32_t result = 0;
    void* args[2] = {&left, &right};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("addI32"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, &result, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return result == 42 ? 0 : 2;
}

static int call_add_u64(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    uint64_t left = UINT64_C(40);
    uint64_t right = UINT64_C(2);
    uint64_t result = 0U;
    void* args[2] = {&left, &right};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("addU64"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, &result, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return result == UINT64_C(42) ? 0 : 2;
}

static int call_add_f64(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    double left = 40.0;
    double right = 2.5;
    double result = 0.0;
    void* args[2] = {&left, &right};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("addF64"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, &result, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return result == 42.5 ? 0 : 2;
}

static int call_strlen(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    const char* text = "sloppy";
    uint32_t result = 0U;
    void* pointer = (void*)text;
    void* args[1] = {&pointer};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("strlen"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, &result, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return result == 6U ? 0 : 2;
}

static int call_sum_bytes(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    uint8_t bytes[4] = {1U, 2U, 3U, 4U};
    uintptr_t length = sizeof(bytes);
    uint32_t result = 0U;
    void* pointer = bytes;
    void* args[2] = {&pointer, &length};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("sumBytes"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, &result, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return result == 10U ? 0 : 2;
}

static int call_fill(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    uint8_t bytes[3] = {0U, 0U, 0U};
    uintptr_t length = sizeof(bytes);
    uint8_t value = 7U;
    void* pointer = bytes;
    void* args[3] = {&pointer, &length, &value};

    if (expect_status(sl_ffi_registry_find(registry, library, sl_str_from_cstr("fill"), &function),
                      SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, NULL, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return bytes[0] == 7U && bytes[1] == 7U && bytes[2] == 7U ? 0 : 2;
}

static int call_write_u32(const SlFfiRegistry* registry, SlStr library)
{
    const SlFfiFunction* function = NULL;
    uint32_t value = 0U;
    void* pointer = &value;
    void* args[1] = {&pointer};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("writeU32"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, NULL, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return value == 0xdecafbadU ? 0 : 2;
}

static int call_point_sum(const SlFfiRegistry* registry, SlStr library)
{
    typedef struct TestPoint
    {
        int32_t x;
        int32_t y;
    } TestPoint;

    const SlFfiFunction* function = NULL;
    TestPoint point = {19, 23};
    int32_t result = 0;
    void* pointer = &point;
    void* args[1] = {&pointer};

    if (expect_status(
            sl_ffi_registry_find(registry, library, sl_str_from_cstr("pointSum"), &function),
            SL_STATUS_OK) != 0 ||
        function == NULL)
    {
        return 1;
    }
    if (expect_status(sl_ffi_call(function, &result, args, NULL), SL_STATUS_OK) != 0) {
        return 3;
    }
    return result == 42 ? 0 : 2;
}

int main(int argc, char** argv)
{
    unsigned char arena_storage[65536];
    SlArena arena = {0};
    SlFfiRegistry registry = {0};
    SlDiag diag = {0};
    SlPlanFfiType add_params[2] = {SL_PLAN_FFI_TYPE_I32, SL_PLAN_FFI_TYPE_I32};
    SlPlanFfiType add_u64_params[2] = {SL_PLAN_FFI_TYPE_U64, SL_PLAN_FFI_TYPE_U64};
    SlPlanFfiType add_f64_params[2] = {SL_PLAN_FFI_TYPE_F64, SL_PLAN_FFI_TYPE_F64};
    SlPlanFfiType strlen_params[1] = {SL_PLAN_FFI_TYPE_CSTRING};
    SlPlanFfiType bytes_params[2] = {SL_PLAN_FFI_TYPE_BYTES, SL_PLAN_FFI_TYPE_USIZE};
    SlPlanFfiType fill_params[3] = {SL_PLAN_FFI_TYPE_MUT_BYTES, SL_PLAN_FFI_TYPE_USIZE,
                                    SL_PLAN_FFI_TYPE_U8};
    SlPlanFfiType write_params[1] = {SL_PLAN_FFI_TYPE_PTR};
    SlPlanFfiType point_params[1] = {SL_PLAN_FFI_TYPE_PTR};
    SlPlanFfiFunction functions[8] = {{.id = sl_str_from_cstr("ffi:ffi-test:addI32"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("addI32"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_add_i32"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_I32,
                                       .parameters = add_params,
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
                                       .parameters = bytes_params,
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
                                       .parameters = write_params,
                                       .parameter_count = 1U},
                                      {.id = sl_str_from_cstr("ffi:ffi-test:pointSum"),
                                       .library = sl_str_from_cstr("ffi-test"),
                                       .name = sl_str_from_cstr("pointSum"),
                                       .symbol = sl_str_from_cstr("sloppy_ffi_point_sum"),
                                       .convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM,
                                       .return_type = SL_PLAN_FFI_TYPE_I32,
                                       .parameters = point_params,
                                       .parameter_count = 1U}};
    SlPlanFfiLibrary libraries[1] = {0};
    SlPlan plan = {0};
    SlStr library_name = {0};

    if (argc < 2 || argv[1] == NULL) {
        return 1;
    }
    if (expect_status(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)), SL_STATUS_OK) !=
        0)
    {
        return 2;
    }

    library_name = sl_str_from_cstr(argv[1]);
    libraries[0].name = library_name;
    libraries[0].convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM;
    libraries[0].functions = functions;
    libraries[0].function_count = 8U;
    plan.ffi_libraries = libraries;
    plan.ffi_library_count = 1U;

    {
        SlPlan invalid_plan = {0};

        invalid_plan.ffi_library_count = 1U;
        if (expect_status(
                sl_ffi_registry_init_from_plan(&registry, &arena, &invalid_plan, NULL, 0U, &diag),
                SL_STATUS_INVALID_ARGUMENT) != 0)
        {
            return 8;
        }
    }

    if (expect_status(sl_ffi_registry_init_from_plan(&registry, &arena, &plan, NULL, 0U, &diag),
                      SL_STATUS_OK) != 0 ||
        diag.code != SL_DIAG_NONE)
    {
        return 3;
    }
    if (call_add_i32(&registry, library_name) != 0 || call_add_u64(&registry, library_name) != 0 ||
        call_add_f64(&registry, library_name) != 0 || call_strlen(&registry, library_name) != 0 ||
        call_sum_bytes(&registry, library_name) != 0 || call_fill(&registry, library_name) != 0 ||
        call_write_u32(&registry, library_name) != 0 ||
        call_point_sum(&registry, library_name) != 0)
    {
        sl_ffi_registry_dispose(&registry);
        return 4;
    }
    sl_ffi_registry_dispose(&registry);

    {
        SlPlanFfiFunction missing_symbol = functions[0];
        SlPlanFfiLibrary missing_symbol_libraries[1] = {0};
        SlPlan missing_symbol_plan = {0};

        missing_symbol.symbol = sl_str_from_cstr("sloppy_ffi_missing_symbol");
        missing_symbol_libraries[0].name = library_name;
        missing_symbol_libraries[0].convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM;
        missing_symbol_libraries[0].functions = &missing_symbol;
        missing_symbol_libraries[0].function_count = 1U;
        missing_symbol_plan.ffi_libraries = missing_symbol_libraries;
        missing_symbol_plan.ffi_library_count = 1U;
        diag = (SlDiag){0};
        if (expect_status(sl_ffi_registry_init_from_plan(&registry, &arena, &missing_symbol_plan,
                                                         NULL, 0U, &diag),
                          SL_STATUS_OUT_OF_RANGE) != 0 ||
            diag.code != SL_DIAG_FFI_SYMBOL_NOT_FOUND)
        {
            return 5;
        }
    }

    {
        SlPlanFfiLibrary missing_library_libraries[1] = {0};
        SlPlan missing_library_plan = {0};

        missing_library_libraries[0].name =
            sl_str_from_cstr("__sloppy_missing_ffi_registry_library__");
        missing_library_libraries[0].convention = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM;
        missing_library_libraries[0].functions = functions;
        missing_library_libraries[0].function_count = 1U;
        missing_library_plan.ffi_libraries = missing_library_libraries;
        missing_library_plan.ffi_library_count = 1U;
        diag = (SlDiag){0};
        if (expect_status(sl_ffi_registry_init_from_plan(&registry, &arena, &missing_library_plan,
                                                         NULL, 0U, &diag),
                          SL_STATUS_UNSUPPORTED) != 0 ||
            diag.code != SL_DIAG_FFI_LIBRARY_NOT_FOUND)
        {
            return 6;
        }
    }

#if !SL_PLATFORM_WINDOWS
    {
        SlPlanFfiFunction stdcall_function = functions[0];
        SlPlanFfiLibrary stdcall_libraries[1] = {0};
        SlPlan stdcall_plan = {0};

        stdcall_function.convention = SL_PLAN_FFI_CALLING_CONVENTION_STDCALL;
        stdcall_libraries[0].name = library_name;
        stdcall_libraries[0].convention = SL_PLAN_FFI_CALLING_CONVENTION_STDCALL;
        stdcall_libraries[0].functions = &stdcall_function;
        stdcall_libraries[0].function_count = 1U;
        stdcall_plan.ffi_libraries = stdcall_libraries;
        stdcall_plan.ffi_library_count = 1U;
        diag = (SlDiag){0};
        if (expect_status(
                sl_ffi_registry_init_from_plan(&registry, &arena, &stdcall_plan, NULL, 0U, &diag),
                SL_STATUS_UNSUPPORTED) != 0 ||
            diag.code != SL_DIAG_FFI_UNSUPPORTED_CALLING_CONVENTION)
        {
            return 7;
        }
    }
#endif

    return 0;
}
