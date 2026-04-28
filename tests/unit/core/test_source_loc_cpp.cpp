#include "sloppy/source_loc.h"

int main()
{
    SlSourceLoc loc = SL_SOURCE_LOC_CURRENT;
    return loc.file != nullptr && loc.function != nullptr && loc.line != 0U ? 0 : 1;
}
