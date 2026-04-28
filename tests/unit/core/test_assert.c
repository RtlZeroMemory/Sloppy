#include "sloppy/assert.h"

int main(void)
{
    SL_ASSERT(1);
    SL_ASSERT_MSG(1, "true invariant should not fail");

    return 0;
}
