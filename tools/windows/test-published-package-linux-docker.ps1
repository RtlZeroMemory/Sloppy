param(
    [string[]]$Images = @("node:22-bullseye", "node:22-bookworm", "node:22-trixie", "fedora:latest"),
    [string[]]$SkipImages = @("node:22-alpine"),
    [string]$PackageSpec = "@rtlzeromemory/sloppy@alpha",
    [string]$ReportRoot = ""
)

$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($ReportRoot)) {
    $ReportRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("sloppy-linux-docker-published-" + [System.Guid]::NewGuid().ToString("N"))
}

New-Item -ItemType Directory -Force -Path $ReportRoot | Out-Null

$summaryPath = Join-Path $ReportRoot "summary.json"
$aggregatePath = Join-Path $ReportRoot "summaries.ndjson"
Set-Content -LiteralPath $aggregatePath -Value "" -Encoding UTF8

$script = @'
set -uo pipefail

PACKAGE_SPEC="${PACKAGE_SPEC:-@rtlzeromemory/sloppy@alpha}"
ROOT="$(mktemp -d)"
RESULTS="/work/results.ndjson"
: > "$RESULTS"

if ! command -v node >/dev/null 2>&1 || ! command -v npm >/dev/null 2>&1; then
  if command -v dnf >/dev/null 2>&1; then
    dnf install -y nodejs npm
  elif command -v apt-get >/dev/null 2>&1; then
    export DEBIAN_FRONTEND=noninteractive
    apt-get update
    apt-get install -y --no-install-recommends ca-certificates nodejs npm
  else
    echo "No supported package manager found for installing Node/npm." >&2
    exit 1
  fi
fi

json_escape() {
  node -e 'process.stdout.write(JSON.stringify(process.argv[1]).slice(1,-1))' "$1"
}

record() {
  flow="$1"
  status="$2"
  notes="$3"
  printf '{"flow":"%s","status":"%s","notes":"%s"}\n' \
    "$(json_escape "$flow")" \
    "$(json_escape "$status")" \
    "$(json_escape "$notes")" >> "$RESULTS"
}

run_log() {
  name="$1"
  shift
  echo "### $name"
  "$@"
}

expect_contains() {
  name="$1"
  needle="$2"
  shift 2
  output="$("$@" 2>&1)"
  status=$?
  printf '%s\n' "$output"
  if [ "$status" -ne 0 ]; then
    record "$name" "FAIL" "exit $status"
    return 1
  fi
  if ! printf '%s\n' "$output" | grep -F "$needle" >/dev/null; then
    record "$name" "FAIL" "missing expected text: $needle"
    return 1
  fi
  record "$name" "PASS" "matched: $needle"
  return 0
}

overall=0

echo "node=$(node --version)"
echo "npm=$(npm --version)"
echo "kernel=$(uname -a)"
node -p "process.platform + ' ' + process.arch"

mkdir -p "$ROOT/install"
cd "$ROOT/install" || exit 1
npm init -y
if npm install "$PACKAGE_SPEC" --registry https://registry.npmjs.org/ --prefer-online --cache "$ROOT/npm-cache"; then
  pkg_json="$(node -e "const p=require('./node_modules/@rtlzeromemory/sloppy/package.json'); console.log(JSON.stringify({name:p.name,version:p.version,optionalDependencies:p.optionalDependencies,bin:p.bin}))")"
  echo "$pkg_json"
  find node_modules/@rtlzeromemory -maxdepth 4 -name package.json -print
  record "install" "PASS" "$pkg_json"
else
  record "install" "FAIL" "npm install failed"
  overall=1
fi

if [ "$overall" -eq 0 ]; then
  expect_contains "cli.version" "Sloppy" npx --no-install sloppy --version || overall=1
  expect_contains "cli.help" "Usage:" npx --no-install sloppy --help || overall=1
  expect_contains "cli.doctor.help" "Usage:" npx --no-install sloppy doctor --help || overall=1
  expect_contains "cli.run.help" "Usage:" npx --no-install sloppy run --help || overall=1
  expect_contains "cli.package.help" "Usage:" npx --no-install sloppy package --help || overall=1
  expect_contains "cli.routes.help" "Usage:" npx --no-install sloppy routes --help || overall=1
  expect_contains "cli.capabilities.help" "Usage:" npx --no-install sloppy capabilities --help || overall=1
  expect_contains "cli.deps.help" "Usage:" npx --no-install sloppy deps --help || overall=1
  expect_contains "cli.audit.help" "Usage:" npx --no-install sloppy audit --help || overall=1
  expect_contains "cli.db.help" "Usage:" npx --no-install sloppy db --help || overall=1
fi

if [ "$overall" -eq 0 ]; then
  cd "$ROOT" || exit 1
  if npx --prefix "$ROOT/install" sloppy create smoke-minimal --template minimal-api &&
     cd smoke-minimal &&
     npx --prefix "$ROOT/install" sloppy build &&
     expect_contains "minimal.health" "HTTP/1.1 200" npx --prefix "$ROOT/install" sloppy run .sloppy --once GET /health &&
     expect_contains "minimal.hello" '{"hello":"Ada"}' npx --prefix "$ROOT/install" sloppy run .sloppy --once GET /hello/Ada &&
     npx --prefix "$ROOT/install" sloppy package --format json; then
    outside="$(mktemp -d)"
    cp -R .sloppy/package "$outside/package"
    test ! -d "$outside/package/node_modules" || { record "minimal.package.no_node_modules" "FAIL" "node_modules exists"; overall=1; }
    expect_contains "minimal.package.roundtrip" '{"hello":"Ada"}' npx --prefix "$ROOT/install" sloppy run "$outside/package" --once GET /hello/Ada || overall=1
    record "minimal" "PASS" "build, run, package roundtrip"
  else
    record "minimal" "FAIL" "minimal template flow failed"
    overall=1
  fi
fi

if [ "$overall" -eq 0 ]; then
  cd "$ROOT" || exit 1
  if npx --prefix "$ROOT/install" sloppy create smoke-api &&
     cd smoke-api &&
     npx --prefix "$ROOT/install" sloppy build &&
     npx --prefix "$ROOT/install" sloppy db migrate .sloppy --provider main; then
    expect_contains "api.health" "HTTP/1.1 200" npx --prefix "$ROOT/install" sloppy run .sloppy --once GET /health || overall=1
    expect_contains "api.json.valid" "HTTP/1.1 201" npx --prefix "$ROOT/install" sloppy run .sloppy --once POST /users --json '{"name":"Linus Torvalds","email":"linus@example.test"}' || overall=1
    expect_contains "api.json.invalid" "HTTP/1.1 400" npx --prefix "$ROOT/install" sloppy run .sloppy --once POST /users --json '{"name":"","email":"bad"}' || overall=1
    expect_contains "api.body.malformed_generic" "HTTP/1.1 415" npx --prefix "$ROOT/install" sloppy run .sloppy --once POST /users --body '{bad json' || overall=1
    SLOPPY_ROUTE_DISPATCH=classic expect_contains "dispatch.classic" "HTTP/1.1 200" npx --prefix "$ROOT/install" sloppy run .sloppy --once GET /health || overall=1
    SLOPPY_ROUTE_DISPATCH=compiled expect_contains "dispatch.compiled" "HTTP/1.1 200" npx --prefix "$ROOT/install" sloppy run .sloppy --once GET /health || overall=1
    SLOPPY_ROUTE_DISPATCH=validate expect_contains "dispatch.validate" "HTTP/1.1 200" npx --prefix "$ROOT/install" sloppy run .sloppy --once GET /health || overall=1
    npx --prefix "$ROOT/install" sloppy package --format json || overall=1
    outside_api="$(mktemp -d)"
    cp -R .sloppy/package "$outside_api/package"
    test ! -d "$outside_api/package/node_modules" || { record "api.package.no_node_modules" "FAIL" "node_modules exists"; overall=1; }
    expect_contains "api.package.health" "HTTP/1.1 200" npx --prefix "$ROOT/install" sloppy run "$outside_api/package" --once GET /health || overall=1
    expect_contains "api.package.static" "Hello from the Sloppy API template." npx --prefix "$ROOT/install" sloppy run "$outside_api/package" --once GET /public/hello.txt || overall=1
    users_output="$(npx --prefix "$ROOT/install" sloppy run "$outside_api/package" --once GET /users 2>&1)"
    printf '%s\n' "$users_output"
    if printf '%s\n' "$users_output" | grep -F "HTTP/1.1 200" >/dev/null; then
      record "api.package.users" "PASS" "packaged /users returned 200"
    else
      record "api.package.users" "FAIL" "packaged /users did not return 200"
      overall=1
    fi
  else
    record "api" "FAIL" "api template setup failed"
    overall=1
  fi
fi

record "streaming" "SKIPPED" "no dedicated streaming template in this published package smoke"
record "diagnostics" "SKIPPED" "diagnostics-json/doctor-report flags were not verified in this package smoke"
record "bench" "SKIPPED" "sloppy_bench is not exposed by the npm launcher package"

node - <<'NODE'
const fs = require('fs');
const rows = fs.readFileSync('/work/results.ndjson', 'utf8').trim().split(/\n/).filter(Boolean).map(JSON.parse);
const result = rows.some(r => r.status === 'FAIL') ? 'FAIL' : 'PASS';
const summary = {
  result,
  image: process.env.IMAGE || '',
  packageSpec: process.env.PACKAGE_SPEC || '@rtlzeromemory/sloppy@alpha',
  node: process.version,
  platform: `${process.platform} ${process.arch}`,
  flows: rows,
};
fs.writeFileSync('/work/summary.json', JSON.stringify(summary, null, 2));
console.log(JSON.stringify(summary, null, 2));
NODE

exit "$overall"
'@

$script = $script -replace "`r`n", "`n"
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)

$overallExit = 0
foreach ($image in $Images) {
    $imageName = ($image -replace '[^A-Za-z0-9_.-]', '_')
    $imageRoot = Join-Path $ReportRoot $imageName
    New-Item -ItemType Directory -Force -Path $imageRoot | Out-Null
    $linuxScript = Join-Path $imageRoot "linux-smoke.sh"
    $imageSummary = Join-Path $imageRoot "summary.json"
    $logPath = Join-Path $imageRoot "docker.log"
    [System.IO.File]::WriteAllText($linuxScript, $script, $utf8NoBom)

    $envVars = @(
        "-e", "PACKAGE_SPEC=$PackageSpec",
        "-e", "IMAGE=$image"
    )

    $dockerArgs = @("run", "--rm") + $envVars + @("-v", "${imageRoot}:/work", $image, "bash", "/work/linux-smoke.sh")
    Write-Host "Running: docker $($dockerArgs -join ' ')"
    & docker @dockerArgs 2>&1 | Tee-Object -FilePath $logPath
    if ($LASTEXITCODE -ne 0) {
        $overallExit = 1
    }
    if (Test-Path -LiteralPath $imageSummary) {
        $compact = Get-Content -LiteralPath $imageSummary -Raw | ConvertFrom-Json | ConvertTo-Json -Depth 12 -Compress
        Add-Content -LiteralPath $aggregatePath -Value $compact
    }
}

foreach ($image in $SkipImages) {
    $skip = [ordered]@{
        result = "SKIPPED"
        image = $image
        packageSpec = $PackageSpec
        reason = "Alpine/musl is not covered by the glibc-only @rtlzeromemory/sloppy-linux-x64 package."
        flows = @(@{
            flow = "install"
            status = "SKIPPED"
            notes = "requires future linux-x64-musl package"
        })
    }
    Add-Content -LiteralPath $aggregatePath -Value ($skip | ConvertTo-Json -Depth 8 -Compress)
}

$summaries = @()
if (Test-Path -LiteralPath $aggregatePath) {
    $summaries = @(Get-Content -LiteralPath $aggregatePath | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | ForEach-Object { $_ | ConvertFrom-Json })
}
$result = if ($summaries | Where-Object { $_.result -eq "FAIL" }) { "FAIL" } else { "PASS" }
$final = [ordered]@{
    result = $result
    packageSpec = $PackageSpec
    images = $summaries
}
$final | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $summaryPath -Encoding UTF8

Write-Host "ReportRoot: $ReportRoot"
Write-Host "Summary: $summaryPath"
Get-Content -LiteralPath $summaryPath

exit $overallExit
