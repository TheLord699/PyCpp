param([switch]$NoGui, [switch]$Verbose)

$base = Join-Path $PSScriptRoot "PyCpp\examples"
$gui = @("--linker-flags", "-lraylib -lopengl32 -lgdi32 -lwinmm")
$script:results = @()
$logDir = Join-Path $PSScriptRoot "test_logs"
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }

function Run($title, $dir, $file, $extra = @(), $expectedExit = 0) {
    if (-not (Get-Command pycpp -ErrorAction SilentlyContinue)) {
        throw "pycpp was not found on PATH. Build the compiler and add its output directory to PATH."
    }
    Write-Host ""
    Write-Host "=== $title ===" -ForegroundColor Cyan
    Push-Location "$base\$dir"


    $args = @($file, "--compiler", "gcc") + $extra
    if ($Verbose) {
        $args += "-v"
        $logFile = Join-Path $logDir "$($title -replace '[^\w]', '_').log"
        pycpp @args 2>&1 | Tee-Object -FilePath $logFile
        $actualExit = $LASTEXITCODE
    } else {
        pycpp @args
        $actualExit = $LASTEXITCODE
    }


    # -1 means any non-zero exit; abort codes vary by platform.
    $passed = if ($expectedExit -eq -1) { $actualExit -ne 0 } else { $actualExit -eq $expectedExit }
    $expectedLabel = if ($expectedExit -eq -1) { "any non-zero exit" } else { "exit $expectedExit" }
    $status = if ($passed) { "PASS" } else { "FAIL" }
    Write-Host "[$status] expected $expectedLabel, got $actualExit" -ForegroundColor $(if ($passed) { "Green" } else { "Red" })
    $script:results += [pscustomobject]@{ Test = $title; Status = $status; ExpectedExit = $expectedExit; PycppExit = $actualExit }
    Pop-Location
}
Run "1. basics"          "simple"       "test.pycpp"
Run "2. oop"             "oop"          "main.pycpp"
Run "3. nesting"         "nesting"      "main.pycpp"
Run "4. memory"          "low_level"    "memory.pycpp"
Run "5. reference"       "low_level"    "reference.pycpp"
Run "6. pointer math"    "low_level"    "pointer_math.pycpp"
Run "7. defer"           "low_level"    "defer.pycpp"
Run "8. match"           "low_level"    "match.pycpp"
Run "9. assert (aborts)" "low_level"    "assert_test.pycpp" @() -1
Run "10. input"          "simple"       "input.pycpp"
Run "11. features"       "simple"       "features.pycpp"
Run "12. arithmetic"     "simple"       "arithmetic.pycpp"
Run "13. pointer chain"  "low_level"    "pointer_chain.pycpp"
Run "14. exceptions"     "simple"       "exceptions.pycpp"
Run "15. imports"        "imports"      "main.pycpp"
Run "16. module namespace regression" "module_regression" "main.pycpp"

if (-not $NoGui) {
    Run "17. raylib window"    "raylib"       "window.pycpp"    $gui
    Run "18. raylib dodge"     "raylib\dodge" "main.pycpp"      $gui
    Run "19. raylib game"      "raylib\game"  "main.pycpp"      $gui
    Run "20. raylib particles" "raylib"       "particles.pycpp" $gui
    Run "21. raylib snake"     "raylib"       "snake.pycpp"     $gui
}

Write-Host ""
Write-Host "=== SUMMARY ===" -ForegroundColor Green
$script:results | Format-Table -AutoSize
$failed = @($script:results | Where-Object { $_.Status -eq "FAIL" }).Count
if ($failed -gt 0) {
    Write-Host "$failed test(s) failed." -ForegroundColor Red
    exit 1
}
Write-Host "All tests passed (including expected non-zero exits)." -ForegroundColor Green
exit 0