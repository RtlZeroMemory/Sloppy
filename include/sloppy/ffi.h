#ifndef SLOPPY_FFI_H
#define SLOPPY_FFI_H

#include "sloppy/arena.h"
#include "sloppy/diagnostics.h"
#include "sloppy/platform_dynlib.h"
#include "sloppy/plan.h"
#include "sloppy/status.h"

#include <ffi.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlFfiFunction
{
    SlStr id;
    SlStr library;
    SlStr name;
    SlStr symbol;
    SlPlanFfiCallingConvention convention;
    SlPlanFfiType return_type;
    const SlPlanFfiType* parameters;
    size_t parameter_count;
    ffi_type** ffi_parameters;
    ffi_cif cif;
    void* symbol_ptr;
} SlFfiFunction;

typedef struct SlFfiLibrary
{
    SlStr name;
    SlPlatformDynlib library;
} SlFfiLibrary;

typedef struct SlFfiLibraryOverride
{
    SlStr name;
    SlStr path;
} SlFfiLibraryOverride;

typedef struct SlFfiRegistry
{
    SlArena* arena;
    SlFfiLibrary* libraries;
    size_t library_count;
    SlFfiFunction* functions;
    size_t function_count;
    bool initialized;
} SlFfiRegistry;

SlStatus sl_ffi_registry_init_from_plan(SlFfiRegistry* registry, SlArena* arena, const SlPlan* plan,
                                        const SlFfiLibraryOverride* library_overrides,
                                        size_t library_override_count, SlDiag* out_diag);
SlStatus sl_ffi_registry_find(const SlFfiRegistry* registry, SlStr library, SlStr name,
                              const SlFfiFunction** out);
void sl_ffi_registry_dispose(SlFfiRegistry* registry);
ffi_type* sl_ffi_type_for_plan_type(SlPlanFfiType type);
ffi_abi sl_ffi_abi_for_convention(SlPlanFfiCallingConvention convention);
size_t sl_ffi_type_size(SlPlanFfiType type);
size_t sl_ffi_type_alignment(SlPlanFfiType type);

#ifdef __cplusplus
}
#endif

#endif
