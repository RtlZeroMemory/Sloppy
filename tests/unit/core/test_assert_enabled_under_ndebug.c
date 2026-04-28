#include "sloppy/assert.h"

int main(void)
{
    SL_ASSERT_MSG(0, "NDEBUG must not disable Sloppy assertions");

    return 1;
}
