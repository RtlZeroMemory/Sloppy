param()

$ErrorActionPreference = "Stop"

Write-Host "V8 source build tooling is not implemented yet."
Write-Host "This future script is maintainer-only, not the normal contributor path."
Write-Host ""
Write-Host "Planned workflow:"
Write-Host "  1. Install/update depot_tools."
Write-Host "  2. Fetch the pinned V8 revision."
Write-Host "  3. Generate GN args for Windows x64."
Write-Host "  4. Build with Ninja."
Write-Host "  5. Package a Sloppy V8 SDK consumed by tools/windows/fetch-v8.ps1."
