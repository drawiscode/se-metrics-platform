<#
.SYNOPSIS
    DevInsight test runner - start backend & run integration tests
.DESCRIPTION
    1. Start backend executable
    2. Wait for service ready
    3. Run API integration tests
    4. Stop backend process
    5. Print test results
.EXAMPLE
    .\run_tests.ps1
    .\run_tests.ps1 -BackendExe "..\build\Release\devinsight_backend.exe" -NoCleanup
#>

param(
    [string]$BackendExe = "backend\build\Release\devinsight_backend.exe",
    [string]$TestRepo = "octocat/Hello-World",
    [switch]$NoCleanup,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$ProjectRoot = Resolve-Path (Join-Path $ScriptDir "..\..\..")   # scripts -> test -> backend -> project root
$BackendPath = Resolve-Path (Join-Path $ProjectRoot $BackendExe)
$TestDataDir  = Join-Path $ScriptDir "..\data"

Write-Host "=== DevInsight Test Runner ===" -ForegroundColor Magenta
Write-Host "Backend : $BackendPath"
Write-Host "TestRepo: $TestRepo"
Write-Host "WorkDir : $ProjectRoot"
Write-Host ""

# ---- 1. Prepare test env & start backend ----
Write-Host "[1/4] Starting backend..." -ForegroundColor Cyan

# Create test data directory
$TestDataDir = Join-Path $ScriptDir "..\data"
New-Item -ItemType Directory -Force -Path $TestDataDir | Out-Null

# Kill any leftover backend process from previous runs
Get-Process -Name "devinsight_backend" -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 1

# Set env vars to override config.env defaults
# NOTE: Working directory = backend exe dir (no config.env nearby),
# so these env vars are NOT overridden by config.env
$testDb = Join-Path $TestDataDir "test_devinsight.db"
$env:DEVINSIGHT_DB = $testDb
$env:PORT = "18080"

# Load GITHUB_TOKEN and LLM_API_KEY from backend/config/config.env so backend can use them
$configPath = Join-Path $ProjectRoot "backend\config\config.env"
if (Test-Path $configPath) {
    Get-Content $configPath | ForEach-Object {
        $line = $_.Trim()
        if ($line -and $line[0] -ne '#') {
            $eq = $line.IndexOf('=')
            if ($eq -gt 0) {
                $key = $line.Substring(0, $eq).Trim()
                $val = $line.Substring($eq + 1).Trim()
                if ($key -in @('GITHUB_TOKEN','LLM_API_KEY','LLM_API_BASE','LLM_MODEL')) {
                    Set-Item -Path "env:$key" -Value $val -ErrorAction SilentlyContinue
                }
            }
        }
    }
    Write-Host "       loaded config from $configPath" -ForegroundColor DarkGray
}

# clean old test db
if (Test-Path $testDb) {
    Remove-Item $testDb -Force
    Write-Host "       cleaned old test db" -ForegroundColor DarkGray
}

$logOut = Join-Path $TestDataDir "backend_test.log"
$logErr = Join-Path $TestDataDir "backend_test_err.log"

$BackendDir = Split-Path $BackendPath -Parent   # e.g. ...\backend\build\Release
$backendProcess = Start-Process -FilePath $BackendPath -WorkingDirectory $BackendDir -NoNewWindow -PassThru -RedirectStandardOutput $logOut -RedirectStandardError $logErr
$global:BackendPid = $backendProcess.Id
Write-Host "       PID: $($backendProcess.Id)"

# ---- 2. Wait for ready ----
Write-Host "[2/4] Waiting for backend..." -ForegroundColor Cyan
$baseUrl = "http://127.0.0.1:18080"
$ready = $false
for ($i = 0; $i -lt 30; $i++) {
    try {
        $resp = Invoke-RestMethod -Method Get -Uri "$baseUrl/api/health" -TimeoutSec 2 -ErrorAction Stop
        if ($resp.ok) {
            $ready = $true
            break
        }
    } catch {
        Start-Sleep -Seconds 1
    }
}

if (-not $ready) {
    Write-Host "  [ERROR] Backend did not start in 30s" -ForegroundColor Red
    if (-not $NoCleanup) { Stop-Process -Id $backendProcess.Id -Force -ErrorAction SilentlyContinue }
    exit 1
}
Write-Host "       Backend ready ($baseUrl)"

# ---- 3. Run tests ----
Write-Host "[3/4] Running API integration tests..." -ForegroundColor Cyan
$testArgs = @()
if ($Verbose) { $testArgs += "-Verbose" }
$testScript = Join-Path $ScriptDir "test_api_integration.ps1"
& $testScript -BaseUrl $baseUrl -TestRepo $TestRepo @testArgs
$testExitCode = $LASTEXITCODE

# ---- 4. Cleanup ----
if (-not $NoCleanup) {
    Write-Host "[4/4] Cleanup..." -ForegroundColor Cyan
    Stop-Process -Id $backendProcess.Id -Force -ErrorAction SilentlyContinue
    Write-Host "       Backend stopped"
} else {
    Write-Host "[4/4] Skipping cleanup (-NoCleanup)" -ForegroundColor Cyan
}

# ---- Result ----
if ($testExitCode -eq 0) {
    Write-Host "[PASS] All tests passed!" -ForegroundColor Green
} else {
    Write-Host "[FAIL] Some tests failed, see details above." -ForegroundColor Red
}
exit $testExitCode
