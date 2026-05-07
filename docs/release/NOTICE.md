# Notice Skeleton

This notice skeleton is for internal alpha artifact dry-runs.

No secrets, credentials, private endpoints, signing keys, API tokens, or machine-local
paths belong in release notes, package notices, or uploaded workflow artifacts.

Dry-run packages may include native runtime libraries restored by vcpkg or provided by the
host build environment. The exact package notice must be regenerated from the package
manifest and bundled runtime file list before any public release.
