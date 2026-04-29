param(
    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$ForwardedArgs
)

$script = Join-Path $PSScriptRoot "windows/resolve-v8-sdk.ps1"
& $script @ForwardedArgs
exit $LASTEXITCODE
