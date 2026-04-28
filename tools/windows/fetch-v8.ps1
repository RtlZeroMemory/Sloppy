param(
    [string]$Destination = ".sdeps/v8"
)

$ErrorActionPreference = "Stop"

Write-Host "V8 SDK fetch is not implemented yet."
Write-Host "This placeholder intentionally downloads nothing."
Write-Host ""
Write-Host "Future contributor workflow:"
Write-Host "  1. Resolve a pinned Sloppy-compatible V8 SDK manifest."
Write-Host "  2. Download verified Windows x64 prebuilt artifacts."
Write-Host "  3. Validate checksum/signature metadata."
Write-Host "  4. Expand to: $Destination"
Write-Host ""
Write-Host "Expected future SLOPPY_V8_ROOT layout:"
Write-Host "  include/"
Write-Host "  lib/"
Write-Host "  bin/"
Write-Host "  share/sloppy-v8-sdk.json"
