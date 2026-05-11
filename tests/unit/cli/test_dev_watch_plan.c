#include "cli/dev_watch_plan.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

static const SlDevWatchRoot* find_root(const SlDevWatchPlan* plan, const char* path)
{
    for (size_t index = 0U; index < plan->root_count; index += 1U) {
        if (strcmp(plan->roots[index].path, path) == 0) {
            return &plan->roots[index];
        }
    }
    return NULL;
}

static void copy_text(char* buffer, size_t capacity, const char* text)
{
    size_t length = strlen(text);
    if (length >= capacity) {
        failures += 1;
        return;
    }
    memcpy(buffer, text, length + 1U);
}

static void expect_root(const SlDevWatchPlan* plan, const char* path, bool recursive)
{
    const SlDevWatchRoot* root = find_root(plan, path);
    if (root == NULL) {
        fprintf(stderr, "missing watch root: %s\n", path);
        failures += 1;
        return;
    }
    if (root->recursive != recursive) {
        fprintf(stderr, "watch root %s recursive mismatch\n", path);
        failures += 1;
    }
}

static void test_project_watch_plan_includes_source_config_assets_and_migrations(void)
{
    SlSloppyRunConfig config = {0};
    SlDevWatchPlan plan = {0};

    copy_text(config.entry, sizeof(config.entry), "src/main.ts");
    copy_text(config.asset_includes[0], sizeof(config.asset_includes[0]), "assets");
    config.asset_include_count = 1U;
    copy_text(config.module_includes[0], sizeof(config.module_includes[0]), "plugins");
    config.module_include_count = 1U;
    copy_text(config.migrations[0].path, sizeof(config.migrations[0].path), "db/migrations/*.sql");
    config.migration_count = 1U;

    if (!sl_dev_watch_plan_build(&config, NULL, &plan)) {
        fprintf(stderr, "project watch plan did not build\n");
        failures += 1;
        return;
    }

    expect_root(&plan, "src", true);
    expect_root(&plan, "sloppy.json", false);
    expect_root(&plan, "appsettings.json", false);
    expect_root(&plan, "appsettings.Development.json", false);
    expect_root(&plan, "public", true);
    expect_root(&plan, "static", true);
    expect_root(&plan, "wwwroot", true);
    expect_root(&plan, "templates", true);
    expect_root(&plan, "views", true);
    expect_root(&plan, "assets", true);
    expect_root(&plan, "plugins", true);
    expect_root(&plan, "db/migrations", true);
    if (plan.root_count != 12U) {
        fprintf(stderr, "unexpected project watch root count: %zu\n", plan.root_count);
        failures += 1;
    }
}

static void test_positional_source_uses_source_directory_without_project_config(void)
{
    SlDevWatchPlan plan = {0};

    if (!sl_dev_watch_plan_build(NULL, "examples/hello/app.ts", &plan)) {
        fprintf(stderr, "positional watch plan did not build\n");
        failures += 1;
        return;
    }

    expect_root(&plan, "examples/hello", true);
    expect_root(&plan, "sloppy.json", false);
    expect_root(&plan, "appsettings.json", false);
    expect_root(&plan, "appsettings.Development.json", false);
    expect_root(&plan, "public", true);
    expect_root(&plan, "static", true);
    expect_root(&plan, "wwwroot", true);
    expect_root(&plan, "templates", true);
    expect_root(&plan, "views", true);
    if (plan.root_count != 9U) {
        fprintf(stderr, "unexpected positional watch root count: %zu\n", plan.root_count);
        failures += 1;
    }
}

static void test_positional_directory_inputs_are_not_collapsed(void)
{
    SlDevWatchPlan plan = {0};

    if (!sl_dev_watch_plan_build(NULL, "examples/hello/", &plan)) {
        fprintf(stderr, "directory watch plan did not build\n");
        failures += 1;
        return;
    }

    expect_root(&plan, "examples/hello/", true);
    if (plan.root_count != 9U) {
        fprintf(stderr, "unexpected directory watch root count: %zu\n", plan.root_count);
        failures += 1;
    }
}

static void test_root_level_source_watches_current_directory(void)
{
    SlDevWatchPlan plan = {0};

    if (!sl_dev_watch_plan_build(NULL, "main.ts", &plan)) {
        fprintf(stderr, "root-level source watch plan did not build\n");
        failures += 1;
        return;
    }

    expect_root(&plan, ".", true);
    if (plan.root_count != 9U) {
        fprintf(stderr, "unexpected root-level source watch root count: %zu\n", plan.root_count);
        failures += 1;
    }
}

int main(void)
{
    test_project_watch_plan_includes_source_config_assets_and_migrations();
    test_positional_source_uses_source_directory_without_project_config();
    test_positional_directory_inputs_are_not_collapsed();
    test_root_level_source_watches_current_directory();
    return failures == 0 ? 0 : 1;
}
