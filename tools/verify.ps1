# CrossDim AI Control Verification Script
# Usage:
#   .\tools\verify.ps1                       # everything: build-check, launch, test, report
#   .\tools\verify.ps1 -NoLaunch             # test already-running instance
#   .\tools\verify.ps1 -NoKill               # don't kill CrossDim after testing
#   .\tools\verify.ps1 -Port 59999           # custom port (must match CROSSDIM_PORT env on launch)
# Exit code 0 = all passed, 1 = some failed.
#
# Output: machine-readable summary on stdout (JSON lines) + human table.

param(
    [int]$Port = 52317,
    [switch]$NoLaunch,
    [switch]$NoKill,
    [int]$ReadyTimeoutMs = 15000
)

$ErrorActionPreference = "Stop"
$base = "http://127.0.0.1:$Port"
$results = @()  # array of @{ test=..; pass=bool; detail=.. }

function Add-Result($name, $pass, $detail) {
    $script:results += [pscustomobject]@{ test = $name; pass = [bool]$pass; detail = $detail }
    if ($pass) { Write-Host ("[PASS] " + $name) -ForegroundColor Green }
    else       { Write-Host ("[FAIL] " + $name + "  ->  " + $detail) -ForegroundColor Red }
}

function Wait-Ready {
    $deadline = [DateTime]::Now.AddMilliseconds($ReadyTimeoutMs)
    while ([DateTime]::Now -lt $deadline) {
        try {
            $r = Invoke-WebRequest -Uri "$base/api/help" -UseBasicParsing -TimeoutSec 3
            if ($r.StatusCode -eq 200) { return $true }
        } catch { Start-Sleep -Milliseconds 300 }
    }
    return $false
}

function Invoke-Action($json) {
    try {
        $body = [System.Text.Encoding]::UTF8.GetBytes($json)
        $r = Invoke-WebRequest -Uri "$base/api/action" -Method POST -Body $body -ContentType "application/json" -UseBasicParsing -TimeoutSec 10
        return @{ status = $r.StatusCode; content = [string]$r.Content }
    } catch {
        return @{ status = 0; content = $_.Exception.Message }
    }
}

# ---- 0. Optionally launch ----
function Test-IsAdmin {
    return ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

$launched = $false
$isAdmin = Test-IsAdmin
if (-not $NoLaunch) {
    $exe = Join-Path (Split-Path $PSScriptRoot -Parent) "build\CrossDim.exe"
    if (-not (Test-Path $exe)) {
        Add-Result "exe-present" $false "missing $exe"
    } else {
        Add-Result "exe-present" $true $exe
        $proc = Get-Process CrossDim -ErrorAction SilentlyContinue
        if (-not $proc) {
            if ($isAdmin) {
                $psi = Start-Process -FilePath $exe -PassThru
                $launched = $true
                Add-Result "launch" $true "pid $($psi.Id) (admin)"
            } else {
                # Elevate via UAC prompt; returns when the elevated proc starts, which is async.
                Start-Process -FilePath $exe -Verb RunAs
                $launched = $true
                Add-Result "launch" $true "elevated via UAC (please confirm the prompt)"
            }
        } else {
            Add-Result "launch" $true "already running pid $($proc.Id)"
        }
    }
}

# ---- 1. Wait ready ----
if (Wait-Ready) {
    Add-Result "server-ready" $true "port $Port"
} else {
    Add-Result "server-ready" $false "no response on $base within ${ReadyTimeoutMs}ms"
    # don't proceed if server isn't up (unless we skip launch and user said NoKill etc.) - still report
    $results | ConvertTo-Json -Compress
    exit 1
}

# ---- 2. Endpoint smoke tests ----
try {
    $help = Invoke-WebRequest -Uri "$base/api/help" -UseBasicParsing -TimeoutSec 5
    $addAction = $help.Content.toString().IndexOf("describe_scene") -ge 0
    Add-Result "api/help" ($help.StatusCode -eq 200) "status=$($help.StatusCode) actionsListable=$addAction"
} catch {
    Add-Result "api/help" $false $_.Exception.Message
}

try {
    $st = Invoke-WebRequest -Uri "$base/api/state" -UseBasicParsing -TimeoutSec 5
    $hasCubes = $st.Content.toString().IndexOf('"cubes"') -ge 0
    Add-Result "api/state" (($st.StatusCode -eq 200) -and $hasCubes) "status=$($st.StatusCode) hasCubes=$hasCubes"
} catch {
    Add-Result "api/state" $false $_.Exception.Message
}

try {
    $r = Invoke-Action '{"action":"ping"}'
    $ok = ($r.status -eq 200) -and ($r.Content.toString().IndexOf('"ok":true') -ge 0)
    Add-Result "action/ping" $ok ("status=" + $r.status)
} catch {
    Add-Result "action/ping" $false $_.Exception.Message
}

try {
    $r = Invoke-Action '{"action":"describe_scene"}'
    $ok = ($r.status -eq 200) -and ($r.Content.toString().IndexOf('"scene"') -ge 0)
    Add-Result "action/describe_scene" $ok ("status=" + $r.status + " len=" + $r.content.Length)
} catch {
    Add-Result "action/describe_scene" $false $_.Exception.Message
}

try {
    $ev = Invoke-WebRequest -Uri "$base/api/events?since=0" -UseBasicParsing -TimeoutSec 5
    $hasEv = $ev.Content.toString().IndexOf('"events"') -ge 0
    Add-Result "api/events" (($ev.StatusCode -eq 200) -and $hasEv) "status=$($ev.StatusCode)"
} catch {
    Add-Result "api/events" $false $_.Exception.Message
}

try {
    $r = Invoke-Action '{"action":"get_log","count":10}'
    $ok = ($r.status -eq 200)
    Add-Result "action/get_log" $ok ("status=" + $r.status)
} catch {
    Add-Result "action/get_log" $false $_.Exception.Message
}

# Screenshot raw -> validate it's a real BMP (starts with "BM" and length >= 54)
try {
    $resp = Invoke-WebRequest -Uri "$base/api/screenshot?raw=1&scale=160" -UseBasicParsing -TimeoutSec 10
    $bytes = $resp.Content  # byte[] for binary responses in PowerShell 7
    $isBmp = ($bytes -is [byte[]]) -and ($bytes.Length -ge 54) -and
             ($bytes[0] -eq 0x42) -and ($bytes[1] -eq 0x4D)  # "BM"
    Add-Result "api/screenshot(raw)" $isBmp ("bytes=" + $(if ($bytes -is [byte[]]) { $bytes.Length } else { "?" }) + " isBmp=$isBmp")
} catch {
    Add-Result "api/screenshot(raw)" $false $_.Exception.Message
}

# base64 JSON screenshot
try {
    $r = Invoke-WebRequest -Uri "$base/api/screenshot?scale=120" -UseBasicParsing -TimeoutSec 10
    $ok = ($r.StatusCode -eq 200) -and ($r.Content.toString().IndexOf('"data"') -ge 0)
    Add-Result "api/screenshot(base64)" $ok "status=$($r.StatusCode)"
} catch {
    Add-Result "api/screenshot(base64)" $false $_.Exception.Message
}

# ---- 3. Cleanup (unless NoKill): graceful quit via API ----
if (-not $NoKill -and $launched) {
    try {
        $null = Invoke-Action '{"action":"quit"}'
        Start-Sleep -Milliseconds 800
        Add-Result "cleanup" $true "graceful quit requested"
    } catch {
        Stop-Process -Name CrossDim -Force -ErrorAction SilentlyContinue
        Add-Result "cleanup" $true "forced stop"
    }
}

# ---- 4. Summary ----
$passCount = ($results | Where-Object pass).Count
$failCount = $results.Count - $passCount
Write-Host ""
Write-Host ("Summary: $passCount passed, $failCount failed") -ForegroundColor $(if ($failCount -eq 0) { "Green" } else { "Yellow" })
$results | ConvertTo-Json -Compress
exit $(if ($failCount -eq 0) { 0 } else { 1 })
