#include "sloppy/plan.h"

#include "fuzz_support.h"

#include <stddef.h>
#include <stdint.h>

#define FUZZ_ARENA_SIZE 131072U
#define FUZZ_RENDER_ARENA_SIZE 32768U

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    unsigned char arena_storage[FUZZ_ARENA_SIZE];
    unsigned char render_storage[FUZZ_RENDER_ARENA_SIZE];
    SlArena arena = {0};
    SlArena render_arena = {0};
    SlPlan plan = {0};
    SlDiag diag = {0};
    SlPlanParseOptions options = {0};
    SlStatus status;

    if (data == NULL || size == 0U) {
        return 0;
    }

    if (!sl_status_is_ok(sl_arena_init(&arena, arena_storage, sizeof(arena_storage)))) {
        return 0;
    }

    options.source_name = sl_str_from_cstr("fuzz://app.plan.json");
    status = sl_plan_parse_json(&arena, sl_bytes_from_parts(data, size), &options, &plan, &diag);
    if (sl_status_is_ok(status)) {
        const SlPlanHandler* handler = NULL;
        (void)sl_plan_has_duplicate_handler_ids(&plan);
        (void)sl_plan_has_duplicate_routes(&plan);
        (void)sl_plan_has_duplicate_route_names(&plan);
        (void)sl_plan_has_duplicate_data_provider_tokens(&plan);
        (void)sl_plan_has_duplicate_capability_tokens(&plan);
        if (plan.handler_count > 0U) {
            (void)sl_plan_find_handler_by_id(&plan, plan.handlers[0].id, &handler);
        }
    }
    else if (diag.code != SL_DIAG_NONE &&
             sl_status_is_ok(sl_arena_init(&render_arena, render_storage, sizeof(render_storage))))
    {
        SlStr rendered = {0};
        (void)sl_diag_render_json(&render_arena, &diag, &rendered);
        sl_arena_reset(&render_arena);
        (void)sl_diag_render_text(&render_arena, &diag, &rendered);
    }

    return 0;
}
