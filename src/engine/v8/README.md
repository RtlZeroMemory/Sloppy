# V8 Bridge

This directory is reserved for the isolated C++ V8 bridge.

Rules:

- V8 integration is not implemented in the foundation phase.
- C++ is allowed here only for engine binding work.
- `v8::*` types must never leak outside this directory.
- JS code must never receive raw C pointers.
- Native handles exposed to JS must flow through resource IDs with generation checks.
