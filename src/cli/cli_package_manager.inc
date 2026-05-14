/*
 * src/cli/cli_package_manager.inc
 *
 * Thin C command hooks for Rust-owned Sloppy package manager operations.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static int sl_cli_delegate_package_manager(const char* subcommand, bool locked)
{
    const char* compiler_path = sl_run_resolve_compiler_path("sloppy package manager");
    char* argv[4];
    size_t arg_count = 0U;
    SlPlatformProcessArgs process_args = {0};
    SlStatus status;
    int exit_code = 1;

    if (compiler_path == NULL || subcommand == NULL) {
        return 1;
    }

    argv[arg_count++] = (char*)compiler_path;
    argv[arg_count++] = (char*)subcommand;
    if (locked) {
        argv[arg_count++] = "--locked";
    }
    argv[arg_count] = NULL;

    process_args.file = compiler_path;
    process_args.argv = argv;
    status = sl_platform_process_run(&process_args, &exit_code);
    if (!sl_status_is_ok(status)) {
        sl_cli_write_error_with_value("sloppy package manager: compiler unavailable: ",
                                      compiler_path, "\n");
        return 1;
    }
    return exit_code;
}

static int sl_cli_command_pack(const SlCliOptions* options)
{
    (void)options;
    return sl_cli_delegate_package_manager("pack", false);
}

static int sl_cli_command_restore(const SlCliOptions* options)
{
    return sl_cli_delegate_package_manager("restore", options != NULL && options->restore_locked);
}
