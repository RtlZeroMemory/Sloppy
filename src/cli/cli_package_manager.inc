/*
 * src/cli/cli_package_manager.inc
 *
 * Thin C command hooks for Rust-owned Sloppy package manager operations.
 * Included only by src/main.c to preserve existing static helper visibility.
 */
static int sl_cli_delegate_package_manager(const SlCliOptions* options)
{
    const char* compiler_path = sl_run_resolve_compiler_path("sloppy package manager");
    char* argv[SL_CLI_MAX_PROGRAM_ARGS + 4U];
    size_t arg_count = 0U;
    size_t index = 0U;
    SlPlatformProcessArgs process_args = {0};
    SlStatus status;
    int exit_code = 1;

    if (compiler_path == NULL || options == NULL || options->command == NULL) {
        return 1;
    }

    argv[arg_count++] = (char*)compiler_path;
    argv[arg_count++] = (char*)options->command;
    if (options->format == SL_CLI_FORMAT_JSON && arg_count < SL_CLI_MAX_PROGRAM_ARGS + 3U) {
        argv[arg_count++] = "--json";
    }
    for (index = 0U; index < options->package_arg_count; index += 1U) {
        if (arg_count >= SL_CLI_MAX_PROGRAM_ARGS + 3U) {
            sl_cli_write_cstr(stderr, "sloppy package manager: too many arguments\n");
            return 1;
        }
        argv[arg_count++] = options->package_args[index];
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
    return sl_cli_delegate_package_manager(options);
}

static int sl_cli_command_restore(const SlCliOptions* options)
{
    return sl_cli_delegate_package_manager(options);
}

static int sl_cli_command_package_manager(const SlCliOptions* options)
{
    return sl_cli_delegate_package_manager(options);
}
