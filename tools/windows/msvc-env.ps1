$ErrorActionPreference = "Stop"

function Test-SlEnvFile {
    param(
        [string]$EnvValue,
        [string]$FileName
    )

    if ([string]::IsNullOrWhiteSpace($EnvValue)) {
        return $false
    }

    foreach ($entry in $EnvValue.Split(';')) {
        if ([string]::IsNullOrWhiteSpace($entry)) {
            continue
        }

        if (Test-Path -LiteralPath (Join-Path $entry $FileName)) {
            return $true
        }
    }

    return $false
}

function Add-SlPathEntries {
    param(
        [string]$Name,
        [string[]]$Entries
    )

    $existing = [Environment]::GetEnvironmentVariable($Name, "Process")
    $parts = @()

    foreach ($entry in $Entries) {
        if (-not [string]::IsNullOrWhiteSpace($entry) -and (Test-Path -LiteralPath $entry)) {
            $parts += $entry
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($existing)) {
        $parts += $existing.Split(';') | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    }

    if ($parts.Count -gt 0) {
        [Environment]::SetEnvironmentVariable($Name, ($parts | Select-Object -Unique) -join ';',
            "Process")
    }
}

function Get-SlLatestDirectoryName {
    param(
        [string]$Path
    )

    if (-not (Test-Path -LiteralPath $Path)) {
        return $null
    }

    $dir = Get-ChildItem -LiteralPath $Path -Directory |
        Sort-Object -Property Name -Descending |
        Select-Object -First 1

    if ($null -eq $dir) {
        return $null
    }

    return $dir.FullName
}

function Get-SlVisualStudioInstallPath {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio/Installer/vswhere.exe"

    if (Test-Path -LiteralPath $vswhere) {
        $installPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
        if ($LASTEXITCODE -eq 0 -and -not [string]::IsNullOrWhiteSpace($installPath)) {
            return $installPath.Trim()
        }
    }

    $candidates = @(
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio/2022/Community"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio/2022/Professional"),
        (Join-Path $env:ProgramFiles "Microsoft Visual Studio/2022/Enterprise")
    )

    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate) {
            return $candidate
        }
    }

    return $null
}

function Add-SlVisualStudioToolPath {
    param(
        [string]$InstallPath
    )

    if ([string]::IsNullOrWhiteSpace($InstallPath)) {
        return $null
    }

    $vcTools = Get-SlLatestDirectoryName -Path (Join-Path $InstallPath "VC/Tools/MSVC")
    if ($null -eq $vcTools) {
        return $null
    }

    Add-SlPathEntries -Name "PATH" -Entries @(
        (Join-Path $vcTools "bin/Hostx64/x64")
    )
    return $vcTools
}

function Import-SlVisualStudioEnvironment {
    $originalPath = $env:PATH
    $existingInstallPath = Get-SlVisualStudioInstallPath

    if ((Test-SlEnvFile -EnvValue $env:INCLUDE -FileName "stdio.h") -and
        (Test-SlEnvFile -EnvValue $env:LIB -FileName "kernel32.lib") -and
        ((Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrt.lib") -or
            (Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrtd.lib")))
    {
        $null = Add-SlVisualStudioToolPath -InstallPath $existingInstallPath
        return
    }

    $installPath = $existingInstallPath
    if ([string]::IsNullOrWhiteSpace($installPath)) {
        throw "Visual Studio with C++ tools was not found. Install MSVC C++ build tools or run from a Developer PowerShell."
    }

    $vsDevCmd = Join-Path $installPath "Common7/Tools/VsDevCmd.bat"
    if (-not (Test-Path -LiteralPath $vsDevCmd)) {
        throw "VsDevCmd.bat was not found at $vsDevCmd."
    }

    $command = '"' + $vsDevCmd + '" -arch=x64 -host_arch=x64 >nul && set'
    $vars = & $env:ComSpec /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd.bat failed with exit code $LASTEXITCODE."
    }

    foreach ($line in $vars) {
        if ($line -match '^(.*?)=(.*)$') {
            [Environment]::SetEnvironmentVariable($matches[1], $matches[2], "Process")
        }
    }

    if (-not [string]::IsNullOrWhiteSpace($originalPath)) {
        Add-SlPathEntries -Name "PATH" -Entries ($originalPath.Split(';'))
    }
    $vcTools = Add-SlVisualStudioToolPath -InstallPath $installPath
    $sdkIncludeRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/Include"
    $sdkLibRoot = Join-Path ${env:ProgramFiles(x86)} "Windows Kits/10/Lib"
    $sdkInclude = Get-SlLatestDirectoryName -Path $sdkIncludeRoot
    $sdkLib = Get-SlLatestDirectoryName -Path $sdkLibRoot

    if ($null -ne $vcTools) {
        Add-SlPathEntries -Name "INCLUDE" -Entries @(
            (Join-Path $vcTools "include"),
            (Join-Path $vcTools "ATLMFC/include")
        )
        Add-SlPathEntries -Name "LIB" -Entries @(
            (Join-Path $vcTools "ATLMFC/lib/x64"),
            (Join-Path $vcTools "lib/x64")
        )
        Add-SlPathEntries -Name "LIBPATH" -Entries @(
            (Join-Path $vcTools "ATLMFC/lib/x64"),
            (Join-Path $vcTools "lib/x64")
        )
    }

    if ($null -ne $sdkInclude) {
        Add-SlPathEntries -Name "INCLUDE" -Entries @(
            (Join-Path $sdkInclude "ucrt"),
            (Join-Path $sdkInclude "shared"),
            (Join-Path $sdkInclude "um"),
            (Join-Path $sdkInclude "winrt"),
            (Join-Path $sdkInclude "cppwinrt")
        )
    }

    if ($null -ne $sdkLib) {
        Add-SlPathEntries -Name "LIB" -Entries @(
            (Join-Path $sdkLib "ucrt/x64"),
            (Join-Path $sdkLib "um/x64")
        )
        Add-SlPathEntries -Name "LIBPATH" -Entries @(
            (Join-Path (Split-Path -Parent $sdkIncludeRoot) "UnionMetadata/$(Split-Path -Leaf $sdkInclude)"),
            (Join-Path (Split-Path -Parent $sdkIncludeRoot) "References/$(Split-Path -Leaf $sdkInclude)")
        )
    }

    if (-not (Test-SlEnvFile -EnvValue $env:INCLUDE -FileName "stdio.h")) {
        throw "MSVC/Windows SDK INCLUDE paths are incomplete; stdio.h was not found."
    }

    if (-not (Test-SlEnvFile -EnvValue $env:LIB -FileName "kernel32.lib")) {
        throw "Windows SDK LIB paths are incomplete; kernel32.lib was not found."
    }

    if (-not ((Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrt.lib") -or
            (Test-SlEnvFile -EnvValue $env:LIB -FileName "msvcrtd.lib")))
    {
        throw "MSVC runtime LIB paths are incomplete; msvcrt/msvcrtd.lib was not found."
    }
}
