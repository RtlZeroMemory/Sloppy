param()

$ErrorActionPreference = "Stop"

Write-Host "V8 source build tooling is not implemented yet."
Write-Host "This future script is maintainer-only, not the normal contributor path."
Write-Host "TASK 07.A only validates prebuilt SDK layout; it does not build or download V8."
Write-Host ""
Write-Host "Planned workflow:"
Write-Host "  1. Install/update depot_tools."
Write-Host "  2. Fetch the pinned V8 revision."
Write-Host "  3. Generate GN args for Windows x64."
Write-Host "  4. Build with Ninja."
Write-Host "  5. Package a Sloppy V8 SDK with include/, lib/, optional bin/, and manifest metadata."
Write-Host ""
Write-Host "Deferred to EPIC-07 follow-up debt:"
Write-Host "  - exact pinned V8 revision"
Write-Host "  - Sloppy SDK manifest/checksum format"
Write-Host "  - GN args and debug/release packaging matrix"
