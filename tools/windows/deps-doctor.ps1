param(
    [ValidateSet("OFF", "AUTO", "REQUIRED")]
    [string]$V8Mode = "AUTO",

    [ValidateSet("text", "json")]
    [string]$Format = "text"
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")
. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$ManifestPath = Join-Path $Root "tools/deps/sloppy-deps.json"

function Read-SlDepsManifest {
    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) {
        throw "Dependency manifest is missing: $ManifestPath"
    }

    try {
        return Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    } catch {
        throw "Dependency manifest is corrupt JSON: $($_.Exception.Message)"
    }
}

function ConvertTo-SlSemVersion {
    param([string]$Version)

    if ([string]::IsNullOrWhiteSpace($Version)) {
        return $null
    }
    if ($Version -notmatch "([0-9]+)(?:\.([0-9]+))?(?:\.([0-9]+))?") {
        return $null
    }

    $major = [int]$matches[1]
    $minor = if ($matches[2]) { [int]$matches[2] } else { 0 }
    $patch = if ($matches[3]) { [int]$matches[3] } else { 0 }
    return @($major, $minor, $patch)
}

function Test-SlVersionAtLeast {
    param(
        [string]$Actual,
        [string]$Minimum
    )

    $actualParts = ConvertTo-SlSemVersion -Version $Actual
    $minimumParts = ConvertTo-SlSemVersion -Version $Minimum
    if ($null -eq $actualParts -or $null -eq $minimumParts) {
        return $false
    }

    for ($i = 0; $i -lt 3; $i++) {
        if ($actualParts[$i] -gt $minimumParts[$i]) {
            return $true
        }
        if ($actualParts[$i] -lt $minimumParts[$i]) {
            return $false
        }
    }
    return $true
}

function Get-SlHostPlatformKey {
    $arch = [System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
    switch ($arch) {
        "x64" { $archKey = "x64" }
        "arm64" { $archKey = "arm64" }
        default { $archKey = $arch }
    }

    if ($IsWindows -or [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Windows)) {
        return "windows-$archKey"
    }
    if ($IsLinux -or [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::Linux)) {
        return "linux-$archKey"
    }
    if ($IsMacOS -or [System.Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([System.Runtime.InteropServices.OSPlatform]::OSX)) {
        return "macos-$archKey"
    }
    return "unknown-$archKey"
}

function Invoke-SlCommandVersion {
    param(
        [string]$CommandPath,
        [string[]]$Arguments
    )

    $versionArgs = if ($Arguments.Count -gt 0) { $Arguments } else { @("--version") }

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $CommandPath
    $startInfo.Arguments = ($versionArgs | ForEach-Object { '"' + ($_ -replace '"', '\"') + '"' }) -join " "
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            return $null
        }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit(5000)) {
            try {
                $process.Kill()
            } catch {
                # The timeout is reported as an unavailable/corrupt probe by the caller.
            }
            return $null
        }
        $exitCode = $process.ExitCode
        $output = @(
            $stdoutTask.GetAwaiter().GetResult(),
            $stderrTask.GetAwaiter().GetResult()
        ) -join [Environment]::NewLine
    } finally {
        $process.Dispose()
    }
    if ($exitCode -ne 0) {
        return $null
    }
    return $output.Trim()
}

function Test-SlTool {
    param(
        [string]$Name,
        [object]$Policy
    )

    $commandName = [string]$Policy.command
    $required = [bool]$Policy.required
    $minimumVersion = [string]$Policy.minimumVersion
    $command = Get-Command $commandName -ErrorAction SilentlyContinue

    if ($null -eq $command) {
        return [ordered]@{
            name = $Name
            kind = "tool"
            required = $required
            status = if ($required) { "missing" } else { "optional unavailable" }
            version = $null
            minimumVersion = $minimumVersion
            detail = "$commandName was not found on PATH."
        }
    }

    $versionArguments = @()
    if ($null -ne $Policy.versionArguments) {
        $versionArguments = @($Policy.versionArguments)
    }
    $versionText = Invoke-SlCommandVersion -CommandPath $command.Source -Arguments $versionArguments
    $actualVersion = $null
    $matchedFallbackVersion = $false
    if ($null -ne $versionText -and $versionText -match [string]$Policy.versionPattern) {
        $actualVersion = $matches[1]
    }
    if ([string]::IsNullOrWhiteSpace($actualVersion) -and $null -ne $versionText -and $versionText -match '([0-9]+(?:\.[0-9]+){1,2})') {
        $actualVersion = $matches[1]
        $matchedFallbackVersion = $true
    }

    if ([string]::IsNullOrWhiteSpace($actualVersion)) {
        return [ordered]@{
            name = $Name
            kind = "tool"
            required = $required
            status = if ($required) { "corrupt dependency" } else { "optional unavailable" }
            version = $null
            minimumVersion = $minimumVersion
            detail = "$commandName was found, but its version output did not match the manifest pattern."
            path = $command.Source
        }
    }

    if (-not (Test-SlVersionAtLeast -Actual $actualVersion -Minimum $minimumVersion)) {
        return [ordered]@{
            name = $Name
            kind = "tool"
            required = $required
            status = if ($required) { "wrong version" } else { "optional unavailable" }
            version = $actualVersion
            minimumVersion = $minimumVersion
            detail = "$commandName version $actualVersion is below required minimum $minimumVersion."
            path = $command.Source
        }
    }

    return [ordered]@{
        name = $Name
        kind = "tool"
        required = $required
        status = "found"
        version = $actualVersion
        minimumVersion = $minimumVersion
        detail = if ($matchedFallbackVersion) { "$commandName resolved using generic semantic version fallback." } else { "$commandName resolved." }
        path = $command.Source
    }
}

function Test-SlVcpkg {
    $candidates = New-Object System.Collections.Generic.List[string]
    $localRoot = Join-Path $Root ".sdeps/vcpkg"
    $candidates.Add($localRoot)

    if (-not [string]::IsNullOrWhiteSpace($env:VCPKG_ROOT)) {
        $candidates.Add($env:VCPKG_ROOT)
    }

    $vcpkgCommand = Get-Command "vcpkg" -ErrorAction SilentlyContinue
    if ($null -ne $vcpkgCommand) {
        $candidates.Add((Split-Path -Parent $vcpkgCommand.Source))
    }

    $installPath = Get-SlVisualStudioInstallPath
    if (-not [string]::IsNullOrWhiteSpace($installPath)) {
        $candidates.Add((Join-Path $installPath "VC/vcpkg"))
    }

    foreach ($candidate in $candidates) {
        if ([string]::IsNullOrWhiteSpace($candidate)) {
            continue
        }

        $toolchain = Join-Path $candidate "scripts/buildsystems/vcpkg.cmake"
        if (Test-Path -LiteralPath $toolchain -PathType Leaf) {
            return [ordered]@{
                name = "vcpkg"
                kind = "toolchain"
                required = $true
                status = "found"
                version = $null
                minimumVersion = $null
                detail = "vcpkg CMake toolchain resolved."
                path = (Resolve-Path -LiteralPath $candidate).Path
            }
        }
    }

    return [ordered]@{
        name = "vcpkg"
        kind = "toolchain"
        required = $true
        status = "missing"
        version = $null
        minimumVersion = $null
        detail = "vcpkg CMake toolchain was not found. Configure requires .sdeps/vcpkg, VCPKG_ROOT, PATH vcpkg, or the Visual Studio bundled vcpkg layout with scripts/buildsystems/vcpkg.cmake."
    }
}

function Test-SlVisualStudioShell {
    $hasCHeader = Test-SlEnvFile -EnvValue $env:INCLUDE -FileName "stdio.h"
    $hasKernelLib = Test-SlEnvFile -EnvValue $env:LIB -FileName "kernel32.lib"
    $hasRuntimeLib = (Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrt.lib") -or
        (Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrtd.lib")
    $hasCompiler = $null -ne (Get-Command "clang-cl" -ErrorAction SilentlyContinue)
    $hasLinker = $null -ne (Get-Command "lld-link" -ErrorAction SilentlyContinue)

    if ($hasCompiler -and $hasLinker -and $hasCHeader -and $hasKernelLib -and $hasRuntimeLib) {
        return [ordered]@{
            name = "msvc-windows-sdk"
            kind = "toolchain"
            required = $true
            status = "found"
            detail = "clang-cl, lld-link, stdio.h, kernel32.lib, and msvcrt/msvcrtd.lib are available in the current shell."
        }
    }

    $installPath = Get-SlVisualStudioInstallPath
    $vcTools = $null
    if (-not [string]::IsNullOrWhiteSpace($installPath)) {
        $vcTools = Get-SlLatestDirectoryName -Path (Join-Path $installPath "VC/Tools/MSVC")
    }
    $sdkIncludeRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/Include"
    $sdkLibRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/Lib"
    $sdkInclude = Get-SlLatestDirectoryName -Path $sdkIncludeRoot
    $sdkLib = Get-SlLatestDirectoryName -Path $sdkLibRoot
    $installedCompiler = $null -ne $vcTools -and
        (Test-Path -LiteralPath (Join-Path $vcTools "bin/Hostx64/x64/clang-cl.exe") -PathType Leaf)
    $installedLinker = $null -ne $vcTools -and
        (Test-Path -LiteralPath (Join-Path $vcTools "bin/Hostx64/x64/lld-link.exe") -PathType Leaf)
    $installedCHeader = ($null -ne $vcTools -and
        (Test-Path -LiteralPath (Join-Path $vcTools "include/stdio.h") -PathType Leaf)) -or
        ($null -ne $sdkInclude -and
            (Test-Path -LiteralPath (Join-Path $sdkInclude "ucrt/stdio.h") -PathType Leaf))
    $installedKernelLib = $null -ne $sdkLib -and
        (Test-Path -LiteralPath (Join-Path $sdkLib "um/x64/kernel32.lib") -PathType Leaf)
    $installedRuntimeLib = ($null -ne $sdkLib -and
        ((Test-Path -LiteralPath (Join-Path $sdkLib "ucrt/x64/msvcrt.lib") -PathType Leaf) -or
            (Test-Path -LiteralPath (Join-Path $sdkLib "ucrt/x64/msvcrtd.lib") -PathType Leaf))) -or
        ($null -ne $vcTools -and
            ((Test-Path -LiteralPath (Join-Path $vcTools "lib/x64/msvcrt.lib") -PathType Leaf) -or
                (Test-Path -LiteralPath (Join-Path $vcTools "lib/x64/msvcrtd.lib") -PathType Leaf)))

    if ($installedCompiler -and $installedLinker -and $installedCHeader -and $installedKernelLib -and $installedRuntimeLib) {
        return [ordered]@{
            name = "msvc-windows-sdk"
            kind = "toolchain"
            required = $true
            status = "found"
            detail = "Visual Studio C++ tools and Windows SDK are installed. Configure/build import their environment when needed."
        }
    }

    $missing = @()
    if (-not ($hasCompiler -or $installedCompiler)) { $missing += "clang-cl" }
    if (-not ($hasLinker -or $installedLinker)) { $missing += "lld-link" }
    if (-not ($hasCHeader -or $installedCHeader)) { $missing += "stdio.h" }
    if (-not ($hasKernelLib -or $installedKernelLib)) { $missing += "kernel32.lib" }
    if (-not ($hasRuntimeLib -or $installedRuntimeLib)) { $missing += "msvcrt.lib or msvcrtd.lib" }

    if ($missing.Count -eq 0) {
        return [ordered]@{
            name = "msvc-windows-sdk"
            kind = "toolchain"
            required = $true
            status = "found"
            detail = "Visual Studio C++ tools and Windows SDK are available through the current shell or installed toolchain locations."
        }
    }

    return [ordered]@{
        name = "msvc-windows-sdk"
        kind = "toolchain"
        required = $true
        status = "missing"
        detail = "Current shell is missing: $($missing -join ', '). Configure/build still imports VsDevCmd when needed; doctor does not run that heavier import."
    }
}

function Test-SlV8ForDoctor {
    param([string]$Mode)

    if ($Mode -eq "OFF") {
        return [ordered]@{
            name = "v8-sdk"
            kind = "v8"
            required = $false
            status = "optional unavailable"
            mode = $Mode
            detail = "V8 mode is OFF; SDK validation was intentionally disabled for this command."
        }
    }

    try {
        $resolution = Resolve-SlV8SdkRootForMode -RepoRoot $Root -Mode $Mode
    } catch {
        $required = ($Mode -eq "REQUIRED")
        return [ordered]@{
            name = "v8-sdk"
            kind = "v8"
            required = $required
            status = if ($required) { "missing" } else { "optional unavailable" }
            mode = $Mode
            detail = [string]$_
        }
    }

    if ([string]::IsNullOrWhiteSpace($resolution.Root)) {
        return [ordered]@{
            name = "v8-sdk"
            kind = "v8"
            required = ($Mode -eq "REQUIRED")
            status = if ($Mode -eq "REQUIRED") { "missing" } else { "optional unavailable" }
            mode = $Mode
            detail = $resolution.Detail
        }
    }

    return [ordered]@{
        name = "v8-sdk"
        kind = "v8"
        required = ($Mode -eq "REQUIRED")
        status = "found"
        mode = $Mode
        detail = $resolution.Detail
        path = $resolution.Root
        platform = $resolution.Platform
        v8Revision = $resolution.V8Revision
    }
}

function Write-SlDoctorText {
    param(
        [string]$PlatformKey,
        [object]$Platform,
        [object[]]$Checks
    )

    $platformStatus = if ($null -eq $Platform) { "unsupported platform" } else { [string]$Platform.status }
    Write-Host "Sloppy dependency doctor"
    Write-Host "platform: $PlatformKey ($platformStatus)"
    Write-Host "v8 mode: $V8Mode"
    Write-Host ""

    foreach ($check in $Checks) {
        $requiredText = if ($check.required) { "required" } else { "optional" }
        $versionText = if ($check.version) { " version $($check.version)" } else { "" }
        Write-Host "[$($check.status)] $requiredText $($check.name)$versionText - $($check.detail)"
    }

    $blocking = @($Checks | Where-Object { $_.required -and $_.status -ne "found" })
    $optionalUnavailable = @($Checks | Where-Object { -not $_.required -and $_.status -ne "found" })
    Write-Host ""
    Write-Host "summary: blocking=$($blocking.Count) optionalUnavailable=$($optionalUnavailable.Count)"
}

$manifest = Read-SlDepsManifest
$platformKey = Get-SlHostPlatformKey
$platform = $manifest.platforms.PSObject.Properties[$platformKey].Value
$checks = New-Object System.Collections.Generic.List[object]

if ($null -eq $platform) {
    $checks.Add([ordered]@{
            name = "host-platform"
            kind = "platform"
            required = $true
            status = "unsupported platform"
            detail = "Host platform '$platformKey' is not represented in tools/deps/sloppy-deps.json."
        })
}

foreach ($tool in $manifest.toolchainPolicy.PSObject.Properties) {
    if ($tool.Name -eq "vcpkg") {
        $checks.Add((Test-SlVcpkg))
    } else {
        $checks.Add((Test-SlTool -Name $tool.Name -Policy $tool.Value))
    }
}

if ($platformKey -eq "windows-x64") {
    $checks.Add((Test-SlVisualStudioShell))
}

$checks.Add((Test-SlV8ForDoctor -Mode $V8Mode))

$checkArray = $checks.ToArray()
if ($Format -eq "json") {
    [ordered]@{
        schemaVersion = 1
        platform = $platformKey
        platformStatus = if ($null -eq $platform) { "unsupported platform" } else { [string]$platform.status }
        v8Mode = $V8Mode
        checks = $checkArray
    } | ConvertTo-Json -Depth 6
} else {
    Write-SlDoctorText -PlatformKey $platformKey -Platform $platform -Checks $checkArray
}

$blockingCount = @($checks | Where-Object { $_.required -and $_.status -ne "found" }).Count
$exitCode = if ($blockingCount -gt 0) { 1 } else { 0 }
[Environment]::ExitCode = $exitCode
exit $exitCode
