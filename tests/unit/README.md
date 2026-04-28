# Unit Tests

C unit tests live here and run through CTest.

The initial core tests are dependency-free C executables that return nonzero on failure.
munit remains a possible future framework if richer C test reporting becomes worthwhile.
Expected-failure CTest entries may be used for failure-mode behavior that intentionally
terminates the process, such as enabled assertions.

Early targets:

- status and core primitives;
- string, byte, and buffer primitives;
- allocator behavior;
- resource table lifetime rules;
- platform helpers.
