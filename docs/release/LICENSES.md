# License and Notice Policy

Every package must include Sloppy's source license and placeholder license/notice files.
These files are sufficient for local package smoke only.

A complete third-party license review is required before any public release. That review
must inventory bundled native runtime libraries, optional V8 runtime files, compiler
dependencies, and any generated notices required by upstream dependencies.

The package scripts must not include source-only dependency caches, V8 SDK headers, V8
import libraries, vcpkg source trees, or build directories.
