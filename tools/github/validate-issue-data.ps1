param()

$ErrorActionPreference = "Stop"

$Root = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$AllowedPrSizes = @(
    "size:bounded",
    "size:large-coherent",
    "size:too-large"
)

function Add-ValidationError {
    param(
        [System.Collections.Generic.List[string]]$Errors,
        [string]$Message
    )

    $Errors.Add($Message) | Out-Null
}

function Read-RequiredJson {
    param(
        [string]$Path,
        [string]$Name,
        [System.Collections.Generic.List[string]]$Errors
    )

    try {
        return Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    } catch {
        Add-ValidationError $Errors "$Name is not valid JSON: $($_.Exception.Message)"
        return $null
    }
}

function Test-RequiredText {
    param($Value)

    return -not [string]::IsNullOrWhiteSpace([string]$Value)
}

function Get-MarkdownSection {
    param(
        [string]$Content,
        [string]$Heading
    )

    $escapedHeading = [regex]::Escape($Heading)
    $match = [regex]::Match(
        $Content,
        "(?ms)^## $escapedHeading\s*\r?\n(?<body>.*?)(?=^## |\z)"
    )

    if (-not $match.Success) {
        return $null
    }

    return $match.Groups["body"].Value.Trim()
}

function Get-MarkdownBulletValues {
    param([string]$Section)

    if ($null -eq $Section) {
        return @()
    }

    return @(
        $Section -split "\r?\n" |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -match "^- " } |
            ForEach-Object { $_ -replace "^- \s*", "" }
    )
}

$errors = [System.Collections.Generic.List[string]]::new()

$labelsPath = Join-Path $PSScriptRoot "labels.json"
$milestonesPath = Join-Path $PSScriptRoot "milestones.json"
$issuesPath = Join-Path $PSScriptRoot "issues.json"

$labels = Read-RequiredJson $labelsPath "tools/github/labels.json" $errors
$milestones = Read-RequiredJson $milestonesPath "tools/github/milestones.json" $errors
$issues = Read-RequiredJson $issuesPath "tools/github/issues.json" $errors

if ($errors.Count -gt 0) {
    $errors | ForEach-Object { Write-Error $_ }
    exit 1
}

$labelNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($label in @($labels)) {
    if (-not (Test-RequiredText $label.name)) {
        Add-ValidationError $errors "tools/github/labels.json contains a label with an empty name."
        continue
    }

    [void]$labelNames.Add([string]$label.name)
}

$milestoneNames = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($milestone in @($milestones)) {
    if (-not (Test-RequiredText $milestone.title)) {
        Add-ValidationError $errors "tools/github/milestones.json contains a milestone with an empty title."
        continue
    }

    [void]$milestoneNames.Add([string]$milestone.title)
}

$allIssues = @($issues.epics) + @($issues.tasks)
$taskCount = @($issues.tasks).Count
$epicCount = @($issues.epics).Count

foreach ($issue in $allIssues) {
    $title = if (Test-RequiredText $issue.title) { [string]$issue.title } else { "<missing title>" }

    if (-not (Test-RequiredText $issue.title)) {
        Add-ValidationError $errors "Issue is missing required field: title."
    }

    if (-not (Test-RequiredText $issue.bodyPath)) {
        Add-ValidationError $errors "Issue '$title' is missing required field: bodyPath."
    } else {
        $bodyPath = Join-Path $Root ([string]$issue.bodyPath)
        if (-not (Test-Path -LiteralPath $bodyPath -PathType Leaf)) {
            Add-ValidationError $errors "Issue '$title' bodyPath does not exist: $($issue.bodyPath)"
        }
    }

    if (-not (Test-RequiredText $issue.milestone)) {
        Add-ValidationError $errors "Issue '$title' is missing required field: milestone."
    } elseif (-not $milestoneNames.Contains([string]$issue.milestone)) {
        Add-ValidationError $errors "Issue '$title' uses unknown milestone: $($issue.milestone)"
    }

    if ($null -eq $issue.labels -or @($issue.labels).Count -eq 0) {
        Add-ValidationError $errors "Issue '$title' is missing required field: labels."
    } else {
        $seenLabels = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($labelValue in @($issue.labels)) {
            $label = [string]$labelValue
            if ([string]::IsNullOrWhiteSpace($label)) {
                Add-ValidationError $errors "Issue '$title' contains an empty label."
                continue
            }

            if ($label -eq "T") {
                Add-ValidationError $errors "Issue '$title' contains stray label 'T'."
            }

            if ($label -match "\s") {
                Add-ValidationError $errors "Issue '$title' contains label with spaces: '$label'"
            }

            if (-not $labelNames.Contains($label)) {
                Add-ValidationError $errors "Issue '$title' uses unknown label: '$label'"
            }

            if (-not $seenLabels.Add($label)) {
                Add-ValidationError $errors "Issue '$title' contains duplicate label: '$label'"
            }
        }
    }
}

foreach ($task in @($issues.tasks)) {
    $title = if (Test-RequiredText $task.title) { [string]$task.title } else { "<missing title>" }
    if (-not (Test-RequiredText $task.bodyPath)) {
        continue
    }

    $bodyPath = Join-Path $Root ([string]$task.bodyPath)
    if (-not (Test-Path -LiteralPath $bodyPath -PathType Leaf)) {
        continue
    }

    $content = Get-Content -Raw -LiteralPath $bodyPath

    $suggestedLabelsSection = Get-MarkdownSection $content "Suggested Labels"
    if ($null -eq $suggestedLabelsSection) {
        Add-ValidationError $errors "Task '$title' is missing ## Suggested Labels in $($task.bodyPath)."
    } else {
        $suggestedLabels = Get-MarkdownBulletValues $suggestedLabelsSection
        if ($suggestedLabels.Count -eq 0) {
            Add-ValidationError $errors "Task '$title' has an empty ## Suggested Labels section in $($task.bodyPath)."
        }

        $seenSuggestedLabels = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
        foreach ($label in $suggestedLabels) {
            if ([string]::IsNullOrWhiteSpace($label)) {
                Add-ValidationError $errors "Task '$title' contains an empty suggested label in $($task.bodyPath)."
                continue
            }

            if ($label -eq "T") {
                Add-ValidationError $errors "Task '$title' contains stray suggested label 'T' in $($task.bodyPath)."
            }

            if ($label -match "\s") {
                Add-ValidationError $errors "Task '$title' contains suggested label with spaces: '$label' in $($task.bodyPath)."
            }

            if (-not $labelNames.Contains($label)) {
                Add-ValidationError $errors "Task '$title' contains unknown suggested label: '$label' in $($task.bodyPath)."
            }

            if (-not $seenSuggestedLabels.Add($label)) {
                Add-ValidationError $errors "Task '$title' contains duplicate suggested label: '$label' in $($task.bodyPath)."
            }
        }
    }

    $prSizeSection = Get-MarkdownSection $content "Suggested PR Size"
    if ($null -eq $prSizeSection) {
        Add-ValidationError $errors "Task '$title' is missing ## Suggested PR Size in $($task.bodyPath)."
    } else {
        $prSize = ($prSizeSection -split "\r?\n" | ForEach-Object { $_.Trim() } | Where-Object { $_ } | Select-Object -First 1)
        if ($AllowedPrSizes -notcontains $prSize) {
            Add-ValidationError $errors "Task '$title' has invalid suggested PR size '$prSize' in $($task.bodyPath). Valid values: $($AllowedPrSizes -join ', ')."
        }
    }
}

if ($errors.Count -gt 0) {
    Write-Host "GitHub issue metadata validation failed:" -ForegroundColor Red
    foreach ($errorMessage in $errors) {
        Write-Host "  - $errorMessage" -ForegroundColor Red
    }
    exit 1
}

Write-Host "GitHub issue metadata validation passed."
Write-Host "  Labels: $($labelNames.Count)"
Write-Host "  Milestones: $($milestoneNames.Count)"
Write-Host "  EPIC issues: $epicCount"
Write-Host "  Task issues: $taskCount"
