/*
 * Sloppy placeholder CLI.
 *
 * This file proves the C toolchain and public header boundaries during the foundation
 * phase. It must not grow runtime features, routing, HTTP, V8, services, or compiler logic.
 */
#include "sloppy/compiler.h"
#include "sloppy/platform.h"
#include "sloppy/status.h"

#include <stdio.h>
#include <string.h>

static void sl_print_version(void)
{
    (void)printf("Sloppy %s\n", SL_VERSION_STRING);
}

static void sl_print_help(void)
{
    sl_print_version();
    (void)printf("Foundation build: runtime features are not implemented yet.\n\n");
    (void)printf("Usage:\n");
    (void)printf("  sloppy --help\n");
    (void)printf("  sloppy --version\n");
}

int main(int argc, char** argv)
{
    SlStatus status = sl_status_ok();

    if (!sl_status_is_ok(status)) {
        return 1;
    }

    if (argc > 1) {
        if (strcmp(argv[1], "--version") == 0) {
            sl_print_version();
            return 0;
        }

        if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
            sl_print_help();
            return 0;
        }
    }

    sl_print_help();
    return 0;
}
