$ErrorActionPreference='Stop'
Set-Location -LiteralPath (Split-Path -Parent $PSScriptRoot)
function Bench([string[]]$Arguments) {
    & wsl -d Ubuntu-26.04 -- python3 ./validation/target_run.py @Arguments
    if ($LASTEXITCODE -ne 0) { throw 'Guard regression failed' }
}
function Remote([string]$Code) {
    $r=$Code | ssh -T -i 'C:/Users/USER/.ssh/id_ed25519_mister' -o BatchMode=yes -o ConnectTimeout=8 root@192.168.15.14 'python3 -'
    if ($LASTEXITCODE -ne 0) { throw 'Remote guard check failed' }
    ($r -join "`n")
}
$root='/media/fat/emulationstation/codex-performance-20261113'
$before=Remote "from pathlib import Path;print(dict(x.split() for x in Path('/proc/vmstat').read_text().splitlines()).get('oom_kill','0'))"
try {
    foreach ($n in @(1,2)) {
        $label="guard-covers-v3-$n"
        Bench @('run',$label,'--binary-label','stage3-last-hypotheses','--cover-fixture','--script','./validation/guard-smoke.txt','--upload-neon','1','--opaque-fade','1','--texture-threads','2','--trace-off')
        $timer=[Diagnostics.Stopwatch]::StartNew();$finished=$false
        while ($timer.Elapsed.TotalSeconds -lt 120) {
            $state=Remote "from pathlib import Path;p=Path('$root/$label');print(not Path('/proc/'+(p/'pid').read_text().strip()).exists() and (p/'memory.json').exists())"
            if ($state.Trim() -eq 'True') { $finished=$true;break }
            Start-Sleep -Seconds 2
        }
        if (-not $finished) { throw 'Guard smoke timeout' }
        Bench @('collect',$label)
        $path="./validation/target-results/$label"
        if (Test-Path -LiteralPath "$path/framebuffer-collision.json") { throw 'Framebuffer owners overlapped' }
        $memory=Get-Content -Raw -LiteralPath "$path/memory.json" | ConvertFrom-Json
        if ((($memory.framebuffer_owners | Measure-Object -Maximum).Maximum) -ne 1) { throw 'Owner guard missing or invalid' }
        Write-Output "GUARD PASS ${label}: one frontend"
        # Immediately start the next test after restore: this exposed the old race.
        Bench @('restore')
    }
    $after=Remote "from pathlib import Path;print(dict(x.split() for x in Path('/proc/vmstat').read_text().splitlines()).get('oom_kill','0'))"
    if ($before.Trim() -ne $after.Trim()) { throw 'OOM counter changed' }
    Write-Output 'GUARD REGRESSION PASSED: two restore/start cycles, no new OOM'
} finally { Bench @('restore') }
