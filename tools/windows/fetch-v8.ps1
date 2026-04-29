param(
    [string]$Destination = ".sdeps/v8",
    [string]$V8Root = "",
    [switch]$ValidateOnly
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path

if ($ValidateOnly) {
    $rootToValidate = $V8Root
    if ([string]::IsNullOrWhiteSpace($rootToValidate)) {
        $rootToValidate = Resolve-SlV8SdkRoot -RepoRoot $Root -Require
    }

    if (-not (Test-SlV8SdkLayout -Root $rootToValidate)) {
        exit 1
    }

    exit 0
}

Write-Host "V8 SDK fetch is not implemented yet."
Write-Host "This helper intentionally downloads nothing until a pinned, verified SDK source exists."
Write-Host ""
Write-Host "Current local discovery workflow:"
Write-Host "  1. Put or build a Sloppy-compatible V8 SDK at .sdeps/v8/windows-x64 in any registered"
Write-Host "     git worktree, or set SLOPPY_V8_ROOT to an SDK root."
Write-Host "  2. Discover and validate it with:"
Write-Host "       .\tools\windows\resolve-v8-sdk.ps1"
Write-Host "  3. Configure V8 without hard-coding the SDK path with:"
Write-Host "       .\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -EnableV8"
Write-Host ""
Write-Host "Portable configuration options:"
Write-Host "  -V8Root <sdk-root>       Explicit SDK root for one command."
Write-Host "  SLOPPY_V8_ROOT           Explicit SDK root for the current shell/agent."
Write-Host "  SLOPPY_V8_SDK_HINTS      Search roots separated by '$([System.IO.Path]::PathSeparator)'."
Write-Host ""
Write-Host "The default non-V8 build does not need this SDK."
Write-Host ""
Write-SlV8ExpectedLayout
Write-Host ""
Write-Host "Default future destination, once fetching is implemented: $Destination"

$resolved = Resolve-SlV8SdkRoot -RepoRoot $Root -V8Root $V8Root
if ($null -ne $resolved) {
    Write-Host ""
    if ([string]::IsNullOrWhiteSpace($V8Root)) {
        Write-Host "Discovered compatible V8 SDK: $resolved"
    } else {
        Write-Host "Compatible V8 SDK from -V8Root: $resolved"
    }
}
