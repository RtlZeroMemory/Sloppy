# Platform Boundary

This directory is reserved for Sloppy-owned platform abstractions.

Windows-first is a workflow priority, not permission to write Windows-only core runtime
code. Core modules must not include OS-specific headers or call OS APIs directly.
