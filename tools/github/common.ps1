function Assert-GhReady {
    param([string]$Repo)

    $gh = Get-Command gh -ErrorAction SilentlyContinue
    if ($null -eq $gh) { throw "GitHub CLI 'gh' was not found. Install it, then run 'gh auth login'." }

    & gh auth status --hostname github.com *> $null
    if ($LASTEXITCODE -ne 0) { throw "GitHub CLI is not authenticated. Run 'gh auth login' and ensure access to $Repo." }
}

function Read-JsonFile {
    param([string]$Path)
    Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
}

function Invoke-GhJson {
    param([string[]]$Arguments)
    $output = & gh @Arguments
    if ($LASTEXITCODE -ne 0) { throw "gh $($Arguments -join ' ') failed with exit code $LASTEXITCODE" }
    if ([string]::IsNullOrWhiteSpace($output)) { return $null }
    $output | ConvertFrom-Json
}