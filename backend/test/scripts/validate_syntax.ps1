# Validate PowerShell script syntax
$scripts = @(
    "D:\Users\rty\Soft_ware Project\se-metrics-platform\backend\test\scripts\run_tests.ps1",
    "D:\Users\rty\Soft_ware Project\se-metrics-platform\backend\test\scripts\test_api_integration.ps1"
)

$allOk = $true
foreach ($script in $scripts) {
    $errors = $null
    $null = [System.Management.Automation.Language.Parser]::ParseFile($script, [ref]$null, [ref]$errors)
    if ($errors.Count -eq 0) {
        Write-Host "OK: $script" -ForegroundColor Green
    } else {
        Write-Host "ERRORS in $script :" -ForegroundColor Red
        $allOk = $false
        foreach ($e in $errors) {
            Write-Host "  Line $($e.Extent.StartLine): $($e.Message)" -ForegroundColor Red
        }
    }
}

if ($allOk) {
    Write-Host "All scripts syntax OK!" -ForegroundColor Green
} else {
    Write-Host "Some scripts have syntax errors!" -ForegroundColor Red
}
