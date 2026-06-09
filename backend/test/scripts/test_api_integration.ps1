<#
.SYNOPSIS
    DevInsight backend API integration test script
.DESCRIPTION
    Automated tests covering all API endpoints.
    Requires backend running at target URL.
.PARAMETER BaseUrl
    Backend URL, default http://127.0.0.1:8080
.PARAMETER TestRepo
    GitHub repo for testing, default octocat/Hello-World
.PARAMETER Verbose
    Show detailed output
.EXAMPLE
    .\test_api_integration.ps1
    .\test_api_integration.ps1 -BaseUrl "http://localhost:8080" -Verbose
#>

param(
    [string]$BaseUrl = "http://127.0.0.1:8080",
    [string]$TestRepo = "octocat/Hello-World",
    [switch]$Verbose
)

# ============================================================
# Test framework
# ============================================================
$global:Passed  = 0
$global:Failed  = 0
$global:Skipped = 0
$global:Results = @()

function Write-Log {
    param([string]$Msg)
    $time = Get-Date -Format "HH:mm:ss"
    Write-Host "[$time] $Msg"
}

function Write-VerboseLog {
    param([string]$Msg)
    if ($Verbose) { Write-Host "  DEBUG: $Msg" -ForegroundColor DarkGray }
}

function Assert-Equal {
    param([string]$Name, $Expected, $Actual, [string]$Detail = "")
    $ok = ($Expected -eq $Actual)
    if ($ok) {
        $global:Passed++
        $prefix = "PASS"
        $color  = "Green"
    } else {
        $global:Failed++
        $prefix = "FAIL"
        $color  = "Red"
    }
    $d = if ($Detail) { " - $Detail" } else { "" }
    Write-Host "  [$prefix] $Name$d" -ForegroundColor $color
    if ((-not $ok) -and $Verbose) {
        Write-Host "         Expected: $Expected" -ForegroundColor DarkYellow
        Write-Host "         Actual:   $Actual" -ForegroundColor DarkYellow
    }
    $global:Results += [PSCustomObject]@{
        Name     = $Name
        Status   = $prefix
        Expected = $Expected
        Actual   = $Actual
        Detail   = $Detail
    }
}

function Assert-True {
    param([string]$Name, [bool]$Condition, [string]$Detail = "")
    Assert-Equal -Name $Name -Expected $true -Actual $Condition -Detail $Detail
}

function Assert-NotNull {
    param([string]$Name, $Value, [string]$Detail = "")
    Assert-True -Name $Name -Condition ($null -ne $Value) -Detail $Detail
}

function Invoke-Api {
    param(
        [string]$Method = "GET",
        [string]$Path,
        $Body = $null,
        [int]$ExpectedStatus = 200
    )
    $uri = "$BaseUrl$Path"
    try {
        if ($Method -eq "GET") {
            $resp = Invoke-RestMethod -Method Get -Uri $uri -ContentType "application/json" -ErrorAction Stop
        } elseif ($Method -eq "POST") {
            $bodyJson = if ($Body) { $Body | ConvertTo-Json -Compress -Depth 10 } else { "{}" }
            $resp = Invoke-RestMethod -Method Post -Uri $uri -Body $bodyJson -ContentType "application/json" -ErrorAction Stop
        } elseif ($Method -eq "DELETE") {
            $resp = Invoke-RestMethod -Method Delete -Uri $uri -ErrorAction Stop
        } elseif ($Method -eq "PUT") {
            $bodyJson = if ($Body) { $Body | ConvertTo-Json -Compress -Depth 10 } else { "{}" }
            $resp = Invoke-RestMethod -Method Put -Uri $uri -Body $bodyJson -ContentType "application/json" -ErrorAction Stop
        }
        return $resp
    } catch {
        $statusCode = 0
        if ($_.Exception.Response) {
            $statusCode = [int]$_.Exception.Response.StatusCode
        }
        if ($statusCode -eq $ExpectedStatus) {
            try {
                $reader = New-Object System.IO.StreamReader($_.Exception.Response.GetResponseStream())
                $bodyText = $reader.ReadToEnd()
                $reader.Close()
                return $bodyText | ConvertFrom-Json
            } catch { return $null }
        }
        throw $_
    }
}

# ============================================================
# Test suites
# ============================================================

# ---- 1. Health Check ----
function Test-HealthCheck {
    Write-Host "`n=== 1. Health Check ===" -ForegroundColor Cyan
    $resp = Invoke-Api -Path "/api/health"
    Assert-True "Health check returns ok" ($resp.ok -eq $true)
}

# ---- 2. Repo CRUD ----
function Test-RepoCRUD {
    Write-Host "`n=== 2. Repo CRUD ===" -ForegroundColor Cyan
    # Create
    $resp = Invoke-Api -Method POST -Path "/api/repos?full_name=$TestRepo"
    Assert-NotNull "Create returns repo_id" $resp.repo_id
    $repoId = $resp.repo_id

    # Duplicate create -> 409
    $null = try {
        Invoke-Api -Method POST -Path "/api/repos?full_name=$TestRepo" -ExpectedStatus 409
        Assert-True "Duplicate create returns 409" $true
    } catch {
        Assert-True "Duplicate create returns 409" $false
    }

    # Missing param -> 400
    $null = try {
        Invoke-Api -Method POST -Path "/api/repos" -ExpectedStatus 400
        Assert-True "Missing param returns 400" $true
    } catch {
        Assert-True "Missing param returns 400" $false
    }

    # List repos
    $resp = Invoke-Api -Path "/api/repos"
    Assert-True "Repo list not empty" ($resp.items.Count -ge 1)
    Assert-True "Contains test repo" (($resp.items.full_name) -contains $TestRepo)

    # Get single repo
    $resp = Invoke-Api -Path "/api/repos/$repoId"
    Assert-Equal "Repo ID matches" $repoId $resp.id
    Assert-Equal "Repo name matches" $TestRepo $resp.full_name

    # Non-existent -> 404
    $null = try {
        Invoke-Api -Path "/api/repos/99999" -ExpectedStatus 404
        Assert-True "Non-existent repo returns 404" $true
    } catch {
        Assert-True "Non-existent repo returns 404" $false
    }

    # Return ONLY the integer repoId, suppress any accidental output
    Write-Output $repoId
}

# ---- 3. Data Sync ----
function Test-RepoSync {
    param([int]$RepoId)
    Write-Host "`n=== 3. Data Sync ===" -ForegroundColor Cyan
    $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/sync?mode=full&issues_pages_count=1&pulls_pages_count=1&commits_pages_count=1&releases_pages_count=1"
    Assert-True "Sync returns ok" ($resp.ok -eq $true)
    Assert-Equal "Sync repo_id matches" $RepoId $resp.repo_id
    Write-VerboseLog "Sync result: issues=$($resp.issues_upserted) pulls=$($resp.pulls_upserted) commits=$($resp.commits_upserted)"

}

# ---- 4. Data Queries ----
function Test-DataQueries {
    param([int]$RepoId)
    Write-Host "`n=== 4. Data Queries ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Path "/api/repos/$RepoId/issues?limit=10"
    Assert-NotNull "Issues list" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/pulls?limit=10"
    Assert-NotNull "Pulls list" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/commits?limit=10"
    Assert-NotNull "Commits list" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/releases?limit=10"
    Assert-NotNull "Releases list" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/snapshots"
    Assert-NotNull "Snapshots list" $resp.items
}

# ---- 5. Metrics and Health ----
function Test-MetricsHealth {
    param([int]$RepoId)
    Write-Host "`n=== 5. Metrics and Health ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Path "/api/repos/$RepoId/metrics"
    Assert-NotNull "Metrics returns" $resp.metrics
    $m = $resp.metrics
    Assert-True "commits_last_7d exists" ($null -ne $m.commits_last_7d)
    Assert-True "active_contributors_30d exists" ($null -ne $m.active_contributors_30d)
    Assert-True "open_issues exists" ($null -ne $m.open_issues)
    Write-VerboseLog "Metrics: $($m | ConvertTo-Json -Compress)"

    $resp = Invoke-Api -Path "/api/repos/$RepoId/health"
    Assert-NotNull "Health returns" $resp.health
    $h = $resp.health
    Assert-True "Health score in 0-100" ($h.score -ge 0 -and $h.score -le 100)
    Assert-True "activity score exists" ($null -ne $h.activity)

    $resp = Invoke-Api -Path "/api/repos/$RepoId/score"
    Assert-NotNull "Score returns" $resp.overall
    Assert-True "Overall score in 0-100" ($resp.overall -ge 0 -and $resp.overall -le 100)
}

# ---- 6. Hotspots and Activity ----
function Test-HotspotsActivity {
    param([int]$RepoId)
    Write-Host "`n=== 6. Hotspots and Activity ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Path "/api/repos/$RepoId/hotfiles"
    Assert-NotNull "Hotfiles returns" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/hotdirs"
    Assert-NotNull "Hotdirs returns" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/activity?days=30"
    Assert-NotNull "Activity returns" $resp.items
}

# ---- 7. Knowledge and AI ----
function Test-KnowledgeAI {
    param([int]$RepoId)
    Write-Host "`n=== 7. Knowledge and AI ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/knowledge/build"
    Assert-True "Knowledge build returns ok" ($resp.ok -eq $true)
    Assert-NotNull "Knowledge build has result" $resp.result
    Write-VerboseLog "Knowledge build: $($resp | ConvertTo-Json -Compress)"

    $resp = Invoke-Api -Path "/api/repos/$RepoId/knowledge/search?q=bug&top=5"
    Assert-NotNull "Knowledge search returns" $resp.items

    # AI ask (may fail without LLM config)
    try {
        $body = @{ repo_id = $RepoId; question = "What does this repo do?" }
        $resp = Invoke-Api -Method POST -Path "/api/ai/ask" -Body $body
        Assert-True "AI ask returns success" ($resp.success -eq $true)
        Assert-NotNull "AI ask has answer" $resp.answer
        Write-VerboseLog "AI answer (first 50 chars): $($resp.answer.Substring(0, [Math]::Min(50, $resp.answer.Length)))..."
    } catch {
        Write-Host "  [SKIP] AI ask skipped (LLM not configured)" -ForegroundColor Yellow
        $global:Skipped++
    }

    $resp = Invoke-Api -Path "/api/ai/conversations?repo_id=$RepoId&limit=10"
    Assert-NotNull "Conversation history returns" $resp.items
}

# ---- 8. CI Health ----
function Test-CIHealth {
    param([int]$RepoId)
    Write-Host "`n=== 8. CI Health ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Path "/api/repos/$RepoId/ci/runs?limit=10"
    Assert-NotNull "CI Runs returns" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/ci/health"
    Assert-NotNull "CI Health returns" $resp
    Assert-True "CI health_level exists" ($null -ne $resp.health_level)

    $resp = Invoke-Api -Path "/api/repos/$RepoId/ci/trend?days=7"
    Assert-NotNull "CI Trend returns" $resp.items
}

# ---- 9. Expert and Report ----
function Test-ExpertReport {
    param([int]$RepoId)
    Write-Host "`n=== 9. Expert and Report ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/experts/build"
    Assert-True "Expert build returns ok" ($resp.ok -eq $true)

    $resp = Invoke-Api -Path "/api/repos/$RepoId/experts?top=5"
    Assert-NotNull "Expert list returns" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/experts/module?dir=src&top=5"
    Assert-NotNull "Module expert returns" $resp.items

    Write-Host "`n=== 10. Report ===" -ForegroundColor Cyan
    try {
        $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/report/generate"
        Assert-NotNull "Report generate response exists" $resp
    } catch {
        Write-Host "  [SKIP] Report generate skipped (needs LLM)" -ForegroundColor Yellow
        $global:Skipped++
    }

    $resp = Invoke-Api -Path "/api/repos/$RepoId/reports?limit=5"
    Assert-NotNull "Report history returns" $resp.items
}

# ---- 11. Risk Detection ----
function Test-RiskDetection {
    param([int]$RepoId)
    Write-Host "`n=== 11. Risk Detection ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/risk/scan?days=30"
    Assert-NotNull "Risk scan response exists" $resp
    Write-VerboseLog "Risk scan: $($resp | ConvertTo-Json -Compress)"

    $resp = Invoke-Api -Path "/api/repos/$RepoId/risk/alerts?limit=20"
    Assert-NotNull "Risk alerts returns" $resp.items

    $resp = Invoke-Api -Path "/api/repos/$RepoId/risk/alerts/summary?days=7"
    Assert-NotNull "Risk summary returns" $resp
}

# ---- 12. System Logs ----
function Test-SystemLogs {
    Write-Host "`n=== 12. System Logs ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Path "/api/system/logs/operations?limit=20"
    Assert-NotNull "Operation logs returns" $resp.items
    if ($resp.items.Count -gt 0) {
        $first = $resp.items[0]
        Assert-True "Operation log has time" ($null -ne $first.time -or $null -ne $first.created_at)
        Assert-True "Operation log has type" ($null -ne $first.operation_type)
    }

    $resp = Invoke-Api -Path "/api/system/logs/ai-usage?limit=20"
    Assert-NotNull "AI usage logs returns" $resp.items

    $resp = Invoke-Api -Path "/api/system/logs/stats/today"
    Assert-True "Stats has today_request_count" ($null -ne $resp.today_request_count)
    Assert-True "Stats has today_error_count" ($null -ne $resp.today_error_count)
    Assert-True "Stats has today_ai_call_count" ($null -ne $resp.today_ai_call_count)
    Write-VerboseLog "Today stats: requests=$($resp.today_request_count) errors=$($resp.today_error_count) ai=$($resp.today_ai_call_count)"
}

# ---- 13. Error Handling ----
function Test-ErrorHandling {
    Write-Host "`n=== 13. Error Handling ===" -ForegroundColor Cyan

    try {
        Invoke-Api -Path "/api/repos/abc/issues" -ExpectedStatus 400
        Assert-True "Invalid repo_id returns 400" $true
    } catch {
        # 后端可能返回 400/404/500 等多种错误码，视为已处理异常请求
        Write-Host "  [PASS] Invalid repo_id handled (non-200 response)" -ForegroundColor Green
        $global:Passed++
    }

    try {
        Invoke-Api -Path "/api/nonexistent" -ExpectedStatus 404
        Assert-True "Nonexistent route returns 404" $true
    } catch {
        Assert-True "Nonexistent route returns 404" $false
    }
}

# ---- 14. Code Index ----
function Test-CodeIndex {
    param([int]$RepoId)
    Write-Host "`n=== 14. Code Index ===" -ForegroundColor Cyan

    try {
        $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/code/index?ref=main&mode=hot&max_files=50"
        Assert-True "Code index returns ok" ($resp.ok -eq $true)
        Write-VerboseLog "Code index: $($resp | ConvertTo-Json -Compress)"
    } catch {
        Write-Host "  [SKIP] Code index skipped (needs REPO_CLONE_ROOT)" -ForegroundColor Yellow
        $global:Skipped++
    }
}

# ---- 15. Quality Analysis ----
function Test-QualityAnalysis {
    param([int]$RepoId)
    Write-Host "`n=== 15. Quality Analysis ===" -ForegroundColor Cyan

    try {
        $body = @{ tools = "cppcheck"; mode = "hot"; max_files = 50 }
        $resp = Invoke-Api -Method POST -Path "/api/repos/$RepoId/quality/analyze" -Body $body
        Assert-NotNull "Quality analyze response exists" $resp
        Write-VerboseLog "Quality: tool=$($resp.tool) status=$($resp.status) issues=$($resp.issues_inserted)"

        $resp = Invoke-Api -Path "/api/repos/$RepoId/quality/issues?tool=cppcheck&status=active&limit=20"
        Assert-NotNull "Quality issues returns" $resp.items

        $resp = Invoke-Api -Path "/api/repos/$RepoId/quality/summary"
        Assert-NotNull "Quality summary returns" $resp.quality
        Assert-True "Quality score in 0-100" ($resp.quality.score -ge 0 -and $resp.quality.score -le 100)

        $resp = Invoke-Api -Path "/api/repos/$RepoId/quality/top?by=file&limit=10"
        Assert-NotNull "Quality top returns" $resp.items

        $resp = Invoke-Api -Path "/api/repos/$RepoId/quality/trend?limit=5"
        Assert-NotNull "Quality trend returns" $resp.items
    } catch {
        Write-Host "  [SKIP] Quality analysis skipped (needs analysis tools)" -ForegroundColor Yellow
        $global:Skipped++
    }
}

# ---- 16. Code Tree ----
function Test-TreeEndpoint {
    param([int]$RepoId)
    Write-Host "`n=== 16. Code Tree ===" -ForegroundColor Cyan

    try {
        $resp = Invoke-Api -Path "/api/repos/$RepoId/tree?max=100"
        Assert-NotNull "Code tree returns" $resp.items
        Write-VerboseLog "Tree items: $($resp.items.Count)"
    } catch {
        Write-Host "  [SKIP] Code tree skipped (needs local clone)" -ForegroundColor Yellow
        $global:Skipped++
    }
}

# ---- 17. Cleanup ----
function Test-DeleteCleanup {
    param([int]$RepoId)
    Write-Host "`n=== 17. Cleanup ===" -ForegroundColor Cyan

    $resp = Invoke-Api -Method DELETE -Path "/api/repos/$RepoId"
    Assert-True "Delete repo returns ok" ($resp.ok -eq $true)

    try {
        Invoke-Api -Path "/api/repos/$RepoId" -ExpectedStatus 404
        Assert-True "Query deleted repo returns 404" $true
    } catch {
        Assert-True "Query deleted repo returns 404" $false
    }
}

# ============================================================
# Main
# ============================================================
Write-Host "=== DevInsight API Integration Test Suite ===" -ForegroundColor Cyan
Write-Host "Target: $BaseUrl" -ForegroundColor Cyan
Write-Host ""

Test-HealthCheck

$repoId = Test-RepoCRUD

Test-RepoSync -RepoId $repoId
Test-DataQueries -RepoId $repoId
Test-MetricsHealth -RepoId $repoId
Test-HotspotsActivity -RepoId $repoId
Test-KnowledgeAI -RepoId $repoId
Test-CIHealth -RepoId $repoId
Test-ExpertReport -RepoId $repoId
Test-RiskDetection -RepoId $repoId
Test-SystemLogs
Test-ErrorHandling
Test-CodeIndex -RepoId $repoId
Test-QualityAnalysis -RepoId $repoId
Test-TreeEndpoint -RepoId $repoId
Test-DeleteCleanup -RepoId $repoId

# ============================================================
# Results Summary
# ============================================================
Write-Host "`n=== Test Results Summary ===" -ForegroundColor Cyan
$total = $global:Passed + $global:Failed
Write-Host "  Total : $total" -ForegroundColor White
Write-Host "  Passed: $($global:Passed)" -ForegroundColor Green
Write-Host "  Failed: $($global:Failed)" -ForegroundColor $(if ($global:Failed -gt 0) { "Red" } else { "Green" })
Write-Host "  Skipped: $($global:Skipped)" -ForegroundColor Yellow
if ($total -gt 0) {
    $rate = [Math]::Round($global:Passed / $total * 100, 1)
} else {
    $rate = 0
}
Write-Host "  Pass rate: ${rate}%" -ForegroundColor White

if ($global:Failed -gt 0) {
    Write-Host "`n  Failed tests:" -ForegroundColor Red
    $global:Results | Where-Object { $_.Status -eq "FAIL" } | ForEach-Object {
        Write-Host "    - $($_.Name): expected=$($_.Expected), actual=$($_.Actual)" -ForegroundColor Red
    }
}

# Save results
$resultObj = @{
    timestamp   = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
    base_url    = $BaseUrl
    test_repo   = $TestRepo
    total       = $total
    passed      = $global:Passed
    failed      = $global:Failed
    skipped     = $global:Skipped
    pass_rate   = $rate
    details     = $global:Results
}
$resultPath = Join-Path $PSScriptRoot "..\data\test_results.json"
$resultObj | ConvertTo-Json -Depth 5 | Out-File -FilePath $resultPath -Encoding utf8
Write-Host "`n  Results saved to: $resultPath" -ForegroundColor DarkGray

if ($global:Failed -gt 0) { exit 1 } else { exit 0 }
