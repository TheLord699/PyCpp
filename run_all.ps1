param(
    [switch]$NoGui,
    [switch]$Verbose,
    [string[]]$Test,      # e.g. -Test 1,3,9  or -Test basics,memory
    [switch]$List
)

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

# --- Test registry -------------------------------------------------------
# Each entry: id, name(s), action
$tests = @(
    @{ Id = 1;  Keys = @("basics", "simple", "test");        Title = "1. basics";          Run = { Run "1. basics"          "simple"       "test.pycpp" } }
    @{ Id = 2;  Keys = @("oop");                              Title = "2. oop";             Run = { Run "2. oop"             "oop"          "main.pycpp" } }
    @{ Id = 3;  Keys = @("nesting");                          Title = "3. nesting";         Run = { Run "3. nesting"         "nesting"      "main.pycpp" } }
    @{ Id = 4;  Keys = @("memory");                           Title = "4. memory";          Run = { Run "4. memory"          "low_level"    "memory.pycpp" } }
    @{ Id = 5;  Keys = @("reference");                        Title = "5. reference";       Run = { Run "5. reference"       "low_level"    "reference.pycpp" } }
    @{ Id = 6;  Keys = @("pointer_math", "pointer-math");     Title = "6. pointer math";    Run = { Run "6. pointer math"    "low_level"    "pointer_math.pycpp" } }
    @{ Id = 7;  Keys = @("defer");                            Title = "7. defer";           Run = { Run "7. defer"           "low_level"    "defer.pycpp" } }
    @{ Id = 8;  Keys = @("match");                            Title = "8. match";           Run = { Run "8. match"           "low_level"    "match.pycpp" } }
    @{ Id = 9;  Keys = @("assert", "assert-aborts");          Title = "9. assert (aborts)"; Run = { Run "9. assert (aborts)" "low_level"    "assert_test.pycpp" @() -1 } }
    @{ Id = 10; Keys = @("input");                            Title = "10. input";          Run = { Run "10. input"          "simple"       "input.pycpp" } }
    @{ Id = 11; Keys = @("features");                         Title = "11. features";       Run = { Run "11. features"       "simple"       "features.pycpp" } }
    @{ Id = 12; Keys = @("arithmetic");                       Title = "12. arithmetic";     Run = { Run "12. arithmetic"     "simple"       "arithmetic.pycpp" } }
    @{ Id = 13; Keys = @("pointer_chain", "pointer-chain");   Title = "13. pointer chain";  Run = { Run "13. pointer chain"  "low_level"    "pointer_chain.pycpp" } }
    @{ Id = 14; Keys = @("exceptions");                       Title = "14. exceptions";     Run = { Run "14. exceptions"     "simple"       "exceptions.pycpp" } }
    @{ Id = 15; Keys = @("imports");                          Title = "15. imports";        Run = { Run "15. imports"        "imports"      "main.pycpp" } }
    @{ Id = 16; Keys = @("module_regression", "module-regression", "module"); Title = "16. module namespace regression"; Run = { Run "16. module namespace regression" "module_regression" "main.pycpp" } }
    @{ Id = 17; Keys = @("stray_token", "stray-token", "error-stray");   Title = "17. error: stray token";        Run = { Run "17. error: stray token"        "errors" "stray_token.pycpp"        @() 1 } }
    @{ Id = 18; Keys = @("unknown_name", "unknown-name", "error-unknown-name"); Title = "18. error: unknown name";  Run = { Run "18. error: unknown name"       "errors" "unknown_name.pycpp"       @() 1 } }
    @{ Id = 19; Keys = @("unknown_type", "unknown-type", "error-unknown-type"); Title = "19. error: unknown type";  Run = { Run "19. error: unknown type"       "errors" "unknown_type.pycpp"       @() 1 } }
    @{ Id = 20; Keys = @("missing_colon", "missing-colon", "error-missing-colon"); Title = "20. error: missing colon"; Run = { Run "20. error: missing colon"      "errors" "missing_colon.pycpp"      @() 1 } }
    @{ Id = 21; Keys = @("unterminated_string", "unterminated-string", "error-unterminated-string"); Title = "21. error: unterminated string"; Run = { Run "21. error: unterminated string" "errors" "unterminated_string.pycpp" @() 1 } }
    @{ Id = 22; Keys = @("missing_module", "missing-module", "error-missing-module"); Title = "22. error: missing module"; Run = { Run "22. error: missing module"     "errors" "missing_module.pycpp"     @() 1 } }
    @{ Id = 23; Keys = @("raylib", "raylib-window", "window");        Title = "23. raylib window";    Run = { Run "23. raylib window"    "raylib"       "window.pycpp"    $gui }; Gui = $true }
    @{ Id = 24; Keys = @("dodge", "raylib-dodge");                     Title = "24. raylib dodge";     Run = { Run "24. raylib dodge"     "raylib\dodge" "main.pycpp"      $gui }; Gui = $true }
    @{ Id = 25; Keys = @("game", "raylib-game");                       Title = "25. raylib game";      Run = { Run "25. raylib game"      "raylib\game"  "main.pycpp"      $gui }; Gui = $true }
    @{ Id = 26; Keys = @("particles", "raylib-particles");             Title = "26. raylib particles"; Run = { Run "26. raylib particles" "raylib"       "particles.pycpp" $gui }; Gui = $true }
    @{ Id = 27; Keys = @("snake", "raylib-snake");                     Title = "27. raylib snake";     Run = { Run "27. raylib snake"     "raylib"       "snake.pycpp"     $gui }; Gui = $true }
)

# --- Resolve which tests to run -----------------------------------------
function Resolve-Tests {
    param([string[]]$Selectors, [bool]$IncludeGui)

    if (-not $Selectors -or $Selectors.Count -eq 0) {
        return $tests | Where-Object { $IncludeGui -or -not $_.Gui }
    }

    $selected = [System.Collections.Generic.List[object]]::new()
    foreach ($raw in $Selectors) {
        foreach ($piece in ($raw -split ',')) {
            $sel = $piece.Trim()
            if (-not $sel) { continue }

            $num = 0
            $match = $null
            if ([int]::TryParse($sel, [ref]$num)) {
                $match = $tests | Where-Object { $_.Id -eq $num }
            } else {
                $match = $tests | Where-Object { $_.Keys -contains $sel.ToLower() }
                if (-not $match) {
                    # partial/fuzzy match on keys or title
                    $match = $tests | Where-Object {
                        ($_.Keys | Where-Object { $_ -like "*$($sel.ToLower())*" }) -or
                        ($_.Title.ToLower() -like "*$($sel.ToLower())*")
                    }
                }
            }

            if (-not $match) {
                Write-Warning "No test matched selector '$sel'"
                continue
            }
            foreach ($m in @($match)) {
                if (-not $IncludeGui -and $m.Gui) {
                    Write-Warning "Skipping GUI test '$($m.Title)' (use without -NoGui to run it)"
                    continue
                }
                if ($selected -notcontains $m) { $selected.Add($m) | Out-Null }
            }
        }
    }
    return $selected
}

if ($List) {
    $tests | ForEach-Object {
        $guiTag = if ($_.Gui) { " [gui]" } else { "" }
        $keys = $_.Keys -join ", "
        Write-Host ("{0,2}. {1}{2}" -f $_.Id, $_.Title, $guiTag)
        Write-Host ("     keys: {0}" -f $keys) -ForegroundColor DarkGray
    }
    exit 0
}

$toRun = Resolve-Tests -Selectors $Test -IncludeGui (-not $NoGui)
if (-not $toRun -or @($toRun).Count -eq 0) {
    Write-Host "No tests to run." -ForegroundColor Yellow
    exit 0
}

Write-Host ("Running {0} test(s)..." -f @($toRun).Count) -ForegroundColor Cyan
foreach ($t in $toRun) {
    & $t.Run
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