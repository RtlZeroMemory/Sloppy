param(
    [switch]$Quiet,
    [string]$V8Root = "",
    [string[]]$SearchRoot = @()
)

$script = Join-Path $PSScriptRoot "windows/resolve-v8-sdk.ps1"
$forwardedArgs = @{}
if (-not [string]::IsNullOrWhiteSpace($V8Root)) {
    $forwardedArgs.V8Root = $V8Root
}
if ($SearchRoot.Count -gt 0) {
    $forwardedArgs.SearchRoot = $SearchRoot
}
if ($Quiet) {
    $forwardedArgs.Quiet = $true
}

& $script @forwardedArgs
if ($?) {
    exit 0
}
exit 1
