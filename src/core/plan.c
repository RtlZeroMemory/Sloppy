/*
 * src/core/plan.c
 *
 * Implements helpers for Sloppy Plan v1's borrowed native schema. This module defines
 * shape-level behavior and arena-based metadata interning for stable Plan pointers.
 *
 * Safety invariants:
 * - arena-only allocation; no heap malloc/free, parser, file I/O, platform API, or engine
 *   dependency;
 * - parsed Plan strings and arrays remain caller-owned until sl_plan_intern_metadata copies
 *   selected stable metadata into the caller-provided arena;
 * - handler lookup returns a borrowed pointer into the caller-owned handler table.
 *
 * Tests: tests/unit/core/test_plan.c.
 */
#include "sloppy/plan.h"

#include "sloppy/container.h"
#include "sloppy/http.h"

const SlPlanRequestBinding sl_plan_route_empty_bindings_sentinel = {0};

static bool sl_plan_token_equal(SlStr left, SlStr right)
{
    return !sl_str_is_empty(left) && sl_str_equal(left, right);
}

bool sl_plan_version_supported(uint32_t version)
{
    return version == SL_PLAN_VERSION_1;
}

bool sl_handler_id_valid(SlHandlerId id)
{
    return id != SL_HANDLER_ID_INVALID;
}

bool sl_plan_route_method_supported(SlStr method)
{
    SlHttpMethod http_method = SL_HTTP_METHOD_UNKNOWN;
    return sl_status_is_ok(sl_http_method_from_str(method, &http_method)) &&
           (sl_http_method_supported(http_method) || http_method == SL_HTTP_METHOD_OPTIONS);
}

bool sl_plan_route_method_runnable(SlStr method)
{
    return sl_plan_route_method_supported(method);
}

bool sl_plan_provider_supported(SlStr provider)
{
    return sl_str_equal(provider, sl_str_from_cstr("sqlite")) ||
           sl_str_equal(provider, sl_str_from_cstr("postgres")) ||
           sl_str_equal(provider, sl_str_from_cstr("sqlserver"));
}

bool sl_plan_capability_kind_supported(SlStr kind)
{
    return sl_str_equal(kind, sl_str_from_cstr("database")) ||
           sl_str_equal(kind, sl_str_from_cstr("filesystem")) ||
           sl_str_equal(kind, sl_str_from_cstr("network")) ||
           sl_str_equal(kind, sl_str_from_cstr("queue")) ||
           sl_str_equal(kind, sl_str_from_cstr("os")) ||
           sl_str_equal(kind, sl_str_from_cstr("env")) ||
           sl_str_equal(kind, sl_str_from_cstr("process")) ||
           sl_str_equal(kind, sl_str_from_cstr("signals")) ||
           sl_str_equal(kind, sl_str_from_cstr("time")) ||
           sl_str_equal(kind, sl_str_from_cstr("crypto")) ||
           sl_str_equal(kind, sl_str_from_cstr("codec")) ||
           sl_str_equal(kind, sl_str_from_cstr("workers")) ||
           sl_str_equal(kind, sl_str_from_cstr("ffi"));
}

bool sl_plan_capability_access_supported(SlStr kind, SlStr access)
{
    if (sl_str_equal(kind, sl_str_from_cstr("database"))) {
        return sl_str_equal(access, sl_str_from_cstr("read")) ||
               sl_str_equal(access, sl_str_from_cstr("write")) ||
               sl_str_equal(access, sl_str_from_cstr("readwrite"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("filesystem"))) {
        return sl_str_equal(access, sl_str_from_cstr("read")) ||
               sl_str_equal(access, sl_str_from_cstr("write")) ||
               sl_str_equal(access, sl_str_from_cstr("readwrite")) ||
               sl_str_equal(access, sl_str_from_cstr("append")) ||
               sl_str_equal(access, sl_str_from_cstr("delete")) ||
               sl_str_equal(access, sl_str_from_cstr("list")) ||
               sl_str_equal(access, sl_str_from_cstr("metadata")) ||
               sl_str_equal(access, sl_str_from_cstr("watch")) ||
               sl_str_equal(access, sl_str_from_cstr("lock"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("network"))) {
        return sl_str_equal(access, sl_str_from_cstr("connect")) ||
               sl_str_equal(access, sl_str_from_cstr("listen")) ||
               sl_str_equal(access, sl_str_from_cstr("connect-listen"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("queue"))) {
        return sl_str_equal(access, sl_str_from_cstr("enqueue")) ||
               sl_str_equal(access, sl_str_from_cstr("process")) ||
               sl_str_equal(access, sl_str_from_cstr("readwrite"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("os"))) {
        return sl_str_equal(access, sl_str_from_cstr("info"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("env"))) {
        return sl_str_equal(access, sl_str_from_cstr("read")) ||
               sl_str_equal(access, sl_str_from_cstr("list"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("process"))) {
        return sl_str_equal(access, sl_str_from_cstr("run")) ||
               sl_str_equal(access, sl_str_from_cstr("shell")) ||
               sl_str_equal(access, sl_str_from_cstr("signal")) ||
               sl_str_equal(access, sl_str_from_cstr("kill"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("signals"))) {
        return sl_str_equal(access, sl_str_from_cstr("shutdown"));
    }
    if (sl_str_equal(kind, sl_str_from_cstr("time")) ||
        sl_str_equal(kind, sl_str_from_cstr("crypto")) ||
        sl_str_equal(kind, sl_str_from_cstr("codec")) ||
        sl_str_equal(kind, sl_str_from_cstr("workers")) ||
        sl_str_equal(kind, sl_str_from_cstr("ffi")))
    {
        return sl_str_equal(access, sl_str_from_cstr("use"));
    }
    return false;
}

SlStatus sl_plan_ffi_type_from_str(SlStr text, SlPlanFfiType* out)
{
    if (out == NULL || (text.length > 0U && text.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = SL_PLAN_FFI_TYPE_UNKNOWN;
    if (sl_str_equal(text, sl_str_from_cstr("void"))) {
        *out = SL_PLAN_FFI_TYPE_VOID;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("bool"))) {
        *out = SL_PLAN_FFI_TYPE_BOOL;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("bool32")) ||
             sl_str_equal(text, sl_str_from_cstr("win.BOOL")))
    {
        *out = SL_PLAN_FFI_TYPE_I32;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("i8"))) {
        *out = SL_PLAN_FFI_TYPE_I8;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("u8"))) {
        *out = SL_PLAN_FFI_TYPE_U8;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("i16"))) {
        *out = SL_PLAN_FFI_TYPE_I16;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("u16"))) {
        *out = SL_PLAN_FFI_TYPE_U16;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("i32"))) {
        *out = SL_PLAN_FFI_TYPE_I32;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("ntstatus"))) {
        *out = SL_PLAN_FFI_TYPE_I32;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("u32"))) {
        *out = SL_PLAN_FFI_TYPE_U32;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("i64"))) {
        *out = SL_PLAN_FFI_TYPE_I64;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("u64"))) {
        *out = SL_PLAN_FFI_TYPE_U64;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("isize"))) {
        *out = SL_PLAN_FFI_TYPE_ISIZE;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("usize"))) {
        *out = SL_PLAN_FFI_TYPE_USIZE;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("f32"))) {
        *out = SL_PLAN_FFI_TYPE_F32;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("f64"))) {
        *out = SL_PLAN_FFI_TYPE_F64;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("ptr")) ||
             sl_str_equal(text, sl_str_from_cstr("handle")) ||
             sl_str_equal(text, sl_str_from_cstr("hwnd")) ||
             sl_str_equal(text, sl_str_from_cstr("hmodule")))
    {
        *out = SL_PLAN_FFI_TYPE_PTR;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("cstring")) ||
             sl_str_equal(text, sl_str_from_cstr("lpcstr")))
    {
        *out = SL_PLAN_FFI_TYPE_CSTRING;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("utf16")) ||
             sl_str_equal(text, sl_str_from_cstr("lpcwstr")))
    {
        *out = SL_PLAN_FFI_TYPE_UTF16;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("bytes"))) {
        *out = SL_PLAN_FFI_TYPE_BYTES;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("mutBytes"))) {
        *out = SL_PLAN_FFI_TYPE_MUT_BYTES;
    }
    else {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    return sl_status_ok();
}

SlStr sl_plan_ffi_type_name(SlPlanFfiType type)
{
    switch (type) {
    case SL_PLAN_FFI_TYPE_VOID:
        return sl_str_from_cstr("void");
    case SL_PLAN_FFI_TYPE_BOOL:
        return sl_str_from_cstr("bool");
    case SL_PLAN_FFI_TYPE_I8:
        return sl_str_from_cstr("i8");
    case SL_PLAN_FFI_TYPE_U8:
        return sl_str_from_cstr("u8");
    case SL_PLAN_FFI_TYPE_I16:
        return sl_str_from_cstr("i16");
    case SL_PLAN_FFI_TYPE_U16:
        return sl_str_from_cstr("u16");
    case SL_PLAN_FFI_TYPE_I32:
        return sl_str_from_cstr("i32");
    case SL_PLAN_FFI_TYPE_U32:
        return sl_str_from_cstr("u32");
    case SL_PLAN_FFI_TYPE_I64:
        return sl_str_from_cstr("i64");
    case SL_PLAN_FFI_TYPE_U64:
        return sl_str_from_cstr("u64");
    case SL_PLAN_FFI_TYPE_ISIZE:
        return sl_str_from_cstr("isize");
    case SL_PLAN_FFI_TYPE_USIZE:
        return sl_str_from_cstr("usize");
    case SL_PLAN_FFI_TYPE_F32:
        return sl_str_from_cstr("f32");
    case SL_PLAN_FFI_TYPE_F64:
        return sl_str_from_cstr("f64");
    case SL_PLAN_FFI_TYPE_PTR:
        return sl_str_from_cstr("ptr");
    case SL_PLAN_FFI_TYPE_CSTRING:
        return sl_str_from_cstr("cstring");
    case SL_PLAN_FFI_TYPE_UTF16:
        return sl_str_from_cstr("utf16");
    case SL_PLAN_FFI_TYPE_BYTES:
        return sl_str_from_cstr("bytes");
    case SL_PLAN_FFI_TYPE_MUT_BYTES:
        return sl_str_from_cstr("mutBytes");
    default:
        return sl_str_empty();
    }
}

SlStatus sl_plan_ffi_calling_convention_from_str(SlStr text, SlPlanFfiCallingConvention* out)
{
    if (out == NULL || (text.length > 0U && text.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = SL_PLAN_FFI_CALLING_CONVENTION_UNKNOWN;
    if (sl_str_equal(text, sl_str_from_cstr("system"))) {
        *out = SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("cdecl"))) {
        *out = SL_PLAN_FFI_CALLING_CONVENTION_CDECL;
    }
    else if (sl_str_equal(text, sl_str_from_cstr("stdcall"))) {
        *out = SL_PLAN_FFI_CALLING_CONVENTION_STDCALL;
    }
    else {
        return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
    }
    return sl_status_ok();
}

SlStr sl_plan_ffi_calling_convention_name(SlPlanFfiCallingConvention convention)
{
    switch (convention) {
    case SL_PLAN_FFI_CALLING_CONVENTION_SYSTEM:
        return sl_str_from_cstr("system");
    case SL_PLAN_FFI_CALLING_CONVENTION_CDECL:
        return sl_str_from_cstr("cdecl");
    case SL_PLAN_FFI_CALLING_CONVENTION_STDCALL:
        return sl_str_from_cstr("stdcall");
    default:
        return sl_str_empty();
    }
}

bool sl_plan_ffi_return_type_supported(SlPlanFfiType type)
{
    return type != SL_PLAN_FFI_TYPE_UNKNOWN && type != SL_PLAN_FFI_TYPE_CSTRING &&
           type != SL_PLAN_FFI_TYPE_UTF16 && type != SL_PLAN_FFI_TYPE_BYTES &&
           type != SL_PLAN_FFI_TYPE_MUT_BYTES;
}

SlStatus sl_plan_find_handler_by_id(const SlPlan* plan, SlHandlerId id, const SlPlanHandler** out)
{
    size_t index = 0U;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;

    if (plan == NULL || !sl_handler_id_valid(id) ||
        (plan->handler_count > 0U && plan->handlers == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    for (index = 0U; index < plan->handler_count; index += 1U) {
        if (plan->handlers[index].id == id) {
            *out = &plan->handlers[index];
            return sl_status_ok();
        }
    }

    return sl_status_from_code(SL_STATUS_OUT_OF_RANGE);
}

bool sl_plan_has_duplicate_handler_ids(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->handlers == NULL || plan->handler_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->handler_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->handler_count; inner += 1U) {
            if (plan->handlers[outer].id == plan->handlers[inner].id) {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_routes(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->routes == NULL || plan->route_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->route_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->route_count; inner += 1U) {
            if (sl_str_equal(plan->routes[outer].method, plan->routes[inner].method) &&
                sl_str_equal(plan->routes[outer].pattern, plan->routes[inner].pattern))
            {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_route_names(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->routes == NULL || plan->route_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->route_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->route_count; inner += 1U) {
            if (sl_plan_token_equal(plan->routes[outer].name, plan->routes[inner].name)) {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_data_provider_tokens(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->data_providers == NULL || plan->data_provider_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->data_provider_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->data_provider_count; inner += 1U) {
            if (sl_str_equal(plan->data_providers[outer].token, plan->data_providers[inner].token))
            {
                return true;
            }
        }
    }

    return false;
}

bool sl_plan_has_duplicate_capability_tokens(const SlPlan* plan)
{
    size_t outer = 0U;
    size_t inner = 0U;

    if (plan == NULL || plan->capabilities == NULL || plan->capability_count < 2U) {
        return false;
    }

    for (outer = 0U; outer < plan->capability_count - 1U; outer += 1U) {
        for (inner = outer + 1U; inner < plan->capability_count; inner += 1U) {
            if (sl_str_equal(plan->capabilities[outer].token, plan->capabilities[inner].token)) {
                return true;
            }
        }
    }

    return false;
}

static SlStatus sl_plan_intern_required(SlInternTable* table, SlStr text, SlStr* out)
{
    SlInternedString interned = {0};
    SlStatus status;

    if (table == NULL || out == NULL || (text.length > 0U && text.ptr == NULL)) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if (sl_str_is_empty(text)) {
        *out = sl_str_empty();
        return sl_status_ok();
    }

    status = sl_intern_table_intern(table, text, &interned);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = interned.text;
    return sl_status_ok();
}

static SlStatus sl_plan_alloc_copy(SlArena* arena, const void* src, size_t count, size_t item_size,
                                   size_t alignment, void** out)
{
    SlSlice copy = {0};
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    *out = NULL;
    if (count == 0U) {
        return sl_status_ok();
    }
    if (arena == NULL || src == NULL || item_size == 0U || alignment == 0U) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_arena_array_copy(arena, src, count, item_size, alignment, &copy);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = copy.ptr;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_header(SlInternTable* table, SlPlan* staged)
{
    SlStatus status;

    status = sl_plan_intern_required(table, staged->compiler_version, &staged->compiler_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status =
        sl_plan_intern_required(table, staged->runtime_min_version, &staged->runtime_min_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->stdlib_version, &staged->stdlib_version);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->target.platform, &staged->target.platform);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->target.engine, &staged->target.engine);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->bundle.id, &staged->bundle.id);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_plan_intern_required(table, staged->source_map.id, &staged->source_map.id);
}

static SlStatus sl_plan_intern_handlers(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanHandler* handlers = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->handler_count == 0U) {
        staged->handlers = NULL;
        return sl_status_ok();
    }
    if (staged->handlers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->handlers, staged->handler_count,
                                sizeof(SlPlanHandler), _Alignof(SlPlanHandler), (void**)&handlers);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (handlers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->handler_count; index += 1U) {
        status = sl_plan_intern_required(table, handlers[index].export_name,
                                         &handlers[index].export_name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, handlers[index].display_name,
                                         &handlers[index].display_name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->handlers = handlers;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_bindings(SlArena* arena, SlInternTable* table, SlPlanRoute* route)
{
    SlPlanRequestBinding* bindings = NULL;
    size_t index = 0U;
    SlStatus status;

    if (route->binding_count == 0U) {
        if (sl_plan_route_has_bindings(route)) {
            sl_plan_route_mark_bindings_empty(route);
            return sl_status_ok();
        }
        route->bindings = NULL;
        return sl_status_ok();
    }
    if (route->bindings == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, route->bindings, route->binding_count,
                                sizeof(SlPlanRequestBinding), _Alignof(SlPlanRequestBinding),
                                (void**)&bindings);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (bindings == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    for (index = 0U; index < route->binding_count; index += 1U) {
        status =
            sl_plan_intern_required(table, bindings[index].parameter, &bindings[index].parameter);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, bindings[index].name, &bindings[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, bindings[index].schema, &bindings[index].schema);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, bindings[index].type, &bindings[index].type);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    route->bindings = bindings;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_route_health(SlArena* arena, SlInternTable* table,
                                            SlPlanRoute* route)
{
    SlStr* checks = NULL;
    size_t index = 0U;
    SlStatus status;

    if (route == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_plan_intern_required(table, route->health_kind, &route->health_kind);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (route->health_check_count == 0U) {
        route->health_checks = NULL;
        return sl_status_ok();
    }
    if (route->health_checks == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_plan_alloc_copy(arena, route->health_checks, route->health_check_count,
                                sizeof(SlStr), _Alignof(SlStr), (void**)&checks);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (checks == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < route->health_check_count; index += 1U) {
        status = sl_plan_intern_required(table, checks[index], &checks[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    route->health_checks = checks;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_str_array(SlArena* arena, SlInternTable* table, const SlStr** values,
                                         size_t count)
{
    SlStr* copied = NULL;
    SlStatus status;

    if (values == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    if (count == 0U) {
        *values = NULL;
        return sl_status_ok();
    }
    if (*values == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status =
        sl_plan_alloc_copy(arena, *values, count, sizeof(SlStr), _Alignof(SlStr), (void**)&copied);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (copied == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (size_t index = 0U; index < count; index += 1U) {
        status = sl_plan_intern_required(table, copied[index], &copied[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    *values = copied;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_route_websocket(SlArena* arena, SlInternTable* table,
                                               SlPlanRoute* route)
{
    SlStatus status;

    if (route == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_plan_intern_str_array(arena, table, &route->websocket.protocols,
                                      route->websocket.protocol_count);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_plan_intern_str_array(arena, table, &route->websocket.origins,
                                    route->websocket.origin_count);
}

static SlStatus sl_plan_intern_routes(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanRoute* routes = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->route_count == 0U) {
        staged->routes = NULL;
        return sl_status_ok();
    }
    if (staged->routes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->routes, staged->route_count, sizeof(SlPlanRoute),
                                _Alignof(SlPlanRoute), (void**)&routes);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (routes == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->route_count; index += 1U) {
        status = sl_plan_intern_required(table, routes[index].kind, &routes[index].kind);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].method, &routes[index].method);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].pattern, &routes[index].pattern);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].name, &routes[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].native_response_kind,
                                         &routes[index].native_response_kind);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].native_response_body,
                                         &routes[index].native_response_body);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].native_response_content_type,
                                         &routes[index].native_response_content_type);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].json_request.schema,
                                         &routes[index].json_request.schema);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].json_request.fallback_reason,
                                         &routes[index].json_request.fallback_reason);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].json_response.schema,
                                         &routes[index].json_response.schema);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].json_response.fallback_reason,
                                         &routes[index].json_response.fallback_reason);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, routes[index].json_response.content_type,
                                         &routes[index].json_response.content_type);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_route_health(arena, table, &routes[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_route_websocket(arena, table, &routes[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_bindings(arena, table, &routes[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->routes = routes;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_route_dispatch(SlInternTable* table, SlPlan* staged)
{
    SlStatus status;

    if (staged == NULL || !staged->has_route_dispatch_artifact) {
        return sl_status_ok();
    }

    status = sl_plan_intern_required(table, staged->route_dispatch_artifact.kind,
                                     &staged->route_dispatch_artifact.kind);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, staged->route_dispatch_artifact.path,
                                     &staged->route_dispatch_artifact.path);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    return sl_plan_intern_required(table, staged->route_dispatch_artifact.hash,
                                   &staged->route_dispatch_artifact.hash);
}

typedef struct SlPlanSchemaNodeInternEntry
{
    const SlPlanSchemaNode* source;
    SlPlanSchemaNode* target;
    struct SlPlanSchemaNodeInternEntry* next;
} SlPlanSchemaNodeInternEntry;

typedef struct SlPlanSchemaInternMap
{
    SlPlanSchemaNodeInternEntry* head;
} SlPlanSchemaInternMap;

static SlPlanSchemaNode* sl_plan_schema_intern_map_find(const SlPlanSchemaInternMap* map,
                                                        const SlPlanSchemaNode* source)
{
    const SlPlanSchemaNodeInternEntry* entry = NULL;

    if (map == NULL || source == NULL) {
        return NULL;
    }
    for (entry = map->head; entry != NULL; entry = entry->next) {
        if (entry->source == source) {
            return entry->target;
        }
    }
    return NULL;
}

static SlStatus sl_plan_schema_intern_map_insert(SlArena* arena, SlPlanSchemaInternMap* map,
                                                 const SlPlanSchemaNode* source,
                                                 SlPlanSchemaNode* target)
{
    SlPlanSchemaNodeInternEntry* entry = NULL;
    void* ptr = NULL;
    SlStatus status;

    if (arena == NULL || map == NULL || source == NULL || target == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    status = sl_arena_alloc(arena, sizeof(SlPlanSchemaNodeInternEntry),
                            _Alignof(SlPlanSchemaNodeInternEntry), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    entry = (SlPlanSchemaNodeInternEntry*)ptr;
    entry->source = source;
    entry->target = target;
    entry->next = map->head;
    map->head = entry;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_schema_node(SlArena* arena, SlInternTable* table,
                                           SlPlanSchemaInternMap* map,
                                           const SlPlanSchemaNode* source,
                                           SlPlanSchemaNode* target);

static SlStatus sl_plan_intern_schema_node_alloc(SlArena* arena, SlInternTable* table,
                                                 SlPlanSchemaInternMap* map,
                                                 const SlPlanSchemaNode* source,
                                                 SlPlanSchemaNode** out)
{
    SlPlanSchemaNode* target = NULL;
    void* ptr = NULL;
    SlStatus status;

    if (out == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }
    *out = NULL;
    if (source == NULL) {
        return sl_status_ok();
    }
    target = sl_plan_schema_intern_map_find(map, source);
    if (target != NULL) {
        *out = target;
        return sl_status_ok();
    }

    status = sl_arena_alloc(arena, sizeof(SlPlanSchemaNode), _Alignof(SlPlanSchemaNode), &ptr);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    target = (SlPlanSchemaNode*)ptr;
    *target = (SlPlanSchemaNode){0};
    status = sl_plan_intern_schema_node(arena, table, map, source, target);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *out = target;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_schema_properties(SlArena* arena, SlInternTable* table,
                                                 SlPlanSchemaInternMap* map,
                                                 SlPlanSchemaNode* target)
{
    SlPlanSchemaProperty* properties = NULL;
    size_t index = 0U;
    SlStatus status;

    if (target->property_count == 0U) {
        target->properties = NULL;
        return sl_status_ok();
    }
    if (target->properties == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, target->properties, target->property_count,
                                sizeof(SlPlanSchemaProperty), _Alignof(SlPlanSchemaProperty),
                                (void**)&properties);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (properties == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    for (index = 0U; index < target->property_count; index += 1U) {
        SlPlanSchemaNode* property_schema = NULL;

        status = sl_plan_intern_required(table, properties[index].name, &properties[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_schema_node_alloc(arena, table, map, properties[index].schema,
                                                  &property_schema);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        properties[index].schema = property_schema;
    }

    target->properties = properties;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_schema_variants(SlArena* arena, SlInternTable* table,
                                               SlPlanSchemaInternMap* map, SlPlanSchemaNode* target)
{
    SlPlanSchemaNode* variants = NULL;
    size_t index = 0U;
    SlStatus status;

    if (target->variant_count == 0U) {
        target->variants = NULL;
        return sl_status_ok();
    }
    if (target->variants == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status =
        sl_plan_alloc_copy(arena, target->variants, target->variant_count, sizeof(SlPlanSchemaNode),
                           _Alignof(SlPlanSchemaNode), (void**)&variants);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (variants == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    for (index = 0U; index < target->variant_count; index += 1U) {
        status = sl_plan_intern_schema_node(arena, table, map, &target->variants[index],
                                            &variants[index]);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    target->variants = variants;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_schema_node(SlArena* arena, SlInternTable* table,
                                           SlPlanSchemaInternMap* map,
                                           const SlPlanSchemaNode* source, SlPlanSchemaNode* target)
{
    SlPlanSchemaNode* items = NULL;
    SlPlanSchemaNode* mapped = NULL;
    SlStatus status;

    if (arena == NULL || table == NULL || map == NULL || source == NULL || target == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mapped = sl_plan_schema_intern_map_find(map, source);
    if (mapped != NULL) {
        if (mapped != target) {
            *target = *mapped;
        }
        return sl_status_ok();
    }
    status = sl_plan_schema_intern_map_insert(arena, map, source, target);
    if (!sl_status_is_ok(status)) {
        return status;
    }

    *target = *source;
    status = sl_plan_intern_required(table, target->semantic, &target->semantic);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, target->validation, &target->validation);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_required(table, target->literal_string, &target->literal_string);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_schema_properties(arena, table, map, target);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    status = sl_plan_intern_schema_node_alloc(arena, table, map, target->items, &items);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    target->items = items;
    return sl_plan_intern_schema_variants(arena, table, map, target);
}

static SlStatus sl_plan_intern_schemas(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanSchema* schemas = NULL;
    SlPlanSchemaInternMap map = {0};
    size_t index = 0U;
    SlStatus status;

    if (staged->schema_count == 0U) {
        staged->schemas = NULL;
        return sl_status_ok();
    }
    if (staged->schemas == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->schemas, staged->schema_count, sizeof(SlPlanSchema),
                                _Alignof(SlPlanSchema), (void**)&schemas);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (schemas == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    for (index = 0U; index < staged->schema_count; index += 1U) {
        status = sl_plan_intern_required(table, schemas[index].name, &schemas[index].name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_schema_node(arena, table, &map, &staged->schemas[index].definition,
                                            &schemas[index].definition);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }

    staged->schemas = schemas;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_providers(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanDataProvider* providers = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->data_provider_count == 0U) {
        staged->data_providers = NULL;
        return sl_status_ok();
    }
    if (staged->data_providers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->data_providers, staged->data_provider_count,
                                sizeof(SlPlanDataProvider), _Alignof(SlPlanDataProvider),
                                (void**)&providers);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (providers == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->data_provider_count; index += 1U) {
        status = sl_plan_intern_required(table, providers[index].token, &providers[index].token);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, providers[index].provider, &providers[index].provider);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, providers[index].capability,
                                         &providers[index].capability);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, providers[index].service, &providers[index].service);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->data_providers = providers;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_capabilities(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanCapability* capabilities = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->capability_count == 0U) {
        staged->capabilities = NULL;
        return sl_status_ok();
    }
    if (staged->capabilities == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->capabilities, staged->capability_count,
                                sizeof(SlPlanCapability), _Alignof(SlPlanCapability),
                                (void**)&capabilities);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (capabilities == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->capability_count; index += 1U) {
        status =
            sl_plan_intern_required(table, capabilities[index].token, &capabilities[index].token);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, capabilities[index].kind, &capabilities[index].kind);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status =
            sl_plan_intern_required(table, capabilities[index].access, &capabilities[index].access);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        status = sl_plan_intern_required(table, capabilities[index].provider,
                                         &capabilities[index].provider);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->capabilities = capabilities;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_required_features(SlArena* arena, SlInternTable* table,
                                                 SlPlan* staged)
{
    SlPlanRequiredFeature* features = NULL;
    size_t index = 0U;
    SlStatus status;

    if (staged->required_feature_count == 0U) {
        staged->required_features = NULL;
        return sl_status_ok();
    }
    if (staged->required_features == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->required_features, staged->required_feature_count,
                                sizeof(SlPlanRequiredFeature), _Alignof(SlPlanRequiredFeature),
                                (void**)&features);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (features == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (index = 0U; index < staged->required_feature_count; index += 1U) {
        status = sl_plan_intern_required(table, features[index].id, &features[index].id);
        if (!sl_status_is_ok(status)) {
            return status;
        }
    }
    staged->required_features = features;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_ffi_libraries(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanFfiLibrary* libraries = NULL;
    size_t library_index = 0U;
    SlStatus status;

    if (staged->ffi_library_count == 0U) {
        staged->ffi_libraries = NULL;
        return sl_status_ok();
    }
    if (staged->ffi_libraries == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status = sl_plan_alloc_copy(arena, staged->ffi_libraries, staged->ffi_library_count,
                                sizeof(SlPlanFfiLibrary), _Alignof(SlPlanFfiLibrary),
                                (void**)&libraries);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (libraries == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }

    for (library_index = 0U; library_index < staged->ffi_library_count; library_index += 1U) {
        SlPlanFfiFunction* functions = NULL;
        SlPlanFfiLibrary* library = &libraries[library_index];
        size_t function_index = 0U;

        status = sl_plan_intern_required(table, library->name, &library->name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (library->function_count == 0U) {
            library->functions = NULL;
            continue;
        }
        if (library->functions == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_plan_alloc_copy(arena, library->functions, library->function_count,
                                    sizeof(SlPlanFfiFunction), _Alignof(SlPlanFfiFunction),
                                    (void**)&functions);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (functions == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        for (function_index = 0U; function_index < library->function_count; function_index += 1U) {
            SlPlanFfiType* parameters = NULL;
            SlPlanFfiFunction* function = &functions[function_index];

            status = sl_plan_intern_required(table, function->id, &function->id);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_plan_intern_required(table, function->library, &function->library);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_plan_intern_required(table, function->name, &function->name);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            status = sl_plan_intern_required(table, function->symbol, &function->symbol);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            if (function->parameter_count == 0U) {
                function->parameters = NULL;
                continue;
            }
            if (function->parameters == NULL) {
                return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
            }
            status = sl_plan_alloc_copy(arena, function->parameters, function->parameter_count,
                                        sizeof(SlPlanFfiType), _Alignof(SlPlanFfiType),
                                        (void**)&parameters);
            if (!sl_status_is_ok(status)) {
                return status;
            }
            function->parameters = parameters;
        }
        library->functions = functions;
    }

    staged->ffi_libraries = libraries;
    return sl_status_ok();
}

static SlStatus sl_plan_intern_ffi_structs(SlArena* arena, SlInternTable* table, SlPlan* staged)
{
    SlPlanFfiStruct* layouts = NULL;
    size_t layout_index = 0U;
    SlStatus status;

    if (staged->ffi_struct_count == 0U) {
        staged->ffi_structs = NULL;
        return sl_status_ok();
    }
    if (staged->ffi_structs == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    status =
        sl_plan_alloc_copy(arena, staged->ffi_structs, staged->ffi_struct_count,
                           sizeof(SlPlanFfiStruct), _Alignof(SlPlanFfiStruct), (void**)&layouts);
    if (!sl_status_is_ok(status)) {
        return status;
    }
    if (layouts == NULL) {
        return sl_status_from_code(SL_STATUS_INVALID_STATE);
    }
    for (layout_index = 0U; layout_index < staged->ffi_struct_count; layout_index += 1U) {
        SlPlanFfiStructField* fields = NULL;
        SlPlanFfiStruct* layout = &layouts[layout_index];
        size_t field_index = 0U;

        status = sl_plan_intern_required(table, layout->name, &layout->name);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (layout->layout.length == 0U) {
            layout->layout = sl_str_from_cstr("sequential");
        }
        status = sl_plan_intern_required(table, layout->layout, &layout->layout);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (layout->field_count == 0U) {
            layout->fields = NULL;
            continue;
        }
        if (layout->fields == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
        }
        status = sl_plan_alloc_copy(arena, layout->fields, layout->field_count,
                                    sizeof(SlPlanFfiStructField), _Alignof(SlPlanFfiStructField),
                                    (void**)&fields);
        if (!sl_status_is_ok(status)) {
            return status;
        }
        if (fields == NULL) {
            return sl_status_from_code(SL_STATUS_INVALID_STATE);
        }
        for (field_index = 0U; field_index < layout->field_count; field_index += 1U) {
            status =
                sl_plan_intern_required(table, fields[field_index].name, &fields[field_index].name);
            if (!sl_status_is_ok(status)) {
                return status;
            }
        }
        layout->fields = fields;
    }

    staged->ffi_structs = layouts;
    return sl_status_ok();
}

SlStatus sl_plan_intern_metadata(SlArena* arena, const SlPlan* plan, size_t capacity,
                                 size_t bucket_count, SlPlan* out_plan, SlInternTable* out_table)
{
    SlArenaMark mark = {0};
    SlInternTable table = {0};
    SlPlan staged = {0};
    SlStatus status;

    if (arena == NULL || plan == NULL || out_plan == NULL || out_table == NULL || capacity == 0U ||
        bucket_count == 0U)
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    if ((plan->handler_count > 0U && plan->handlers == NULL) ||
        (plan->route_count > 0U && plan->routes == NULL) ||
        (plan->schema_count > 0U && plan->schemas == NULL) ||
        (plan->data_provider_count > 0U && plan->data_providers == NULL) ||
        (plan->capability_count > 0U && plan->capabilities == NULL) ||
        (plan->required_feature_count > 0U && plan->required_features == NULL) ||
        (plan->ffi_library_count > 0U && plan->ffi_libraries == NULL) ||
        (plan->ffi_struct_count > 0U && plan->ffi_structs == NULL))
    {
        return sl_status_from_code(SL_STATUS_INVALID_ARGUMENT);
    }

    mark = sl_arena_mark(arena);
    staged = *plan;
    table.generation = out_table->generation;

    status = sl_intern_table_init(&table, arena, capacity, bucket_count);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    status = sl_plan_intern_header(&table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_handlers(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_routes(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_route_dispatch(&table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_schemas(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_providers(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_capabilities(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_required_features(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_ffi_libraries(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }
    status = sl_plan_intern_ffi_structs(arena, &table, &staged);
    if (!sl_status_is_ok(status)) {
        goto failure;
    }

    *out_plan = staged;
    *out_table = table;
    return sl_status_ok();

failure:
    sl_arena_reset_to(arena, mark);
    return status;
}
