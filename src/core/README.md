# Runtime Core

The C runtime core owns portable foundation code that other runtime modules can depend on.

Implemented TASK 02.A primitives:

- `SlStatus`/`SlStatusCode`;
- `SlSourceLoc`;
- borrowed `SlStr` and `SlBytes` views;
- checked `size_t` add/multiply helpers;
- internal assertion macros.

No app-host runtime feature code exists here yet. Add modules only after their ownership,
tests, diagnostics, and public headers are specified.
