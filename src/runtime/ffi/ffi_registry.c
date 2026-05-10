#include "sloppy/ffi.h"

#include "sloppy/checked_math.h"
#include "sloppy/container.h"
#include "sloppy/platform.h"

#include <ffi.h>
#include <stdint.h>

static SlStatus sl_ffi_alloc_array(SlArena* arena, size_t count, size_t elem_size, size_t alignment,
                                   void** out)
{
    SlSlice slice = {0};

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (count == 0U) {
        return sl_status_ok();
    }
    if (arena == NULL || elem_size == 0U || alignment == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    SlStatus status = sl_arena_array_alloc(arena, count, elem_size, alignment, &slice);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    *out = slice.ptr;
    return sl_status_ok();
}

static void sl_ffi_set_diag(SlDiag* out_diag, SlDiagCode code, SlStr message, SlStr detail)
{
    if (out_diag == NULL) {
        return;
    }
    *out_diag = (SlDiag){0};
    out_diag->severity = SL_DIAG_SEVERITY_ERROR;
    out_diag->code = code;
    out_diag->message = message;
    out_diag->primary_span = sl_source_span_unknown();
    if (!sl_str_is_empty(detail)) {
        out_diag->hints[0] = detail;
        out_diag->hint_count = 1U;
    }
}

static ffi_type* sl_ffi_type_for_plan_type(SlPlanFfiType type)
{
    switch (type) {
    case SL_PLAN_FFI_TYPE_VOID:
        return &ffi_type_void;
    case SL_PLAN_FFI_TYPE_BOOL:
    case SL_PLAN_FFI_TYPE_U8:
        return &ffi_type_uint8;
    case SL_PLAN_FFI_TYPE_I8:
        return &ffi_type_sint8;
    case SL_PLAN_FFI_TYPE_U16:
        return &ffi_type_uint16;
    case SL_PLAN_FFI_TYPE_I16:
        return &ffi_type_sint16;
    case SL_PLAN_FFI_TYPE_U32:
        return &ffi_type_uint32;
    case SL_PLAN_FFI_TYPE_I32:
        return &ffi_type_sint32;
    case SL_PLAN_FFI_TYPE_U64:
        return &ffi_type_uint64;
    case SL_PLAN_FFI_TYPE_I64:
        return &ffi_type_sint64;
    case SL_PLAN_FFI_TYPE_F32:
        return &ffi_type_float;
    case SL_PLAN_FFI_TYPE_F64:
        return &ffi_type_double;
    case SL_PLAN_FFI_TYPE_ISIZE:
        return sizeof(void*) == sizeof(int64_t) ? &ffi_type_sint64 : &ffi_type_sint32;
    case SL_PLAN_FFI_TYPE_USIZE:
        return sizeof(void*) == sizeof(uint64_t) ? &ffi_type_uint64 : &ffi_type_uint32;
    case SL_PLAN_FFI_TYPE_PTR:
    case SL_PLAN_FFI_TYPE_CSTRING:
    case SL_PLAN_FFI_TYPE_UTF16:
    case SL_PLAN_FFI_TYPE_BYTES:
    case SL_PLAN_FFI_TYPE_MUT_BYTES:
        return &ffi_type_pointer;
    default:
        return NULL;
    }
}

static ffi_abi sl_ffi_abi_for_convention(SlPlanFfiCallingConvention convention)
{
    switch (convention) {
    case SL_PLAN_FFI_CALLING_CONVENTION_CDECL:
        return FFI_DEFAULT_ABI;
    case SL_PLAN_FFI_CALLING_CONVENTION_STDCALL:
#if defined(_WIN32) && defined(FFI_STDCALL)
        return FFI_STDCALL;
#else
        return FFI_DEFAULT_ABI;
#endif
    case SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM:
    default:
        return FFI_DEFAULT_ABI;
    }
}

size_t sl_ffi_type_size(SlPlanFfiType type)
{
    switch (type) {
    case SL_PLAN_FFI_TYPE_BOOL:
    case SL_PLAN_FFI_TYPE_I8:
    case SL_PLAN_FFI_TYPE_U8:
        return 1U;
    case SL_PLAN_FFI_TYPE_I16:
    case SL_PLAN_FFI_TYPE_U16:
        return 2U;
    case SL_PLAN_FFI_TYPE_I32:
    case SL_PLAN_FFI_TYPE_U32:
    case SL_PLAN_FFI_TYPE_F32:
        return 4U;
    case SL_PLAN_FFI_TYPE_I64:
    case SL_PLAN_FFI_TYPE_U64:
    case SL_PLAN_FFI_TYPE_F64:
        return 8U;
    case SL_PLAN_FFI_TYPE_ISIZE:
    case SL_PLAN_FFI_TYPE_USIZE:
    case SL_PLAN_FFI_TYPE_PTR:
    case SL_PLAN_FFI_TYPE_CSTRING:
    case SL_PLAN_FFI_TYPE_UTF16:
    case SL_PLAN_FFI_TYPE_BYTES:
    case SL_PLAN_FFI_TYPE_MUT_BYTES:
        return sizeof(void*);
    default:
        return 0U;
    }
}

size_t sl_ffi_type_alignment(SlPlanFfiType type)
{
    size_t size = sl_ffi_type_size(type);
    if (size == 0U) {
        return 1U;
    }
    return size > sizeof(void*) ? sizeof(void*) : size;
}

static SlStatus sl_ffi_prepare_function(SlFfiFunction* function, SlDiag* out_diag)
{
    ffi_type* return_type = sl_ffi_type_for_plan_type(function->return_type);
    ffi_type** parameters = (ffi_type**)function->native_parameters;
    ffi_cif* cif = (ffi_cif*)function->native_cif;
    ffi_status status;

    if (return_type == NULL || cif == NULL ||
        (function->parameter_count > 0U && parameters == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (function->convention == SL_PLAN_FFI_CALLING_CONVENTION_STDCALL) {
#if !(SL_PLATFORM_WINDOWS && defined(FFI_STDCALL))
        sl_ffi_set_diag(out_diag, SL_DIAG_FFI_UNSUPPORTED_CALLING_CONVENTION,
                        sl_str_from_cstr("stdcall is only supported on Windows"), function->symbol);
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
#endif
    }
    status = ffi_prep_cif(cif, sl_ffi_abi_for_convention(function->convention),
                          (unsigned int)function->parameter_count, return_type, parameters);
    if (status != FFI_OK) {
        sl_ffi_set_diag(out_diag, SL_DIAG_FFI_UNSUPPORTED_CALLING_CONVENTION,
                        sl_str_from_cstr("libffi rejected the FFI calling convention"),
                        function->symbol);
        return sl_status_from_code(SL_STATUS_UNSUPPORTED);
    }
    return sl_status_ok();
}

SlStatus sl_ffi_call(const SlFfiFunction* function, void* result, void** args, SlDiag* out_diag)
{
    if (function == NULL || function->native_cif == NULL || function->symbol_ptr == NULL) {
        sl_ffi_set_diag(out_diag, SL_DIAG_FFI_CALL_FAILED,
                        sl_str_from_cstr("FFI function is not prepared"), sl_str_empty());
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    ffi_call((ffi_cif*)function->native_cif, FFI_FN(function->symbol_ptr), result, args);
    return sl_status_ok();
}

static SlStr sl_ffi_library_load_path(const SlPlanFfiLibrary* library,
                                      const SlFfiLibraryOverride* overrides, size_t override_count)
{
    if (library == NULL) {
        return sl_str_empty();
    }
    if (overrides == NULL) {
        override_count = 0U;
    }
    for (size_t index = 0U; index < override_count; index += 1U) {
        if (sl_str_equal(overrides[index].name, library->name) &&
            !sl_str_is_empty(overrides[index].path))
        {
            return overrides[index].path;
        }
    }
    return library->name;
}

SlStatus sl_ffi_registry_init_from_plan(SlFfiRegistry* registry, SlArena* arena, const SlPlan* plan,
                                        const SlFfiLibraryOverride* library_overrides,
                                        size_t library_override_count, SlDiag* out_diag)
{
    size_t library_index = 0U;
    size_t function_total = 0U;
    size_t function_index = 0U;
    SlStatus status;

    if (registry == NULL || arena == NULL || plan == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *registry = (SlFfiRegistry){0};
    registry->arena = arena;

    if (plan->ffi_library_count == 0U) {
        registry->initialized = true;
        return sl_status_ok();
    }
    if (plan->ffi_libraries == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    for (library_index = 0U; library_index < plan->ffi_library_count; library_index += 1U) {
        const SlPlanFfiLibrary* plan_library = &plan->ffi_libraries[library_index];
        if (sl_str_is_empty(plan_library->name) ||
            (plan_library->function_count > 0U && plan_library->functions == NULL) ||
            !sl_status_is_ok(
                sl_checked_add_size(function_total, plan_library->function_count, &function_total)))
        {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
    }
    status = sl_ffi_alloc_array(arena, plan->ffi_library_count, sizeof(SlFfiLibrary),
                                _Alignof(SlFfiLibrary), (void**)&registry->libraries);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_ffi_alloc_array(arena, function_total, sizeof(SlFfiFunction),
                                _Alignof(SlFfiFunction), (void**)&registry->functions);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    registry->function_count = function_total;
    for (library_index = 0U; library_index < plan->ffi_library_count; library_index += 1U) {
        const SlPlanFfiLibrary* plan_library = &plan->ffi_libraries[library_index];
        SlFfiLibrary* library = &registry->libraries[library_index];
        SlStr load_path =
            sl_ffi_library_load_path(plan_library, library_overrides, library_override_count);

        library->name = plan_library->name;
        status = sl_platform_dynlib_open(load_path, &library->library, out_diag);
        if (!sl_status_is_ok(status)) {
            sl_ffi_registry_dispose(registry);
            return status;
        }
        registry->library_count = library_index + 1U;
        for (size_t fn = 0U; fn < plan_library->function_count; fn += 1U) {
            const SlPlanFfiFunction* plan_function = &plan_library->functions[fn];
            SlFfiFunction* function = &registry->functions[function_index];
            ffi_type** parameters = NULL;
            if (sl_str_is_empty(plan_function->id) || sl_str_is_empty(plan_function->name) ||
                sl_str_is_empty(plan_function->symbol) ||
                (plan_function->parameter_count > 0U && plan_function->parameters == NULL))
            {
                sl_ffi_registry_dispose(registry);
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            function->id = plan_function->id;
            function->library = plan_library->name;
            function->name = plan_function->name;
            function->symbol = plan_function->symbol;
            function->convention = plan_function->convention;
            function->return_type = plan_function->return_type;
            function->parameters = plan_function->parameters;
            function->parameter_count = plan_function->parameter_count;

            status = sl_platform_dynlib_symbol(&library->library, function->symbol,
                                               &function->symbol_ptr, out_diag);
            if (!sl_status_is_ok(status)) {
                sl_ffi_registry_dispose(registry);
                return status;
            }
            status = sl_ffi_alloc_array(arena, 1U, sizeof(ffi_cif), _Alignof(ffi_cif),
                                        &function->native_cif);
            if (!sl_status_is_ok(status)) {
                sl_ffi_registry_dispose(registry);
                return status;
            }
            status = sl_ffi_alloc_array(arena, function->parameter_count, sizeof(ffi_type*),
                                        _Alignof(ffi_type*), (void**)&parameters);
            if (!sl_status_is_ok(status)) {
                sl_ffi_registry_dispose(registry);
                return status;
            }
            function->native_parameters = parameters;
            for (size_t param = 0U; param < function->parameter_count; param += 1U) {
                parameters[param] = sl_ffi_type_for_plan_type(function->parameters[param]);
                if (parameters[param] == NULL) {
                    sl_ffi_registry_dispose(registry);
                    return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
                }
            }
            status = sl_ffi_prepare_function(function, out_diag);
            if (!sl_status_is_ok(status)) {
                sl_ffi_registry_dispose(registry);
                return status;
            }
            function_index += 1U;
        }
    }
    registry->initialized = true;
    return sl_status_ok();
}

SlStatus sl_ffi_registry_find(const SlFfiRegistry* registry, SlStr library, SlStr name,
                              const SlFfiFunction** out)
{
    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (registry == NULL || !registry->initialized) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (size_t index = 0U; index < registry->function_count; index += 1U) {
        const SlFfiFunction* function = &registry->functions[index];
        if (sl_str_equal(function->library, library) && sl_str_equal(function->name, name)) {
            *out = function;
            return sl_status_ok();
        }
    }
    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

void sl_ffi_registry_dispose(SlFfiRegistry* registry)
{
    if (registry == NULL) {
        return;
    }
    for (size_t index = 0U; index < registry->library_count; index += 1U) {
        sl_platform_dynlib_close(&registry->libraries[index].library);
    }
    registry->initialized = false;
    registry->libraries = NULL;
    registry->library_count = 0U;
    registry->functions = NULL;
    registry->function_count = 0U;
}
