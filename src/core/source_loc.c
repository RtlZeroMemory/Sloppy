/*
 * src/core/source_loc.c
 *
 * Implements default source-location helpers. Source locations are borrowed metadata and do
 * not own the file/function strings captured by compiler macros.
 *
 * Tests: tests/unit/core/test_source_loc.c.
 */
#include "sloppy/source_loc.h"

#include <stddef.h>

SlSourceLoc sl_source_loc_unknown(void)
{
    SlSourceLoc loc = {NULL, 0U, NULL};
    return loc;
}

bool sl_source_loc_is_unknown(SlSourceLoc loc)
{
    return loc.file == NULL && loc.line == 0U && loc.function == NULL;
}
