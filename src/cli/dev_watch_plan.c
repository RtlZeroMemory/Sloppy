#include "dev_watch_plan.h"

#include <stdint.h>

static size_t sl_dev_watch_text_length(const char* value)
{
    size_t length = 0U;

    if (value == NULL) {
        return 0U;
    }
    while (value[length] != '\0') {
        length += 1U;
    }
    return length;
}

static bool sl_dev_watch_copy(char* buffer, size_t capacity, const char* value)
{
    size_t length = sl_dev_watch_text_length(value);
    size_t index = 0U;

    if (buffer == NULL || capacity == 0U || length == 0U) {
        return false;
    }
    if (length >= capacity) {
        return false;
    }
    for (index = 0U; index < length; index += 1U) {
        buffer[index] = value[index];
    }
    buffer[length] = '\0';
    return true;
}

static bool sl_dev_watch_path_equal(const char* left, const char* right)
{
    size_t index = 0U;

    if (left == NULL || right == NULL) {
        return false;
    }
    while (left[index] != '\0' && right[index] != '\0') {
        if (left[index] != right[index]) {
            return false;
        }
        index += 1U;
    }
    return left[index] == right[index];
}

static bool sl_dev_watch_plan_add_root(SlDevWatchPlan* plan, const char* path, bool recursive)
{
    size_t index = 0U;

    if (plan == NULL || path == NULL || path[0] == '\0') {
        return false;
    }
    for (index = 0U; index < plan->root_count; index += 1U) {
        if (sl_dev_watch_path_equal(plan->roots[index].path, path)) {
            plan->roots[index].recursive = plan->roots[index].recursive || recursive;
            return true;
        }
    }
    if (plan->root_count >= SL_DEV_WATCH_MAX_ROOTS) {
        return false;
    }
    if (!sl_dev_watch_copy(plan->roots[plan->root_count].path,
                           sizeof(plan->roots[plan->root_count].path), path))
    {
        return false;
    }
    plan->roots[plan->root_count].recursive = recursive;
    plan->root_count += 1U;
    return true;
}

static bool sl_dev_watch_entry_root(char* buffer, size_t capacity, const char* entry)
{
    size_t index = 0U;
    size_t last_separator = SIZE_MAX;
    size_t copy_length = 0U;

    if (buffer == NULL || capacity == 0U || entry == NULL || entry[0] == '\0') {
        return false;
    }
    for (index = 0U; entry[index] != '\0'; index += 1U) {
        if (entry[index] == '/' || entry[index] == '\\') {
            last_separator = index;
        }
    }
    if (last_separator == SIZE_MAX) {
        return sl_dev_watch_copy(buffer, capacity, entry);
    }
    if (entry[last_separator + 1U] == '\0' ||
        (last_separator == 2U && entry[1] == ':' && (entry[2] == '\\' || entry[2] == '/')))
    {
        return sl_dev_watch_copy(buffer, capacity, entry);
    }
    if (last_separator == 0U || last_separator >= capacity) {
        return false;
    }
    copy_length = last_separator;
    if (copy_length + 1U > capacity) {
        return false;
    }
    for (index = 0U; index < copy_length; index += 1U) {
        buffer[index] = entry[index];
    }
    buffer[copy_length] = '\0';
    return true;
}

static bool sl_dev_watch_ends_with(const char* text, const char* suffix)
{
    size_t text_length = sl_dev_watch_text_length(text);
    size_t suffix_length = sl_dev_watch_text_length(suffix);
    size_t offset = 0U;

    if (text_length < suffix_length) {
        return false;
    }
    offset = text_length - suffix_length;
    for (size_t index = 0U; index < suffix_length; index += 1U) {
        if (text[offset + index] != suffix[index]) {
            return false;
        }
    }
    return true;
}

static bool sl_dev_watch_migration_dir(char* buffer, size_t capacity, const char* glob)
{
    size_t text_length = sl_dev_watch_text_length(glob);
    size_t length = 0U;
    size_t index = 0U;

    if (buffer == NULL || capacity == 0U || text_length == 0U ||
        (!sl_dev_watch_ends_with(glob, "/*.sql") && !sl_dev_watch_ends_with(glob, "\\*.sql")))
    {
        return false;
    }
    length = text_length - 6U;
    if (length == 0U || length >= capacity) {
        return false;
    }
    for (index = 0U; index < length; index += 1U) {
        buffer[index] = glob[index];
    }
    buffer[length] = '\0';
    return true;
}

static bool sl_dev_watch_plan_require_root(SlDevWatchPlan* plan, const char* path, bool recursive)
{
    return sl_dev_watch_plan_add_root(plan, path, recursive);
}

bool sl_dev_watch_plan_build(const SlSloppyRunConfig* config, const char* input_path,
                             SlDevWatchPlan* plan)
{
    char entry_root[SL_SLOPPYRC_PATH_MAX_BYTES];
    const char* entry = input_path;

    if (plan == NULL || (config == NULL && (input_path == NULL || input_path[0] == '\0'))) {
        return false;
    }
    *plan = (SlDevWatchPlan){0};
    if ((entry == NULL || entry[0] == '\0') && config != NULL) {
        entry = config->entry;
    }
    if (sl_dev_watch_entry_root(entry_root, sizeof(entry_root), entry)) {
        if (!sl_dev_watch_plan_require_root(plan, entry_root, true)) {
            return false;
        }
    }
    if (!sl_dev_watch_plan_require_root(plan, SL_SLOPPYRC_FILE, false) ||
        !sl_dev_watch_plan_require_root(plan, "appsettings.json", false) ||
        !sl_dev_watch_plan_require_root(plan, "appsettings.Development.json", false) ||
        !sl_dev_watch_plan_require_root(plan, "public", true) ||
        !sl_dev_watch_plan_require_root(plan, "static", true) ||
        !sl_dev_watch_plan_require_root(plan, "wwwroot", true) ||
        !sl_dev_watch_plan_require_root(plan, "templates", true) ||
        !sl_dev_watch_plan_require_root(plan, "views", true))
    {
        return false;
    }

    if (config != NULL) {
        for (size_t index = 0U; index < config->asset_include_count; index += 1U) {
            if (!sl_dev_watch_plan_require_root(plan, config->asset_includes[index], true)) {
                return false;
            }
        }
        for (size_t index = 0U; index < config->module_include_count; index += 1U) {
            if (!sl_dev_watch_plan_require_root(plan, config->module_includes[index], true)) {
                return false;
            }
        }
        for (size_t index = 0U; index < config->migration_count; index += 1U) {
            char migration_dir[SL_SLOPPYRC_PATH_MAX_BYTES];
            if (sl_dev_watch_migration_dir(migration_dir, sizeof(migration_dir),
                                           config->migrations[index].path))
            {
                if (!sl_dev_watch_plan_require_root(plan, migration_dir, true)) {
                    return false;
                }
            }
        }
    }

    return plan->root_count > 0U;
}
