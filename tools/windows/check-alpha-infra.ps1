param(
    [switch]$SelfTest
)

$ErrorActionPreference = "Stop"

. (Join-Path $PSScriptRoot "v8-sdk.ps1")

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$ManifestPath = Join-Path $Root "tools/deps/sloppy-deps.json"
$DogfoodManifestPath = Join-Path $Root "examples/dogfood/alpha-dogfood.json"
$ReadinessManifestPath = Join-Path $Root "docs/project/alpha-infra-readiness.json"

function Read-JsonFile {
    param([string]$Path)

    try {
        return Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json
    } catch {
        throw "Invalid JSON in ${Path}: $($_.Exception.Message)"
    }
}

function Assert-True {
    param(
        [bool]$Condition,
        [string]$Message
    )

    if (-not $Condition) {
        throw $Message
    }
}

function Assert-InSet {
    param(
        [string]$Value,
        [string[]]$Allowed,
        [string]$Message
    )

    if (-not ($Allowed -contains $Value)) {
        throw "$Message Value '$Value' is not one of: $($Allowed -join ', ')"
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

function Write-FakeV8Manifest {
    param(
        [string]$Root,
        [string]$Platform = "windows-x64",
        [string]$V8Revision = $SlV8ExpectedRevision
    )

    New-Item -ItemType Directory -Force -Path (Join-Path $Root "share") | Out-Null
    [ordered]@{
        name = "sloppy-v8-sdk"
        platform = $Platform
        v8Revision = $V8Revision
        buildType = "release"
        crtCompatibility = "Release or RelWithDebInfo"
        abi = [ordered]@{
            crLibcxxRevision = $SlV8ExpectedCrLibcxxRevision
            v8TargetArch = "x64"
            v8CompressPointers = $true
            v8CompressPointersInSharedCage = $true
            v8_31BitSmisOn64BitArch = $true
            v8EnableSandbox = $true
        }
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $Root "share/sloppy-v8-sdk.json") -Encoding ASCII
}

function New-FakeV8Sdk {
    param([string]$Root)

    New-Item -ItemType Directory -Force -Path (Join-Path $Root "include/libplatform") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "lib") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "support/libcxx/include") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $Root "support/libcxx/buildtools") | Out-Null
    Set-Content -LiteralPath (Join-Path $Root "include/v8.h") -Value "/* fixture */" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "include/libplatform/libplatform.h") -Value "/* fixture */" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "lib/v8_monolith_fixture.lib") -Value "" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "lib/v8_libplatform_fixture.lib") -Value "" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "lib/v8_libbase_fixture.lib") -Value "" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "lib/libc++_fixture.lib") -Value "" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "support/libcxx/include/memory") -Value "/* fixture */" -Encoding ASCII
    Set-Content -LiteralPath (Join-Path $Root "support/libcxx/buildtools/__config_site") -Value "/* fixture */" -Encoding ASCII
    Write-FakeV8Manifest -Root $Root
}

function Invoke-SelfTest {
    Assert-True (Test-SlVersionAtLeast -Actual "3.25.1" -Minimum "3.25.0") "Version parser failed greater-than fixture."
    Assert-True (Test-SlVersionAtLeast -Actual "3.25.0" -Minimum "3.25.0") "Version parser failed equality fixture."
    Assert-True (-not (Test-SlVersionAtLeast -Actual "3.24.9" -Minimum "3.25.0")) "Version parser accepted an old version."
    Assert-True (-not (Test-SlVersionAtLeast -Actual "not-a-version" -Minimum "3.25.0")) "Version parser accepted malformed version text."

    $dogfoodFixture = [pscustomobject]@{
        schemaVersion = 1
        statusVocabulary = @("available", "v8-gated", "package-gated", "live-provider-gated", "blocked", "unavailable", "planned")
        scenarios = @(
            [pscustomobject]@{ id = "hello-artifact"; status = "v8-gated"; reason = "requires V8" },
            [pscustomobject]@{ id = "hello-source-input"; status = "v8-gated"; reason = "requires V8" },
            [pscustomobject]@{ id = "package-hello-artifact"; status = "package-gated"; reason = "requires package" },
            [pscustomobject]@{ id = "http-app"; status = "blocked"; reason = "owned by HTTP track" },
            [pscustomobject]@{ id = "https-app"; status = "blocked"; reason = "owned by TLS track" },
            [pscustomobject]@{ id = "sqlite-app"; status = "v8-gated"; reason = "requires V8 lane" },
            [pscustomobject]@{ id = "postgresql-app"; status = "live-provider-gated"; reason = "requires live service" },
            [pscustomobject]@{ id = "sqlserver-app"; status = "live-provider-gated"; reason = "requires live service" },
            [pscustomobject]@{ id = "framework-v2-app"; status = "blocked"; reason = "owned by framework track" }
        )
    }
    Test-DogfoodManifestObject -Manifest $dogfoodFixture

    $readinessFixture = [pscustomobject]@{
        schemaVersion = 1
        parentIssue = 873
        consumerIssues = @(300, 681, 685, 684, 301)
        completedIssues = @(874, 875, 876, 877, 878, 879, 880, 881, 882, 883)
        deferredIssues = @()
        evidenceLanes = @(
            [pscustomobject]@{ id = "windows-x64"; status = "supported"; evidence = "fixture" },
            [pscustomobject]@{ id = "v8"; status = "deferred"; evidence = "fixture" },
            [pscustomobject]@{ id = "package"; status = "experimental"; evidence = "fixture" },
            [pscustomobject]@{ id = "outside-checkout-package"; status = "experimental"; evidence = "fixture" },
            [pscustomobject]@{ id = "dogfood-source"; status = "experimental"; evidence = "fixture" },
            [pscustomobject]@{ id = "dogfood-package"; status = "skipped"; evidence = "fixture" }
        )
        dogfoodCatalog = "examples/dogfood/alpha-dogfood.json"
    }
    Test-ReadinessManifestObject -Manifest $readinessFixture

    $hygieneRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-alpha-infra-hygiene-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $hygieneRoot | Out-Null
    try {
        $hygieneFixture = Join-Path $hygieneRoot "paths.txt"
        @(
            ('sdk=' + 'D:' + '\deps\v8'),
            ('\\' + 'buildshare\deps\v8'),
            ('/' + 'Users/example/v8'),
            ('~' + '/sloppy-sdk'),
            './tools/windows/bootstrap.ps1'
        ) | Set-Content -LiteralPath $hygieneFixture -Encoding ASCII
        $hygieneMatches = Get-LocalPathHygieneMatches -Path $hygieneFixture -Relative "paths.txt"
        Assert-True ($hygieneMatches.Count -eq 4) "Local path hygiene scanner missed absolute path fixtures."
    } finally {
        Remove-Item -LiteralPath $hygieneRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    $tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-alpha-infra-v8-" + [System.Guid]::NewGuid().ToString("N"))
    New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
    $oldV8Root = $env:SLOPPY_V8_ROOT
    $oldV8Hints = $env:SLOPPY_V8_SDK_HINTS
    $env:SLOPPY_V8_ROOT = ""
    $env:SLOPPY_V8_SDK_HINTS = ""
    try {
        $validRoot = Join-Path $tempRoot "valid"
        New-FakeV8Sdk -Root $validRoot
        Assert-True (Test-SlV8SdkLayout -Root $validRoot -Quiet) "Valid V8 fixture did not pass layout validation."

        $missingRoot = Join-Path $tempRoot "missing"
        Assert-True (-not (Test-SlV8SdkLayout -Root $missingRoot -Quiet)) "Missing V8 fixture unexpectedly passed."

        $wrongArchRoot = Join-Path $tempRoot "wrong-arch"
        New-FakeV8Sdk -Root $wrongArchRoot
        Write-FakeV8Manifest -Root $wrongArchRoot -Platform "linux-x64"
        Assert-True (-not (Test-SlV8SdkLayout -Root $wrongArchRoot -Quiet)) "Wrong-platform V8 fixture unexpectedly passed."

        $corruptRoot = Join-Path $tempRoot "corrupt"
        New-FakeV8Sdk -Root $corruptRoot
        Set-Content -LiteralPath (Join-Path $corruptRoot "share/sloppy-v8-sdk.json") -Value "{ not json" -Encoding ASCII
        Assert-True (-not (Test-SlV8SdkLayout -Root $corruptRoot -Quiet)) "Corrupt-manifest V8 fixture unexpectedly passed."

        $off = Resolve-SlV8SdkRootForMode -RepoRoot $tempRoot -Mode OFF
        Assert-True ([string]::IsNullOrWhiteSpace($off.Root)) "V8 OFF mode returned a root."

        $auto = Resolve-SlV8SdkRootForMode -RepoRoot $tempRoot -Mode AUTO -SearchRoots @($missingRoot)
        Assert-True ([string]::IsNullOrWhiteSpace($auto.Root)) "V8 AUTO mode should tolerate missing SDK."

        $requiredMissingFailed = $false
        try {
            Resolve-SlV8SdkRootForMode -RepoRoot $tempRoot -Mode REQUIRED -SearchRoots @($missingRoot) | Out-Null
        } catch {
            $requiredMissingFailed = $true
        }
        Assert-True $requiredMissingFailed "V8 REQUIRED mode should fail for a missing SDK."

        $required = Resolve-SlV8SdkRootForMode -RepoRoot $tempRoot -Mode REQUIRED -SearchRoots @($validRoot)
        Assert-True (-not [string]::IsNullOrWhiteSpace($required.Root)) "V8 REQUIRED mode did not resolve a valid SDK."

        $archiveStage = Join-Path $tempRoot "archive-stage"
        $archiveSdkRoot = Join-Path $archiveStage "windows-x64"
        New-FakeV8Sdk -Root $archiveSdkRoot
        $archivePath = Join-Path $tempRoot "sloppy-v8-sdk-fixture.zip"
        Compress-Archive -Path $archiveSdkRoot -DestinationPath $archivePath -Force
        $archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
        $fetchDestination = Join-Path $tempRoot "fetch-destination"
        $fetchScript = Join-Path $PSScriptRoot "fetch-v8.ps1"
        & powershell -NoProfile -ExecutionPolicy Bypass -File $fetchScript -Destination $fetchDestination -SourceArchive $archivePath -Sha256 $archiveHash -Force | Out-Null
        Assert-True ($LASTEXITCODE -eq 0) "V8 artifact fetch fixture failed."
        Assert-True (Test-SlV8SdkLayout -Root (Join-Path $fetchDestination "windows-x64") -Quiet) "Fetched V8 fixture did not validate."

        $resolveScript = Join-Path $PSScriptRoot "resolve-v8-sdk.ps1"
        $badExplicitRoot = Start-Process -FilePath "powershell" -ArgumentList @(
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            $resolveScript,
            "-V8Root",
            $missingRoot,
            "-Fetch",
            "-Quiet"
        ) -Wait -PassThru -WindowStyle Hidden
        Assert-True ($badExplicitRoot.ExitCode -ne 0) "V8 resolver should not fetch around an explicit invalid -V8Root."

        $badFetch = Start-Process -FilePath "powershell" -ArgumentList @(
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-File",
            $fetchScript,
            "-Destination",
            (Join-Path $tempRoot "bad-fetch"),
            "-SourceArchive",
            $archivePath,
            "-Sha256",
            ("0" * 64),
            "-Force"
        ) -Wait -PassThru -WindowStyle Hidden
        Assert-True ($badFetch.ExitCode -ne 0) "V8 artifact fetch fixture should fail for wrong checksum."
    } finally {
        $env:SLOPPY_V8_ROOT = $oldV8Root
        $env:SLOPPY_V8_SDK_HINTS = $oldV8Hints
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    }

    Write-Host "alpha infra self-test passed."
}

function Test-Manifest {
    $manifest = Read-JsonFile -Path $ManifestPath
    Assert-True ($manifest.schemaVersion -eq 1) "Dependency manifest schemaVersion must be 1."
    Assert-True ($manifest.statusVocabulary.Count -gt 0) "Dependency manifest must define dependency statuses."
    Assert-True ($manifest.platformStatusVocabulary.Count -gt 0) "Dependency manifest must define platform statuses."

    foreach ($platformName in @("windows-x64", "linux-x64", "macos-arm64", "macos-x64", "windows-arm64")) {
        $platform = $manifest.platforms.PSObject.Properties[$platformName].Value
        Assert-True ($null -ne $platform) "Dependency manifest missing platform '$platformName'."
        Assert-InSet -Value ([string]$platform.status) -Allowed @($manifest.platformStatusVocabulary) -Message "Invalid status for platform '$platformName'."
    }

    foreach ($tool in @("cmake", "ninja", "git", "vcpkg", "rust")) {
        $policy = $manifest.toolchainPolicy.PSObject.Properties[$tool].Value
        Assert-True ($null -ne $policy) "Dependency manifest missing toolchain policy '$tool'."
        Assert-True ([bool]$policy.required) "Toolchain '$tool' must be required."
        if ($tool -ne "vcpkg") {
            Assert-True (-not [string]::IsNullOrWhiteSpace([string]$policy.minimumVersion)) "Toolchain '$tool' must name minimumVersion."
        }
    }

    Assert-True ($manifest.v8Sdk.name -eq "sloppy-v8-sdk") "V8 SDK manifest policy must name sloppy-v8-sdk."
    Assert-True ($manifest.v8Sdk.platforms.PSObject.Properties["windows-x64"].Value.status -eq "supported") "V8 windows-x64 SDK status must be supported."
    Assert-True ($manifest.v8Sdk.artifactSource.status -eq "active") "V8 artifact source must be active for Windows x64."
    Assert-True ($manifest.v8Sdk.artifactSource.downloadImplemented -eq $true) "V8 artifact download must be implemented."
    $windowsV8Artifact = $manifest.v8Sdk.artifactSource.platforms.PSObject.Properties["windows-x64"].Value
    Assert-True ($windowsV8Artifact.status -eq "available") "V8 windows-x64 artifact source must be available."
    Assert-True ([string]$windowsV8Artifact.url -match '^https://github.com/RtlZeroMemory/Slop/releases/download/') "V8 windows-x64 artifact URL must be a GitHub release asset."
    Assert-True ([string]$windowsV8Artifact.sha256 -match '^[0-9a-f]{64}$') "V8 windows-x64 artifact must name a SHA-256 checksum."
    foreach ($mode in @("OFF", "AUTO", "REQUIRED")) {
        Assert-True (@($manifest.features.v8.supportedModes) -contains $mode) "V8 supportedModes must include $mode."
    }
}

function Get-LocalPathHygieneMatches {
    param(
        [string]$Path,
        [string]$Relative
    )

    $localPathPatterns = @(
        '(?i)[A-Z]:[\\/]',
        '\\\\[^\\\s"<>|]+\\[^\\\s"<>|]+',
        '//[^/\s"<>|]+/[^/\s"<>|]+',
        '(^|[\s"=:,(])~(/|$)',
        '(^|[\s"=:,(])/(Users|home|Volumes|opt|var|tmp|private|mnt|workspace|workspaces)(/|$)'
    )
    $allowlistPaths = @(
        '//[^/\s"<>|]+/[^/\s"<>|]+',
        '~[\\/]\.ccache',
        '\\r\?\\n',
        'vcpkg[\\/]+scripts[\\/]buildsystems[\\/]vcpkg\.cmake'
    )
    $bad = New-Object System.Collections.Generic.List[string]
    $foundMatches = Select-String -LiteralPath $Path -Pattern $localPathPatterns -AllMatches
    foreach ($match in $foundMatches) {
        $line = $match.Line.Trim()
        $allowed = $false
        $lineWithoutUrls = $line -replace 'https?://\S+', ''
        $hasLocalPathOutsideUrl = $false
        foreach ($localPathPattern in $localPathPatterns) {
            if ($lineWithoutUrls -match $localPathPattern) {
                $hasLocalPathOutsideUrl = $true
                break
            }
        }
        if (-not $hasLocalPathOutsideUrl) {
            $allowed = $true
        }
        foreach ($allowlistPath in $allowlistPaths) {
            if ($lineWithoutUrls.Contains($allowlistPath) -or $lineWithoutUrls -match $allowlistPath) {
                $allowed = $true
                break
            }
        }
        if (-not $allowed) {
            $bad.Add("${Relative}:$($match.LineNumber): $line")
        }
    }
    return @($bad)
}

function Test-LocalPathHygiene {
    $paths = @(
        "tools/deps/sloppy-deps.json",
        "tools/windows/deps-doctor.ps1",
        "tools/windows/check-alpha-infra.ps1",
        "tools/windows/v8-sdk.ps1",
        "tools/windows/fetch-v8.ps1",
        "tools/windows/resolve-v8-sdk.ps1",
        "tools/windows/bootstrap.ps1",
        "tools/windows/dev.ps1",
        "tools/windows/package.ps1",
        "tools/windows/test-package.ps1",
        "tools/windows/dogfood.ps1",
        "tools/windows/check-alpha-claims.ps1",
        "tools/windows/check-release-artifacts.ps1",
        "tools/windows/release-dry-run.ps1",
        "tools/unix/bootstrap.sh",
        "tools/unix/dev.sh",
        "tools/unix/package.sh",
        "tools/unix/test-package.sh",
        "tools/unix/dogfood.sh",
        "tools/unix/release-dry-run.sh",
        "examples/dogfood/README.md",
        "examples/dogfood/alpha-dogfood.json",
        "docs/dependencies.md",
        "docs/build-and-distribution.md",
        "docs/project/alpha-infra-readiness.md",
        "docs/project/alpha-infra-readiness.json",
        "docs/release/README.md",
        "docs/release/KNOWN_LIMITATIONS.md",
        "docs/release/LICENSES.md",
        "docs/release/NOTICE.md",
        "RELEASE_NOTES.md",
        "CHANGELOG.md",
        ".github/workflows/release-artifacts.yml"
    )

    $bad = New-Object System.Collections.Generic.List[string]
    foreach ($relative in $paths) {
        $path = Join-Path $Root $relative
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            $bad.Add("${relative}: missing required guardrail file")
            continue
        }
        $foundMatches = Get-LocalPathHygieneMatches -Path $path -Relative $relative
        foreach ($match in $foundMatches) {
            $bad.Add($match)
        }
    }

    if ($bad.Count -gt 0) {
        throw "Alpha infra local absolute path hygiene failed:`n$($bad -join [Environment]::NewLine)"
    }
}

function Invoke-ProcessCheck {
    param(
        [string]$File,
        [string[]]$Arguments,
        [int[]]$AllowedExitCodes,
        [string]$OutputMustContain
    )

    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $File
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true
    $startInfo.UseShellExecute = $false
    $quotedArguments = foreach ($argument in $Arguments) {
        '"' + ($argument -replace '"', '\"') + '"'
    }
    $startInfo.Arguments = ($quotedArguments -join " ")

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) {
            throw "Failed to start $File."
        }
        $stdout = $process.StandardOutput.ReadToEnd()
        $stderr = $process.StandardError.ReadToEnd()
        $process.WaitForExit()
        $combined = $stdout + $stderr
        if (-not ($AllowedExitCodes -contains $process.ExitCode)) {
            throw "$File exited with $($process.ExitCode), expected one of $($AllowedExitCodes -join ', '): $combined"
        }
        if (-not [string]::IsNullOrWhiteSpace($OutputMustContain) -and
            -not $combined.Contains($OutputMustContain))
        {
            throw "$File output did not contain '$OutputMustContain'. Output: $combined"
        }
    } finally {
        $process.Dispose()
    }
}

function Test-DevCommandContract {
    $devScript = Join-Path $Root "tools/windows/dev.ps1"
    Invoke-ProcessCheck -File "powershell" -Arguments @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $devScript,
        "help"
    ) -AllowedExitCodes @(0) -OutputMustContain "test-package"

    Invoke-ProcessCheck -File "powershell" -Arguments @(
        "-NoProfile",
        "-ExecutionPolicy",
        "Bypass",
        "-File",
        $devScript,
        "definitely-not-a-command"
    ) -AllowedExitCodes @(2) -OutputMustContain "unknown command"

    $bootstrapPath = Join-Path $Root "tools/unix/bootstrap.sh"
    Assert-True (Test-Path -LiteralPath $bootstrapPath -PathType Leaf) "Unix bootstrap script is missing."
    $bootstrapText = Get-Content -LiteralPath $bootstrapPath -Raw
    Assert-True $bootstrapText.Contains("Usage: tools/unix/bootstrap.sh") "Unix bootstrap help contract is missing."

    $devPath = Join-Path $Root "tools/unix/dev.sh"
    Assert-True (Test-Path -LiteralPath $devPath -PathType Leaf) "Unix dev script is missing."
    $devText = Get-Content -LiteralPath $devPath -Raw
    foreach ($command in @("doctor", "configure", "build", "test", "lint", "format-check", "package", "test-package", "dogfood")) {
        Assert-True $devText.Contains($command) "Unix dev command contract is missing '$command'."
    }
}

function Test-DogfoodManifestObject {
    param([object]$Manifest)

    Assert-True ($Manifest.schemaVersion -eq 1) "Dogfood catalog schemaVersion must be 1."
    $statuses = @($Manifest.statusVocabulary)
    foreach ($status in @("available", "v8-gated", "package-gated", "live-provider-gated", "blocked", "unavailable", "planned")) {
        Assert-True ($statuses -contains $status) "Dogfood catalog statusVocabulary missing '$status'."
    }

    $ids = New-Object System.Collections.Generic.HashSet[string]
    foreach ($scenario in @($Manifest.scenarios)) {
        $id = [string]$scenario.id
        Assert-True (-not [string]::IsNullOrWhiteSpace($id)) "Dogfood scenario missing id."
        Assert-True ($ids.Add($id)) "Dogfood scenario id duplicated: $id"
        Assert-True ($statuses -contains [string]$scenario.status) "Dogfood scenario '$id' has invalid status '$($scenario.status)'."
        if ([string]$scenario.status -in @("blocked", "unavailable", "live-provider-gated")) {
            Assert-True (-not [string]::IsNullOrWhiteSpace([string]$scenario.reason)) "Dogfood scenario '$id' must explain blocked/unavailable/live-provider status."
        }
    }

    foreach ($requiredId in @("hello-artifact", "hello-source-input", "package-hello-artifact", "http-app", "https-app", "sqlite-app", "postgresql-app", "sqlserver-app", "framework-v2-app")) {
        Assert-True ($ids.Contains($requiredId)) "Dogfood catalog missing required scenario '$requiredId'."
    }
}

function Test-DogfoodManifest {
    $manifest = Read-JsonFile -Path $DogfoodManifestPath
    Test-DogfoodManifestObject -Manifest $manifest

    foreach ($scenario in @($manifest.scenarios)) {
        foreach ($property in @("source", "artifactFixture", "sourceFixture", "example", "metadataFixture")) {
            $value = [string]$scenario.PSObject.Properties[$property].Value
            if ([string]::IsNullOrWhiteSpace($value)) {
                continue
            }
            $path = Join-Path $Root $value
            Assert-True (Test-Path -LiteralPath $path) "Dogfood scenario '$($scenario.id)' references missing path '$value'."
        }
    }
}

function Test-ReadinessManifestObject {
    param([object]$Manifest)

    Assert-True ($Manifest.schemaVersion -eq 1) "Readiness manifest schemaVersion must be 1."
    Assert-True ($Manifest.parentIssue -eq 873) "Readiness manifest must point to parent issue #873."
    foreach ($issue in @(300, 681, 685, 684, 301)) {
        Assert-True (@($Manifest.consumerIssues) -contains $issue) "Readiness manifest missing consumer issue #$issue."
    }
    foreach ($issue in @(874, 875, 876, 877, 878, 879, 880, 881, 882, 883)) {
        Assert-True (@($Manifest.completedIssues) -contains $issue) "Readiness manifest missing completed issue #$issue."
    }
    $deferred = @($Manifest.deferredIssues | Where-Object { $_.issue -eq 876 })
    Assert-True ($deferred.Count -eq 0) "Readiness manifest must not keep #876 deferred after Windows x64 V8 artifact source lands."
    Assert-True (-not [string]::IsNullOrWhiteSpace([string]$Manifest.dogfoodCatalog)) "Readiness manifest must link dogfoodCatalog."

    $laneIds = @($Manifest.evidenceLanes | ForEach-Object { [string]$_.id })
    foreach ($lane in @("windows-x64", "v8", "package", "outside-checkout-package", "dogfood-source", "dogfood-package")) {
        Assert-True ($laneIds -contains $lane) "Readiness manifest missing evidence lane '$lane'."
    }
}

function Test-ReadinessManifest {
    $manifest = Read-JsonFile -Path $ReadinessManifestPath
    Test-ReadinessManifestObject -Manifest $manifest

    $dogfoodCatalog = Join-Path $Root ([string]$manifest.dogfoodCatalog)
    Assert-True (Test-Path -LiteralPath $dogfoodCatalog -PathType Leaf) "Readiness dogfoodCatalog path is missing: $($manifest.dogfoodCatalog)"
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

Test-Manifest
Test-DogfoodManifest
Test-ReadinessManifest
Test-LocalPathHygiene
Test-DevCommandContract
Write-Host "alpha infra manifest and hygiene checks passed."
exit 0
