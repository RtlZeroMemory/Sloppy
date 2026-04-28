param(
    [string]$DepotToolsRoot = ".sdeps/depot_tools",
    [string]$WorkRoot = ".sdeps/v8-work",
    [string]$SdkRoot = ".sdeps/v8/windows-x64",
    [string]$WindowsSdkPath = "C:\Program Files (x86)\Windows Kits\10",
    [string]$WindowsSdkVersion = "10.0.26100.0",
    [string]$V8Revision = "",
    [switch]$SkipFetch,
    [switch]$SkipBuild,
    [switch]$PackageOnly
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "msvc-env.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$FetchV8Script = Join-Path $PSScriptRoot "fetch-v8.ps1"
$DepotToolsGit = "https://chromium.googlesource.com/chromium/tools/depot_tools.git"

function Resolve-RepoPath {
    param([string]$Path)

    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }

    return (Join-Path $Root $Path)
}

function Invoke-Native {
    param(
        [string]$File,
        [string[]]$Arguments,
        [string]$WorkingDirectory = $Root
    )

    Push-Location $WorkingDirectory
    try {
        & $File @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$File failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Copy-DirectoryContents {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (-not (Test-Path -LiteralPath $Source -PathType Container)) {
        throw "Missing directory: $Source"
    }

    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    Get-ChildItem -LiteralPath $Source -Force |
        Copy-Item -Destination $Destination -Recurse -Force
}

function Write-GnArgs {
    param([string]$ArgsPath)

    $content = @"
is_debug = false
target_cpu = "x64"
is_component_build = false
v8_monolithic = true
v8_use_external_startup_data = false
v8_enable_temporal_support = false
windows_sdk_path = "$($WindowsSdkPath -replace "\\", "\\")"
windows_sdk_version = "$WindowsSdkVersion"
treat_warnings_as_errors = false
"@

    Set-Content -LiteralPath $ArgsPath -Value $content -NoNewline
}

$DepotToolsRoot = Resolve-RepoPath $DepotToolsRoot
$WorkRoot = Resolve-RepoPath $WorkRoot
$SdkRoot = Resolve-RepoPath $SdkRoot
$V8Checkout = Join-Path $WorkRoot "v8"
$V8BuildDir = Join-Path $V8Checkout "out.gn/x64.release"

Import-SlVisualStudioEnvironment

if (-not (Test-Path -LiteralPath $DepotToolsRoot -PathType Container)) {
    Write-Host "Cloning depot_tools into $DepotToolsRoot"
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $DepotToolsRoot) | Out-Null
    Invoke-Native "git" @("clone", $DepotToolsGit, $DepotToolsRoot)
}

$env:PATH = "$DepotToolsRoot;$env:PATH"
$env:DEPOT_TOOLS_WIN_TOOLCHAIN = "0"
$env:GYP_MSVS_OVERRIDE_PATH = $env:VSINSTALLDIR
$env:vs2022_install = $env:VSINSTALLDIR

New-Item -ItemType Directory -Force -Path $WorkRoot | Out-Null

if (-not $SkipFetch -and -not $PackageOnly) {
    if (-not (Test-Path -LiteralPath $V8Checkout -PathType Container)) {
        Write-Host "Fetching V8 source into $WorkRoot"
        Invoke-Native "fetch" @("v8") $WorkRoot
    } else {
        Write-Host "Updating existing V8 checkout at $V8Checkout"
        Invoke-Native "git" @("fetch", "origin") $V8Checkout
        Invoke-Native "git" @("pull", "--ff-only") $V8Checkout
    }

    if (-not [string]::IsNullOrWhiteSpace($V8Revision)) {
        Invoke-Native "git" @("checkout", $V8Revision) $V8Checkout
    }

    Invoke-Native "gclient" @("sync") $V8Checkout
}

New-Item -ItemType Directory -Force -Path $V8BuildDir | Out-Null
Write-GnArgs -ArgsPath (Join-Path $V8BuildDir "args.gn")

if (-not $SkipBuild -and -not $PackageOnly) {
    Invoke-Native "gn" @("gen", "out.gn/x64.release") $V8Checkout
    Invoke-Native "ninja" @("-C", "out.gn/x64.release", "v8_monolith", "v8_libplatform", "v8_libbase") $V8Checkout
}

$libcxxObjects = @(Get-ChildItem -LiteralPath (Join-Path $V8BuildDir "obj/buildtools/third_party/libc++/libc++") -Filter "*.obj" -File -ErrorAction SilentlyContinue)
if ($libcxxObjects.Count -eq 0) {
    throw "No libc++ object files found under $V8BuildDir. Build V8 before packaging."
}

Remove-Item -LiteralPath $SdkRoot -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path (Join-Path $SdkRoot "include") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $SdkRoot "lib") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $SdkRoot "support/libcxx/buildtools") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $SdkRoot "share") | Out-Null

Copy-DirectoryContents -Source (Join-Path $V8Checkout "include") -Destination (Join-Path $SdkRoot "include")
Copy-Item -LiteralPath (Join-Path $V8BuildDir "obj/v8_monolith.lib") -Destination (Join-Path $SdkRoot "lib/v8_monolith.lib") -Force
Copy-Item -LiteralPath (Join-Path $V8BuildDir "obj/v8_libplatform.lib") -Destination (Join-Path $SdkRoot "lib/v8_libplatform.lib") -Force
Copy-Item -LiteralPath (Join-Path $V8BuildDir "obj/v8_libbase.lib") -Destination (Join-Path $SdkRoot "lib/v8_libbase.lib") -Force

$libcxxLibPath = Join-Path $SdkRoot "lib/libc++.lib"
$libArgs = @("/NOLOGO", "/OUT:$libcxxLibPath")
$libArgs += $libcxxObjects.FullName
Invoke-Native "lib.exe" $libArgs

Copy-DirectoryContents -Source (Join-Path $V8Checkout "third_party/libc++/src/include") -Destination (Join-Path $SdkRoot "support/libcxx/include")
Copy-Item -LiteralPath (Join-Path $V8Checkout "buildtools/third_party/libc++/__config_site") -Destination (Join-Path $SdkRoot "support/libcxx/buildtools/__config_site") -Force
Copy-Item -LiteralPath (Join-Path $V8Checkout "buildtools/third_party/libc++/__assertion_handler") -Destination (Join-Path $SdkRoot "support/libcxx/buildtools/__assertion_handler") -Force

$Revision = (& git -C $V8Checkout rev-parse HEAD).Trim()
$manifest = [ordered]@{
    name = "sloppy-v8-sdk"
    platform = "windows-x64"
    v8Revision = $Revision
    buildType = "release"
    crtCompatibility = "Release or RelWithDebInfo"
    gnArgs = @(
        "is_debug=false",
        "target_cpu=x64",
        "is_component_build=false",
        "v8_monolithic=true",
        "v8_use_external_startup_data=false",
        "v8_enable_temporal_support=false",
        "treat_warnings_as_errors=false"
    )
}

$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $SdkRoot "share/sloppy-v8-sdk.json")

& $FetchV8Script -ValidateOnly -V8Root $SdkRoot
if ($LASTEXITCODE -ne 0) {
    throw "Packaged V8 SDK failed validation."
}

Write-Host ""
Write-Host "Packaged Sloppy V8 SDK: $SdkRoot"
Write-Host "Configure with:"
Write-Host "  .\tools\windows\dev.ps1 configure -Preset windows-relwithdebinfo -CMakeArgs `"-DSLOPPY_ENABLE_V8=ON`",`"-DSLOPPY_V8_ROOT=$SdkRoot`""
